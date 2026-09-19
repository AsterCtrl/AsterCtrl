#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include "aster_runtime/platform/linux/supervisor.hpp"

namespace aster::platform::linux {

// Embeddable configuration-driven Host runtime. Borrowed static Modules must
// outlive this object. Shutdown must be called from the controlling thread.
class NodeRuntime final {
 public:
  NodeRuntime();
  ~NodeRuntime();
  NodeRuntime(const NodeRuntime&) = delete;
  NodeRuntime& operator=(const NodeRuntime&) = delete;

  Status RegisterModule(std::string_view instance_name, ModuleBase& module) noexcept;
  Status Initialize(std::string_view yaml,
                    const std::filesystem::path& base_directory = {}) noexcept;
  Status Start() noexcept;
  void Shutdown() noexcept;
  [[nodiscard]] SupervisorState state() const noexcept;
  [[nodiscard]] std::string_view diagnostic() const noexcept;
  Status VisitGraph(GraphVisitor visitor, void* state) const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace aster::platform::linux
