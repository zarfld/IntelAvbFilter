/*++

Module Name:

    avb_test_i210.c

Abstract:

    Simple test application for Intel AVB Filter Driver I210 validation
    Tests device detection and basic hardware access via IOCTLs

--*/

// NOTE: Kernel build placeholder only. The real user-mode test binary is
// built from avb_test_i210_um.c via VS Code task (build-test-i210) which
// uses the shared IOCTL ABI header include/avb_ioctl.h without pulling WDK headers.

#include "precomp.h"

VOID AvbTestI210_DriverBuildPlaceHolder(void)
{
    // Intentionally empty for KM build
}
