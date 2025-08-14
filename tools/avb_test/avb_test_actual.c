#include "precomp.h"

#ifndef VOID
#define VOID void
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Kernel-mode placeholder for user-mode AVB test files
// Real user-mode tool is built separately via nmake and NOT included in the driver build.
VOID AvbTestActual_KmBuildPlaceHolder(void)
{
    // Intentionally empty
}

#ifdef __cplusplus
}
#endif