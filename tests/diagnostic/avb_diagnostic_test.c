/*++

Module Name:

    avb_diagnostic_test.c

Abstract:

    Kernel-mode build placeholder for diagnostic test tool
    The real user-mode implementation is built separately via nmake

--*/

#include "precomp.h"

// Kernel-mode placeholder - the real diagnostic test is user-mode only
// The actual implementation should be built separately using nmake

#ifdef __cplusplus
extern "C" {
#endif

VOID AvbDiagnosticTest_KmBuildPlaceHolder(void)
{
    // Intentionally empty; user-mode diagnostic tool is built separately
}

#ifdef __cplusplus
}
#endif

// End of kernel-mode placeholder
// The actual user-mode implementation should be built separately