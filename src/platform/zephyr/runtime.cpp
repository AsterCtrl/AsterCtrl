#include "aster/platform/zephyr/runtime_services.hpp"

namespace aster::platform::zephyr {

static_assert(CONFIG_ASTERCTRL_MAX_MODULES > 0);
static_assert(CONFIG_ASTERCTRL_MAX_CHANNELS > 0);
static_assert(CONFIG_ASTERCTRL_MAX_RPC_SERVICES > 0);

}  // namespace aster::platform::zephyr
