#include "aster_runtime/platform/linux/plugin_loader.hpp"

#include <dlfcn.h>

#include <bit>
#include <cstring>
#include <new>
#include <string>

namespace aster::platform::linux {
namespace {

constexpr auto kGetAbiVersion = "AsterDynlibGetAbiVersion";
constexpr auto kGetPackageName = "AsterDynlibGetPackageName";
constexpr auto kGetPackageVersion = "AsterDynlibGetPackageVersion";
constexpr auto kGetModuleNum = "AsterDynlibGetModuleNum";
constexpr auto kGetModuleNameList = "AsterDynlibGetModuleNameList";
constexpr auto kCreateModule = "AsterDynlibCreateModule";
constexpr auto kDestroyModule = "AsterDynlibDestroyModule";

using GetAbiVersion = std::uint32_t (*)();
using GetString = aster_string_view_t (*)();
using GetModuleNum = std::size_t (*)();
using GetModuleNameList = const aster_string_view_t* (*)();

template <typename Function>
[[nodiscard]] Function LoadFunction(void* handle, const char* name) noexcept {
  void* symbol = dlsym(handle, name);
  if (symbol == nullptr) {
    return nullptr;
  }
  static_assert(sizeof(Function) == sizeof(symbol));
  return std::bit_cast<Function>(symbol);
}

[[nodiscard]] bool ValidView(aster_string_view_t value) noexcept {
  return value.data != nullptr && value.size != 0;
}

}  // namespace

PluginLoader::PluginLoader() noexcept = default;
PluginLoader::~PluginLoader() { Close(); }

Status PluginLoader::Open(std::string_view path) noexcept {
  if (is_open()) {
    return Status::kInvalidState;
  }
  if (path.empty() || path.find('\0') != std::string_view::npos) {
    return Status::kInvalidArgument;
  }
  try {
    return OpenImpl(path);
  } catch (const std::bad_alloc&) {
    Close();
    return Status::kCapacityExceeded;
  } catch (...) {
    Close();
    return Status::kInternal;
  }
}

// Keep the type/instance call shape; the catalog and instance checks below reject invalid names.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Status PluginLoader::CreateModule(std::string_view module_name, std::string_view instance_name,
                                  CoreRef core) noexcept {
  if (!is_open() || create_module_ == nullptr || destroy_module_ == nullptr) {
    return Status::kInvalidState;
  }
  bool registered{};
  for (const auto candidate : module_names_) {
    if (candidate == module_name) {
      registered = true;
      break;
    }
  }
  if (!registered) {
    return Status::kNotFound;
  }

  try {
    owned_modules_.reserve(owned_modules_.size() + 1U);
    slots_.reserve(slots_.size() + 1U);
    const auto* module = create_module_(ToAbiString(module_name));
    if (module == nullptr) {
      return Status::kCapacityExceeded;
    }
    const ModuleRef module_ref(module);
    const auto module_status = module_ref.Validate();
    if (!IsOk(module_status)) {
      destroy_module_(module);
      return module_status;
    }
    const auto module_info = module_ref.Info();
    if (module_info.type != module_name) {
      destroy_module_(module);
      return Status::kTypeMismatch;
    }
    const auto resolved_instance_name = instance_name.empty() ? module_info.name : instance_name;
    if (resolved_instance_name.empty()) {
      destroy_module_(module);
      return Status::kInvalidArgument;
    }
    for (const auto& slot : slots_) {
      if (slot.instance_name == resolved_instance_name) {
        destroy_module_(module);
        return Status::kAlreadyExists;
      }
    }
    owned_modules_.push_back(module);
    slots_.push_back({module_ref, core, resolved_instance_name});
    return Status::kOk;
  } catch (const std::bad_alloc&) {
    return Status::kCapacityExceeded;
  } catch (...) {
    return Status::kInternal;
  }
}

Status PluginLoader::OpenImpl(std::string_view path) {
  const std::string path_string(path);
  handle_ = dlopen(path_string.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle_ == nullptr) {
    return Status::kNotFound;
  }

  const auto get_abi_version = LoadFunction<GetAbiVersion>(handle_, kGetAbiVersion);
  const auto get_package_name = LoadFunction<GetString>(handle_, kGetPackageName);
  const auto get_package_version = LoadFunction<GetString>(handle_, kGetPackageVersion);
  const auto get_module_num = LoadFunction<GetModuleNum>(handle_, kGetModuleNum);
  const auto get_module_names = LoadFunction<GetModuleNameList>(handle_, kGetModuleNameList);
  create_module_ = LoadFunction<CreateModuleFn>(handle_, kCreateModule);
  destroy_module_ = LoadFunction<DestroyModule>(handle_, kDestroyModule);
  if (get_abi_version == nullptr || get_package_name == nullptr || get_package_version == nullptr ||
      get_module_num == nullptr || get_module_names == nullptr || create_module_ == nullptr ||
      destroy_module_ == nullptr) {
    Close();
    return Status::kNotFound;
  }
  if (get_abi_version() != ASTER_ABI_VERSION) {
    Close();
    return Status::kVersionMismatch;
  }

  const auto package_name = get_package_name();
  const auto package_version = get_package_version();
  const auto module_count = get_module_num();
  const auto* module_names = get_module_names();
  if (!ValidView(package_name) || !ValidView(package_version) || module_count == 0 ||
      module_names == nullptr) {
    Close();
    return Status::kInvalidArgument;
  }
  name_ = FromAbiString(package_name);
  version_ = FromAbiString(package_version);

  module_names_.reserve(module_count);
  for (std::size_t index = 0; index < module_count; ++index) {
    if (!ValidView(module_names[index])) {
      Close();
      return Status::kInvalidArgument;
    }
    const auto registration_name = FromAbiString(module_names[index]);
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (registration_name == FromAbiString(module_names[previous])) {
        Close();
        return Status::kAlreadyExists;
      }
    }
    module_names_.push_back(registration_name);
  }
  return Status::kOk;
}

void PluginLoader::Close() noexcept {
  slots_.clear();
  if (destroy_module_ != nullptr) {
    for (auto module = owned_modules_.rbegin(); module != owned_modules_.rend(); ++module) {
      try {
        destroy_module_(*module);
      } catch (...) {  // NOLINT(bugprone-empty-catch)
      }
    }
  }
  owned_modules_.clear();
  module_names_.clear();
  create_module_ = nullptr;
  destroy_module_ = nullptr;
  name_ = {};
  version_ = {};
  if (handle_ != nullptr) {
    dlclose(handle_);
    handle_ = nullptr;
  }
}

CorePluginLoader::CorePluginLoader() noexcept = default;
CorePluginLoader::~CorePluginLoader() { Close(); }

Status CorePluginLoader::Open(std::string_view path) noexcept {
  if (is_open()) {
    return Status::kInvalidState;
  }
  if (path.empty() || path.find('\0') != std::string_view::npos) {
    return Status::kInvalidArgument;
  }
  try {
    return OpenImpl(path);
  } catch (const std::bad_alloc&) {
    Close();
    return Status::kCapacityExceeded;
  } catch (...) {
    Close();
    return Status::kInternal;
  }
}

Status CorePluginLoader::OpenImpl(std::string_view path) {
  const std::string path_string(path);
  handle_ = dlopen(path_string.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle_ == nullptr) {
    return Status::kNotFound;
  }
  const auto create_plugin =
      LoadFunction<aster_core_plugin_create_t>(handle_, ASTER_CORE_PLUGIN_CREATE_SYMBOL);
  destroy_plugin_ =
      LoadFunction<aster_core_plugin_destroy_t>(handle_, ASTER_CORE_PLUGIN_DESTROY_SYMBOL);
  if (create_plugin == nullptr || destroy_plugin_ == nullptr) {
    Close();
    return Status::kNotFound;
  }
  plugin_ = create_plugin();
  if (plugin_ == nullptr) {
    Close();
    return Status::kCapacityExceeded;
  }
  if (plugin_->abi_version != ASTER_ABI_VERSION ||
      plugin_->struct_size < sizeof(aster_core_plugin_t)) {
    Close();
    return Status::kVersionMismatch;
  }
  if (!ValidView(plugin_->name) || !ValidView(plugin_->version) ||
      plugin_->query_interface == nullptr) {
    Close();
    return Status::kInvalidArgument;
  }
  name_ = FromAbiString(plugin_->name);
  version_ = FromAbiString(plugin_->version);
  return Status::kOk;
}

Status CorePluginLoader::QueryInterface(std::string_view name, std::uint32_t version,
                                        std::uint32_t minimum_struct_size,
                                        const void*& interface_table) const noexcept {
  interface_table = nullptr;
  if (plugin_ == nullptr) {
    return Status::kInvalidState;
  }
  if (name.empty() || version == 0 || minimum_struct_size < sizeof(aster_interface_header_t)) {
    return Status::kInvalidArgument;
  }
  std::uint32_t struct_size{};
  aster_status_t result{};
  try {
    result = plugin_->query_interface(plugin_->impl, ToAbiString(name), version, &interface_table,
                                      &struct_size);
  } catch (...) {
    interface_table = nullptr;
    return Status::kInternal;
  }
  const auto status = FromAbiStatus(result);
  if (!IsOk(status)) {
    interface_table = nullptr;
    return status;
  }
  if (interface_table == nullptr) {
    return Status::kInternal;
  }
  aster_interface_header_t header{};
  std::memcpy(&header, interface_table, sizeof(header));
  if (header.interface_version != version || header.struct_size != struct_size ||
      struct_size < minimum_struct_size) {
    interface_table = nullptr;
    return Status::kVersionMismatch;
  }
  return Status::kOk;
}

void CorePluginLoader::Close() noexcept {
  if (plugin_ != nullptr && destroy_plugin_ != nullptr) {
    try {
      destroy_plugin_(plugin_);
    } catch (...) {  // NOLINT(bugprone-empty-catch)
    }
  }
  plugin_ = nullptr;
  destroy_plugin_ = nullptr;
  name_ = {};
  version_ = {};
  if (handle_ != nullptr) {
    dlclose(handle_);
    handle_ = nullptr;
  }
}

}  // namespace aster::platform::linux
