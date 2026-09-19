#pragma once

#include <cstdint>
#include <new>
#include <string_view>

#include "aster_module_c_interface/module_base.h"
#include "aster_module_cpp_interface/core_ref.hpp"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_module_cpp_interface/util/string_view.hpp"

namespace aster {

struct SemanticVersion {
  std::uint16_t major{};
  std::uint16_t minor{};
  std::uint16_t patch{};

  constexpr bool operator==(const SemanticVersion&) const noexcept = default;
};

struct ModuleInfo {
  std::string_view name;
  std::string_view type;
  std::string_view package;
  SemanticVersion version;
};

class ModuleBase {
 public:
  ModuleBase() noexcept : native_(MakeNativeHandle(this)) {}
  virtual ~ModuleBase() = default;

  ModuleBase(const ModuleBase&) = delete;
  ModuleBase& operator=(const ModuleBase&) = delete;
  ModuleBase(ModuleBase&&) = delete;
  ModuleBase& operator=(ModuleBase&&) = delete;

  [[nodiscard]] virtual ModuleInfo Info() const noexcept = 0;
  virtual Status Initialize(CoreRef core) = 0;
  virtual Status Start() = 0;
  virtual void Shutdown() noexcept = 0;

  [[nodiscard]] const aster_module_base_t* NativeHandle() const noexcept { return &native_; }

 private:
  static aster_module_base_t MakeNativeHandle(ModuleBase* instance) noexcept {
    return {
        .abi_version = ASTER_ABI_VERSION,
        .struct_size = sizeof(aster_module_base_t),
        .impl = instance,
        .info = NativeInfo,
        .initialize = NativeInitialize,
        .start = NativeStart,
        .shutdown = NativeShutdown,
    };
  }

  static aster_module_info_t NativeInfo(void* instance) noexcept {
    const auto info = static_cast<ModuleBase*>(instance)->Info();
    return {
        ToAbiString(info.name),
        ToAbiString(info.type),
        ToAbiString(info.package),
        {info.version.major, info.version.minor, info.version.patch},
    };
  }

  static aster_status_t NativeInitialize(void* instance, const aster_core_base_t* core) noexcept {
#if defined(__cpp_exceptions)
    try {
#endif
      return ToAbiStatus(static_cast<ModuleBase*>(instance)->Initialize(CoreRef(core)));
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
      return ASTER_STATUS_CAPACITY_EXCEEDED;
    } catch (...) {
      return ASTER_STATUS_INTERNAL;
    }
#endif
  }

  static aster_status_t NativeStart(void* instance) noexcept {
#if defined(__cpp_exceptions)
    try {
#endif
      return ToAbiStatus(static_cast<ModuleBase*>(instance)->Start());
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
      return ASTER_STATUS_CAPACITY_EXCEEDED;
    } catch (...) {
      return ASTER_STATUS_INTERNAL;
    }
#endif
  }

  static void NativeShutdown(void* instance) noexcept {
    static_cast<ModuleBase*>(instance)->Shutdown();
  }

  aster_module_base_t native_;
};

class ModuleRef {
 public:
  constexpr ModuleRef() noexcept = default;
  constexpr explicit ModuleRef(const aster_module_base_t* module) noexcept : module_(module) {}
  explicit ModuleRef(const ModuleBase& module) noexcept : module_(module.NativeHandle()) {}

  [[nodiscard]] Status Validate() const noexcept {
    const auto status = ValidateStructure();
    if (!IsOk(status)) {
      return status;
    }
    const auto info = module_->info(module_->impl);
    if (info.name.data == nullptr || info.name.size == 0 || info.type.data == nullptr ||
        info.type.size == 0 || info.package.data == nullptr || info.package.size == 0) {
      return Status::kInvalidArgument;
    }
    return Status::kOk;
  }

  [[nodiscard]] ModuleInfo Info() const noexcept {
    if (!IsOk(ValidateStructure())) {
      return {};
    }
    const auto info = module_->info(module_->impl);
    return {
        FromAbiString(info.name),
        FromAbiString(info.type),
        FromAbiString(info.package),
        {info.version.major, info.version.minor, info.version.patch},
    };
  }

  Status Initialize(CoreRef core) const noexcept {
    const auto status = ValidateStructure();
    return IsOk(status) ? FromAbiStatus(module_->initialize(module_->impl, core.NativeHandle()))
                        : status;
  }

  Status Start() const noexcept {
    const auto status = ValidateStructure();
    return IsOk(status) ? FromAbiStatus(module_->start(module_->impl)) : status;
  }

  void Shutdown() const noexcept {
    if (IsOk(ValidateStructure())) {
      module_->shutdown(module_->impl);
    }
  }

  [[nodiscard]] constexpr const aster_module_base_t* NativeHandle() const noexcept {
    return module_;
  }
  [[nodiscard]] explicit operator bool() const noexcept { return IsOk(ValidateStructure()); }

 private:
  [[nodiscard]] Status ValidateStructure() const noexcept {
    if (module_ == nullptr) {
      return Status::kInvalidArgument;
    }
    if (module_->abi_version != ASTER_ABI_VERSION ||
        module_->struct_size < sizeof(aster_module_base_t)) {
      return Status::kVersionMismatch;
    }
    if (module_->impl == nullptr || module_->info == nullptr || module_->initialize == nullptr ||
        module_->start == nullptr || module_->shutdown == nullptr) {
      return Status::kInvalidArgument;
    }
    return Status::kOk;
  }

  const aster_module_base_t* module_{};
};

}  // namespace aster
