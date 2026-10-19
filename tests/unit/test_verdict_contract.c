/*
 * test_verdict_contract.c
 * Offline characterization of the testcase verdict contract (docs/testing/test-verdict-contract.md).
 *
 * Maps a raw NTSTATUS plus evidence about the OPERATION's hardware capability, payload
 * validity and safety to exactly one of:
 *     PASS | FAIL | SKIP_UNSUPPORTED | SKIP_NOT_IMPLEMENTED | BLOCKED | INCONCLUSIVE
 * and checks that the evidence line the tests print keeps testcase id, adapter, PCI device
 * id and the ORIGINAL NTSTATUS (the PowerShell parsers read the same format).
 *
 * No driver, no hardware, no elevated privileges.
 *
 * Implements: test verdict taxonomy
 * Verifies:   #340 (fail-closed verification), #9 / #206 (TAS: unsafe restore -> BLOCKED)
 * Traces to:  #199, #35
 *
 * Build:
 *   cl /nologo /W3 /Zi -I include tests\unit\test_verdict_contract.c /Fe:test_verdict_contract.exe
 * Exit codes: 0 = all pass, 1 = at least one failure
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../common/avb_verdict.h"
#include "../common/avb_ntstatus.h"

static int g_run = 0, g_fail = 0;

#define CHECK_V(label, got, want) do { \
    g_run++; \
    if ((got) == (want)) { printf("  [PASS] %s\n", (label)); } \
    else { g_fail++; printf("  [FAIL] %s: got=%s want=%s\n", (label), \
           avb_verdict_tag(got), avb_verdict_tag(want)); } \
} while (0)

#define CHECK_TRUE(label, expr) do { \
    g_run++; \
    if (expr) { printf("  [PASS] %s\n", (label)); } \
    else { g_fail++; printf("  [FAIL] %s\n", (label)); } \
} while (0)

/* Shorthands: (status, capability evidence for THIS operation, payload valid, safe) */
#define V(st, cap, pl, safe) avb_verdict_from_status((st), (cap), (pl), (safe))

int main(void)
{
    printf("=======================================================================\n");
    printf(" Test verdict contract - offline characterization\n");
    printf(" Verifies: #340, #9, #206 | no driver, no hardware\n");
    printf("=======================================================================\n");

    /* 1. Proven hardware absence + STATUS_NOT_SUPPORTED -> SKIP_UNSUPPORTED */
    printf("\n[V-001] hardware capability proven absent\n");
    CHECK_V("NOT_SUPPORTED + cap absent => SKIP_UNSUPPORTED",
            V(STATUS_NOT_SUPPORTED, AVB_CAP_ABSENT, 0, 1), AVB_VERDICT_SKIP_UNSUPPORTED);

    /* 2. Supported hardware, driver op missing: STATUS_NOT_IMPLEMENTED -> SKIP_NOT_IMPLEMENTED,
     *    never FAIL / SKIP_UNSUPPORTED / PASS, whatever the capability evidence says. */
    printf("\n[V-002] driver operation not implemented (hardware supports it)\n");
    CHECK_V("NOT_IMPLEMENTED + cap present => SKIP_NOT_IMPLEMENTED",
            V(STATUS_NOT_IMPLEMENTED, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_SKIP_NOT_IMPLEMENTED);
    CHECK_V("NOT_IMPLEMENTED + cap unknown => SKIP_NOT_IMPLEMENTED (not converted)",
            V(STATUS_NOT_IMPLEMENTED, AVB_CAP_UNKNOWN, 0, 1), AVB_VERDICT_SKIP_NOT_IMPLEMENTED);
    CHECK_V("NOT_IMPLEMENTED + cap absent => SKIP_NOT_IMPLEMENTED (not SKIP_UNSUPPORTED)",
            V(STATUS_NOT_IMPLEMENTED, AVB_CAP_ABSENT, 0, 1), AVB_VERDICT_SKIP_NOT_IMPLEMENTED);

    /* 3. Supported hardware, implemented operation fails with an actual error status -> FAIL */
    printf("\n[V-003] implemented operation fails\n");
    CHECK_V("INVALID_PARAMETER + cap present => FAIL",
            V(STATUS_INVALID_PARAMETER, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_FAIL);
    CHECK_V("UNSUCCESSFUL + cap present => FAIL",
            V(STATUS_UNSUCCESSFUL, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_FAIL);
    CHECK_V("INVALID_DEVICE_REQUEST (IOCTL unknown to this driver build) => FAIL",
            V(STATUS_INVALID_DEVICE_REQUEST, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_FAIL);
    CHECK_V("BUFFER_OVERFLOW (warning severity, truncated) => FAIL",
            V(STATUS_BUFFER_OVERFLOW, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_FAIL);

    /* 4. IOCTL succeeds but payload/provenance invalid -> FAIL */
    printf("\n[V-004] success status, invalid result\n");
    CHECK_V("SUCCESS + payload invalid => FAIL",
            V(STATUS_SUCCESS, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_FAIL);
    CHECK_V("SUCCESS + payload valid => PASS",
            V(STATUS_SUCCESS, AVB_CAP_PRESENT, 1, 1), AVB_VERDICT_PASS);

    /* 5. Device temporarily unavailable: never an unsupported-hardware SKIP */
    printf("\n[V-005] device temporarily unavailable\n");
    CHECK_V("DEVICE_NOT_READY + cap absent => BLOCKED (not SKIP_UNSUPPORTED)",
            V(STATUS_DEVICE_NOT_READY, AVB_CAP_ABSENT, 0, 1), AVB_VERDICT_BLOCKED);
    CHECK_V("DEVICE_NOT_READY + cap present => BLOCKED",
            V(STATUS_DEVICE_NOT_READY, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_BLOCKED);
    CHECK_V("INVALID_DEVICE_STATE => BLOCKED",
            V(STATUS_INVALID_DEVICE_STATE, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_BLOCKED);

    /* 7. Unsafe prerequisite (e.g. no safe TAS restore path) -> BLOCKED even if the call "worked" */
    printf("\n[V-007] unsafe precondition\n");
    CHECK_V("unsafe + SUCCESS => BLOCKED", V(STATUS_SUCCESS, AVB_CAP_PRESENT, 1, 0), AVB_VERDICT_BLOCKED);
    CHECK_V("unsafe + NOT_SUPPORTED => BLOCKED", V(STATUS_NOT_SUPPORTED, AVB_CAP_ABSENT, 0, 0), AVB_VERDICT_BLOCKED);

    /* D-D: a generic capability mask is not evidence for a specific operation */
    printf("\n[V-008] capability evidence for the operation\n");
    CHECK_V("NOT_SUPPORTED + cap present (advertised) => FAIL (contract violation)",
            V(STATUS_NOT_SUPPORTED, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_FAIL);
    CHECK_V("NOT_SUPPORTED + cap unknown => INCONCLUSIVE (never SKIP_UNSUPPORTED)",
            V(STATUS_NOT_SUPPORTED, AVB_CAP_UNKNOWN, 0, 1), AVB_VERDICT_INCONCLUSIVE);
    CHECK_V("status not captured => INCONCLUSIVE",
            V(AVB_NTSTATUS_UNAVAILABLE, AVB_CAP_PRESENT, 0, 1), AVB_VERDICT_INCONCLUSIVE);

    /* Distinct, stable tags (the PowerShell parsers key on these exact strings) */
    printf("\n[V-009] tags\n");
    CHECK_TRUE("PASS tag", strcmp(avb_verdict_tag(AVB_VERDICT_PASS), "PASS") == 0);
    CHECK_TRUE("FAIL tag", strcmp(avb_verdict_tag(AVB_VERDICT_FAIL), "FAIL") == 0);
    CHECK_TRUE("SKIP_UNSUPPORTED tag", strcmp(avb_verdict_tag(AVB_VERDICT_SKIP_UNSUPPORTED), "SKIP_UNSUPPORTED") == 0);
    CHECK_TRUE("SKIP_NOT_IMPLEMENTED tag", strcmp(avb_verdict_tag(AVB_VERDICT_SKIP_NOT_IMPLEMENTED), "SKIP_NOT_IMPLEMENTED") == 0);
    CHECK_TRUE("BLOCKED tag", strcmp(avb_verdict_tag(AVB_VERDICT_BLOCKED), "BLOCKED") == 0);
    CHECK_TRUE("INCONCLUSIVE tag", strcmp(avb_verdict_tag(AVB_VERDICT_INCONCLUSIVE), "INCONCLUSIVE") == 0);

    /* Evidence line: testcase id, adapter identity, PCI device id and the ORIGINAL ntstatus survive */
    printf("\n[V-010] evidence line\n");
    {
        char line[256];
        avb_verdict_format(line, sizeof(line), AVB_VERDICT_SKIP_NOT_IMPLEMENTED,
                           "TC-TAS-009", 2, 0x125B, STATUS_NOT_IMPLEMENTED, "DISARM_TAS",
                           "safe TAS disable not implemented");
        CHECK_TRUE("line starts with [SKIP_NOT_IMPLEMENTED] TC-TAS-009/adapter:2:",
                   strncmp(line, "[SKIP_NOT_IMPLEMENTED] TC-TAS-009/adapter:2:", 44) == 0);
        CHECK_TRUE("line keeps did=0x125B", strstr(line, "did=0x125B") != NULL);
        CHECK_TRUE("line keeps ntstatus=0xC0000002", strstr(line, "ntstatus=0xC0000002") != NULL);
        CHECK_TRUE("line keeps op=DISARM_TAS", strstr(line, "op=DISARM_TAS") != NULL);
        printf("    sample: %s\n", line);

        avb_verdict_format(line, sizeof(line), AVB_VERDICT_INCONCLUSIVE,
                           "TC-X-001", -1, 0x15BB, AVB_NTSTATUS_UNAVAILABLE, "GET_TX_TIMESTAMP", "status not captured");
        CHECK_TRUE("no adapter index => no /adapter suffix", strstr(line, "/adapter") == NULL);
        CHECK_TRUE("unavailable ntstatus is explicit, not 0", strstr(line, "ntstatus=unavailable") != NULL);
    }

    /* Why the exact NTSTATUS must be captured where a verdict depends on it.
     * RtlNtStatusToDosError is the mapping DeviceIoControl/GetLastError uses. */
    printf("\n[V-011] raw NTSTATUS capture vs lossy GetLastError()\n");
    {
        typedef ULONG (NTAPI *PFN_Dos)(NTSTATUS);
        PFN_Dos dos = (PFN_Dos)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError");
        CHECK_TRUE("RtlNtStatusToDosError resolvable", dos != NULL);
        if (dos) {
            CHECK_TRUE("STATUS_NOT_IMPLEMENTED -> Win32 1 (ERROR_INVALID_FUNCTION)",
                       dos(STATUS_NOT_IMPLEMENTED) == ERROR_INVALID_FUNCTION);
            CHECK_TRUE("STATUS_INVALID_DEVICE_REQUEST -> the SAME Win32 1 (lossy: indistinguishable)",
                       dos(STATUS_INVALID_DEVICE_REQUEST) == dos(STATUS_NOT_IMPLEMENTED));
            CHECK_TRUE("STATUS_NOT_SUPPORTED -> Win32 50 (ERROR_NOT_SUPPORTED)",
                       dos(STATUS_NOT_SUPPORTED) == ERROR_NOT_SUPPORTED);
        }

        /* Raw capture from an in-box synchronous device with an IOCTL it does not handle.
         * (Never the AVB driver: this test performs no driver access.) */
        HANDLE nul = CreateFileW(L"\\\\.\\NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK_TRUE("opened \\\\.\\NUL", nul != INVALID_HANDLE_VALUE);
        if (nul != INVALID_HANDLE_VALUE) {
            BYTE out[16] = {0}; DWORD br = 123;
            LONG st = avb_ioctl_ntstatus(nul, CTL_CODE(0x22, 0xABC, METHOD_BUFFERED, FILE_ANY_ACCESS),
                                         NULL, 0, out, sizeof(out), &br);
            printf("    observed raw NTSTATUS from NUL: 0x%08X\n", (unsigned)st);
            CHECK_TRUE("status captured (not unavailable)", st != AVB_NTSTATUS_UNAVAILABLE);
            CHECK_TRUE("error severity (0xC...)", ((ULONG)st & 0xC0000000u) == 0xC0000000u);
            CHECK_TRUE("exact NTSTATUS, not a Win32 code", (ULONG)st > 0xFFFFu);
            CHECK_TRUE("matches DeviceIoControl+GetLastError mapping",
                       dos && (DeviceIoControl(nul, CTL_CODE(0x22, 0xABC, METHOD_BUFFERED, FILE_ANY_ACCESS),
                               NULL, 0, out, sizeof(out), &br, NULL) == FALSE) && GetLastError() == dos(st));
            CloseHandle(nul);
        }
    }

    printf("\n=======================================================================\n");
    printf(" PASS: %d / %d\n", g_run - g_fail, g_run);
    printf(" FAIL: %d / %d\n", g_fail, g_run);
    printf("=======================================================================\n");
    if (g_fail > 0) { printf("[RESULT] FAILURE\n"); return 1; }
    printf("[RESULT] ALL VERDICT CONTRACT TESTS PASSED\n");
    return 0;
}
