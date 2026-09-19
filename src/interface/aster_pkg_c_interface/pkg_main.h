#ifndef ASTER_PKG_C_INTERFACE_MAIN_H_
#define ASTER_PKG_C_INTERFACE_MAIN_H_

#include <stddef.h>
#include <stdint.h>

#include "aster_module_c_interface/module_base.h"
#include "aster_module_c_interface/util/string.h"
#include "aster_module_c_interface/util/version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Module Package entrypoints resolved by the Linux loader. */
ASTER_INTERFACE_EXPORT uint32_t AsterDynlibGetAbiVersion(void);
ASTER_INTERFACE_EXPORT aster_string_view_t AsterDynlibGetPackageName(void);
ASTER_INTERFACE_EXPORT aster_string_view_t AsterDynlibGetPackageVersion(void);
ASTER_INTERFACE_EXPORT size_t AsterDynlibGetModuleNum(void);
ASTER_INTERFACE_EXPORT const aster_string_view_t* AsterDynlibGetModuleNameList(void);
ASTER_INTERFACE_EXPORT const aster_module_base_t* AsterDynlibCreateModule(
    aster_string_view_t module_name);
ASTER_INTERFACE_EXPORT void AsterDynlibDestroyModule(const aster_module_base_t* module);

#ifdef __cplusplus
}
#endif

#endif /* ASTER_PKG_C_INTERFACE_MAIN_H_ */
