#include "precomp.h"

#ifndef VOID
#define VOID void
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Kernel-mode placeholder for the user-mode AVB test tool.
// The real user-mode tool is built separately via nmake and is not part of the driver build.
VOID AvbTestActual_KmBuildPlaceHolder(void)
{
    // Intentionally empty
}

#ifdef __cplusplus
}
#endif