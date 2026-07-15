#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "xrobot/runtime/service.hpp"
#include "xrobot/transport/can/link.hpp"
#include "xrobot/transport/can/reliable_path.hpp"

namespace xrobot::transport::can {

namespace service_detail {

inline void WriteU32(std::uint32_t value, std::span<std::byte, 4> output) noexcept {
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<std::byte>(value >> (index * 8U));
  }
}

inline std::uint32_t ReadU32(std::span<const std::byte, 4> input) noexcept {
  std::uint32_t value{};
  for (std::size_t index = 0; index < input.size(); ++index) {
    value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
  }
  return value;
}

}  // namespace service_detail

struct ServiceClientTransportStats {
  std::uint32_t accepted{};
  std::uint32_t completed{};
  std::uint32_t rejected{};
  std::uint32_t decode_failures{};
  std::uint32_t send_failures{};
};

template <xrobot::runtime::ServiceType Service>
class CanServiceClient final : public xrobot::runtime::ServiceEndpoint<Service> {
 public:
  using Request = xrobot::runtime::ServiceRequest<Service>;
  using Response = xrobot::runtime::ServiceResponse<Service>;
  using Completion = xrobot::runtime::ServiceCompletion<Service>;
  static constexpr std::size_t kRequestSize =
      xrobot::runtime::TypeSupport<Request>::descriptor().max_serialized_size;
  static constexpr std::size_t kResponseSize =
      xrobot::runtime::TypeSupport<Response>::descriptor().max_serialized_size;

  constexpr CanServiceClient(std::uint16_t route_id, CanPriority priority,
                             CanFrameWriter writer, CanClockReader clock,
                             std::uint64_t retry_timeout_ns = 5'000'000,
                             std::uint8_t maximum_retries = 2) noexcept
      : route_id_(route_id),
        priority_(priority),
        writer_(writer),
        clock_(clock),
        retry_timeout_ns_(retry_timeout_ns),
        maximum_retries_(maximum_retries) {}

  Status CallAsync(const Request& request, Completion completion,
                   void* completion_state,
                   const xrobot::runtime::ExecutionContext& caller) noexcept override {
    if (pending_ || completion == nullptr || clock_.read == nullptr) {
      ++stats_.rejected;
      return pending_ ? Status::kCapacityExceeded : Status::kInvalidArgument;
    }
    ++next_request_id_;
    if (next_request_id_ == 0) {
      ++next_request_id_;
    }
    service_detail::WriteU32(
        next_request_id_, std::span<std::byte>(request_payload_).first<4>());
    std::size_t written{};
    const auto encode_status = xrobot::runtime::TypeSupport<Request>::Encode(
        request, std::span<std::byte>(request_payload_).subspan(4), written);
    if (encode_status != Status::kOk || written != kRequestSize) {
      ++stats_.rejected;
      return encode_status == Status::kOk ? Status::kInternal : encode_status;
    }

    sequence_ = next_sequence_;
    next_sequence_ = static_cast<std::uint8_t>((next_sequence_ + 1U) & 0x0fU);
    completion_ = completion;
    completion_state_ = completion_state;
    call_info_ = {next_request_id_};
    pending_ = true;
    const auto begin_status = request_sender_.Begin(
        route_id_, priority_, sequence_, request_payload_, clock_.NowNs(),
        retry_timeout_ns_, maximum_retries_);
    if (begin_status != Status::kOk) {
      ClearPending();
      ++stats_.rejected;
      return begin_status;
    }
    const auto send_status = PumpRequest(caller);
    if (send_status != Status::kOk) {
      ClearPending();
      ++stats_.send_failures;
      return send_status;
    }
    ++stats_.accepted;
    return Status::kOk;
  }

  Status Accept(const CanFrame& frame, std::uint64_t,
                const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (frame.size == 0 ||
        GetFrameKind(frame.data[0]) != FrameKind::kReliable) {
      return Status::kInvalidArgument;
    }
    if (GetReliableSubtype(frame.data[0]) == ReliableSubtype::kAck) {
      return request_sender_.HandleAck(frame);
    }

    ReassembledMessage message;
    CanFrame acknowledgement;
    const auto status =
        response_receiver_.Accept(frame, message, acknowledgement);
    if (acknowledgement.size != 0) {
      const auto ack_status = writer_.Send(acknowledgement, caller);
      if (ack_status != Status::kOk) {
        ++stats_.send_failures;
        return ack_status;
      }
    }
    if (status != Status::kOk) {
      return status;
    }
    if (!pending_ || message.sequence != sequence_ ||
        message.payload.size() != kResponseSize + 5U ||
        service_detail::ReadU32(message.payload.subspan<1, 4>()) !=
            call_info_.request_id) {
      ++stats_.decode_failures;
      return Status::kTypeMismatch;
    }
    const auto status_value =
        std::to_integer<std::uint8_t>(message.payload[0]);
    if (status_value > static_cast<std::uint8_t>(Status::kInternal)) {
      ++stats_.decode_failures;
      return Status::kInvalidArgument;
    }
    Response response;
    const auto decode_status = xrobot::runtime::TypeSupport<Response>::Decode(
        message.payload.subspan(5), response);
    if (decode_status != Status::kOk) {
      ++stats_.decode_failures;
      return decode_status;
    }
    const auto completion = completion_;
    auto* const completion_state = completion_state_;
    const auto info = call_info_;
    ClearPending();
    ++stats_.completed;
    completion(completion_state, static_cast<Status>(status_value), response,
               info, caller);
    return Status::kOk;
  }

  Status Poll(std::uint64_t now_ns,
              const xrobot::runtime::ExecutionContext& caller) noexcept {
    const auto status = request_sender_.Poll(now_ns);
    if (status == Status::kOk) {
      return PumpRequest(caller);
    }
    if (status == Status::kTimeout && pending_) {
      const auto completion = completion_;
      auto* const completion_state = completion_state_;
      const auto info = call_info_;
      ClearPending();
      completion(completion_state, Status::kTimeout, Response{}, info, caller);
    }
    return status;
  }

  xrobot::runtime::ServiceClient<Service> client() noexcept {
    return xrobot::runtime::ServiceClient<Service>(*this);
  }
  const ServiceClientTransportStats& stats() const noexcept { return stats_; }

 private:
  Status PumpRequest(
      const xrobot::runtime::ExecutionContext& caller) noexcept {
    CanFrame frame;
    while (request_sender_.NextFrame(frame) == Status::kOk) {
      const auto status = writer_.Send(frame, caller);
      if (status != Status::kOk) {
        return status;
      }
    }
    return Status::kOk;
  }

  void ClearPending() noexcept {
    pending_ = false;
    completion_ = nullptr;
    completion_state_ = nullptr;
  }

  std::uint16_t route_id_{};
  CanPriority priority_{CanPriority::kBackground};
  CanFrameWriter writer_;
  CanClockReader clock_;
  std::uint64_t retry_timeout_ns_{};
  std::uint8_t maximum_retries_{};
  std::uint8_t sequence_{};
  std::uint8_t next_sequence_{};
  std::uint32_t next_request_id_{};
  std::array<std::byte, kRequestSize + 4U> request_payload_{};
  ReliableSender<kRequestSize + 4U> request_sender_;
  ReliableReceiver<kResponseSize + 5U> response_receiver_;
  Completion completion_{};
  void* completion_state_{};
  xrobot::runtime::ServiceCallInfo call_info_{};
  bool pending_{};
  ServiceClientTransportStats stats_{};
};

struct ServiceServerTransportStats {
  std::uint32_t requests{};
  std::uint32_t responses{};
  std::uint32_t rejected{};
  std::uint32_t decode_failures{};
  std::uint32_t send_failures{};
};

template <xrobot::runtime::ServiceType Service>
class CanServiceServer {
 public:
  using Request = xrobot::runtime::ServiceRequest<Service>;
  using Response = xrobot::runtime::ServiceResponse<Service>;
  static constexpr std::size_t kRequestSize =
      xrobot::runtime::TypeSupport<Request>::descriptor().max_serialized_size;
  static constexpr std::size_t kResponseSize =
      xrobot::runtime::TypeSupport<Response>::descriptor().max_serialized_size;

  constexpr CanServiceServer(
      std::uint16_t route_id, CanPriority priority,
      xrobot::runtime::ServiceClient<Service> local_service,
      CanFrameWriter writer, CanClockReader clock,
      std::uint64_t retry_timeout_ns = 5'000'000,
      std::uint8_t maximum_retries = 2) noexcept
      : route_id_(route_id),
        priority_(priority),
        local_service_(local_service),
        writer_(writer),
        clock_(clock),
        retry_timeout_ns_(retry_timeout_ns),
        maximum_retries_(maximum_retries) {}

  Status Accept(const CanFrame& frame, std::uint64_t,
                const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (frame.size == 0 ||
        GetFrameKind(frame.data[0]) != FrameKind::kReliable) {
      return Status::kInvalidArgument;
    }
    if (GetReliableSubtype(frame.data[0]) == ReliableSubtype::kAck) {
      return response_sender_.HandleAck(frame);
    }
    ReassembledMessage message;
    CanFrame acknowledgement;
    const auto status =
        request_receiver_.Accept(frame, message, acknowledgement);
    if (acknowledgement.size != 0) {
      const auto ack_status = writer_.Send(acknowledgement, caller);
      if (ack_status != Status::kOk) {
        ++stats_.send_failures;
        return ack_status;
      }
    }
    if (status != Status::kOk) {
      return status;
    }
    if (active_ || ResponseBusy()) {
      ++stats_.rejected;
      return Status::kCapacityExceeded;
    }
    if (message.payload.size() != kRequestSize + 4U) {
      ++stats_.decode_failures;
      return Status::kTypeMismatch;
    }
    active_request_id_ =
        service_detail::ReadU32(message.payload.first<4>());
    if (active_request_id_ == 0) {
      ++stats_.decode_failures;
      return Status::kInvalidArgument;
    }
    Request request;
    const auto decode_status = xrobot::runtime::TypeSupport<Request>::Decode(
        message.payload.subspan(4), request);
    if (decode_status != Status::kOk) {
      ++stats_.decode_failures;
      return decode_status;
    }
    active_sequence_ = message.sequence;
    active_ = true;
    const auto call_status = local_service_.CallAsync(
        request, CompleteThunk, this, caller);
    if (call_status != Status::kOk) {
      active_ = false;
      ++stats_.rejected;
      return SendResponse(call_status, Response{}, caller);
    }
    ++stats_.requests;
    return Status::kOk;
  }

  Status Poll(std::uint64_t now_ns,
              const xrobot::runtime::ExecutionContext& caller) noexcept {
    const auto status = response_sender_.Poll(now_ns);
    return status == Status::kOk ? PumpResponse(caller) : status;
  }

  const ServiceServerTransportStats& stats() const noexcept { return stats_; }

 private:
  static void CompleteThunk(
      void* state, Status status, const Response& response,
      const xrobot::runtime::ServiceCallInfo&,
      const xrobot::runtime::ExecutionContext& context) noexcept {
    auto& self = *static_cast<CanServiceServer*>(state);
    self.active_ = false;
    if (self.SendResponse(status, response, context) != Status::kOk) {
      ++self.stats_.send_failures;
    }
  }

  Status SendResponse(
      Status status, const Response& response,
      const xrobot::runtime::ExecutionContext& context) noexcept {
    response_payload_[0] = static_cast<std::byte>(status);
    service_detail::WriteU32(
        active_request_id_, std::span<std::byte>(response_payload_).subspan<1, 4>());
    std::size_t written{};
    const auto encode_status = xrobot::runtime::TypeSupport<Response>::Encode(
        response, std::span<std::byte>(response_payload_).subspan(5), written);
    if (encode_status != Status::kOk || written != kResponseSize) {
      return encode_status == Status::kOk ? Status::kInternal : encode_status;
    }
    const auto begin_status = response_sender_.Begin(
        route_id_, priority_, active_sequence_, response_payload_, clock_.NowNs(),
        retry_timeout_ns_, maximum_retries_);
    if (begin_status != Status::kOk) {
      return begin_status;
    }
    const auto send_status = PumpResponse(context);
    if (send_status == Status::kOk) {
      ++stats_.responses;
    }
    return send_status;
  }

  Status PumpResponse(
      const xrobot::runtime::ExecutionContext& caller) noexcept {
    CanFrame frame;
    while (response_sender_.NextFrame(frame) == Status::kOk) {
      const auto status = writer_.Send(frame, caller);
      if (status != Status::kOk) {
        return status;
      }
    }
    return Status::kOk;
  }

  bool ResponseBusy() const noexcept {
    return response_sender_.state() == ReliableSenderState::kSending ||
           response_sender_.state() == ReliableSenderState::kWaitingForAck;
  }

  std::uint16_t route_id_{};
  CanPriority priority_{CanPriority::kBackground};
  xrobot::runtime::ServiceClient<Service> local_service_;
  CanFrameWriter writer_;
  CanClockReader clock_;
  std::uint64_t retry_timeout_ns_{};
  std::uint8_t maximum_retries_{};
  std::uint8_t active_sequence_{};
  std::uint32_t active_request_id_{};
  std::array<std::byte, kResponseSize + 5U> response_payload_{};
  ReliableReceiver<kRequestSize + 4U> request_receiver_;
  ReliableSender<kResponseSize + 5U> response_sender_;
  bool active_{};
  ServiceServerTransportStats stats_{};
};

}  // namespace xrobot::transport::can
