/*++

Module Name:

    avb_test_i210.c

Abstract:

    Simple test application for Intel AVB Filter Driver I210 validation
    Tests device detection and basic hardware access via IOCTLs

--*/

// This file is included in the kernel-mode driver project. To avoid user-mode
// header conflicts under the WindowsKernelModeDriver toolset, compile only a
// no-op placeholder here. Build the actual user-mode test tool in a separate
// user-mode project (see avb_test.mak) that does not include precomp.h.

#include "precomp.h"

VOID AvbTestI210_DriverBuildPlaceHolder(void)
{
    // Intentionally empty for KM build
}
