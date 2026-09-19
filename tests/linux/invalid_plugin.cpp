#include "aster_pkg_c_interface/pkg_main.h"

namespace {

const aster_string_view_t kModuleNames[]{{"invalid.Module", 14}};

}  // namespace

extern "C" {

uint32_t AsterDynlibGetAbiVersion() { return 99; }
aster_string_view_t AsterDynlibGetPackageName() { return {"invalid", 7}; }
aster_string_view_t AsterDynlibGetPackageVersion() { return {"1.0.0", 5}; }
size_t AsterDynlibGetModuleNum() { return 1; }
const aster_string_view_t* AsterDynlibGetModuleNameList() { return kModuleNames; }
const aster_module_base_t* AsterDynlibCreateModule(aster_string_view_t) { return nullptr; }
void AsterDynlibDestroyModule(const aster_module_base_t*) {}
}
