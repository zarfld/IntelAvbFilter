#include "precomp.h"

// Kernel-mode placeholder for the user-mode TSN IOCTL Handler verification test.
// The real user-mode test is built separately via nmake from tools/avb_test/test_tsn_ioctl_handlers_um.c
// using the makefile tools/avb_test/tsn_ioctl_test.mak

VOID TestTsnIoctlHandlers_KmBuildPlaceHolder(void)
{
    // Intentionally empty to satisfy KM precompiled-header build
}