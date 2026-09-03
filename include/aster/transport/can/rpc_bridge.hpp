#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/rpc.hpp"
#include "aster/transport/can/link.hpp"
#include "aster/transport/can/reliable_path.hpp"

namespace aster::transport::can {

namespace rpc_detail {

inline void WriteU32(std::uint32_t value, std::span<std::byte, 4> output) noexcept {
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<std::byte>(value >> (index * 8U));
  }
}

inline std::uint32_t ReadU32(std::span<const std::byte, 4> input) noexcept {
  std::uint32_t value{};
  for (std::size_t index = 0; index < input.size(); ++index) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index]))
             << (index * 8U);
  }
  return value;
}

inline void WriteU64(std::uint64_t value, std::span<std::byte, 8> output) noexcept {
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<std::byte>(value >> (index * 8U));
  }
}

inline std::uint64_t ReadU64(std::span<const std::byte, 8> input) noexcept {
  std::uint64_t value{};
  for (std::size_t index = 0; index < input.size(); ++index) {
    value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(input[index]))
             << (index * 8U);
  }
  return value;
}

inline bool EncodeStatus(Status status, std::byte& encoded) noexcept {
  switch (status) {
    case Status::kOk:
      encoded = std::byte{0};
      return true;
    case Status::kInvalidArgument:
      encoded = std::byte{1};
      return true;
    case Status::kInvalidState:
      encoded = std::byte{2};
      return true;
    case Status::kCapacityExceeded:
      encoded = std::byte{3};
      return true;
    case Status::kUnavailable:
      encoded = std::byte{4};
      return true;
    case Status::kTimeout:
      encoded = std::byte{5};
      return true;
    case Status::kCancelled:
      encoded = std::byte{6};
      return true;
    case Status::kTypeMismatch:
      encoded = std::byte{7};
      return true;
    case Status::kNotFound:
      encoded = std::byte{8};
      return true;
    case Status::kAlreadyExists:
      encoded = std::byte{9};
      return true;
    case Status::kVersionMismatch:
      encoded = std::byte{10};
      return true;
    case Status::kProtocolError:
      encoded = std::byte{12};
      return true;
    case Status::kInternal:
      encoded = std::byte{11};
      return true;
  }
  encoded = {};
  return false;
}

inline bool DecodeStatus(std::byte encoded, Status& status) noexcept {
  switch (std::to_integer<std::uint8_t>(encoded)) {
    case 0:
      status = Status::kOk;
      return true;
    case 1:
      status = Status::kInvalidArgument;
      return true;
    case 2:
      status = Status::kInvalidState;
      return true;
    case 3:
      status = Status::kCapacityExceeded;
      return true;
    case 4:
      status = Status::kUnavailable;
      return true;
    case 5:
      status = Status::kTimeout;
      return true;
    case 6:
      status = Status::kCancelled;
      return true;
    case 7:
      status = Status::kTypeMismatch;
      return true;
    case 8:
      status = Status::kNotFound;
      return true;
    case 9:
      status = Status::kAlreadyExists;
      return true;
    case 10:
      status = Status::kVersionMismatch;
      return true;
    case 11:
      status = Status::kInternal;
      return true;
    case 12:
      status = Status::kProtocolError;
      return true;
    default:
      status = Status::kInternal;
      return false;
  }
}

inline bool IsForRoute(const CanFrame& frame, std::uint16_t route_id) noexcept {
  const auto decoded = CanArbitrationId::Decode(frame.arbitration_id);
  return decoded.has_value() && decoded->route_id == route_id;
}

inline bool SenderBusy(ReliableSenderState state) noexcept {
  return state == ReliableSenderState::kSending || state == ReliableSenderState::kWaitingForAck;
}

}  // namespace rpc_detail

struct RpcClientTransportStats {
  std::uint32_t accepted{};
  std::uint32_t completed{};
  std::uint32_t rejected{};
  std::uint32_t timeouts{};
  std::uint32_t decode_failures{};
  std::uint32_t send_failures{};
};

template <ServiceType Service>
class CanRpcClient final : public RpcBackend {
 public:
  using Request = typename ServiceTypeSupport<Service>::Request;
  using Response = typename ServiceTypeSupport<Service>::Response;
  static constexpr std::size_t kRequestSize =
      TypeSupport<Request>::descriptor().max_serialized_size;
  static constexpr std::size_t kResponseSize =
      TypeSupport<Response>::descriptor().max_serialized_size;
  static constexpr std::size_t kRequestHeaderSize = 12;
  static constexpr std::size_t kResponseHeaderSize = 5;
  static constexpr std::size_t kRequestPayloadSize = kRequestHeaderSize + kRequestSize;
  static constexpr std::size_t kResponsePayloadSize = kResponseHeaderSize + kResponseSize;

  static_assert(kRequestPayloadSize <= 96, "CAN RPC request exceeds reliable CAN capacity");
  static_assert(kResponsePayloadSize <= 96, "CAN RPC response exceeds reliable CAN capacity");

  constexpr CanRpcClient(std::uint16_t route_id, CanPriority priority, CanFrameWriter writer,
                         CanClockReader clock, std::uint64_t retry_timeout_ns = 5'000'000,
                         std::uint8_t maximum_retries = 2) noexcept
      : route_id_(route_id),
        priority_(priority),
        writer_(writer),
        clock_(clock),
        retry_timeout_ns_(retry_timeout_ns),
        maximum_retries_(maximum_retries) {}

  Status RegisterClient(const ServiceDescriptor& descriptor) noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (!SameService(descriptor, Descriptor())) {
      return Status::kTypeMismatch;
    }
    registered_ = true;
    return Status::kOk;
  }

  Status RegisterServer(const ServiceDescriptor&, RawRpcHandler, void*) noexcept override {
    return Status::kInvalidState;
  }

  Status CallAsync(const ServiceDescriptor& descriptor, std::span<const std::byte> request,
                   std::uint64_t deadline_ns, RawRpcCompletion completion, void* completion_state,
                   const ExecutionContext& caller) noexcept override {
    if (!sealed_) {
      return Reject(Status::kInvalidState);
    }
    if (!SameService(descriptor, Descriptor()) || request.size() > kRequestSize) {
      return Reject(Status::kTypeMismatch);
    }
    if (completion == nullptr || clock_.read == nullptr) {
      return Reject(Status::kInvalidArgument);
    }
    const auto now_ns = clock_.NowNs();
    if (deadline_ns != 0 && now_ns >= deadline_ns) {
      ++stats_.timeouts;
      return Reject(Status::kTimeout);
    }
    if (pending_ || rpc_detail::SenderBusy(request_sender_.state())) {
      return Reject(Status::kCapacityExceeded);
    }

    ++next_request_id_;
    if (next_request_id_ == 0) {
      ++next_request_id_;
    }
    rpc_detail::WriteU32(next_request_id_, std::span<std::byte>(request_payload_).first<4>());
    rpc_detail::WriteU64(deadline_ns, std::span<std::byte>(request_payload_).subspan<4, 8>());
    std::copy(request.begin(), request.end(),
              request_payload_.begin() + static_cast<std::ptrdiff_t>(kRequestHeaderSize));

    sequence_ = next_sequence_;
    next_sequence_ = static_cast<std::uint8_t>((next_sequence_ + 1U) & 0x0fU);
    completion_ = completion;
    completion_state_ = completion_state;
    call_info_ = {next_request_id_, deadline_ns};
    pending_ = true;
    const auto payload =
        std::span<const std::byte>(request_payload_).first(kRequestHeaderSize + request.size());
    const auto begin_status = request_sender_.Begin(route_id_, priority_, sequence_, payload,
                                                    now_ns, retry_timeout_ns_, maximum_retries_);
    if (!IsOk(begin_status)) {
      ClearPending();
      return Reject(begin_status);
    }
    const auto send_status = PumpRequest(caller);
    if (!IsOk(send_status)) {
      ClearPending();
      request_sender_ = {};
      ++stats_.send_failures;
      return send_status;
    }
    ++stats_.accepted;
    return Status::kOk;
  }

  Status Seal() noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (!registered_ || writer_.write == nullptr || clock_.read == nullptr ||
        retry_timeout_ns_ == 0) {
      return Status::kUnavailable;
    }
    sealed_ = true;
    return Status::kOk;
  }

  [[nodiscard]] bool sealed() const noexcept override { return sealed_; }

  Status Accept(const CanFrame& frame, std::uint64_t, const ExecutionContext& caller) noexcept {
    if (!sealed_) {
      return Status::kInvalidState;
    }
    if (frame.size == 0 || !rpc_detail::IsForRoute(frame, route_id_) ||
        GetFrameKind(frame.data[0]) != FrameKind::kReliable) {
      return Status::kInvalidArgument;
    }
    if (GetReliableSubtype(frame.data[0]) == ReliableSubtype::kAck) {
      return request_sender_.HandleAck(frame);
    }

    ReassembledMessage message;
    CanFrame acknowledgement;
    const auto receive_status = response_receiver_.Accept(frame, message, acknowledgement);
    if (acknowledgement.size != 0) {
      const auto ack_status = writer_.Send(acknowledgement, caller);
      if (!IsOk(ack_status)) {
        ++stats_.send_failures;
        return ack_status;
      }
    }
    if (!IsOk(receive_status)) {
      return receive_status;
    }
    if (!pending_) {
      return Status::kUnavailable;
    }
    if (message.sequence != sequence_ || message.payload.size() < kResponseHeaderSize ||
        rpc_detail::ReadU32(message.payload.subspan<1, 4>()) != call_info_.request_id) {
      ++stats_.decode_failures;
      return Status::kTypeMismatch;
    }

    Status response_status{};
    if (!rpc_detail::DecodeStatus(message.payload[0], response_status) ||
        (IsOk(response_status) && message.payload.size() - kResponseHeaderSize > kResponseSize) ||
        (!IsOk(response_status) && message.payload.size() != kResponseHeaderSize)) {
      ++stats_.decode_failures;
      return Status::kInvalidArgument;
    }
    const auto completion = completion_;
    auto* const completion_state = completion_state_;
    const auto info = call_info_;
    const auto response = message.payload.subspan(kResponseHeaderSize);
    ClearPending();
    ++stats_.completed;
    completion(completion_state, response_status, response, info, caller);
    return Status::kOk;
  }

  Status Poll(std::uint64_t now_ns, const ExecutionContext& caller) noexcept {
    if (!sealed_) {
      return Status::kInvalidState;
    }
    if (!pending_) {
      return Status::kUnavailable;
    }
    if (call_info_.deadline_ns != 0 && now_ns >= call_info_.deadline_ns) {
      FinishTimeout(caller);
      return Status::kTimeout;
    }
    const auto poll_status = request_sender_.Poll(now_ns);
    if (IsOk(poll_status)) {
      return PumpRequest(caller);
    }
    if (poll_status == Status::kTimeout) {
      FinishTimeout(caller);
    }
    return poll_status;
  }

  [[nodiscard]] const RpcClientTransportStats& stats() const noexcept { return stats_; }
  [[nodiscard]] const ReliableSenderStats& reliable_stats() const noexcept {
    return request_sender_.stats();
  }

 private:
  static constexpr ServiceDescriptor Descriptor() noexcept {
    return ServiceTypeSupport<Service>::descriptor();
  }

  Status Reject(Status status) noexcept {
    ++stats_.rejected;
    return status;
  }

  Status PumpRequest(const ExecutionContext& caller) noexcept {
    CanFrame frame;
    while (IsOk(request_sender_.NextFrame(frame))) {
      const auto status = writer_.Send(frame, caller);
      if (!IsOk(status)) {
        ++stats_.send_failures;
        return status;
      }
    }
    return Status::kOk;
  }

  void FinishTimeout(const ExecutionContext& caller) noexcept {
    const auto completion = completion_;
    auto* const completion_state = completion_state_;
    const auto info = call_info_;
    ClearPending();
    request_sender_ = {};
    ++stats_.timeouts;
    ++stats_.completed;
    completion(completion_state, Status::kTimeout, {}, info, caller);
  }

  void ClearPending() noexcept {
    completion_ = nullptr;
    completion_state_ = nullptr;
    pending_ = false;
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
  std::array<std::byte, kRequestPayloadSize> request_payload_{};
  ReliableSender<kRequestPayloadSize> request_sender_;
  ReliableReceiver<kResponsePayloadSize> response_receiver_;
  RawRpcCompletion completion_{};
  void* completion_state_{};
  RpcCallInfo call_info_{};
  RpcClientTransportStats stats_{};
  bool registered_{};
  bool pending_{};
  bool sealed_{};
};

struct RpcServerTransportStats {
  std::uint32_t requests{};
  std::uint32_t responses{};
  std::uint32_t rejected{};
  std::uint32_t timeouts{};
  std::uint32_t decode_failures{};
  std::uint32_t send_failures{};
};

template <ServiceType Service>
class CanRpcServer {
 public:
  using Request = typename ServiceTypeSupport<Service>::Request;
  using Response = typename ServiceTypeSupport<Service>::Response;
  static constexpr std::size_t kRequestSize =
      TypeSupport<Request>::descriptor().max_serialized_size;
  static constexpr std::size_t kResponseSize =
      TypeSupport<Response>::descriptor().max_serialized_size;
  static constexpr std::size_t kRequestHeaderSize = 12;
  static constexpr std::size_t kResponseHeaderSize = 5;
  static constexpr std::size_t kRequestPayloadSize = kRequestHeaderSize + kRequestSize;
  static constexpr std::size_t kResponsePayloadSize = kResponseHeaderSize + kResponseSize;

  static_assert(kRequestPayloadSize <= 96, "CAN RPC request exceeds reliable CAN capacity");
  static_assert(kResponsePayloadSize <= 96, "CAN RPC response exceeds reliable CAN capacity");

  constexpr CanRpcServer(std::uint16_t route_id, CanPriority priority, CanFrameWriter writer,
                         CanClockReader clock, std::uint64_t retry_timeout_ns = 5'000'000,
                         std::uint8_t maximum_retries = 2) noexcept
      : route_id_(route_id),
        priority_(priority),
        writer_(writer),
        clock_(clock),
        retry_timeout_ns_(retry_timeout_ns),
        maximum_retries_(maximum_retries) {}

  Status Bind(RpcRef rpc) noexcept {
    if (bound_) {
      return Status::kInvalidState;
    }
    const auto status = rpc.RegisterClient(Descriptor());
    if (IsOk(status)) {
      rpc_ = rpc;
      bound_ = true;
    }
    return status;
  }

  Status Accept(const CanFrame& frame, std::uint64_t receive_time_ns,
                const ExecutionContext& caller) noexcept {
    if (!bound_) {
      return Status::kInvalidState;
    }
    if (frame.size == 0 || !rpc_detail::IsForRoute(frame, route_id_) ||
        GetFrameKind(frame.data[0]) != FrameKind::kReliable) {
      return Status::kInvalidArgument;
    }
    if (GetReliableSubtype(frame.data[0]) == ReliableSubtype::kAck) {
      return response_sender_.HandleAck(frame);
    }

    ReassembledMessage message;
    CanFrame acknowledgement;
    const auto receive_status = request_receiver_.Accept(frame, message, acknowledgement);
    if (acknowledgement.size != 0) {
      const auto ack_status = writer_.Send(acknowledgement, caller);
      if (!IsOk(ack_status)) {
        ++stats_.send_failures;
        return ack_status;
      }
    }
    if (!IsOk(receive_status)) {
      return receive_status;
    }
    if (active_ || rpc_detail::SenderBusy(response_sender_.state())) {
      ++stats_.rejected;
      return Status::kCapacityExceeded;
    }
    if (message.payload.size() < kRequestHeaderSize ||
        message.payload.size() - kRequestHeaderSize > kRequestSize) {
      ++stats_.decode_failures;
      return Status::kTypeMismatch;
    }

    active_request_id_ = rpc_detail::ReadU32(message.payload.first<4>());
    const auto deadline_ns = rpc_detail::ReadU64(message.payload.subspan<4, 8>());
    if (active_request_id_ == 0) {
      ++stats_.decode_failures;
      return Status::kInvalidArgument;
    }
    active_sequence_ = message.sequence;
    active_ = true;
    if (deadline_ns != 0 && receive_time_ns >= deadline_ns) {
      active_ = false;
      ++stats_.timeouts;
      return SendResponse(Status::kTimeout, {}, caller);
    }

    const auto status = rpc_.CallAsync(Descriptor(), message.payload.subspan(kRequestHeaderSize),
                                       deadline_ns, Complete, this, caller);
    if (!IsOk(status)) {
      active_ = false;
      ++stats_.rejected;
      return SendResponse(status, {}, caller);
    }
    ++stats_.requests;
    return Status::kOk;
  }

  Status Poll(std::uint64_t now_ns, const ExecutionContext& caller) noexcept {
    const auto status = response_sender_.Poll(now_ns);
    return IsOk(status) ? PumpResponse(caller) : status;
  }

  [[nodiscard]] const RpcServerTransportStats& stats() const noexcept { return stats_; }
  [[nodiscard]] const ReliableSenderStats& reliable_stats() const noexcept {
    return response_sender_.stats();
  }

 private:
  static constexpr ServiceDescriptor Descriptor() noexcept {
    return ServiceTypeSupport<Service>::descriptor();
  }

  static void Complete(void* state, Status status, std::span<const std::byte> response,
                       const RpcCallInfo&, const ExecutionContext& context) noexcept {
    auto& self = *static_cast<CanRpcServer*>(state);
    self.active_ = false;
    if (!IsOk(self.SendResponse(status, response, context))) {
      ++self.stats_.send_failures;
    }
  }

  Status SendResponse(Status status, std::span<const std::byte> response,
                      const ExecutionContext& caller) noexcept {
    if (response.size() > kResponseSize || (!IsOk(status) && !response.empty())) {
      return Status::kInvalidArgument;
    }
    if (!rpc_detail::EncodeStatus(status, response_payload_[0])) {
      return Status::kInvalidArgument;
    }
    rpc_detail::WriteU32(active_request_id_,
                         std::span<std::byte>(response_payload_).subspan<1, 4>());
    std::copy(response.begin(), response.end(),
              response_payload_.begin() + static_cast<std::ptrdiff_t>(kResponseHeaderSize));
    const auto payload =
        std::span<const std::byte>(response_payload_).first(kResponseHeaderSize + response.size());
    const auto begin_status =
        response_sender_.Begin(route_id_, priority_, active_sequence_, payload, clock_.NowNs(),
                               retry_timeout_ns_, maximum_retries_);
    if (!IsOk(begin_status)) {
      return begin_status;
    }
    const auto send_status = PumpResponse(caller);
    if (IsOk(send_status)) {
      ++stats_.responses;
    }
    return send_status;
  }

  Status PumpResponse(const ExecutionContext& caller) noexcept {
    CanFrame frame;
    while (IsOk(response_sender_.NextFrame(frame))) {
      const auto status = writer_.Send(frame, caller);
      if (!IsOk(status)) {
        ++stats_.send_failures;
        return status;
      }
    }
    return Status::kOk;
  }

  std::uint16_t route_id_{};
  CanPriority priority_{CanPriority::kBackground};
  RpcRef rpc_;
  CanFrameWriter writer_;
  CanClockReader clock_;
  std::uint64_t retry_timeout_ns_{};
  std::uint8_t maximum_retries_{};
  std::uint8_t active_sequence_{};
  std::uint32_t active_request_id_{};
  std::array<std::byte, kResponsePayloadSize> response_payload_{};
  ReliableReceiver<kRequestPayloadSize> request_receiver_;
  ReliableSender<kResponsePayloadSize> response_sender_;
  RpcServerTransportStats stats_{};
  bool active_{};
  bool bound_{};
};

}  // namespace aster::transport::can
