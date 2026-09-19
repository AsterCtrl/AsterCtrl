#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "aster_core_plugin_interface/core_plugin_main.h"
#include "aster_module_cpp_interface/module.hpp"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_runtime/runtime.hpp"

namespace aster::platform::linux {

class PluginLoader {
 public:
  PluginLoader() noexcept;
  ~PluginLoader();

  PluginLoader(const PluginLoader&) = delete;
  PluginLoader& operator=(const PluginLoader&) = delete;
  PluginLoader(PluginLoader&&) = delete;
  PluginLoader& operator=(PluginLoader&&) = delete;

  Status Open(std::string_view path) noexcept;
  Status CreateModule(std::string_view module_name, std::string_view instance_name,
                      CoreRef core = {}) noexcept;
  void Close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] std::string_view name() const noexcept { return name_; }
  [[nodiscard]] std::string_view version() const noexcept { return version_; }
  [[nodiscard]] std::span<const std::string_view> module_names() const noexcept {
    return module_names_;
  }
  [[nodiscard]] std::span<ModuleSlot> modules() noexcept { return slots_; }

 private:
  using CreateModuleFn = const aster_module_base_t* (*)(aster_string_view_t module_name);
  using DestroyModule = void (*)(const aster_module_base_t* module);

  Status OpenImpl(std::string_view path);

  void* handle_{};
  CreateModuleFn create_module_{};
  DestroyModule destroy_module_{};
  std::vector<std::string_view> module_names_;
  std::vector<const aster_module_base_t*> owned_modules_;
  std::vector<ModuleSlot> slots_;
  std::string_view name_;
  std::string_view version_;
};

class CorePluginLoader {
 public:
  CorePluginLoader() noexcept;
  ~CorePluginLoader();

  CorePluginLoader(const CorePluginLoader&) = delete;
  CorePluginLoader& operator=(const CorePluginLoader&) = delete;
  CorePluginLoader(CorePluginLoader&&) = delete;
  CorePluginLoader& operator=(CorePluginLoader&&) = delete;

  Status Open(std::string_view path) noexcept;
  Status QueryInterface(std::string_view name, std::uint32_t version,
                        std::uint32_t minimum_struct_size,
                        const void*& interface_table) const noexcept;
  void Close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] std::string_view name() const noexcept { return name_; }
  [[nodiscard]] std::string_view version() const noexcept { return version_; }

 private:
  Status OpenImpl(std::string_view path);

  void* handle_{};
  const aster_core_plugin_t* plugin_{};
  aster_core_plugin_destroy_t destroy_plugin_{};
  std::string_view name_;
  std::string_view version_;
};

}  // namespace aster::platform::linux
