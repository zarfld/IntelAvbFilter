#pragma warning(disable:4201)  //nonstandard extension used : nameless struct/union
#pragma warning(disable:4115)  //named type definition in parentheses

// Kernel mode headers only - no user mode headers in NDIS drivers
#include <ndis.h>
#include <filteruser.h>
#include "flt_dbg.h"
#include "filter.h"
#include "avb_integration.h"
#include "tsn_config.h"

