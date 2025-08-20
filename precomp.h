#pragma warning(disable:4201)  //nonstandard extension used : nameless struct/union

// Define for Intel library Windows kernel compatibility
#define INTEL_WIN32_KERNEL_MODE 1

#include <ndis.h>
#include <filteruser.h>
#include "flt_dbg.h"
#include "filter.h"

// Use Intel library headers for common types and device enums
#include "external/intel_avb/lib/intel.h"

#include "avb_integration.h"
#include "tsn_config.h"

// Auto-generated Intel Ethernet register maps (SSOT)
// Headers are generated from intel-ethernet-regs/devices/*.yaml
// Do not edit manually; run the generator task to update.
#include "intel-ethernet-regs/gen/i210_regs.h"
#include "intel-ethernet-regs/gen/i217_regs.h"
#include "intel-ethernet-regs/gen/i219_regs.h"
#include "intel-ethernet-regs/gen/i225_regs.h"
#include "intel-ethernet-regs/gen/i226_regs.h"

