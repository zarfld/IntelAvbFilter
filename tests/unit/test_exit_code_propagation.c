/*
 * test_exit_code_propagation.c
 * Mock executable for verifying Run-Tests.ps1 exit-code propagation.
 *
 * Usage:
 *   test_exit_code_propagation.exe --exit 0   -> exits 0  (PASS)
 *   test_exit_code_propagation.exe --exit 1   -> exits 1  (FAIL)
 *   test_exit_code_propagation.exe --exit 2   -> exits 2  (NO_TESTS)
 *   test_exit_code_propagation.exe --exit 3   -> exits 3  (BLOCKED)
 *   test_exit_code_propagation.exe --exit 4   -> exits 4  (CLEANUP_FAILED)
 *
 * Also emits a [PASS]/[FAIL]/[SKIP]/[BLOCKED]/[CLEANUP_FAILED] line so
 * Parse-TestResults.ps1 can read it.
 *
 * Issue #328 / P0 exit-code propagation verification
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *exit_label(int code) {
    switch (code) {
        case 0: return "PASS";
        case 1: return "FAIL";
        case 2: return "SKIP_NO_TESTS";
        case 3: return "BLOCKED";
        case 4: return "CLEANUP_FAILED";
        default: return "UNKNOWN";
    }
}

int main(int argc, char *argv[]) {
    int exit_code = 0;  /* default: PASS */

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--exit") == 0 || strcmp(argv[i], "-exit") == 0)
                && i + 1 < argc) {
            exit_code = atoi(argv[++i]);
        }
    }

    printf("=======================================================================\n");
    printf(" Mock Exit-Code Test — propagation verification (#328 P0)\n");
    printf(" Requested exit code: %d (%s)\n", exit_code, exit_label(exit_code));
    printf("=======================================================================\n");

    /* Emit a machine-readable verdict line matching Run-Tests.ps1 expectations */
    switch (exit_code) {
        case 0:
            printf("  [PASS] TC-EXITCODE-MOCK: exit %d (%s)\n", exit_code, exit_label(exit_code));
            break;
        case 1:
            printf("  [FAIL] TC-EXITCODE-MOCK: exit %d (%s)\n", exit_code, exit_label(exit_code));
            break;
        case 2:
            printf("  [SKIP] TC-EXITCODE-MOCK: exit %d (%s)\n", exit_code, exit_label(exit_code));
            break;
        case 3:
            printf("  [BLOCKED] TC-EXITCODE-MOCK: exit %d (%s)\n", exit_code, exit_label(exit_code));
            break;
        case 4:
            printf("  [CLEANUP_FAILED] TC-EXITCODE-MOCK: exit %d (%s)\n", exit_code, exit_label(exit_code));
            break;
        default:
            printf("  [FAIL] TC-EXITCODE-MOCK: exit %d (unknown code)\n", exit_code);
            break;
    }

    printf("=======================================================================\n");
    return exit_code;
}
