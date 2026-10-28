#include "sas/version.h"
#include "sas/version_info.h"

namespace sas {

const char* engineVersion() noexcept { return SAS_VERSION_STRING; }

}  // namespace sas
