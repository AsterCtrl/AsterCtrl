#include "aster/plugin.h"

namespace {

const AsterModuleBundlePluginV1 kPlugin{
    99, sizeof(AsterModuleBundlePluginV1), {"invalid", 7}, {"1.0.0", 5}, nullptr, nullptr, nullptr,
};

}  // namespace

extern "C" const AsterModuleBundlePluginV1* aster_module_bundle_v1() { return &kPlugin; }
