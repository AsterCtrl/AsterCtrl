#pragma once

#if defined(__ZEPHYR__)
#error "aster_pkg_c_interface is only available for Linux Module Packages"
#endif

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "aster_module_cpp_interface/module.hpp"
#include "aster_pkg_c_interface/pkg_main.h"

namespace aster {

struct ModuleRegistration {
  std::string_view name;
  ModuleBase* (*create)();
};

template <typename ModuleType>
  requires std::is_base_of_v<ModuleBase, ModuleType> && std::is_default_constructible_v<ModuleType>
ModuleBase* CreateModule() {
  return new ModuleType;
}

}  // namespace aster

/*
 * The CLI generates the registration array and this one invocation. Module
 * authors implement ModuleBase only; they never maintain C entrypoints.
 */
#define ASTER_PKG_MAIN(module_register_array, package_name, package_version)            \
  static constexpr std::size_t kAsterModuleCount =                                      \
      sizeof(module_register_array) / sizeof(module_register_array[0]);                 \
                                                                                        \
  extern "C" {                                                                          \
                                                                                        \
  std::uint32_t AsterDynlibGetAbiVersion() { return ASTER_ABI_VERSION; }                \
                                                                                        \
  aster_string_view_t AsterDynlibGetPackageName() {                                     \
    return {package_name, sizeof(package_name) - 1U};                                   \
  }                                                                                     \
                                                                                        \
  aster_string_view_t AsterDynlibGetPackageVersion() {                                  \
    return {package_version, sizeof(package_version) - 1U};                             \
  }                                                                                     \
                                                                                        \
  std::size_t AsterDynlibGetModuleNum() { return kAsterModuleCount; }                   \
                                                                                        \
  const aster_string_view_t* AsterDynlibGetModuleNameList() {                           \
    static const auto names = [] {                                                      \
      std::array<aster_string_view_t, kAsterModuleCount> result{};                      \
      for (std::size_t index = 0; index < kAsterModuleCount; ++index) {                 \
        result[index] = {module_register_array[index].name.data(),                      \
                         module_register_array[index].name.size()};                     \
      }                                                                                 \
      return result;                                                                    \
    }();                                                                                \
    return names.data();                                                                \
  }                                                                                     \
                                                                                        \
  const aster_module_base_t* AsterDynlibCreateModule(aster_string_view_t module_name) { \
    if (module_name.data == nullptr) {                                                  \
      return nullptr;                                                                   \
    }                                                                                   \
    const std::string_view requested(module_name.data, module_name.size);               \
    for (std::size_t index = 0; index < kAsterModuleCount; ++index) {                   \
      if (requested != module_register_array[index].name ||                             \
          module_register_array[index].create == nullptr) {                             \
        continue;                                                                       \
      }                                                                                 \
      try {                                                                             \
        auto* module = module_register_array[index].create();                           \
        return module == nullptr ? nullptr : module->NativeHandle();                    \
      } catch (...) {                                                                   \
        return nullptr;                                                                 \
      }                                                                                 \
    }                                                                                   \
    return nullptr;                                                                     \
  }                                                                                     \
                                                                                        \
  void AsterDynlibDestroyModule(const aster_module_base_t* module) {                    \
    if (module != nullptr && module->abi_version == ASTER_ABI_VERSION &&                \
        module->struct_size >= sizeof(aster_module_base_t)) {                           \
      delete static_cast<::aster::ModuleBase*>(module->impl);                           \
    }                                                                                   \
  }                                                                                     \
  }
