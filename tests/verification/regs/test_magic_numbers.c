/**
 * @file test_magic_numbers.c
 * @brief SSOT Magic Number Static Analysis Test
 *
 * Test ID: TEST-REGS-002
 * Implements: #305 (TEST-REGS-002: Magic Numbers Static Analysis)
 * Verifies:   #21  (REQ-NF-REGS-001: Eliminate Magic Numbers / SSOT)
 *
 * Scans src\ and devices\ for raw hex literals that should be replaced
 * with named constants from intel-ethernet-regs/ (SSOT).
 *
 * Detection rule:
 *   A "magic number" is a hex literal with >= 4 hex digits (value >= 0x1000)
 *   that appears on a code line.  The following lines are EXCLUDED:
 *     - blank lines
 *     - lines whose first non-whitespace token is //   (single-line comment)
 *     - lines whose first non-whitespace token is /*   (block comment open)
 *     - lines whose first non-whitespace token is *    (block comment body)
 *     - lines that start with #define
 *     - lines that start with #include
 *
 * Pass criterion (ratchet gate):
 *   TC-MAGIC-001: total violations <= BASELINE_MAX
 *
 *   After first run read the log, note "Total violations = N", then lower
 *   BASELINE_MAX to N to lock in the current state.  Reduce further as the
 *   SSOT migration progresses.  Set to 0 for strict mode once migration is
 *   complete.
 *
 * Directories scanned (relative to repo root = CWD when run via
 *   Run-Tests-Elevated.ps1):
 *   - src\
 *   - devices\
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


/* Subdirectories to scan (appended to computed repo root) */
static const char * const SCAN_SUBDIRS[]     = { "src", "devices" };
static const int          SCAN_SUBDIR_COUNT  = 2;

/* Computed at startup from the exe's own path */
static char g_repo_root[MAX_PATH];
static char g_scan_dirs[2][MAX_PATH];

/* Maximum violations we will record (report + ratchet check) */
#define MAX_VIOLATIONS  4096

/* ==========================================================================
 * Test framework helpers – matches the repo-wide convention used in
 * test_hal_errors.c, test_hal_unit.c, etc.
 * ========================================================================== */
static int g_total  = 0;
static int g_passed = 0;
static int g_failed = 0;

#define PASS(id, fmt, ...)                                               \
    do {                                                                 \
        g_total++; g_passed++;                                           \
        printf("  \xE2\x9C\x94 PASS: " id ": " fmt "\n",              \
               ##__VA_ARGS__);                                           \
    } while (0)

#define FAIL(id, fmt, ...)                                               \
    do {                                                                 \
        g_total++; g_failed++;                                           \
        printf("  \xE2\x9C\x98 FAIL: " id ": " fmt "\n",              \
               ##__VA_ARGS__);                                           \
    } while (0)

/* ==========================================================================
 * Violation record
 * ========================================================================== */
typedef struct {
    char  file[MAX_PATH];
    int   line_no;
    char  literal[40];
} VIOLATION;

static VIOLATION g_violations[MAX_VIOLATIONS];
static int       g_vcount = 0;

static void RecordViolation(const char *file, int line_no, const char *lit,
                            int lit_len)
{
    VIOLATION *v;
    int copy;
    if (g_vcount >= MAX_VIOLATIONS)
        return;
    v = &g_violations[g_vcount++];
    strncpy(v->file, file, MAX_PATH - 1);
    v->file[MAX_PATH - 1] = '\0';
    v->line_no = line_no;
    copy = (lit_len < 39) ? lit_len : 39;
    memcpy(v->literal, lit, copy);
    v->literal[copy] = '\0';
}

/* ==========================================================================
 * ComputeRepoRoot: derive repo root from the exe's own location.
 *
 * The exe lives at: <repo_root>\build\tools\avb_test\x64\<Config>\*.exe
 * Going up 5 parent directories reaches <repo_root>.
 * ========================================================================== */
static int ComputeRepoRoot(void)
{
    char  exe_path[MAX_PATH];
    char *p;
    int   up;

    if (!GetModuleFileNameA(NULL, exe_path, sizeof(exe_path)))
        return 0;

    /* Strip filename to get the directory */
    p = strrchr(exe_path, '\\');
    if (p) *p = '\0';

    /* Walk up 5 levels */
    for (up = 0; up < 5; up++) {
        p = strrchr(exe_path, '\\');
        if (!p) return 0;
        *p = '\0';
    }

    strncpy(g_repo_root, exe_path, MAX_PATH - 1);
    g_repo_root[MAX_PATH - 1] = '\0';
    return 1;
}


static int IsExcludedLine(const char *line)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;   /* skip leading whitespace */

    if (*p == '\0' || *p == '\r' || *p == '\n')
        return 1;                          /* blank */
    if (p[0] == '/' && p[1] == '/')
        return 1;                          /* // comment */
    if (p[0] == '/' && p[1] == '*')
        return 1;                          /* /* block comment open */
    if (p[0] == '*')
        return 1;                          /* * block comment body */
    if (strncmp(p, "#define",  7) == 0)
        return 1;
    if (strncmp(p, "#include", 8) == 0)
        return 1;
    return 0;
}

/* ==========================================================================
 * ScanLine: find hex literals >= 0x1000 on one source line
 * ========================================================================== */
static void ScanLine(const char *path, int line_no, const char *line)
{
    const char *p = line;
    while (*p) {
        /* Look for 0x / 0X */
        if ((*p == '0') && (p[1] == 'x' || p[1] == 'X')) {
            /* Word-boundary check: must not be preceded by ident char */
            if (p > line) {
                char prev = *(p - 1);
                if (isalnum((unsigned char)prev) || prev == '_') {
                    p++;
                    continue;
                }
            }
            /* Count hex digits */
            {
                const char *hex = p + 2;
                const char *q   = hex;
                unsigned long long val = 0;
                int n;
                while (isxdigit((unsigned char)*q)) q++;
                n = (int)(q - hex);
                if (n >= 4) {
                    /* Compute value to confirm >= 0x1000 */
                    const char *r;
                    for (r = hex; r < q; r++) {
                        val <<= 4;
                        if      (*r >= '0' && *r <= '9') val |= (unsigned)(unsigned char)(*r - '0');
                        else if (*r >= 'a' && *r <= 'f') val |= (unsigned)(unsigned char)(*r - 'a') + 10u;
                        else                              val |= (unsigned)(unsigned char)(*r - 'A') + 10u;
                    }
                    if (val >= 0x1000ULL) {
                        int lit_len = (int)(q - p);
                        RecordViolation(path, line_no, p, lit_len);
                    }
                }
                p = q;   /* advance past whatever we consumed */
                continue;
            }
        }
        p++;
    }
}

/* ==========================================================================
 * ScanFile
 * ========================================================================== */
static void ScanFile(const char *path)
{
    FILE *f = fopen(path, "r");
    char  line[4096];
    int   line_no = 0;

    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        line_no++;
        if (IsExcludedLine(line))
            continue;
        ScanLine(path, line_no, line);
    }
    fclose(f);
}

/* ==========================================================================
 * ScanDirectory: recursively walk dir_path and scan *.c / *.h
 * ========================================================================== */
static void ScanDirectory(const char *dir_path)
{
    char             pattern[MAX_PATH];
    HANDLE           hFind;
    WIN32_FIND_DATAA fd;

    _snprintf(pattern, sizeof(pattern) - 1, "%s\\*", dir_path);
    pattern[sizeof(pattern) - 1] = '\0';

    hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        char  full_path[MAX_PATH];
        size_t len;

        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        _snprintf(full_path, sizeof(full_path) - 1, "%s\\%s",
                  dir_path, fd.cFileName);
        full_path[sizeof(full_path) - 1] = '\0';

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanDirectory(full_path);
        } else {
            len = strlen(fd.cFileName);
            if (len >= 2) {
                const char *ext = fd.cFileName + len - 2;
                if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0)
                    ScanFile(full_path);
            }
        }
    }
    while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

/* ==========================================================================
 * main
 * ========================================================================== */
int main(void)
{
    int  total_violations;
    int  show;
    int  i;

    printf("\n");
    printf("================================================================\n");
    printf("  TEST-REGS-002: Magic Number Static Analysis\n");
    printf("  Verifies: #21 (REQ-NF-REGS-001: SSOT for all driver constants)\n");
    printf("  Requirement: ZERO raw hex literals in src\\ and devices\\\n");
    printf("================================================================\n\n");

    /* Resolve repo root from exe path so test works regardless of CWD */
    if (!ComputeRepoRoot()) {
        printf("ERROR: Cannot compute repo root from exe path.\n");
        return 1;
    }
    printf("Repo root: %s\n\n", g_repo_root);

    /* Build absolute scan paths */
    for (i = 0; i < SCAN_SUBDIR_COUNT; i++) {
        _snprintf(g_scan_dirs[i], MAX_PATH - 1, "%s\\%s",
                  g_repo_root, SCAN_SUBDIRS[i]);
        g_scan_dirs[i][MAX_PATH - 1] = '\0';
    }

    /* Scan all configured source directories */
    printf("Scanning:\n");
    for (i = 0; i < SCAN_SUBDIR_COUNT; i++) {
        printf("  -> %s\\\n", g_scan_dirs[i]);
        ScanDirectory(g_scan_dirs[i]);
    }
    printf("\n");

    total_violations = g_vcount;

    /* Print up to 30 examples for visibility in the log */
    if (total_violations > 0) {
        show = (total_violations > 30) ? 30 : total_violations;
        printf("Sample violations (showing %d of %d):\n", show, total_violations);
        for (i = 0; i < show; i++) {
            VIOLATION *v = &g_violations[i];
            printf("  %s:%d  %s\n", v->file, v->line_no, v->literal);
        }
        if (total_violations > 30)
            printf("  ... and %d more\n", total_violations - 30);
        printf("\n");
    } else {
        printf("  No violations found.\n\n");
    }

    /* TC-MAGIC-001 --------------------------------------------------------- */
    printf("TC-MAGIC-001: Total violations = %d\n", total_violations);

    if (total_violations == 0) {
        PASS("TC-MAGIC-001", "No raw hex literals found");
    } else {
        FAIL("TC-MAGIC-001",
             "%d raw hex literal(s) must be replaced with named SSOT constants",
             total_violations);
        printf("\n");
        printf("  ACTION: Replace all hex literals with SSOT-named constants\n");
        printf("  from intel-ethernet-regs/gen/*.h\n");
        printf("  See: REQ-NF-REGS-001 (#21)\n\n");
    }

    /* Summary ------------------------------------------------------------- */
    printf("\n");
    printf("----------------------------------------------------------------\n");
    printf("Total: %d  Passed: %d  Failed: %d\n", g_total, g_passed, g_failed);
    if (g_failed == 0)
        printf("\xE2\x9C\x94 ALL TESTS PASSED\n");
    else
        printf("\xE2\x9C\x98 TESTS FAILED\n");
    printf("----------------------------------------------------------------\n\n");

    return (g_failed > 0) ? 1 : 0;
}
