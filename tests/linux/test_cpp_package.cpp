#include "aster_pkg_c_interface/pkg_macro.hpp"
#include "test_types.hpp"

namespace {

class GeneratedModule final : public aster::ModuleBase {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"generated", "test.GeneratedModule", "generated-package", {1, 2, 3}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    if (!core.instance_name().empty()) {
      return core.logger().Write(aster::LogLevel::kInfo, core.instance_name());
    }
    return core.logger().Write(aster::LogLevel::kInfo, "generated initialized");
  }

  aster::Status Start() noexcept override { return aster::Status::kOk; }
  void Shutdown() noexcept override {}
};

class ConfiguredSource final : public aster::ModuleBase {
 public:
  aster::ModuleInfo Info() const noexcept override {
    return {"source", "test.ConfiguredSource", "generated-package", {1, 2, 3}};
  }
  aster::Status Initialize(aster::CoreRef core) override {
    auto status = core.configurator().Get("gain", value_);
    if (!aster::IsOk(status)) return status;
    std::string_view executor;
    status = core.configurator().Get("expected_executor", executor);
    if (!aster::IsOk(status)) return status;
    if (core.executor().Name() != executor) return aster::Status::kInvalidArgument;
    status = core.logger().Write(aster::LogLevel::kInfo, core.instance_name());
    if (!aster::IsOk(status)) return status;
    return publisher_.Bind(core.channel(), "state");
  }
  aster::Status Start() override { return publisher_.Publish({value_}); }
  void Shutdown() noexcept override {}

 private:
  std::uint32_t value_{};
  aster::Publisher<test::Sample> publisher_;
};

constexpr aster::ModuleRegistration kModules[]{
    {"test.GeneratedModule", &aster::CreateModule<GeneratedModule>},
    {"test.ConfiguredSource", &aster::CreateModule<ConfiguredSource>},
    {"test.WrongType", &aster::CreateModule<GeneratedModule>},
};

}  // namespace

ASTER_PKG_MAIN(kModules, "generated-package", "1.2.3")
