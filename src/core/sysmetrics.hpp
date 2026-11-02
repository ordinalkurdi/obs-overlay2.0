// System-wide CPU and RAM usage in percent (not just OBS's own process). Call sample() about once a second.
#pragma once
#include "types.hpp"

namespace ovl {
SysMetrics sample_system_metrics();
}
