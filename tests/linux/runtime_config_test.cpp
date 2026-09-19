#include <cassert>
#include <string>

#include "aster_runtime/platform/linux/configuration.hpp"

int main() {
  using aster::Status;
  using namespace aster::platform::linux;
  constexpr auto yaml = R"(
api_version: aster.dev/v1alpha3
aster:
  packages:
    - {name: demo, path: ./libdemo.so}
  executors:
    - {name: io, type: thread_pool, threads: 2, queue_capacity: 8}
  modules:
    - name: left
      type: demo.Controller
      package: demo
      namespace: /left
      remap: {/left/state: /robot/joints}
      executor: io
      config: {gain: 2, label: "123", nested: {enabled: true}}
      parameters: {target: 4.5}
    - {name: right, type: demo.Controller, package: demo, enabled: false}
)";
  RuntimeConfig config;
  std::string error;
  assert(ParseRuntimeConfig(yaml, config, error) == Status::kOk);
  assert(config.modules.size() == 2 && !config.modules[1].enabled);
  assert(config.executors[0].threads == 2 && config.executors[0].queue_capacity == 8);
  const auto& values = config.modules[0].config;
  assert(std::get<std::int64_t>(values.at("gain")) == 2);
  assert(std::get<std::string>(values.at("label")) == "123");
  assert(std::get<bool>(values.at("nested.enabled")));
  assert(ParseRuntimeConfig("api_version: aster.dev/v1alpha2\naster: {}", config, error) ==
         Status::kVersionMismatch);
  assert(error.find("v1alpha3") != std::string::npos);
  assert(config.modules.size() == 2);  // Failure does not publish partial configuration.
  for (const auto invalid : {
           "aster: {modules: [], unknown: true}",
           "aster: {modules: [{name: x, type: X, executor: missing}]}",
           "aster: {modules: [{name: x, type: X, config: {a: 1, a: 2}}]}",
           "aster: {modules: [{name: x, type: X, config: {a: [1, 2]}}]}",
           "aster: {executors: [{name: x, type: priority}], modules: []}",
           "aster: {executors: [{name: x, type: serial, threads: 2}], modules: []}",
           "aster: {modules: [{name: x, type: X, package: missing}]}",
           "aster: {modules: [{name: x, type: X, remap: {relative: /x}}]}",
           "aster: {modules: [{name: x, type: X}, {name: x, type: X}]}",
       }) {
    assert(ParseRuntimeConfig(std::string("api_version: aster.dev/v1alpha3\n") + invalid, config,
                              error) == Status::kInvalidArgument);
    assert(!error.empty());
  }
}
