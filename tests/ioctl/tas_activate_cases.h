/*
 * tas_activate_cases.h - the testcases contained in test_tas_activate.exe and how a run selects and sums them up (#487).
 *
 * One table for both uses: no argument runs every contained case, an explicit argument selects that case. Pure rules, shared by
 * tests/hardware/test_tas_activate.c and the offline case TC-MOCK-064 in test_tas_restore_contract.c; TEST-HILPATH-026 compares the
 * table with the registered runner entries of the program. Passing those checks says nothing about hardware.
 *
 * Verifies: #357
 */
#pragma once

#include <string.h>
#include <windows.h>

typedef enum { MODE_NONE = 0, MODE_HOLD, MODE_PASS, MODE_FAULT, MODE_FAULTV, MODE_HOLDCTL, MODE_META, MODE_COUNT } RUN_MODE;

typedef struct {
    const char *arg;       /* the argument that selects the case */
    RUN_MODE mode;
    const char *case_id;   /* prefix of its verdict lines */
} TAS_ACT_CASE;

/* The contained cases in run order. A new mode is added here only (#487) */
static const TAS_ACT_CASE g_tas_act_cases[] = {
    { "--hold", MODE_HOLD, "TC-TASAPI-004" },                         /* #369 */
    { "--pass", MODE_PASS, "TC-TASAPI-005" },                         /* #370 */
    { "--fault", MODE_FAULT, "TC-TASACT-FAULT-001" },                 /* #357, function-71 withdrawal (#482) */
    { "--fault-versioned", MODE_FAULTV, "TC-TASACT-FAULT-001" },      /* #482 */
    { "--phc-hold-control", MODE_HOLDCTL, "TC-TASACT-HOLDCTL-001" },  /* #505 */
    { "--metadata-events", MODE_META, "TC-TASACT-META-001" },         /* #357: L9/L10 events, -002/-003 (F1), -004 (restart) */
};
#define TAS_ACT_CASE_COUNT ((unsigned)(sizeof(g_tas_act_cases) / sizeof(g_tas_act_cases[0])))

/* Cases a command line selects (selected[TAS_ACT_CASE_COUNT]): no argument - every contained case; arguments - exactly the cases
 * they name, each once. FALSE for an unknown argument: nothing is selected */
static __inline BOOL tas_act_select(int argc, char *const *argv, BOOL *selected)
{
    BOOL valid = TRUE;
    unsigned k;
    int i;

    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        selected[k] = argc <= 1;
    }
    for (i = 1; i < argc; i++) {
        BOOL known = FALSE;
        for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
            if (strcmp(argv[i], g_tas_act_cases[k].arg) == 0) {
                selected[k] = TRUE;
                known = TRUE;
            }
        }
        valid = valid && known;
    }
    for (k = 0; !valid && k < TAS_ACT_CASE_COUNT; k++) {
        selected[k] = FALSE;
    }
    return valid;
}

/* Exit status of one case from its own counts: 1 a nonpassing verdict, 3 blocked, 2 nothing applicable ran, 0 every check passed.
 * A failure or a block before anything ran is never reported as "nothing applicable" */
static __inline int tas_act_case_exit(int ran, int failed, int blocked)
{
    if (failed != 0) { return 1; }
    if (blocked != 0) { return 3; }
    return ran == 0 ? 2 : 0;
}

/* Exit status of the run: 0 only when a case was selected and every selected case returned 0; otherwise 1 (any failure or unknown
 * status) before 3 (any block) before 2 (a case without any applicable adapter). The order of the cases has no influence */
static __inline int tas_act_run_exit(const BOOL *selected, const int *case_exit)
{
    unsigned k, any = 0, failed = 0, blocked = 0, none = 0;

    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        if (selected[k]) {
            any++;
            failed += case_exit[k] != 0 && case_exit[k] != 2 && case_exit[k] != 3;
            blocked += case_exit[k] == 3;
            none += case_exit[k] == 2;
        }
    }
    if (failed != 0) { return 1; }
    if (blocked != 0) { return 3; }
    return any == 0 || none != 0 ? 2 : 0;
}
