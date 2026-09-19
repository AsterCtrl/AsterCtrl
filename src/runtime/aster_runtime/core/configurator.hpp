#pragma once

#include "aster_module_cpp_interface/configurator/configurator.hpp"

namespace aster {

class Configurator {
 public:
  Configurator() noexcept;
  virtual ~Configurator() = default;
  Configurator(const Configurator&) = delete;
  Configurator& operator=(const Configurator&) = delete;
  [[nodiscard]] const aster_configurator_base_t* NativeHandle() const noexcept { return &native_; }
  virtual Status Get(std::string_view key, ValueView& output) const noexcept = 0;

 private:
  static aster_status_t GetNative(void* context, aster_string_view_t key,
                                  aster_value_t* output) noexcept;
  aster_configurator_base_t native_;
};
}  // namespace aster
