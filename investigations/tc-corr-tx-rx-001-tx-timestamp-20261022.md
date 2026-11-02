# TC-CORR-TX-RX-001 — no hardware TX timestamp for injected PTP frames (2026-10-22)

Requirement: #35 REQ-F-IOCTL-TS-001 (TX timestamp retrieval); gap closure #199. Test:
`tests/integration/tx_timestamp/test_tx_timestamp_retrieval.c` (TC-CORR-TX-RX-001).

## FACTS (verified)

| # | Fact | Evidence |
|---|---|---|
| F1 | RED on all three adapters: `ts_provenance` = 1 (`PRE_SEND_PHC`), never 4 (`VERIFIED_HARDWARE_TX`); NTSTATUS 0. | `logs/test_tx_timestamp_retrieval.exe_20261022_101811.log` (Release), `..._095358.log` (Debug); 0x125B I226, 0x1533 I210, 0x15BB I219; LocalSuites_20261022_094632 (Unit + Mandatory, Debug + Release = 4 runs per adapter) |
| F2 | Installed drivers of those runs: Debug SHA256 `E217BE7C…`, Release `AC9B1495…`. | `logs/dbgview_test_tx_timestamp_retrieval_20261022_100452.log.capture.json`, `..._102909...capture.json` |
| F3 | `TEST_SEND_PTP` stores its own pre-send SYSTIM sample as the "last TX timestamp" with provenance `PRE_SEND_PHC`. | `src/avb_integration_fixed.c` AvbSendPtpCore (`InterlockedExchange64(&last_ndis_tx_record, AVB_TX_RECORD_PACK(preSendTs, sendProv))`) |
| F4 | `GET_TX_TIMESTAMP` returns that stored record first; it polls the TXSTMP FIFO only when the record is empty. | `src/avb_integration_fixed.c` case `IOCTL_AVB_GET_TX_TIMESTAMP` |
| F5 | The send-complete upgrade to `NDIS_TX_COMPLETION` needs NBL slot 26; the driver no longer requests it (`NDIS_NBL_FLAGS_CAPTURE_TIMESTAMP_ON_TRANSMIT` removed). | `src/filter.c` FilterSendNetBufferListsComplete comment + code |
| F6 | The FIFO path labels any FIFO entry `VERIFIED_HARDWARE_TX` although its own comment says it is not matched to the packet. | `src/avb_integration_fixed.c` (`/* FIFO entry is not correlated ... */`) |
| F7 | UT-CORR-005 on 0x125B failed its precondition (TX-PHC delta 2036 / 5534 ns >= 1 µs) — the same pre-send value. | `logs/test_ptp_phc_stability@125B.exe_20261021_175509.log`, `..._181828.log` |
| F8 | TSYNCTXCTL = 0x0B614 with EN = bit 4 on I210/I219/I226; bit 0 (TXTT) is defined only for I226 (and I225/I217) in the repository register SSOT. | `intel-ethernet-regs/gen/i210_regs.h`, `i219_regs.h`, `i226_regs.h` |
| F9 | `READ_REGISTER` exists only in Debug driver builds. | `include/avb_ioctl.h` (`#ifndef NDEBUG`), `src/avb_integration_fixed.c` |

## THEORIES (unverified — to be tested in this order)

| # | Theory | Test |
|---|---|---|
| H1 | Software design: injected frames can never yield a hardware egress timestamp because F3/F4 short-circuit before any hardware read. | Source-confirmed for the code path; behavioural confirmation = H2–H5 results |
| H2 | TX timestamping is disabled in hardware (TSYNCTXCTL.EN = 0). | Phase-1 probe: EN bit per adapter |
| H3 | EN = 1, but the miniport does not request a timestamp in the TX descriptor for filter-injected NBLs, so nothing latches. | Phase-1 probe: bit 0 stays 0 after every probe frame |
| H4 | A latch is already held (bit 0 = 1 before sending) by an earlier frame / another consumer, so new frames cannot latch until TXSTMPH is read. | Phase-1 probe: bit 0 before any send |
| H5 | Frames do latch, but association with the injected frame is impossible without sequence-ID matching (F6). | Phase-1 probe: 0 -> 1 after send; phase 2 (TXSTMP read, separate approval) compares value with phc_at_send |
| H6 | I219 SYSTIM is in a different epoch / software-fallback domain (hw_ts ≈ 1.79e18 ns vs ≈ 3.2e11 on I210/I226), so its "hardware" timestamps would not be comparable either. | Baseline evidence PHC characterization + probe clock context |

## Next steps

1. Phase-1 probe (`tools/test/Invoke-AvbTxTimestampProbe.ps1`, Debug driver; never reads TXSTMPL/H) on 0x125B, 0x1533, 0x15BB.
2. Depending on H2/H3/H4: driver RED test at the driver boundary, then the fix (TX-timestamp request on injected NBLs
   and/or GET_TX_TIMESTAMP prefering a hardware latch with provenance honesty, F6), then HIL GREEN.
