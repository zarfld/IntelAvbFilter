#include "precomp.h"

// This file is built as part of the KM driver project but contains user-mode test code.
// When building the driver, we only compile a kernel-mode placeholder.
// The real user-mode test app is built separately via nmake.

// Kernel-mode placeholder for avb_test files
// Real user-mode implementations are built separately via nmake

// Kernel-mode placeholder - ensure Windows types are available
#ifndef VOID
#define VOID void
#endif

#ifdef __cplusplus
extern "C" {
#endif

VOID AvbTestUser_Main_KmBuildPlaceHolder(void)
{
    // Intentionally empty; user-mode app is built separately via nmake
}

#ifdef __cplusplus
}
#endif

// End of kernel-mode placeholder
// The actual user-mode implementation is built separately using nmake -f tools\avb_test\avb_test.mak
