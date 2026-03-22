/**
 * @file test_scripts_consolidate.c
 * @brief Script Consolidation & Architecture Structural Verification
 *
 * Test ID:   TEST-SCRIPTS-CONSOLIDATE-001
 * Implements: #292 (TEST-SCRIPTS-CONSOLIDATE-001: Script Consolidation Test)
 * Verifies:   #27  (REQ-NF-SCRIPTS-001: Consolidated Script Architecture)
 *
 * Checks that the repo script architecture is consolidated as required:
 *   TC-SCRIPTS-001: Canonical script count after consolidation
 *   TC-SCRIPTS-003: Consolidated build script (Build-Driver.ps1) exists
 *   TC-SCRIPTS-004: Consolidated install script (Install-Driver.ps1) exists
 *   TC-SCRIPTS-005: Consolidated test runner (Run-Tests.ps1) exists
 *   TC-SCRIPTS-006: Deprecated scripts archived (tools\archive\deprecated)
 *   TC-SCRIPTS-007: Documentation coverage (README.md in key tool dirs)
 *   TC-SCRIPTS-008: CI/CD uses canonical scripts (workflows dir present)
 *   TC-SCRIPTS-009: Error handling in canonical scripts ($ErrorActionPreference)
 *
 * Run via infrastructure only:
 *   .\tools\test\Run-Tests-Elevated.ps1 -TestName test_scripts_consolidate.exe
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * Repo root (computed from exe location: 5 levels up from build output dir)
 * ========================================================================= */
static char g_repo[MAX_PATH];

static int compute_repo_root(void)
{
    char exe[MAX_PATH];
    char *p;
    int  i;

    if (!GetModuleFileNameA(NULL, exe, sizeof(exe)))
        return 0;

    p = strrchr(exe, '\\');
    if (p) *p = '\0';

    for (i = 0; i < 5; i++) {
        p = strrchr(exe, '\\');
        if (!p) return 0;
        *p = '\0';
    }

    strncpy(g_repo, exe, MAX_PATH - 1);
    g_repo[MAX_PATH - 1] = '\0';
    return 1;
}

/* =========================================================================
 * Test framework
 * ========================================================================= */
static int g_total  = 0;
static int g_passed = 0;
static int g_failed = 0;

#define PASS(id, fmt, ...) \
    do { g_total++; g_passed++; \
         printf("  PASS: " id ": " fmt "\n", ##__VA_ARGS__); } while (0)

#define FAIL(id, fmt, ...) \
    do { g_total++; g_failed++; \
         printf("  FAIL: " id ": " fmt "\n", ##__VA_ARGS__); } while (0)

/* =========================================================================
 * Helpers
 * ========================================================================= */

static void mk(char *out, const char *rel)
{
    snprintf(out, MAX_PATH, "%s\\%s", g_repo, rel);
}

static int fexists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dexists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Count files (non-recursive) in a directory */
static int count_files(const char *dir)
{
    char             pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE           h;
    int              n = 0;

    snprintf(pat, MAX_PATH, "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

/*
 * Count script files (.ps1, .bat, .cmd) recursively under dir,
 * excluding subdirectory *names* listed in excl[].
 */
static int count_scripts_recursive(const char *dir,
                                   const char **excl, int nexcl)
{
    char             pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE           h;
    int              n = 0;
    int              i;

    snprintf(pat, MAX_PATH, "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        int  skip = 0;
        char sub[MAX_PATH];

        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        for (i = 0; i < nexcl; i++) {
            if (_stricmp(fd.cFileName, excl[i]) == 0) { skip = 1; break; }
        }
        if (skip) continue;

        snprintf(sub, MAX_PATH, "%s\\%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            n += count_scripts_recursive(sub, excl, nexcl);
        } else {
            const char *ext = strrchr(fd.cFileName, '.');
            if (ext &&
                (_stricmp(ext, ".ps1") == 0 ||
                 _stricmp(ext, ".bat") == 0 ||
                 _stricmp(ext, ".cmd") == 0))
                n++;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return n;
}

/* Check if a file contains a given plaintext needle (reads first 64 KB) */
static int file_contains(const char *path, const char *needle)
{
    static char  buf[65537];
    HANDLE       h;
    DWORD        nr;

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &nr, NULL)) {
        CloseHandle(h); return 0;
    }
    CloseHandle(h);
    buf[nr] = '\0';
    return strstr(buf, needle) != NULL;
}

/* =========================================================================
 * Test cases
 * ========================================================================= */

/*
 * TC-SCRIPTS-001: Canonical script count < 25 (consolidated from 80+)
 * Counts .ps1/.bat/.cmd in tools\, excluding archive and lib subdirs.
 */
static void tc_script_count(void)
{
    char        tools_dir[MAX_PATH];
    const char *excl[] = { "archive", "lib", "scripts" };
    int         n;
    char        msg[128];

    printf("\n--- TC-SCRIPTS-001: Canonical script count --\n");
    mk(tools_dir, "tools");

    if (!dexists(tools_dir)) {
        FAIL("TC-SCRIPTS-001", "tools\\ directory not found");
        return;
    }

    n = count_scripts_recursive(tools_dir, excl, 3);
    snprintf(msg, sizeof(msg), "%d canonical scripts in tools\\ (target <25)", n);

    if (n < 25)   PASS("TC-SCRIPTS-001", "%s", msg);
    else          FAIL("TC-SCRIPTS-001", "%s", msg);
}

/*
 * TC-SCRIPTS-003: Consolidated build scripts present
 */
static void tc_build_scripts(void)
{
    char path[MAX_PATH];

    printf("\n--- TC-SCRIPTS-003: Consolidated build scripts --\n");

    mk(path, "tools\\build\\Build-Driver.ps1");
    if (fexists(path)) PASS("TC-SCRIPTS-003", "tools\\build\\Build-Driver.ps1 exists");
    else               FAIL("TC-SCRIPTS-003", "tools\\build\\Build-Driver.ps1 NOT FOUND");

    mk(path, "tools\\build\\Build-Tests.ps1");
    if (fexists(path)) PASS("TC-SCRIPTS-003", "tools\\build\\Build-Tests.ps1 exists");
    else               FAIL("TC-SCRIPTS-003", "tools\\build\\Build-Tests.ps1 NOT FOUND");

    mk(path, "tools\\build\\Build-And-Sign.ps1");
    if (fexists(path)) PASS("TC-SCRIPTS-003", "tools\\build\\Build-And-Sign.ps1 exists");
    else               FAIL("TC-SCRIPTS-003", "tools\\build\\Build-And-Sign.ps1 NOT FOUND");
}

/*
 * TC-SCRIPTS-004: Consolidated install script present
 */
static void tc_install_scripts(void)
{
    char path[MAX_PATH];

    printf("\n--- TC-SCRIPTS-004: Consolidated install script --\n");

    mk(path, "tools\\setup\\Install-Driver.ps1");
    if (fexists(path)) PASS("TC-SCRIPTS-004", "tools\\setup\\Install-Driver.ps1 exists");
    else               FAIL("TC-SCRIPTS-004", "tools\\setup\\Install-Driver.ps1 NOT FOUND");

    mk(path, "tools\\setup\\Install-Driver-Elevated.ps1");
    if (fexists(path)) PASS("TC-SCRIPTS-004", "tools\\setup\\Install-Driver-Elevated.ps1 exists");
    else               FAIL("TC-SCRIPTS-004", "tools\\setup\\Install-Driver-Elevated.ps1 NOT FOUND");
}

/*
 * TC-SCRIPTS-005: Consolidated test runner present
 */
static void tc_test_runner(void)
{
    char path[MAX_PATH];

    printf("\n--- TC-SCRIPTS-005: Consolidated test runner --\n");

    mk(path, "tools\\test\\Run-Tests.ps1");
    if (fexists(path)) PASS("TC-SCRIPTS-005", "tools\\test\\Run-Tests.ps1 exists");
    else               FAIL("TC-SCRIPTS-005", "tools\\test\\Run-Tests.ps1 NOT FOUND");

    mk(path, "tools\\test\\Run-Tests-Elevated.ps1");
    if (fexists(path)) PASS("TC-SCRIPTS-005", "tools\\test\\Run-Tests-Elevated.ps1 exists");
    else               FAIL("TC-SCRIPTS-005", "tools\\test\\Run-Tests-Elevated.ps1 NOT FOUND");
}

/*
 * TC-SCRIPTS-006: Deprecated scripts archived
 */
static void tc_deprecated_archived(void)
{
    char path[MAX_PATH];
    char readme[MAX_PATH];
    int  n;
    char msg[128];

    printf("\n--- TC-SCRIPTS-006: Deprecated scripts archived --\n");

    mk(path, "tools\\archive\\deprecated");
    if (!dexists(path)) {
        FAIL("TC-SCRIPTS-006", "tools\\archive\\deprecated\\ directory missing");
        return;
    }
    PASS("TC-SCRIPTS-006", "tools\\archive\\deprecated\\ directory exists");

    n = count_files(path);
    snprintf(msg, sizeof(msg), "%d files in tools\\archive\\deprecated\\ root", n);
    if (n >= 1) PASS("TC-SCRIPTS-006", "%s", msg);
    else        FAIL("TC-SCRIPTS-006", "%s (expected >=1)", msg);

    snprintf(readme, MAX_PATH, "%s\\README.md", path);
    if (fexists(readme)) PASS("TC-SCRIPTS-006", "tools\\archive\\deprecated\\README.md exists");
    else                 FAIL("TC-SCRIPTS-006", "tools\\archive\\deprecated\\README.md missing");
}

/*
 * TC-SCRIPTS-007: Documentation READMEs in key tool directories
 */
static void tc_documentation(void)
{
    static const char *readme_paths[] = {
        "tools\\build\\README.md",
        "tools\\setup\\README.md",
        "tools\\test\\README.md",
        "tools\\development\\README.md"
    };
    char path[MAX_PATH];
    int  found = 0;
    int  i;
    char msg[128];

    printf("\n--- TC-SCRIPTS-007: Tool directory documentation --\n");

    for (i = 0; i < 4; i++) {
        mk(path, readme_paths[i]);
        if (fexists(path)) {
            printf("    present: %s\n", readme_paths[i]);
            found++;
        } else {
            printf("    missing: %s\n", readme_paths[i]);
        }
    }

    snprintf(msg, sizeof(msg), "%d/4 tool READMEs present", found);
    if (found >= 2) PASS("TC-SCRIPTS-007", "%s", msg);
    else            FAIL("TC-SCRIPTS-007", "%s (need >=2)", msg);
}

/*
 * TC-SCRIPTS-008: CI/CD workflows directory present
 */
static void tc_cicd_integration(void)
{
    char path[MAX_PATH];

    printf("\n--- TC-SCRIPTS-008: CI/CD workflows --\n");

    mk(path, ".github\\workflows");
    if (dexists(path)) PASS("TC-SCRIPTS-008", ".github\\workflows\\ directory exists");
    else               FAIL("TC-SCRIPTS-008", ".github\\workflows\\ directory missing");
}

/*
 * TC-SCRIPTS-009: Canonical scripts have error handling ($ErrorActionPreference)
 */
static void tc_error_handling(void)
{
    static const char *scripts[] = {
        "tools\\build\\Build-Driver.ps1",
        "tools\\build\\Build-Tests.ps1",
        "tools\\test\\Run-Tests.ps1"
    };
    char path[MAX_PATH];
    int  i;

    printf("\n--- TC-SCRIPTS-009: Error handling in canonical scripts --\n");

    for (i = 0; i < 3; i++) {
        mk(path, scripts[i]);
        if (!fexists(path)) {
            FAIL("TC-SCRIPTS-009", "%s not found", scripts[i]);
            continue;
        }
        if (file_contains(path, "$ErrorActionPreference"))
            PASS("TC-SCRIPTS-009", "%s has $ErrorActionPreference", scripts[i]);
        else
            FAIL("TC-SCRIPTS-009", "%s missing $ErrorActionPreference", scripts[i]);
    }
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    printf("=== TEST-SCRIPTS-CONSOLIDATE-001: Script Consolidation Verification ===\n");
    printf("Verifies: #27 (REQ-NF-SCRIPTS-001), #292\n");

    if (!compute_repo_root()) {
        printf("ERROR: failed to compute repo root\n");
        return 1;
    }
    printf("Repo root: %s\n", g_repo);

    tc_script_count();
    tc_build_scripts();
    tc_install_scripts();
    tc_test_runner();
    tc_deprecated_archived();
    tc_documentation();
    tc_cicd_integration();
    tc_error_handling();

    printf("\n=== RESULTS ===\n");
    printf("Total: %d  PASS: %d  FAIL: %d\n", g_total, g_passed, g_failed);

    if (g_failed == 0) {
        printf("OVERALL: PASS\n");
        return 0;
    }
    printf("OVERALL: FAIL\n");
    return 1;
}
