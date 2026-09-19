#include "aster_runtime/configuration.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

int main() {
  aster::StaticConfigurator<2, 8> config;
  const std::uint32_t answer = 42;
  assert(config.Put("answer", answer) == aster::Status::kOk);
  char source[] = "hello";
  assert(config.Put("label", std::string_view(source, 5)) == aster::Status::kOk);
  source[0] = 'X';
  assert(config.Seal() == aster::Status::kOk);
  std::uint32_t loaded{};
  assert(aster::ConfiguratorRef(config).Get("answer", loaded) == aster::Status::kOk);
  assert(loaded == answer);
  float wrong_type = 1.0F;
  assert(aster::ConfiguratorRef(config).Get("answer", wrong_type) == aster::Status::kTypeMismatch);
  assert(wrong_type == 1.0F);
  std::string_view label;
  assert(aster::ConfiguratorRef(config).Get("label", label) == aster::Status::kOk);
  assert(label == "hello");
  std::uint8_t narrow = 7;
  assert(aster::ValueView(std::uint64_t{256}).Get(narrow) == aster::Status::kInvalidArgument);
  assert(narrow == 7);

  aster::StaticParameterStore<1, 8> parameters;
  assert(parameters.Register("gain", aster::ValueView(answer)) == aster::Status::kOk);
  assert(parameters.Seal() == aster::Status::kOk);
  const std::uint32_t next = 7;
  const aster::ExecutionContext caller("test", aster::ExecutionKind::kThread, 0);
  assert(aster::ParameterRef(parameters).Set("gain", next, caller) == aster::Status::kOk);
  std::uint32_t output{};
  assert(aster::ParameterRef(parameters).Get("gain", output) == aster::Status::kOk);
  assert(output == next);

  aster::StaticParameterStore<2, 8> typed_parameters;
  assert(typed_parameters.Register("gain", aster::ValueView(std::int64_t{3})) ==
         aster::Status::kOk);
  assert(typed_parameters.Register("label", aster::ValueView(std::string_view("hello"))) ==
         aster::Status::kOk);
  assert(typed_parameters.Seal() == aster::Status::kOk);
  const aster::ParameterRef typed(typed_parameters);
  std::int64_t gain{};
  assert(typed.Get("gain", gain) == aster::Status::kOk && gain == 3);
  assert(typed.Set("gain", std::int64_t{9}, caller) == aster::Status::kOk);
  assert(typed.Set("gain", true, caller) == aster::Status::kTypeMismatch);
  assert(typed.Get("gain", gain) == aster::Status::kOk && gain == 9);
  std::array<std::byte, 8> storage{};
  assert(typed.Get("label", label) == aster::Status::kCapacityExceeded);
  assert(typed.Get("label", label, storage) == aster::Status::kOk && label == "hello");
  assert(typed.Set("label", std::string_view("world"), caller) == aster::Status::kOk);
  assert(label == "hello");  // the caller's snapshot remains valid after a mutation
}
