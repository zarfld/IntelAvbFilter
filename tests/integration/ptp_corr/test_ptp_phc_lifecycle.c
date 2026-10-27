/**
 * @file test_ptp_phc_lifecycle.c
 * @brief UT-CORR-009 Driver reload: TX-PHC correlation restored after the driver is reinstalled
 *
 * Split out of test_ptp_phc_stability.c (Q 2026-10-22, TEST-HILSUITE-REAL-003): the driver-lifecycle path
 * (tools\setup\Install-Driver.ps1 -Reinstall by process creation) exists only in this executable.  Run-Tests-CI.ps1
 * classifies it Unsafe: attended runs only, AVB_HIL_AUTHORIZE_UNSAFE naming this exact entry, never under GitHub Actions.
 *
 * Verifies: #149 (REQ-F-PTP-007) | Traces to: #48 (REQ-F-IOCTL-PHC-004) | Spec: issue #199 | Track A of #317
 *
 * Failure handling (Q 2026-10-27, #328; TEST-HILGATE-082 is the STATIC source contract, not runtime evidence).
 * FullSuite_20261024_113438 (Release, 13:03:58): the previous version blocked in its own ControlService(STOP), printed
 * no verdict, was killed at 1800 s, and its descendants kept mutating afterwards.  Now:
 *   - The installer is the only mutation path: Install-Driver.ps1 -NonInteractive (its STOP_PENDING prompt throws
 *     instead of waiting or offering a reboot), bounded by LC_INSTALL_TIMEOUT_MS, output preserved live in
 *     logs\ut_corr_009_install_<cfg>_<time>.log.  On timeout its process tree (job object) is terminated so that it
 *     cannot keep mutating after this test has reported.  No retry, forced unload, reboot or safety-record change.
 *   - The functional verdict ("UT-CORR-009 Driver Lifecycle" PASS/FAIL) and the restoration verdict are separate.
 *     Restoration is decided from observations taken before and after, never from the installer exit code:
 *       verified                     every required observation equals the state found ([POST] line, no verdict tag)
 *       [CLEANUP_FAILED]             an observed required property differs from the state found
 *       [INCONCLUSIVE_RESTORATION]   a required property cannot be observed afterwards and none observed differs
 *     Required observations: installed driver file SHA-256, SCM service state, control device, IOCTL_AVB_GET_VERSION
 *     (version and build flags of the LOADED driver), adapter enumeration (VID/DID/capabilities as a multiset: the
 *     index order is not stable across reloads).  Run-Tests-CI.ps1 additionally re-reads the adapters, TIMINCA and
 *     TSAUXC after the test.  Evidence limit: no observation hashes the loaded image.
 *   - BLOCKED (installer never started): driver provenance or a precondition is not established.
 *
 * Exit codes (same contract as test_ptp_phc_stability.exe; the printed verdict line names the restoration case):
 *   0 = PASS, 1 = FAIL or BLOCKED, 2 = SKIP_ONLY (no PASS result),
 *   4 = restoration not verified (CLEANUP_FAILED or INCONCLUSIVE_RESTORATION)
 */
#include "ptp_phc_common.h"
#include <string.h>   /* memcmp, strstr, strcmp */
#include <bcrypt.h>   /* SHA-256 of the installed driver */
#pragma comment(lib, "bcrypt.lib")

#define LC_MAX_ADAPTERS        8
#define LC_INSTALL_TIMEOUT_MS  (300u * 1000u)  /* below the HardwareIntegration suite timeout (600 s, Run-Tests-CI.ps1);
                                                 the Debug reinstall of 2026-10-24 13:37 took 17 s */
#define LC_TERMINATE_WAIT_MS   (10u * 1000u)
#define LC_SETTLE_TIMEOUT_MS   (30u * 1000u)   /* service RUNNING + control device after the installer */
#define LC_SETTLE_STEP_MS      1000u

static int s_inconclusive_restoration = 0;     /* INCONCLUSIVE_RESTORATION: restoration not establishable */

/* =========================================================================
 * Restoration identity (Q 2026-10-27): UT-CORR-009 reinstalls the driver it found.  Before any mutation the installed
 * System32 driver is matched byte-for-byte against the installer's source for each configuration (Install-Driver.ps1:
 * build\x64\<cfg>\IntelAvbFilter\IntelAvbFilter).  CWD is the repo root (Run-Tests-Elevated.ps1 / Run-Tests-CI.ps1).
 * =========================================================================*/
typedef struct { const char *config; uint8_t sha256[32]; } restore_identity_t;

static const struct { const char *config; const wchar_t *source; } k_restore_candidates[] = {
    { "Debug",   L"build\\x64\\Debug\\IntelAvbFilter\\IntelAvbFilter\\IntelAvbFilter.sys" },
    { "Release", L"build\\x64\\Release\\IntelAvbFilter\\IntelAvbFilter\\IntelAvbFilter.sys" },
};

static bool sha256_file(const wchar_t *path, uint8_t digest[32])
{
    static uint8_t buf[65536];
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) &&
              BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0));
    while (ok) {
        DWORD n = 0;
        if (!ReadFile(h, buf, sizeof(buf), &n, NULL)) { ok = false; break; }   /* a read error is no hash */
        if (n == 0) break;
        ok = BCRYPT_SUCCESS(BCryptHashData(hash, buf, n, 0));
    }
    ok = ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, 32, 0));
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(h);
    return ok;
}

static bool sha256_installed_driver(uint8_t digest[32])
{
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    if (wcscat_s(path, MAX_PATH, L"\\drivers\\IntelAvbFilter.sys") != 0) return false;
    return sha256_file(path, digest);
}

/* Exactly one candidate must be identical to the installed driver; otherwise the driver found cannot be restored
 * and the caller must not start the installer. */
static bool select_restore_configuration(restore_identity_t *out)
{
    if (!sha256_installed_driver(out->sha256)) {
        printf("  [PRE] installed driver System32\\drivers\\IntelAvbFilter.sys cannot be hashed (error %lu)\n", GetLastError());
        return false;
    }
    out->config = NULL;
    int matches = 0;
    for (size_t i = 0; i < sizeof(k_restore_candidates) / sizeof(k_restore_candidates[0]); i++) {
        uint8_t d[32];
        bool hashed = sha256_file(k_restore_candidates[i].source, d);
        bool same = hashed && memcmp(d, out->sha256, sizeof(d)) == 0;
        printf("  [PRE] %-7s installer source %S: %s\n", k_restore_candidates[i].config, k_restore_candidates[i].source,
               !hashed ? "not readable" : (same ? "IDENTICAL to the installed driver" : "differs"));
        if (same) { out->config = k_restore_candidates[i].config; matches++; }
    }
    if (matches != 1) {
        printf("  [PRE] %d installer sources match the installed driver (exactly 1 required)\n", matches);
        out->config = NULL;
        return false;
    }
    return true;
}

/* =========================================================================
 * Driver-state observation (read-only IOCTLs plus SCM query and file hash)
 * =========================================================================*/
typedef enum { LC_SVC_UNKNOWN = 0, LC_SVC_ABSENT, LC_SVC_STOPPED, LC_SVC_PENDING, LC_SVC_RUNNING } lc_svc_t;
static const char *const k_svc_name[] = { "UNKNOWN (query failed)", "ABSENT", "STOPPED", "PENDING", "RUNNING" };

typedef struct { uint16_t vid, did; uint32_t caps; } lc_adapter_t;

typedef struct {
    bool file_ok; uint8_t file_sha[32];
    lc_svc_t svc;
    bool dev_ok;
    bool ver_ok; IOCTL_VERSION ver;
    bool enum_ok; int count; lc_adapter_t a[LC_MAX_ADAPTERS];
    bool phc_ok;                 /* GET_TIMESTAMP on the default adapter (the existing UT-CORR-009 criterion) */
} lc_obs_t;

static lc_svc_t lc_query_service(void)
{
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return LC_SVC_UNKNOWN;
    lc_svc_t r = LC_SVC_UNKNOWN;
    SC_HANDLE svc = OpenServiceA(scm, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!svc) {
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) r = LC_SVC_ABSENT;
    } else {
        SERVICE_STATUS ss = {0};
        if (QueryServiceStatus(svc, &ss)) {
            r = (ss.dwCurrentState == SERVICE_RUNNING) ? LC_SVC_RUNNING :
                (ss.dwCurrentState == SERVICE_STOPPED) ? LC_SVC_STOPPED : LC_SVC_PENDING;
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return r;
}

static void lc_observe(lc_obs_t *o)
{
    memset(o, 0, sizeof(*o));
    o->file_ok = sha256_installed_driver(o->file_sha);
    o->svc = lc_query_service();
    HANDLE h = open_device();
    if (h == INVALID_HANDLE_VALUE) return;
    o->dev_ok = true;
    DWORD br = 0;
    o->ver_ok = DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &o->ver, sizeof(o->ver), &o->ver, sizeof(o->ver), &br, NULL)
                && br >= sizeof(o->ver);
    uint32_t total = 0;
    for (int k = 0; k < LC_MAX_ADAPTERS; k++) {
        AVB_ENUM_REQUEST r = {0};
        r.index = (avb_u32)k;
        if (!DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS, &r, sizeof(r), &r, sizeof(r), &br, NULL) ||
            r.status != NDIS_STATUS_SUCCESS) break;
        total = r.count;
        o->a[o->count].vid = r.vendor_id;
        o->a[o->count].did = r.device_id;
        o->a[o->count].caps = r.capabilities;
        o->count++;
    }
    o->enum_ok = (o->count > 0 && (uint32_t)o->count == total);   /* every adapter the driver reports was read */
    uint64_t t = 0;
    o->phc_ok = read_phc(h, 0, &t);
    CloseHandle(h);
}

static void lc_print_obs(const char *tag, const lc_obs_t *o)
{
    printf("  [%s] driver file SHA-256 ", tag);
    if (o->file_ok) { for (int i = 0; i < 32; i++) printf("%02X", o->file_sha[i]); } else printf("not readable");
    printf("\n  [%s] service %s; control device %s", tag, k_svc_name[o->svc], o->dev_ok ? "accessible" : "NOT accessible");
    if (o->ver_ok)
        printf("; loaded driver %u.%u.%u.%u flags=0x%04X (%s build)", o->ver.Major, o->ver.Minor, o->ver.Build,
               o->ver.Revision, o->ver.Flags, (o->ver.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) ? "Debug" : "Release");
    else if (o->dev_ok)
        printf("; GET_VERSION failed");
    printf("\n");
    if (o->dev_ok) {
        printf("  [%s] adapters: %d%s", tag, o->count, o->enum_ok ? "" : " (enumeration incomplete)");
        for (int i = 0; i < o->count; i++) printf("  %04X:%04X caps=0x%08X", o->a[i].vid, o->a[i].did, o->a[i].caps);
        printf("; PHC %s\n", o->phc_ok ? "readable" : "NOT readable");
    }
}

static bool lc_same_adapters(const lc_obs_t *a, const lc_obs_t *b)
{
    bool used[LC_MAX_ADAPTERS] = {false};
    if (a->count != b->count) return false;
    for (int i = 0; i < a->count; i++) {
        int j;
        for (j = 0; j < b->count; j++)
            if (!used[j] && b->a[j].vid == a->a[i].vid && b->a[j].did == a->a[i].did && b->a[j].caps == a->a[i].caps) break;
        if (j == b->count) return false;
        used[j] = true;
    }
    return true;
}

typedef enum { LC_REST_VERIFIED, LC_REST_DIFFERS, LC_REST_UNOBSERVABLE } lc_rest_t;

#define LC_APPEND(buf, ...) do { size_t l_ = strlen(buf); snprintf((buf) + l_, sizeof(buf) - l_, __VA_ARGS__); } while (0)

/* Differences are demonstrated by observation; an unobservable property alone never becomes CLEANUP_FAILED. */
static lc_rest_t lc_compare(const lc_obs_t *pre, const lc_obs_t *post, char *why, size_t n)
{
    char d[512] = "", u[512] = "";
    if (!post->file_ok) LC_APPEND(u, "driver file not readable; ");
    else if (memcmp(post->file_sha, pre->file_sha, sizeof(pre->file_sha)) != 0) LC_APPEND(d, "driver file differs from the driver found; ");
    if (post->svc == LC_SVC_UNKNOWN) LC_APPEND(u, "service state not observable; ");
    else if (post->svc != LC_SVC_RUNNING) LC_APPEND(d, "service %s (was RUNNING); ", k_svc_name[post->svc]);
    if (!post->dev_ok) LC_APPEND(d, "control device not accessible (was accessible); ");
    else {
        if (!post->ver_ok) LC_APPEND(u, "GET_VERSION failed; ");
        else if (memcmp(&post->ver, &pre->ver, sizeof(pre->ver)) != 0) LC_APPEND(d, "loaded driver version/build flags differ; ");
        if (!post->enum_ok) LC_APPEND(u, "adapter enumeration incomplete; ");
        else if (!lc_same_adapters(pre, post)) LC_APPEND(d, "adapter set differs; ");
    }
    if (d[0]) { snprintf(why, n, "%s", d); return LC_REST_DIFFERS; }
    if (u[0]) { snprintf(why, n, "%s", u); return LC_REST_UNOBSERVABLE; }
    why[0] = '\0';
    return LC_REST_VERIFIED;
}

/* =========================================================================
 * Installer (the only mutation path): bounded, contained in a job, output preserved live
 * =========================================================================*/
typedef enum { LC_PH_STARTED = 0, LC_PH_PRECONDITIONS, LC_PH_UNINSTALL, LC_PH_INSTALL, LC_PH_COMPLETE } lc_phase_t;
static const char *const k_phase_name[] = { "started, no phase marker", "preconditions", "uninstall", "install", "complete" };

typedef struct {
    lc_phase_t phase;
    bool stop_requested;         /* "Stopping service..." reached in the uninstall phase */
    bool timed_out;
    bool tree_terminated;        /* after a timeout: the job reports no active process */
    bool exit_known; DWORD exit_code;
    char log[MAX_PATH];
} lc_install_t;

/* Phase markers printed by tools\setup\Install-Driver.ps1 (Write-Step / Write-Host). */
static void lc_read_phase(lc_install_t *r)
{
    static char buf[4u << 20];
    FILE *f = NULL;
    if (fopen_s(&f, r->log, "rb") != 0 || !f) return;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    for (size_t i = 0; i < n; i++) if (buf[i] == '\0') buf[i] = ' ';
    buf[n] = '\0';
    if (strstr(buf, "==> Checking installation preconditions")) r->phase = LC_PH_PRECONDITIONS;
    const char *u = strstr(buf, "==> Uninstalling Driver");
    if (u) { r->phase = LC_PH_UNINSTALL; r->stop_requested = strstr(u, "Stopping service...") != NULL; }
    if (strstr(buf, "==> Installing Driver")) r->phase = LC_PH_INSTALL;
    if (strstr(buf, "Installation complete!")) r->phase = LC_PH_COMPLETE;
}

static const char *lc_mutation_text(const lc_install_t *r)
{
    switch (r->phase) {
    case LC_PH_UNINSTALL: return r->stop_requested ? "attempted: uninstall started, service stop requested, reinstall not reached"
                                                   : "attempted: uninstall started before the service stop, reinstall not reached";
    case LC_PH_INSTALL:   return "attempted: uninstall completed, install started, not completed";
    case LC_PH_COMPLETE:  return "completed: the installer reported the reinstall complete";
    default:              return "none attempted (the installer ended before its uninstall phase)";
    }
}

/* false = containment or evidence log not established; then the installer never ran (created suspended). */
static bool run_installer(const char *config, lc_install_t *r)
{
    memset(r, 0, sizeof(*r));
    SYSTEMTIME st;
    GetLocalTime(&st);
    CreateDirectoryA("logs", NULL);
    snprintf(r->log, sizeof(r->log), "logs\\ut_corr_009_install_%s_%04u%02u%02u_%02u%02u%02u.log", config,
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hLog = CreateFileA(r->log, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hNul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    HANDLE hJob = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
    memset(&li, 0, sizeof(li));
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;   /* nothing outlives this test */
    PROCESS_INFORMATION pi = {0};
    bool ok = hLog != INVALID_HANDLE_VALUE && hNul != INVALID_HANDLE_VALUE && hJob != NULL &&
              SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &li, sizeof(li));
    if (ok) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass"
                                   " -File tools\\setup\\Install-Driver.ps1 -Configuration %s -Reinstall", config);
        STARTUPINFOA si;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hNul; si.hStdOutput = hLog; si.hStdError = hLog;
        ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi) != 0;
        if (ok && !AssignProcessToJobObject(hJob, pi.hProcess)) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            ok = false;
        }
    }
    if (!ok) {
        printf("  [PRE] installer containment or evidence log %s not established (error %lu)\n", r->log, GetLastError());
        if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
        if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);
        if (hJob) CloseHandle(hJob);
        return false;
    }
    printf("  Reinstalling the driver found (%s): Install-Driver.ps1 -NonInteractive -Reinstall, bounded %u s, output %s\n",
           config, LC_INSTALL_TIMEOUT_MS / 1000u, r->log);
    fflush(stdout);
    ResumeThread(pi.hThread);
    if (WaitForSingleObject(pi.hProcess, LC_INSTALL_TIMEOUT_MS) == WAIT_OBJECT_0) {
        r->exit_known = GetExitCodeProcess(pi.hProcess, &r->exit_code) != 0;
    } else {
        r->timed_out = true;
        TerminateJobObject(hJob, 1);
        for (DWORD waited = 0; waited <= LC_TERMINATE_WAIT_MS; waited += 200u) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION ai;
            memset(&ai, 0, sizeof(ai));
            if (QueryInformationJobObject(hJob, JobObjectBasicAccountingInformation, &ai, sizeof(ai), NULL) &&
                ai.ActiveProcesses == 0) { r->tree_terminated = true; break; }
            Sleep(200u);
        }
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    CloseHandle(hLog); CloseHandle(hNul);
    CloseHandle(hJob);
    lc_read_phase(r);
    return true;
}

/* Informational only (unchanged from the original procedure): TX SEND_PTP availability on the default adapter. */
static void lc_info_tx(const char *when, uint32_t seq)
{
    HANDLE h = open_device();
    if (h == INVALID_HANDLE_VALUE) return;
    uint64_t tx = 0, phc_send = 0;
    if (send_ptp_get_tx(h, 0, seq, &tx, &phc_send))
        printf("  %s: TX SEND_PTP OK (prov=PRE_SEND_PHC, delta=%lld ns)\n", when, (long long)(tx - phc_send));
    else
        printf("  %s: TX SEND_PTP not available (link down / not ready)\n", when);
    CloseHandle(h);
}

/* =========================================================================
 * UT-CORR-009: Driver Reload — PHC reachable and adapters rebound after the driver is reinstalled
 *
 * Procedure (per issue #199, lifecycle part):
 *   1. Observe the driver found; establish its provenance (else BLOCKED, nothing started)
 *   2. Reinstall that driver (Install-Driver.ps1 -Reinstall stops, removes and reinstalls it)
 *   3. Re-observe: device re-opens, adapter count maintained, PHC readable (functional verdict)
 *   4. Compare with the state found (restoration verdict)
 * Hardware TX-PHC correlation needs VERIFIED_HARDWARE_TX (SKIP, not #328/#199 close criteria).
 * =========================================================================*/
static void test_ut_corr_009(void)
{
    printf("\n[UT-CORR-009] Driver Reload: TX-PHC correlation restored after service restart\n");
    printf("  Verifies: #149 (REQ-F-PTP-007) | Traces to: #48 , Spec: issue #199\n");
    printf("  Requires elevated privileges for SCM service control.\n");

    lc_obs_t pre, post;
    lc_observe(&pre);
    lc_print_obs("PRE", &pre);

    /* --- 1. Preconditions and provenance, before any state change --- */
    restore_identity_t found;
    if (!select_restore_configuration(&found)) {
        tc_blocked("UT-CORR-009 installed driver matches no single installer source; it cannot be restored --- installer not started, no state modified");
        return;
    }
    const char *why = NULL;
    bool loaded_debug = pre.ver_ok && (pre.ver.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) != 0;
    if (pre.svc != LC_SVC_RUNNING)          why = "service not RUNNING before the test";
    else if (!pre.dev_ok)                   why = "control device not accessible before the test";
    else if (!pre.ver_ok)                   why = "IOCTL_AVB_GET_VERSION failed before the test";
    else if (loaded_debug != (strcmp(found.config, "Debug") == 0))
        why = "the loaded driver reports a different build than the installed file: the loaded image is not the installed file";
    else if (!pre.enum_ok)                  why = "adapter enumeration incomplete before the test";
    if (why) {
        char m[300];
        snprintf(m, sizeof(m), "UT-CORR-009 %s --- installer not started, no state modified", why);
        tc_blocked(m);
        return;
    }
    printf("  [PRE] driver found = %s build; it is reinstalled from that installer source\n", found.config);
    lc_info_tx("Before reload", 0x900U);

    /* --- 2. Reinstall --- */
    lc_install_t inst;
    if (!run_installer(found.config, &inst)) {
        tc_blocked("UT-CORR-009 installer containment or evidence log not established --- installer not started, no state modified");
        return;
    }
    if (inst.timed_out)
        printf("  [INSTALLER] did not finish within %u s; process tree %s\n", LC_INSTALL_TIMEOUT_MS / 1000u,
               inst.tree_terminated ? "terminated (verified: no process left in its job)" : "termination NOT verified");
    else if (inst.exit_known)
        printf("  [INSTALLER] exit code %lu\n", inst.exit_code);
    else
        printf("  [INSTALLER] exit code not available\n");
    printf("  [INSTALLER] last phase: %s; mutation (installer log, not used for the restoration verdict): %s\n",
           k_phase_name[inst.phase], lc_mutation_text(&inst));

    /* --- 3. Re-observe (bounded settle: the control device registers after the service reaches RUNNING) --- */
    for (DWORD waited = 0;; waited += LC_SETTLE_STEP_MS) {
        lc_observe(&post);
        if ((post.svc == LC_SVC_RUNNING && post.dev_ok) || waited >= LC_SETTLE_TIMEOUT_MS) break;
        Sleep(LC_SETTLE_STEP_MS);
    }
    lc_print_obs("POST", &post);
    if (inst.timed_out && inst.phase == LC_PH_UNINSTALL && inst.stop_requested)
        printf("  [#328] the driver stop did not complete within %u s (service %s afterwards); evidence: %s and the [PRE]/[POST]"
               " lines above; no retry, no forced unload, no reboot, no safety-record change\n",
               LC_INSTALL_TIMEOUT_MS / 1000u, k_svc_name[post.svc], inst.log);
    if (post.dev_ok) lc_info_tx("After reload", 0x901U);

    char fwhy[512] = "";
    if (inst.timed_out)                                  LC_APPEND(fwhy, "installer timed out; ");
    else if (!inst.exit_known || inst.exit_code != 0)    LC_APPEND(fwhy, "installer failed; ");
    if (!inst.timed_out && inst.phase != LC_PH_COMPLETE) LC_APPEND(fwhy, "installer did not report completion; ");
    if (post.svc != LC_SVC_RUNNING)                      LC_APPEND(fwhy, "service %s; ", k_svc_name[post.svc]);
    if (!post.dev_ok)                                    LC_APPEND(fwhy, "control device not accessible after reload (#328 Defect C); ");
    else {
        if (!post.enum_ok || post.count < pre.count)     LC_APPEND(fwhy, "adapter count %d -> %d%s; ", pre.count, post.count, post.enum_ok ? "" : " (incomplete)");
        if (!post.phc_ok)                                LC_APPEND(fwhy, "PHC not readable after reload; ");
    }
    if (fwhy[0]) printf("  FAIL: %s\n", fwhy);
    tc_result("UT-CORR-009 Driver Lifecycle: re-open, PHC reachable, adapter count maintained", fwhy[0] == '\0');
    tc_skip("UT-CORR-009 Hardware TX-PHC correlation after reload: VERIFIED_HARDWARE_TX unavailable (not #328/#199 close criteria)");

    /* --- 4. Restoration verdict, independent of the functional verdict and of the installer exit code --- */
    char rwhy[512];
    switch (lc_compare(&pre, &post, rwhy, sizeof(rwhy))) {
    case LC_REST_VERIFIED:
        printf("  [POST] UT-CORR-009 restoration verified: driver file, service, control device, loaded driver version/build"
               " and adapter set equal the state found\n");
        break;
    case LC_REST_DIFFERS:
        printf("  [CLEANUP_FAILED] UT-CORR-009 original state NOT restored: %s--- evidence %s; later driver-mutating tests"
               " must not run against this state\n", rwhy, inst.log);
        s_cleanup_failed++;
        break;
    default:
        printf("  [INCONCLUSIVE_RESTORATION] UT-CORR-009 restoration cannot be established: %s--- evidence %s; later"
               " driver-mutating tests must not run against this state\n", rwhy, inst.log);
        s_inconclusive_restoration++;
        break;
    }
    printf("  Evidence limit: the file SHA-256 and GET_VERSION do not hash the loaded image.\n");
}

int main(void)
{
    printf("========================================================================\n");
    printf("PHC Stability -- UT-CORR-009 Driver Lifecycle (reinstalls the driver)\n");
    printf("Verifies: #149 (REQ-F-PTP-007) | Spec: issue #199 | Track A of #317\n");
    printf("========================================================================\n");

    test_ut_corr_009();

    printf("\n========================================================================\n");
    printf("Test Summary -- UT-CORR-009 Driver Lifecycle (#317 Track A)\n");
    printf("  Total: %d  Passed: %d  Failed: %d  Skipped: %d  CleanupFailed: %d  InconclusiveRestoration: %d\n",
           s_total, s_passed, s_failed, s_skipped, s_cleanup_failed, s_inconclusive_restoration);
    printf("========================================================================\n");
    if (s_cleanup_failed > 0 || s_inconclusive_restoration > 0) return 4;
    if (s_failed > 0)         return 1;
    if (s_passed == 0)        return 2;
    return 0;
}
