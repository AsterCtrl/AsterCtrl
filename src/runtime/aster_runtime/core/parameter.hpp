#pragma once

#include "aster_module_cpp_interface/parameter/parameter.hpp"

namespace aster {

class ParameterStore {
 public:
  ParameterStore() noexcept;
  virtual ~ParameterStore() = default;
  ParameterStore(const ParameterStore&) = delete;
  ParameterStore& operator=(const ParameterStore&) = delete;
  [[nodiscard]] const aster_parameter_base_t* NativeHandle() const noexcept { return &native_; }
  virtual Status Get(std::string_view name, ValueView& output,
                     std::span<std::byte> storage) const noexcept = 0;
  virtual Status Set(std::string_view name, ValueView value,
                     const ExecutionContext& caller) noexcept = 0;

 private:
  static aster_status_t GetNative(void* context, aster_string_view_t name, aster_value_t* output,
                                  std::uint8_t* storage, std::size_t capacity) noexcept;
  static aster_status_t SetNative(void* context, aster_string_view_t name,
                                  const aster_value_t* value,
                                  const aster_execution_context_t* caller) noexcept;
  aster_parameter_base_t native_;
};
}  // namespace aster
