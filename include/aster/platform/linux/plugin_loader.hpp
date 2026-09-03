#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "aster/module.hpp"
#include "aster/plugin.h"
#include "aster/status.hpp"

namespace aster::platform::linux {

class PluginLoader {
 public:
  PluginLoader() noexcept;
  ~PluginLoader();

  PluginLoader(const PluginLoader&) = delete;
  PluginLoader& operator=(const PluginLoader&) = delete;
  PluginLoader(PluginLoader&&) = delete;
  PluginLoader& operator=(PluginLoader&&) = delete;

  Status Open(std::string_view path, CoreRef core) noexcept;
  void Close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] std::string_view name() const noexcept { return name_; }
  [[nodiscard]] std::string_view version() const noexcept { return version_; }
  [[nodiscard]] std::span<ModuleSlot> modules() noexcept { return slots_; }

 private:
  class CAbiModule;

  Status OpenImpl(std::string_view path, CoreRef core);

  void* handle_{};
  const AsterModuleBundlePluginV1* bundle_plugin_{};
  AsterModuleBundleV1 bundle_{};
  std::vector<std::unique_ptr<CAbiModule>> adapters_;
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
  const AsterCorePluginV1* plugin_{};
  std::string_view name_;
  std::string_view version_;
};

}  // namespace aster::platform::linux
