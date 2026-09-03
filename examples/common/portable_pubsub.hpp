/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/channel.hpp"
#include "aster/module.hpp"
#include "aster/runtime.hpp"

namespace aster::examples {

struct Pulse {
  std::uint32_t sequence{};
};

}  // namespace aster::examples

namespace aster {

template <>
struct TypeSupport<examples::Pulse> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"aster.examples.Pulse/v1",
            {{std::byte{0x9a}, std::byte{0x7e}, std::byte{0x44}, std::byte{0x2f}, std::byte{0x9b},
              std::byte{0xd8}, std::byte{0x45}, std::byte{0x32}, std::byte{0x82}, std::byte{0x5f},
              std::byte{0x04}, std::byte{0x01}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
              std::byte{0x01}}},
            4};
  }

  static Status Encode(const examples::Pulse& value, std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.size() < 4) {
      return Status::kCapacityExceeded;
    }
    for (std::size_t index = 0; index < 4; ++index) {
      output[index] = static_cast<std::byte>(value.sequence >> (index * 8U));
    }
    written = 4;
    return Status::kOk;
  }

  static Status Decode(std::span<const std::byte> input, examples::Pulse& value) noexcept {
    if (input.size() != 4) {
      return Status::kInvalidArgument;
    }
    value.sequence = 0;
    for (std::size_t index = 0; index < 4; ++index) {
      value.sequence |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index]))
                        << (index * 8U);
    }
    return Status::kOk;
  }
};

}  // namespace aster

namespace aster::examples {

class PulseSink final : public Module {
 public:
  [[nodiscard]] ModuleInfo Info() const noexcept override {
    return {"pulse-sink", "aster.examples.PulseSink", "portable-pubsub", {0, 2, 0}};
  }

  Status Initialize(CoreRef core) noexcept override {
    return subscriber_.Bind(core.channel(), "pulse", OnPulse, this);
  }

  Status Start() noexcept override {
    started_ = true;
    return Status::kOk;
  }

  void Shutdown() noexcept override { started_ = false; }

  [[nodiscard]] bool received() const noexcept { return received_; }
  [[nodiscard]] std::uint32_t sequence() const noexcept { return sequence_; }

 private:
  static Status OnPulse(void* state, const Pulse& pulse, const MessageInfo&,
                        const ExecutionContext&) noexcept {
    auto& self = *static_cast<PulseSink*>(state);
    if (!self.started_) {
      return Status::kInvalidState;
    }
    self.received_ = true;
    self.sequence_ = pulse.sequence;
    return Status::kOk;
  }

  Subscriber<Pulse> subscriber_;
  std::uint32_t sequence_{};
  bool started_{};
  bool received_{};
};

class PulseSource final : public Module {
 public:
  [[nodiscard]] ModuleInfo Info() const noexcept override {
    return {"pulse-source", "aster.examples.PulseSource", "portable-pubsub", {0, 2, 0}};
  }

  Status Initialize(CoreRef core) noexcept override {
    return publisher_.Bind(core.channel(), "pulse");
  }

  Status Start() noexcept override {
    const ExecutionContext context{"portable-pubsub", ExecutionKind::kThread, 1};
    return publisher_.Publish(Pulse{42}, 1, context);
  }

  void Shutdown() noexcept override {}

 private:
  Publisher<Pulse> publisher_;
};

template <std::size_t MaximumMessageSize = 32>
class PortablePubSubComposition {
 public:
  PortablePubSubComposition() noexcept
      : core_(CoreHandles{.configurator = {},
                          .logger = {},
                          .executor = {},
                          .channel = ChannelRef(channel_),
                          .rpc = {},
                          .parameter = {},
                          .clock = {},
                          .allocator = {},
                          .hardware = {}}),
        modules_{{{&sink_, core_, "sink"}, {&source_, core_, "source"}}},
        registries_{{{&channel_}}},
        runtime_(modules_, registries_) {}

  Status Run() noexcept {
    auto status = runtime_.Initialize();
    if (IsOk(status)) {
      status = runtime_.Start();
    }
    return status;
  }

  void Shutdown() noexcept { runtime_.Shutdown(); }

  [[nodiscard]] const PulseSink& sink() const noexcept { return sink_; }
  [[nodiscard]] RuntimeState state() const noexcept { return runtime_.state(); }

 private:
  LocalChannel<1, 1, MaximumMessageSize> channel_;
  PulseSink sink_;
  PulseSource source_;
  CoreRef core_;
  std::array<ModuleSlot, 2> modules_;
  std::array<RegistrySlot, 1> registries_;
  Runtime runtime_;
};

}  // namespace aster::examples
