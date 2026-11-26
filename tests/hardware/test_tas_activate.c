/**
 * @file test_tas_activate.c
 * @brief IOCTL_AVB_TAS_ACTIVATE on a real adapter: first gate schedule, wire behaviour, state, refusal, cleanup (#357)
 *
 * Without arguments every contained case runs, one after another in this process (#487): each with its own device handle,
 * adapter discovery, setup, cleanup, counts and result; a failed, blocked or unsupported case never ends the run, and the exit
 * status is 0 only when every case passed (tests/ioctl/tas_activate_cases.h, TC-MOCK-064). Arguments select cases; each is also
 * its own registered runner entry:
 *   --hold   TEST-TAS-API-004 (#369, Verifies #357 and #350), case TC-TASAPI-004, per #369's amendments of 2026-11-18 (P2 step 5)
 *            and 2026-11-19 (oracle: executable accepted-unverified HOLD) against #365 13.1-13.4 and its published R1/R2/R4/R5/R7
 *            (#352 R1, #351, #362): the versioned AVB_TAS_OP_REQUEST (144 bytes, version 1) on IOCTL_AVB_TAS_ACTIVATE_80C
 *            (0x0017A030). Each request carries a distinct non-zero client_op_id, the bound net_luid, pre_activation HOLD,
 *            debug_fault 0, AVB_TAS_F_ACCEPT_UNVERIFIED (#362: it authorizes execution of an unqualified HOLD control, never
 *            HARDWARE_VERIFIED) and the generation and epoch of the test's own query taken immediately before the race. When that
 *            query is not valid the proved pre-state's pair is sent as the last observed one and printed NOT measured; 0 = not
 *            reported, never a wildcard and never fabricated. The request carries no link speed: that query's driver-owned
 *            link_speed_mbps (#351 R2) and limits.tier_hold are measured facts, printed. Checks of this mode (the legacy lines
 *            below describe --pass and --fault):
 *              ACCESS      a read-only bound handle: STATUS_ACCESS_DENIED (raw NTSTATUS) with no output on 0x80C, GET_TAS_STATE
 *                          still served.
 *              METADATA    the query before the race reports a non-zero generation and epoch (#352 R1 L13): 0 is FAIL (an
 *                          implementation RED, never a valid control), a query that is not valid INCONCLUSIVE (not measured). Its own
 *                          line; the race still runs with what was measured.
 *              COMMIT      two bound open instances race the same schedule (tas_hold_commit, TC-MOCK-062): exactly one
 *                          STATUS_SUCCESS with Information 144, bytes 0-135 equal to what it sent, reserved1 0, no out_flags bit but
 *                          bit 0, and AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE exactly when acceptance acted (#365 R4): tier
 *                          IMPLEMENTED_UNVERIFIED bit 0, IMPLEMENTED_VERIFIED 0; the other STATUS_DEVICE_BUSY or
 *                          STATUS_INVALID_DEVICE_STATE with Information 0. With the tier not reported, or the query not valid, the
 *                          output is printed and COMMIT is INCONCLUSIVE, never PASS. FAIL: STATUS_INVALID_DEVICE_REQUEST (0x80C not
 *                          served, never SKIP); an incomplete success or a wrong output; two successes; a success of a request
 *                          carrying 0; a tier refusal STATUS_REQUEST_NOT_ACCEPTED of the accepted control (R4: the flag acts);
 *                          STATUS_IO_TIMEOUT or STATUS_IO_DEVICE_ERROR (R5 BASE_PASSED / COMMITTED_UNTIMED, named; the shared
 *                          STATUS_IO_DEVICE_ERROR is no outcome detail); both refused STATE with a 0 sent (the zero metadata named
 *                          as the likely cause, never an excuse); a STATE refusal with an established pair unchanged across the race
 *                          on an adapter measured clean immediately before it; NOT_READY at a speed measured unchanged and non-zero
 *                          around it; any other captured completion (checked before any INCONCLUSIVE) or output. INCONCLUSIVE: a
 *                          completion not captured; a STATE refusal with the pair changed or not measured around the race; NOT_READY
 *                          with the speed unknown, changed or not measured; a STATE refusal or a success when the adapter was not
 *                          measured clean immediately before the race (GET_TAS_STATE CLEAN and a clean query, as the proved
 *                          pre-state; NOT_CLEAN, R7 step 9, shares STATUS_INVALID_DEVICE_STATE); each measurement printed. Observed
 *                          violations are kept when another measurement is missing.
 *              STATE       GET_TAS_STATE ARMED with the requested gate list. REFUSE: a further accepted activation on the armed
 *                          adapter is STATUS_INVALID_DEVICE_STATE with Information 0 and no reported state change. Both need a
 *                          committed schedule (exactly one STATUS_SUCCESS with the 144-byte reply; its output is COMMIT's to judge);
 *                          without one they are BLOCKED. The further request carries the generation and epoch of the query after
 *                          the race (BLOCKED, not sent, when that query fails). NOT_CLEAN and STATE_CONFLICT share that status
 *                          (#365 13.3, 13.4), so a refusal is PASS only when that query reports a non-zero generation and epoch;
 *                          with 0 (not reported) it is INCONCLUSIVE. Acceptance, output or a changed schedule is FAIL.
 *              GENERATION  (tas_hold_generation, TC-MOCK-063; #352 R1 L2/L7/L9) the query after the race reports the generation of
 *                          the query before it plus one per commit or BASE_PASSED (STATUS_IO_TIMEOUT, #365 R5: writes attempted)
 *                          and none per refusal before any write, in the same context
 *                          (epoch unchanged). A 0 or a query not valid before or after, a changed epoch, or a completion whose write
 *                          attempt is not established (not captured; a status shared by outcomes before and after a write, R5/R8)
 *                          is INCONCLUSIVE, named; a decrease is FAIL.
 *              LOOKUP      evidence only: a query carrying the committed client_op_id, on the committing open instance (#365
 *                          13.1), prints lookup_result (the producer stage expects NOT_IMPLEMENTED; success-record retrieval is
 *                          #460, never asserted here).
 *              WIRE, STATUS, CLEANUP, RESTORE  as below; WIRE needs a committed schedule and an established observer.
 *            The competing request carries the same schedule, so after a commit its own hardware effect cannot be separated from
 *            the winner's: the query after the race is printed as evidence of one schedule, not as proof that the refusal wrote
 *            nothing. Expected RED against a driver without the 0x80C producer: COMMIT FAIL (not served), METADATA FAIL while the
 *            query reports 0, GENERATION INCONCLUSIVE (not reported), the dependent lines BLOCKED, ACCESS PASS (the I/O manager
 *            refuses the read-only handle whatever the handler). Passing COMMIT does not establish K2 (#352), and an accepted
 *            unverified commit is not HARDWARE_VERIFIED.
 *   --pass   TEST-TAS-API-005 (#370, Verifies #357), case TC-TASAPI-005, per its amendment of 2026-11-19: the 144-byte
 *            version-1 AVB_TAS_OP_REQUEST on IOCTL_AVB_TAS_ACTIVATE_80C (0x0017A030) with pre_activation PASS (2), debug_fault 0,
 *            flags 0, a distinct non-zero client_op_id, the bound net_luid and the generation and epoch of the test's own query
 *            (0 = not reported, printed as such, never fabricated), the HOLD schedule and the same BaseT lead. Function 71 is not
 *            called by this mode (its withdrawal is #368's LEGACY_FUNCTION_71).
 *              ACCESS   as for --hold: a read-only bound handle gets STATUS_ACCESS_DENIED on 0x80C, GET_TAS_STATE still served.
 *              COMMIT   under #365's R7 order a header-valid PASS completes STATUS_NOT_IMPLEMENTED, Information 0, before the
 *                       metadata, schedule, link and tier checks and before any register or PHC access, until ADR-TSN-003
 *                       (#363). Measured against the proved-clean pre-state: the TAS record stays CLEAN, the restoration set
 *                       (DES 12.5) stays as captured register by register (a register unread in either reply is a missing
 *                       measurement and never suppresses the comparison of the others, PR #484), and a reported generation
 *                       stays unchanged (0 is neither a value nor
 *                       evidence of no change). SKIP_NOT_IMPLEMENTED only for that completion with every applicable
 *                       observation established, never PASS. STATUS_INVALID_DEVICE_REQUEST (0x80C not served), any other
 *                       completion or output, or a measured change is FAIL, also STATE_CONFLICT with zero metadata (PASS
 *                       is recognized first under R7). Expected completion without an observed change but with a
 *                       measurement missing is INCONCLUSIVE, each missing measurement named; observed violations are kept
 *                       even when other measurements are missing. STATE, REFUSE and WIRE do not apply.
 *            No link is required by the request (it carries no link speed); the cleanup's TX probe still needs one.
 *   --fault  function-71 WITHDRAWAL check, case TC-TASACT-FAULT-001 (D-C, Q 2026-11-21, supplementary resolution on #482 and
 *            #357; formerly the legacy failed-rollback regression of PR #397, kept executable until the atomic cutover). The
 *            request it always sent (the verified HOLD set with the fault selector 13, 80 bytes) goes to the withdrawn function 71:
 *              WITHDRAWN  STATUS_INVALID_DEVICE_REQUEST with no output is PASS; a served request is FAIL; an uncaptured status
 *                         INCONCLUSIVE.
 *              UNCHANGED  the restoration set register by register against the pre-state query and the record CLEAN; a measured
 *                         change is FAIL, a missing measurement INCONCLUSIVE.
 *            It proves only that function 71 is refused: never fault/rollback evidence, which is --fault-versioned's. No link is
 *            required; the cleanup's TX probe still needs one. Expected RED before the cutover: function 71 is still served.
 *   --metadata-events  case TC-TASACT-META-001 (#357 producer; #352 R1 L9 and L10). Per TAS adapter, from the proved pre-state,
 *            each line one generation measurement across one event (meta_line: two valid replies with a non-zero pair in the
 *            same context, else INCONCLUSIVE; any other measured move FAIL):
 *              CLEAN_EVENT    PHC_OFFSET_ADJUST +1 us while the record is CLEAN: +0 (L10 applies to a retained schedule only).
 *              COMMIT         the versioned HOLD (explicit acceptance, the pair just queried) commits: the retained schedule
 *                             the L10 lines need; without it they are BLOCKED (prerequisite), never judged.
 *              RESEED_SET     SET_TIMESTAMP (current PHC + 1 s, a PHC reseed) with the schedule retained: +1 (L10).
 *              STATUS_SET     the schedule status after that reseed, the first event since the commit (#350; tas_status_reseed, TC-MOCK-072,
 *                             as STATUS_OFFSET below). Required: INCONSISTENT with PHC_RESET_SINCE_COMMIT (#365 12.4
 *                             S2), commit PHC_DISCONTINUOUS with the committed schedule and its bracket (12.2), next NONE when every
 *                             compared field equals the committed schedule (R1); otherwise exactly the mismatch reasons besides and
 *                             next UNKNOWN/SLOT_UNATTRIBUTED (R2, tas_status_match); a comparison that cannot be established is
 *                             INCONCLUSIVE. RETAINED or a progression status as before the event is FAIL; a reply whose
 *                             inputs are not observable must be UNKNOWN (S1) and is then INCONCLUSIVE. Judged only with the line's own
 *                             context: this case's commit (epoch, expected_generation + 1) in the query before, the event completed and
 *                             its effect on the PHC measured (here: the step against wall time, tas_phc_step), exactly one generation
 *                             step in the query after, and no reset reported before. Anything missing: INCONCLUSIVE, never PASS.
 *              ONCE           a second query after the same event: +0 (L10: once per event, never per query).
 *              RESEED_OFFSET  PHC_OFFSET_ADJUST +1 us with the schedule retained: +1 (L10, another reseed event).
 *              DISARM_NOWRITE DISARM_TAS completing STATUS_NOT_SUPPORTED (its device path writes nothing): +0 (L9).
 *              SETUP_WRITE    SETUP_TAS completing STATUS_SUCCESS (registers written): exactly +1 (L9).
 *              SETUP_NOWRITE  SETUP_TAS refused before its first TAS write: +0 (L9, #503). SYSTIM frozen (SET_HW_TIMESTAMPING
 *                             enable=0) and zeroed through the Debug-only WRITE_REGISTER at the SYSTIML/SYSTIMH offsets the HAL
 *                             reports in QUERY_HW_SNAPSHOT version 2 (no offset in this test); each unestablished setup step is
 *                             INCONCLUSIVE, and on a Release build the line is INCONCLUSIVE (never PASS).
 *            Then the owned NIC-cycle cleanup. Not reachable here and therefore not judged: a failed register read before the
 *            first write in the legacy setup_tas, and the bring-up init_ptp reseed (L10).
 *            Per TAS adapter a second case follows, TC-TASACT-META-002 (#357 F1; #352 R1 L8/L10), a complete run of its own with its own
 *            handles, proved pre-state and owned NIC-cycle cleanup: the same ordering question made deterministic by the Debug-only
 *            hold control (#505, no repetition). On the CLEAN record one PHC_OFFSET_ADJUST (+1 us) is held before its physical write
 *            and a HOLD ACTIVATE (explicit acceptance, the pair just queried) is sent while it is held. Judged from the driver's
 *            markers of this arm and this op id, never from the order in which the IOCTLs return:
 *              WINDOW      the ACTIVATE arrived between hold begin and hold end, both operations really completed (the write
 *                          STATUS_SUCCESS, the ACTIVATE COMMITTED) and commit and write do not overlap. Both orders are valid; the
 *                          observed one is printed, none is prescribed. Anything else is not the controlled interleaving:
 *                          INCONCLUSIVE, and GENERATION with it, never PASS (tas_race_order, TC-MOCK-066).
 *              GENERATION  commit before the write entry: the reseed hit a retained schedule, +2 in total (commit +1, L10 +1).
 *                          Write done before the commit: the reseed hit a CLEAN record, +1 in total.
 *              STATUS_OFFSET  after these lines: PHC_OFFSET_ADJUST +100 ms on this case's own, not yet hit commit, then the status as for
 *                          STATUS_SET (the +1 us step of META-001 is not measurable and follows another reseed, so it proves no status). A
 *                          race that did not commit, or that committed before the held write, leaves no such commit: INCONCLUSIVE.
 *              SETTLED     both operations ended within their bounds; otherwise INCONCLUSIVE. While one has not ended when the
 *                          cleanup starts (after a further bounded wait), CLEANUP and RESTORE are INCONCLUSIVE_RESTORATION at best,
 *                          a measured failure stays CLEANUP_FAILED, and a later case on this adapter is BLOCKED (TC-MOCK-067).
 *            TC-TASACT-META-003, a third complete run: the same interleaving, but the held reseed steps the PHC back (5 s) and BaseT had
 *            already passed on the old PHC, which refuses it (STATUS_INVALID_PARAMETER, TAS-DR-08); its STATUS_OFFSET steps back (-100 ms).
 *            Additional line:
 *              NEW_PHC     write before commit and the request COMMITTED: it was judged by the new PHC (tas_race_phc_used). A commit
 *                          before the write would have committed a passed BaseT: FAIL. No commit (the old PHC refused it):
 *                          INCONCLUSIVE, as WINDOW and GENERATION; never PASS.
 *            A Release driver does not serve the control: every line INCONCLUSIVE, never PASS.
 *            TC-TASACT-META-004, a fourth complete run (#357, #352 R1 L10; lifecycle facts of #364): a FilterRestart of this adapter
 *            (IOCTL_FILTER_RESTART_ONE_INSTANCE on the one instance that carries its interface GUID) while a schedule is retained.
 *              COMMIT      the versioned HOLD committed, GET_TAS_STATE ARMED, a valid non-zero pair. Without it the later lines have no
 *                          prerequisite: BLOCKED, nothing is triggered.
 *              INIT_READY  control: INIT_DEVICE on the second bound instance of the READY adapter, STATUS_SUCCESS, the PHC measured running
 *                          before and after without a reset: +0. A PHC that was not read or did not run is no control: INCONCLUSIVE.
 *              CONTROL_BEFORE  the query before the restart: this commit RETAINED in its context with the generation after the commit,
 *                          no snapshot reason, next NONE, the slot PENDING or past BaseT UNKNOWN with LINK_EVENTS_UNOBSERVED alone
 *                          (tas_ev_control_retained). Otherwise INCONCLUSIVE, and STATUS_RESTART judges nothing.
 *              RESTART     Pause +1, Restart +1, Pause/Restart generation +1, no attach and no detach, the same non-zero epoch
 *                          (tas_lifecycle_outcome, TC-MOCK-015), and its end: hw_read READ again after the Restart entry (the query gate
 *                          reopens only after a successful bring-up). The counters move on entry and show no end; the trigger's own result
 *                          is no evidence. Otherwise INCONCLUSIVE (tas_restart_stage, TC-MOCK-070).
 *              RESEED      the PHC ran before and after and went below its last value (tas_phc_reset): measured, not concluded from the
 *                          bring-up. Otherwise INCONCLUSIVE. Who reset it is not separated (#348).
 *              GENERATION  only with RESTART (its end included) and RESEED: exactly +1 (one event, the reset; the Pause has no step of
 *                          its own). Without a shown end a later write may still follow, so the value is printed and not judged; a failed
 *                          bring-up is therefore not covered here.
 *              STATUS_RESTART  the Pause class (#365 decision 2026-11-26 B: FilterPause counts, whatever its reason; the return of
 *                          FilterRestart ends nothing): with CONTROL_BEFORE and RESTART (its end included), the reply after the Restart and
 *                          two further replies each S1 UNKNOWN/UNKNOWN, reasons 0, with EVENT_UNRECONCILED (12.4, 12.10). A reply without it
 *                          is FAIL. The answer is the Pause's only when FilterStatus was not entered on this context from before the
 *                          control to after the last reply (a link indication gives the same answer, META-007); otherwise INCONCLUSIVE
 *                          (tas_ev_pause_phase). The PHC reset is not required for it and is judged apart (RESEED, GENERATION, RECORD_RESTART).
 *              RECORD_RESTART  with RESTART and RESEED: the commit no longer RETAINED, the committed schedule and its (possible) bracket
 *                          kept, one epoch, next commit NONE: otherwise FAIL. Its state from the reply's own registers against the
 *                          committed schedule (tas_status_match): R1 PHC_DISCONTINUOUS, R2 also HW_MISMATCH; without that comparison
 *                          INCONCLUSIVE (tas_ev_record_reset, PR #528 review). Without RESTART and RESEED: INCONCLUSIVE.
 *              ONCE        only with a shown end: two further queries, +0.
 *              STALE_VIEW  only with RESTART and RESEED: an ACTIVATE with the pair from before the event must be refused STATE_CONFLICT.
 *                          Both refusals complete STATUS_INVALID_DEVICE_STATE, so the #505 record of this arm and this op id decides
 *                          (tas_stale_view). Nothing is written in either case (the record is ARMED). Release: INCONCLUSIVE.
 *              NEW_CONTEXT after the owned NIC cycle: an epoch that differs from the old one (compared, never assumed), generation 1 and
 *                          record CLEAN, although attach and its restart reset the PHC again (no retained schedule: no step).
 *            TC-TASACT-META-005, a fifth complete run (#369, Debug only; the approved exception is stated at m5_restore): the STATUS oracle of
 *            --hold and the driver's answer under a real deviation of one compared register.
 *              COMMIT      a HOLD of its own, BaseT at least 3 s ahead.
 *              GUARD       a query of its own, reads only: this commit's pair, ARMED, PENDING and RETAINED, nine registers (STQT, ENDQT,
 *                          QBVCYCLET) read through the bound handle equal to the query, the PHC at least 1.5 s before BaseT. Anything
 *                          missing: INCONCLUSIVE and nothing is written.
 *              DEVIATION   ENDQT3.END_TIME 400 -> 450 ms, read back, present in the reference query and in the judged one, nothing else
 *                          changed. REG_RESTORE: the exact raw value back and read; otherwise CLEANUP_FAILED or
 *                          INCONCLUSIVE_RESTORATION, whatever the NIC cycle recovers later. HANDLE (also META-006): the accesses' own duplicate of
 *                          the bound handle closed once, after they ended; still open: INCONCLUSIVE_RESTORATION.
 *              GENERATION, GENERATION_RESTORED   +0, measured.
 *              R2          the driver: 6/0x8, next 7/0x1000, commit 4, record kept, in both queries.
 *              ORACLE_SLOT, ORACLE_NEXT   the oracle on these replies: a correct R2 answer is no positive control and no failure.
 *              CONTROL     after the restoration: 3/0x0, next 1/0x0, commit 2.
 *              ORACLE_RESTORED_SLOT, _NEXT   the oracle with the deviated query as the one after the commit: PASS.
 *            PENDING or NONE accepted against a deviation needs a driver that answers wrongly: not reachable here, offline only.
 *            TC-TASACT-META-006, a sixth complete run (#369, PR #524 review, Debug only; the extension is stated at m5_restore): the abort path
 *            of META-005 on hardware after the write ended. Its own COMMIT, GUARD and DEVIATION as META-005; with the deviation confirmed a
 *            second thread calls what the console handler calls.
 *              ABORT_ORDER  the write ended before the abort was called, the abort before the restoration.
 *              ABORT_ONE    one restoration, on the abort's thread; the case takes its result.
 *              REG_RESTORE  as META-005. CONTROL, GENERATION_RESTORED: the driver's answer after it.
 *              NO_REWRITE   one deviation write; a further change of the same record is refused (its accesses are counters); ENDQT3 raw twice.
 *              ABORT_SETTLED  the abort's thread ended, nothing is open; otherwise INCONCLUSIVE_RESTORATION, and CLEANUP and RESTORE cannot PASS.
 *            Not shown: an abort before or during the write (TC-MOCK-077 only), the delivery of a console signal, the end of a process.
 *            TC-TASACT-META-007, a seventh complete run (#350, #362; the MDIO restart approved 2026-11-26 for this case, see run_meta_link): a
 *            link change without a detach under a retained schedule. COMMIT (HOLD, BaseT 40 s ahead) and GUARD (this commit PENDING, the link
 *            connected and stable, #472's PHY checks on a handle proven bound, PHC at least 35 s before BaseT); then one BMCR restart.
 *              CONTROL_BEFORE  PENDING 3/0x0, next NONE, snapshot 0, before the write.
 *              LINK_EVENT, CONTEXT  the OS saw the link down and up after the write; one epoch and generation, no attach, detach, pause or
 *                          restart, no PHC reset. Otherwise INCONCLUSIVE and the status lines judge nothing.
 *              BEFORE_BASET, PAST_BASET  every reply after the link is back, and two past BaseT: S1 UNKNOWN with EVENT_UNRECONCILED, slot
 *                          reasons 0 (#362, #365 12.4/12.10). The transition itself is printed, not judged.
 *              LINK_RESTORE  link, rates and PHY as before (CLEANUP_FAILED / INCONCLUSIVE_RESTORATION); the owned NIC cycle follows anyway.
 *            The trigger is sent when no send is outstanding, waited for within bounds, never withheld because of it. It only schedules the
 *            restart: until the END of the restart was observed it counts as an open operation, so CLEANUP and RESTORE cannot PASS before
 *            (TC-MOCK-068, TC-MOCK-070).
 *   --phc-hold-control  case TC-TASACT-HOLDCTL-001 (#505): the Debug-only one-shot PHC reseed hold control
 *            (IOCTL_AVB_DEBUG_PHC_RESEED_HOLD, 0x0017A3C4) on every adapter advertising INTEL_CAP_BASIC_1588, through independent
 *            handles each bound to that adapter. Lines: UNBOUND (STATUS_INVALID_DEVICE_STATE), MALFORMED (exact refusals, state
 *            unchanged), ARM (new arm id and identity; a second ARM STATUS_DEVICE_BUSY), HOLD (a PHC_OFFSET_ADJUST on its own handle
 *            is observed HOLDING, then performs its real write and returns STATUS_SUCCESS; driver markers strictly ordered),
 *            ONESHOT, DISARM, EXPIRY, ACTIVATE_ARRIVAL (TSN_TAS adapters: an ACTIVATE with a zero metadata pair arriving while
 *            armed is recorded with its op id and STATE_CONFLICT, no commit) and CLEANUP (PHC steps compensated, then verified
 *            independently: snapshot registers read VALID before and after unchanged, the PHC continuous against wall time within
 *            1 ms, a readable control state neither ARMED nor HOLDING; a measured deviation is CLEANUP_FAILED, anything unmeasured
 *            INCONCLUSIVE). Against a Release driver (GET_VERSION, decided at run time: Release test builds do not define
 *            NDEBUG) the line RELEASE_ABSENT requires STATUS_INVALID_DEVICE_REQUEST and every other line is INCONCLUSIVE,
 *            never PASS. This is
 *            the control's own verification, not the #357 F1/F2 race result.
 *   --fault-versioned  TEST-TASACT-FAULT-001 (#482, Verifies #357 and #352), cases TC-TASACT-FAULT-001-F1 and -F2 on the 144-byte
 *            version-1 AVB_TAS_OP_REQUEST on IOCTL_AVB_TAS_ACTIVATE_80C (0x0017A030): HOLD, the bound NET_LUID, a distinct non-zero
 *            client_op_id, generation and epoch from the test's own query (0 printed "(not reported)", never fabricated),
 *            AVB_TAS_F_ACCEPT_UNVERIFIED (#362/#351/#365: it authorizes execution of an unqualified HOLD control, not
 *            HARDWARE_VERIFIED). The common numbered-write contract (#482): N = 1 is the first schedule write for every backend;
 *            no commit-write index is assumed. Per TAS adapter F1 then F2, each with its own handles, proved pre-state (recovered
 *            once by the owned NIC cycle when not proved), observer trial and owned NIC-cycle cleanup with its CLEANUP/RESTORE lines.
 *            A complete GET_VERSION reply classifies the driver: only a Debug driver gets a non-zero debug_fault (the Release
 *            refusal is #368's negative control, not claimed here); an unknown build sends nothing.
 *              METADATA    (both cases) the control needs a valid, current, non-zero generation/epoch pair from the test's own
 *                          pre-state query; without it the case is nonpassing (FAIL) and stays visible, whatever the request
 *                          completes with. The request is still sent with the query's values, never fabricated ones.
 *              F1  debug_fault = N 1, restore not failed (bit 8 clear):
 *                  COMPLETION  STATUS_IO_DEVICE_ERROR, Information 0 (FAILED_RESTORED 3 is the record's outcome; error completions
 *                              carry no output, record retrieval is #352/#379/#460 and not inferred from the status).
 *                  ROLLBACK    every measured restoration-set register equal to the test's own pre-state query; the record CLEAN
 *                              when restoration is established (#365 R8: a failure before the transmit-mode write preserves the
 *                              previous state if restoration is established). A measured difference is FAIL whatever else is
 *                              unread; a non-CLEAN record is FAIL once the registers establish restoration, or when no write was
 *                              attempted. (After a mode write reached hardware a later failure stays DIRTY even with matching
 *                              read-back, #362 decision 4 - not reachable with N = 1.)
 *                  GENERATION  advanced exactly once when reported before and after; 0 is not a value (INCONCLUSIVE).
 *              F2  debug_fault = N 1 | AVB_TAS_DEBUG_FAULT_RESTORE:
 *                  COMPLETION  STATUS_UNSUCCESSFUL, Information 0 (FAILED_NOT_RESTORED 4 is the record's outcome).
 *                  STATE       the record DIRTY (GET_TAS_STATE): the deliberately failed restore leaves restoration unestablished
 *                              (#365 R8, #362 decisions 4 and 5).
 *                  REFUSE      a further otherwise-valid HOLD (debug_fault 0, a new client_op_id) with the post-failure query's
 *                              metadata: STATUS_INVALID_DEVICE_STATE, Information 0, record and restoration set unchanged. #369's
 *                              rule: with reported metadata PASS; with generation or epoch 0 INCONCLUSIVE (NOT_CLEAN cannot be told
 *                              from STATE_CONFLICT); acceptance, output or a measured change FAIL.
 *            A STATUS_INVALID_DEVICE_STATE refusal (no output) of a request carrying the query's unreported metadata (generation or
 *            epoch 0) leaves only COMPLETION's fault-path attribution INCONCLUSIVE: 0 is never a valid value and is refused
 *            STATE_CONFLICT before any write (#352 L2, #365 13.6 D.1), so it does not show that the injected fault ran. The missing
 *            metadata stays the separate METADATA failure; with reported metadata that refusal is FAIL. A refusal never proves
 *            injection or rollback, and no generation advance is expected for a refusal before any write. #369's ambiguity rule
 *            applies to F2's subsequent REFUSE probe only.
 *            0x80C absent (STATUS_INVALID_DEVICE_REQUEST) is FAIL, never SKIP, and the experiment is not suppressed: every
 *            independently measurable result is still taken and reported, and a check whose fault path was not reached is
 *            INCONCLUSIVE (not exercised), never PASS, unless a measured change makes it FAIL. Coverage limits (#482): N = 1 only;
 *            no late-write, partial-schedule, PHC-recheck or read-back-failure control.
 *
 * Pre-state and setup (#369 "pre-state proved by the test itself through the TAS query"; #365 13.7, tests own setup): before
 * TAS_ACTIVATE the driver record is CLEAN (GET_TAS_STATE) and IOCTL_AVB_TAS_QUERY_HW returns a readable, clean reply (hw_read
 * READ, no register invalid, TSN mode off, both slots NONE, restoration COMPLETE). A pre-state that is not clean is not a veto:
 * the test recovers it once through its own NIC cycle, the recovery its cleanup uses, and proves it again. Only a pre-state
 * still not proved after that recovery is BLOCKED, with both observations printed. The proved reply is the restoration reference.
 * The recovery's own outcome is a separate SETUP line: CLEANUP_FAILED when the cycle did not complete or left observed residue
 * (record not CLEAN, or a readable query not clean), INCONCLUSIVE_RESTORATION when the state after it could not be measured. An
 * abort during the recovery cycle reports SETUP INCONCLUSIVE_RESTORATION with the operator action (report_abort): the recovery
 * is tracked from before the first disable call until its final state is verified or its failure recorded. A blocked adapter
 * does not stop the others; each runs its own setup and cleanup.
 *
 * Checks, each with its own verdict line <case>-<check>:
 *   ACCESS   a handle opened for reading only is refused TAS_ACTIVATE (FILE_WRITE_ACCESS, #362) and still serves GET_TAS_STATE.
 *            The request is invalid, so nothing is written even if the access check were missing.
 *   COMMIT   two bound handles race the same request: exactly one COMMITTED, the other BUSY or NOT_CLEAN (ADR-TSN-002).
 *   STATE    GET_TAS_STATE reports ARMED and the windows as the gate list it must report (#350, PR #397 review).
 *   REFUSE   a further activation on the armed adapter is NOT_CLEAN and changes no reported state.
 *   WIRE     tagged broadcast seen by the established wire observer (see OBSERVER), judged by tests/ioctl/tas_activate_eval.h against
 *            BaseT and the window.
 *   STATUS_* (#369 only, for #350) the schedule status from IOCTL_AVB_TAS_QUERY_HW, judged by tas_status_* in
 *            tests/ioctl/tas_activate_eval.h (TC-MOCK-071) against the published prerequisites only (#350, #365 12.2, 12.4, 12.7). The
 *            test measures each prerequisite itself: the COMMITTED reply, the PHC bracket on the right side of the requested BaseT, a
 *            PHC not below its value after the commit, TSN mode, windows and cycle as committed (the reply's own registers against the
 *            schedule, tas_status_match; not against an earlier reading), the same context and generation, and that
 *            the driver counted no status, PnP, pause, restart, attach or detach event with an unchanged link (that observation only: no
 *            complete event capture, no #472 EVENT). BLOCKED without a commit; a prerequisite or reply not measured is INCONCLUSIVE; an
 *            observed wrong or missing driver answer is FAIL. Neither a register readback nor BaseT having passed is evidence of an
 *            activation, and no line is HARDWARE_VERIFIED.
 *     STATUS_RECORD   before and past BaseT: current_commit RETAINED, the requested schedule, a bracket that is ordered, ends before
 *                     BaseT and lies between the test's own PHC readings around the race (C6).
 *                     Every line but STATUS_READONLY first asks whose reply it is: the committed request's epoch and its generation + 1.
 *                     In another context or generation a matching or deviating answer says nothing about this commit: INCONCLUSIVE.
 *     STATUS_PENDING  the query after REFUSE and LOOKUP, its bracket before BaseT: current PENDING without reasons.
 *     STATUS_ACTIVE   the first observable query with its bracket past BaseT (bounded wait, with or without a wire observer): the full
 *                     #369 requirement is INFERRED_ACTIVE without reasons. While K8 is not verified (TAS_STATUS_K8_VERIFIED, #472 EVENT)
 *                     #365 12.4 never returns it: then it is FAIL, and UNKNOWN only for LINK_EVENTS_UNOBSERVED is the published answer,
 *                     reported as the observed gap (SKIP_NOT_IMPLEMENTED, nonpassing; 12.6: this part stays not GREEN). A lost commit or
 *                     any other answer is FAIL. EVENT_UNRECONCILED is INCONCLUSIVE: no published rule sets or clears it.
 *     STATUS_NEXT     both queries: next NONE, no reasons, no CYCLE_MISMATCH, next_commit NONE (12.7, R1). QBVCYCLET_S is printed only.
 *                     Under a measured mismatch with the committed schedule (R2) PENDING, INFERRED_ACTIVE and next NONE are FAIL, the
 *                     correct R2 answer is INCONCLUSIVE (no positive control), any other answer FAIL. A comparison that cannot be
 *                     established is INCONCLUSIVE.
 *     STATUS_READONLY two queries in immediate succession, and across BaseT: generation and epoch unchanged.
 *   CLEANUP  the test's own NIC cycle ran and the adapter came back: primary cleanup, owned by this test.
 *   RESTORE  restoration as #361 states it (tas_act_restore in tests/ioctl/tas_activate_eval.h): restoration-set registers at
 *            baseline, TSN mode off, driver record clean, TX on the wire. The restoration set is DES-C-TAS-010 12.5's: the v1
 *            register set without DTXMXPKTSZ, BASET_L and BASET_H included. It is read back through IOCTL_AVB_TAS_QUERY_HW after the
 *            owned NIC cycle and compared register by register with the test's own pre-state reply, under the validity rule of
 *            test_tas_query_clean --restore (#377). Without a valid set before and after, the registers stay UNVERIFIED and
 *            restoration is at best INCONCLUSIVE_RESTORATION. It is never claimed from GET_TAS_STATE or from the driver's advisory
 *            restoration field. TX on the wire is judged only from tagged frames sendto accepted after the cycle (seen ratio,
 *            no long delay); unrelated captured traffic never decides it. Every attempt failing in sendto is an observed
 *            transmission failure; nothing attempted leaves the wire unmeasured. PHC continuity across the cycle is a separate
 *            evidence line, never a restoration input (#361).
 *   HOLD characterization  (--hold, evidence only, no verdict) the measurements a HOLD gate-window assertion needs: the raw
 *            tagged frames sent and seen by the capture after the activation (lost = sent - seen), the baseline's own sent/seen
 *            (the capture's loss before any mutation), sendto failures of both and the restoration class; and, only when every
 *            such frame was mapped to
 *            PHC time, the early, released, inside, near-edge and outside counts (otherwise reported unavailable, never as zeros).
 *            Limits, printed with it:
 *            the test cannot observe the driver's register write order (#365 13.5 records the source order); the capture is a
 *            passive, software-timestamped Npcap capture on the host clock through a switch, mapped to PHC time by two bracketed
 *            anchors; arrivals within TAS_ACT_EDGE_MARGIN_NS of an edge decide nothing; low frame rate (SEND_PERIOD_MS). So 0
 *            outside here does not by itself prove that no frame left outside a hardware window, and a WIRE PASS tolerates loss
 *            down to TAS_ACT_SEEN_MIN_PERCENT. These observations are not EG-02 qualification or a tier claim on their own.
 *
 * Schedule: 1 s cycle, one window 200 to 400 ms on all four queues, BaseT the first cycle boundary at least 1.5 s after the
 * PHC (or limits.min_margin_ns plus 500 ms when the query establishes a larger margin). That is the verified set of the E6
 * characterisation (LOCAL_DIAGNOSTIC, I226 0x125B REV_04, Debug), within the window rule of #357. Adapters are chosen by
 * INTEL_CAP_TSN_TAS; every TAS adapter runs (no fixed inventory: the identities are allocated from the driver's count).
 * OBSERVER (#369 amendment, #463): no fixed observer model and no execution veto. Every other enumerated adapter is tried,
 * without a count limit and one evidence line each, as the passive capture until one receives the baseline frames the adapter
 * under test sends. Without one, or without a link and an IPv4 address, WIRE and the cleanup's TX probe are not measured and the
 * other checks still run (no mode needs a connected link for its request: --fault's withdrawn legacy request is refused
 * whatever caller link speed it carries).
 * The PCI revision and the loaded driver image with its SHA256 are printed as evidence, never used for selection.
 *
 * Mutations: TAS_ACTIVATE, bounded broadcast UDP and ICMP on the adapter under test, and the NIC cycle of that adapter
 * (CM_Disable_DevNode / CM_Enable_DevNode), which resets its PHC. The cycle runs after every TAS_ACTIVATE issued, whatever
 * it returned, and once before it when the pre-state needs recovery. Dedicated IOCTLs only: no READ_REGISTER, no
 * WRITE_REGISTER, except --metadata-events SETUP_NOWRITE (SYSTIML/SYSTIMH through the Debug-only WRITE_REGISTER) and META-005, -006 (the
 * approved exception there, per run: nine READ_REGISTER, ENDQT3 written and restored); META-007 writes one BMCR auto-negotiation
 * restart through IOCTL_AVB_MDIO_WRITE (approved 2026-11-26); that mode also
 * sends SET_TIMESTAMP, PHC_OFFSET_ADJUST, SETUP_TAS, DISARM_TAS, SET_HW_TIMESTAMPING, INIT_DEVICE and one filter restart. Runs on Debug and Release drivers.
 *
 * Exit: 0 all passed, 1 any nonpassing verdict, 2 no adapter ran, 3 only BLOCKED.
 *
 * Verifies: #357
 * Verifies: #350
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <cfgmgr32.h>
#include <psapi.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/avb_ioctl.h"
#include "../../include/intel_pci_ids.h"
#include "../ioctl/tas_activate_eval.h"
#include "../ioctl/owned_worker.h"         /* worker and storage ownership beyond a bounded wait (#505, PR #518) */
#include "../ioctl/owned_mutation.h"       /* META-005: the deviation and its restoration against an abort (#369, PR #524; TC-MOCK-077) */
#include "../ioctl/tas_activate_cases.h"   /* contained cases, selection, exit status (#487) */
#include "../ioctl/tas_lifecycle_eval.h"   /* instance lookup and lifecycle outcome of #364 (META-004) */
#include "../ioctl/tas_link_event_eval.h"   /* META-007: OS readings around a link change (#472, unchanged) */
#include "../ioctl/tas_link_status_eval.h"  /* META-007: the query after a link change, checked offline by TC-MOCK-078 */
typedef int NDIS_PARAMETER_TYPE;           /* a kernel type in structures of filteruser.h that this test does not use (as in #364) */
#include "../../src/filteruser.h"          /* IOCTL_FILTER_RESTART_ONE_INSTANCE, IOCTL_FILTER_ENUERATE_ALL_INSTANCES */
#include "../common/avb_ntstatus.h"   /* exact NTSTATUS: STATUS_NOT_IMPLEMENTED and an unknown IOCTL share Win32 error 1 */
#include "../ioctl/link_phy_restart.h"     /* META-007: the guarded auto-negotiation restart (copy of #472's helpers) */
#include "tas_wire_observer.h"

#define DEVICE_NAME          "\\\\.\\IntelAvbFilter"
#define DRIVER_IMAGE         "intelavbfilter.sys"
#define NS_PER_MS            1000000LL
#define NS_PER_SEC           1000000000LL
#define CYCLE_NS             (1000LL * NS_PER_MS)
#define WINDOW_START_NS      (200LL * NS_PER_MS)
#define WINDOW_END_NS        (400LL * NS_PER_MS)
#define BASE_LEAD_NS         (1500LL * NS_PER_MS)    /* BaseT is the first cycle boundary at least this far ahead */
#define SEND_CYCLES_AFTER    3                       /* frames keep going until BaseT plus this many cycles and a half */
#define SEND_PERIOD_MS       50
#define SEND_BOUND_MS        66000                   /* the send phase ends here even if the PHC cannot be read; covers the
                                                        largest admitted lead (LEAD_MAX_NS, C_ASSERT below the lead bounds) */
#define DRAIN_MS             1500                    /* longer than a closed interval, so held frames leave before the end */
#define PAYLOAD_BYTES        256
#define FRAME_OVERHEAD_BYTES 38u                     /* MTU plus header, FCS, preamble and gap: the frame on the wire */
#define BASELINE_MS          2000
#define ANCHOR_READS         5
#define FAULT_WRITE          13u                     /* HOLD's commit write, so everything before it reached the adapter */
#define UNGATED_DELAY_US     500000LL                /* after the cycle: a gate of this schedule holds most frames longer */
#define PHC_TOLERANCE_NS     (2 * NS_PER_SEC)
#define DISABLE_BOUND_MS     120000
#define ENABLE_BOUND_MS      60000
#define RETURN_BOUND_MS      60000
#define SETTLE_MS            5000
#define FILETIME_TO_UNIX_US  11644473600000000LL
#define NET_CLASS_KEY        "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\"

/* #482: the common numbered-write contract - N = 1 is the first schedule write for every backend (published, not a commit index) */
#define FAULTV_FIRST_SCHEDULE_WRITE 1u

#define META_OFFSET_NS        1000LL                  /* --metadata-events: the PHC_OFFSET_ADJUST step (+1 us) */
#define META_SET_AHEAD_NS     NS_PER_SEC              /* --metadata-events: SET_TIMESTAMP to the current PHC + 1 s (forward) */
#define META_RACE_HOLD_MS     1000u                   /* META-002: the reseed is held this long; the ACTIVATE must arrive in it */

static RUN_MODE g_mode;
#define META_BACK_NS          (5 * NS_PER_SEC)        /* META-003: the held reseed steps the PHC back by this much */
#define META_STATUS_STEP_NS   (100LL * NS_PER_MS)     /* STATUS_OFFSET: a step that can be measured against wall time (META-003: back) */
static int g_meta_case;                              /* --metadata-events: 1 to 7 = META-001 to -007 (the run_adapter call in progress) */
static int g_fault_case;                             /* --fault-versioned: 1 = F1, 2 = F2 (the run_adapter call in progress) */
static const char *g_case;
static int g_failed, g_blocked, g_ran;   /* g_ran: case executions (run_adapter calls); --fault-versioned runs F1 and F2 per adapter */
static int g_tas_adapters;                /* physical TAS-capable adapters run, counted once each (PR #489) */
static avb_u64 g_capture_luid;
static unsigned g_next_seq;
static LONGLONG g_send_us[WIRE_MAX_FRAMES];
static const AVB_ADAPTER_IDENTITY_REQUEST *g_all;   /* every validated identity: the observer candidates */
static UINT32 g_nall;
static BOOL g_cleanup_link_was_up = TRUE;            /* cleanup: the link before the test (an adapter down before is not a failure) */
static OWNED_OP *g_ops;                              /* operations this process issued in workers and has not seen end (key: NET_LUID) */
#define OPS_SETTLE_MS        30000u                  /* further bounded wait per such operation when the cleanup starts */
static avb_u64 g_op_id;                              /* client_op_id of the next versioned request: non-zero, distinct */

#define LEAD_HEADROOM_NS     (500LL * NS_PER_MS)    /* test scheduling headroom above an established limits.min_margin_ns */
#define LEAD_MAX_NS          (60LL * NS_PER_SEC)    /* a reported margin needing more lead than this is not used (BLOCKED) */
/* PR #481: BaseT is at most LEAD_MAX_NS plus one cycle (rounding to a boundary) after the PHC read, so the send phase reaches
 * BaseT + SEND_CYCLES_AFTER cycles and a half within SEND_BOUND_MS for every admitted lead, and its frames, the observer
 * baseline and the cleanup's TX probe fit the capture's tag slots */
C_ASSERT((long long)SEND_BOUND_MS * NS_PER_MS >= LEAD_MAX_NS + CYCLE_NS + SEND_CYCLES_AFTER * CYCLE_NS + CYCLE_NS / 2);
C_ASSERT(SEND_BOUND_MS / SEND_PERIOD_MS + 1 + 2 * (BASELINE_MS / SEND_PERIOD_MS + 1) <= WIRE_MAX_FRAMES);

/* Non-negative while the test owns an unreported state change of an adapter: from its setup recovery NIC cycle until that
 * recovery is verified or its failure reported (g_pending_setup 1), and from TAS_ACTIVATE until the cleanup has reported */
static volatile LONG g_pending_adapter = -1;
static volatile LONG g_pending_did;
static volatile LONG g_pending_setup;
/* The run (#487): the selected cases, the one in progress (-1: none) and the results of the finished ones */
static BOOL g_selected[TAS_ACT_CASE_COUNT];
static int g_case_result[TAS_ACT_CASE_COUNT];
static volatile LONG g_case_pos = -1;

/* An unreported state change the case in progress owns: named, never dropped. TRUE when there was one */
static BOOL report_pending(void)
{
    LONG adapter = InterlockedExchange(&g_pending_adapter, -1);

    if (adapter >= 0 && InterlockedCompareExchange(&g_pending_setup, 0, 0) != 0) {
        printf("[INCONCLUSIVE_RESTORATION] %s-SETUP/adapter:%ld did=0x%04lX: test aborted during its setup recovery NIC cycle, before any "
               "TAS_ACTIVATE; the adapter may be left disabled; operator: NIC cycle of this adapter, then TC-TSNREG-001\n",
               g_case, adapter, g_pending_did);
        fflush(stdout);
    } else if (adapter >= 0) {
        printf("[INCONCLUSIVE_RESTORATION] %s-RESTORE/adapter:%ld did=0x%04lX: test aborted after TAS_ACTIVATE, its cleanup did not run; "
               "operator: NIC cycle of this adapter, then TC-TSNREG-001\n", g_case, adapter, g_pending_did);
        fflush(stdout);
    }
    return adapter >= 0;
}

static void report_abort(void)
{
    LONG pos = InterlockedExchange(&g_case_pos, -1);
    unsigned k;

    (void)report_pending();
    /* An ended process runs no further case: every selected one without a result is named, never silent */
    for (k = pos >= 0 ? (unsigned)pos : TAS_ACT_CASE_COUNT; k < TAS_ACT_CASE_COUNT; k++) {
        if (g_selected[k]) {
            printf("[INCONCLUSIVE] %s%s: %s: the program ended during case %s\n", g_tas_act_cases[k].case_id, g_tas_act_cases[k].arg,
                   k == (unsigned)pos ? "not completed" : "not executed", g_tas_act_cases[pos].arg);
            fflush(stdout);
        }
    }
}

/* ---- --metadata-events, TC-TASACT-META-005 (#369): the STATUS oracle of --hold under a real deviation of a compared register ----
 * EXCEPTION, approved by Q on 2026-11-25 for this case alone: the Debug-only generic READ_REGISTER / WRITE_REGISTER, one deviation of
 * ENDQT3.END_TIME at the generated SSOT offset and the restoration of its exact raw value. It is no permission for generic register writes in
 * regression tests. Extended by Q the same day by exactly one further run, TC-TASACT-META-006: the same deviation and restoration at the same
 * offset under its own guard, restored by the abort path from another thread. Nothing else is written: no mode, no cycle, no BaseT, no other bit of the register. Every access uses the one bound
 * handle of the case. Nothing is written unless every precondition below was measured. Once the write was sent the restoration runs on every
 * way out (also on Ctrl-C and on an unhandled exception); a process that is terminated from outside cannot restore, the NIC cycle is then owed.
 * An abort runs on another thread: it prevents a write that was not issued, or waits M5_ABORT_MS for the end of an issued one and restores only
 * then (owned_mutation.h). An end that was not seen stays INCONCLUSIVE_RESTORATION; the case itself still restores as long as it runs.
 * M5_ABORT_MS bounds the wait for the end of the write, not the restoration: that request is synchronous and has no bound of its own.
 * The accesses use a duplicate of the case's bound handle (the same file object and binding) that the change owns until all of them have ended:
 * the cleanup closes the case's own handle only. A duplicate still open then is named, and nothing is closed early to let the cycle pass.
 */
#define M5_LEAD_NS        (3 * NS_PER_SEC)        /* BaseT at least this far ahead of the commit */
#define M5_MARGIN_NS      (1500LL * NS_PER_MS)    /* PHC before BaseT immediately before the write: measured, never assumed from a duration */
#define M5_END_NS         450000000u              /* the deviated END_TIME of queue 3 (committed: WINDOW_END_NS, 400 ms) */
#define M5_REGS           9u
#define M5_ABORT_MS       3000u                   /* an abort waits this long for the end of an issued write (a console close ends the process after 5 s) */

/* One run of META-005 or -006: its change, what it writes, what the restoration measured, and in which order. Never freed: an abort may hold it */
typedef struct M5_RUN {
    OWNED_MUTATION m;
    const char *case_id;
    HANDLE own;              /* value of the change's own handle, for the evidence only: the accesses read m.handle */
    avb_u64 luid;
    avb_u32 offset, raw, deviated, back, value;
    LONG wst, bst, write_st, read_st;
    volatile LONG seq, writes, restores;                             /* order of the events below; the two register writes sent */
    LONG write_end, abort_begin, restore_begin, restore_end;         /* their places in that order, 0: did not happen */
    DWORD write_tid, abort_tid, restore_tid;
    BOOL programmatic;                                               /* META-006: the abort is called by the case */
    volatile LONG abort;                                             /* OWNED_ABORT + 1 of that call */
    struct M5_RUN *next;
} M5_RUN;

static M5_RUN *volatile g_m5_runs;   /* every run of this process, the newest first */
static volatile LONG g_m5_stop;      /* a real abort came: nothing is issued any more, in no run */

static M5_RUN *m5_new(avb_u64 luid)
{
    M5_RUN *r = (M5_RUN *)calloc(1, sizeof(*r));

    if (r == NULL || !owned_mutation_prepare(&r->m)) {
        if (r != NULL) { owned_mutation_unprepare(&r->m); }
        free(r);
        return NULL;
    }
    r->case_id = g_case;
    r->luid = luid;
    r->wst = r->bst = (LONG)AVB_NTSTATUS_UNAVAILABLE;
    r->m.stop = &g_m5_stop;
    r->m.ctx = r;
    r->next = g_m5_runs;
    InterlockedExchangePointer((PVOID volatile *)&g_m5_runs, r);
    return r;
}

/** TRUE when a deviation of this adapter or its restoration has not been seen to end: no final state can be judged */
static BOOL m5_pending(avb_u64 luid)
{
    M5_RUN *r;

    for (r = g_m5_runs; r != NULL; r = r->next) {
        if (r->luid == luid && owned_mutation_open(&r->m)) {
            return TRUE;
        }
    }
    return FALSE;
}

/** Evidence of the cleanup: what is still open on this adapter, and that its handle is kept */
static void m5_report_open(avb_u64 luid, const char *when)
{
    M5_RUN *r;

    for (r = g_m5_runs; r != NULL; r = r->next) {
        if (r->luid == luid && owned_mutation_open(&r->m)) {
            printf("    [EVIDENCE] %s, %s: the ENDQT3 deviation or its restoration is open (state %ld); the change's own handle is kept until it has "
                   "ended. The results of this cycle are as printed, nothing is closed for it\n", r->case_id, when, owned_mutation_state(&r->m));
        }
    }
}

#ifndef NDEBUG
static LONG m5_access(HANDLE h, DWORD code, avb_u32 offset, avb_u32 *value)
{
    AVB_REGISTER_REQUEST r;
    DWORD information = 0;
    LONG st;

    ZeroMemory(&r, sizeof(r));
    r.offset = offset;
    r.value = *value;
    st = avb_ioctl_ntstatus(h, code, &r, sizeof(r), &r, sizeof(r), &information);
    if (st == 0 && (information != (DWORD)sizeof(r) || r.status != 0 || r.offset != offset)) {
        st = (LONG)STATUS_UNSUCCESSFUL;
    }
    if (st == 0) {
        *value = r.value;
    }
    return st;
}

static void m5_write(void *ctx)
{
    M5_RUN *r = (M5_RUN *)ctx;
    avb_u32 v = r->deviated;

    InterlockedIncrement(&r->writes);
    r->write_tid = GetCurrentThreadId();
    r->wst = m5_access(r->m.handle, IOCTL_AVB_WRITE_REGISTER, r->offset, &v);
    r->write_end = InterlockedIncrement(&r->seq);
}

/** Back to the raw value read before the write, then read. Once, on one thread, after the write ended (owned_mutation.h) */
static LONG m5_restore(void *ctx)
{
    M5_RUN *r = (M5_RUN *)ctx;
    avb_u32 value = r->raw;

    InterlockedIncrement(&r->restores);
    r->restore_tid = GetCurrentThreadId();
    r->restore_begin = InterlockedIncrement(&r->seq);
    r->write_st = m5_access(r->m.handle, IOCTL_AVB_WRITE_REGISTER, r->offset, &value);
    value = 0;
    r->read_st = m5_access(r->m.handle, IOCTL_AVB_READ_REGISTER, r->offset, &value);
    r->value = value;
    r->restore_end = InterlockedIncrement(&r->seq);
    return r->read_st != 0 ? M5_RESTORE_UNREAD : (value == r->raw ? M5_RESTORE_VERIFIED : M5_RESTORE_DIFFERS);
}
#endif

/** The common abort path (console handler, exception filter, META-006): every run that did not reach its own restoration is named, and
 *  restored from here once its write has ended. META-006 calls it itself: its line is evidence there, not a verdict */
static void m5_abort(void)
{
    M5_RUN *r;

    for (r = g_m5_runs; r != NULL; r = r->next) {
        OWNED_ABORT a = owned_mutation_abort(&r->m, M5_ABORT_MS);
        LONG result = InterlockedCompareExchange(&r->m.restored, 0, 0);
        const char *tag = m5_abort_tag(a, result);

        if (r->programmatic) {
            InterlockedExchange(&r->abort, (LONG)a + 1);
        }
        if (a == OWNED_ABORT_NOTHING) {
            continue;
        }
        printf(r->programmatic ? "    [EVIDENCE] programmatic abort, not a verdict: %s %s-REG_RESTORE: " : "[%s] %s-REG_RESTORE: ", tag, r->case_id);
        if (a == OWNED_ABORT_PREVENTED) {
            printf("aborted before the ENDQT3 deviation was sent: it is not sent any more\n");
        } else if (a == OWNED_ABORT_RESTORED) {
            printf("aborted with the ENDQT3 deviation sent and ended; write 0x%08lX, read 0x%08lX value 0x%08X (raw before 0x%08X); operator: NIC cycle of "
                   "this adapter\n", (unsigned long)r->write_st, (unsigned long)r->read_st, r->value, r->raw);
        } else {
            printf("aborted with the ENDQT3 deviation sent; the end of the write or of its restoration was not seen within %u ms: not restored by the "
                   "abort (raw before 0x%08X); operator: NIC cycle of this adapter\n", M5_ABORT_MS, r->raw);
        }
    }
    fflush(stdout);
}

/** A real abort: the process is ending */
static void m5_end(void)
{
    InterlockedExchange(&g_m5_stop, 1);
    m5_abort();
}

/* META-007: from before the MDIO restart until LINK_RESTORE has reported */
static volatile LONG g_m7_adapter = -1;
static volatile LONG g_m7_did;

static void m7_report_abort(void)
{
    LONG adapter = InterlockedExchange(&g_m7_adapter, -1);

    if (adapter >= 0) {
        printf("[INCONCLUSIVE_RESTORATION] %s-LINK_RESTORE/adapter:%ld did=0x%04lX: aborted after the auto-negotiation restart; link and PHY were not "
               "checked afterwards and the owned NIC cycle did not run. Operator: check the link state and speed, NIC cycle this adapter\n",
               g_case, adapter, g_m7_did);
        fflush(stdout);
    }
}

static BOOL WINAPI on_console_ctrl(DWORD type) { (void)type; m5_end(); m7_report_abort(); report_abort(); return FALSE; }
static LONG WINAPI on_crash(EXCEPTION_POINTERS *info) { (void)info; m5_end(); m7_report_abort(); report_abort(); return EXCEPTION_CONTINUE_SEARCH; }

/* One verdict line. Nonpassing tags count as failures, BLOCKED separately */
static void verdict(const char *tag, const char *check, UINT32 adapter, avb_u16 did, const char *reason)
{
    printf("[%s] %s-%s/adapter:%u did=0x%04X%s%s\n", tag, g_case, check, adapter, did, reason ? ": " : "", reason ? reason : "");
    fflush(stdout);
    if (strcmp(tag, "BLOCKED") == 0) {
        g_blocked++;
    } else if (strcmp(tag, "PASS") != 0) {
        g_failed++;
    }
}

static LONGLONG host_us(void)
{
    FILETIME ft;
    ULARGE_INTEGER t;

    GetSystemTimePreciseAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return (LONGLONG)(t.QuadPart / 10) - FILETIME_TO_UNIX_US;
}

static HANDLE open_device(DWORD access)
{
    return CreateFileA(DEVICE_NAME, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static BOOL identity_at(HANDLE h, UINT32 index, AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    DWORD br = 0;
    ZeroMemory(id, sizeof(*id));
    id->version = AVB_ADAPTER_IDENTITY_VERSION;
    id->index = index;
    return DeviceIoControl(h, IOCTL_AVB_GET_ADAPTER_IDENTITY, id, sizeof(*id), id, sizeof(*id), &br, NULL);
}

static BOOL bind_luid(HANDLE h, avb_u64 luid)
{
    AVB_OPEN_BY_LUID_REQUEST bind = {0};
    DWORD br = 0;
    bind.version = AVB_OPEN_BY_LUID_VERSION;
    bind.net_luid = luid;
    return DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER_BY_LUID, &bind, sizeof(bind), &bind, sizeof(bind), &br, NULL);
}

static HANDLE open_bound(DWORD access, avb_u64 luid)
{
    HANDLE h = open_device(access);
    if (h != INVALID_HANDLE_VALUE && !bind_luid(h, luid)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

static BOOL read_state(HANDLE h, AVB_TAS_STATE *s)
{
    DWORD br = 0;
    ZeroMemory(s, sizeof(*s));
    return DeviceIoControl(h, IOCTL_AVB_GET_TAS_STATE, s, sizeof(*s), s, sizeof(*s), &br, NULL) && s->status == 0;
}

static BOOL read_phc(HANDLE h, long long *phc)
{
    AVB_CLOCK_CONFIG c;
    DWORD br = 0;
    ZeroMemory(&c, sizeof(c));
    if (!DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &c, sizeof(c), &c, sizeof(c), &br, NULL) || c.status != 0) {
        return FALSE;
    }
    *phc = (long long)c.systim;
    return TRUE;
}

static BOOL read_stats(HANDLE h, AVB_DRIVER_STATISTICS *s)
{
    DWORD br = 0;
    ZeroMemory(s, sizeof(*s));
    return DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS, s, sizeof(*s), s, sizeof(*s), &br, NULL);
}

static BOOL read_if(avb_u64 luid, MIB_IF_ROW2 *row)
{
    ZeroMemory(row, sizeof(*row));
    row->InterfaceLuid.Value = luid;
    return GetIfEntry2(row) == NO_ERROR;
}

/** The tightest of a few bracketed PHC reads: host midpoint and PHC, the line anchor for capture times */
static BOOL take_anchor(HANDLE h, TAS_ACT_ANCHOR *a, const char *label)
{
    LONGLONG best = -1;
    int i;

    for (i = 0; i < ANCHOR_READS; i++) {
        LONGLONG before = host_us(), after;
        long long phc;
        if (!read_phc(h, &phc)) {
            continue;
        }
        after = host_us();
        if (best < 0 || after - before < best) {
            best = after - before;
            a->host_us = before + (after - before) / 2;
            a->phc_ns = phc;
        }
    }
    if (best < 0) {
        ZeroMemory(a, sizeof(*a));
        printf("    anchor %s: unavailable (all PHC reads failed)\n", label);
        return FALSE;
    }
    printf("    anchor %s: hostUs=%lld phcNs=%lld bracketUs=%lld\n", label, a->host_us, a->phc_ns, best);
    return TRUE;
}

/** Finds the adapter's IPv4 address, and whether another adapter keeps the machine reachable */
static BOOL find_addressing(avb_u64 luid, IN_ADDR *ipv4, BOOL *other_link)
{
    ULONG size = 32 * 1024;
    IP_ADAPTER_ADDRESSES *list = (IP_ADAPTER_ADDRESSES *)malloc(size), *a;
    BOOL found = FALSE;

    *other_link = FALSE;
    if (list == NULL || GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, list, &size) != NO_ERROR) {
        free(list);
        return FALSE;
    }
    for (a = list; a != NULL; a = a->Next) {
        if (a->Luid.Value == luid) {
            if (a->FirstUnicastAddress != NULL) {
                *ipv4 = ((SOCKADDR_IN *)a->FirstUnicastAddress->Address.lpSockaddr)->sin_addr;
                found = TRUE;
            }
        } else if (a->OperStatus == IfOperStatusUp && a->FirstGatewayAddress != NULL) {
            *other_link = TRUE;
        }
    }
    free(list);
    return found;
}

static SOCKET open_tx_socket(IN_ADDR ipv4)
{
    SOCKADDR_IN local = {0};
    BOOL on = TRUE;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DWORD start = GetTickCount();

    local.sin_family = AF_INET;
    local.sin_addr = ipv4;
    if (sock == INVALID_SOCKET || setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on)) != 0) {
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        return INVALID_SOCKET;
    }
    while (bind(sock, (SOCKADDR *)&local, sizeof(local)) != 0) {
        if (GetTickCount() - start >= RETURN_BOUND_MS) {
            closesocket(sock);
            return INVALID_SOCKET;
        }
        Sleep(1000);
    }
    return sock;
}

#define SEND_OK        1    /* sendto accepted the frame: it has a sequence number and a send time */
#define SEND_FAILED    0    /* sendto failed: an observed failure to transmit (g_last_send_error) */
#define SEND_NO_SLOT   (-1) /* no capture slot left (WIRE_MAX_FRAMES): nothing was attempted, nothing measured */

static int g_last_send_error;

/** One tagged broadcast frame; its host send time is kept by sequence number. Returns SEND_OK, SEND_FAILED or SEND_NO_SLOT */
static int send_tagged(SOCKET sock, unsigned nonce)
{
    static unsigned char payload[PAYLOAD_BYTES];
    SOCKADDR_IN peer = {0};

    if (g_next_seq >= WIRE_MAX_FRAMES) {
        return SEND_NO_SLOT;
    }
    peer.sin_family = AF_INET;
    peer.sin_port = htons(TAS_WIRE_UDP_PORT);
    peer.sin_addr.s_addr = INADDR_BROADCAST;
    tas_wire_fill(payload, sizeof(payload), nonce, g_next_seq);
    g_send_us[g_next_seq] = host_us();
    if (sendto(sock, (const char *)payload, (int)sizeof(payload), 0, (SOCKADDR *)&peer, sizeof(peer)) == SOCKET_ERROR) {
        g_last_send_error = WSAGetLastError();
        return SEND_FAILED;
    }
    g_next_seq++;
    return SEND_OK;
}

/** Sends for ms, drains, and returns how many of these frames the observer saw. sent: frames sendto accepted; failed: sendto
 *  failures; delayed: seen later than UNGATED_DELAY_US */
static unsigned send_window(SOCKET sock, unsigned nonce, DWORD ms, unsigned *sent, unsigned *failed, unsigned *delayed)
{
    unsigned first = g_next_seq, seq, seen = 0;
    DWORD start = GetTickCount();

    *delayed = 0;
    *failed = 0;
    while (GetTickCount() - start < ms) {
        if (send_tagged(sock, nonce) == SEND_FAILED) {
            (*failed)++;
        }
        Sleep(SEND_PERIOD_MS);
    }
    Sleep(DRAIN_MS);
    *sent = g_next_seq - first;
    for (seq = first; seq < g_next_seq; seq++) {
        if (g_wire_seen[seq] != 0) {
            seen++;
            if (g_wire_rx_us[seq] - g_send_us[seq] > UNGATED_DELAY_US) {
                (*delayed)++;
            }
        }
    }
    return seen;
}

/** Image and SHA256 of the driver Windows has loaded: the runner's System32 hash may name another file (#357 HIL record) */
static void driver_provenance(void)
{
    LPVOID bases[1024];
    DWORD needed = 0, i;
    char name[MAX_PATH], path[MAX_PATH * 2];

    if (!EnumDeviceDrivers(bases, sizeof(bases), &needed)) {
        printf("    [EVIDENCE] loaded driver image: unknown (EnumDeviceDrivers error %lu)\n", GetLastError());
        return;
    }
    for (i = 0; i < needed / sizeof(bases[0]); i++) {
        if (GetDeviceDriverBaseNameA(bases[i], name, sizeof(name)) == 0 || _stricmp(name, DRIVER_IMAGE) != 0 ||
            GetDeviceDriverFileNameA(bases[i], name, sizeof(name)) == 0) {
            continue;
        }
        if (_strnicmp(name, "\\SystemRoot\\", 12) == 0) {
            char root[MAX_PATH];
            GetWindowsDirectoryA(root, sizeof(root));
            sprintf_s(path, sizeof(path), "%s\\%s", root, name + 12);
        } else {
            strcpy_s(path, sizeof(path), name);
        }
        {
            BCRYPT_ALG_HANDLE alg = NULL;
            BCRYPT_HASH_HANDLE hash = NULL;
            unsigned char digest[32], buf[65536];
            HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            DWORD got = 0;
            BOOL ok = f != INVALID_HANDLE_VALUE && BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
                      BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0;
            int k;

            while (ok) {
                if (!ReadFile(f, buf, sizeof(buf), &got, NULL)) {
                    ok = FALSE;
                    break;
                }
                if (got == 0) { break; }
                ok = BCryptHashData(hash, buf, got, 0) == 0;
            }
            ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
            printf("    [EVIDENCE] loaded driver image: %s sha256=", path);
            for (k = 0; ok && k < 32; k++) { printf("%02X", digest[k]); }
            printf("%s\n", ok ? "" : "unreadable");
            if (hash != NULL) { BCryptDestroyHash(hash); }
            if (alg != NULL) { BCryptCloseAlgorithmProvider(alg, 0); }
            if (f != INVALID_HANDLE_VALUE) { CloseHandle(f); }
        }
        return;
    }
    printf("    [EVIDENCE] loaded driver image: %s is not loaded\n", DRIVER_IMAGE);
}

typedef struct { DEVINST inst; BOOL enable; CONFIGRET result; } CM_CALL;

static DWORD WINAPI cm_thread(LPVOID arg)
{
    CM_CALL *call = (CM_CALL *)arg;
    call->result = call->enable ? CM_Enable_DevNode(call->inst, 0) : CM_Disable_DevNode(call->inst, 0);
    return 0;
}

/** One device-node call with a bound: a stalled disable must end in a verdict, not a hung test */
static BOOL cm_bounded(DEVINST inst, BOOL enable, DWORD bound_ms)
{
    CM_CALL *call = (CM_CALL *)calloc(1, sizeof(*call));
    HANDLE thread;
    BOOL ok;

    if (call == NULL) {
        return FALSE;
    }
    call->inst = inst;
    call->enable = enable;
    call->result = CR_FAILURE;
    thread = CreateThread(NULL, 0, cm_thread, call, 0, NULL);
    if (thread == NULL) {
        free(call);
        return FALSE;
    }
    if (WaitForSingleObject(thread, bound_ms) != WAIT_OBJECT_0) {
        CloseHandle(thread);   /* call must outlive the worker thread; leaked on purpose */
        printf("    %s did not return within %lu ms\n", enable ? "CM_Enable_DevNode" : "CM_Disable_DevNode", bound_ms);
        return FALSE;
    }
    CloseHandle(thread);
    printf("    %s result=0x%08X\n", enable ? "CM_Enable_DevNode" : "CM_Disable_DevNode", (unsigned)call->result);
    ok = (call->result == CR_SUCCESS);
    free(call);
    return ok;
}

/** NET_LUID to exactly this adapter's device node, never by model or name. Prints the instance, which carries the revision */
static BOOL find_devinst(avb_u64 luid, DEVINST *inst)
{
    NET_LUID l;
    GUID guid;
    char key[256], pnp[512];
    DWORD size = sizeof(pnp);

    l.Value = luid;
    if (ConvertInterfaceLuidToGuid(&l, &guid) != NO_ERROR) {
        return FALSE;
    }
    sprintf_s(key, sizeof(key), NET_CLASS_KEY "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\\Connection",
              guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
              guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, "PnPInstanceId", RRF_RT_REG_SZ, NULL, pnp, &size) != ERROR_SUCCESS) {
        return FALSE;
    }
    printf("    [EVIDENCE] device instance: %s\n", pnp);
    return CM_Locate_DevNodeA(inst, pnp, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
}

/** Waits until the adapter is back with the same identity and a handle is bound to it */
static HANDLE wait_for_adapter(const AVB_ADAPTER_IDENTITY_REQUEST *want)
{
    DWORD start = GetTickCount();

    while (GetTickCount() - start < RETURN_BOUND_MS) {
        HANDLE h = open_device(GENERIC_READ | GENERIC_WRITE);
        if (h != INVALID_HANDLE_VALUE) {
            AVB_ADAPTER_IDENTITY_REQUEST id;
            UINT32 i, count = 1;
            for (i = 0; i < count && identity_at(h, i, &id); i++) {
                count = id.count;
                if (id.net_luid == want->net_luid && id.device_id == want->device_id &&
                    memcmp(id.mac, want->mac, sizeof(id.mac)) == 0 && bind_luid(h, want->net_luid)) {
                    return h;
                }
            }
            CloseHandle(h);
        }
        Sleep(1000);
    }
    return INVALID_HANDLE_VALUE;
}

static const char *result_name(avb_u32 r)
{
    switch (r) {
    case AVB_TAS_ACT_COMMITTED:           return "COMMITTED";
    case AVB_TAS_ACT_INVALID:             return "INVALID";
    case AVB_TAS_ACT_NOT_CLEAN:           return "NOT_CLEAN";
    case AVB_TAS_ACT_FAILED_RESTORED:     return "FAILED_RESTORED";
    case AVB_TAS_ACT_FAILED_NOT_RESTORED: return "FAILED_NOT_RESTORED";
    case AVB_TAS_ACT_BASE_PASSED:         return "BASE_PASSED";
    case AVB_TAS_ACT_COMMITTED_UNTIMED:   return "COMMITTED_UNTIMED";
    case AVB_TAS_ACT_BUSY:                return "BUSY";
    default:                              return "UNKNOWN";
    }
}

typedef struct {
    HANDLE h, go;
    AVB_TAS_ACTIVATE_REQUEST r;
    BOOL ok;
    DWORD error;
} ACT_CALL;

static BOOL activate(HANDLE h, AVB_TAS_ACTIVATE_REQUEST *r, DWORD *error)
{
    DWORD br = 0;
    *error = ERROR_SUCCESS;
    if (!DeviceIoControl(h, IOCTL_AVB_TAS_ACTIVATE_LEGACY_FN71, r, sizeof(*r), r, sizeof(*r), &br, NULL)) {
        *error = GetLastError();
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI act_thread(LPVOID arg)
{
    ACT_CALL *c = (ACT_CALL *)arg;
    (void)WaitForSingleObject(c->go, INFINITE);
    c->ok = activate(c->h, &c->r, &c->error);
    return 0;
}

static void print_call(const char *label, BOOL ok, DWORD error, const AVB_TAS_ACTIVATE_REQUEST *r)
{
    printf("    %s: ok=%d win32=%lu result=%s(%u) status=0x%08X phcAtCommit=%llu\n", label, ok, error,
           ok ? result_name(r->result) : "-", ok ? r->result : 0, ok ? r->status : 0, ok ? r->phc_at_commit_ns : 0ULL);
}

static void build_request(AVB_TAS_ACTIVATE_REQUEST *r, avb_u32 pre, long long base, avb_u32 mbps, avb_u32 frame)
{
    unsigned q;

    ZeroMemory(r, sizeof(*r));
    r->version = AVB_TAS_ACTIVATE_VERSION;
    r->pre_activation = pre;
    r->base_time_ns = (avb_u64)base;
    r->cycle_ns = (avb_u32)CYCLE_NS;
    r->link_mbps = mbps;
    r->max_frame_bytes = frame;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        r->window_start_ns[q] = (avb_u32)WINDOW_START_NS;
        r->window_end_ns[q] = (avb_u32)WINDOW_END_NS;
    }
}

/** What GET_TAS_STATE must report for the request: closed, open on queues 0 to 3, closed. Written out, not derived */
static BOOL state_matches(const AVB_TAS_STATE *s, long long base)
{
    unsigned i;
    BOOL ok = s->armed == AVB_TAS_STATE_ARMED && s->config.base_time_s == (uint64_t)(base / NS_PER_SEC) &&
              s->config.base_time_ns == (uint32_t)(base % NS_PER_SEC) && s->config.cycle_time_s == 1 && s->config.cycle_time_ns == 0 &&
              s->config.gate_states[0] == 0x00 && s->config.gate_durations[0] == (uint32_t)WINDOW_START_NS &&
              s->config.gate_states[1] == 0x0F && s->config.gate_durations[1] == (uint32_t)(WINDOW_END_NS - WINDOW_START_NS) &&
              s->config.gate_states[2] == 0x00 && s->config.gate_durations[2] == (uint32_t)(CYCLE_NS - WINDOW_END_NS);
    for (i = 3; i < 8; i++) {
        ok = ok && s->config.gate_states[i] == 0 && s->config.gate_durations[i] == 0;
    }
    return ok;
}

static void print_state(const char *label, const AVB_TAS_STATE *s)
{
    unsigned i;
    printf("    GET_TAS_STATE %s: armed=%u base=%llu.%09u cycle=%u.%09u gcl=", label, s->armed, (unsigned long long)s->config.base_time_s,
           s->config.base_time_ns, s->config.cycle_time_s, s->config.cycle_time_ns);
    for (i = 0; i < 8; i++) {
        printf("%s0x%02X/%u", i ? "," : "", s->config.gate_states[i], s->config.gate_durations[i]);
    }
    printf("\n");
}

/* ---- IOCTL_AVB_TAS_QUERY_HW read-back (#365 12.1-12.5). The same rules as tests/hardware/test_tas_query_clean.c (PRESTATE and
 *      --restore, #368/#377), written again here for this test's pre-state and restoration rather than shared ---- */
typedef struct { LONG status; DWORD got; AVB_TAS_QUERY_HW_REQUEST q; } QUERY_READ;

/** One query on a bound handle: in = version, size, lookup id 0. Keeps the raw NTSTATUS and IoStatus.Information */
static void query(HANDLE h, QUERY_READ *r)
{
    ZeroMemory(r, sizeof(*r));
    r->q.version = AVB_TAS_QUERY_VERSION;
    r->q.size = (avb_u32)sizeof(r->q);
    r->status = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_QUERY_HW, &r->q, sizeof(r->q), &r->q, sizeof(r->q), &r->got);
}

/** A reply whose fields mean something: STATUS_SUCCESS with the full reply */
static BOOL query_valid(const QUERY_READ *r)
{
    return r->status == 0 && r->got == sizeof(r->q) && r->q.version == AVB_TAS_QUERY_VERSION && r->q.size == sizeof(r->q);
}

/** hw_read READ and no register invalid */
static BOOL query_readable(const QUERY_READ *r)
{
    return query_valid(r) && r->q.hw_read == AVB_TAS_HWREAD_READ && r->q.regs.read_failed == 0;
}

static BOOL query_clean(const QUERY_READ *r)
{
    return query_readable(r) && r->q.hw_mode == AVB_TAS_HWMODE_OFF && r->q.current_status == AVB_TAS_ST_NONE &&
           r->q.next_status == AVB_TAS_ST_NONE && r->q.restoration == AVB_TAS_RESTORE_COMPLETE;
}

static void print_query(const char *label, const QUERY_READ *r)
{
    const AVB_TAS_QUERY_HW_REQUEST *q = &r->q;
    const AVB_TAS_REGS *g = &q->regs;
    int i;

    printf("    [EVIDENCE] query %s: ntstatus=0x%08lX information=%lu\n", label, (unsigned long)r->status, r->got);
    if (!query_valid(r)) {
        printf("    query %s: no valid reply, its fields are not reported\n", label);
        return;
    }
    printf("    query %s: hw_read=%u hw_mode=%u current=%u/0x%X next=%u/0x%X snapshot=0x%X restoration=%u flags=0x%X link=%u "
           "epoch=%llu gen=%llu phc=[%llu, %llu]\n", label, q->hw_read, q->hw_mode, q->current_status, q->current_reasons,
           q->next_status, q->next_reasons, q->snapshot_reasons, q->restoration, q->flags, q->link_speed_mbps, q->context_epoch,
           q->generation, q->phc_before_ns, q->phc_after_ns);
    printf("    regs %s: TQAVCTRL=0x%08X BASET_L=0x%08X BASET_H=0x%08X QBVCYCLET=0x%08X QBVCYCLET_S=0x%08X DTXMXPKTSZ=0x%08X "
           "read_failed=0x%X consistency=0x%X\n", label, g->tqavctrl, g->baset_l, g->baset_h, g->qbvcyclet, g->qbvcyclet_s,
           g->dtxmxpktsz, g->read_failed, g->consistency);
    for (i = 0; i < 4; i++) {
        printf("    regs %s: q%d STQT=0x%08X ENDQT=0x%08X TXQCTL=0x%08X\n", label, i, g->stqt[i], g->endqt[i], g->txqctl[i]);
    }
}

/* read_failed bits 0 to 16 of AVB_TAS_REGS (include/avb_ioctl.h): the restoration set of DES-C-TAS-010 12.5. DTXMXPKTSZ is
 * bit 17 and not a restoration input; the PHC bracket is not an input either. The rule of test_tas_query_clean --restore (#377) */
#define TAS_RESTORATION_SET_BITS ((1u << 17) - 1u)

/** The reply observed every restoration-set register: hw_read READ, or PARTIAL because only DTXMXPKTSZ or a PHC read failed */
static BOOL restoration_set_valid(const QUERY_READ *r)
{
    return query_valid(r) && (r->q.hw_read == AVB_TAS_HWREAD_READ || r->q.hw_read == AVB_TAS_HWREAD_PARTIAL) &&
           (r->q.regs.read_failed & TAS_RESTORATION_SET_BITS) == 0;
}

static unsigned reg_differs(const char *name, int queue, avb_u32 before, avb_u32 after)
{
    if (before == after) {
        return 0;
    }
    if (queue < 0) {
        printf("    [EVIDENCE] restoration set: %s before=0x%08X after=0x%08X\n", name, before, after);
    } else {
        printf("    [EVIDENCE] restoration set: %s%d before=0x%08X after=0x%08X\n", name, queue, before, after);
    }
    return 1;
}

/** The restoration set of DES-C-TAS-010 12.5: the v1 register set without DTXMXPKTSZ, BASET_L and BASET_H included. Counts and
 *  prints every register that differs between the two replies */
static unsigned restoration_set_differing(const AVB_TAS_REGS *a, const AVB_TAS_REGS *b)
{
    unsigned n = 0;
    int i;

    n += reg_differs("TQAVCTRL", -1, a->tqavctrl, b->tqavctrl);
    n += reg_differs("BASET_L", -1, a->baset_l, b->baset_l);
    n += reg_differs("BASET_H", -1, a->baset_h, b->baset_h);
    n += reg_differs("QBVCYCLET", -1, a->qbvcyclet, b->qbvcyclet);
    n += reg_differs("QBVCYCLET_S", -1, a->qbvcyclet_s, b->qbvcyclet_s);
    for (i = 0; i < 4; i++) {
        n += reg_differs("STQT", i, a->stqt[i], b->stqt[i]);
        n += reg_differs("ENDQT", i, a->endqt[i], b->endqt[i]);
        n += reg_differs("TXQCTL", i, a->txqctl[i], b->txqctl[i]);
    }
    return n;
}

/** The restoration set register by register (PR #484 r4626979931): every register whose read_failed bit is clear in both valid
 *  replies is compared and each difference printed and counted; every other register is counted and printed as unread. A
 *  register unread in one reply never suppresses the comparison of the others. Without two valid replies all 17 are unread */
static void restoration_set_compare(const QUERY_READ *a, const QUERY_READ *b, unsigned *differing, unsigned *unread)
{
    static const char *const names[17] = { "TQAVCTRL", "BASET_L", "BASET_H", "QBVCYCLET", "QBVCYCLET_S", "STQT0", "STQT1", "STQT2",
                                           "STQT3", "ENDQT0", "ENDQT1", "ENDQT2", "ENDQT3", "TXQCTL0", "TXQCTL1", "TXQCTL2", "TXQCTL3" };
    avb_u32 va[17], vb[17], failed;
    unsigned bit;
    int i;

    *differing = 0;
    *unread = 0;
    if (!query_valid(a) || !query_valid(b)) {
        *unread = 17;
        printf("    [EVIDENCE] restoration set: no valid query reply %s, none of the 17 registers measured\n",
               query_valid(a) ? "after" : "before");
        return;
    }
    va[0] = a->q.regs.tqavctrl; va[1] = a->q.regs.baset_l; va[2] = a->q.regs.baset_h; va[3] = a->q.regs.qbvcyclet;
    va[4] = a->q.regs.qbvcyclet_s;
    vb[0] = b->q.regs.tqavctrl; vb[1] = b->q.regs.baset_l; vb[2] = b->q.regs.baset_h; vb[3] = b->q.regs.qbvcyclet;
    vb[4] = b->q.regs.qbvcyclet_s;
    for (i = 0; i < 4; i++) {
        va[5 + i] = a->q.regs.stqt[i];   vb[5 + i] = b->q.regs.stqt[i];
        va[9 + i] = a->q.regs.endqt[i];  vb[9 + i] = b->q.regs.endqt[i];
        va[13 + i] = a->q.regs.txqctl[i]; vb[13 + i] = b->q.regs.txqctl[i];
    }
    failed = (a->q.regs.read_failed | b->q.regs.read_failed) & TAS_RESTORATION_SET_BITS;
    for (bit = 0; bit < 17; bit++) {
        if (failed & (1u << bit)) {
            (*unread)++;
            printf("    [EVIDENCE] restoration set: %s unread (%s%s%s), not compared\n", names[bit],
                   (a->q.regs.read_failed & (1u << bit)) ? "before" : "", ((a->q.regs.read_failed & b->q.regs.read_failed) & (1u << bit)) ? " and " : "",
                   (b->q.regs.read_failed & (1u << bit)) ? "after" : "");
        } else if (va[bit] != vb[bit]) {
            (*differing)++;
            printf("    [EVIDENCE] restoration set: %s before=0x%08X after=0x%08X\n", names[bit], va[bit], vb[bit]);
        }
    }
}

/**
 * Primary cleanup and restoration: NIC cycle, then everything this test can observe without raw register access. Closes h.
 * Restoration follows #361 (tas_act_restore): schedule registers, TSN mode, driver record and the wire decide it; the PHC is
 * reported separately and never decides it. The restoration set (DES 12.5, BASET_L/H included) and the TSN mode are read back
 * through IOCTL_AVB_TAS_QUERY_HW after the cycle and compared with the test's own pre-state reply (before); without a valid set on
 * both sides they are unmeasured and restoration cannot be COMPLETE.
 */
static TAS_ACT_RESTORE_CLASS cleanup_and_verify(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, IN_ADDR ipv4,
                               const AVB_DRIVER_STATISTICS *stats_before, long long phc_before, LARGE_INTEGER qpc_before,
                               const QUERY_READ *before)
{
    TAS_ACT_RESTORE_EVIDENCE e;
    TAS_ACT_RESTORE_CLASS restore;
    DEVINST inst = 0;
    HANDLE fresh;
    MIB_IF_ROW2 row;
    DWORD start;
    BOOL link_up = FALSE, attach = FALSE;
    long long phc_after = 0, wall_ns = 0;

    ZeroMemory(&e, sizeof(e));
    e.phc = TAS_ACT_PHC_UNMEASURED;
    printf("\n  --- Primary cleanup: NIC cycle of this adapter ---\n");
    /* The cleanup judges a final state only when no operation of this test can still change it. The cycle runs either way: it is the
     * recovery, bounded, and an open operation may hold the disable back */
    e.operations_pending = !owned_ops_settled(&g_ops, id->net_luid, OPS_SETTLE_MS) || m5_pending(id->net_luid);
    if (e.operations_pending) {
        printf("    [EVIDENCE] an operation of this test on this adapter has not been observed to end: whatever is measured below is not final\n");
    }
    m5_report_open(id->net_luid, "before the NIC cycle");
    if (find_devinst(id->net_luid, &inst)) {
        CloseHandle(h);   /* an open bound handle must not hold the adapter context across the detach */
        h = INVALID_HANDLE_VALUE;
        e.cycle_completed = cm_bounded(inst, FALSE, DISABLE_BOUND_MS);
        if (e.cycle_completed) {
            Sleep(3000);
            e.cycle_completed = cm_bounded(inst, TRUE, ENABLE_BOUND_MS);
        } else {
            printf("    enable not attempted: the disable did not complete and may still be pending in Windows\n");
        }
    }
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
    fresh = e.cycle_completed ? wait_for_adapter(id) : INVALID_HANDLE_VALUE;
    e.adapter_returned = (fresh != INVALID_HANDLE_VALUE);
    m5_report_open(id->net_luid, "after the NIC cycle");
    InterlockedExchange(&g_pending_adapter, -1);
    switch (tas_act_cycle(&e)) {
    case TAS_ACT_RESTORE_COMPLETE:
        verdict("PASS", "CLEANUP", index, id->device_id, NULL);
        break;
    case TAS_ACT_RESTORE_UNVERIFIED:
        verdict("INCONCLUSIVE_RESTORATION", "CLEANUP", index, id->device_id,
                "the NIC cycle ran and the adapter returned, but an operation of this test had not been observed to end before it and may still "
                "change the state. Operator: NIC cycle once it ends, then TC-TSNREG-001");
        break;
    default:
        verdict("CLEANUP_FAILED", "CLEANUP", index, id->device_id,
                "the NIC cycle did not complete or the adapter did not return; a timed-out disable may still be "
                "pending in Windows. Operator: NIC cycle once it ends, then TC-TSNREG-001");
        break;
    }

    if (e.adapter_returned) {
        AVB_DRIVER_STATISTICS stats_after;
        AVB_TAS_STATE s;

        start = GetTickCount();
        while (GetTickCount() - start < RETURN_BOUND_MS) {
            if (read_if(id->net_luid, &row) && row.OperStatus == IfOperStatusUp) {
                link_up = TRUE;
                break;
            }
            Sleep(1000);
        }
        Sleep(SETTLE_MS);
        if (read_stats(fresh, &stats_after)) {   /* the statistics live in the adapter context: lower means a new one */
            attach = stats_after.IoctlCount < stats_before->IoctlCount;
            printf("    IoctlCount %llu -> %llu\n", stats_before->IoctlCount, stats_after.IoctlCount);
        }
        if (read_state(fresh, &s)) {
            e.driver_clean = (s.armed == AVB_TAS_STATE_CLEAN);
            print_state("after the cycle", &s);
        } else {
            printf("    GET_TAS_STATE after the cycle: unreadable\n");
        }
        {   /* Read back through the TAS query and compared with the test's own pre-state reply, register by register; never
             * assumed, and never taken from the driver's advisory restoration field */
            QUERY_READ after;
            query(fresh, &after);
            print_query("after the cycle", &after);
            e.regs_measured = restoration_set_valid(&after) && restoration_set_valid(before);
            if (e.regs_measured) {
                e.regs_differing = restoration_set_differing(&before->q.regs, &after.q.regs);
                e.tsn_mode_off = after.q.hw_mode == AVB_TAS_HWMODE_OFF;
                printf("    [EVIDENCE] restoration set (DES 12.5, BASET_L/H included): %u of 17 registers differ from the pre-state query\n",
                       e.regs_differing);
            } else {
                printf("    [EVIDENCE] restoration set after the cycle: UNVERIFIED, no valid restoration set in the query before and after the cycle\n");
            }
            if (query_valid(&after) && query_valid(before) && !(after.q.regs.read_failed & ~TAS_RESTORATION_SET_BITS) &&
                after.q.regs.dtxmxpktsz != before->q.regs.dtxmxpktsz) {
                printf("    [EVIDENCE] DTXMXPKTSZ before=0x%08X after=0x%08X: outside the restoration set (DES 12.5), reported only\n",
                       before->q.regs.dtxmxpktsz, after.q.regs.dtxmxpktsz);
            }
        }
        if (read_phc(fresh, &phc_after)) {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            wall_ns = (long long)((double)(now.QuadPart - qpc_before.QuadPart) * 1e9 / (double)freq.QuadPart);
            e.phc = tas_act_phc(TRUE, phc_after - phc_before, wall_ns, PHC_TOLERANCE_NS);
        }
        if (!link_up && !g_cleanup_link_was_up) {   /* no link before the test and none after: the wire is not measured */
            e.tx_probed = FALSE;
            printf("    TX after the cycle: not measured, the link was down before the test as well\n");
        } else if (!link_up) {
            e.tx_probed = TRUE;   /* the link was up before the test: no link now is an observed failure, not a gap */
            e.tx_on_wire = FALSE;
            printf("    TX after the cycle: link did not come back within %d ms\n", RETURN_BOUND_MS);
        } else {
            SOCKET sock = open_tx_socket(ipv4);
            unsigned nonce = GetTickCount() ^ (GetCurrentProcessId() << 8), sent = 0, failed = 0, delayed = 0, seen;
            LONG captured = 0, from_target = 0;
            BOOL attempted = sock != INVALID_SOCKET && g_capture_luid != 0;   /* no capture without an established observer */
            int observers = attempted ? wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4) : 0;

            if (observers != 0) {
                seen = send_window(sock, nonce, BASELINE_MS, &sent, &failed, &delayed);
                wire_counts(&captured, &from_target);
                wire_stop();
                printf("    TX after the cycle: sent=%u sendFailed=%u seen=%u delayedOver%lldms=%u captured=%ld\n", sent, failed, seen,
                       UNGATED_DELAY_US / 1000, delayed, captured);
                if (sent != 0) {
                    /* The ratio means something only for frames this test sent; unrelated captured traffic never decides it */
                    e.tx_probed = captured != 0;
                    e.tx_on_wire = (unsigned long long)seen * 100 >= (unsigned long long)sent * TAS_ACT_SEEN_MIN_PERCENT && delayed == 0;
                } else if (failed != 0) {
                    e.tx_probed = TRUE;   /* every attempt failed in sendto: an observed failure to transmit, not a gap */
                    e.tx_on_wire = FALSE;
                    printf("    TX after the cycle: every one of %u sends failed (last WSA error %d): observed transmission failure\n",
                           failed, g_last_send_error);
                } else {
                    e.tx_probed = FALSE;  /* nothing was attempted (no capture slot left): the wire was not measured */
                    e.tx_on_wire = FALSE;
                    printf("    TX after the cycle: not measured, no tagged frame was attempted (capture capacity)\n");
                }
            } else {
                if (attempted) {
                    wire_stop();   /* zero observers: wire_start may still have started its ICMP worker */
                }
                printf("    TX after the cycle: not probed, %s\n", sock == INVALID_SOCKET ? "no socket" : "no established observer");
            }
            if (sock != INVALID_SOCKET) { closesocket(sock); }
        }
        CloseHandle(fresh);
    }
    printf("    evidence: cycle=%d returned=%d attach=%d driverClean=%d regsMeasured=%d regsDiffering=%u tsnOff=%d link=%d txProbed=%d "
           "txOnWire=%d opsPending=%d\n", e.cycle_completed, e.adapter_returned, attach, e.driver_clean, e.regs_measured, e.regs_differing,
           e.tsn_mode_off, link_up, e.tx_probed, e.tx_on_wire, e.operations_pending);
    restore = tas_act_restore(&e);
    /* A separate outcome (#361): the cycle reseeds the PHC, which is reported, not judged as restoration */
    printf("    [EVIDENCE] PHC across the cleanup: %s (before=%lld after=%lld wallNs=%lld)\n", tas_act_phc_name(e.phc), phc_before,
           phc_after, wall_ns);
    switch (restore) {
    case TAS_ACT_RESTORE_COMPLETE:
        verdict("PASS", "RESTORE", index, id->device_id,
                "restoration set (DES 12.5, BASET_L/H included) as in the pre-state query, TSN mode off, record clean, TX on the wire (#361)");
        break;
    case TAS_ACT_RESTORE_UNVERIFIED:
        verdict("INCONCLUSIVE_RESTORATION", "RESTORE", index, id->device_id,
                e.operations_pending ? "nothing observed failed, but an operation of this test had not been observed to end before the cycle: the "
                "measured state is not final" :
                "nothing observed failed, but the restoration set (no valid query before and after) or the wire was not measured; see the evidence line");
        break;
    default:
        verdict("CLEANUP_FAILED", "RESTORE", index, id->device_id,
                "restoration incomplete: see the evidence line (cycle, return, driver record or TX on the wire failed)");
        break;
    }
    return restore;
}

/** Frames sent after the activation returned, with PHC times on the line through the two anchors */
static unsigned collect_frames(unsigned first, unsigned end, const TAS_ACT_ANCHOR *a, const TAS_ACT_ANCHOR *b, TAS_ACT_FRAME *out)
{
    unsigned seq, n = 0;

    for (seq = first; seq < end && seq < WIRE_MAX_FRAMES; seq++) {
        TAS_ACT_FRAME *f = &out[n];
        if (!tas_act_phc_at(a, b, g_send_us[seq], &f->sent_ns)) {
            return 0;
        }
        f->seen = g_wire_seen[seq] != 0 && tas_act_phc_at(a, b, g_wire_rx_us[seq], &f->arrival_ns);
        n++;
    }
    return n;
}

/** OBSERVER (#369 amendment, #463): every other enumerated adapter is tried, without a count limit, as the passive capture until
 *  one receives the baseline frames the adapter under test sends. TRUE with that capture still running (g_capture_luid, *nonce) */
static BOOL find_observer(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, SOCKET sock, IN_ADDR ipv4, unsigned *nonce,
                          unsigned *sent, unsigned *failed, unsigned *seen)
{
    UINT32 o, tries = 0;
    unsigned delayed = 0;
    char reason[220];

    g_capture_luid = 0;
    for (o = 0; o < g_nall; o++) {
        int observers;
        if (g_all[o].net_luid == id->net_luid) {
            continue;
        }
        tries++;
        /* No capture is active here (none before the first candidate, wire_stop after each one), so every tag slot is free: each
         * candidate's baseline starts at slot 0 and the number of candidates never exhausts WIRE_MAX_FRAMES (PR #481) */
        g_next_seq = 0;
        *nonce = GetTickCount() ^ GetCurrentProcessId() ^ (o << 24);
        observers = wire_start(id->net_luid, g_all[o].net_luid, id->mac, *nonce, ipv4);
        if (observers != 0) {
            *sent = 0;
            *failed = 0;
            *seen = send_window(sock, *nonce, BASELINE_MS, sent, failed, &delayed);
            printf("    [EVIDENCE] OBSERVER candidate adapter:%u did=0x%04X: capture started, baseline sent=%u sendFailed=%u seen=%u\n",
                   g_all[o].index, g_all[o].device_id, *sent, *failed, *seen);
            if (*sent != 0 && (unsigned long long)*seen * 100 >= (unsigned long long)*sent * TAS_ACT_SEEN_MIN_PERCENT) {
                g_capture_luid = g_all[o].net_luid;
                sprintf_s(reason, sizeof(reason), "adapter:%u did=0x%04X received %u of %u baseline frames the adapter under test sent",
                          g_all[o].index, g_all[o].device_id, *seen, *sent);
                verdict("PASS", "OBSERVER", index, id->device_id, reason);
                return TRUE;
            }
        } else {
            printf("    [EVIDENCE] OBSERVER candidate adapter:%u did=0x%04X: capture not started\n", g_all[o].index, g_all[o].device_id);
        }
        wire_stop();   /* also after 0 observers: the ICMP worker may run */
    }
    sprintf_s(reason, sizeof(reason), "no other enumerated adapter received the baseline frames (%u candidate(s) tried, each listed above): "
              "WIRE and the cleanup's TX probe are not measured; the other checks still run", tries);
    verdict("BLOCKED", "OBSERVER", index, id->device_id, reason);
    return FALSE;
}

/* ---- --hold on the versioned request (#369 amendment, #365 13.1-13.4) ---- */
static avb_u64 next_op_id(void)
{
    if (g_op_id == 0) {
        g_op_id = ((avb_u64)GetCurrentProcessId() << 32) | (avb_u64)GetTickCount();
    }
    if (++g_op_id == 0) {
        g_op_id = 1;
    }
    return g_op_id;
}

/** One HOLD AVB_TAS_OP_REQUEST: the verified schedule set, the bound adapter, the metadata as the test's own query reports it */
static void build_op(AVB_TAS_OP_REQUEST *r, avb_u64 luid, const AVB_TAS_QUERY_HW_REQUEST *q, long long base, avb_u32 frame)
{
    unsigned i;

    ZeroMemory(r, sizeof(*r));
    r->version = AVB_TAS_OP_VERSION;
    r->size = (avb_u32)sizeof(*r);
    r->net_luid = luid;
    r->client_op_id = next_op_id();
    r->expected_generation = q->generation;
    r->context_epoch = q->context_epoch;
    r->pre_activation = AVB_TAS_PRE_HOLD;
    r->schedule.base_time_ns = (avb_u64)base;
    r->schedule.cycle_ns = (avb_u32)CYCLE_NS;
    for (i = 0; i < AVB_TAS_QUEUES; i++) {
        r->schedule.queue[i].start_ns = (avb_u32)WINDOW_START_NS;
        r->schedule.queue[i].end_ns = (avb_u32)WINDOW_END_NS;
        r->schedule.queue[i].max_frame_bytes = frame;
    }
}

typedef struct {
    HANDLE h, go;
    AVB_TAS_OP_REQUEST sent, r;
    LONG status;
    DWORD got;
} OP_CALL;

static DWORD WINAPI op_thread(LPVOID arg)
{
    OP_CALL *c = (OP_CALL *)arg;
    (void)WaitForSingleObject(c->go, INFINITE);
    c->status = avb_ioctl_ntstatus(c->h, IOCTL_AVB_TAS_ACTIVATE_80C, &c->r, sizeof(c->r), &c->r, sizeof(c->r), &c->got);
    return 0;
}

static void print_op(const char *label, const OP_CALL *c)
{
    char status_text[96];

    tas_fv_status_text(c->status != AVB_NTSTATUS_UNAVAILABLE, c->status, status_text, sizeof(status_text));
    printf("    %s: op=0x%016llX %s information=%lu%s out_flags=0x%X reserved1=0x%X\n", label, c->sent.client_op_id,
           status_text, c->got, c->status == 0 && c->got == sizeof(c->r) &&
           memcmp(&c->r, &c->sent, offsetof(AVB_TAS_OP_REQUEST, out_flags)) != 0 ? " (inputs NOT echoed)" : "", c->r.out_flags,
           c->r.reserved1);
}

/** ACCESS on the versioned code: a read-only handle (FILE_WRITE_ACCESS, #362, #365 13.3). The request is invalid (version 0), so
 *  nothing is written even if the access check were missing */
static void check_access_80c(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    AVB_TAS_OP_REQUEST r;
    AVB_TAS_STATE s;
    DWORD got = 0;
    LONG st;
    BOOL state_ok;
    char reason[200];
    HANDLE h = open_bound(GENERIC_READ, id->net_luid);

    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "ACCESS", index, id->device_id, "cannot open and bind a read-only handle");
        return;
    }
    ZeroMemory(&r, sizeof(r));
    st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &r, sizeof(r), &r, sizeof(r), &got);
    state_ok = read_state(h, &s);
    CloseHandle(h);
    printf("    read-only handle: IOCTL_AVB_TAS_ACTIVATE_80C ntstatus=0x%08lX information=%lu; GET_TAS_STATE ok=%d\n", (unsigned long)st,
           got, state_ok);
    if (st == (LONG)STATUS_ACCESS_DENIED && got == 0 && state_ok) {
        verdict("PASS", "ACCESS", index, id->device_id, "STATUS_ACCESS_DENIED with no output; GET_TAS_STATE still served");
    } else {
        sprintf_s(reason, sizeof(reason), "expected STATUS_ACCESS_DENIED with no output and GET_TAS_STATE served; ntstatus=0x%08lX "
                  "information=%lu, GET_TAS_STATE %s", (unsigned long)st, got, state_ok ? "served" : "not served");
        verdict("FAIL", "ACCESS", index, id->device_id, reason);
    }
}

/** A query carrying a lookup id (#365 12.1): LOOKUP evidence */
static void query_lookup(HANDLE h, avb_u64 op, QUERY_READ *r)
{
    ZeroMemory(r, sizeof(*r));
    r->q.version = AVB_TAS_QUERY_VERSION;
    r->q.size = (avb_u32)sizeof(r->q);
    r->q.lookup_client_op_id = op;
    r->status = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_QUERY_HW, &r->q, sizeof(r->q), &r->q, sizeof(r->q), &r->got);
}

/** Pre-state proved by the test's own reads (#369): GET_TAS_STATE CLEAN and a clean, readable TAS query. Prints both.
 *  *armed: the record's armed value, or -1 when GET_TAS_STATE could not be read */
static BOOL prove_prestate(HANDLE h, const char *label, QUERY_READ *q, LONG *armed)
{
    AVB_TAS_STATE s;
    BOOL record = read_state(h, &s);

    *armed = record ? (LONG)s.armed : -1;
    if (record) {
        print_state(label, &s);
    } else {
        printf("    GET_TAS_STATE %s: unreadable\n", label);
    }
    query(h, q);
    print_query(label, q);
    return record && s.armed == AVB_TAS_STATE_CLEAN && query_clean(q);
}

/**
 * Test-owned setup recovery (#369 pre-state, #365 13.7 "tests own setup"): a pre-state not proved clean is recovered once through
 * this test's own NIC cycle, the recovery its cleanup uses, then the link is waited for and the pre-state proved again. Closes h.
 * Returns a bound handle with a proved pre-state, or INVALID_HANDLE_VALUE with the reason in why. Nothing else is attempted.
 */
static HANDLE recover_prestate(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst, QUERY_READ *q,
                               const char **why)
{
    MIB_IF_ROW2 row;
    DWORD start;
    BOOL cycled;
    LONG armed;

    printf("\n  --- Setup: pre-state not proved clean, recovered through this test's own NIC cycle ---\n");
    /* From here the test owns a state change: an abort reports it (report_abort) until it is verified or reported below */
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_setup, 1);
    InterlockedExchange(&g_pending_adapter, (LONG)index);
    CloseHandle(h);   /* an open bound handle must not hold the adapter context across the detach */
    cycled = cm_bounded(inst, FALSE, DISABLE_BOUND_MS);
    if (cycled) {
        Sleep(3000);
        cycled = cm_bounded(inst, TRUE, ENABLE_BOUND_MS);
    } else {
        printf("    enable not attempted: the disable did not complete and may still be pending in Windows\n");
    }
    h = cycled ? wait_for_adapter(id) : INVALID_HANDLE_VALUE;
    if (h == INVALID_HANDLE_VALUE) {
        verdict("CLEANUP_FAILED", "SETUP", index, id->device_id,
                "the setup recovery's NIC cycle did not complete or the adapter did not return; a timed-out disable may still be "
                "pending in Windows. Operator: NIC cycle once it ends, then TC-TSNREG-001");
        /* Tracking ends only once the failure is recorded: an abort before this line still reports the setup phase */
        InterlockedExchange(&g_pending_adapter, -1);
        InterlockedExchange(&g_pending_setup, 0);
        *why = "the setup recovery's NIC cycle did not complete or the adapter did not return";
        return INVALID_HANDLE_VALUE;
    }
    start = GetTickCount();
    while (GetTickCount() - start < RETURN_BOUND_MS) {
        if (read_if(id->net_luid, &row) && row.OperStatus == IfOperStatusUp) {
            break;
        }
        Sleep(1000);
    }
    Sleep(SETTLE_MS);
    if (!prove_prestate(h, "after the setup recovery", q, &armed)) {
        /* The test cycled the NIC and did not establish a clean state: observed residue is a cleanup failure of its setup,
         * an unmeasurable state is inconclusive. Either way the activation stays BLOCKED (the caller) */
        BOOL residue = (armed >= 0 && armed != AVB_TAS_STATE_CLEAN) || (query_readable(q) && !query_clean(q));
        if (residue) {
            verdict("CLEANUP_FAILED", "SETUP", index, id->device_id,
                    "after the setup recovery NIC cycle the driver record is not CLEAN or the readable TAS query is not clean (TSN mode, "
                    "slots or restoration); see the observations above. Operator: NIC cycle, then TC-TSNREG-001");
        } else {
            verdict("INCONCLUSIVE_RESTORATION", "SETUP", index, id->device_id,
                    "after the setup recovery NIC cycle the state could not be measured (GET_TAS_STATE unreadable or the TAS query not "
                    "readable); see the observations above");
        }
        InterlockedExchange(&g_pending_adapter, -1);   /* only after the outcome is recorded */
        InterlockedExchange(&g_pending_setup, 0);
        *why = "the pre-state is still not proved clean after the test-owned setup recovery (GET_TAS_STATE CLEAN and a clean, "
               "readable TAS query); see both observations above";
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    printf("    setup recovery: pre-state proved clean after the NIC cycle\n");
    InterlockedExchange(&g_pending_adapter, -1);   /* only after the final state is verified */
    InterlockedExchange(&g_pending_setup, 0);
    return h;
}

/** --metadata-events: one generation measurement across an event (#352 R1). PASS when both replies are valid, report a
 *  non-zero pair in the same context and the generation moved by exactly `expected`; FAIL on any other measured move (a
 *  decrease included); INCONCLUSIVE when a reply is not valid, reports 0 (not reported, never a value) or names another
 *  context (epoch changed). Every value is printed */
static void meta_line(const char *check, UINT32 index, avb_u16 did, const QUERY_READ *a, const QUERY_READ *b, avb_u64 expected,
                      const char *what)
{
    char reason[420];

    if (!query_valid(a) || !query_valid(b) || a->q.generation == 0 || a->q.context_epoch == 0 || b->q.generation == 0 ||
        b->q.context_epoch == 0) {
        sprintf_s(reason, sizeof(reason), "%s: not measured: before valid %d gen %llu epoch %llu, after valid %d gen %llu epoch %llu "
                  "(0 = not reported, never a value)", what, query_valid(a), a->q.generation, a->q.context_epoch, query_valid(b),
                  b->q.generation, b->q.context_epoch);
        verdict("INCONCLUSIVE", check, index, did, reason);
    } else if (b->q.context_epoch != a->q.context_epoch) {
        sprintf_s(reason, sizeof(reason), "%s: the epoch changed (%llu -> %llu): another context, the generations are not comparable",
                  what, a->q.context_epoch, b->q.context_epoch);
        verdict("INCONCLUSIVE", check, index, did, reason);
    } else if (b->q.generation >= a->q.generation && b->q.generation - a->q.generation == expected) {
        sprintf_s(reason, sizeof(reason), "%s: generation %llu -> %llu in context %llu (+%llu as required)", what, a->q.generation,
                  b->q.generation, a->q.context_epoch, expected);
        verdict("PASS", check, index, did, reason);
    } else {
        sprintf_s(reason, sizeof(reason), "%s: generation %llu -> %llu in context %llu, required +%llu", what, a->q.generation,
                  b->q.generation, a->q.context_epoch, expected);
        verdict("FAIL", check, index, did, reason);
    }
}

/* ---- --hold STATUS_* (#369 for #350): the schedule status, the retained commit and the read-only property of the query ---- */
#define STATUS_POLL_MS 100u
/* K8 for #365 12.4: FALSE until #472 EVENT is accepted as verified evidence. A published fact, never derived from a run of this test */
#define TAS_STATUS_K8_VERIFIED FALSE

static void print_status(const char *label, const QUERY_READ *r)
{
    char text[64];

    sprintf_s(text, sizeof(text), "status %s", label);
    print_query(text, r);
    if (query_valid(r)) {
        printf("    status %s: current_commit state=%u bracket=[%llu, %llu] record base=%llu cycle=%u q0=[%u, %u] frame=%u; next_commit state=%u\n",
               label, r->q.current_commit.state, r->q.current_commit.phc_before_commit_ns, r->q.current_commit.phc_after_commit_ns,
               r->q.current.base_time_ns, r->q.current.cycle_ns, r->q.current.queue[0].start_ns, r->q.current.queue[0].end_ns,
               r->q.current.queue[0].max_frame_bytes, r->q.next_commit.state);
    }
}

static void status_verdict(const char *check, TAS_STATUS_CLASS c, UINT32 index, avb_u16 did, const char *expected, const char *observed)
{
    char reason[900];

    sprintf_s(reason, sizeof(reason), "%s; expected %s; observed %s", tas_status_class_name(c), expected, observed);
    verdict(tas_status_tag(c), check, index, did, reason);
}

static long long wall_now_ns(void)
{
    LARGE_INTEGER now, freq;

    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (long long)((double)now.QuadPart * 1e9 / (double)freq.QuadPart);
}

/** --metadata-events STATUS_*: the schedule status after one PHC reseed under the schedule that op committed. event: the caller observed the
 *  reseed's completion and its effect on the PHC */
static void meta_reseed_status(const char *check, UINT32 index, avb_u16 did, const QUERY_READ *before, const QUERY_READ *after,
                               const AVB_TAS_OP_REQUEST *op, BOOL event, const char *what)
{
    const AVB_TAS_QUERY_HW_REQUEST *b = &before->q, *a = &after->q;
    char seen[400];

    printf("    [EVIDENCE] %s: %s: event %s; this commit is epoch %llu generation %llu (request + 1) BaseT %llu\n", check, what,
           event ? "observed" : "NOT observed", op->context_epoch, op->expected_generation + 1, op->schedule.base_time_ns);
    printf("    [EVIDENCE] %s: before: valid %d epoch %llu generation %llu commit state=%u bracket=[%llu, %llu] base=%llu cycle=%u\n", check,
           query_valid(before), b->context_epoch, b->generation, b->current_commit.state, b->current_commit.phc_before_commit_ns,
           b->current_commit.phc_after_commit_ns, b->current.base_time_ns, b->current.cycle_ns);
    printf("    [EVIDENCE] %s: after: valid %d epoch %llu generation %llu commit state=%u bracket=[%llu, %llu] base=%llu cycle=%u next_commit=%u\n",
           check, query_valid(after), a->context_epoch, a->generation, a->current_commit.state, a->current_commit.phc_before_commit_ns,
           a->current_commit.phc_after_commit_ns, a->current.base_time_ns, a->current.cycle_ns, a->next_commit.state);
    sprintf_s(seen, sizeof(seen), "current %u/0x%X next %u/0x%X commit %u snapshot 0x%X generation %llu PHC [%llu, %llu]; before the event current "
              "%u/0x%X next %u/0x%X commit %u generation %llu", a->current_status, a->current_reasons, a->next_status, a->next_reasons,
              a->current_commit.state, a->snapshot_reasons, a->generation, a->phc_before_ns, a->phc_after_ns, b->current_status,
              b->current_reasons, b->next_status, b->next_reasons, b->current_commit.state, b->generation);
    status_verdict(check, tas_status_reseed(query_valid(before), b, query_valid(after), a, op, event), index, did,
                   "INCONSISTENT (6) with PHC_RESET_SINCE_COMMIT (0x80), commit PHC_DISCONTINUOUS (3) with the committed schedule, next NONE (1)", seen);
}

/** Every line is its own measurement. op: the committed request, whose pair identifies the commit. race / commit / pending: the queries
 *  immediately before the race, right after it and after the refused requests. stats0 / row0: the adapter's statistics and link before the activation */
static void check_status(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, BOOL committed, const AVB_TAS_OP_REQUEST *op,
                         const QUERY_READ *race, const QUERY_READ *commit, const QUERY_READ *pending,
                         const AVB_DRIVER_STATISTICS *stats0, const MIB_IF_ROW2 *row0)
{
    const AVB_TAS_SCHEDULE *sent = &op->schedule;
    static const char *const checks[] = { "STATUS_RECORD", "STATUS_PENDING", "STATUS_ACTIVE", "STATUS_NEXT", "STATUS_READONLY" };
    AVB_DRIVER_STATISTICS sa, sb, sc;
    QUERY_READ past, again;
    TAS_STATUS_CONTROL cp, ca;
    TAS_STATUS_CLASS first, second;
    MIB_IF_ROW2 row1;
    BOOL read_a, read_b, read_c, row_ok, link_same, none_counted, own_only, outer;
    avb_u64 lo = 0, hi = 0;
    DWORD start;
    unsigned k;
    char seen[500];

    if (!committed) {
        for (k = 0; k < sizeof(checks) / sizeof(checks[0]); k++) {
            verdict("BLOCKED", checks[k], index, id->device_id, "no committed schedule (see COMMIT)");
        }
        return;
    }
    printf("\n  --- Schedule status past BaseT ---\n");
    start = GetTickCount();
    for (;;) {   /* the time bound only ends the wait: whether BaseT has passed is measured in the query's own PHC bracket */
        query(h, &past);
        if ((tas_status_observable(query_valid(&past), &past.q) && past.q.phc_before_ns >= sent->base_time_ns) ||
            GetTickCount() - start >= SEND_BOUND_MS) {
            break;
        }
        Sleep(STATUS_POLL_MS);
    }
    read_a = read_stats(h, &sa);
    read_b = read_stats(h, &sb);
    query(h, &past);
    query(h, &again);
    read_c = read_stats(h, &sc);
    row_ok = read_if(id->net_luid, &row1);
    print_status("past BaseT", &past);
    print_status("past BaseT, again", &again);

    link_same = row_ok && row0->MediaConnectState == MediaConnectStateConnected && row1.MediaConnectState == MediaConnectStateConnected &&
                row0->TransmitLinkSpeed == row1.TransmitLinkSpeed && query_valid(race) && query_valid(&past) &&
                race->q.link_speed_mbps != 0 && race->q.link_speed_mbps == past.q.link_speed_mbps;
    none_counted = tas_status_no_event_counted(TRUE, stats0, read_c, &sc, link_same);
    own_only = read_a && read_b && read_c && sb.IoctlCount == sa.IoctlCount + 1 && sc.IoctlCount == sb.IoctlCount + 3 &&
               sb.FilterPauseCount == sc.FilterPauseCount && sb.FilterRestartCount == sc.FilterRestartCount;
    printf("    [EVIDENCE] events since before the activation: statistics %s, status+%lld pnp+%lld pause+%lld restart+%lld attach+%lld "
           "detach+%lld; OS link %s state %d -> %d, %llu -> %llu bit/s; driver link speed %u -> %u Mbit/s: %s (this observation only: no "
           "proof of complete event capture and none of #472 EVENT)\n",
           read_c ? "read" : "NOT read", read_c ? (long long)(sc.FilterStatusCount - stats0->FilterStatusCount) : 0LL,
           read_c ? (long long)(sc.FilterNetPnPCount - stats0->FilterNetPnPCount) : 0LL,
           read_c ? (long long)(sc.FilterPauseCount - stats0->FilterPauseCount) : 0LL,
           read_c ? (long long)(sc.FilterRestartCount - stats0->FilterRestartCount) : 0LL,
           read_c ? (long long)(sc.FilterAttachCount - stats0->FilterAttachCount) : 0LL,
           read_c ? (long long)(sc.FilterDetachCount - stats0->FilterDetachCount) : 0LL, row_ok ? "read" : "NOT read",
           (int)row0->MediaConnectState, row_ok ? (int)row1.MediaConnectState : -1, row0->TransmitLinkSpeed, row_ok ? row1.TransmitLinkSpeed : 0ULL,
           query_valid(race) ? race->q.link_speed_mbps : 0u, query_valid(&past) ? past.q.link_speed_mbps : 0u,
           none_counted ? "no event was counted" : "an event was counted, or the counters or the link were not measured");
    tas_status_control(query_valid(commit), &commit->q, query_valid(pending), &pending->q, op, none_counted,
                       TAS_STATUS_K8_VERIFIED, &cp);
    tas_status_control(query_valid(commit), &commit->q, query_valid(&past), &past.q, op, none_counted,
                       TAS_STATUS_K8_VERIFIED, &ca);
    printf("    [EVIDENCE] control of this commit: before BaseT %s contradiction=0x%X, past BaseT %s contradiction=0x%X "
           "(0x1 mode off, 0x2 windows or cycle differ from the committed schedule, 0x4 PHC below, 0x8 context, 0x10 generation); this commit is epoch %llu "
           "generation %llu (request + 1), identified before BaseT %d, past it %d\n", cp.measured ? "measured" : "NOT measured",
           cp.contradiction, ca.measured ? "measured" : "NOT measured", ca.contradiction, op->context_epoch, op->expected_generation + 1,
           cp.identified, ca.identified);
    printf("    [EVIDENCE] registers against the committed schedule: before BaseT %s mismatch=0x%X, past BaseT %s mismatch=0x%X (slot reason bits)\n",
           cp.compared ? "compared" : "NOT compared", cp.mismatch, ca.compared ? "compared" : "NOT compared", ca.mismatch);

    outer = tas_status_observable(query_valid(race), &race->q) && tas_status_observable(query_valid(commit), &commit->q);
    if (outer) {
        lo = race->q.phc_after_ns;
        hi = commit->q.phc_before_ns;
    }
    first = tas_status_record(query_valid(pending), &pending->q, sent, outer, lo, hi, &cp);
    second = tas_status_record(query_valid(&past), &past.q, sent, outer, lo, hi, &ca);
    sprintf_s(seen, sizeof(seen), "before BaseT state %u bracket [%llu, %llu] base %llu (%s), past BaseT state %u bracket [%llu, %llu] base %llu "
              "(%s); test readings %s [%llu, %llu]", pending->q.current_commit.state, pending->q.current_commit.phc_before_commit_ns,
              pending->q.current_commit.phc_after_commit_ns, pending->q.current.base_time_ns, tas_status_is_fail(first) ? "FAIL" :
              (first == TAS_STATUS_PASS ? "PASS" : "not judged"), past.q.current_commit.state, past.q.current_commit.phc_before_commit_ns,
              past.q.current_commit.phc_after_commit_ns, past.q.current.base_time_ns, tas_status_is_fail(second) ? "FAIL" :
              (second == TAS_STATUS_PASS ? "PASS" : "not judged"), outer ? "measured" : "NOT measured", lo, hi);
    status_verdict("STATUS_RECORD", tas_status_worst(first, second), index, id->device_id,
                   "RETAINED (2) with the requested schedule and a bracket before BaseT inside the test's readings, in both queries", seen);

    first = tas_status_slot(AVB_TAS_ST_PENDING, query_valid(pending), &pending->q, &cp);
    sprintf_s(seen, sizeof(seen), "current %u/0x%X snapshot 0x%X, PHC [%llu, %llu], BaseT %llu", pending->q.current_status,
              pending->q.current_reasons, pending->q.snapshot_reasons, pending->q.phc_before_ns, pending->q.phc_after_ns, sent->base_time_ns);
    status_verdict("STATUS_PENDING", first, index, id->device_id, "PENDING (3) without reasons before BaseT", seen);

    second = tas_status_slot(AVB_TAS_ST_INFERRED_ACTIVE, query_valid(&past), &past.q, &ca);
    sprintf_s(seen, sizeof(seen), "current %u/0x%X snapshot 0x%X, PHC [%llu, %llu], BaseT %llu", past.q.current_status,
              past.q.current_reasons, past.q.snapshot_reasons, past.q.phc_before_ns, past.q.phc_after_ns, sent->base_time_ns);
    status_verdict("STATUS_ACTIVE", second, index, id->device_id,
                   "INFERRED_ACTIVE (4) without reasons past BaseT for the full #369 requirement (an inference, never HARDWARE_VERIFIED); while K8 is "
                   "not verified #365 12.4 keeps it UNKNOWN (7) with LINK_EVENTS_UNOBSERVED (0x800), nonpassing here", seen);

    first = tas_status_next(query_valid(pending), &pending->q, &cp);
    second = tas_status_next(query_valid(&past), &past.q, &ca);
    sprintf_s(seen, sizeof(seen), "before BaseT next %u/0x%X commit %u, past BaseT next %u/0x%X commit %u", pending->q.next_status,
              pending->q.next_reasons, pending->q.next_commit.state, past.q.next_status, past.q.next_reasons, past.q.next_commit.state);
    status_verdict("STATUS_NEXT", tas_status_worst(first, second), index, id->device_id,
                   "next NONE (1) without reasons, no CYCLE_MISMATCH, next_commit NONE (1), in both queries", seen);

    first = tas_status_readonly(query_valid(&past) && query_valid(&again), &past.q, &again.q, own_only);
    second = tas_status_readonly(query_valid(pending) && query_valid(&past), &pending->q, &past.q, FALSE);
    sprintf_s(seen, sizeof(seen), "generation %llu before BaseT, %llu and %llu in two successive queries past it, epoch %llu, %llu, %llu; between "
              "the two successive queries %s", pending->q.generation, past.q.generation, again.q.generation, pending->q.context_epoch,
              past.q.context_epoch, again.q.context_epoch, own_only ? "only this test's own IOCTLs were counted" :
              "other IOCTLs are not excluded");
    status_verdict("STATUS_READONLY", tas_status_worst(first, second), index, id->device_id, "generation and epoch unchanged by queries", seen);
}

static void run_meta_race(HANDLE h, HANDLE ha, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u32 frame, BOOL back);
static avb_u64 run_meta_restart(HANDLE h, HANDLE h2, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u32 frame);
static void meta_new_context(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u64 old_epoch);
static void run_meta_mismatch(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u32 frame);
#ifndef NDEBUG
static void run_meta_link(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u32 frame);
#endif

static void run_adapter(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    static TAS_ACT_FRAME frames[WIRE_MAX_FRAMES];
    AVB_DRIVER_STATISTICS stats;
    AVB_TAS_STATE s;
    AVB_TAS_ACTIVATE_REQUEST req;
    AVB_TAS_OP_REQUEST req_op;
    BOOL committed = FALSE;
    TAS_ACT_ANCHOR before_anchor, after_anchor;
    QUERY_READ qbefore;
    TAS_ACT_WIRE_COUNTS wc = {0};
    TAS_ACT_WIRE_CLASS wclass = TAS_ACT_WIRE_UNOBSERVED;
    TAS_ACT_RESTORE_CLASS restore;
    LONG armed_before;
    unsigned raw_sent = 0, raw_seen = 0;   /* after the activation: frames sendto accepted, and of those the unique ones the
                                              capture saw; read before cleanup reuses the capture */
    unsigned raw_send_failed = 0;          /* after the activation: sendto failures (observed, not counted as sent) */
    BOOL mapped = FALSE;                   /* every such frame got a PHC time: the timing counts mean something */
    BOOL anchor_ok = FALSE;                /* the PHC anchor after the frames drained was read */
    MIB_IF_ROW2 row;
    LARGE_INTEGER qpc;
    IN_ADDR ipv4;
    SOCKET sock;
    HANDLE h, h2;
    BOOL other_link = FALSE;
    long long phc = 0, base;
    unsigned nonce, sent = 0, send_failed = 0, seen = 0, first_after = 0;
    avb_u32 mbps, frame;
    DEVINST inst;
    LONG captured = 0, from_target = 0;
    QUERY_READ st_race, st_commit, st_pending;   /* --hold STATUS_*: immediately before the race, right after it, before BaseT */

    BOOL wire = FALSE, has_link, has_ip = FALSE;
    char why[300];

    printf("\n=== adapter:%u did=0x%04X luid=0x%016llX ifIndex=%u ===\n", index, id->device_id, id->net_luid, id->if_index);
    g_ran++;
    g_next_seq = 0;   /* per adapter run: no capture is active here, so every tag slot is free */
    g_capture_luid = 0;   /* no observer carried over from the previous adapter run; find_observer sets it only when one is found (PR #481) */
    ZeroMemory(&ipv4, sizeof(ipv4));
    if (!owned_ops_settled(&g_ops, id->net_luid, 0) || m5_pending(id->net_luid)) {
        verdict("BLOCKED", "COMMIT", index, id->device_id,
                "an operation of an earlier case of this process on this adapter has not been observed to end: no pre-state can be established");
        return;
    }
    if (!find_devinst(id->net_luid, &inst)) {
        verdict("BLOCKED", "COMMIT", index, id->device_id,
                "cannot resolve the adapter device node required for NIC-cycle cleanup");
        return;
    }

    /* Up to the activation: reads, bounded TX and, only when the pre-state is not proved clean, the test-owned setup recovery */
    h = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "cannot bind a handle to the adapter");
        return;
    }
    /* Pre-state proved by the TAS query, as #369 requires; also the restoration reference. Not proved: recovered, not vetoed */
    if (!prove_prestate(h, "before", &qbefore, &armed_before)) {
        const char *why = NULL;
        h = recover_prestate(h, index, id, inst, &qbefore, &why);
        if (h == INVALID_HANDLE_VALUE) {
            verdict("BLOCKED", "COMMIT", index, id->device_id, why);
            return;
        }
    }
    /* The link and an IPv4 address serve the wire and the cleanup's TX probe only: without them those are unmeasured, the
     * activation still runs (#369 amendment, #463). The MTU sizes max_frame_bytes */
    if (!read_if(id->net_luid, &row)) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "GetIfEntry2 failed: no MTU for max_frame_bytes");
        CloseHandle(h);
        return;
    }
    has_link = row.OperStatus == IfOperStatusUp;
    has_ip = has_link && find_addressing(id->net_luid, &ipv4, &other_link);
    g_cleanup_link_was_up = has_link;
    printf("    [EVIDENCE] another adapter up with a gateway: %s (diagnostic only, not a precondition: the owned NIC cycle may interrupt the host network; evidence is kept locally, #463)\n", other_link ? "yes" : "no");
    mbps = has_link ? (avb_u32)(row.TransmitLinkSpeed / 1000000ULL) : 0u;
    frame = (avb_u32)row.Mtu + FRAME_OVERHEAD_BYTES;
    printf("    link=%s %u Mbit/s ipv4=%s mtu=%lu frameOnWire=%u\n", has_link ? "up" : "down", mbps, has_ip ? "yes" : "no", row.Mtu, frame);
    if (g_mode == MODE_HOLD || g_mode == MODE_PASS) {   /* #370 amendment: --pass no longer calls function 71 */
        check_access_80c(index, id);
    }
    sock = has_ip ? open_tx_socket(ipv4) : INVALID_SOCKET;
    nonce = 0;
    if (sock != INVALID_SOCKET) {
        wire = find_observer(index, id, sock, ipv4, &nonce, &sent, &send_failed, &seen);
    } else {
        verdict("BLOCKED", "OBSERVER", index, id->device_id, has_ip ? "no TX socket: WIRE and the cleanup's TX probe are not measured; "
                "the other checks still run" : "no link or no IPv4 address: no frames can be sent, WIRE and the cleanup's TX probe are not "
                "measured; the other checks still run");
    }
    if (!wire && sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    printf("    baseline: sent=%u sendFailed=%u seen=%u (observer %s)\n", sent, send_failed, seen, wire ? "established" : "none");
    h2 = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
    if (h2 == INVALID_HANDLE_VALUE || !read_stats(h, &stats) || !take_anchor(h, &before_anchor, "before") || !read_phc(h, &phc)) {
        if (h2 != INVALID_HANDLE_VALUE) { CloseHandle(h2); }
        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        verdict("BLOCKED", "COMMIT", index, id->device_id, "second handle, statistics or PHC unavailable before the activation");
        CloseHandle(h);
        return;
    }
    QueryPerformanceCounter(&qpc);
    {   /* BaseT lead: BASE_LEAD_NS, or the queried limits.min_margin_ns plus headroom when established and larger (#369 amendment) */
        long long lead = BASE_LEAD_NS;
        if (qbefore.q.limits.min_margin_state == AVB_TAS_LIMIT_ESTABLISHED) {
            if (qbefore.q.limits.min_margin_ns > (avb_u64)(LEAD_MAX_NS - LEAD_HEADROOM_NS)) {
                sprintf_s(why, sizeof(why), "limits.min_margin_ns %llu needs a lead beyond the test's bound of %lld ns",
                          qbefore.q.limits.min_margin_ns, (long long)LEAD_MAX_NS);
                CloseHandle(h2);
                if (wire) { wire_stop(); }
                if (sock != INVALID_SOCKET) { closesocket(sock); }
                verdict("BLOCKED", "COMMIT", index, id->device_id, why);
                CloseHandle(h);
                return;
            }
            if ((long long)qbefore.q.limits.min_margin_ns + LEAD_HEADROOM_NS > lead) {
                lead = (long long)qbefore.q.limits.min_margin_ns + LEAD_HEADROOM_NS;
            }
        }
        printf("    [EVIDENCE] BaseT lead %lld ns (limits.min_margin_ns %llu, state %u: %s)\n", lead, qbefore.q.limits.min_margin_ns,
               qbefore.q.limits.min_margin_state, qbefore.q.limits.min_margin_state == AVB_TAS_LIMIT_ESTABLISHED ? "established" :
               "not established, D.3: the test's own 1.5 s lead");
        base = (phc + lead + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS;
    }
    build_request(&req, g_mode == MODE_PASS ? AVB_TAS_PRE_PASS : AVB_TAS_PRE_HOLD, base, mbps, frame);
    ZeroMemory(&req_op, sizeof(req_op));
    if (g_mode == MODE_FAULT) {
        printf("    request: pre=%u base=%lld (PHC %lld, lead %lld ms) cycle=%u window=%lld..%lld ms link=%u frame=%u\n", req.pre_activation, base,
               phc, (base - phc) / NS_PER_MS, req.cycle_ns, WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS, mbps, frame);
    }

    /* First mutation. From here the cleanup always runs, whatever TAS_ACTIVATE returns */
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_adapter, (LONG)index);

    if (g_mode == MODE_META && g_meta_case == 4) {
        /* TC-TASACT-META-004: FilterRestart with a retained schedule. h controls and measures, h2 is the second instance (closed there) */
        avb_u64 old_epoch;

        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        old_epoch = run_meta_restart(h, h2, index, id, frame);
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        meta_new_context(index, id, old_epoch);
        return;
    }

    if (g_mode == MODE_META && g_meta_case == 7) {
        /* TC-TASACT-META-007: h queries and judges; the MDIO restart has its own read/write handle bound to the same NET_LUID */
        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        CloseHandle(h2);
#ifndef NDEBUG
        run_meta_link(h, index, id, frame);
#else
        verdict("INCONCLUSIVE", "COMMIT", index, id->device_id, "built with NDEBUG: META-007 is not compiled (never PASS)");
        m5_not_run("INCONCLUSIVE", index, id->device_id, "built with NDEBUG: not exercised");
#endif
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    if (g_mode == MODE_META && (g_meta_case == 5 || g_meta_case == 6)) {
        /* TC-TASACT-META-005 and -006: one handle for every access of the case */
        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        CloseHandle(h2);
        run_meta_mismatch(h, index, id, frame);
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    if (g_mode == MODE_META && g_meta_case != 1) {
        /* TC-TASACT-META-002 and -003: the F1 interleaving. h controls and measures, h2 sends the ACTIVATE (closed by run_meta_race) */
        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        run_meta_race(h, h2, index, id, frame, g_meta_case == 3);
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    if (g_mode == MODE_META) {
        /* --metadata-events (#352 R1 L9/L10, under #357). Each line is its own measurement; a missing one is named, never read as
         * unchanged. The owned cleanup below always runs */
        AVB_TAS_OP_REQUEST op;
        AVB_TIMESTAMP_REQUEST ts;
        AVB_OFFSET_REQUEST off;
        AVB_DISARM_TAS_REQUEST dis;
        AVB_TAS_REQUEST setup;
        QUERY_READ qm0, qm1, qa, qb, qc, qd, qe, qf;
        DWORD information = 0;
        LONG st;
        long long phc2 = 0, base2, w0, w1, w2, w3;
        BOOL retained, stepped;

        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        CloseHandle(h2);

        /* CLEAN_EVENT: a PHC offset step while the record is CLEAN: L10 applies to a retained schedule only, so +0 */
        query(h, &qm0);
        print_query("before the clean-state event", &qm0);
        ZeroMemory(&off, sizeof(off));
        off.offset_ns = META_OFFSET_NS;
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_PHC_OFFSET_ADJUST, &off, sizeof(off), &off, sizeof(off), &information);
        printf("    [EVIDENCE] PHC_OFFSET_ADJUST +%lld ns on the CLEAN record: ntstatus=0x%08lX information=%lu status=0x%X\n",
               (long long)META_OFFSET_NS, (unsigned long)st, information, off.status);
        query(h, &qm1);
        print_query("after the clean-state event", &qm1);
        if (st != 0) {
            sprintf_s(why, sizeof(why), "the PHC offset step did not complete (ntstatus=0x%08lX): no event to judge", (unsigned long)st);
            verdict("INCONCLUSIVE", "CLEAN_EVENT", index, id->device_id, why);
        } else {
            meta_line("CLEAN_EVENT", index, id->device_id, &qm0, &qm1, 0, "PHC offset step with the record CLEAN (no retained schedule)");
        }

        /* COMMIT: the retained schedule the L10 events need (HOLD, explicit acceptance, the pair of the query just taken) */
        retained = FALSE;
        if (!read_phc(h, &phc2)) {
            verdict("BLOCKED", "COMMIT", index, id->device_id, "PHC unavailable: no BaseT can be placed for the retained schedule");
        } else {
            base2 = (phc2 + BASE_LEAD_NS + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS;
            build_op(&op, id->net_luid, &qm1.q, base2, frame);
            op.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
            information = 0;
            st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE, &op, sizeof(op), &op, sizeof(op), &information);
            printf("    [EVIDENCE] activation: op=0x%016llX base=%lld (PHC %lld) gen=%llu epoch=%llu -> ntstatus=0x%08lX information=%lu "
                   "out_flags=0x%X\n", op.client_op_id, base2, phc2, op.expected_generation, op.context_epoch, (unsigned long)st,
                   information, op.out_flags);
            retained = st == 0 && information == (DWORD)sizeof(op);
            if (retained) {
                verdict("PASS", "COMMIT", index, id->device_id, "COMMITTED: a schedule is retained for the L10 events");
            } else {
                sprintf_s(why, sizeof(why), "the HOLD activation did not commit (ntstatus=0x%08lX information=%lu)", (unsigned long)st,
                          information);
                verdict("FAIL", "COMMIT", index, id->device_id, why);
            }
        }
        w0 = wall_now_ns();
        query(h, &qa);
        w1 = wall_now_ns();
        print_query("after the commit", &qa);

        /* RESEED_SET and ONCE: SET_TIMESTAMP (a PHC reseed) while the schedule is retained invalidates the old view exactly
         * once in that event (L10); repeated queries do not advance it */
        if (!retained) {
            verdict("BLOCKED", "RESEED_SET", index, id->device_id, "no retained schedule (see COMMIT): the L10 event cannot be judged");
            verdict("BLOCKED", "STATUS_SET", index, id->device_id, "no retained schedule (see COMMIT)");
            verdict("BLOCKED", "ONCE", index, id->device_id, "no retained schedule (see COMMIT)");
            verdict("BLOCKED", "RESEED_OFFSET", index, id->device_id, "no retained schedule (see COMMIT)");
        } else {
            ZeroMemory(&ts, sizeof(ts));
            st = avb_ioctl_ntstatus(h, IOCTL_AVB_GET_TIMESTAMP, &ts, sizeof(ts), &ts, sizeof(ts), &information);
            if (st != 0) {
                sprintf_s(why, sizeof(why), "GET_TIMESTAMP failed (ntstatus=0x%08lX): no SET_TIMESTAMP target", (unsigned long)st);
                verdict("INCONCLUSIVE", "RESEED_SET", index, id->device_id, why);
                verdict("INCONCLUSIVE", "STATUS_SET", index, id->device_id, "the reseed was not performed (see RESEED_SET)");
                verdict("INCONCLUSIVE", "ONCE", index, id->device_id, "the reseed was not performed (see RESEED_SET)");
            } else {
                avb_u64 target = ts.timestamp + (avb_u64)META_SET_AHEAD_NS;
                ZeroMemory(&ts, sizeof(ts));
                ts.timestamp = target;
                st = avb_ioctl_ntstatus(h, IOCTL_AVB_SET_TIMESTAMP, &ts, sizeof(ts), &ts, sizeof(ts), &information);
                printf("    [EVIDENCE] SET_TIMESTAMP %llu with the schedule retained: ntstatus=0x%08lX status=0x%X\n", target,
                       (unsigned long)st, ts.status);
                w2 = wall_now_ns();
                query(h, &qb);
                w3 = wall_now_ns();
                print_query("after SET_TIMESTAMP", &qb);
                query(h, &qc);
                print_query("queried again", &qc);
                if (st != 0) {
                    sprintf_s(why, sizeof(why), "SET_TIMESTAMP did not complete (ntstatus=0x%08lX): no reseed to judge", (unsigned long)st);
                    verdict("INCONCLUSIVE", "RESEED_SET", index, id->device_id, why);
                } else {
                    meta_line("RESEED_SET", index, id->device_id, &qa, &qb, 1, "SET_TIMESTAMP (PHC reseed) with a retained schedule");
                }
                stepped = tas_phc_step(META_SET_AHEAD_NS, query_valid(&qa), &qa.q, query_valid(&qb), &qb.q, w2 - w1, w3 - w0);
                printf("    [EVIDENCE] STATUS_SET: PHC %llu -> %llu over %lld to %lld ns of wall time: the +1 s step is %s\n", qa.q.phc_after_ns,
                       qb.q.phc_before_ns, w2 - w1, w3 - w0, stepped ? "measured" : "NOT measured");
                meta_reseed_status("STATUS_SET", index, id->device_id, &qa, &qb, &op, st == 0 && stepped, "SET_TIMESTAMP +1 s");
                meta_line("ONCE", index, id->device_id, &qb, &qc, 0, "a second query after the same event");
            }
            /* RESEED_OFFSET: a PHC offset step with the schedule still retained is another reseed event: +1 once */
            query(h, &qc);
            ZeroMemory(&off, sizeof(off));
            off.offset_ns = META_OFFSET_NS;
            st = avb_ioctl_ntstatus(h, IOCTL_AVB_PHC_OFFSET_ADJUST, &off, sizeof(off), &off, sizeof(off), &information);
            printf("    [EVIDENCE] PHC_OFFSET_ADJUST +%lld ns with the schedule retained: ntstatus=0x%08lX status=0x%X\n",
                   (long long)META_OFFSET_NS, (unsigned long)st, off.status);
            query(h, &qd);
            print_query("after PHC_OFFSET_ADJUST", &qd);
            if (st != 0) {
                sprintf_s(why, sizeof(why), "PHC_OFFSET_ADJUST did not complete (ntstatus=0x%08lX): no event to judge", (unsigned long)st);
                verdict("INCONCLUSIVE", "RESEED_OFFSET", index, id->device_id, why);
            } else {
                meta_line("RESEED_OFFSET", index, id->device_id, &qc, &qd, 1, "PHC offset step with a retained schedule");
            }
        }

        /* DISARM_NOWRITE (L9): a disarm whose device path writes nothing (STATUS_NOT_SUPPORTED: -ENOTSUP) does not advance */
        query(h, &qd);
        ZeroMemory(&dis, sizeof(dis));
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_DISARM_TAS, &dis, sizeof(dis), &dis, sizeof(dis), &information);
        printf("    [EVIDENCE] DISARM_TAS: ntstatus=0x%08lX status=0x%X\n", (unsigned long)st, dis.status);
        query(h, &qe);
        print_query("after DISARM_TAS", &qe);
        if (st == (LONG)STATUS_NOT_SUPPORTED) {
            meta_line("DISARM_NOWRITE", index, id->device_id, &qd, &qe, 0, "DISARM_TAS on its no-write path (STATUS_NOT_SUPPORTED)");
        } else {
            sprintf_s(why, sizeof(why), "DISARM_TAS completed 0x%08lX, not the no-write path this line judges; whether it attempted a "
                      "write is not observable here (generation %llu -> %llu printed)", (unsigned long)st, qd.q.generation, qe.q.generation);
            verdict("INCONCLUSIVE", "DISARM_NOWRITE", index, id->device_id, why);
        }

        /* SETUP_WRITE (L9): a SETUP_TAS that completes STATUS_SUCCESS wrote registers: exactly +1 */
        ZeroMemory(&setup, sizeof(setup));
        setup.config.base_time_s = (uint64_t)((phc2 + BASE_LEAD_NS) / NS_PER_SEC);
        setup.config.cycle_time_s = (uint32_t)(CYCLE_NS / NS_PER_SEC);
        setup.config.cycle_time_ns = (uint32_t)(CYCLE_NS % NS_PER_SEC);
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_SETUP_TAS, &setup, sizeof(setup), &setup, sizeof(setup), &information);
        printf("    [EVIDENCE] SETUP_TAS: ntstatus=0x%08lX status=0x%X\n", (unsigned long)st, setup.status);
        query(h, &qf);
        print_query("after SETUP_TAS", &qf);
        if (st == 0) {
            meta_line("SETUP_WRITE", index, id->device_id, &qe, &qf, 1, "SETUP_TAS that completed STATUS_SUCCESS (registers written)");
        } else {
            sprintf_s(why, sizeof(why), "SETUP_TAS completed 0x%08lX: whether its device path attempted a write is not observable here "
                      "(generation %llu -> %llu printed)", (unsigned long)st, qe.q.generation, qf.q.generation);
            verdict("INCONCLUSIVE", "SETUP_WRITE", index, id->device_id, why);
        }

        /* SETUP_NOWRITE (L9, #503): a SETUP_TAS whose device path attempts no TAS write does not advance. The path is "PHC not
         * running": SYSTIM reads 0 before the first write. SYSTIM is frozen generically (SET_HW_TIMESTAMPING enable=0) and zeroed
         * through the Debug-only WRITE_REGISTER at the SYSTIML/SYSTIMH offsets the driver's HAL reports in QUERY_HW_SNAPSHOT
         * version 2; this test holds no register offset and selects no family. Each setup step that does not establish the
         * frozen zero is named (INCONCLUSIVE), never judged as L9. Owned restoration: the NIC cycle below (bring-up reseeds
         * SYSTIM); timestamping is re-enabled first, reported only */
#ifndef NDEBUG
        {
            AVB_HW_SNAPSHOT_REQUEST_V2 snap2;
            AVB_HW_TIMESTAMPING_REQUEST hts;
            AVB_REGISTER_REQUEST wr;
            QUERY_READ qg, qh;
            const AVB_HWREG_ENTRY *lo, *hi;
            avb_u32 offL = 0, offH = 0;
            LONG wst;

            ZeroMemory(&snap2, sizeof(snap2));
            snap2.version = AVB_HW_SNAPSHOT_VERSION_2;
            information = 0;
            st = avb_ioctl_ntstatus(h, IOCTL_AVB_QUERY_HW_SNAPSHOT, &snap2, sizeof(snap2), &snap2, sizeof(snap2), &information);
            lo = &snap2.entries[AVB_HWREG_SYSTIML - 1];
            hi = &snap2.entries[AVB_HWREG_SYSTIMH - 1];
            printf("    [EVIDENCE] snapshot v2: ntstatus=0x%08lX information=%lu did=0x%04X luid=0x%016llX SYSTIML offset=0x%05X "
                   "value=0x%08X state=%u, SYSTIMH offset=0x%05X value=0x%08X state=%u\n", (unsigned long)st, information,
                   snap2.device_id, snap2.net_luid, lo->offset, lo->value, lo->state, hi->offset, hi->value, hi->state);
            if (st != 0 || information != (DWORD)sizeof(snap2) || snap2.device_id != id->device_id || snap2.net_luid != id->net_luid ||
                lo->state != AVB_HWREG_STATE_VALID || hi->state != AVB_HWREG_STATE_VALID) {
                sprintf_s(why, sizeof(why), "the version-2 snapshot did not report both SYSTIM offsets for this adapter (ntstatus=0x%08lX, "
                          "%lu bytes, states %u/%u): the no-write path cannot be established", (unsigned long)st, information,
                          lo->state, hi->state);
                verdict("INCONCLUSIVE", "SETUP_NOWRITE", index, id->device_id, why);
            } else {
                offL = lo->offset;
                offH = hi->offset;
                ZeroMemory(&hts, sizeof(hts));
                hts.enable = 0;
                hts.timer_mask = 0x1;
                information = 0;
                st = avb_ioctl_ntstatus(h, IOCTL_AVB_SET_HW_TIMESTAMPING, &hts, sizeof(hts), &hts, sizeof(hts), &information);
                printf("    [EVIDENCE] SET_HW_TIMESTAMPING enable=0: ntstatus=0x%08lX TSAUXC 0x%08X -> 0x%08X\n", (unsigned long)st,
                       hts.previous_tsauxc, hts.current_tsauxc);
                wst = (LONG)0xFFFFFFFF;
                if (st == 0) {
                    ZeroMemory(&wr, sizeof(wr));
                    wr.offset = offL;
                    wr.value = 0;
                    wst = avb_ioctl_ntstatus(h, IOCTL_AVB_WRITE_REGISTER, &wr, sizeof(wr), &wr, sizeof(wr), &information);
                    if (wst == 0) {
                        ZeroMemory(&wr, sizeof(wr));
                        wr.offset = offH;
                        wr.value = 0;
                        wst = avb_ioctl_ntstatus(h, IOCTL_AVB_WRITE_REGISTER, &wr, sizeof(wr), &wr, sizeof(wr), &information);
                    }
                    printf("    [EVIDENCE] WRITE_REGISTER SYSTIML(0x%05X)=0, SYSTIMH(0x%05X)=0: ntstatus=0x%08lX\n", offL, offH,
                           (unsigned long)wst);
                }
                ZeroMemory(&snap2, sizeof(snap2));
                snap2.version = AVB_HW_SNAPSHOT_VERSION_2;
                (void)avb_ioctl_ntstatus(h, IOCTL_AVB_QUERY_HW_SNAPSHOT, &snap2, sizeof(snap2), &snap2, sizeof(snap2), &information);
                printf("    [EVIDENCE] SYSTIM read back: SYSTIML=0x%08X (state %u) SYSTIMH=0x%08X (state %u)\n", lo->value, lo->state,
                       hi->value, hi->state);
                if (st != 0 || wst != 0 || lo->state != AVB_HWREG_STATE_VALID || hi->state != AVB_HWREG_STATE_VALID ||
                    lo->value != 0 || hi->value != 0) {
                    sprintf_s(why, sizeof(why), "SYSTIM was not established frozen at 0 (timestamping 0x%08lX, writes 0x%08lX, read back "
                              "0x%08X/0x%08X): the no-write path was not reached", (unsigned long)st, (unsigned long)wst, lo->value,
                              hi->value);
                    verdict("INCONCLUSIVE", "SETUP_NOWRITE", index, id->device_id, why);
                } else {
                    query(h, &qg);
                    print_query("before the no-write SETUP_TAS", &qg);
                    setup.status = 0;
                    information = 0;
                    st = avb_ioctl_ntstatus(h, IOCTL_AVB_SETUP_TAS, &setup, sizeof(setup), &setup, sizeof(setup), &information);
                    printf("    [EVIDENCE] SETUP_TAS with SYSTIM frozen at 0: ntstatus=0x%08lX status=0x%X\n", (unsigned long)st,
                           setup.status);
                    query(h, &qh);
                    print_query("after the no-write SETUP_TAS", &qh);
                    if (st == 0) {
                        sprintf_s(why, sizeof(why), "SETUP_TAS completed STATUS_SUCCESS with SYSTIM frozen at 0: the no-write path was "
                                  "not taken (generation %llu -> %llu printed)", qg.q.generation, qh.q.generation);
                        verdict("INCONCLUSIVE", "SETUP_NOWRITE", index, id->device_id, why);
                    } else {
                        meta_line("SETUP_NOWRITE", index, id->device_id, &qg, &qh, 0,
                                  "SETUP_TAS refused before its first TAS write (SYSTIM frozen at 0)");
                    }
                }
                ZeroMemory(&hts, sizeof(hts));
                hts.enable = 1;
                hts.timer_mask = 0x1;
                st = avb_ioctl_ntstatus(h, IOCTL_AVB_SET_HW_TIMESTAMPING, &hts, sizeof(hts), &hts, sizeof(hts), &information);
                printf("    [EVIDENCE] SET_HW_TIMESTAMPING enable=1 before the cleanup: ntstatus=0x%08lX TSAUXC 0x%08X -> 0x%08X "
                       "(SYSTIM is reseeded by the NIC cycle)\n", (unsigned long)st, hts.previous_tsauxc, hts.current_tsauxc);
            }
        }
#else
        verdict("INCONCLUSIVE", "SETUP_NOWRITE", index, id->device_id,
                "Release build: WRITE_REGISTER is Debug-only, so the L9 no-write path is not executable here (never PASS)");
#endif
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    if (g_mode == MODE_FAULT) {
        /* Function-71 withdrawal check (D-C, Q 2026-11-21, supplementary resolution on #482/#357): the request this legacy
         * regression always sent (the verified HOLD set with the fault selector FAULT_WRITE) on the withdrawn 80-byte function
         * 71 must be refused as an unserved code, with no output and nothing changed. This proves only that function 71 is
         * refused; it is never fault/rollback evidence, which is --fault-versioned's alone. Each measurement keeps its own
         * validity: a missing one is named, a measured change stays FAIL whatever else is missing */
        QUERY_READ qa;
        DWORD information = 0;
        LONG st;
        BOOL state_ok;
        unsigned differing = 0, unread = 0;

        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        CloseHandle(h2);
        req.reserved = FAULT_WRITE;
        printf("\n  --- function-71 withdrawal: the legacy 80-byte request (fault selector %u) on IOCTL_AVB_TAS_ACTIVATE_LEGACY_FN71 0x%08lX ---\n",
               FAULT_WRITE, (unsigned long)IOCTL_AVB_TAS_ACTIVATE_LEGACY_FN71);
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_LEGACY_FN71, &req, (DWORD)sizeof(req), &req, (DWORD)sizeof(req), &information);
        printf("    [EVIDENCE] function-71 reply: ntstatus=%s0x%08lX information=%lu%s\n", st == (LONG)AVB_NTSTATUS_UNAVAILABLE ? "unavailable " : "",
               (unsigned long)st, information, information == (DWORD)sizeof(req) ? " (the legacy request was served)" : "");
        if (information == (DWORD)sizeof(req)) {
            print_call("served legacy reply", TRUE, ERROR_SUCCESS, &req);
        }
        if (st == (LONG)AVB_NTSTATUS_UNAVAILABLE) {
            verdict("INCONCLUSIVE", "WITHDRAWN", index, id->device_id, "the raw NTSTATUS of the function-71 request was not captured");
        } else if (st == (LONG)STATUS_INVALID_DEVICE_REQUEST && information == 0) {
            verdict("PASS", "WITHDRAWN", index, id->device_id, "function 71 refused as an unserved code (STATUS_INVALID_DEVICE_REQUEST, no output)");
        } else {
            sprintf_s(why, sizeof(why), "function 71 must be withdrawn (STATUS_INVALID_DEVICE_REQUEST, no output); ntstatus=0x%08lX "
                      "information=%lu%s", (unsigned long)st, information, information != 0 ? ": the legacy request was served" : "");
            verdict("FAIL", "WITHDRAWN", index, id->device_id, why);
        }
        /* UNCHANGED: the restoration set against the test's own pre-state query, register by register, and the TAS record */
        query_lookup(h, 0, &qa);
        restoration_set_compare(&qbefore, &qa, &differing, &unread);
        state_ok = read_state(h, &s);
        if (state_ok) { print_state("after the function-71 request", &s); }
        if (differing != 0 || (state_ok && s.armed != AVB_TAS_STATE_CLEAN)) {
            sprintf_s(why, sizeof(why), "a function-71 request must change nothing: %u restoration-set register(s) differ from the pre-state "
                      "query, record %s", differing, state_ok ? (s.armed == AVB_TAS_STATE_CLEAN ? "CLEAN" : "not CLEAN") : "unread");
            verdict("FAIL", "UNCHANGED", index, id->device_id, why);
        } else if (unread != 0 || !state_ok) {
            sprintf_s(why, sizeof(why), "no change measured, but %u of 17 restoration-set register(s) unread and the record %s: missing "
                      "measurement, not a pass", unread, state_ok ? "read" : "unread");
            verdict("INCONCLUSIVE", "UNCHANGED", index, id->device_id, why);
        } else {
            verdict("PASS", "UNCHANGED", index, id->device_id, "17 of 17 restoration-set registers as in the pre-state query, record CLEAN");
        }
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    if (g_mode == MODE_FAULTV) {
        /* #482 F1/F2 on the versioned request. Every measurement keeps its own validity rule; a missing one is named, never read
         * as unchanged, and a measured violation stays FAIL whatever else is missing. The owned cleanup below always runs */
        const BOOL f2 = g_fault_case == 2;
        const LONG expected = f2 ? (LONG)STATUS_UNSUCCESSFUL : (LONG)STATUS_IO_DEVICE_ERROR;
        AVB_TAS_OP_REQUEST op;
        IOCTL_VERSION version = {0};
        QUERY_READ qa;
        DWORD br = 0, information = 0;
        LONG st;
        BOOL build_known, debug_driver = FALSE, state_ok, fault_ran, gen_reported, gen_measured;
        avb_u32 armed_after = 0;
        unsigned differing = 0, unread = 0;
        char viol[700], miss[400], reason[1200], status_text[96];

        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        CloseHandle(h2);
        build_known = DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL) &&
                      br >= sizeof(version);   /* only a complete reply carries Flags */
        if (build_known) {
            debug_driver = (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) != 0;
        }
        if (!build_known || !debug_driver) {
            verdict("BLOCKED", "COMPLETION", index, id->device_id, !build_known
                    ? "GET_VERSION gave no complete reply: Debug or Release is unknown, so the fault request is not sent"
                    : "Release driver: debug_fault injection is Debug-only (#365 13.3); the Release refusal of a non-zero debug_fault is "
                      "#368's negative control, not claimed here; the fault request is not sent");
            (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
            return;
        }
        build_op(&op, id->net_luid, &qbefore.q, base, frame);
        op.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
        op.debug_fault = (FAULTV_FIRST_SCHEDULE_WRITE & AVB_TAS_DEBUG_FAULT_WRITE_MASK) | (f2 ? AVB_TAS_DEBUG_FAULT_RESTORE : 0u);
        printf("\n  --- %s: IOCTL_AVB_TAS_ACTIVATE_80C, HOLD, the first schedule write fails%s (Debug driver) ---\n", f2 ? "F2" : "F1",
               f2 ? " and the first restore write fails" : ", restore writes not failed");
        printf("    request: op=0x%016llX pre=HOLD base=%lld (PHC %lld, lead %lld ms) cycle=%lld window=%lld..%lld ms frame=%u flags=0x%X "
               "(ACCEPT_UNVERIFIED: authorizes execution, not HARDWARE_VERIFIED) debug_fault=0x%X gen=%llu%s epoch=%llu%s\n",
               op.client_op_id, base, phc, (base - phc) / NS_PER_MS, CYCLE_NS, WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS,
               frame, op.flags, op.debug_fault, op.expected_generation, op.expected_generation == 0 ? " (not reported)" : "",
               op.context_epoch, op.context_epoch == 0 ? " (not reported)" : "");
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &op, sizeof(op), &op, sizeof(op), &information);
        tas_fv_status_text(st != AVB_NTSTATUS_UNAVAILABLE, st, status_text, sizeof(status_text));
        printf("    fault request: %s information=%lu (expected 0x%08lX, no output)\n", status_text, information, (unsigned long)expected);
        fault_ran = st == expected && information == 0;
        state_ok = read_state(h, &s);
        if (state_ok) {
            armed_after = s.armed;
            print_state("after the fault request", &s);
        }
        query(h, &qa);
        print_query("after the fault request", &qa);
        restoration_set_compare(&qbefore, &qa, &differing, &unread);
        printf("    [EVIDENCE] restoration set (DES 12.5, BASET_L/H included): %u of %u compared registers differ from the pre-state "
               "query, %u of 17 unread\n", differing, 17u - unread, unread);
        gen_reported = query_valid(&qbefore) && qbefore.q.generation != 0;
        gen_measured = gen_reported && query_valid(&qa) && qa.q.generation != 0;
        if (!gen_reported) {
            printf("    [EVIDENCE] generation: not reported before the request (0): neither a value nor evidence of a transition\n");
        } else {
            printf("    [EVIDENCE] generation: before %llu, after %s%llu\n", qbefore.q.generation, gen_measured ? "" : "not reported ",
                   query_valid(&qa) ? qa.q.generation : 0ull);
        }

        /* METADATA (#482; Q 2026-11-19): whether setup obtained a valid, current, non-zero generation/epoch pair. Independent of the
         * completion: missing metadata stays nonpassing and visible; the request above carried the query's values as reported */
        if (!query_valid(&qbefore)) {
            verdict("FAIL", "METADATA", index, id->device_id, "the pre-state query is not valid: no current generation and epoch were "
                    "obtained; the control is not established (nothing fabricated)");
        } else if (qbefore.q.generation == 0 || qbefore.q.context_epoch == 0) {
            sprintf_s(reason, sizeof(reason), "the pre-state query reports generation %llu and epoch %llu: 0 is never a valid value, it "
                      "means not reported (#352 L2, #365 13.6 D.1); a valid current non-zero pair is required for the F1/F2 control, "
                      "which is therefore not established; the request carried the reported values, none fabricated",
                      qbefore.q.generation, qbefore.q.context_epoch);
            verdict("FAIL", "METADATA", index, id->device_id, reason);
        } else {
            sprintf_s(reason, sizeof(reason), "generation %llu and epoch %llu from the test's own pre-state query", qbefore.q.generation,
                      qbefore.q.context_epoch);
            verdict("PASS", "METADATA", index, id->device_id, reason);
        }

        /* COMPLETION */
        if (st == AVB_NTSTATUS_UNAVAILABLE) {
            verdict("INCONCLUSIVE", "COMPLETION", index, id->device_id, "the raw NTSTATUS was not captured");
        } else if (st == (LONG)STATUS_INVALID_DEVICE_REQUEST) {
            verdict("FAIL", "COMPLETION", index, id->device_id, "IOCTL_AVB_TAS_ACTIVATE_80C not served (STATUS_INVALID_DEVICE_REQUEST): the "
                    "code is allocated to the adapter's TAS function (#362, #357), so this is FAIL, never SKIP; the fault path was not "
                    "reached");
        } else if (st == (LONG)STATUS_INVALID_DEVICE_STATE && information == 0 && (op.expected_generation == 0 || op.context_epoch == 0)) {
            /* #352 L2 / #365 13.6 D.1: 0 is never a valid generation or epoch; a request carrying the query's unreported (0) metadata
             * is refused STATE_CONFLICT before any write. #369's rule: such a refusal is reported, not judged - the fault path was
             * not reached and nothing is attributed to it */
            verdict("INCONCLUSIVE", "COMPLETION", index, id->device_id, "fault-path attribution not established: STATUS_INVALID_DEVICE_STATE "
                    "with no output for a request carrying the query's generation or epoch 0 - refused before any write (STATE_CONFLICT, "
                    "#352 L2), so the injected fault is not shown to have run; the missing metadata is the separate METADATA failure");
        } else if (fault_ran) {
            verdict("PASS", "COMPLETION", index, id->device_id, f2 ? "STATUS_UNSUCCESSFUL with no output (the record's FAILED_NOT_RESTORED "
                    "is not retrievable yet, #352)" : "STATUS_IO_DEVICE_ERROR with no output (the record's FAILED_RESTORED is not "
                    "retrievable yet, #352)");
        } else {
            sprintf_s(reason, sizeof(reason), "completion ntstatus=0x%08lX information=%lu, expected 0x%08lX (%s) with no output",
                      (unsigned long)st, information, (unsigned long)expected, f2 ? "STATUS_UNSUCCESSFUL" : "STATUS_IO_DEVICE_ERROR");
            verdict("FAIL", "COMPLETION", index, id->device_id, reason);
        }

        if (!f2) {
            /* F1 ROLLBACK: registers at the test's own baseline and the record CLEAN */
            viol[0] = '\0';
            miss[0] = '\0';
            if (differing != 0) {
                sprintf_s(reason, sizeof(reason), "; %u measured restoration-set register(s) differ from the pre-state query (listed above)",
                          differing);
                strcat_s(viol, sizeof(viol), reason);
            }
            /* The record must be CLEAN when restoration is established (#365 R8): here, every restoration-set register measured
             * equal; and always when no write was attempted (fault path not reached). Otherwise a non-CLEAN record is not judged */
            if (!state_ok) {
                strcat_s(miss, sizeof(miss), "; the TAS record (GET_TAS_STATE) was not read");
            } else if (armed_after != AVB_TAS_STATE_CLEAN) {
                if (!fault_ran || (differing == 0 && unread == 0)) {
                    sprintf_s(reason, sizeof(reason), "; the TAS record is not CLEAN (armed=%u) although %s", armed_after,
                              fault_ran ? "every restoration-set register measured equal establishes restoration"
                                        : "the fault path was not reached (no write attempted)");
                    strcat_s(viol, sizeof(viol), reason);
                } else {
                    sprintf_s(reason, sizeof(reason), "; the TAS record is armed=%u while restoration is not established by the "
                              "measurement (registers unread)", armed_after);
                    strcat_s(miss, sizeof(miss), reason);
                }
            }
            if (unread != 0) {
                sprintf_s(reason, sizeof(reason), "; %u of 17 restoration-set register(s) unread before or after (listed above)", unread);
                strcat_s(miss, sizeof(miss), reason);
            }
            if (viol[0] != '\0') {
                sprintf_s(reason, sizeof(reason), "%s%s%s", viol + 2, miss[0] != '\0' ? "; also not measured" : "", miss);
                verdict("FAIL", "ROLLBACK", index, id->device_id, reason);
            } else if (!fault_ran) {
                sprintf_s(reason, sizeof(reason), "not exercised: the fault path was not reached (see COMPLETION), so restoration after a "
                          "failed schedule write is not established; measured: no restoration-set difference, record %s%s%s",
                          state_ok ? "CLEAN" : "unread", miss[0] != '\0' ? "; not measured" : "", miss);
                verdict("INCONCLUSIVE", "ROLLBACK", index, id->device_id, reason);
            } else if (miss[0] != '\0') {
                sprintf_s(reason, sizeof(reason), "no observed violation, but not established: %s", miss + 2);
                verdict("INCONCLUSIVE", "ROLLBACK", index, id->device_id, reason);
            } else {
                verdict("PASS", "ROLLBACK", index, id->device_id, "every restoration-set register equal to the pre-state query, record CLEAN");
            }
            /* F1 GENERATION: exactly one advance for the attempted mutation and its rollback, when reported */
            if (!gen_reported || !gen_measured) {
                verdict("INCONCLUSIVE", "GENERATION", index, id->device_id, !gen_reported
                        ? "the generation was not reported before the request (0): the transition cannot be measured and is not fabricated"
                        : "the generation was reported before the request but not after it");
            } else if (!fault_ran) {
                if (qa.q.generation != qbefore.q.generation) {
                    sprintf_s(reason, sizeof(reason), "the fault path was not reached, yet the generation changed from %llu to %llu",
                              qbefore.q.generation, qa.q.generation);
                    verdict("FAIL", "GENERATION", index, id->device_id, reason);
                } else {
                    verdict("INCONCLUSIVE", "GENERATION", index, id->device_id, "not exercised: the fault path was not reached (see "
                            "COMPLETION); the generation is unchanged");
                }
            } else if (qa.q.generation == qbefore.q.generation + 1) {
                verdict("PASS", "GENERATION", index, id->device_id, "advanced exactly once");
            } else {
                sprintf_s(reason, sizeof(reason), "generation %llu -> %llu, expected exactly one advance for the attempted mutation",
                          qbefore.q.generation, qa.q.generation);
                verdict("FAIL", "GENERATION", index, id->device_id, reason);
            }
        } else {
            /* F2 STATE: the record DIRTY after a failed restore (classification: tas_fv_f2_state, TC-MOCK-058) */
            switch (tas_fv_f2_state(fault_ran, state_ok, armed_after, differing)) {
            case TAS_FV_STATE_DIRTY:
                verdict("PASS", "STATE", index, id->device_id, NULL);
                break;
            case TAS_FV_STATE_NOT_DIRTY:
                sprintf_s(reason, sizeof(reason), "GET_TAS_STATE reports armed=%u, expected DIRTY (2)", armed_after);
                verdict("FAIL", "STATE", index, id->device_id, reason);
                break;
            case TAS_FV_STATE_UNREAD:
                verdict("INCONCLUSIVE", "STATE", index, id->device_id, "the TAS record (GET_TAS_STATE) was not read after the fault request");
                break;
            case TAS_FV_STATE_CHANGED_UNREACHED:
                if (state_ok) {
                    sprintf_s(reason, sizeof(reason), "the fault path was not reached (see COMPLETION), yet the record is armed=%u and %u "
                              "restoration-set register(s) differ from the pre-state query", armed_after, differing);
                } else {
                    sprintf_s(reason, sizeof(reason), "the fault path was not reached (see COMPLETION), yet %u restoration-set register(s) "
                              "differ from the pre-state query (the record was not read; a measured change is not hidden by it)", differing);
                }
                verdict("FAIL", "STATE", index, id->device_id, reason);
                break;
            default:
                verdict("INCONCLUSIVE", "STATE", index, id->device_id, "not exercised: the fault path was not reached (see COMPLETION), so "
                        "DIRTY is not established; measured: record CLEAN, no restoration-set difference");
                break;
            }
            /* F2 REFUSE: a further valid activation with the post-failure metadata (#369's rule) */
            if (!query_valid(&qa)) {
                verdict("BLOCKED", "REFUSE", index, id->device_id, "the query after the fault request failed: no current generation and "
                        "epoch for the further request, which is not sent");
            } else {
                OP_CALL again;
                AVB_TAS_STATE s2;
                QUERY_READ qb;
                unsigned d2 = 0, u2 = 0;
                TAS_FV_REFUSE_EVIDENCE ev;
                BOOL s2_ok;

                ZeroMemory(&again, sizeof(again));
                build_op(&again.sent, id->net_luid, &qa.q, base + CYCLE_NS, frame);
                again.sent.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;   /* debug_fault 0: an otherwise-valid HOLD */
                again.r = again.sent;
                again.status = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &again.r, sizeof(again.r), &again.r, sizeof(again.r),
                                                  &again.got);
                printf("    further request: op=0x%016llX base=%lld debug_fault=0 gen=%llu%s epoch=%llu%s (from the query after the failure)\n",
                       again.sent.client_op_id, base + CYCLE_NS, qa.q.generation, qa.q.generation == 0 ? " (not reported)" : "",
                       qa.q.context_epoch, qa.q.context_epoch == 0 ? " (not reported)" : "");
                print_op("activation after the failure", &again);
                s2_ok = read_state(h, &s2);
                if (s2_ok) { print_state("after the further request", &s2); }
                query(h, &qb);
                print_query("after the further request", &qb);
                restoration_set_compare(&qa, &qb, &d2, &u2);
                printf("    [EVIDENCE] further request: %u of %u compared restoration-set registers differ from the post-failure query, "
                       "%u of 17 unread\n", d2, 17u - u2, u2);
                /* Classification: tas_fv_f2_refuse (TC-MOCK-059) */
                ZeroMemory(&ev, sizeof(ev));
                ev.not_served = again.status == (LONG)STATUS_INVALID_DEVICE_REQUEST;
                ev.status_unavailable = again.status == AVB_NTSTATUS_UNAVAILABLE;
                ev.accepted = again.status == 0;
                ev.output = again.got != 0;
                ev.state_refusal = again.status == (LONG)STATUS_INVALID_DEVICE_STATE;
                ev.record_before = state_ok;
                ev.armed_before = armed_after;
                ev.record_after = s2_ok;
                ev.armed_after = s2_ok ? s2.armed : 0u;
                ev.differing = d2;
                ev.unread = u2;
                ev.fault_ran = fault_ran;
                ev.metadata_reported = qa.q.generation != 0 && qa.q.context_epoch != 0;
                switch (tas_fv_f2_refuse(&ev)) {
                case TAS_FV_REFUSE_NOT_SERVED:
                    verdict("FAIL", "REFUSE", index, id->device_id, "IOCTL_AVB_TAS_ACTIVATE_80C not served (STATUS_INVALID_DEVICE_REQUEST)");
                    break;
                case TAS_FV_REFUSE_STATUS_UNAVAILABLE:
                    sprintf_s(reason, sizeof(reason), "the raw NTSTATUS of the further request was not captured, and no independent "
                              "violation was measured (information=%lu, record %s, %u restoration-set register(s) differ, %u of 17 unread); "
                              "neither a refusal nor an unchanged state is established", again.got, s2_ok && state_ok ? "unchanged" :
                              "not read", d2, u2);
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, reason);
                    break;
                case TAS_FV_REFUSE_CHANGED:
                    tas_fv_status_text(!ev.status_unavailable, again.status, status_text, sizeof(status_text));
                    sprintf_s(reason, sizeof(reason), "the further request must be refused with no output and change nothing: %s "
                              "information=%lu, record %u -> %s%u, %u restoration-set register(s) changed", status_text,
                              again.got, armed_after, s2_ok ? "" : "unread ", s2_ok ? s2.armed : 0u, d2);
                    verdict("FAIL", "REFUSE", index, id->device_id, reason);
                    break;
                case TAS_FV_REFUSE_OTHER_STATUS:
                    sprintf_s(reason, sizeof(reason), "refused with ntstatus=0x%08lX, expected STATUS_INVALID_DEVICE_STATE", (unsigned long)again.status);
                    verdict("FAIL", "REFUSE", index, id->device_id, reason);
                    break;
                case TAS_FV_REFUSE_UNCHANGED_UNESTABLISHED:
                    sprintf_s(reason, sizeof(reason), "STATUS_INVALID_DEVICE_STATE with no output, but the unchanged state is not established "
                              "(record %s, %u of 17 restoration-set registers unread)", s2_ok && state_ok ? "read" : "unread", u2);
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, reason);
                    break;
                case TAS_FV_REFUSE_NOT_DIRTY:
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, "refused with STATUS_INVALID_DEVICE_STATE and nothing changed, but "
                            "the DIRTY precondition was not established (see COMPLETION and STATE)");
                    break;
                case TAS_FV_REFUSE_METADATA_ZERO:
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, "reported, not judged: STATUS_INVALID_DEVICE_STATE with no output "
                            "and the state kept, but the post-failure query reports generation or epoch 0 (not reported, #365 13.6 D.1), so "
                            "NOT_CLEAN cannot be told from STATE_CONFLICT");
                    break;
                default:
                    verdict("PASS", "REFUSE", index, id->device_id, "STATUS_INVALID_DEVICE_STATE, no output, record DIRTY and restoration set "
                            "unchanged");
                    break;
                }
            }
        }
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    if (g_mode == MODE_PASS) {
        /* #370 amendment 2026-11-19: PASS on the versioned request. Under #365's R7 order a header-valid PASS completes
         * STATUS_NOT_IMPLEMENTED with no output before the metadata, schedule, link and tier checks and before any register or
         * PHC access, until ADR-TSN-003 (#363). The state is measured against the proved-clean pre-state; each measurement keeps
         * its own validity rule, a missing one is named and never read as unchanged, and an observed violation stays FAIL even
         * when another measurement is missing. The cleanup below still runs */
        AVB_TAS_OP_REQUEST op;
        QUERY_READ qa;
        DWORD information = 0;
        LONG st;
        BOOL state_ok, gen_reported, gen_measured;
        unsigned differing = 0, unread = 0;
        char viol[700], miss[300], reason[1100];

        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        CloseHandle(h2);
        build_op(&op, id->net_luid, &qbefore.q, base, frame);
        op.pre_activation = AVB_TAS_PRE_PASS;
        printf("\n  --- IOCTL_AVB_TAS_ACTIVATE_80C with PASS (not implemented until ADR-TSN-003 is accepted, #363) ---\n");
        printf("    request: op=0x%016llX pre=PASS base=%lld (PHC %lld, lead %lld ms) cycle=%lld window=%lld..%lld ms frame=%u "
               "flags=0x%X fault=0x%X gen=%llu%s epoch=%llu%s\n", op.client_op_id, base, phc, (base - phc) / NS_PER_MS, CYCLE_NS,
               WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS, frame, op.flags, op.debug_fault, op.expected_generation,
               op.expected_generation == 0 ? " (not reported)" : "", op.context_epoch, op.context_epoch == 0 ? " (not reported)" : "");
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &op, sizeof(op), &op, sizeof(op), &information);
        printf("    PASS request: ntstatus=0x%08lX information=%lu\n", (unsigned long)st, information);
        state_ok = read_state(h, &s);
        if (state_ok) { print_state("after the PASS request", &s); }
        query(h, &qa);
        print_query("after the PASS request", &qa);
        restoration_set_compare(&qbefore, &qa, &differing, &unread);
        printf("    [EVIDENCE] restoration set (DES 12.5, BASET_L/H included): %u of %u compared registers differ from the pre-state "
               "query, %u of 17 unread\n", differing, 17u - unread, unread);
        gen_reported = query_valid(&qbefore) && qbefore.q.generation != 0;
        gen_measured = gen_reported && query_valid(&qa) && qa.q.generation != 0;
        if (!gen_reported) {
            printf("    [EVIDENCE] generation: not reported before the request (0): neither a value nor evidence that it did not "
                   "change; not applicable\n");
        } else {
            printf("    [EVIDENCE] generation: before %llu, after %s%llu\n", qbefore.q.generation, gen_measured ? "" : "not reported ",
                   query_valid(&qa) ? qa.q.generation : 0ull);
        }

        viol[0] = '\0';
        miss[0] = '\0';
        if (st == AVB_NTSTATUS_UNAVAILABLE) {
            strcat_s(miss, sizeof(miss), "; the raw NTSTATUS was not captured");
        } else if (st == (LONG)STATUS_INVALID_DEVICE_REQUEST) {
            strcat_s(viol, sizeof(viol), "; IOCTL_AVB_TAS_ACTIVATE_80C not served (STATUS_INVALID_DEVICE_REQUEST): the code is allocated "
                     "to the adapter's TAS function (#362, #357), so this is FAIL, never SKIP");
        } else if (st != (LONG)STATUS_NOT_IMPLEMENTED || information != 0) {
            sprintf_s(reason, sizeof(reason), "; completion ntstatus=0x%08lX information=%lu, expected STATUS_NOT_IMPLEMENTED with no "
                      "output (a header-valid PASS is recognized before the metadata check under R7, so zero metadata explains "
                      "no other status)", (unsigned long)st, information);
            strcat_s(viol, sizeof(viol), reason);
        }
        if (!state_ok) {
            strcat_s(miss, sizeof(miss), "; the TAS record (GET_TAS_STATE) was not read");
        } else if (s.armed != AVB_TAS_STATE_CLEAN) {
            sprintf_s(reason, sizeof(reason), "; the TAS record is not CLEAN (armed=%u)", s.armed);
            strcat_s(viol, sizeof(viol), reason);
        }
        /* Each register on its own (PR #484 r4626979931): a measured difference is a violation whatever else is unread, and
         * unread registers are a missing measurement whatever was found in the others */
        if (differing != 0) {
            sprintf_s(reason, sizeof(reason), "; %u measured restoration-set register(s) differ from the pre-state query (listed above)",
                      differing);
            strcat_s(viol, sizeof(viol), reason);
        }
        if (unread != 0) {
            sprintf_s(reason, sizeof(reason), "; %u of 17 restoration-set register(s) unread before or after (listed above)", unread);
            strcat_s(miss, sizeof(miss), reason);
        }
        if (gen_reported && !gen_measured) {
            strcat_s(miss, sizeof(miss), "; the generation was reported before the request but not after it");
        } else if (gen_measured && qa.q.generation != qbefore.q.generation) {
            sprintf_s(reason, sizeof(reason), "; the generation changed from %llu to %llu", qbefore.q.generation, qa.q.generation);
            strcat_s(viol, sizeof(viol), reason);
        }
        if (viol[0] != '\0') {
            sprintf_s(reason, sizeof(reason), "%s%s%s", viol + 2, miss[0] != '\0' ? "; also not measured" : "", miss);
            verdict("FAIL", "COMMIT", index, id->device_id, reason);
        } else if (miss[0] != '\0') {
            sprintf_s(reason, sizeof(reason), "no observed violation, but not established: %s", miss + 2);
            verdict("INCONCLUSIVE", "COMMIT", index, id->device_id, reason);
        } else {
            verdict("SKIP_NOT_IMPLEMENTED", "COMMIT", index, id->device_id,
                    "PASS is not implemented until ADR-TSN-003 is accepted (#363, #370 amendment): STATUS_NOT_IMPLEMENTED, no output, "
                    "TAS record CLEAN, restoration set as captured, generation unchanged or not reported; #370's traffic objective "
                    "is not verified by this");
        }
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    {
        /* The accepted HOLD race (#369 amendments 2026-11-18 and 2026-11-19), judged by tas_hold_commit and tas_hold_generation
         * (tests/ioctl/tas_activate_eval.h, TC-MOCK-062/063) from the two completions and the queries immediately before and after */
        OP_CALL calls[2];
        HANDLE threads[2], go = CreateEventA(NULL, TRUE, FALSE, NULL);
        unsigned success = 0, not_served = 0, k, winner = 0, transitions = 0;
        BOOL armed_ok, race_valid, race_clean;
        LONG armed_race = -1;
        QUERY_READ qr, qa, ql;
        const AVB_TAS_QUERY_HW_REQUEST *pair;
        TAS_HOLD_EVIDENCE ev;
        TAS_HOLD_OUT_EXPECT out;
        TAS_HOLD_COMMIT_CLASS cc;
        TAS_HOLD_GEN_CLASS gc;
        const char *tag;
        char reason[900], detail[300];

        printf("\n  --- IOCTL_AVB_TAS_ACTIVATE_80C from two bound open instances at once ---\n");
        /* The pair the driver compares under its claim, the driver-owned speed and the HOLD tier are measured immediately before the
         * race (as #368's cases are, PR #491 review): the observer trial lies between the proved pre-state and here. When this query
         * is not valid, the proved pre-state's pair is sent as the last observed one and printed NOT measured, never fabricated.
         * The clean prerequisite is measured here again, as the pre-state was proved (PR #492): NOT_CLEAN (#365 R7 step 9) shares
         * STATUS_INVALID_DEVICE_STATE with STATE_CONFLICT, so COMMIT attributes a STATE refusal or judges a success only on an
         * adapter measured clean immediately before the race. Not clean or not measured is reported, and the race still runs */
        race_clean = prove_prestate(h, "immediately before the race", &qr, &armed_race);
        race_valid = query_valid(&qr);
        printf("    [EVIDENCE] clean immediately before the race: %s (GET_TAS_STATE CLEAN and a clean, readable TAS query, as the proved "
               "pre-state; NOT_CLEAN, #365 R7 step 9, shares STATUS_INVALID_DEVICE_STATE with STATE_CONFLICT)\n", race_clean ? "yes" :
               (armed_race < 0 || !race_valid ? "NOT measured" : "no"));
        pair = race_valid ? &qr.q : &qbefore.q;
        if (race_valid) {
            printf("    [EVIDENCE] query before the race: valid; generation=%llu context_epoch=%llu (0 = not reported, never a value, #352 "
                   "R1) link_speed_mbps=%u (driver-owned, 0 = unknown, #351 R2) tier_hold=%u (AVB_TAS_TIER_*, 0 = not reported)\n",
                   qr.q.generation, qr.q.context_epoch, qr.q.link_speed_mbps, qr.q.limits.tier_hold);
        } else {
            printf("    [EVIDENCE] query before the race: NOT valid (ntstatus=0x%08lX information=%lu): pair, speed and tier NOT measured; "
                   "the requests carry the proved pre-state's generation=%llu context_epoch=%llu as the last observed pair\n",
                   (unsigned long)qr.status, qr.got, qbefore.q.generation, qbefore.q.context_epoch);
        }
        /* METADATA (#369 oracle amendment 2026-11-19, #352 R1 L13): once initialized the query reports the context's actual non-zero
         * generation and epoch. A 0 exposes missing implementation (or an unavailable context), never a valid value. Its own line;
         * the race still runs with what was measured */
        if (!race_valid) {
            verdict("INCONCLUSIVE", "METADATA", index, id->device_id, "the query before the race is not valid: generation and epoch "
                    "not measured");
        } else if (qr.q.generation == 0 || qr.q.context_epoch == 0) {
            sprintf_s(reason, sizeof(reason), "the query reports generation=%llu context_epoch=%llu: #352 R1 requires the actual non-zero "
                      "pair of the adapter context (0 = not reported, never a value). The accepted control carries it, so the contract "
                      "requires its refusal as STATE_CONFLICT at R7 step 4 (#352 R1); the actual completions are on the COMMIT "
                      "line", qr.q.generation, qr.q.context_epoch);
            verdict("FAIL", "METADATA", index, id->device_id, reason);
        } else {
            sprintf_s(reason, sizeof(reason), "generation=%llu context_epoch=%llu, both non-zero (#352 R1)", qr.q.generation,
                      qr.q.context_epoch);
            verdict("PASS", "METADATA", index, id->device_id, reason);
        }
        for (k = 0; k < 2; k++) {
            calls[k].h = k == 0 ? h : h2;
            calls[k].go = go;
            build_op(&calls[k].sent, id->net_luid, pair, base, frame);   /* the same schedule, a distinct client_op_id */
            calls[k].sent.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;   /* #362, #365 R4: executes unverified HOLD, never HARDWARE_VERIFIED */
            calls[k].r = calls[k].sent;
            calls[k].status = (LONG)AVB_NTSTATUS_UNAVAILABLE;
            calls[k].got = 0;
            threads[k] = go != NULL ? CreateThread(NULL, 0, op_thread, &calls[k], 0, NULL) : NULL;
        }
        printf("    request: pre=HOLD flags=0x%X (ACCEPT_UNVERIFIED: authorizes execution, not HARDWARE_VERIFIED) base=%lld (PHC %lld, lead "
               "%lld ms) cycle=%lld window=%lld..%lld ms frame=%u gen=%llu%s epoch=%llu%s\n", calls[0].sent.flags, base, phc,
               (base - phc) / NS_PER_MS, CYCLE_NS, WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS, frame, pair->generation,
               pair->generation == 0 ? " (not reported)" : "", pair->context_epoch, pair->context_epoch == 0 ? " (not reported)" : "");
        if (go != NULL) { SetEvent(go); }
        for (k = 0; k < 2; k++) {
            if (threads[k] != NULL) {
                (void)WaitForSingleObject(threads[k], INFINITE);
                CloseHandle(threads[k]);
            }
            print_op(k == 0 ? "instance 1" : "instance 2", &calls[k]);
        }
        if (go != NULL) { CloseHandle(go); }
        /* h2 stays open through LOOKUP: the record is retrieved on the originating open instance (#365 13.1, PR #481) */
        first_after = g_next_seq;
        query(h, &qa);   /* before any further request: GENERATION, COMMIT's isolation and REFUSE's metadata */
        print_query("after the race", &qa);
        st_race = qr;
        st_commit = qa;
        ZeroMemory(&st_pending, sizeof(st_pending));

        ZeroMemory(&ev, sizeof(ev));
        for (k = 0; k < 2; k++) {
            ev.call[k].captured = calls[k].status != AVB_NTSTATUS_UNAVAILABLE;
            ev.call[k].status = calls[k].status;
            ev.call[k].information = calls[k].got;
            ev.call[k].echoed = memcmp(&calls[k].r, &calls[k].sent, offsetof(AVB_TAS_OP_REQUEST, out_flags)) == 0;
            ev.call[k].out_flags = calls[k].r.out_flags;
            ev.call[k].reserved1 = calls[k].r.reserved1;
            if (ev.call[k].captured && calls[k].status == 0) { success++; winner = k; }
            if (ev.call[k].captured && calls[k].status == (LONG)STATUS_INVALID_DEVICE_REQUEST) { not_served++; }
        }
        ev.accept_sent = TRUE;
        ev.pre_valid = race_valid;
        ev.pre_clean = race_clean;
        ev.pre_generation = pair->generation;
        ev.pre_epoch = pair->context_epoch;
        ev.pre_mbps = race_valid ? qr.q.link_speed_mbps : 0u;
        ev.tier_hold = race_valid ? qr.q.limits.tier_hold : 0u;
        ev.post_valid = query_valid(&qa);
        ev.post_generation = ev.post_valid ? qa.q.generation : 0u;
        ev.post_epoch = ev.post_valid ? qa.q.context_epoch : 0u;
        ev.post_mbps = ev.post_valid ? qa.q.link_speed_mbps : 0u;
        out = tas_hold_out_expect(ev.pre_valid, ev.tier_hold);
        cc = tas_hold_commit(&ev);
        gc = tas_hold_generation(&ev, &transitions);
        printf("    [EVIDENCE] race facts: out_flags expectation %s (tier_hold %u%s); query after the race %s generation=%llu "
               "context_epoch=%llu link_speed_mbps=%u\n", out == TAS_HOLD_OUT_BIT0 ? "bit 0 (acceptance acts)" :
               (out == TAS_HOLD_OUT_ZERO ? "0 (acceptance does not act)" : "not judged"), ev.tier_hold,
               race_valid ? "" : ", not measured", ev.post_valid ? "valid" : "NOT valid:", ev.post_generation, ev.post_epoch, ev.post_mbps);

        detail[0] = '\0';
        if (cc == TAS_HOLD_COMMIT_PAIR_CHANGED || cc == TAS_HOLD_COMMIT_SPEED_UNDETERMINED || cc == TAS_HOLD_COMMIT_OUT_UNJUDGED) {
            sprintf_s(detail, sizeof(detail), "before the race %s generation=%llu epoch=%llu speed=%u tier_hold=%u; after it %s "
                      "generation=%llu epoch=%llu speed=%u", race_valid ? "valid" : "NOT measured", ev.pre_generation, ev.pre_epoch,
                      ev.pre_mbps, ev.tier_hold, ev.post_valid ? "valid" : "NOT measured", ev.post_generation, ev.post_epoch, ev.post_mbps);
        }
        switch (cc) {
        case TAS_HOLD_COMMIT_PASS:
            sprintf_s(reason, sizeof(reason), "one complete STATUS_SUCCESS reply (144 bytes, inputs echoed, reserved1 0, out_flags 0x%X as "
                      "tier_hold %u requires, #365 R4) and one STATUS_DEVICE_BUSY or STATUS_INVALID_DEVICE_STATE with no output; an "
                      "accepted unverified commit is not HARDWARE_VERIFIED and does not establish K2 (#352)", calls[winner].r.out_flags,
                      ev.tier_hold);
            break;
        case TAS_HOLD_COMMIT_NOT_SERVED:
            sprintf_s(reason, sizeof(reason), "IOCTL_AVB_TAS_ACTIVATE_80C not served (STATUS_INVALID_DEVICE_REQUEST on %u of 2 calls): the "
                      "code is allocated to the adapter's TAS function (#362, #357), so this is FAIL, never SKIP", not_served);
            break;
        case TAS_HOLD_COMMIT_BAD_SUCCESS:
            sprintf_s(reason, sizeof(reason), "a STATUS_SUCCESS reply is not complete (#365 13.2, R4): Information 144, inputs echoed, "
                      "reserved1 0 and no out_flags bit but bit 0 are required; see the two calls above");
            break;
        case TAS_HOLD_COMMIT_OUT_FLAGS:
            sprintf_s(reason, sizeof(reason), "out_flags 0x%X, but tier_hold %u requires %s: AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE is set "
                      "exactly when unverified acceptance acted (#365 R4), not because the flag was sent", calls[winner].r.out_flags,
                      ev.tier_hold, out == TAS_HOLD_OUT_BIT0 ? "bit 0" : "0");
            break;
        case TAS_HOLD_COMMIT_ZERO_ACCEPTED:
            sprintf_s(reason, sizeof(reason), "a request carrying generation=%llu context_epoch=%llu (0 = not reported) completed "
                      "STATUS_SUCCESS: #352 R1 requires a zero pair to be refused STATE_CONFLICT at R7 step 4", ev.pre_generation, ev.pre_epoch);
            break;
        case TAS_HOLD_COMMIT_TWO_SUCCESSES:
            sprintf_s(reason, sizeof(reason), "both calls completed STATUS_SUCCESS: the non-waiting per-adapter claim must admit "
                      "only one (#365 R7 step 3)");
            break;
        case TAS_HOLD_COMMIT_TIER_REFUSED:
            sprintf_s(reason, sizeof(reason), "STATUS_REQUEST_NOT_ACCEPTED although both requests carried AVB_TAS_F_ACCEPT_UNVERIFIED: "
                      "with it the driver must execute the HOLD operation (#365 R4, #362); no longer an undecided tier policy");
            break;
        case TAS_HOLD_COMMIT_R5:
            sprintf_s(reason, sizeof(reason), "STATUS_IO_TIMEOUT or STATUS_IO_DEVICE_ERROR with no output: #365 R5 maps BASE_PASSED to "
                      "STATUS_IO_TIMEOUT and COMMITTED_UNTIMED to STATUS_IO_DEVICE_ERROR, which R8's FAILED_RESTORED shares, so that status "
                      "does not detail the outcome; no complete commit (the adapter may hold a schedule, the owned cleanup follows)");
            break;
        case TAS_HOLD_COMMIT_META_ZERO:
            sprintf_s(reason, sizeof(reason), "both refused STATUS_INVALID_DEVICE_STATE: the requests carried generation=%llu "
                      "context_epoch=%llu (0 = not reported), which #352 R1 requires to be refused STATE_CONFLICT; the missing metadata (see "
                      "METADATA) is the likely cause, not an excuse: no commit", ev.pre_generation, ev.pre_epoch);
            break;
        case TAS_HOLD_COMMIT_STATE_REFUSED:
            sprintf_s(reason, sizeof(reason), "refused STATUS_INVALID_DEVICE_STATE although the pair sent (generation=%llu "
                      "context_epoch=%llu) was measured unchanged before and after the race on an adapter measured clean immediately "
                      "before it: no commit",
                      ev.pre_generation, ev.pre_epoch);
            break;
        case TAS_HOLD_COMMIT_NOT_READY:
            sprintf_s(reason, sizeof(reason), "refused STATUS_DEVICE_NOT_READY although the driver-owned speed was measured %u Mbit/s "
                      "before and after the race (#351 R2): no commit", ev.pre_mbps);
            break;
        case TAS_HOLD_COMMIT_OUT_UNJUDGED:
            sprintf_s(reason, sizeof(reason), "one complete STATUS_SUCCESS reply (out_flags 0x%X) and one BUSY or STATE refusal, but "
                      "whether unverified acceptance acted is not measured (%s), so out_flags is reported, not judged (#365 R4); never "
                      "PASS", calls[winner].r.out_flags, detail);
            break;
        case TAS_HOLD_COMMIT_PAIR_CHANGED:
            sprintf_s(reason, sizeof(reason), "refused STATUS_INVALID_DEVICE_STATE, but the pair the driver compared under its claim is "
                      "not determined: %s", detail);
            break;
        case TAS_HOLD_COMMIT_SPEED_UNDETERMINED:
            sprintf_s(reason, sizeof(reason), "refused STATUS_DEVICE_NOT_READY, but the driver-owned speed it sampled is not determined "
                      "(#351 R2: NOT_READY is the defined refusal of an unknown speed): %s", detail);
            break;
        case TAS_HOLD_COMMIT_NOT_CAPTURED:
            sprintf_s(reason, sizeof(reason), "a raw NTSTATUS was not captured (no driver completion observed) and no violation was "
                      "observed: not established");
            break;
        case TAS_HOLD_COMMIT_CLEAN_UNDETERMINED:
            sprintf_s(reason, sizeof(reason), "%s, but the adapter was %s immediately before the race (GET_TAS_STATE CLEAN and a clean, "
                      "readable TAS query): NOT_CLEAN (#365 R7 step 9) shares STATUS_INVALID_DEVICE_STATE with STATE_CONFLICT, so the "
                      "outcome is not attributed; see the evidence above", success != 0 ? "one complete STATUS_SUCCESS reply and one "
                      "BUSY or STATE refusal" : "refused STATUS_INVALID_DEVICE_STATE with the pair measured unchanged",
                      armed_race < 0 || !race_valid ? "not measured clean" : "measured NOT clean");
            break;
        default:
            sprintf_s(reason, sizeof(reason), "expected exactly one complete STATUS_SUCCESS reply and one STATUS_DEVICE_BUSY or "
                      "STATUS_INVALID_DEVICE_STATE with no output; see the two calls above%s", ev.pre_generation == 0 ||
                      ev.pre_epoch == 0 ? " (the requests carried generation or epoch 0, which #352 R1 requires to be refused STATE_CONFLICT; see "
                      "METADATA)" : "");
            break;
        }
        tag = cc == TAS_HOLD_COMMIT_PASS ? "PASS" : (cc == TAS_HOLD_COMMIT_OUT_UNJUDGED || cc == TAS_HOLD_COMMIT_PAIR_CHANGED ||
              cc == TAS_HOLD_COMMIT_SPEED_UNDETERMINED || cc == TAS_HOLD_COMMIT_NOT_CAPTURED ||
              cc == TAS_HOLD_COMMIT_CLEAN_UNDETERMINED ? "INCONCLUSIVE" : "FAIL");
        verdict(tag, "COMMIT", index, id->device_id, reason);

        /* The schedule is committed when exactly one call completed STATUS_SUCCESS with the 144-byte reply; its output fields are
         * COMMIT's to judge, so a wrong output never suppresses the independent STATE, REFUSE and WIRE observations */
        committed = success == 1 && calls[winner].got == sizeof(calls[winner].r);
        if (committed) {
            req_op = calls[winner].sent;
            printf("    [EVIDENCE] the competing request carried the same schedule: after the commit its own hardware effect cannot be "
                   "separated from the winner's; the query after the race is evidence of one schedule, not proof that the refusal wrote "
                   "nothing\n");
            armed_ok = read_state(h, &s);
            if (armed_ok) { print_state("after the activation", &s); }
            verdict(armed_ok && state_matches(&s, base) ? "PASS" : "FAIL", "STATE", index, id->device_id,
                    armed_ok && state_matches(&s, base) ? NULL : "GET_TAS_STATE does not report ARMED with the requested gate list");
            if (!query_valid(&qa)) {
                /* The further request carries the generation and epoch of the test's own query (#369 amendment); after the commit
                 * that is this query, so without it the request has no current metadata and is not sent (PR #481) */
                verdict("BLOCKED", "REFUSE", index, id->device_id, "the query after the commit failed: no current generation and "
                        "epoch for the further request, which is not sent");
            } else {
                /* NOT_CLEAN and STATE_CONFLICT both complete STATUS_INVALID_DEVICE_STATE (#365 13.3, 13.4), so a refusal is attributed
                 * to the armed state only when the request carried reported post-commit metadata (0 = not reported, #365 12.2
                 * offset 88; unknown values are not wildcards, #369). Acceptance, output or a changed schedule fails whatever the
                 * metadata: an armed adapter must refuse and keep its schedule (#369 REFUSE). The request carries the acceptance
                 * flag, so R7's NOT_CLEAN (step 9) is reached after the tier step (8) */
                OP_CALL again;
                BOOL meta_ok = qa.q.generation != 0 && qa.q.context_epoch != 0, refused;
                ZeroMemory(&again, sizeof(again));
                build_op(&again.sent, id->net_luid, &qa.q, base + CYCLE_NS, frame);
                again.sent.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
                again.r = again.sent;
                again.status = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &again.r, sizeof(again.r), &again.r, sizeof(again.r), &again.got);
                printf("    further request: base=%lld flags=0x%X gen=%llu epoch=%llu (from the query after the commit)\n", base + CYCLE_NS,
                       again.sent.flags, qa.q.generation, qa.q.context_epoch);
                print_op("activation on the armed adapter", &again);
                armed_ok = read_state(h, &s);
                refused = again.status == (LONG)STATUS_INVALID_DEVICE_STATE && again.got == 0 && armed_ok && state_matches(&s, base);
                if (refused && meta_ok) {
                    verdict("PASS", "REFUSE", index, id->device_id, NULL);
                } else if (refused) {
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, "reported, not judged: STATUS_INVALID_DEVICE_STATE with no "
                            "output and the schedule kept, but the query after the commit reports generation or epoch 0 (not reported, "
                            "#352 R1), so NOT_CLEAN cannot be told from STATE_CONFLICT");
                } else {
                    verdict("FAIL", "REFUSE", index, id->device_id, "an armed adapter must refuse activation with "
                            "STATUS_INVALID_DEVICE_STATE, no output, and keep the reported schedule");
                }
            }
            query_lookup(calls[winner].h, req_op.client_op_id, &ql);
            printf("    [EVIDENCE] LOOKUP: query on the committing open instance (%s) with client_op_id 0x%016llX: ntstatus=0x%08lX "
                   "information=%lu lookup_result=%u (the producer stage expects NOT_IMPLEMENTED 4; success-record retrieval is #460, "
                   "not asserted here)\n", winner == 0 ? "instance 1" : "instance 2", req_op.client_op_id, (unsigned long)ql.status, ql.got,
                   query_valid(&ql) ? ql.q.lookup_result : 0u);
            query(h, &st_pending);   /* STATUS_PENDING: after the refused requests, which must not cost the retained commit */
            print_status("before BaseT", &st_pending);
        } else {
            const char *no = success != 0 ? "no single complete commit was established (see COMMIT); the adapter may hold a schedule, "
                                            "the owned cleanup follows" : "no committed schedule (see COMMIT)";
            verdict("BLOCKED", "STATE", index, id->device_id, no);
            verdict("BLOCKED", "REFUSE", index, id->device_id, no);
            printf("    [EVIDENCE] LOOKUP: not queried, %s\n", no);
        }
        CloseHandle(h2);

        /* GENERATION across the race (#352 R1 L2/L7/L9), from the queries immediately before and after it */
        switch (gc) {
        case TAS_HOLD_GEN_PASS:
            sprintf_s(reason, sizeof(reason), "generation %llu -> %llu in context %llu: %u transition(s), one per commit and none per "
                      "refusal before any write (#352 R1)", ev.pre_generation, ev.post_generation, ev.pre_epoch, transitions);
            break;
        case TAS_HOLD_GEN_FAIL:
            if (ev.post_generation < ev.pre_generation) {
                sprintf_s(reason, sizeof(reason), "generation %llu -> %llu in context %llu: a decrease (never, #352 R1 L12)",
                          ev.pre_generation, ev.post_generation, ev.pre_epoch);
            } else {
                sprintf_s(reason, sizeof(reason), "generation %llu -> %llu in context %llu, expected +%u: one per write-attempting "
                          "mutation and none per refusal before any write (#352 R1 L2/L7/L9)", ev.pre_generation, ev.post_generation,
                          ev.pre_epoch, transitions);
            }
            break;
        case TAS_HOLD_GEN_PRE_UNREPORTED:
            sprintf_s(reason, sizeof(reason), "not judged: the query before the race %s (0 = not reported, never a value; METADATA "
                      "carries it)", race_valid ? "reports generation or epoch 0" : "is not valid");
            break;
        case TAS_HOLD_GEN_POST_UNREPORTED:
            sprintf_s(reason, sizeof(reason), "not judged: the query after the race %s (0 = not reported, never a value)",
                      ev.post_valid ? "reports generation or epoch 0" : "is not valid");
            break;
        case TAS_HOLD_GEN_EPOCH_CHANGED:
            sprintf_s(reason, sizeof(reason), "not judged: the context epoch changed across the race (%llu -> %llu), another adapter "
                      "context (#352 R1 L11)", ev.pre_epoch, ev.post_epoch);
            break;
        default:
            sprintf_s(reason, sizeof(reason), "not judged: a completion's write attempt is not established (not captured, or a status "
                      "shared by outcomes before and after a write, #365 R5/R8); generation %llu -> %llu", ev.pre_generation,
                      ev.post_generation);
            break;
        }
        verdict(gc == TAS_HOLD_GEN_PASS ? "PASS" : (gc == TAS_HOLD_GEN_FAIL ? "FAIL" : "INCONCLUSIVE"), "GENERATION", index,
                id->device_id, reason);
    }

    if (committed && wire) {
        DWORD start = GetTickCount();
        long long now = 0;
        unsigned n, seq, end;
        char reason[256];

        printf("\n  --- Tagged frames across BaseT ---\n");
        while (GetTickCount() - start < SEND_BOUND_MS && (!read_phc(h, &now) || now < base + SEND_CYCLES_AFTER * CYCLE_NS + CYCLE_NS / 2)) {
            if (send_tagged(sock, nonce) == SEND_FAILED) {
                raw_send_failed++;
            }
            Sleep(SEND_PERIOD_MS);
        }
        Sleep(DRAIN_MS);
        wire_counts(&captured, &from_target);
        anchor_ok = take_anchor(h, &after_anchor, "after");
        wire_stop();
        closesocket(sock);
        sock = INVALID_SOCKET;
        n = collect_frames(first_after, g_next_seq, &before_anchor, &after_anchor, frames);
        /* Raw counts from the capture, independent of the PHC mapping; the cleanup's TX probe reuses the capture arrays */
        end = g_next_seq < WIRE_MAX_FRAMES ? g_next_seq : WIRE_MAX_FRAMES;
        for (seq = first_after; seq < end; seq++) {
            raw_sent++;
            raw_seen += g_wire_seen[seq] != 0;
        }
        mapped = anchor_ok && n == raw_sent;
        wclass = tas_act_wire(AVB_TAS_PRE_HOLD, frames, n, captured != 0, base, CYCLE_NS, WINDOW_START_NS, WINDOW_END_NS, TAS_ACT_EDGE_MARGIN_NS, &wc);
        printf("    wire: %s frames=%u seen=%u early=%u heldBeforeBase=%u flowed=%u released=%u inside=%u nearEdge=%u outside=%u captured=%ld "
               "fromTarget=%ld\n", tas_act_wire_name(wclass), wc.frames, wc.seen, wc.early, wc.held_before_base, wc.flowed, wc.released, wc.inside,
               wc.near_edge, wc.outside, captured, from_target);
        sprintf_s(reason, sizeof(reason), "%s; see the wire line", tas_act_wire_name(wclass));
        verdict(wclass == TAS_ACT_WIRE_GATED ? "PASS" : (wclass == TAS_ACT_WIRE_UNOBSERVED ? "INCONCLUSIVE" : "FAIL"), "WIRE", index, id->device_id,
                wclass == TAS_ACT_WIRE_GATED ? NULL : reason);
    } else {
        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); sock = INVALID_SOCKET; }
        verdict("BLOCKED", "WIRE", index, id->device_id, !committed ? "no committed schedule (see COMMIT)" : "no established wire observer (see OBSERVER)");
    }
    check_status(h, index, id, committed, &req_op, &st_race, &st_commit, &st_pending, &stats, &row);
    restore = cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
    g_cleanup_link_was_up = TRUE;
    if (g_mode == MODE_HOLD && committed && wire) {
        /* HOLD characterization: measurements only, no verdict, not EG-02 qualification. The raw counts come from the capture
         * itself: lost = tagged frames sent after the activation and not seen. The baseline pair is the capture's own loss before
         * any mutation. The timing counts need the PHC mapping; without it they are reported unavailable, never as zeros. Near-edge
         * arrivals are counted apart from inside and outside by the classifier; released is a subset of the first window */
        printf("    [EVIDENCE] HOLD characterization: sent=%u sendFailed=%u seen=%u lost=%u captured=%ld fromTarget=%ld baselineSent=%u "
               "baselineSendFailed=%u baselineSeen=%u restoration=%s\n", raw_sent, raw_send_failed, raw_seen, raw_sent - raw_seen,
               captured, from_target, sent, send_failed, seen,
               restore == TAS_ACT_RESTORE_COMPLETE ? "COMPLETE" : (restore == TAS_ACT_RESTORE_UNVERIFIED ? "UNVERIFIED" : "INCOMPLETE"));
        if (mapped) {
            printf("    [EVIDENCE] HOLD characterization timing (PHC-mapped): wire=%s frames=%u seen=%u early=%u released=%u inside=%u "
                   "nearEdge=%u outside=%u\n", tas_act_wire_name(wclass), wc.frames, wc.seen, wc.early, wc.released, wc.inside,
                   wc.near_edge, wc.outside);
        } else {
            printf("    [EVIDENCE] HOLD characterization timing: unavailable, the PHC mapping failed (after anchor %s, %u of %u frames "
                   "mapped); early, released, inside, nearEdge and outside are not reported. The WIRE verdict above is unchanged\n",
                   anchor_ok ? "read" : "not read", wc.frames, raw_sent);
        }
        printf("    [EVIDENCE] HOLD characterization limits: the driver's register write order is not observable by this test "
               "(#365 13.5 records the source order); capture = passive Npcap, software timestamps on the host clock, through a "
               "switch, mapped to PHC time through the two anchors above (see their bracketUs); arrivals within %lld ms of a window "
               "edge are nearEdge and decide nothing; one frame every %d ms; cycle %lld ms, window %lld..%lld ms. 0 outside does not "
               "by itself prove that no frame left outside a hardware window\n", TAS_ACT_EDGE_MARGIN_NS / NS_PER_MS, SEND_PERIOD_MS,
               CYCLE_NS / NS_PER_MS, WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS);
    }
}

/* --phc-hold-control (#505, TC-TASACT-HOLDCTL-001): the Debug-only one-shot PHC reseed hold control. Independent handles, each
 * bound to the adapter under test: one for control (ARM/QUERY/DISARM), one per PHC reseed (a worker thread), one for ACTIVATE.
 * Each line is its own measurement; a prerequisite that is not established makes the dependent line INCONCLUSIVE, never PASS */
typedef struct {
    avb_u64 luid;
    INT64 offset_ns;
    BOOL opened;
    LONG st;
    avb_u32 status;
    DWORD ms;
} HOLDCTL_PHC_JOB;

static DWORD WINAPI holdctl_phc_worker(LPVOID p)
{
    HOLDCTL_PHC_JOB *job = (HOLDCTL_PHC_JOB *)p;
    AVB_OFFSET_REQUEST off;
    DWORD info = 0, t0;
    HANDLE h = open_bound(GENERIC_READ | GENERIC_WRITE, job->luid);

    job->opened = (h != INVALID_HANDLE_VALUE);
    job->st = (LONG)AVB_NTSTATUS_UNAVAILABLE;
    if (!job->opened) {
        return 0;
    }
    ZeroMemory(&off, sizeof(off));
    off.offset_ns = job->offset_ns;
    t0 = GetTickCount();
    job->st = avb_ioctl_ntstatus(h, IOCTL_AVB_PHC_OFFSET_ADJUST, &off, sizeof(off), &off, sizeof(off), &info);
    job->ms = GetTickCount() - t0;
    job->status = off.status;
    CloseHandle(h);
    return 0;
}

#ifndef NDEBUG
static LONG holdctl(HANDLE h, avb_u32 action, avb_u32 hold_ms, avb_u32 timeout_ms, AVB_DEBUG_PHC_HOLD_REQUEST *out, DWORD *info)
{
    LONG st;

    ZeroMemory(out, sizeof(*out));
    out->version = AVB_DEBUG_PHC_HOLD_VERSION;
    out->action = action;
    out->hold_ms = hold_ms;
    out->arm_timeout_ms = timeout_ms;
    *info = 0;
    st = avb_ioctl_ntstatus(h, IOCTL_AVB_DEBUG_PHC_RESEED_HOLD, out, sizeof(*out), out, sizeof(*out), info);
    return st;
}

static void holdctl_print(const char *label, LONG st, DWORD info, const AVB_DEBUG_PHC_HOLD_REQUEST *r)
{
    printf("    [EVIDENCE] %s: ntstatus=0x%08lX information=%lu", label, (unsigned long)st, (unsigned long)info);
    if (st == 0) {
        printf(" arm=%llu state=%u holds=%u src=%u rc=%d luid=0x%016llX epoch=%llu seq armed=%llu hold=%llu..%llu write=%llu..%llu "
               "act=%llu/%llu op=0x%016llX res=0x%X elapsed_us=%u", r->arm_id, r->state, r->holds_completed, r->phc_source, r->phc_rc,
               r->net_luid, r->context_epoch, r->armed_seq, r->hold_begin_seq, r->hold_end_seq, r->phc_write_enter_seq,
               r->phc_write_done_seq, r->activate_arrival_seq, r->activate_commit_seq, r->activate_op_id, r->activate_result,
               r->hold_elapsed_us);
    }
    printf("\n");
}

/* Test-owned restoration evidence: the version-1 register snapshot of the bound adapter (#456) and a PHC/QPC pair. The PHC is
 * a running clock, so its restoration is judged as continuity against wall time, never from the sum of the steps sent */
#define HOLDCTL_PHC_TOLERANCE_NS (1000000LL)   /* 1 ms: detects a lost step back or a reseed, not residual offsets of a few us */

typedef struct {
    BOOL snap_ok;
    AVB_HW_SNAPSHOT_REQUEST snap;
    BOOL phc_ok;
    long long phc;
    LARGE_INTEGER qpc;
    BOOL phc_running;   /* pre-state only: the PHC advanced by more than half the wall time across a 50 ms probe */
} HOLDCTL_STATE;

static void holdctl_capture(HANDLE h, HOLDCTL_STATE *s, BOOL probe_rate)
{
    DWORD info = 0;
    LONG st;
    long long phc2 = 0;

    ZeroMemory(s, sizeof(*s));
    s->snap.version = AVB_HW_SNAPSHOT_VERSION;
    st = avb_ioctl_ntstatus(h, IOCTL_AVB_QUERY_HW_SNAPSHOT, &s->snap, sizeof(s->snap), &s->snap, sizeof(s->snap), &info);
    s->snap_ok = (st == 0 && info == sizeof(s->snap) && s->snap.status == 0 && s->snap.count == AVB_HWREG_COUNT);
    s->phc_ok = read_phc(h, &s->phc);
    QueryPerformanceCounter(&s->qpc);
    if (probe_rate && s->phc_ok) {
        Sleep(50);
        s->phc_running = read_phc(h, &phc2) && (phc2 - s->phc) > 25LL * NS_PER_MS;
    }
}

/* Every snapshot register id is accounted for, none skipped silently: VALID on both sides is compared (a different value is
 * a difference); the same not-applicable state on both sides (NOT_IN_SSOT, NO_HAL, NO_MMIO) is not applicable; READ_FAILED on
 * either side or a state that changed between before and after is unmeasured. Each difference and each unmeasured id is
 * printed. Returns the number of differences; -1 when a snapshot itself is missing (everything unmeasured) */
static int holdctl_regs_differing(const HOLDCTL_STATE *a, const HOLDCTL_STATE *b, unsigned *compared, unsigned *not_applicable,
                                  unsigned *unmeasured)
{
    unsigned i;
    int differing = 0;

    *compared = 0;
    *not_applicable = 0;
    *unmeasured = 0;
    if (!a->snap_ok || !b->snap_ok) {
        *unmeasured = AVB_HWREG_COUNT;
        return -1;
    }
    for (i = 0; i < AVB_HWREG_COUNT; i++) {
        const AVB_HWREG_ENTRY *x = &a->snap.entries[i], *y = &b->snap.entries[i];
        if (x->state == AVB_HWREG_STATE_VALID && y->state == AVB_HWREG_STATE_VALID) {
            (*compared)++;
            if (x->value != y->value) {
                differing++;
                printf("    [EVIDENCE] register id %u (offset 0x%05X) before=0x%08X after=0x%08X\n", x->id, x->offset, x->value, y->value);
            }
        } else if (x->state == y->state && x->state != AVB_HWREG_STATE_READ_FAILED) {
            (*not_applicable)++;
        } else {
            (*unmeasured)++;
            printf("    [EVIDENCE] register id %u unmeasured: state before %u, after %u\n", x->id, x->state, y->state);
        }
    }
    return differing;
}

/* Diagnostic anchors (#505 I226 drift plan, Q 2026-11-22): evidence only, never a verdict. Each anchor reads the PHC three times
 * through GET_CLOCK_CONFIG and three times through GET_TIMESTAMP (both read-only, routed to the handle's bound adapter), each
 * read bracketed by QPC. A failed read, a wrong length or a non-success status is never the anchor's measurement. Neither
 * reply reports a software fallback, so a fallback is not detectable here (stated in the output). The extra reads change the
 * test's timing. */
#define HOLDCTL_DIAG_READS   3
#define HOLDCTL_DIAG_ANCHORS 16

typedef struct {
    LONG st;
    DWORD info;
    avb_u32 status;
    long long phc;
    LONGLONG q0, q1;   /* QPC ticks before and after the IOCTL */
} HOLDCTL_DIAG_READ;

typedef struct {
    const char *label;
    BOOL valid;          /* a usable GET_CLOCK_CONFIG read exists */
    double mid_ns;       /* best GET_CLOCK_CONFIG bracket centre, ns since the first anchor's first read */
    double half_ns;      /* half its QPC bracket width */
    long long phc;       /* its PHC */
} HOLDCTL_DIAG_ANCHOR;

typedef struct {
    HOLDCTL_DIAG_ANCHOR a[HOLDCTL_DIAG_ANCHORS];
    int n;
    LONGLONG base, freq;
    INT64 pending_ns;    /* requested offsets of the steps that completed STATUS_SUCCESS since the previous anchor */
    int pending_steps;   /* steps (any outcome) since the previous anchor */
} HOLDCTL_DIAG;

static double holdctl_diag_ns(const HOLDCTL_DIAG *d, LONGLONG q)
{
    return (double)(q - d->base) * 1e9 / (double)d->freq;
}

/* Records that one PHC step happened since the previous anchor; its offset counts only when it completed STATUS_SUCCESS */
static void holdctl_diag_step(HOLDCTL_DIAG *d, INT64 offset_ns, BOOL ok)
{
    d->pending_steps++;
    if (ok) {
        d->pending_ns += offset_ns;
    }
}

static void holdctl_diag_anchor(HOLDCTL_DIAG *d, HANDLE h, const AVB_ADAPTER_IDENTITY_REQUEST *id, const char *label)
{
    HOLDCTL_DIAG_READ cc[HOLDCTL_DIAG_READS], ts[HOLDCTL_DIAG_READS];
    LARGE_INTEGER t;
    int i, bc = -1, bt = -1;
    HOLDCTL_DIAG_ANCHOR *a;

    if (d->n >= HOLDCTL_DIAG_ANCHORS) {
        printf("    [DIAG] anchor '%s' not taken: anchor table full\n", label);
        return;
    }
    if (d->n == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        d->freq = f.QuadPart;
        QueryPerformanceCounter(&t);
        d->base = t.QuadPart;
    }
    for (i = 0; i < HOLDCTL_DIAG_READS; i++) {
        AVB_CLOCK_CONFIG c;
        AVB_TIMESTAMP_REQUEST r;
        ZeroMemory(&c, sizeof(c));
        QueryPerformanceCounter(&t);
        cc[i].q0 = t.QuadPart;
        cc[i].st = avb_ioctl_ntstatus(h, IOCTL_AVB_GET_CLOCK_CONFIG, &c, sizeof(c), &c, sizeof(c), &cc[i].info);
        QueryPerformanceCounter(&t);
        cc[i].q1 = t.QuadPart;
        cc[i].status = c.status;
        cc[i].phc = (long long)c.systim;
        ZeroMemory(&r, sizeof(r));
        QueryPerformanceCounter(&t);
        ts[i].q0 = t.QuadPart;
        ts[i].st = avb_ioctl_ntstatus(h, IOCTL_AVB_GET_TIMESTAMP, &r, sizeof(r), &r, sizeof(r), &ts[i].info);
        QueryPerformanceCounter(&t);
        ts[i].q1 = t.QuadPart;
        ts[i].status = r.status;
        ts[i].phc = (long long)r.timestamp;
    }
    for (i = 0; i < HOLDCTL_DIAG_READS; i++) {
        BOOL okc = cc[i].st == 0 && cc[i].info == sizeof(AVB_CLOCK_CONFIG) && cc[i].status == 0;
        BOOL okt = ts[i].st == 0 && ts[i].info == sizeof(AVB_TIMESTAMP_REQUEST) && ts[i].status == 0;
        printf("    [DIAG] A%d '%s' did=0x%04X luid=0x%016llX CLOCK_CONFIG#%d qpc=[%.0f,%.0f]ns width=%.0fns phc=%lld st=0x%08lX info=%lu status=0x%X%s\n",
               d->n, label, id->device_id, id->net_luid, i, holdctl_diag_ns(d, cc[i].q0), holdctl_diag_ns(d, cc[i].q1),
               holdctl_diag_ns(d, cc[i].q1) - holdctl_diag_ns(d, cc[i].q0), cc[i].phc, (unsigned long)cc[i].st, (unsigned long)cc[i].info,
               cc[i].status, okc ? "" : " EXCLUDED");
        printf("    [DIAG] A%d '%s' did=0x%04X luid=0x%016llX TIMESTAMP#%d    qpc=[%.0f,%.0f]ns width=%.0fns phc=%lld st=0x%08lX info=%lu status=0x%X%s\n",
               d->n, label, id->device_id, id->net_luid, i, holdctl_diag_ns(d, ts[i].q0), holdctl_diag_ns(d, ts[i].q1),
               holdctl_diag_ns(d, ts[i].q1) - holdctl_diag_ns(d, ts[i].q0), ts[i].phc, (unsigned long)ts[i].st, (unsigned long)ts[i].info,
               ts[i].status, okt ? "" : " EXCLUDED");
        if (okc && (bc < 0 || (cc[i].q1 - cc[i].q0) < (cc[bc].q1 - cc[bc].q0))) {
            bc = i;
        }
        if (okt && (bt < 0 || (ts[i].q1 - ts[i].q0) < (ts[bt].q1 - ts[bt].q0))) {
            bt = i;
        }
    }
    a = &d->a[d->n];
    a->label = label;
    a->valid = (bc >= 0);
    if (a->valid) {
        a->mid_ns = (holdctl_diag_ns(d, cc[bc].q0) + holdctl_diag_ns(d, cc[bc].q1)) / 2.0;
        a->half_ns = (holdctl_diag_ns(d, cc[bc].q1) - holdctl_diag_ns(d, cc[bc].q0)) / 2.0;
        a->phc = cc[bc].phc;
        printf("    [DIAG] A%d '%s' best CLOCK_CONFIG#%d mid=%.0fns +-%.0fns phc=%lld\n", d->n, label, bc, a->mid_ns, a->half_ns, a->phc);
        if (bt >= 0) {
            double tmid = (holdctl_diag_ns(d, ts[bt].q0) + holdctl_diag_ns(d, ts[bt].q1)) / 2.0;
            double thalf = (holdctl_diag_ns(d, ts[bt].q1) - holdctl_diag_ns(d, ts[bt].q0)) / 2.0;
            /* the two paths are read one after the other: compare after removing the read-time difference */
            printf("    [DIAG] A%d '%s' TIMESTAMP#%d minus CLOCK_CONFIG#%d = %lld ns raw, %.0f ns after removing the %.0f ns read-time difference, "
                   "+-%.0f ns\n", d->n, label, bt, bc, ts[bt].phc - a->phc, (double)(ts[bt].phc - a->phc) - (tmid - a->mid_ns),
                   tmid - a->mid_ns, a->half_ns + thalf);
        } else {
            printf("    [DIAG] A%d '%s' no usable TIMESTAMP read: path comparison unmeasured\n", d->n, label);
        }
    } else {
        printf("    [DIAG] A%d '%s' no usable CLOCK_CONFIG read: this anchor is unmeasured\n", d->n, label);
    }
    if (d->n > 0) {
        const HOLDCTL_DIAG_ANCHOR *p = &d->a[d->n - 1];
        if (p->valid && a->valid) {
            double dwall = a->mid_ns - p->mid_ns;
            double dphc = (double)(a->phc - p->phc);
            double resid = dphc - dwall - (double)d->pending_ns;
            printf("    [DIAG] interval A%d->A%d %s (%d step(s), counted offset %lld ns): dWall=%.0fns dPHC=%.0fns residual=%.0fns +-%.0fns "
                   "(%.1f ppm of dWall)\n", d->n - 1, d->n, d->pending_steps ? "STEP" : "FREE", d->pending_steps, (long long)d->pending_ns,
                   dwall, dphc, resid, p->half_ns + a->half_ns, dwall > 0 ? resid * 1e6 / dwall : 0.0);
        } else {
            printf("    [DIAG] interval A%d->A%d %s: unmeasured (an anchor has no usable read)\n", d->n - 1, d->n, d->pending_steps ? "STEP" : "FREE");
        }
    }
    d->pending_ns = 0;
    d->pending_steps = 0;
    d->n++;
}

/* One synchronous PHC offset step on its own bound handle; returns TRUE when it completed STATUS_SUCCESS */
static BOOL holdctl_step(avb_u64 luid, INT64 ns, HOLDCTL_PHC_JOB *job)
{
    ZeroMemory(job, sizeof(*job));
    job->luid = luid;
    job->offset_ns = ns;
    (void)holdctl_phc_worker(job);
    printf("    [EVIDENCE] PHC_OFFSET_ADJUST %+lld ns: opened=%d ntstatus=0x%08lX status=0x%X %lu ms\n", (long long)ns, job->opened,
           (unsigned long)job->st, job->status, (unsigned long)job->ms);
    return job->opened && job->st == 0;
}

#define HOLDCTL_WORKER_MS   10000u   /* the held reseed: the driver holds at most 2000 ms */
#define HOLDCTL_SETTLE_MS   30000u   /* further bounded wait for a reseed that has not completed by then */

/* TRUE while a PHC_OFFSET_ADJUST this test sent has not been observed to complete. A completion established here is collected: its
 * real result is printed and a completed step is added to *net_ns, so the cleanup compensates it */
static BOOL holdctl_outstanding(OWNED_WORKER *w, DWORD wait_ms, INT64 *net_ns)
{
    const HOLDCTL_PHC_JOB *job;

    if (w->state != OWNED_WORKER_RUNNING) {
        return FALSE;
    }
    if (owned_worker_wait(w, wait_ms) == OWNED_WORKER_RUNNING) {
        return TRUE;
    }
    job = (const HOLDCTL_PHC_JOB *)owned_worker_result(w);
    printf("    [EVIDENCE] late completion of the held PHC_OFFSET_ADJUST %+lld ns: opened=%d ntstatus=0x%08lX status=0x%X %lu ms\n",
           (long long)job->offset_ns, job->opened, (unsigned long)job->st, job->status, (unsigned long)job->ms);
    if (job->opened && job->st == 0) {
        *net_ns += job->offset_ns;
    }
    return FALSE;
}
#endif

/* ---- --metadata-events, TC-TASACT-META-002 and -003: ACTIVATE commits while a PHC reseed is held (#357 F1, #505 control) ---- */
#ifndef NDEBUG
#define META_RACE_ARM_MS      5000u
#define META_RACE_HOLDING_MS  3000u    /* bound for observing HOLDING after the reseed was started */
#define META_RACE_WORKER_MS   10000u   /* each operation: the driver holds at most 2000 ms */

typedef struct {
    HANDLE h;
    AVB_TAS_OP_REQUEST op;
    LONG st;
    DWORD information;
} META_RACE_ACT;

static DWORD WINAPI meta_race_activate(LPVOID p)
{
    META_RACE_ACT *a = (META_RACE_ACT *)p;

    a->st = avb_ioctl_ntstatus(a->h, IOCTL_AVB_TAS_ACTIVATE, &a->op, sizeof(a->op), &a->op, sizeof(a->op), &a->information);
    CloseHandle(a->h);
    return 0;
}
#endif

/** The race was not started: every line of the case is INCONCLUSIVE with the reason, never PASS */
static void meta_race_not_run(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, BOOL back, const char *reason)
{
    verdict("INCONCLUSIVE", "WINDOW", index, id->device_id, reason);
    verdict("INCONCLUSIVE", "GENERATION", index, id->device_id, reason);
    verdict("INCONCLUSIVE", "STATUS_OFFSET", index, id->device_id, reason);
    if (back) {
        verdict("INCONCLUSIVE", "NEW_PHC", index, id->device_id, reason);
    }
}

/** h: bound control and query handle; ha: bound handle of the ACTIVATE, closed here. Every mutation is undone by the caller's
 *  owned NIC cycle. Each line is its own measurement; what was not established is INCONCLUSIVE, never PASS. back: META-003 */
static void run_meta_race(HANDLE h, HANDLE ha, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u32 frame, BOOL back)
{
#ifdef NDEBUG
    (void)h; (void)frame;
    CloseHandle(ha);
    meta_race_not_run(index, id, back, "Release build: the #505 hold control is Debug-only, not exercised (never PASS)");
#else
    IOCTL_VERSION ver = {0};
    AVB_DEBUG_PHC_HOLD_REQUEST armed, r, d;
    HOLDCTL_PHC_JOB job, reseed;
    META_RACE_ACT act, done;
    OWNED_OP *wp, *wa;   /* registered: cleanup_and_verify waits for them itself and never judges a final state while one is open */
    QUERY_READ q0, q1, qb, qs;
    AVB_OFFSET_REQUEST off;
    DWORD br = 0, info = 0, t0;
    LONG st, stq;
    const INT64 step_ns = back ? -(INT64)META_BACK_NS : (INT64)META_OFFSET_NS;
    const long long status_step = back ? -META_STATUS_STEP_NS : META_STATUS_STEP_NS;
    long long w0, w1, w2, w3;
    BOOL stepped;
    long long phc = 0, phc_after = 0, base;
    BOOL holding = FALSE, sent = FALSE, held, settled, real, phc_read;
    TAS_RACE_MARKERS m;
    TAS_RACE_ORDER order;
    unsigned polls = 0;
    char why[700];

    wp = owned_op_new(&g_ops, id->net_luid);
    wa = owned_op_new(&g_ops, id->net_luid);
    if (wp == NULL || wa == NULL) {
        CloseHandle(ha);
        meta_race_not_run(index, id, back, "no memory to own the operations of the race: not started");
        return;
    }
    if (!DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &ver, sizeof(ver), &ver, sizeof(ver), &br, NULL) || br < sizeof(ver) ||
        !(ver.Flags & AVB_VERSION_FLAG_DEBUG_BUILD)) {
        CloseHandle(ha);
        meta_race_not_run(index, id, back, "no Debug driver (GET_VERSION): the hold control is not served, not exercised (never PASS)");
        return;
    }
    query(h, &q0);
    print_query("before the race", &q0);
    if (!query_valid(&q0) || q0.q.generation == 0 || q0.q.context_epoch == 0 || !read_phc(h, &phc)) {
        CloseHandle(ha);
        meta_race_not_run(index, id, back, "no valid non-zero generation/epoch pair or no PHC: the race request cannot be built");
        return;
    }
    /* BaseT leaves the whole hold plus the usual lead, so an ACTIVATE that has to wait for the reseed still has its margin */
    base = (phc + BASE_LEAD_NS + (long long)META_RACE_HOLD_MS * NS_PER_MS + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS;
    if (back) {   /* META-003: the last cycle boundary the old PHC has passed; seconds ahead once the PHC is stepped back */
        base = phc / CYCLE_NS * CYCLE_NS;
        if (phc < 2 * (long long)META_BACK_NS) {
            CloseHandle(ha);
            meta_race_not_run(index, id, back, "the PHC is too small to be stepped back by the test's 5 s: not exercised");
            return;
        }
    }
    ZeroMemory(&act, sizeof(act));
    act.h = ha;
    act.st = (LONG)AVB_NTSTATUS_UNAVAILABLE;
    build_op(&act.op, id->net_luid, &q0.q, base, frame);
    act.op.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
    done = act;
    st = holdctl(h, AVB_DEBUG_PHC_HOLD_ARM, META_RACE_HOLD_MS, META_RACE_ARM_MS, &armed, &info);
    holdctl_print("ARM", st, info, &armed);
    if (st != 0 || armed.state != AVB_DEBUG_PHC_HOLD_ARMED || armed.arm_id == 0) {
        CloseHandle(ha);
        sprintf_s(why, sizeof(why), "ARM completed 0x%08lX: the reseed cannot be held, the race was not started", (unsigned long)st);
        meta_race_not_run(index, id, back, why);
        return;
    }
    ZeroMemory(&job, sizeof(job));
    ZeroMemory(&reseed, sizeof(reseed));
    job.luid = id->net_luid;
    job.offset_ns = step_ns;
    reseed.st = (LONG)AVB_NTSTATUS_UNAVAILABLE;
    t0 = GetTickCount();
    held = owned_worker_start(&wp->worker, holdctl_phc_worker, &job, sizeof(job));
    while (held && !holding && GetTickCount() - t0 < META_RACE_HOLDING_MS) {
        stq = holdctl(h, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &r, &info);
        polls++;
        holding = stq == 0 && r.arm_id == armed.arm_id && r.state == AVB_DEBUG_PHC_HOLD_HOLDING;
        if (!holding) { Sleep(1); }
    }
    if (holding) {
        sent = owned_worker_start(&wa->worker, meta_race_activate, &act, sizeof(act));   /* arrives while the reseed is held */
    }
    if (!sent) {
        CloseHandle(ha);
    }
    (void)holdctl(h, AVB_DEBUG_PHC_HOLD_DISARM, 0, 0, &d, &info);   /* an unused arm only; no-op while HOLDING */
    (void)owned_worker_wait(&wa->worker, META_RACE_WORKER_MS);
    (void)owned_worker_wait(&wp->worker, META_RACE_WORKER_MS);
    settled = wp->worker.state != OWNED_WORKER_RUNNING && wa->worker.state != OWNED_WORKER_RUNNING;
    if (wp->worker.state == OWNED_WORKER_DONE) { reseed = *(const HOLDCTL_PHC_JOB *)owned_worker_result(&wp->worker); }
    if (wa->worker.state == OWNED_WORKER_DONE) { done = *(const META_RACE_ACT *)owned_worker_result(&wa->worker); }
    printf("    [EVIDENCE] HOLDING observed=%d after %u polls; PHC_OFFSET_ADJUST %+lld ns: started=%d ended=%d ntstatus=0x%08lX status=0x%X "
           "%lu ms; ACTIVATE op=0x%016llX gen=%llu epoch=%llu base=%lld (PHC %lld): sent=%d ended=%d ntstatus=0x%08lX information=%lu\n",
           holding, polls, (long long)step_ns, held, wp->worker.state == OWNED_WORKER_DONE, (unsigned long)reseed.st, reseed.status,
           (unsigned long)reseed.ms, act.op.client_op_id, act.op.expected_generation, act.op.context_epoch, base, phc, sent,
           wa->worker.state == OWNED_WORKER_DONE, (unsigned long)done.st, (unsigned long)done.information);
    stq = holdctl(h, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &r, &info);
    holdctl_print("QUERY after the race", stq, info, &r);
    query(h, &q1);
    print_query("after the race", &q1);

    phc_read = read_phc(h, &phc_after);

    /* Both operations are this test's own and really took effect; the order then comes from the markers alone */
    real = settled && sent && stq == 0 && r.arm_id == armed.arm_id && r.net_luid == id->net_luid && r.context_epoch == q0.q.context_epoch &&
           r.state == AVB_DEBUG_PHC_HOLD_RELEASED && r.phc_source == AVB_DEBUG_PHC_HOLD_SRC_PHC_OFFSET_ADJUST && reseed.opened &&
           reseed.st == 0 && r.phc_rc == 0 && r.activate_op_id == act.op.client_op_id && r.activate_result == AVB_TAS_ACT_COMMITTED &&
           done.st == 0 && done.information == (DWORD)sizeof(done.op);
    m.hold_begin = r.hold_begin_seq;
    m.hold_end = r.hold_end_seq;
    m.arrival = r.activate_arrival_seq;
    m.commit = r.activate_commit_seq;
    m.write_enter = r.phc_write_enter_seq;
    m.write_done = r.phc_write_done_seq;
    order = real ? tas_race_order(&m) : TAS_RACE_NOT_ESTABLISHED;
    sprintf_s(why, sizeof(why), "markers: hold %llu..%llu, ACTIVATE arrival %llu, commit %llu, PHC write %llu..%llu; reseed 0x%08lX rc %d, ACTIVATE "
              "0x%08lX result %u, operations ended %d: %s", m.hold_begin, m.hold_end, m.arrival, m.commit, m.write_enter, m.write_done,
              (unsigned long)reseed.st, r.phc_rc, (unsigned long)done.st, r.activate_result, settled,
              order == TAS_RACE_COMMIT_FIRST ? "the commit precedes the held PHC write" :
              (order == TAS_RACE_WRITE_FIRST ? "the held PHC write precedes the commit" : "no order of both operations established (not judged)"));
    verdict(order != TAS_RACE_NOT_ESTABLISHED ? "PASS" : "INCONCLUSIVE", "WINDOW", index, id->device_id, why);
    if (order == TAS_RACE_NOT_ESTABLISHED) {
        sprintf_s(why, sizeof(why), "no established order (see WINDOW): the required step is unknown (generation %llu -> %llu printed)",
                  q0.q.generation, q1.q.generation);
        verdict("INCONCLUSIVE", "GENERATION", index, id->device_id, why);
    } else {
        meta_line("GENERATION", index, id->device_id, &q0, &q1, tas_race_required_step(order), order == TAS_RACE_COMMIT_FIRST ?
                  "commit, then the held PHC reseed on the retained schedule (L7 +1, L10 +1)" : "PHC reseed on the CLEAN record, then the commit (L7 +1)");
    }
    if (back) {
        const TAS_RACE_PHC used = phc_read ? tas_race_phc_used(order, base, phc, phc_after) : TAS_RACE_PHC_NOT_SHOWN;
        sprintf_s(why, sizeof(why), "BaseT %lld, old PHC %lld (BaseT passed on it), PHC after the step %s%lld: %s", base, phc, phc_read ? "" : "unread ",
                  phc_after, used == TAS_RACE_PHC_NEW ? "committed after the PHC write, so judged by the new PHC" :
                  (used == TAS_RACE_PHC_PASSED_BASE ? "committed before the PHC write although BaseT had passed" :
                  "the ACTIVATE did not commit after the PHC write: which PHC it was judged by is not shown"));
        verdict(used == TAS_RACE_PHC_NEW ? "PASS" : (used == TAS_RACE_PHC_PASSED_BASE ? "FAIL" : "INCONCLUSIVE"), "NEW_PHC", index, id->device_id, why);
    }
    if (settled) {
        verdict("PASS", "SETTLED", index, id->device_id, "the PHC reseed and the ACTIVATE ended within their bounds");
    } else {
        verdict("INCONCLUSIVE", "SETTLED", index, id->device_id, "the PHC reseed or the ACTIVATE did not end within its bound: the race is not judged, "
                "and the cleanup decides whether a final state can be established");
    }

    /* STATUS_OFFSET: a measurable PHC_OFFSET_ADJUST (the one-shot hold is spent) on the commit of this case, which no event has hit yet */
    if (order != TAS_RACE_WRITE_FIRST) {
        verdict("INCONCLUSIVE", "STATUS_OFFSET", index, id->device_id, "no commit of this case that the held reseed did not hit (see WINDOW): the step is not sent");
    } else {
        w0 = wall_now_ns();
        query(h, &qb);
        w1 = wall_now_ns();
        ZeroMemory(&off, sizeof(off));
        off.offset_ns = status_step;
        info = 0;
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_PHC_OFFSET_ADJUST, &off, sizeof(off), &off, sizeof(off), &info);
        w2 = wall_now_ns();
        query(h, &qs);
        w3 = wall_now_ns();
        print_query("before the status step", &qb);
        print_query("after the status step", &qs);
        stepped = tas_phc_step(status_step, query_valid(&qb), &qb.q, query_valid(&qs), &qs.q, w2 - w1, w3 - w0);
        printf("    [EVIDENCE] STATUS_OFFSET: PHC_OFFSET_ADJUST %+lld ns: ntstatus=0x%08lX information=%lu status=0x%X; PHC %llu -> %llu over %lld to "
               "%lld ns of wall time: the step is %s\n", status_step, (unsigned long)st, info, off.status, qb.q.phc_after_ns,
               qs.q.phc_before_ns, w2 - w1, w3 - w0, stepped ? "measured" : "NOT measured");
        meta_reseed_status("STATUS_OFFSET", index, id->device_id, &qb, &qs, &act.op, st == 0 && stepped,
                           back ? "PHC_OFFSET_ADJUST -100 ms" : "PHC_OFFSET_ADJUST +100 ms");
    }
#endif
}

/* ---- --metadata-events, TC-TASACT-META-004: a FilterRestart with a retained schedule (#357, #352 R1 L10; trigger and lookup of #364) ---- */
#define META_RESTART_LIST_BYTES  16384
#define META_RESTART_NAME_CHARS  160
#define META_RESTART_QUIET_MS    10000u   /* looking for a moment in the open window with no outstanding send; then it is sent anyway */
#define META_RESTART_BOUND_MS    60000u   /* Pause and Restart entered; then hw_read READ again: a stop condition, no criterion */
#define META_RESTART_IOCTL_MS    5000u    /* the trigger only schedules, so it returns at once (#364) */
#define META_STALE_ARM_MS        5000u

typedef struct {
    avb_u64 luid;
    WCHAR name[META_RESTART_NAME_CHARS];
    USHORT bytes;
    BOOL opened;
    LONG st;
    DWORD information;
} META_RESTART_JOB;

static DWORD WINAPI meta_restart_worker(LPVOID p)
{
    META_RESTART_JOB *job = (META_RESTART_JOB *)p;
    HANDLE h = open_bound(GENERIC_READ | GENERIC_WRITE, job->luid);

    job->st = (LONG)AVB_NTSTATUS_UNAVAILABLE;
    job->opened = h != INVALID_HANDLE_VALUE;
    if (job->opened) {
        job->st = avb_ioctl_ntstatus(h, IOCTL_FILTER_RESTART_ONE_INSTANCE, job->name, job->bytes, NULL, 0, &job->information);
        CloseHandle(h);
    }
    return 0;
}

/** The one filter instance that carries this adapter's interface GUID (#364); every instance is printed */
static TAS_INSTANCE_RESULT meta_find_instance(HANDLE h, avb_u64 luid, META_RESTART_JOB *job)
{
    static BYTE list[META_RESTART_LIST_BYTES];
    const WCHAR *name = NULL;
    WCHAR guid[40];
    NET_LUID l;
    GUID g;
    USHORT bytes = 0, n;
    DWORD br = 0, at;
    TAS_INSTANCE_RESULT r;

    l.Value = luid;
    if (ConvertInterfaceLuidToGuid(&l, &g) != NO_ERROR) {
        return TAS_INSTANCE_NONE;
    }
    swprintf_s(guid, 40, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
               g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    if (!DeviceIoControl(h, IOCTL_FILTER_ENUERATE_ALL_INSTANCES, NULL, 0, list, sizeof(list), &br, NULL)) {
        printf("    [EVIDENCE] filter instance list unreadable (error %lu)\n", GetLastError());
        return TAS_INSTANCE_MALFORMED;
    }
    for (at = 0; at + sizeof(USHORT) <= br; at += n) {
        memcpy(&n, list + at, sizeof(n));
        at += sizeof(USHORT);
        if (n > br - at) {
            break;
        }
        printf("    [EVIDENCE] filter instance %.*ls\n", (int)(n / sizeof(WCHAR)), (const WCHAR *)(list + at));
    }
    r = tas_instance_find(list, br, guid, &name, &bytes);
    if (r == TAS_INSTANCE_FOUND && bytes > sizeof(job->name)) {
        r = TAS_INSTANCE_MALFORMED;
    }
    if (r == TAS_INSTANCE_FOUND) {
        memcpy(job->name, name, bytes);
        job->bytes = bytes;
    }
    printf("    [EVIDENCE] adapter GUID %ls: lookup %d (0 = exactly one instance), %u bytes\n", guid, r, bytes);
    return r;
}

/** Two successive PHC reads: a running clock rises between them */
static void meta_phc_reads(HANDLE h, const char *label, TAS_PHC_READS *r)
{
    ZeroMemory(r, sizeof(*r));
    r->read = read_phc(h, &r->phc[0]);
    Sleep(2);
    r->read = read_phc(h, &r->phc[1]) && r->read;
    printf("    [EVIDENCE] PHC %s: read=%d %lld, %lld\n", label, r->read, r->phc[0], r->phc[1]);
}

static BOOL meta_same_context(const QUERY_READ *a, const QUERY_READ *b)
{
    return query_valid(a) && query_valid(b) && a->q.context_epoch != 0 && a->q.context_epoch == b->q.context_epoch;
}

/** h: control and measurement; h2: the second bound instance, closed here. Returns the epoch of the context the case ran in, 0 when none was
 *  measured. Every state change is undone by the caller's owned NIC cycle */
static avb_u64 run_meta_restart(HANDLE h, HANDLE h2, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u32 frame)
{
    AVB_TAS_OP_REQUEST op;
    AVB_TAS_STATE s;
    AVB_HW_STATE_QUERY hw;
    AVB_DEVICE_INFO_REQUEST init;
    AVB_DRIVER_STATISTICS before, now;
    TAS_LIFECYCLE_EVIDENCE le;
    TAS_LIFECYCLE_OUTCOME outcome = TAS_LIFECYCLE_NOT_TRIGGERED;
    META_RESTART_JOB job, *done;
    TAS_PHC_READS p0, p1;
    TAS_PHC_RESET reset;
    QUERY_READ q0, qa, qi, qt, qe, qx, qy;
    OWNED_OP *trigger;
    DWORD information = 0, br = 0, t0;
    LONG st;
    long long phc = 0, base;
    BOOL record, retained, quiet = FALSE, entered, completed, event, readable = FALSE;
    AVB_DRIVER_STATISTICS sp, sz;
    BOOL read_sp = FALSE, read_sz = FALSE, paused, status_quiet;
    TAS_EV_OUT ctl, pause_out, rec;
    TAS_EV_COUNT cnt;
    TAS_RESTART_STAGE stage;
    unsigned polls;
    char why[700];

    /* COMMIT: the retained schedule, its epoch and its generation, measured */
    query(h, &q0);
    print_query("before the commit", &q0);
    retained = FALSE;
    ZeroMemory(&s, sizeof(s));
    ZeroMemory(&op, sizeof(op));
    st = (LONG)AVB_NTSTATUS_UNAVAILABLE;
    if (query_valid(&q0) && read_phc(h, &phc)) {
        base = (phc + BASE_LEAD_NS + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS;
        build_op(&op, id->net_luid, &q0.q, base, frame);
        op.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE, &op, sizeof(op), &op, sizeof(op), &information);
    } else {
        base = 0;
    }
    record = read_state(h, &s);
    query(h, &qa);
    print_query("after the commit", &qa);
    retained = st == 0 && information == (DWORD)sizeof(op) && record && s.armed == AVB_TAS_STATE_ARMED && meta_same_context(&q0, &qa) &&
               qa.q.generation != 0;
    sprintf_s(why, sizeof(why), "ACTIVATE op=0x%016llX base=%lld ntstatus=0x%08lX information=%lu, record %s%d, pair epoch %llu generation %llu -> %llu",
              op.client_op_id, base, (unsigned long)st, (unsigned long)information, record ? "" : "unread ", record ? (int)s.armed : -1,
              qa.q.context_epoch, q0.q.generation, qa.q.generation);
    verdict(retained ? "PASS" : "FAIL", "COMMIT", index, id->device_id, why);
    if (!retained) {
        static const char *const lines[] = { "INIT_READY", "CONTROL_BEFORE", "RESTART", "RESEED", "GENERATION", "STATUS_RESTART", "RECORD_RESTART",
                                             "ONCE", "STALE_VIEW" };
        unsigned k;

        for (k = 0; k < sizeof(lines) / sizeof(lines[0]); k++) {
            verdict("BLOCKED", lines[k], index, id->device_id, "no retained schedule (see COMMIT): no restart is triggered and nothing can be judged");
        }
        CloseHandle(h2);
        return query_valid(&qa) ? qa.q.context_epoch : 0;
    }

    /* INIT_READY: the second instance's INIT_DEVICE on a READY adapter is no invalidating event */
    ZeroMemory(&hw, sizeof(hw));
    if (!DeviceIoControl(h2, IOCTL_AVB_GET_HW_STATE, &hw, sizeof(hw), &hw, sizeof(hw), &br, NULL)) {
        hw.hw_state = (avb_u32)AVB_HW_UNBOUND;
    }
    meta_phc_reads(h, "before INIT_DEVICE", &p0);
    ZeroMemory(&init, sizeof(init));
    information = 0;
    st = avb_ioctl_ntstatus(h2, IOCTL_AVB_INIT_DEVICE, &init, sizeof(init), &init, sizeof(init), &information);
    meta_phc_reads(h, "after INIT_DEVICE", &p1);
    query(h, &qi);
    printf("    [EVIDENCE] INIT_DEVICE on the second bound instance: hw_state=%u (READY is %u) ntstatus=0x%08lX generation %llu -> %llu\n", hw.hw_state,
           (unsigned)AVB_HW_PTP_READY, (unsigned long)st, qa.q.generation, qi.q.generation);
    reset = tas_phc_reset(&p0, &p1);
    if (!tas_init_control(hw.hw_state == (avb_u32)AVB_HW_PTP_READY, st == 0, reset)) {
        sprintf_s(why, sizeof(why), "not the control this line needs: hw_state %u, INIT_DEVICE 0x%08lX, PHC %s (generation %llu -> %llu printed)", hw.hw_state,
                  (unsigned long)st, reset == TAS_PHC_RESET_SHOWN ? "was reset" : (reset == TAS_PHC_RESET_NOT_SHOWN ? "ran on without a reset" :
                  "not measured running before and after"), qa.q.generation, qi.q.generation);
        verdict("INCONCLUSIVE", "INIT_READY", index, id->device_id, why);
    } else {
        meta_line("INIT_READY", index, id->device_id, &qa, &qi, 0, "INIT_DEVICE on a second instance of the READY adapter, schedule retained");
    }
    CloseHandle(h2);

    /* The trigger: after BaseT, in the open window, with no outstanding send when that can be observed within the bound */
    t0 = GetTickCount();
    for (polls = 0; !quiet && GetTickCount() - t0 < META_RESTART_QUIET_MS; polls++) {
        quiet = read_phc(h, &phc) && phc > base + CYCLE_NS && phc % CYCLE_NS > WINDOW_START_NS + (WINDOW_END_NS - WINDOW_START_NS) / 4 &&
                phc % CYCLE_NS < WINDOW_END_NS - (WINDOW_END_NS - WINDOW_START_NS) / 4 && read_stats(h, &before) && before.OutstandingSendNBLs == 0;
        if (!quiet) { Sleep(5); }
    }
    ZeroMemory(&job, sizeof(job));
    job.luid = id->net_luid;
    trigger = owned_op_new(&g_ops, id->net_luid);
    read_sp = read_stats(h, &sp);   /* before the control: a status indication from here on is counted (STATUS_RESTART) */
    query(h, &qt);
    print_query("before the restart", &qt);
    ctl = meta_same_context(&qa, &qt) && qt.q.generation == qa.q.generation ? tas_ev_control_retained(TRUE, &qt.q) : TAS_EV_INCONCLUSIVE;
    sprintf_s(why, sizeof(why), "this commit before the Pause: current %u/0x%X next %u/0x%X commit %u snapshot 0x%X, epoch %llu generation %llu "
              "(after the commit %llu, %llu): %s", qt.q.current_status, qt.q.current_reasons, qt.q.next_status, qt.q.next_reasons,
              qt.q.current_commit.state, qt.q.snapshot_reasons, qt.q.context_epoch, qt.q.generation, qa.q.context_epoch, qa.q.generation,
              ctl == TAS_EV_PASS ? "RETAINED without a snapshot reason: the positive control" :
              "no positive control in this commit's context: STATUS_RESTART judges nothing");
    verdict(ctl == TAS_EV_PASS ? "PASS" : "INCONCLUSIVE", "CONTROL_BEFORE", index, id->device_id, why);
    meta_phc_reads(h, "before the restart", &p0);
    if (trigger == NULL || meta_find_instance(h, id->net_luid, &job) != TAS_INSTANCE_FOUND || !read_stats(h, &before)) {
        static const char *const lines[] = { "RESTART", "RESEED", "GENERATION", "STATUS_RESTART", "RECORD_RESTART", "ONCE", "STALE_VIEW" };
        unsigned k;

        for (k = 0; k < sizeof(lines) / sizeof(lines[0]); k++) {
            verdict("INCONCLUSIVE", lines[k], index, id->device_id, "the restart was not triggered: no single filter instance, no statistics or no memory");
        }
        return qa.q.context_epoch;
    }
    printf("    [EVIDENCE] trigger: PHC %lld (BaseT %lld), quiet moment %d after %u polls, outstanding sends %llu, pause=%llu restart=%llu gen=%llu attach=%llu "
           "detach=%llu\n", phc, base, quiet, polls, before.OutstandingSendNBLs, before.FilterPauseCount, before.FilterRestartCount,
           before.PauseRestartGeneration, before.FilterAttachCount, before.FilterDetachCount);
    trigger->effect_open = TRUE;   /* it only schedules: its returning settles nothing */
    if (!owned_worker_start(&trigger->worker, meta_restart_worker, &job, sizeof(job))) {
        trigger->effect_open = FALSE;
    }
    (void)owned_worker_wait(&trigger->worker, META_RESTART_IOCTL_MS);
    done = (META_RESTART_JOB *)owned_worker_result(&trigger->worker);
    printf("    [EVIDENCE] IOCTL_FILTER_RESTART_ONE_INSTANCE: returned=%d opened=%d ntstatus=0x%08lX information=%lu (no evidence of a restart)\n", done != NULL,
           done != NULL && done->opened, (unsigned long)(done != NULL ? done->st : (LONG)AVB_NTSTATUS_UNAVAILABLE), (unsigned long)(done != NULL ? done->information : 0));
    if (done != NULL && (!done->opened || !NT_SUCCESS(done->st))) {
        trigger->effect_open = FALSE;   /* it was not sent, or it was refused: nothing was scheduled */
    }

    /* RESTART: counters and epoch; then the end of the event */
    t0 = GetTickCount();
    ZeroMemory(&le, sizeof(le));
    now = before;
    while (GetTickCount() - t0 < META_RESTART_BOUND_MS) {
        if (read_stats(h, &now)) {
            tas_lifecycle_evidence(&before, &now, FALSE, &le);
            outcome = tas_lifecycle_outcome(&le);
            if (outcome != TAS_LIFECYCLE_NOT_TRIGGERED && outcome != TAS_LIFECYCLE_PAUSE_ONLY) {
                break;
            }
        }
        Sleep(20);
    }
    printf("    [EVIDENCE] after %lu ms: outcome %d pause%+lld restart%+lld gen%+lld attach%+lld detach%+lld outstanding sends %llu\n",
           (unsigned long)(GetTickCount() - t0), outcome, le.pause_delta, le.restart_delta, le.generation_delta, le.attach_delta, le.detach_delta,
           now.OutstandingSendNBLs);
    entered = outcome == TAS_LIFECYCLE_RESTART_ENTERED;   /* seen before any query that could show the end */
    t0 = GetTickCount();
    do {
        query(h, &qe);
        readable = query_readable(&qe);
        if (!readable) { Sleep(50); }
    } while (entered && !readable && GetTickCount() - t0 < META_RESTART_BOUND_MS);
    meta_phc_reads(h, "after the restart", &p1);
    query(h, &qe);
    readable = query_readable(&qe);   /* the judged query; it shows the end only after the Restart entry was seen (tas_restart_stage) */
    print_query("after the restart", &qe);
    if (read_stats(h, &now)) {
        tas_lifecycle_evidence(&before, &now, FALSE, &le);   /* after the judged query: a pause that began since then is not missed */
        outcome = tas_lifecycle_outcome(&le);
    } else {
        outcome = TAS_LIFECYCLE_NOT_TRIGGERED;   /* not measured after the query: nothing is established */
    }
    stage = tas_restart_stage(entered, outcome == TAS_LIFECYCLE_RESTART_ENTERED, meta_same_context(&qt, &qe), readable);
    completed = stage == TAS_RESTART_COMPLETED;
    if (completed) {
        trigger->effect_open = FALSE;   /* only a shown end closes it: the entry counters move before the bring-up */
    }
    sprintf_s(why, sizeof(why), "pause%+lld restart%+lld gen%+lld attach%+lld detach%+lld, epoch %llu -> %llu, bring-up %s: %s", le.pause_delta, le.restart_delta,
              le.generation_delta, le.attach_delta, le.detach_delta, qt.q.context_epoch, qe.q.context_epoch,
              completed ? "completed (hw_read READ)" : "completion not shown within the bound",
              completed ? "one restart in the same context" :
              (stage == TAS_RESTART_ENTERED ? "entered in the same context, its end is not shown: it may still write and stays an open operation" :
               "one restart in the same context is not established"));
    verdict(completed ? "PASS" : "INCONCLUSIVE", "RESTART", index, id->device_id, why);
    reset = tas_phc_reset(&p0, &p1);
    sprintf_s(why, sizeof(why), "PHC %lld, %lld before, %lld, %lld after: %s", p0.phc[0], p0.phc[1], p1.phc[0], p1.phc[1],
              reset == TAS_PHC_RESET_SHOWN ? "the clock went below its last value and runs: reset" :
              (reset == TAS_PHC_RESET_NOT_SHOWN ? "no reset shown (it advanced; continuity is not judged)" : "not measured"));
    verdict(reset == TAS_PHC_RESET_SHOWN ? "PASS" : "INCONCLUSIVE", "RESEED", index, id->device_id, why);
    event = completed && reset == TAS_PHC_RESET_SHOWN;
    if (event) {
        meta_line("GENERATION", index, id->device_id, &qt, &qe, 1, "one FilterRestart that reset the PHC under a retained schedule (L10)");
    } else {
        sprintf_s(why, sizeof(why), "the invalidating event is not established (see RESTART and RESEED): no step is required (generation %llu -> %llu printed)",
                  qt.q.generation, qe.q.generation);
        verdict("INCONCLUSIVE", "GENERATION", index, id->device_id, why);
    }
    /* STATUS_RESTART: the Pause class (#365 B). The reply after the completed Restart and two further ones: its return ends nothing */
    query(h, &qx);
    query(h, &qy);
    read_sz = read_stats(h, &sz);   /* after the last judged reply */
    printf("    [EVIDENCE] two further queries: generation %llu, %llu\n", qx.q.generation, qy.q.generation);
    ZeroMemory(&cnt, sizeof(cnt));
    tas_ev_count(&cnt, tas_ev_unreconciled(query_valid(&qe), &qe.q));
    tas_ev_count(&cnt, tas_ev_unreconciled(query_valid(&qx), &qx.q));
    tas_ev_count(&cnt, tas_ev_unreconciled(query_valid(&qy), &qy.q));
    paused = ctl == TAS_EV_PASS && completed && meta_same_context(&qe, &qy) && qe.q.context_epoch == qt.q.context_epoch;
    status_quiet = read_sp && read_sz && sp.FilterStatusCount == sz.FilterStatusCount;
    pause_out = tas_ev_pause_phase(&cnt, paused, status_quiet);
    printf("    [EVIDENCE] STATUS_RESTART: replies %s, %s, %s; from before the control to after the last reply: statistics %s, FilterStatus %+lld, "
           "NetPnP %+lld, pause %+lld, restart %+lld (evidence of this context's counters only)\n", tas_ev_class(&qe.q), tas_ev_class(&qx.q),
           tas_ev_class(&qy.q), read_sp && read_sz ? "read" : "NOT read",
           read_sp && read_sz ? (long long)(sz.FilterStatusCount - sp.FilterStatusCount) : 0LL,
           read_sp && read_sz ? (long long)(sz.FilterNetPnPCount - sp.FilterNetPnPCount) : 0LL,
           read_sp && read_sz ? (long long)(sz.FilterPauseCount - sp.FilterPauseCount) : 0LL,
           read_sp && read_sz ? (long long)(sz.FilterRestartCount - sp.FilterRestartCount) : 0LL);
    sprintf_s(why, sizeof(why), "required after the Pause: S1 UNKNOWN/UNKNOWN, reasons 0, EVENT_UNRECONCILED in each of 3 replies: %u as required, "
              "%u not, %u missing; first current %u/0x%X next %u/0x%X commit %u snapshot 0x%X, last current %u/0x%X snapshot 0x%X: %s",
              cnt.pass, cnt.fail, cnt.missing, qe.q.current_status, qe.q.current_reasons, qe.q.next_status, qe.q.next_reasons,
              qe.q.current_commit.state, qe.q.snapshot_reasons, qy.q.current_status, qy.q.current_reasons, qy.q.snapshot_reasons,
              !paused ? "no Pause after a positive control with its Restart completed in this context (see CONTROL_BEFORE, RESTART): not judged" :
              (pause_out == TAS_EV_FAIL ? "a reply without the answer: the Pause did not invalidate the retained commit" :
              (!status_quiet ? "FilterStatus entered on this context or not read: a link indication gives the same answer, the Pause is not separated" :
              (pause_out == TAS_EV_PASS ? "no status indication on this context: the answer is the Pause's" : "not every reply measured"))));
    verdict(pause_out == TAS_EV_PASS ? "PASS" : (pause_out == TAS_EV_FAIL ? "FAIL" : "INCONCLUSIVE"), "STATUS_RESTART", index, id->device_id, why);
    rec = tas_ev_record_reset(query_valid(&qt), &qt.q, query_valid(&qe), &qe.q, &op, event);
    if (tas_status_observable(query_valid(&qe), &qe.q)) {
        TAS_STATUS_MATCH m;

        tas_status_match(&qe.q, &op.schedule, &m);
        printf("    [EVIDENCE] RECORD_RESTART: the reply's registers against the committed schedule: %s, mismatch 0x%X (%s)\n",
               m.established ? "compared" : "NOT established", m.mismatch, !m.established ? "no R1/R2" : (m.mismatch == 0 ? "R1" : "R2"));
    } else {
        printf("    [EVIDENCE] RECORD_RESTART: the reply's registers are not observable: no R1/R2\n");
    }
    sprintf_s(why, sizeof(why), "commit %u -> %u, bracket [%llu, %llu] -> [%llu, %llu], committed schedule %s, epoch %llu -> %llu (commit %llu), "
              "next commit %u: %s", qt.q.current_commit.state, qe.q.current_commit.state, qt.q.current_commit.phc_before_commit_ns,
              qt.q.current_commit.phc_after_commit_ns, qe.q.current_commit.phc_before_commit_ns, qe.q.current_commit.phc_after_commit_ns,
              memcmp(&qe.q.current, &op.schedule, sizeof(op.schedule)) == 0 ? "kept" : "NOT kept", qt.q.context_epoch, qe.q.context_epoch,
              op.context_epoch, qe.q.next_commit.state,
              rec == TAS_EV_PASS ? "the reset commit keeps its schedule and bracket, its state fits the measured registers" :
              (rec == TAS_EV_FAIL ? "the record after the reset is wrong" :
               "the reset, the control or the register comparison is not established (see RESTART, RESEED, CONTROL_BEFORE, the evidence line)"));
    verdict(rec == TAS_EV_PASS ? "PASS" : (rec == TAS_EV_FAIL ? "FAIL" : "INCONCLUSIVE"), "RECORD_RESTART", index, id->device_id, why);
    record = read_state(h, &s);
    printf("    [EVIDENCE] record after the restart: %s%d; registers (TQAVCTRL 0x%08X BASET 0x%08X/0x%08X) against before (0x%08X 0x%08X/0x%08X), reported only (#364)\n",
           record ? "" : "unread ", record ? (int)s.armed : -1, qe.q.regs.tqavctrl, qe.q.regs.baset_h, qe.q.regs.baset_l, qt.q.regs.tqavctrl, qt.q.regs.baset_h,
           qt.q.regs.baset_l);

    /* ONCE: the query is a read-only observer of the same event (the two further queries above) */
    if (trigger->effect_open) {
        verdict("INCONCLUSIVE", "ONCE", index, id->device_id, "the end of the restart is not shown (see RESTART): a change could still come from it");
    } else {
        meta_line("ONCE", index, id->device_id, &qe, &qy, 0, "two further queries after the same event");
    }

    /* STALE_VIEW: the pair from before the event, told apart by the driver's record of this arm and this operation */
    if (!event) {
        verdict("INCONCLUSIVE", "STALE_VIEW", index, id->device_id, "the invalidating event is not established (see RESTART and RESEED): whether the old pair is stale is "
                "not judged");
    } else {
#ifndef NDEBUG
        AVB_DEBUG_PHC_HOLD_REQUEST armed, r, d;
        IOCTL_VERSION ver = {0};
        TAS_STALE_VIEW stale = TAS_STALE_NOT_JUDGED;
        LONG sta = (LONG)AVB_NTSTATUS_UNAVAILABLE, stact = (LONG)AVB_NTSTATUS_UNAVAILABLE, stq = (LONG)AVB_NTSTATUS_UNAVAILABLE, std;
        DWORD hinfo = 0;
        BOOL mine;

        ZeroMemory(&armed, sizeof(armed));
        ZeroMemory(&r, sizeof(r));
        ZeroMemory(&d, sizeof(d));
        ZeroMemory(&op, sizeof(op));
        if (DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &ver, sizeof(ver), &ver, sizeof(ver), &br, NULL) && br >= sizeof(ver) &&
            (ver.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) && read_phc(h, &phc)) {
            sta = holdctl(h, AVB_DEBUG_PHC_HOLD_ARM, AVB_DEBUG_PHC_HOLD_MIN_HOLD_MS, META_STALE_ARM_MS, &armed, &hinfo);
            holdctl_print("ARM for the recorded result", sta, hinfo, &armed);
        }
        if (sta == 0) {
            build_op(&op, id->net_luid, &qt.q, (phc + BASE_LEAD_NS + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS, frame);   /* the pair before the event */
            op.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
            information = 0;
            stact = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE, &op, sizeof(op), &op, sizeof(op), &information);
            stq = holdctl(h, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &r, &hinfo);
            holdctl_print("QUERY after the old-pair ACTIVATE", stq, hinfo, &r);
            std = holdctl(h, AVB_DEBUG_PHC_HOLD_DISARM, 0, 0, &d, &hinfo);
            holdctl_print("DISARM", std, hinfo, &d);
        }
        mine = sta == 0 && stq == 0 && armed.arm_id != 0 && r.arm_id == armed.arm_id && r.activate_op_id == op.client_op_id && op.client_op_id != 0 &&
               r.net_luid == id->net_luid && r.context_epoch == qt.q.context_epoch && r.activate_arrival_seq > r.armed_seq;
        if (mine) {
            stale = tas_stale_view(r.activate_result);
        }
        sprintf_s(why, sizeof(why), "ACTIVATE op=0x%016llX with the old pair (generation %llu, epoch %llu): ntstatus=0x%08lX information=%lu; recorded for this arm %llu "
                  "and op: %s result %u: %s", op.client_op_id, qt.q.generation, qt.q.context_epoch, (unsigned long)stact, (unsigned long)information, armed.arm_id,
                  mine ? "yes," : "no,", r.activate_result, stale == TAS_STALE_REFUSED ? "STATE_CONFLICT, the old view is refused" :
                  (stale == TAS_STALE_STILL_CURRENT ? "it passed the metadata check, the old view is still current (STATE_CONFLICT required)" : "not judged"));
        verdict(stale == TAS_STALE_REFUSED ? "PASS" : (stale == TAS_STALE_STILL_CURRENT ? "FAIL" : "INCONCLUSIVE"), "STALE_VIEW", index, id->device_id, why);
#else
        verdict("INCONCLUSIVE", "STALE_VIEW", index, id->device_id, "Release build: the recorded result is Debug-only, not exercised (never PASS)");
#endif
    }
    return qt.q.context_epoch;
}

/** NEW_CONTEXT: after the owned NIC cycle, the pair and the record of the context the adapter has now */
static void meta_new_context(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u64 old_epoch)
{
    HANDLE h = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
    AVB_TAS_STATE s;
    QUERY_READ q;
    TAS_NEW_CONTEXT c;
    BOOL record = FALSE, valid = FALSE;
    char why[400];

    ZeroMemory(&q, sizeof(q));
    ZeroMemory(&s, sizeof(s));
    if (h != INVALID_HANDLE_VALUE) {
        record = read_state(h, &s);
        query(h, &q);
        print_query("in the context after the cleanup", &q);
        valid = query_valid(&q);
        CloseHandle(h);
    }
    c = owned_ops_settled(&g_ops, id->net_luid, 0) ? tas_new_context(old_epoch, valid, q.q.context_epoch, q.q.generation, record, s.armed == AVB_TAS_STATE_CLEAN)
                                                   : TAS_NEW_CONTEXT_NOT_SHOWN;
    sprintf_s(why, sizeof(why), "epoch %llu (the old one %llu), generation %llu, record %s%d: %s", q.q.context_epoch, old_epoch, q.q.generation, record ? "" : "unread ",
              record ? (int)s.armed : -1, c == TAS_NEW_CONTEXT_OK ? "a new context starts at 1 and CLEAN" :
              (c == TAS_NEW_CONTEXT_VIOLATION ? "a new epoch that does not start at generation 1 with a CLEAN record" :
               "no new context is shown, or an operation of this test is still open"));
    verdict(c == TAS_NEW_CONTEXT_OK ? "PASS" : (c == TAS_NEW_CONTEXT_VIOLATION ? "FAIL" : "INCONCLUSIVE"), "NEW_CONTEXT", index, id->device_id, why);
}

static void run_holdctl_adapter(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
#ifdef NDEBUG
    static const char *lines[] = { "UNBOUND", "MALFORMED", "ARM", "HOLD", "ONESHOT", "DISARM", "EXPIRY", "ACTIVATE_ARRIVAL", "CLEANUP" };
    unsigned i;

    g_ran++;
    for (i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        verdict("INCONCLUSIVE", lines[i], index, id->device_id,
                "Release build: the #505 Debug-only control is not compiled, so this line is not exercised (never PASS)");
    }
#else
    char why[512];
    AVB_DEBUG_PHC_HOLD_REQUEST r, base, armed;
    HOLDCTL_PHC_JOB job;
    HANDLE hc, hu;
    LONG st;
    DWORD info;
    INT64 net_ns = 0;   /* sum of the PHC steps that completed; compensated in CLEANUP */
    BOOL armed_ok = FALSE;
    const avb_u32 HOLD_MS = 400u;
    HOLDCTL_STATE pre;
    HOLDCTL_DIAG diag;   /* #505 I226 drift diagnosis: evidence only, no verdict */
    OWNED_WORKER held;   /* the held reseed; owned until its completion is established */
    static const char *const outstanding_why = "the held PHC_OFFSET_ADJUST has not completed: another PHC operation would race with it, "
                                               "so this line is not exercised (never PASS)";

    ZeroMemory(&held, sizeof(held));
    g_ran++;
    printf("\n=== %s adapter:%u did=0x%04X luid=0x%016llX caps=0x%08X ===\n", g_case, index, id->device_id, id->net_luid, id->capabilities);
    hc = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
    if (hc == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "SETUP", index, id->device_id, "no handle could be bound to this adapter (OPEN_ADAPTER_BY_LUID)");
        return;
    }
    {   /* The driver build decides, at run time: Release test builds do not define NDEBUG either (#505 finding), so the
         * compile-time branch above cannot tell a Release driver. A Release driver must refuse the control as an unserved code
         * (negative probe); every other line is then not executable there, never PASS */
        IOCTL_VERSION ver = {0};
        DWORD br = 0;
        if (!DeviceIoControl(hc, IOCTL_AVB_GET_VERSION, &ver, sizeof(ver), &ver, sizeof(ver), &br, NULL)) {
            verdict("BLOCKED", "SETUP", index, id->device_id, "GET_VERSION failed: the driver build (Debug or Release) is unknown");
            CloseHandle(hc);
            return;
        }
        if (!(ver.Flags & AVB_VERSION_FLAG_DEBUG_BUILD)) {
            static const char *lines[] = { "UNBOUND", "MALFORMED", "ARM", "HOLD", "ONESHOT", "DISARM", "EXPIRY", "ACTIVATE_ARRIVAL", "CLEANUP" };
            unsigned i;
            st = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &r, &info);
            sprintf_s(why, sizeof(why), "Release driver (flags 0x%04X): QUERY completed 0x%08lX, required STATUS_INVALID_DEVICE_REQUEST (code not served)",
                      ver.Flags, (unsigned long)st);
            verdict(st == (LONG)STATUS_INVALID_DEVICE_REQUEST ? "PASS" : "FAIL", "RELEASE_ABSENT", index, id->device_id, why);
            for (i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
                verdict("INCONCLUSIVE", lines[i], index, id->device_id, "Release driver: the Debug-only control is not served, so this line is not exercised (never PASS)");
            }
            CloseHandle(hc);
            return;
        }
    }
    holdctl_capture(hc, &pre, TRUE);   /* test-owned pre-state for CLEANUP: register snapshot, a PHC/QPC pair and the PHC rate */
    printf("    [EVIDENCE] pre-state: snapshot %s, PHC %s (%lld), PHC %s\n", pre.snap_ok ? "read" : "NOT read", pre.phc_ok ? "read" : "NOT read",
           pre.phc, pre.phc_running ? "running" : "NOT running (judged as unchanged, not as continuous)");
    ZeroMemory(&diag, sizeof(diag));
    printf("    [DIAG] PHC drift diagnosis (#505 plan): anchors of 3 GET_CLOCK_CONFIG + 3 GET_TIMESTAMP reads on this bound handle; neither "
           "reply reports a software fallback, so a fallback is not detectable here; the extra reads change the test's timing\n");
    holdctl_diag_anchor(&diag, hc, id, "start");

    /* UNBOUND: an unbound handle is refused STATUS_INVALID_DEVICE_STATE with no control-state change */
    hu = open_device(GENERIC_READ | GENERIC_WRITE);
    st = (hu != INVALID_HANDLE_VALUE) ? holdctl(hu, AVB_DEBUG_PHC_HOLD_ARM, HOLD_MS, 5000u, &r, &info) : (LONG)AVB_NTSTATUS_UNAVAILABLE;
    holdctl_print("ARM on an unbound handle", st, info, &r);
    if (hu != INVALID_HANDLE_VALUE) {
        CloseHandle(hu);
    }
    sprintf_s(why, sizeof(why), "unbound ARM completed 0x%08lX, required STATUS_INVALID_DEVICE_STATE", (unsigned long)st);
    verdict(st == (LONG)STATUS_INVALID_DEVICE_STATE ? "PASS" : "FAIL", "UNBOUND", index, id->device_id, why);

    /* MALFORMED: every refusal before any state change, then a QUERY shows the state unchanged */
    st = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &base, &info);
    holdctl_print("QUERY baseline", st, info, &base);
    if (st != 0 || info != sizeof(base)) {
        sprintf_s(why, sizeof(why), "baseline QUERY completed 0x%08lX with %lu bytes, required STATUS_SUCCESS and %u bytes",
                  (unsigned long)st, (unsigned long)info, (unsigned)sizeof(base));
        verdict("FAIL", "MALFORMED", index, id->device_id, why);
    } else {
        struct { const char *what; avb_u32 version, action, hold, timeout; avb_u64 reserved; DWORD size; LONG want; } cases[] = {
            { "version 2",            2u, AVB_DEBUG_PHC_HOLD_QUERY, 0u, 0u, 0u, sizeof(r), (LONG)STATUS_REVISION_MISMATCH },
            { "4 bytes short",        AVB_DEBUG_PHC_HOLD_VERSION, AVB_DEBUG_PHC_HOLD_QUERY, 0u, 0u, 0u, sizeof(r) - 4, (LONG)STATUS_BUFFER_TOO_SMALL },
            { "unknown action 9",     AVB_DEBUG_PHC_HOLD_VERSION, 9u, 0u, 0u, 0u, sizeof(r), (LONG)STATUS_INVALID_PARAMETER },
            { "reserved_in 1",        AVB_DEBUG_PHC_HOLD_VERSION, AVB_DEBUG_PHC_HOLD_QUERY, 0u, 0u, 1u, sizeof(r), (LONG)STATUS_INVALID_PARAMETER },
            { "ARM hold_ms 0",        AVB_DEBUG_PHC_HOLD_VERSION, AVB_DEBUG_PHC_HOLD_ARM, 0u, 5000u, 0u, sizeof(r), (LONG)STATUS_INVALID_PARAMETER },
            { "ARM hold_ms 2001",     AVB_DEBUG_PHC_HOLD_VERSION, AVB_DEBUG_PHC_HOLD_ARM, 2001u, 5000u, 0u, sizeof(r), (LONG)STATUS_INVALID_PARAMETER },
            { "ARM timeout 0",        AVB_DEBUG_PHC_HOLD_VERSION, AVB_DEBUG_PHC_HOLD_ARM, HOLD_MS, 0u, 0u, sizeof(r), (LONG)STATUS_INVALID_PARAMETER },
            { "ARM timeout 10001",    AVB_DEBUG_PHC_HOLD_VERSION, AVB_DEBUG_PHC_HOLD_ARM, HOLD_MS, 10001u, 0u, sizeof(r), (LONG)STATUS_INVALID_PARAMETER },
            { "QUERY with hold_ms 5", AVB_DEBUG_PHC_HOLD_VERSION, AVB_DEBUG_PHC_HOLD_QUERY, 5u, 0u, 0u, sizeof(r), (LONG)STATUS_INVALID_PARAMETER },
            { "DISARM with timeout",  AVB_DEBUG_PHC_HOLD_VERSION, AVB_DEBUG_PHC_HOLD_DISARM, 0u, 5u, 0u, sizeof(r), (LONG)STATUS_INVALID_PARAMETER },
        };
        unsigned i, bad = 0;
        char list[256] = "";

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            ZeroMemory(&r, sizeof(r));
            r.version = cases[i].version;
            r.action = cases[i].action;
            r.hold_ms = cases[i].hold;
            r.arm_timeout_ms = cases[i].timeout;
            r.reserved_in = cases[i].reserved;
            st = avb_ioctl_ntstatus(hc, IOCTL_AVB_DEBUG_PHC_RESEED_HOLD, &r, cases[i].size, &r, cases[i].size, &info);
            printf("    [EVIDENCE] %s: ntstatus=0x%08lX (required 0x%08lX) information=%lu\n", cases[i].what, (unsigned long)st,
                   (unsigned long)cases[i].want, (unsigned long)info);
            if (st != cases[i].want) {
                bad++;
                strcat_s(list, sizeof(list), cases[i].what);
                strcat_s(list, sizeof(list), "; ");
            }
        }
        st = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &r, &info);
        holdctl_print("QUERY after the malformed requests", st, info, &r);
        if (st != 0 || r.arm_id != base.arm_id || r.state != base.state) {
            bad++;
            strcat_s(list, sizeof(list), "state changed or QUERY failed; ");
        }
        sprintf_s(why, sizeof(why), "%u deviation(s) from the required refusals and the unchanged state%s%s", bad, bad ? ": " : "", list);
        verdict(bad == 0 ? "PASS" : "FAIL", "MALFORMED", index, id->device_id, why);
    }

    /* ARM: accepted once with identity and a fresh arm id; a second ARM while ARMED is STATUS_DEVICE_BUSY and changes nothing */
    st = holdctl(hc, AVB_DEBUG_PHC_HOLD_ARM, HOLD_MS, 5000u, &armed, &info);
    holdctl_print("ARM 400 ms / 5000 ms", st, info, &armed);
    if (st != 0 || info != sizeof(armed) || armed.state != AVB_DEBUG_PHC_HOLD_ARMED || armed.arm_id == 0 || armed.arm_id == base.arm_id ||
        armed.net_luid != id->net_luid || armed.armed_seq == 0 || armed.hold_ms != HOLD_MS) {
        sprintf_s(why, sizeof(why), "ARM completed 0x%08lX (%lu bytes) state %u arm %llu (before %llu) luid 0x%016llX armed_seq %llu: required "
                  "STATUS_SUCCESS, ARMED, a new arm id, this adapter's LUID", (unsigned long)st, (unsigned long)info, armed.state,
                  armed.arm_id, base.arm_id, armed.net_luid, armed.armed_seq);
        verdict("FAIL", "ARM", index, id->device_id, why);
    } else {
        LONG st2 = holdctl(hc, AVB_DEBUG_PHC_HOLD_ARM, HOLD_MS, 5000u, &r, &info);
        AVB_DEBUG_PHC_HOLD_REQUEST q;
        LONG stq;
        holdctl_print("second ARM while ARMED", st2, info, &r);
        stq = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &q, &info);
        holdctl_print("QUERY after the second ARM", stq, info, &q);
        armed_ok = TRUE;
        if (st2 == (LONG)STATUS_DEVICE_BUSY && stq == 0 && q.arm_id == armed.arm_id && q.state == AVB_DEBUG_PHC_HOLD_ARMED) {
            verdict("PASS", "ARM", index, id->device_id, "ARMED with a new arm id and this adapter's identity; a second ARM was STATUS_DEVICE_BUSY and changed nothing");
        } else {
            sprintf_s(why, sizeof(why), "second ARM completed 0x%08lX (required STATUS_DEVICE_BUSY); afterwards arm %llu state %u (required %llu, ARMED)",
                      (unsigned long)st2, q.arm_id, q.state, armed.arm_id);
            verdict("FAIL", "ARM", index, id->device_id, why);
        }
    }

    /* HOLD: a PHC reseed on its own handle consumes the arm, is observed HOLDING through the control handle, then performs its
     * real write and returns its real result; the markers show hold begin < hold end < write enter < write done */
    if (!armed_ok) {
        verdict("INCONCLUSIVE", "HOLD", index, id->device_id, "no accepted arm (see ARM): the hold cannot be exercised");
    } else {
        HOLDCTL_PHC_JOB start;
        DWORD t0;
        BOOL seen = FALSE, started, late = FALSE, outstanding = FALSE;
        unsigned polls = 0;

        ZeroMemory(&start, sizeof(start));
        start.luid = id->net_luid;
        start.offset_ns = 1000;
        ZeroMemory(&job, sizeof(job));
        holdctl_diag_anchor(&diag, hc, id, "before HOLD step");
        t0 = GetTickCount();
        started = owned_worker_start(&held, holdctl_phc_worker, &start, sizeof(start));
        while (started && GetTickCount() - t0 < 3000u) {
            st = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &r, &info);
            polls++;
            if (st == 0 && r.state == AVB_DEBUG_PHC_HOLD_HOLDING) {
                seen = TRUE;
                holdctl_print("QUERY while the reseed is held", st, info, &r);
                break;
            }
            Sleep(2);
        }
        if (started && owned_worker_wait(&held, HOLDCTL_WORKER_MS) == OWNED_WORKER_RUNNING) {
            late = TRUE;
            printf("    [EVIDENCE] the held PHC_OFFSET_ADJUST did not complete within %u ms; waiting up to %u ms more for it\n",
                   HOLDCTL_WORKER_MS, HOLDCTL_SETTLE_MS);
            outstanding = holdctl_outstanding(&held, HOLDCTL_SETTLE_MS, &net_ns);
        }
        if (started && !outstanding) {
            job = *(const HOLDCTL_PHC_JOB *)owned_worker_result(&held);   /* completed: its storage may be read */
        }
        printf("    [EVIDENCE] held PHC_OFFSET_ADJUST +1000 ns: %s, opened=%d ntstatus=0x%08lX status=0x%X %lu ms; HOLDING observed=%d after %u polls\n",
               !started ? "not started" : (outstanding ? "STILL RUNNING" : (late ? "completed late" : "completed")), job.opened, (unsigned long)job.st,
               job.status, (unsigned long)job.ms, seen, polls);
        if (!late && job.opened && job.st == 0) {
            net_ns += 1000;   /* a late completion was already counted when it was collected */
        }
        holdctl_diag_step(&diag, 1000, job.opened && job.st == 0);
        holdctl_diag_anchor(&diag, hc, id, "after HOLD step (+1000 held)");
        st = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &r, &info);
        holdctl_print("QUERY after the held reseed", st, info, &r);
        if (late) {
            sprintf_s(why, sizeof(why), "the held reseed did not complete within %u ms: %s", HOLDCTL_WORKER_MS,
                      outstanding ? "still running, its effect on the PHC is not established" : "it completed late (see the evidence line)");
            verdict("FAIL", "HOLD", index, id->device_id, why);
        } else if (!started || !seen || !job.opened || job.st != 0 || st != 0 || r.arm_id != armed.arm_id || r.state != AVB_DEBUG_PHC_HOLD_RELEASED ||
            r.holds_completed != armed.holds_completed + 1 || r.phc_source != AVB_DEBUG_PHC_HOLD_SRC_PHC_OFFSET_ADJUST || r.phc_rc != 0 ||
            !(r.armed_seq < r.hold_begin_seq && r.hold_begin_seq < r.hold_end_seq && r.hold_end_seq < r.phc_write_enter_seq &&
              r.phc_write_enter_seq < r.phc_write_done_seq) || r.hold_elapsed_us < HOLD_MS * 900u) {
            sprintf_s(why, sizeof(why), "HOLDING seen %d, reseed ntstatus 0x%08lX, state %u, holds %u (before %u), src %u, rc %d, markers %llu<%llu<%llu<%llu<%llu, "
                      "elapsed %u us: required HOLDING observed, STATUS_SUCCESS, RELEASED, +1 hold, PHC_OFFSET_ADJUST, rc 0, strictly ordered markers, "
                      ">= %u us", seen, (unsigned long)job.st, r.state, r.holds_completed, armed.holds_completed, r.phc_source, r.phc_rc, r.armed_seq,
                      r.hold_begin_seq, r.hold_end_seq, r.phc_write_enter_seq, r.phc_write_done_seq, r.hold_elapsed_us, HOLD_MS * 900u);
            verdict("FAIL", "HOLD", index, id->device_id, why);
        } else {
            sprintf_s(why, sizeof(why), "held %u us, observed HOLDING through an independent handle, then the real write completed STATUS_SUCCESS "
                      "(markers %llu<%llu<%llu<%llu<%llu)", r.hold_elapsed_us, r.armed_seq, r.hold_begin_seq, r.hold_end_seq, r.phc_write_enter_seq,
                      r.phc_write_done_seq);
            verdict("PASS", "HOLD", index, id->device_id, why);
        }
    }

    /* ONESHOT: the consumed arm does not hold a second reseed */
    if (!armed_ok) {
        verdict("INCONCLUSIVE", "ONESHOT", index, id->device_id, "no accepted arm (see ARM)");
    } else if (holdctl_outstanding(&held, 0, &net_ns)) {
        verdict("INCONCLUSIVE", "ONESHOT", index, id->device_id, outstanding_why);
    } else {
        AVB_DEBUG_PHC_HOLD_REQUEST before;
        LONG stb = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &before, &info);
        BOOL ok = holdctl_step(id->net_luid, -1000, &job);
        if (ok) {
            net_ns -= 1000;
        }
        holdctl_diag_step(&diag, -1000, ok);
        holdctl_diag_anchor(&diag, hc, id, "after ONESHOT step (-1000)");
        st = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &r, &info);
        holdctl_print("QUERY after a second reseed", st, info, &r);
        sprintf_s(why, sizeof(why), "second reseed ntstatus 0x%08lX in %lu ms; holds %u -> %u, state %u -> %u, write marker %llu -> %llu",
                  (unsigned long)job.st, (unsigned long)job.ms, before.holds_completed, r.holds_completed, before.state, r.state,
                  before.phc_write_done_seq, r.phc_write_done_seq);
        verdict(stb == 0 && st == 0 && ok && r.holds_completed == before.holds_completed && r.state == before.state &&
                r.phc_write_done_seq == before.phc_write_done_seq ? "PASS" : "FAIL", "ONESHOT", index, id->device_id, why);
    }

    /* DISARM: an unused arm is cancelled (IDLE) and holds nothing; a DISARM in a terminal or IDLE state is a no-op */
    if (holdctl_outstanding(&held, 0, &net_ns)) {
        verdict("INCONCLUSIVE", "DISARM", index, id->device_id, outstanding_why);
    } else {
        AVB_DEBUG_PHC_HOLD_REQUEST a, d, q;
        LONG sta = holdctl(hc, AVB_DEBUG_PHC_HOLD_ARM, HOLD_MS, 5000u, &a, &info);
        LONG std = holdctl(hc, AVB_DEBUG_PHC_HOLD_DISARM, 0, 0, &d, &info);
        BOOL ok;
        LONG std2, stq;
        holdctl_diag_anchor(&diag, hc, id, "before DISARM step");
        ok = holdctl_step(id->net_luid, 1000, &job);
        if (ok) {
            net_ns += 1000;
        }
        holdctl_diag_step(&diag, 1000, ok);
        holdctl_diag_anchor(&diag, hc, id, "after DISARM step (+1000)");
        std2 = holdctl(hc, AVB_DEBUG_PHC_HOLD_DISARM, 0, 0, &q, &info);
        stq = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &q, &info);
        holdctl_print("ARM", sta, info, &a);
        holdctl_print("DISARM", std, info, &d);
        holdctl_print("QUERY after DISARM, a reseed and a second DISARM", stq, info, &q);
        sprintf_s(why, sizeof(why), "ARM 0x%08lX, DISARM 0x%08lX -> state %u, reseed 0x%08lX, second DISARM 0x%08lX, holds %u -> %u, state %u",
                  (unsigned long)sta, (unsigned long)std, d.state, (unsigned long)job.st, (unsigned long)std2, a.holds_completed,
                  q.holds_completed, q.state);
        verdict(sta == 0 && std == 0 && d.state == AVB_DEBUG_PHC_HOLD_IDLE && ok && std2 == 0 && stq == 0 &&
                q.holds_completed == a.holds_completed && q.state == AVB_DEBUG_PHC_HOLD_IDLE ? "PASS" : "FAIL", "DISARM", index, id->device_id, why);
    }

    /* EXPIRY: an unused arm expires at arm_timeout_ms and holds nothing afterwards */
    if (holdctl_outstanding(&held, 0, &net_ns)) {
        verdict("INCONCLUSIVE", "EXPIRY", index, id->device_id, outstanding_why);
    } else {
        AVB_DEBUG_PHC_HOLD_REQUEST a, q;
        LONG sta = holdctl(hc, AVB_DEBUG_PHC_HOLD_ARM, HOLD_MS, 300u, &a, &info);
        LONG stq;
        BOOL ok;
        holdctl_diag_anchor(&diag, hc, id, "EXPIRY wait begins");
        Sleep(800);
        stq = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &q, &info);
        holdctl_print("QUERY 800 ms after ARM (timeout 300 ms)", stq, info, &q);
        holdctl_diag_anchor(&diag, hc, id, "EXPIRY wait ended, before step");
        ok = holdctl_step(id->net_luid, -1000, &job);
        if (ok) {
            net_ns -= 1000;
        }
        holdctl_diag_step(&diag, -1000, ok);
        holdctl_diag_anchor(&diag, hc, id, "after EXPIRY step (-1000)");
        sprintf_s(why, sizeof(why), "ARM 0x%08lX; 800 ms later state %u (required EXPIRED), reseed 0x%08lX in %lu ms, holds %u -> %u",
                  (unsigned long)sta, q.state, (unsigned long)job.st, (unsigned long)job.ms, a.holds_completed, q.holds_completed);
        {
            AVB_DEBUG_PHC_HOLD_REQUEST q2;
            LONG stq2 = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &q2, &info);
            verdict(sta == 0 && stq == 0 && q.state == AVB_DEBUG_PHC_HOLD_EXPIRED && ok && stq2 == 0 && q2.holds_completed == a.holds_completed &&
                    q2.state == AVB_DEBUG_PHC_HOLD_EXPIRED ? "PASS" : "FAIL", "EXPIRY", index, id->device_id, why);
        }
    }

    /* ACTIVATE_ARRIVAL (TAS adapters): an ACTIVATE that arrives while armed is recorded with its op id and result. A zero
     * metadata pair is refused STATE_CONFLICT before any write (#352 R1 L6), so nothing to restore; no commit marker */
    if (!(id->capabilities & INTEL_CAP_TSN_TAS)) {
        printf("[SKIP_UNSUPPORTED] %s-ACTIVATE_ARRIVAL/adapter:%u did=0x%04X: adapter does not advertise INTEL_CAP_TSN_TAS\n", g_case, index,
               id->device_id);
    } else if (holdctl_outstanding(&held, 0, &net_ns)) {
        verdict("INCONCLUSIVE", "ACTIVATE_ARRIVAL", index, id->device_id, outstanding_why);
    } else {
        AVB_DEBUG_PHC_HOLD_REQUEST a, q, d;
        AVB_TAS_OP_REQUEST op;
        QUERY_READ qr;
        long long phc = 0;
        HANDLE ha = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
        LONG sta = holdctl(hc, AVB_DEBUG_PHC_HOLD_ARM, HOLD_MS, 5000u, &a, &info);
        LONG stact = (LONG)AVB_NTSTATUS_UNAVAILABLE, stq;
        DWORD ainfo = 0;

        ZeroMemory(&op, sizeof(op));
        if (ha != INVALID_HANDLE_VALUE && read_phc(ha, &phc)) {
            query(ha, &qr);
            build_op(&op, id->net_luid, &qr.q, (phc + BASE_LEAD_NS + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS, 1522u);
            op.expected_generation = 0;   /* zero pair: STATE_CONFLICT before any write */
            stact = avb_ioctl_ntstatus(ha, IOCTL_AVB_TAS_ACTIVATE, &op, sizeof(op), &op, sizeof(op), &ainfo);
        }
        printf("    [EVIDENCE] ACTIVATE (zero metadata) op=0x%016llX: ntstatus=0x%08lX information=%lu\n", op.client_op_id,
               (unsigned long)stact, (unsigned long)ainfo);
        if (ha != INVALID_HANDLE_VALUE) {
            CloseHandle(ha);
        }
        stq = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &q, &info);
        holdctl_print("QUERY after the ACTIVATE", stq, info, &q);
        (void)holdctl(hc, AVB_DEBUG_PHC_HOLD_DISARM, 0, 0, &d, &info);
        sprintf_s(why, sizeof(why), "ARM 0x%08lX, ACTIVATE 0x%08lX; arrival %llu (armed %llu), op 0x%016llX (sent 0x%016llX), result 0x%X, commit %llu: "
                  "required arrival after ARM, this op id, STATE_CONFLICT (12), no commit", (unsigned long)sta, (unsigned long)stact,
                  q.activate_arrival_seq, q.armed_seq, q.activate_op_id, op.client_op_id, q.activate_result, q.activate_commit_seq);
        verdict(sta == 0 && stact == (LONG)STATUS_INVALID_DEVICE_STATE && stq == 0 && q.arm_id == a.arm_id && q.activate_arrival_seq > q.armed_seq &&
                q.activate_op_id == op.client_op_id && q.activate_result == AVB_TAS_ACT_STATE_CONFLICT && q.activate_commit_seq == 0 ? "PASS" : "FAIL",
                "ACTIVATE_ARRIVAL", index, id->device_id, why);
    }

    /* CLEANUP (test-owned): compensate the PHC steps, disarm, then verify independently against the hardware and the driver:
     * every snapshot register read VALID before and after unchanged, the PHC continuous against wall time (within 1 ms), and a
     * readable control state that is neither ARMED nor HOLDING. A measured deviation is CLEANUP_FAILED; anything not measured is
     * INCONCLUSIVE, never PASS (the step sum alone proves no restoration). While an operation of this test may still be running
     * nothing is compensated and no final state is established: INCONCLUSIVE_RESTORATION, with what was measured */
    {
        BOOL comp = TRUE, failed = FALSE, unmeasured = FALSE;
        const BOOL unsettled = holdctl_outstanding(&held, HOLDCTL_WORKER_MS, &net_ns);
        LONG stq;
        HOLDCTL_STATE post;
        TAS_ACT_PHC_FACT phc;
        long long wall_ns = 0;
        LARGE_INTEGER freq;
        unsigned compared = 0, not_applicable = 0, unmeasured_regs = 0;
        int differing;
        char part[160];

        holdctl_diag_anchor(&diag, hc, id, "before cleanup compensation");
        if (unsettled) {
            printf("    [DIAG] cleanup compensation: not sent, a PHC_OFFSET_ADJUST of this test may still be running\n");
        } else if (net_ns != 0) {
            INT64 comp_ns = -net_ns;
            comp = holdctl_step(id->net_luid, comp_ns, &job);
            if (comp) {
                net_ns = 0;
            }
            holdctl_diag_step(&diag, comp_ns, comp);
            printf("    [DIAG] cleanup compensation %+lld ns: %s\n", (long long)comp_ns, comp ? "completed" : "FAILED");
        } else {
            printf("    [DIAG] cleanup compensation: none needed (net of the completed steps 0 ns)\n");
        }
        holdctl_diag_anchor(&diag, hc, id, "end (after compensation)");
        (void)holdctl(hc, AVB_DEBUG_PHC_HOLD_DISARM, 0, 0, &r, &info);
        stq = holdctl(hc, AVB_DEBUG_PHC_HOLD_QUERY, 0, 0, &r, &info);
        holdctl_print("QUERY at cleanup", stq, info, &r);
        holdctl_capture(hc, &post, FALSE);
        differing = holdctl_regs_differing(&pre, &post, &compared, &not_applicable, &unmeasured_regs);
        QueryPerformanceFrequency(&freq);
        wall_ns = (long long)((double)(post.qpc.QuadPart - pre.qpc.QuadPart) * 1e9 / (double)freq.QuadPart);
        /* A running PHC must stay continuous against wall time; a PHC that was not running before (e.g. #389) must be unchanged */
        phc = tas_act_phc(pre.phc_ok && post.phc_ok, post.phc - pre.phc, pre.phc_running ? wall_ns : 0, HOLDCTL_PHC_TOLERANCE_NS);
        printf("    [EVIDENCE] PHC across the test: %s (before=%lld after=%lld wallNs=%lld drift=%lld ns, tolerance %lld ns)\n",
               tas_act_phc_name(phc), pre.phc, post.phc, wall_ns, (post.phc - pre.phc) - wall_ns, (long long)HOLDCTL_PHC_TOLERANCE_NS);

        why[0] = '\0';
        if (unsettled) {
            strcat_s(why, sizeof(why), "a PHC_OFFSET_ADJUST (+1000 ns) of this test may still be running: not compensated; measured so far: ");
        }
        if (!comp || net_ns != 0) {
            failed = TRUE;
            sprintf_s(part, sizeof(part), "PHC compensation step failed (net %lld ns); ", (long long)net_ns);
            strcat_s(why, sizeof(why), part);
        }
        if (differing < 0) {
            unmeasured = TRUE;
            strcat_s(why, sizeof(why), "registers not measured (snapshot unavailable before or after); ");
        } else {
            sprintf_s(part, sizeof(part), "%d of %u compared snapshot registers differ, %u unmeasured, %u not applicable; ", differing, compared,
                      unmeasured_regs, not_applicable);
            strcat_s(why, sizeof(why), part);
            failed = failed || differing != 0;
            unmeasured = unmeasured || unmeasured_regs != 0;
        }
        if (phc == TAS_ACT_PHC_UNMEASURED) {
            unmeasured = TRUE;
            strcat_s(why, sizeof(why), "PHC continuity not measured; ");
        } else {
            sprintf_s(part, sizeof(part), "PHC %s; ", tas_act_phc_name(phc));
            strcat_s(why, sizeof(why), part);
            failed = failed || phc != TAS_ACT_PHC_CONTINUOUS;
        }
        if (stq != 0) {
            unmeasured = TRUE;
            sprintf_s(part, sizeof(part), "control state not readable (QUERY 0x%08lX)", (unsigned long)stq);
            strcat_s(why, sizeof(why), part);
        } else {
            sprintf_s(part, sizeof(part), "control state %u", r.state);
            strcat_s(why, sizeof(why), part);
            failed = failed || r.state == AVB_DEBUG_PHC_HOLD_ARMED || r.state == AVB_DEBUG_PHC_HOLD_HOLDING;
        }
        {
            static const char *const tags[] = { "PASS", "CLEANUP_FAILED", "INCONCLUSIVE", "INCONCLUSIVE_RESTORATION" };   /* HOLDCTL_CLEANUP */
            verdict(tags[holdctl_cleanup(!unsettled, failed, unmeasured)], "CLEANUP", index, id->device_id, why);
        }
    }
    if (!owned_worker_release(&held)) {
        printf("    [EVIDENCE] the worker of the held PHC_OFFSET_ADJUST is left running: its handle and storage stay valid until the process ends\n");
    }
    CloseHandle(hc);
#endif
}

/* ---- --metadata-events, TC-TASACT-META-005 (#369): see the exception at m5_restore ---- */
#ifndef NDEBUG
#define M6_WORKER_MS      10000u                  /* META-006: the end of the thread that calls the abort path */

static QUERY_READ g_m5_qr, g_m5_qj;   /* under the deviation: the oracle's reference, the judged query */

static void m5_observe(void *ctx)
{
    M5_RUN *r = (M5_RUN *)ctx;

    r->bst = m5_access(r->m.handle, IOCTL_AVB_READ_REGISTER, r->offset, &r->back);
    query(r->m.handle, &g_m5_qr);   /* the deviation is there before it */
    query(r->m.handle, &g_m5_qj);
}

static DWORD WINAPI m6_abort_worker(LPVOID p)
{
    M5_RUN *r = *(M5_RUN **)p;

    r->abort_tid = GetCurrentThreadId();
    r->abort_begin = InterlockedIncrement(&r->seq);
    m5_abort();   /* what the console handler calls */
    return 0;
}

/** META-006: only with the deviation confirmed, a second thread runs the abort path. It stays registered until its end was seen */
static void m6_observe(void *ctx)
{
    M5_RUN *r = (M5_RUN *)ctx;
    OWNED_OP *op;

    m5_observe(ctx);
    if (r->wst != 0 || r->bst != 0 || r->back != r->deviated || !query_valid(&g_m5_qr) || g_m5_qr.q.regs.endqt[3] != r->deviated ||
        !query_valid(&g_m5_qj) || g_m5_qj.q.regs.endqt[3] != r->deviated) {
        return;   /* not confirmed: the case restores as usual */
    }
    op = owned_op_new(&g_ops, r->luid);
    r->programmatic = TRUE;
    if (op != NULL && owned_worker_start(&op->worker, m6_abort_worker, &r, sizeof(r))) {
        (void)owned_worker_wait(&op->worker, M6_WORKER_MS);
    }
}

/* Counted instead of sent: a further change of the same record after the abort must not get here */
static volatile LONG g_m6_counted;
static void m6_count_write(void *ctx) { (void)ctx; InterlockedIncrement(&g_m6_counted); }
static LONG m6_count_restore(void *ctx) { (void)ctx; InterlockedIncrement(&g_m6_counted); return OWNED_MUT_NONE; }
#endif

static const char *const m5_lines[] = { "REG_RESTORE", "HANDLE", "DEVIATION", "GENERATION", "R2", "ORACLE_SLOT", "ORACLE_NEXT", "CONTROL", "GENERATION_RESTORED",
                                        "ORACLE_RESTORED_SLOT", "ORACLE_RESTORED_NEXT" };
static const char *const m6_lines[] = { "REG_RESTORE", "HANDLE", "DEVIATION", "ABORT_ORDER", "ABORT_ONE", "NO_REWRITE", "ABORT_SETTLED", "GENERATION_RESTORED", "CONTROL" };

static const char *const m7_lines[] = { "CONTROL_BEFORE", "LINK_EVENT", "CONTEXT", "BEFORE_BASET", "PAST_BASET", "LINK_RESTORE" };

static void m5_not_run(const char *tag, UINT32 index, avb_u16 did, const char *reason)
{
    const char *const *lines = g_meta_case == 7 ? m7_lines : (g_meta_case == 6 ? m6_lines : m5_lines);
    unsigned k, n = g_meta_case == 7 ? (unsigned)(sizeof(m7_lines) / sizeof(m7_lines[0])) :
                    (g_meta_case == 6 ? (unsigned)(sizeof(m6_lines) / sizeof(m6_lines[0])) : (unsigned)(sizeof(m5_lines) / sizeof(m5_lines[0])));

    for (k = 0; k < n; k++) {
        verdict(tag, lines[k], index, did, reason);
    }
}

#ifndef NDEBUG
/** The compared registers and the transmit mode of two replies, ENDQT3 excepted */
static BOOL m5_others_equal(const AVB_TAS_REGS *a, const AVB_TAS_REGS *b)
{
    return memcmp(a->stqt, b->stqt, sizeof(a->stqt)) == 0 && memcmp(a->endqt, b->endqt, 3 * sizeof(a->endqt[0])) == 0 &&
           a->qbvcyclet == b->qbvcyclet && a->tqavctrl == b->tqavctrl;
}

/** This case's commit: the bound adapter, its epoch and expected_generation + 1, observable, its bracket before BaseT */
static BOOL m5_mine(const QUERY_READ *r, const AVB_TAS_OP_REQUEST *op, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    return query_readable(r) && tas_status_observable(TRUE, &r->q) && r->q.net_luid == id->net_luid && r->q.device_id == id->device_id &&
           op->context_epoch != 0 && op->expected_generation != 0 && r->q.context_epoch == op->context_epoch &&
           r->q.generation == op->expected_generation + 1 && r->q.phc_after_ns < op->schedule.base_time_ns;
}

/** The retained record of the guard's reply, unchanged */
static BOOL m5_record(const QUERY_READ *r, const QUERY_READ *guard, const AVB_TAS_OP_REQUEST *op)
{
    return memcmp(&r->q.current, &op->schedule, sizeof(op->schedule)) == 0 && r->q.next_commit.state == AVB_TAS_COMMIT_NONE &&
           r->q.current_commit.phc_before_commit_ns == guard->q.current_commit.phc_before_commit_ns &&
           r->q.current_commit.phc_after_commit_ns == guard->q.current_commit.phc_after_commit_ns;
}

static BOOL m5_r1(const QUERY_READ *r)
{
    return r->q.current_status == AVB_TAS_ST_PENDING && r->q.current_reasons == 0 && r->q.next_status == AVB_TAS_ST_NONE && r->q.next_reasons == 0 &&
           r->q.current_commit.state == AVB_TAS_COMMIT_RETAINED;
}

static BOOL m5_r2(const QUERY_READ *r)
{
    return r->q.current_status == AVB_TAS_ST_INCONSISTENT && r->q.current_reasons == AVB_TAS_SLOT_RSN_WINDOW_MISMATCH &&
           r->q.next_status == AVB_TAS_ST_UNKNOWN && r->q.next_reasons == AVB_TAS_SLOT_RSN_SLOT_UNATTRIBUTED &&
           r->q.current_commit.state == AVB_TAS_COMMIT_HW_MISMATCH;
}
#endif

#ifndef NDEBUG
/** TC-TASACT-META-006 (#369, PR #524 review): the abort path after the write ended. Each line is its own measurement. It shows neither an abort
 *  before or during the write (TC-MOCK-077) nor the delivery of a Windows console signal or the end of a process */
static void m6_judge(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, M5_RUN *run, BOOL deviation, LONG restored,
                     const AVB_TAS_OP_REQUEST *op, const QUERY_READ *qg, const QUERY_READ *qj, const QUERY_READ *qc)
{
    BOOL called = run->abort_begin != 0, ended = called && InterlockedCompareExchange(&run->abort, 0, 0) != 0 &&
                  owned_ops_settled(&g_ops, id->net_luid, 0), open = m5_pending(id->net_luid), again = FALSE, control, r1;
    LONG outcome = run->abort - 1;
    volatile BOOL issued = FALSE;
    QUERY_READ qd;
    char why[800];

    printf("    [EVIDENCE] META-006 order: write ended %ld, abort called %ld, restoration began %ld and ended %ld; threads: write %lu, abort %lu, "
           "restoration %lu; writes %ld, restorations %ld; abort outcome %ld (2: restored after the end of the write), its thread %s\n",
           run->write_end, run->abort_begin, run->restore_begin, run->restore_end, run->write_tid, run->abort_tid, run->restore_tid, run->writes,
           run->restores, outcome, ended ? "ended" : (called ? "NOT seen to end" : "not started"));
    sprintf_s(why, sizeof(why), "required: write ended < abort called < restoration began < restoration ended, outcome restored; observed %ld, %ld, %ld, %ld, "
              "outcome %ld%s", run->write_end, run->abort_begin, run->restore_begin, run->restore_end, outcome,
              deviation ? (ended ? "" : "; the abort path was not seen to end") : "; the deviation is not established (see DEVIATION)");
    verdict(!deviation || !ended ? "INCONCLUSIVE" : (run->write_end != 0 && run->write_end < run->abort_begin && run->abort_begin < run->restore_begin &&
            run->restore_begin < run->restore_end && outcome == OWNED_ABORT_RESTORED ? "PASS" : "FAIL"), "ABORT_ORDER", index, id->device_id, why);
    sprintf_s(why, sizeof(why), "required: one restoration, by the abort's thread, not by the writer's; observed %ld by thread %lu (abort %lu, writer %lu), the "
              "case took result %ld over", run->restores, run->restore_tid, run->abort_tid, run->write_tid, restored);
    verdict(!deviation || !ended ? "INCONCLUSIVE" : (run->restores == 1 && run->restore_tid == run->abort_tid && run->restore_tid != run->write_tid &&
            restored == run->m.restored ? "PASS" : "FAIL"), "ABORT_ONE", index, id->device_id, why);

    /* A further change of the same record, its register accesses replaced by counters: refused, and nothing is sent even if it were not */
    if (ended && !open) {
        run->m.write = m6_count_write;
        run->m.observe = m6_count_write;
        run->m.restore = m6_count_restore;
        InterlockedExchange(&g_m6_counted, 0);
        (void)owned_mutation_run(&run->m, M5_ABORT_MS, &issued);
        again = TRUE;
    }
    query(h, &qd);
    sprintf_s(why, sizeof(why), "deviation writes sent %ld (required 1); a further change after the abort: %s, %ld calls (required: refused, 0; counted, no register "
              "access); ENDQT3 in the two queries after the restoration 0x%08X and 0x%08X (raw 0x%08X)", run->writes, !again ? "not tried" :
              (issued ? "ISSUED" : "refused"), g_m6_counted, qc->q.regs.endqt[3], qd.q.regs.endqt[3], run->raw);
    verdict(!deviation || !again || !query_valid(qc) || !query_valid(&qd) ? "INCONCLUSIVE" : (run->writes == 1 && !issued && g_m6_counted == 0 &&
            qc->q.regs.endqt[3] == run->raw && qd.q.regs.endqt[3] == run->raw ? "PASS" : "FAIL"), "NO_REWRITE", index, id->device_id, why);
    verdict(ended && !open ? "PASS" : (called || open ? "INCONCLUSIVE_RESTORATION" : "INCONCLUSIVE"), "ABORT_SETTLED", index, id->device_id,
            ended && !open ? "the abort's thread ended and no deviation or restoration is open before the cleanup" :
            (called || open ? "not seen to end: CLEANUP and RESTORE cannot pass" : "the abort path was not called"));
    meta_line("GENERATION_RESTORED", index, id->device_id, qg, &qd, 0, "the deviation and its restoration by the abort path");
    control = restored == M5_RESTORE_VERIFIED && m5_mine(&qd, op, id) && qd.q.regs.endqt[3] == qg->q.regs.endqt[3] && m5_others_equal(&qd.q.regs, &qg->q.regs);
    r1 = control && m5_r1(&qd) && m5_record(&qd, qg, op);
    sprintf_s(why, sizeof(why), "required 3/0x0, next 1/0x0, commit 2 with the committed schedule and bracket; observed %u/0x%X next %u/0x%X commit %u, ENDQT3 0x%08X "
              "(under the deviation 0x%08X)%s", qd.q.current_status, qd.q.current_reasons, qd.q.next_status, qd.q.next_reasons, qd.q.current_commit.state,
              qd.q.regs.endqt[3], qj->q.regs.endqt[3], control ? "" : "; the restored state before BaseT is not established");
    verdict(!control ? "INCONCLUSIVE" : (r1 ? "PASS" : "FAIL"), "CONTROL", index, id->device_id, why);
}
#endif

#ifndef NDEBUG
/* ---- --metadata-events, TC-TASACT-META-007 (#350, #362): a link change without a detach under a retained schedule ----
 * The MDIO auto-negotiation restart was approved by Q on 2026-11-26 for this case: one write per execution (BMCR | restart), with #472's PHY
 * identification, binding proof, guards and restoration rules; no PHY reset, no power-down, no other register write, no driver change.
 * Requirement (#362 ADR-TSN-002, lifecycle table: link change, "report unknown until re-established"; #365 12.4 S1, 12.10): after an attributed
 * link change both slots are UNKNOWN with EVENT_UNRECONCILED, before and after BaseT. PENDING before BaseT, or only LINK_EVENTS_UNOBSERVED past it,
 * is the predicted defect. Nothing is judged without an attributed event (the transition seen by the OS, one context, no PHC reset). */
#define M7_LEAD_NS        (40LL * NS_PER_SEC)     /* BaseT ahead of the commit: the change and the samples after it fit before BaseT */
#define M7_MARGIN_NS      (35LL * NS_PER_SEC)     /* PHC before BaseT immediately before the MDIO write, measured */
#define M7_PERIOD_MS      20
#define M7_BASELINE_MS    500
#define M7_DOWN_BOUND_MS  15000                   /* from the write: the OS must report the link down */
#define M7_UP_BOUND_MS    60000                   /* from the link-down: the OS must report it up */
#define M7_STABLE_MS      3000                    /* unchanged OS state after the link is back */
#define M7_PAST_BOUND_MS  70000                   /* wait for the PHC one cycle past BaseT */
#define M7_MAX_SAMPLES    6000
C_ASSERT(EV_IF_CONNECTED == MediaConnectStateConnected && EV_IF_DISCONNECTED == MediaConnectStateDisconnected);

typedef struct {
    EV_SAMPLE os;                  /* OS reading, query, OS reading */
    BOOL valid;                    /* a complete reply for this adapter */
    BOOL regs_same;                /* the compared schedule registers as in the guard's reply */
    AVB_TAS_QUERY_HW_REQUEST q;
} M7_SAMPLE;

static M7_SAMPLE g_m7[M7_MAX_SAMPLES + 2];

static double m7_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static DWORD m7_if(avb_u64 luid, MIB_IF_ROW2 *row)
{
    ZeroMemory(row, sizeof(*row));
    row->InterfaceLuid.Value = luid;
    return GetIfEntry2(row);
}

static void m7_sample(HANDLE h, const AVB_ADAPTER_IDENTITY_REQUEST *id, const QUERY_READ *qg, M7_SAMPLE *s)
{
    MIB_IF_ROW2 a, b;
    QUERY_READ q;

    ZeroMemory(s, sizeof(*s));
    s->os.t0 = m7_ms();
    s->os.e1 = m7_if(id->net_luid, &a);
    s->os.t1 = m7_ms();
    query(h, &q);
    s->os.t2 = m7_ms();
    s->os.e2 = m7_if(id->net_luid, &b);
    s->os.t3 = m7_ms();
    s->os.s1 = (int)a.MediaConnectState;
    s->os.s2 = (int)b.MediaConnectState;
    s->os.tx1 = a.TransmitLinkSpeed;
    s->os.tx2 = b.TransmitLinkSpeed;
    s->os.qstatus = q.status;
    s->os.qgot = q.got;
    s->valid = query_readable(&q) && q.q.net_luid == id->net_luid && q.q.device_id == id->device_id;
    s->os.qok = s->valid;
    if (s->valid) {
        s->q = q.q;
        s->os.mbps = q.q.link_speed_mbps;
        s->os.epoch = q.q.context_epoch;
        s->os.generation = q.q.generation;
        s->regs_same = m5_others_equal(&q.q.regs, &qg->q.regs) && q.q.regs.endqt[3] == qg->q.regs.endqt[3] && q.q.hw_mode == qg->q.hw_mode;
    }
}

/** Every sample, as runs of the same OS state and the same answer, with times after the write and PHC relative to BaseT */
static void m7_print(unsigned from, unsigned to, double tw, long long base)
{
    unsigned i, j;

    for (i = from; i < to; i = j) {
        const M7_SAMPLE *a = &g_m7[i];
        for (j = i + 1; j < to; j++) {
            const M7_SAMPLE *b = &g_m7[j];
            if (b->os.s1 != a->os.s1 || b->os.s2 != a->os.s2 || b->valid != a->valid || b->regs_same != a->regs_same ||
                (a->valid && (b->q.current_status != a->q.current_status || b->q.current_reasons != a->q.current_reasons ||
                              b->q.next_status != a->q.next_status || b->q.next_reasons != a->q.next_reasons ||
                              b->q.snapshot_reasons != a->q.snapshot_reasons || b->q.current_commit.state != a->q.current_commit.state))) {
                break;
            }
        }
        printf("    [EVIDENCE] META-007 %+.1f .. %+.1f ms (%u): OS %d/%d tx=%llu | ", a->os.t0 - tw, g_m7[j - 1].os.t3 - tw, j - i, a->os.s1, a->os.s2,
               a->os.tx1);
        if (a->valid) {
            printf("current %u/0x%X next %u/0x%X snapshot 0x%X commit %u (%s), link %u, registers %s, PHC %+.1f ms to BaseT\n", a->q.current_status,
                   a->q.current_reasons, a->q.next_status, a->q.next_reasons, a->q.snapshot_reasons, a->q.current_commit.state, tas_ev_class(&a->q),
                   a->q.link_speed_mbps, a->regs_same ? "as committed" : "CHANGED", ((double)(long long)a->q.phc_after_ns - (double)base) / 1e6);
        } else {
            printf("no complete reply (ntstatus=0x%08lX information=%lu)\n", (unsigned long)a->os.qstatus, a->os.qgot);
        }
    }
    fflush(stdout);
}

static void run_meta_link(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u32 frame)
{
    AVB_TAS_OP_REQUEST op, sent;
    AVB_TAS_STATE s0, s;
    AVB_DRIVER_STATISTICS st0, st1;
    QUERY_READ q0, qg, qb;
    MIB_IF_ROW2 pre, fin;
    DWORD pre_err, fin_err, information = 0, t0;
    LP_PHY before, after;
    EV_TRACK track;
    TAS_EV_COUNT cb, cp;
    LP_COMPARE cmp = LP_UNTRUSTED;
    HANDLE hw = INVALID_HANDLE_VALUE;
    avb_u16 bmcr = 0, value;
    long long phc = 0, phc_write = 0, phc_now = 0, base, last_phc = 0;
    double tw = 0, t_change = 0, t_down = 0;
    unsigned n = 0, iw, i, i_past, n_past = 0, after_up = 0, past_base_in_up = 0, changed = 0, cls[4] = {0, 0, 0, 0};
    LONG st;
    BOOL committed, armed, control, guard, write_ok = FALSE, stats0 = FALSE, stats1, one_context, no_reset, attributed, link_event, stable, full;
    BOOL snap_ok = FALSE, link_ok;
    int last_os = -1;
    unsigned long long last_tx = 0, pre_mbps = 0, e;
    const char *missing = NULL;
    char why[900], phy_why[300];
    TAS_EV_OUT out;

    ZeroMemory(&before, sizeof(before));
    ZeroMemory(&after, sizeof(after));
    ZeroMemory(&s, sizeof(s));
    ZeroMemory(&st0, sizeof(st0));
    ZeroMemory(&st1, sizeof(st1));
    ZeroMemory(&cb, sizeof(cb));
    ZeroMemory(&cp, sizeof(cp));
    phy_why[0] = '\0';
    printf("\n  --- META-007: a link change without a detach under a retained schedule ---\n");
    query(h, &q0);
    if (!read_state(h, &s0) || s0.armed != AVB_TAS_STATE_CLEAN || !query_clean(&q0) || q0.q.net_luid != id->net_luid || q0.q.generation == 0 ||
        q0.q.context_epoch == 0 || !read_phc(h, &phc)) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "no clean pre-state with a reported pair on the bound adapter, or no PHC: nothing is committed");
        verdict("BLOCKED", "GUARD", index, id->device_id, "nothing was committed (see COMMIT): nothing is written");
        m5_not_run("BLOCKED", index, id->device_id, "nothing was committed (see COMMIT): nothing was written");
        return;
    }
    base = (phc + M7_LEAD_NS + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS;
    build_op(&op, id->net_luid, &q0.q, base, frame);
    op.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
    sent = op;
    st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE, &op, sizeof(op), &op, sizeof(op), &information);
    committed = st == 0 && information == (DWORD)sizeof(op) && op.client_op_id == sent.client_op_id && op.net_luid == sent.net_luid &&
                op.context_epoch == sent.context_epoch && op.expected_generation == sent.expected_generation &&
                memcmp(&op.schedule, &sent.schedule, sizeof(op.schedule)) == 0;
    op = sent;
    armed = read_state(h, &s) && state_matches(&s, base);
    query(h, &qg);
    control = committed && m5_mine(&qg, &op, id) && m5_record(&qg, &qg, &op) && qg.q.hw_mode == AVB_TAS_HWMODE_TSN;

    /* The guard: everything measured before the one write, the first missing condition is the reason */
    pre_err = m7_if(id->net_luid, &pre);
    if (!committed) {
        missing = "this case's commit (one complete STATUS_SUCCESS reply)";
    } else if (!armed) {
        missing = "the driver record ARMED with the committed gate list";
    } else if (!control || tas_ev_control(TRUE, &qg.q) != TAS_EV_PASS) {
        missing = "the positive control: this commit's pair, PENDING without reasons, next NONE, snapshot 0, RETAINED, TSN mode, before BaseT";
    } else if ((id->capabilities & INTEL_CAP_MDIO) == 0) {
        missing = "INTEL_CAP_MDIO: no owned link-change mechanism";
    } else if (pre_err != NO_ERROR || pre.MediaConnectState != MediaConnectStateConnected || pre.TransmitLinkSpeed == 0 ||
               pre.TransmitLinkSpeed == ~0ULL || pre.TransmitLinkSpeed % EV_BPS_PER_MBIT != 0) {
        missing = "an OS-connected link with a known whole-Mbit/s rate";
    } else {
        pre_mbps = pre.TransmitLinkSpeed / EV_BPS_PER_MBIT;
        hw = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
        if (hw != INVALID_HANDLE_VALUE) {
            query(hw, &qb);
        }
        if (hw == INVALID_HANDLE_VALUE || !query_valid(&qb) || qb.q.net_luid != id->net_luid) {
            missing = "a read/write handle proven to be bound to this NET_LUID (an unbound MDIO request would reach another adapter)";
        } else if (!lp_phy_snap(hw, "before", TRUE, &before, phy_why, sizeof(phy_why)) || !lp_phy_allows_restart(&before, phy_why, sizeof(phy_why))) {
            missing = phy_why;
        } else {
            stats0 = read_stats(h, &st0);
            if (!stats0) {
                missing = "the driver statistics before the event";
            }
        }
    }
    if (missing == NULL) {   /* the state before the write, sampled the same way, and once more immediately before it */
        double ts = m7_ms();
        while (m7_ms() - ts < M7_BASELINE_MS && n < M7_MAX_SAMPLES) {
            m7_sample(h, id, &qg, &g_m7[n++]);
            Sleep(M7_PERIOD_MS);
        }
        if (!lp_mdio(hw, FALSE, LP_REG_BMCR, &bmcr, "before the write") || bmcr != before.bmcr) {
            missing = "BMCR read again equal to the guard's value";
        } else {
            m7_sample(h, id, &qg, &g_m7[n++]);
            for (i = 0; i < n && missing == NULL; i++) {
                if (!g_m7[i].valid || !ev_expected(&g_m7[i].os, &e) || e != pre_mbps || tas_ev_control(TRUE, &g_m7[i].q) != TAS_EV_PASS) {
                    missing = "the link and the PENDING answer stable in every sample before the write, the last one immediately before it";
                }
            }
        }
        if (missing == NULL && (!read_phc(h, &phc_write) || phc_write >= base || base - phc_write < M7_MARGIN_NS)) {
            missing = "the PHC at least 35 s before BaseT immediately before the write";
        }
    }
    guard = missing == NULL;

    sprintf_s(why, sizeof(why), "ACTIVATE op=0x%016llX base=%lld (PHC %lld) ntstatus=0x%08lX information=%lu, pair epoch %llu generation %llu -> %llu, record %d",
              op.client_op_id, base, phc, (unsigned long)st, information, op.context_epoch, op.expected_generation, qg.q.generation, (int)s.armed);
    verdict(committed ? "PASS" : "FAIL", "COMMIT", index, id->device_id, why);
    print_status("guard (positive control)", &qg);
    sprintf_s(why, sizeof(why), "missing: %s", guard ? "" : missing);
    if (guard) {
        sprintf_s(why, sizeof(why), "every precondition measured: PHY 0x%04X 0x%04X at address %u, BMCR 0x%04X, BMSR 0x%04X, OS connected at %llu Mbit/s, "
                  "PHC %.3f s before BaseT", before.id1, before.id2, LP_PHY_ADDR, before.bmcr, before.bmsr, pre_mbps, (double)(base - phc_write) / 1e9);
    }
    verdict(guard ? "PASS" : "INCONCLUSIVE", "GUARD", index, id->device_id, why);
    out = committed && m5_mine(&qg, &op, id) ? tas_ev_control(TRUE, &qg.q) : TAS_EV_INCONCLUSIVE;
    sprintf_s(why, sizeof(why), "required PENDING 3/0x0, next 1/0x0, snapshot 0, commit RETAINED 2; observed %u/0x%X next %u/0x%X snapshot 0x%X commit %u",
              qg.q.current_status, qg.q.current_reasons, qg.q.next_status, qg.q.next_reasons, qg.q.snapshot_reasons, qg.q.current_commit.state);
    verdict(out == TAS_EV_PASS ? "PASS" : (out == TAS_EV_FAIL ? "FAIL" : "INCONCLUSIVE"), "CONTROL_BEFORE", index, id->device_id, why);
    if (!guard) {
        for (i = 1; i < sizeof(m7_lines) / sizeof(m7_lines[0]); i++) {
            verdict("INCONCLUSIVE", m7_lines[i], index, id->device_id, "the guard did not hold (see GUARD): nothing was written");
        }
        if (n != 0) {
            m7_print(0, n, m7_ms(), base);
        }
        if (hw != INVALID_HANDLE_VALUE) {
            CloseHandle(hw);
        }
        return;
    }

    /* The one write: the value read plus the restart command, nothing else */
    iw = n;
    value = (avb_u16)(before.bmcr | LP_BMCR_ANRESTART);
    g_m7_did = id->device_id;
    InterlockedExchange(&g_m7_adapter, (LONG)index);
    tw = m7_ms();
    write_ok = lp_mdio(hw, TRUE, LP_REG_BMCR, &value, "restart auto-negotiation (META-007)");
    t_change = tw;
    ev_track_start(&track, &g_m7[iw - 1].os);
    while (n < M7_MAX_SAMPLES) {
        M7_SAMPLE *p = &g_m7[n++];
        BOOL had_down = track.saw_down;
        int os;
        double now;

        m7_sample(h, id, &qg, p);
        ev_track(&track, &p->os);
        os = ev_os(&p->os);
        now = p->os.t3;
        if (os != last_os || (os == 2 && p->os.tx1 != last_tx)) {
            t_change = now;
            last_os = os;
            last_tx = p->os.tx1;
        }
        if (track.saw_down && !had_down) {
            t_down = now;
        }
        if ((!track.saw_down && now - tw > M7_DOWN_BOUND_MS) || (track.phase == EV_PHASE_DOWN && now - t_down > M7_UP_BOUND_MS) ||
            (track.phase == EV_PHASE_UP && now - t_change >= M7_STABLE_MS) || now - tw > M7_DOWN_BOUND_MS + M7_UP_BOUND_MS + M7_STABLE_MS) {
            break;
        }
        Sleep(M7_PERIOD_MS);
    }
    full = n == M7_MAX_SAMPLES;
    stable = last_os == 2 && g_m7[n - 1].os.t3 - t_change >= M7_STABLE_MS;

    /* Past BaseT: the PHC one cycle beyond it, then two samples */
    t0 = GetTickCount();
    while (GetTickCount() - t0 < M7_PAST_BOUND_MS && (!read_phc(h, &phc_now) || phc_now < base + CYCLE_NS)) {
        Sleep(200);
    }
    i_past = n;
    if (phc_now >= base + CYCLE_NS) {
        m7_sample(h, id, &qg, &g_m7[n++]);
        m7_sample(h, id, &qg, &g_m7[n++]);
        n_past = 2;
    }
    stats1 = read_stats(h, &st1);
    m7_print(0, n, tw, base);

    /* Attribution: the transition seen by the OS after the write, one context throughout, no PHC reset */
    link_event = write_ok && track.saw_down && track.saw_up;
    one_context = stats0 && stats1 && st0.FilterAttachCount == st1.FilterAttachCount && st0.FilterDetachCount == st1.FilterDetachCount &&
                  st0.FilterPauseCount == st1.FilterPauseCount && st0.FilterRestartCount == st1.FilterRestartCount;
    no_reset = TRUE;
    for (i = 0; i < n; i++) {
        const M7_SAMPLE *p = &g_m7[i];
        if (!p->valid) {
            continue;
        }
        one_context = one_context && p->q.context_epoch == op.context_epoch && p->q.generation == op.expected_generation + 1;
        no_reset = no_reset && p->q.current_commit.state != AVB_TAS_COMMIT_PHC_DISCONTINUOUS && (long long)p->q.phc_before_ns >= last_phc &&
                   p->q.phc_after_ns >= p->q.phc_before_ns;
        last_phc = (long long)p->q.phc_before_ns;
        changed += i >= iw && !p->regs_same;
    }
    attributed = link_event && one_context && no_reset && !full;
    sprintf_s(why, sizeof(why), "write %s; after it the OS reported the link %s%s; %u samples%s", write_ok ? "accepted" : "NOT accepted (it may still have reached "
              "the PHY)", track.saw_down ? "disconnected" : "never disconnected", track.saw_down ? (track.saw_up ? " and connected again" : " and not "
              "connected again") : "", n - iw, full ? " (sample capacity reached)" : "");
    verdict(link_event ? "PASS" : "INCONCLUSIVE", "LINK_EVENT", index, id->device_id, why);
    sprintf_s(why, sizeof(why), "epoch %llu and generation %llu in every reply: %s; attach %llu/%llu detach %llu/%llu pause %llu/%llu restart %llu/%llu; PHC "
              "monotonic and commit never PHC_DISCONTINUOUS: %s", op.context_epoch, op.expected_generation + 1, one_context ? "yes" : "NO",
              st0.FilterAttachCount, st1.FilterAttachCount, st0.FilterDetachCount, st1.FilterDetachCount, st0.FilterPauseCount, st1.FilterPauseCount,
              st0.FilterRestartCount, st1.FilterRestartCount, no_reset ? "yes" : "NO");
    verdict(one_context && no_reset ? "PASS" : "INCONCLUSIVE", "CONTEXT", index, id->device_id, one_context && no_reset ? why :
            "not attributable to the link change alone; no verdict on the status");
    if (!(one_context && no_reset)) {
        printf("    [EVIDENCE] META-007 CONTEXT: %s\n", why);
    }
    printf("    [EVIDENCE] META-007 schedule registers after the write: %u sample(s) differ from the committed ones (reported, not judged; EG-12)\n", changed);

    /* Before BaseT: every reply after the link is back; the transition itself is evidence only */
    ev_track_start(&track, &g_m7[iw - 1].os);
    for (i = iw; i < i_past; i++) {
        const M7_SAMPLE *p = &g_m7[i];
        ev_track(&track, &p->os);
        if (track.phase != EV_PHASE_UP) {
            if (!p->valid) { cls[3]++; } else if (strcmp(tas_ev_class(&p->q), "PENDING") == 0) { cls[0]++; }
            else if (strcmp(tas_ev_class(&p->q), "EVENT_UNRECONCILED") == 0) { cls[1]++; } else { cls[2]++; }
            continue;
        }
        if (p->valid && (long long)p->q.phc_after_ns >= base) {
            past_base_in_up++;
            continue;
        }
        after_up++;
        tas_ev_count(&cb, tas_ev_unreconciled(p->valid, &p->q));
    }
    printf("    [EVIDENCE] META-007 during the transition (evidence only, the order of NDIS indication and OS table is unknown): PENDING %u, "
           "EVENT_UNRECONCILED %u, other %u, no complete reply %u\n", cls[0], cls[1], cls[2], cls[3]);
    out = tas_ev_phase(&cb, attributed);
    sprintf_s(why, sizeof(why), "after the link was back, before BaseT: %u replies, %u as required (S1 UNKNOWN, EVENT_UNRECONCILED, slot reasons 0), %u not, %u "
              "incomplete%s; %u replies were already past BaseT%s", after_up, cb.pass, cb.fail, cb.missing, after_up == 0 ? " (none: not measured)" : "",
              past_base_in_up, attributed ? "" : "; the event is not attributed (LINK_EVENT, CONTEXT)");
    verdict(out == TAS_EV_PASS ? "PASS" : (out == TAS_EV_FAIL ? "FAIL" : "INCONCLUSIVE"), "BEFORE_BASET", index, id->device_id, why);
    for (i = i_past; i < i_past + n_past; i++) {
        tas_ev_count(&cp, tas_ev_unreconciled(g_m7[i].valid && (long long)g_m7[i].q.phc_before_ns > base, &g_m7[i].q));
    }
    out = tas_ev_phase(&cp, attributed);
    sprintf_s(why, sizeof(why), "past BaseT: %u replies, %u as required, %u not (observed %u/0x%X next %u/0x%X snapshot 0x%X), %u incomplete%s", n_past, cp.pass, cp.fail,
              n_past ? g_m7[i_past].q.current_status : 0u, n_past ? g_m7[i_past].q.current_reasons : 0u, n_past ? g_m7[i_past].q.next_status : 0u,
              n_past ? g_m7[i_past].q.next_reasons : 0u, n_past ? g_m7[i_past].q.snapshot_reasons : 0u, cp.missing,
              n_past == 0 ? "; the PHC did not pass BaseT within the bound: not measured" : "");
    verdict(out == TAS_EV_PASS ? "PASS" : (out == TAS_EV_FAIL ? "FAIL" : "INCONCLUSIVE"), "PAST_BASET", index, id->device_id, why);

    /* LINK_RESTORE: the link as before and stable, the PHY by #472's rules. The owned NIC-cycle cleanup follows in every case */
    fin_err = m7_if(id->net_luid, &fin);
    snap_ok = lp_phy_snap(hw, "after", FALSE, &after, phy_why, sizeof(phy_why));
    if (snap_ok) {
        cmp = lp_compare_phy(&before, &after, phy_why, sizeof(phy_why));
    }
    link_ok = fin_err == NO_ERROR && fin.MediaConnectState == pre.MediaConnectState && fin.TransmitLinkSpeed == pre.TransmitLinkSpeed &&
              fin.ReceiveLinkSpeed == pre.ReceiveLinkSpeed;
    sprintf_s(why, sizeof(why), "OS before %d tx=%llu rx=%llu, after err=%lu %d tx=%llu rx=%llu, %s; PHY: %s%s", (int)pre.MediaConnectState, pre.TransmitLinkSpeed,
              pre.ReceiveLinkSpeed, fin_err, (int)fin.MediaConnectState, fin.TransmitLinkSpeed, fin.ReceiveLinkSpeed, stable ? "stable" : "NOT stable",
              !snap_ok ? "not read: " : (cmp == LP_SAME ? "identity and compared configuration as before" : (cmp == LP_VIOLATION ? "" : "")), phy_why);
    verdict(fin_err == NO_ERROR && (!link_ok || !stable || (snap_ok && cmp == LP_VIOLATION)) ? "CLEANUP_FAILED" :
            (fin_err != NO_ERROR || !snap_ok || cmp == LP_UNTRUSTED ? "INCONCLUSIVE_RESTORATION" : "PASS"), "LINK_RESTORE", index, id->device_id, why);
    InterlockedExchange(&g_m7_adapter, -1);
    CloseHandle(hw);
}
#endif

/** h: the bound handle of every access of this case. The caller owns the NIC-cycle cleanup. No print between the commit and the last query:
 *  the PHC phase of every step is measured, and the fewer delays the better */
static void run_meta_mismatch(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, avb_u32 frame)
{
#ifdef NDEBUG
    (void)h; (void)frame;
    verdict("INCONCLUSIVE", "COMMIT", index, id->device_id, "Release build: READ_REGISTER and WRITE_REGISTER are Debug-only, nothing is committed or written");
    verdict("INCONCLUSIVE", "GUARD", index, id->device_id, "Release build: nothing is written");
    m5_not_run("INCONCLUSIVE", index, id->device_id, "Release build: not executable here (never PASS)");
#else
    static const avb_u32 offset[M5_REGS] = { I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3, I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3,
                                             I226_QBVCYCLET };
    IOCTL_VERSION ver = {0};
    AVB_TAS_OP_REQUEST op, sent;
    AVB_TAS_STATE s0, s;
    QUERY_READ q0, qg, qr, qj, qc;
    avb_u32 reg[M5_REGS], want[M5_REGS], deviated = 0, back = 0;
    LONG reg_st[M5_REGS], wst, bst, st, restored = OWNED_MUT_NONE;
    volatile BOOL issued = FALSE;
    M5_RUN *run;
    TAS_STATUS_CONTROL c;
    TAS_STATUS_CLASS cls;
    DWORD br = 0, information = 0;
    long long phc = 0, phc_write = 0, base;
    BOOL committed, armed, nine = TRUE, owned, phase, guard, deviation, r2, r1, control;
    const char *missing;
    char why[800];
    unsigned k;

    if (!DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &ver, sizeof(ver), &ver, sizeof(ver), &br, NULL) || br < sizeof(ver) ||
        !(ver.Flags & AVB_VERSION_FLAG_DEBUG_BUILD)) {
        verdict("INCONCLUSIVE", "COMMIT", index, id->device_id, "no Debug driver shown (GET_VERSION): nothing is committed or written");
        verdict("INCONCLUSIVE", "GUARD", index, id->device_id, "no Debug driver shown (GET_VERSION): nothing is written");
        m5_not_run("INCONCLUSIVE", index, id->device_id, "no Debug driver shown: not exercised (never PASS)");
        return;
    }
    ZeroMemory(&qc, sizeof(qc));
    ZeroMemory(&s, sizeof(s));
    ZeroMemory(&g_m5_qr, sizeof(g_m5_qr));
    ZeroMemory(&g_m5_qj, sizeof(g_m5_qj));
    run = m5_new(id->net_luid);
    if (run == NULL) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "no record or events that order the write against an abort: nothing is committed");
        verdict("BLOCKED", "GUARD", index, id->device_id, "nothing was committed (see COMMIT): nothing is written");
        m5_not_run("BLOCKED", index, id->device_id, "nothing was committed (see COMMIT): nothing was written");
        return;
    }
    query(h, &q0);
    if (!read_state(h, &s0) || s0.armed != AVB_TAS_STATE_CLEAN || !query_clean(&q0) || q0.q.net_luid != id->net_luid || q0.q.generation == 0 ||
        q0.q.context_epoch == 0 || !read_phc(h, &phc)) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "no clean pre-state with a reported pair on the bound adapter, or no PHC: nothing is committed");
        verdict("BLOCKED", "GUARD", index, id->device_id, "nothing was committed (see COMMIT): nothing is written");
        m5_not_run("BLOCKED", index, id->device_id, "nothing was committed (see COMMIT): nothing was written");
        return;
    }
    base = (phc + M5_LEAD_NS + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS;
    build_op(&op, id->net_luid, &q0.q, base, frame);
    op.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
    sent = op;
    st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE, &op, sizeof(op), &op, sizeof(op), &information);
    committed = st == 0 && information == (DWORD)sizeof(op) && op.client_op_id == sent.client_op_id && op.net_luid == sent.net_luid &&
                op.context_epoch == sent.context_epoch && op.expected_generation == sent.expected_generation &&
                memcmp(&op.schedule, &sent.schedule, sizeof(op.schedule)) == 0;
    op = sent;

    /* The guard: its own query, apart from the oracle's reference. Reads only */
    armed = read_state(h, &s) && state_matches(&s, base);
    query(h, &qg);
    for (k = 0; k < AVB_TAS_QUEUES; k++) {
        want[k] = qg.q.regs.stqt[k];
        want[4 + k] = qg.q.regs.endqt[k];
    }
    want[8] = qg.q.regs.qbvcyclet;
    for (k = 0; k < M5_REGS; k++) {
        reg[k] = 0;
        reg_st[k] = committed ? m5_access(h, IOCTL_AVB_READ_REGISTER, offset[k], &reg[k]) : (LONG)AVB_NTSTATUS_UNAVAILABLE;
        nine = nine && reg_st[k] == 0 && reg[k] == want[k];
    }
    deviated = (avb_u32)I226_ENDQT_SET(reg[7], I226_ENDQT_END_TIME_MASK, I226_ENDQT_END_TIME_SHIFT, M5_END_NS);
    owned = owned_mutation_own(&run->m, h);   /* a duplicate: the same file object, the same binding */
    run->own = run->m.handle;
    phase = read_phc(h, &phc_write) && phc_write < base && base - phc_write >= M5_MARGIN_NS;
    missing = !committed ? "this case's commit (one complete STATUS_SUCCESS reply)" :
              (!armed ? "the driver record ARMED with the committed gate list" :
              (!m5_mine(&qg, &op, id) ? "an observable reply of this adapter at this commit's epoch and generation, before BaseT" :
              (qg.q.hw_mode != AVB_TAS_HWMODE_TSN || !m5_r1(&qg) || !m5_record(&qg, &qg, &op) ? "the initial state: TSN mode, PENDING, next NONE, RETAINED "
               "with the committed schedule" :
              (!nine ? "nine registers read through the bound handle equal to the query's own values" :
              (I226_ENDQT_GET(reg[7], I226_ENDQT_END_TIME_MASK, I226_ENDQT_END_TIME_SHIFT) != op.schedule.queue[3].end_ns ||
               deviated == reg[7] ? "ENDQT3.END_TIME as committed and different from the deviation" :
              (!owned ? "a handle of the change's own, duplicated from the bound one" :
              (!phase ? "the PHC read at least 1.5 s before BaseT immediately before the write" : NULL)))))));
    guard = missing == NULL;
    if (!guard) {
        owned_mutation_leave(&run->m);   /* not run: its handle is closed here */
    } else {
        run->offset = offset[7];
        run->raw = reg[7];
        run->deviated = deviated;
        run->m.write = m5_write;
        run->m.observe = g_meta_case == 6 ? m6_observe : m5_observe;
        run->m.restore = m5_restore;
        restored = owned_mutation_run(&run->m, M5_ABORT_MS, &issued);   /* owed from before the write is sent; restored on every way out */
        if (issued) {
            query(h, &qc);   /* after the restoration */
        }
    }
    wst = run->wst;
    bst = run->bst;
    back = run->back;
    qr = g_m5_qr;
    qj = g_m5_qj;

    /* Everything below only prints and judges */
    printf("    [EVIDENCE] EXCEPTION (Q 2026-11-25, %s): READ_REGISTER x9, WRITE_REGISTER x2 at the generated ENDQT3 offset; Debug driver flags=0x%04X\n",
           g_meta_case == 6 ? "the one further run META-006 only" : "this case only", ver.Flags);
    print_status("before the commit", &q0);
    sprintf_s(why, sizeof(why), "ACTIVATE op=0x%016llX base=%lld (PHC %lld) ntstatus=0x%08lX information=%lu, pair epoch %llu generation %llu -> %llu, record %d",
              op.client_op_id, base, phc, (unsigned long)st, information, op.context_epoch, op.expected_generation, qg.q.generation, (int)s.armed);
    verdict(committed ? "PASS" : "FAIL", "COMMIT", index, id->device_id, why);
    print_status("guard", &qg);
    for (k = 0; k < M5_REGS; k++) {
        printf("    [EVIDENCE] guard %s%u offset 0x%05X: READ_REGISTER 0x%08lX value 0x%08X, query 0x%08X\n", k < 4 ? "STQT" : (k < 8 ? "ENDQT" : "QBVCYCLET"),
               k < 4 ? k : (k < 8 ? k - 4 : 0), offset[k], (unsigned long)reg_st[k], reg[k], want[k]);
    }
    printf("    [EVIDENCE] PHC immediately before the write %lld, BaseT %lld\n", phc_write, base);
    sprintf_s(why, sizeof(why), "%s%s", guard ? "every precondition measured on the bound handle" : "missing: ", guard ? "" : missing);
    verdict(guard ? "PASS" : "INCONCLUSIVE", "GUARD", index, id->device_id, why);
    if (!guard) {
        m5_not_run("INCONCLUSIVE", index, id->device_id, "the guard did not hold (see GUARD): nothing was written");
        return;
    }
    if (!issued) {
        m5_not_run("INCONCLUSIVE", index, id->device_id, "aborted before the deviation was sent: nothing was written");
        return;
    }
    print_status("reference under the deviation", &qr);
    print_status("judged under the deviation", &qj);
    print_status("after the restoration", &qc);
    sprintf_s(why, sizeof(why), "ENDQT3 offset 0x%05X raw before 0x%08X, deviation 0x%08X: write 0x%08lX; restoration write 0x%08lX, read 0x%08lX value 0x%08X%s",
              run->offset, run->raw, deviated, (unsigned long)wst, (unsigned long)run->write_st, (unsigned long)run->read_st, run->value,
              restored == M5_RESTORE_VERIFIED ? ": the exact raw value" : (restored == M5_RESTORE_DIFFERS ? ": another value" :
              (restored == M5_RESTORE_UNREAD ? ": not read" : ": its end was not seen")));
    verdict(m5_restore_tag(restored), "REG_RESTORE", index, id->device_id, why);
    sprintf_s(why, sizeof(why), "the change's own handle %p (the case's %p), duplicated from the bound one before the write; closed %ld times, now %p (required: "
              "closed once, after the writer left and the restoration ended)", run->own, h, run->m.closed, run->m.handle);
    verdict(owned_mutation_open(&run->m) || restored == OWNED_MUT_UNCONFIRMED ? "INCONCLUSIVE_RESTORATION" : (run->own != NULL && run->own != h && run->m.closed == 1 && run->m.handle == NULL ?
            "PASS" : "FAIL"), "HANDLE", index, id->device_id, why);

    deviation = bst == 0 && back == deviated && m5_mine(&qr, &op, id) && m5_mine(&qj, &op, id) && qr.q.regs.endqt[3] == deviated &&
                qj.q.regs.endqt[3] == deviated && m5_others_equal(&qr.q.regs, &qg.q.regs) && m5_others_equal(&qj.q.regs, &qg.q.regs);
    sprintf_s(why, sizeof(why), "read back 0x%08lX value 0x%08X; ENDQT3 in the reference 0x%08X and in the judged query 0x%08X (required 0x%08X before both), other "
              "compared registers %s, this commit's pair before BaseT: reference %d, judged %d", (unsigned long)bst, back, qr.q.regs.endqt[3],
              qj.q.regs.endqt[3], deviated, m5_others_equal(&qr.q.regs, &qg.q.regs) && m5_others_equal(&qj.q.regs, &qg.q.regs) ? "unchanged" : "CHANGED",
              m5_mine(&qr, &op, id), m5_mine(&qj, &op, id));
    verdict(deviation ? "PASS" : "INCONCLUSIVE", "DEVIATION", index, id->device_id, why);
    if (g_meta_case == 6) {
        m6_judge(h, index, id, run, deviation, restored, &op, &qg, &qj, &qc);
        return;
    }
    meta_line("GENERATION", index, id->device_id, &qg, &qj, 0, "two queries and a raw register write under the committed schedule");

    /* R2: the driver's own answer to the real deviation, in both queries */
    r2 = deviation && m5_r2(&qr) && m5_r2(&qj) && m5_record(&qr, &qg, &op) && m5_record(&qj, &qg, &op);
    sprintf_s(why, sizeof(why), "required 6/0x8, next 7/0x1000, commit 4 with the committed schedule and bracket; reference %u/0x%X next %u/0x%X commit %u, judged "
              "%u/0x%X next %u/0x%X commit %u, record and bracket kept %d/%d%s", qr.q.current_status, qr.q.current_reasons, qr.q.next_status,
              qr.q.next_reasons, qr.q.current_commit.state, qj.q.current_status, qj.q.current_reasons, qj.q.next_status, qj.q.next_reasons,
              qj.q.current_commit.state, m5_record(&qr, &qg, &op), m5_record(&qj, &qg, &op), deviation ? "" : "; the deviation is not established (see DEVIATION)");
    verdict(!deviation ? "INCONCLUSIVE" : (r2 ? "PASS" : "FAIL"), "R2", index, id->device_id, why);

    /* The --hold oracle on these real replies: a correct R2 answer is no positive control and no failure */
    tas_status_control(query_valid(&qr), &qr.q, query_valid(&qj), &qj.q, &op, FALSE, TAS_STATUS_K8_VERIFIED, &c);
    for (k = 0; k < 2; k++) {
        cls = k == 0 ? tas_status_slot(AVB_TAS_ST_PENDING, query_valid(&qj), &qj.q, &c) : tas_status_next(query_valid(&qj), &qj.q, &c);
        sprintf_s(why, sizeof(why), "oracle class %d (%s: %s), contradiction 0x%X; required: not judged, a measured contradiction (a correct R2 answer is no positive "
                  "control and no failure)%s", (int)cls, tas_status_tag(cls), tas_status_class_name(cls), c.contradiction, r2 ? "" : "; R2 is not established");
        verdict(!r2 ? "INCONCLUSIVE" : (cls == TAS_STATUS_CONTROL_LOST ? "PASS" : "FAIL"), k == 0 ? "ORACLE_SLOT" : "ORACLE_NEXT", index, id->device_id, why);
    }

    /* After the restoration: the driver's answer, then the oracle with the deviated reference */
    control = restored == M5_RESTORE_VERIFIED && m5_mine(&qc, &op, id) && qc.q.regs.endqt[3] == qg.q.regs.endqt[3] && m5_others_equal(&qc.q.regs, &qg.q.regs);
    r1 = control && m5_r1(&qc) && m5_record(&qc, &qg, &op);
    sprintf_s(why, sizeof(why), "required 3/0x0, next 1/0x0, commit 2 with the committed schedule and bracket; observed %u/0x%X next %u/0x%X commit %u, ENDQT3 0x%08X%s",
              qc.q.current_status, qc.q.current_reasons, qc.q.next_status, qc.q.next_reasons, qc.q.current_commit.state, qc.q.regs.endqt[3],
              control ? "" : "; the restored state before BaseT is not established");
    verdict(!control ? "INCONCLUSIVE" : (r1 ? "PASS" : "FAIL"), "CONTROL", index, id->device_id, why);
    meta_line("GENERATION_RESTORED", index, id->device_id, &qj, &qc, 0, "the restoration of the register");
    tas_status_control(query_valid(&qr), &qr.q, query_valid(&qc), &qc.q, &op, FALSE, TAS_STATUS_K8_VERIFIED, &c);
    for (k = 0; k < 2; k++) {
        cls = k == 0 ? tas_status_slot(AVB_TAS_ST_PENDING, query_valid(&qc), &qc.q, &c) : tas_status_next(query_valid(&qc), &qc.q, &c);
        sprintf_s(why, sizeof(why), "oracle class %d (%s: %s), contradiction 0x%X, reference with the deviation; required: PASS, the current registers equal the committed "
                  "schedule%s", (int)cls, tas_status_tag(cls), tas_status_class_name(cls), c.contradiction, r1 && r2 ? "" : "; R2 or CONTROL is not established");
        verdict(!(r1 && r2) ? "INCONCLUSIVE" : (cls == TAS_STATUS_PASS ? "PASS" : "FAIL"), k == 0 ? "ORACLE_RESTORED_SLOT" : "ORACLE_RESTORED_NEXT", index,
                id->device_id, why);
    }
#endif
}

/* Every case starts from its own state: nothing a previous case counted, captured or left pending is carried over. g_op_id alone
 * continues - client_op_id stays distinct within the process */
static void reset_case_state(const TAS_ACT_CASE *c)
{
    g_mode = c->mode;
    g_case = c->case_id;
    g_fault_case = 0;
    g_meta_case = 0;
    g_failed = g_blocked = g_ran = 0;
    g_tas_adapters = 0;
    g_capture_luid = 0;
    g_next_seq = 0;
    memset(g_send_us, 0, sizeof(g_send_us));
    g_last_send_error = 0;
    g_all = NULL;
    g_nall = 0;
    g_cleanup_link_was_up = TRUE;
    InterlockedExchange(&g_pending_adapter, -1);
    InterlockedExchange(&g_pending_did, 0);
    InterlockedExchange(&g_pending_setup, 0);
}

/* One case: its own Winsock use, device handle and adapter discovery (a NIC cycle of an earlier case may have reordered the
 * enumeration). Returns tas_act_case_exit of its own counts */
static int run_case(void)
{
    AVB_ADAPTER_IDENTITY_REQUEST *ids = NULL, *all = NULL, first;
    BOOL complete = FALSE;
    IOCTL_VERSION version = {0};
    WSADATA wsa;
    DWORD br = 0;
    HANDLE h;
    UINT32 i = 0, count = 0, stored = 0, nall = 0;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[BLOCKED] %s: Winsock unavailable\n", g_case);
        return 3;
    }
    h = open_device(GENERIC_READ | GENERIC_WRITE);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[BLOCKED] %s: cannot open %s (error %lu)\n", g_case, DEVICE_NAME, GetLastError());
        WSACleanup();
        return 3;
    }
    if (DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL)) {
        printf("    [EVIDENCE] driver version %u.%u flags=0x%04X (%s build)\n", version.Major, version.Minor, version.Flags,
               (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) ? "Debug" : "Release");
    }
    driver_provenance();

    /* Identities first (a NIC cycle may reorder the enumeration), every one of them: the arrays are sized by the driver's count,
     * no fixed inventory; an incomplete inventory is a nonpassing setup result, never silence. No observer is chosen by model */
    if (identity_at(h, 0, &first)) {
        count = first.count;
        ids = (AVB_ADAPTER_IDENTITY_REQUEST *)calloc(count, sizeof(*ids));
        all = (AVB_ADAPTER_IDENTITY_REQUEST *)calloc(count, sizeof(*all));
        complete = ids != NULL && all != NULL;
        for (i = 0; complete && i < count; i++) {
            AVB_ADAPTER_IDENTITY_REQUEST id;
            if (!identity_at(h, i, &id) || id.count != count) {
                complete = FALSE;
                break;
            }
            all[nall++] = id;
            if (g_mode == MODE_HOLDCTL) {
                /* #505: the PHC hold applies to every adapter with a PHC; the ACTIVATE line inside checks TSN_TAS itself */
            } else if (id.capabilities & INTEL_CAP_TSN_TAS) {
                ids[stored++] = id;
            } else {
                printf("[SKIP_UNSUPPORTED] %s-COMMIT/adapter:%u did=0x%04X: adapter does not advertise INTEL_CAP_TSN_TAS\n", g_case, i, id.device_id);
            }
        }
    }
    CloseHandle(h);
    if (!complete) {
        printf("[BLOCKED] %s-DISCOVERY: adapter discovery incomplete: %u of %u identities validated; untested adapters are not passes\n",
               g_case, i, count);
        g_blocked++;
    }
    g_all = all;
    g_nall = nall;
    if (g_mode == MODE_HOLDCTL) {
        for (i = 0; i < nall; i++) {   /* #505: every enumerated adapter that advertises a PHC, no fixed limit */
            if (all[i].capabilities & INTEL_CAP_BASIC_1588) {
                run_holdctl_adapter(all[i].index, &all[i]);
            } else {
                printf("[SKIP_UNSUPPORTED] %s/adapter:%u did=0x%04X: adapter does not advertise INTEL_CAP_BASIC_1588\n", g_case, all[i].index,
                       all[i].device_id);
            }
        }
    }
    for (i = 0; i < stored; i++) {   /* every TAS adapter, no fixed limit */
        g_tas_adapters++;
        if (g_mode == MODE_FAULTV) {
            /* #482: F1 then F2, each a complete run with its own handles, pre-state, observer and owned cleanup */
            g_fault_case = 1;
            g_case = "TC-TASACT-FAULT-001-F1";
            run_adapter(ids[i].index, &ids[i]);
            g_fault_case = 2;
            g_case = "TC-TASACT-FAULT-001-F2";
            run_adapter(ids[i].index, &ids[i]);
            g_case = "TC-TASACT-FAULT-001";
        } else if (g_mode == MODE_META) {
            /* #357: the sequential events, the F1 interleaving twice and the FilterRestart, each a complete run with its own pre-state and cleanup */
            g_meta_case = 1;
            run_adapter(ids[i].index, &ids[i]);
            g_meta_case = 2;
            g_case = "TC-TASACT-META-002";
            run_adapter(ids[i].index, &ids[i]);
            g_meta_case = 3;
            g_case = "TC-TASACT-META-003";
            run_adapter(ids[i].index, &ids[i]);
            g_meta_case = 4;
            g_case = "TC-TASACT-META-004";
            run_adapter(ids[i].index, &ids[i]);
            g_meta_case = 5;
            g_case = "TC-TASACT-META-005";
            run_adapter(ids[i].index, &ids[i]);
            g_meta_case = 6;
            g_case = "TC-TASACT-META-006";
            run_adapter(ids[i].index, &ids[i]);
            g_meta_case = 7;
            g_case = "TC-TASACT-META-007";
            run_adapter(ids[i].index, &ids[i]);
            g_case = "TC-TASACT-META-001";
        } else {
            run_adapter(ids[i].index, &ids[i]);
        }
    }
    g_all = NULL;
    g_nall = 0;
    free(ids);
    free(all);
    WSACleanup();

    printf("\nTAS adapters run=%d case executions=%d nonpassing verdicts=%d blocked=%d\n", g_tas_adapters, g_ran, g_failed, g_blocked);
    return tas_act_case_exit(g_ran, g_failed, g_blocked);
}

int main(int argc, char **argv)
{
    static const char *const outcome[] = { "PASSED", "FAILED", "NO APPLICABLE ADAPTER", "BLOCKED" };
    unsigned k;
    int rc;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    SetUnhandledExceptionFilter(on_crash);
    printf("=== IntelAvbFilter - IOCTL_AVB_TAS_ACTIVATE on real hardware (#357) ===\n");
    if (!tas_act_select(argc, argv, g_selected)) {
        printf("[BLOCKED] test_tas_activate: unknown argument; without arguments every case runs, or select with:");
        for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
            printf(" %s", g_tas_act_cases[k].arg);
        }
        printf("\n");
        return 3;
    }
    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        if (g_selected[k]) {
            printf("\n--- case %s (%s) ---\n", g_tas_act_cases[k].arg, g_tas_act_cases[k].case_id);
            reset_case_state(&g_tas_act_cases[k]);
            InterlockedExchange(&g_case_pos, (LONG)k);
            g_case_result[k] = run_case();
            if (report_pending()) {
                g_case_result[k] = 1;   /* the case ended without reporting the restoration it owns: never successful */
            }
            InterlockedExchange(&g_case_pos, -1);
        }
    }
    rc = tas_act_run_exit(g_selected, g_case_result);
    printf("\n--- result per case ---\n");
    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        if (g_selected[k]) {
            printf("  %-18s %s: %s (%d)\n", g_tas_act_cases[k].arg, g_tas_act_cases[k].case_id,
                   g_case_result[k] >= 0 && g_case_result[k] <= 3 ? outcome[g_case_result[k]] : "FAILED", g_case_result[k]);
        } else {
            printf("  %-18s %s: not selected\n", g_tas_act_cases[k].arg, g_tas_act_cases[k].case_id);
        }
    }
    printf("run exit status %d (0 only when every selected case passed)\n", rc);
    return rc;
}
