/**
 * @file test_cleanup_archive.c
 * @brief Repo Archival & Organization Structural Verification
 *
 * Test ID:   TEST-CLEANUP-ARCHIVE-001
 * Implements: #294 (TEST-CLEANUP-ARCHIVE-001: Obsolete File Archival/Repo Org)
 * Verifies:   #293 (REQ-NF-CLEANUP-002: Archive Obsolete/Redundant Source Files)
 *
 *   TC-CLEANUP-001: Archive directories exist and have README.md
 *   TC-CLEANUP-002: Known archived files are present in archive (not lost)
 *   TC-CLEANUP-003: Archived filenames not found in active source paths
 *   TC-CLEANUP-004: No dirty naming patterns in active source tree
 *   TC-CLEANUP-005: tools\archive\deprecated substructure intact
 *   TC-CLEANUP-006: No unexpected .c/.h files at repo root
 *   TC-CLEANUP-007: No stray scripts (.ps1/.py/.bat/.cmd/.sh) at repo root
 *
 * Run via infrastructure only:
 *   .\tools\test\Run-Tests-Elevated.ps1 -TestName test_cleanup_archive.exe
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
    char  exe[MAX_PATH];
    char *p;
    int   i;

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

/* Count all files (non-recursive) in a directory */
static int count_files_in_dir(const char *dir)
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
 * Check whether a filename (just the file name part) exists anywhere under
 * dir (recursively), excluding subdirectory *names* in excl[].
 * Returns 1 if found, 0 if not.
 */
static int find_file_recursive(const char *dir, const char *filename,
                                const char **excl, int nexcl)
{
    char             pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE           h;
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
            if (find_file_recursive(sub, filename, excl, nexcl)) {
                FindClose(h);
                return 1;
            }
        } else {
            if (_stricmp(fd.cFileName, filename) == 0) {
                FindClose(h);
                return 1;
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return 0;
}

/*
 * Check if a filename matches one of the dirty naming patterns.
 * Returns 1 if the name matches a dirty pattern, 0 otherwise.
 */
static int is_dirty_name(const char *name)
{
    /* Only check .c and .h files */
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    if (_stricmp(dot, ".c") != 0 && _stricmp(dot, ".h") != 0)
        return 0;

    /* Check each suffix pattern before the extension */
    static const char *dirty_suffixes[] = {
        "_old", "_backup", "_v2", "_v3", "_new",
        "_fixed_new", "_fixed_complete",
        "_temp", "_final", "_copy",
        "_old2", "_backup2"
    };
    int i;
    int base_len = (int)(dot - name);

    for (i = 0; i < (int)(sizeof(dirty_suffixes) / sizeof(dirty_suffixes[0])); i++) {
        int slen = (int)strlen(dirty_suffixes[i]);
        if (base_len >= slen &&
            _strnicmp(name + base_len - slen, dirty_suffixes[i], slen) == 0)
            return 1;
    }

    /* Also catch files ending in a digit before the extension (e.g. filter2.c) */
    if (base_len >= 1 && isdigit((unsigned char)name[base_len - 1]))
        return 1;

    return 0;
}

/*
 * Scan dir recursively for dirty-named .c/.h files,
 * excluding subdirectory names in excl[].
 * Returns count found (and prints each).
 */
static int find_dirty_names(const char *dir, const char **excl, int nexcl)
{
    char             pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE           h;
    int              i, total = 0;

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
            total += find_dirty_names(sub, excl, nexcl);
        } else {
            if (is_dirty_name(fd.cFileName)) {
                printf("    dirty: %s\n", sub + (int)strlen(g_repo) + 1);
                total++;
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return total;
}

/* =========================================================================
 * TC-CLEANUP-001: Archive directories exist and have README.md
 * ========================================================================= */
static void tc_cleanup_001(void)
{
    static const char *archive_dirs[] = {
        "archive\\filter_old",
        "archive\\backup",
        "archive\\avb_integration_old",
        "tools\\archive\\deprecated"
    };
    char dir[MAX_PATH], readme[MAX_PATH];
    int  i;

    printf("\n--- TC-CLEANUP-001: Archive directories have README.md ---\n");

    for (i = 0; i < 4; i++) {
        mk(dir, archive_dirs[i]);
        if (!dexists(dir)) {
            FAIL("TC-CLEANUP-001", "%s\\ missing", archive_dirs[i]);
            continue;
        }
        snprintf(readme, MAX_PATH, "%s\\README.md", dir);
        if (fexists(readme))
            PASS("TC-CLEANUP-001", "%s\\README.md exists", archive_dirs[i]);
        else
            FAIL("TC-CLEANUP-001", "%s exists but README.md missing", archive_dirs[i]);
    }
}

/* =========================================================================
 * TC-CLEANUP-002: Known archived files present in archive (not lost)
 * ========================================================================= */
static void tc_cleanup_002(void)
{
    static const char *archived[] = {
        "archive\\filter_old\\filter_august.c",
        "archive\\filter_old\\filter_august.txt",
        "archive\\backup\\temp_filter_7624c57.c",
        "archive\\backup\\intel_tsn_enhanced_implementations.c",
        "archive\\backup\\tsn_hardware_activation_investigation.c",
        "archive\\avb_integration_old\\avb_integration.c",
        "archive\\avb_integration_old\\avb_context_management.c",
        "archive\\avb_integration_old\\avb_integration_fixed_backup.c",
        "archive\\avb_integration_old\\avb_integration_fixed_complete.c",
        "archive\\avb_integration_old\\avb_integration_fixed_new.c",
        "archive\\avb_integration_old\\avb_integration_hardware_only.c"
    };
    char path[MAX_PATH];
    int  i;

    printf("\n--- TC-CLEANUP-002: Archived files present in archive ---\n");

    for (i = 0; i < 11; i++) {
        mk(path, archived[i]);
        if (fexists(path))
            PASS("TC-CLEANUP-002", "present: %s", archived[i]);
        else
            FAIL("TC-CLEANUP-002", "MISSING: %s", archived[i]);
    }
}

/* =========================================================================
 * TC-CLEANUP-003: Archived filenames not in active source paths
 * Searches: repo root (non-recursive) + src\ (recursive)
 * ========================================================================= */
static void tc_cleanup_003(void)
{
    static const char *archived_names[] = {
        "filter_august.c",
        "temp_filter_7624c57.c",
        "intel_tsn_enhanced_implementations.c",
        "tsn_hardware_activation_investigation.c",
        "avb_context_management.c",
        "avb_integration_fixed_backup.c",
        "avb_integration_fixed_complete.c",
        "avb_integration_fixed_new.c",
        "avb_integration_hardware_only.c"
    };
    /* src\ recursive, no exclusions needed for these specific names */
    static const char *no_excl[] = { NULL };
    char        src_dir[MAX_PATH];
    char        root_check[MAX_PATH];
    int         i, leaks = 0;

    printf("\n--- TC-CLEANUP-003: Archived filenames not in active source ---\n");

    mk(src_dir, "src");

    for (i = 0; i < 9; i++) {
        int  leaked = 0;

        /* Check repo root (non-recursive: just look for <root>\<name>) */
        snprintf(root_check, MAX_PATH, "%s\\%s", g_repo, archived_names[i]);
        if (fexists(root_check)) leaked = 1;

        /* Check src\ recursively */
        if (!leaked && dexists(src_dir)) {
            if (find_file_recursive(src_dir, archived_names[i], no_excl, 0))
                leaked = 1;
        }

        if (leaked) {
            FAIL("TC-CLEANUP-003", "LEAKED into active source: %s", archived_names[i]);
            leaks++;
        }
    }
    if (leaks == 0)
        PASS("TC-CLEANUP-003", "No archived filenames found in active source paths");
}

/* =========================================================================
 * TC-CLEANUP-004: No dirty naming patterns in active source tree
 * Scans repo root + src\ + devices\, excluding archive/build/external/etc.
 * ========================================================================= */
static void tc_cleanup_004(void)
{
    static const char *excl[] = {
        "archive", "build", "external", ".git", ".venv",
        "node_modules", "intel-ethernet-regs", "intel_avb", "wil",
        "logs", "tests"
    };
    int nexcl = (int)(sizeof(excl) / sizeof(excl[0]));
    int total = 0;

    printf("\n--- TC-CLEANUP-004: No dirty naming patterns in active sources ---\n");

    /* Scan from repo root, exclusions handle the archived/external trees */
    total = find_dirty_names(g_repo, excl, nexcl);

    if (total == 0)
        PASS("TC-CLEANUP-004", "No dirty-named files found in active source tree");
    else
        FAIL("TC-CLEANUP-004", "%d dirty-named file(s) in active source tree", total);
}

/* =========================================================================
 * TC-CLEANUP-005: tools\archive\deprecated substructure intact
 * ========================================================================= */
static void tc_cleanup_005(void)
{
    static const char *subdirs[] = { "development", "setup", "test" };
    char        dep_root[MAX_PATH];
    char        sub[MAX_PATH];
    int         root_count;
    int         i;
    char        msg[128];

    printf("\n--- TC-CLEANUP-005: tools\\archive\\deprecated structure ---\n");

    mk(dep_root, "tools\\archive\\deprecated");

    if (!dexists(dep_root)) {
        FAIL("TC-CLEANUP-005", "tools\\archive\\deprecated\\ does not exist");
        return;
    }

    for (i = 0; i < 3; i++) {
        snprintf(sub, MAX_PATH, "%s\\%s", dep_root, subdirs[i]);
        if (dexists(sub)) {
            int n = count_files_in_dir(sub);
            PASS("TC-CLEANUP-005", "deprecated\\%s exists (%d files)", subdirs[i], n);
        } else {
            FAIL("TC-CLEANUP-005", "deprecated\\%s subfolder missing", subdirs[i]);
        }
    }

    root_count = count_files_in_dir(dep_root);
    snprintf(msg, sizeof(msg), "deprecated\\ root has %d files (expected >=10)", root_count);
    if (root_count >= 10) PASS("TC-CLEANUP-005", "%s", msg);
    else                  FAIL("TC-CLEANUP-005", "%s", msg);
}

/* =========================================================================
 * TC-CLEANUP-006: No unexpected .c/.h files at repo root
 * ========================================================================= */
static void tc_cleanup_006(void)
{
    static const char *allowed[] = {
        "filter.c", "device.c", "avb_integration_fixed.c",
        "avb_hal.c", "ptp_hal.c", "avb_ndis.c"
    };
    char             pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE           h;
    int              strays = 0;
    int              i;

    printf("\n--- TC-CLEANUP-006: No unexpected .c/.h at repo root ---\n");

    snprintf(pat, MAX_PATH, "%s\\*", g_repo);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char *ext;
            int         ok;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            ext = strrchr(fd.cFileName, '.');
            if (!ext) continue;
            if (_stricmp(ext, ".c") != 0 && _stricmp(ext, ".h") != 0) continue;

            ok = 0;
            for (i = 0; i < 6; i++) {
                if (_stricmp(fd.cFileName, allowed[i]) == 0) { ok = 1; break; }
            }
            if (!ok) {
                printf("    stray: %s\n", fd.cFileName);
                strays++;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    if (strays == 0)
        PASS("TC-CLEANUP-006", "No unexpected .c/.h files at repo root");
    else
        FAIL("TC-CLEANUP-006", "%d unexpected .c/.h file(s) at repo root", strays);
}

/* =========================================================================
 * TC-CLEANUP-007: No stray scripts at repo root
 * ========================================================================= */
static void tc_cleanup_007(void)
{
    static const char *script_exts[] = { ".ps1", ".py", ".bat", ".cmd", ".sh" };
    char             pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE           h;
    int              strays = 0;
    int              i;

    printf("\n--- TC-CLEANUP-007: No stray scripts at repo root ---\n");

    snprintf(pat, MAX_PATH, "%s\\*", g_repo);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char *ext;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            ext = strrchr(fd.cFileName, '.');
            if (!ext) continue;

            for (i = 0; i < 5; i++) {
                if (_stricmp(ext, script_exts[i]) == 0) {
                    printf("    stray: %s\n", fd.cFileName);
                    strays++;
                    break;
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    if (strays == 0)
        PASS("TC-CLEANUP-007", "No stray scripts at repo root");
    else
        FAIL("TC-CLEANUP-007", "%d stray script(s) at repo root", strays);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    printf("=== TEST-CLEANUP-ARCHIVE-001: Repo Archival & Organization ===\n");
    printf("Verifies: #293 (REQ-NF-CLEANUP-002), #294\n");

    if (!compute_repo_root()) {
        printf("ERROR: failed to compute repo root\n");
        return 1;
    }
    printf("Repo root: %s\n", g_repo);

    tc_cleanup_001();
    tc_cleanup_002();
    tc_cleanup_003();
    tc_cleanup_004();
    tc_cleanup_005();
    tc_cleanup_006();
    tc_cleanup_007();

    printf("\n=== RESULTS ===\n");
    printf("Total: %d  PASS: %d  FAIL: %d\n", g_total, g_passed, g_failed);

    if (g_failed == 0) {
        printf("OVERALL: PASS\n");
        return 0;
    }
    printf("OVERALL: FAIL\n");
    return 1;
}
