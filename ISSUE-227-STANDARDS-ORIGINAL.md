# TEST-STANDARDS-COMPLIANCE-001: IEEE 1722 AVTP Standards Compliance Verification

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #60 (REQ-NF-COMPATIBILITY-001: Standards Compliance and Compatibility)
- Related Requirements: #14
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P0 (Critical - Standards Compliance)

## 📋 Test Objective

Verify IEEE 1722 AVTP (Audio Video Transport Protocol) standards compliance including packet format conformance, timestamp processing accuracy, sequence number handling, and protocol-specific features.

## 🧪 Test Coverage

### 10 Unit Tests

1. **AVTP Common Header Format Validation** (verify subtype, version, stream_id fields per IEEE 1722-2016 Section 5.3)
   - **Objective**: Validate AVTP common header structure matches IEEE 1722 specification
   - **Preconditions**: AVTP packet capture available
   - **Procedure**:
     1. Parse AVTP packet common header
     2. Verify `subtype` field (bits 0-6) = 0x00 for AAF (AVTP Audio Format)
     3. Verify `version` field (bit 7) = 0 for AVTP version 0
     4. Verify `stream_id` field (64 bits) contains valid EUI-64 identifier
     5. Check `tv` (timestamp valid) bit set when timestamp present
   - **Expected**: All header fields conform to IEEE 1722-2016 Figure 5-1
   - **Implementation**: `ValidateAvtpCommonHeader()` parses packet bytes, checks bit fields against spec

2. **AVTP Timestamp Processing** (validate presentation time accuracy per IEEE 1722-2016 Section 6.4)
   - **Objective**: Verify AVTP timestamp field processed correctly with nanosecond precision
   - **Preconditions**: PHC synchronized, AVTP stream active
   - **Procedure**:
     1. Capture AVTP packet with `tv=1` (timestamp valid)
     2. Extract `avtp_timestamp` field (32 bits)
     3. Convert to nanoseconds using formula: `ns = (timestamp * 1e9) / frequency`
     4. Compare with PHC time at packet reception
     5. Verify presentation time accuracy ±100ns
   - **Expected**: Timestamp accuracy within ±100ns of PHC time
   - **Implementation**: `ProcessAvtpTimestamp()` extracts timestamp, converts to nanoseconds, compares with `KeQueryPerformanceCounter()`

3. **AVTP Sequence Number Handling** (verify packet ordering per IEEE 1722-2016 Section 5.3.2)
   - **Objective**: Validate sequence number increments correctly and detect packet loss/reordering
   - **Preconditions**: AVTP stream with multiple packets
   - **Procedure**:
     1. Initialize expected sequence number = 0
     2. For each packet, extract `sequence_num` field (8 bits)
     3. Verify `sequence_num == expected_seq % 256`
     4. Detect gaps (packet loss) if `sequence_num != expected_seq`
     5. Increment `expected_seq` for next packet
   - **Expected**: Sequence numbers increment modulo 256, gaps detected
   - **Implementation**: `ValidateAvtpSequence()` tracks `last_seq`, detects discontinuities, logs gaps

4. **AAF (AVTP Audio Format) Specific Fields** (validate audio-specific headers per IEEE 1722-2016 Section 7)
   - **Objective**: Verify AAF format-specific fields (format, rate, channels, bit_depth)
   - **Preconditions**: AAF audio stream
   - **Procedure**:
     1. Parse AAF format-specific header
     2. Verify `format` field = 0x02 for PCM
     3. Verify `nsr` (nominal sample rate) matches stream config (e.g., 0x03 for 48kHz)
     4. Verify `channels_per_frame` matches audio configuration
     5. Verify `bit_depth` = 0x00 for 16-bit, 0x02 for 24-bit
   - **Expected**: All AAF fields match stream configuration
   - **Implementation**: `ValidateAafFormat()` parses format fields, compares with `STREAM_CONFIG`

5. **AVTP Packet Length Validation** (verify stream_data_length per IEEE 1722-2016 Section 5.3.3)
   - **Objective**: Validate `stream_data_length` field matches actual payload size
   - **Preconditions**: AVTP packet with payload
   - **Procedure**:
     1. Extract `stream_data_length` field (16 bits)
     2. Calculate actual payload size from packet length
     3. Verify `stream_data_length == actual_payload_length`
     4. Check `stream_data_length % sample_size == 0` for audio
   - **Expected**: Declared length matches actual payload
   - **Implementation**: `ValidateAvtpLength()` compares declared vs. actual length, checks alignment

6. **AVTP Gateway Info (gv/mr) Fields** (validate gateway-specific fields per IEEE 1722-2016 Section 5.3.4)
   - **Objective**: Verify `gv` (gateway valid) and `mr` (media clock restart) bits
   - **Preconditions**: AVTP stream through gateway
   - **Procedure**:
     1. Extract `gv` bit (bit 8 of common header)
     2. If `gv=1`, verify `gateway_info` field present
     3. Extract `mr` bit (bit 9)
     4. If `mr=1`, verify media clock restart event
   - **Expected**: Gateway bits processed correctly
   - **Implementation**: `ProcessGatewayInfo()` checks `gv` bit, parses `gateway_info`, handles `mr` restart

7. **AVTP Stream ID Validation** (verify EUI-64 identifier per IEEE 1722-2016 Section 5.3.1)
   - **Objective**: Validate `stream_id` format as valid EUI-64
   - **Preconditions**: AVTP stream active
   - **Procedure**:
     1. Extract `stream_id` field (64 bits)
     2. Verify format: `{MAC_address[48 bits]}{unique_id[16 bits]}`
     3. Check MAC address portion matches adapter MAC
     4. Verify `unique_id` is non-zero and unique per stream
   - **Expected**: Stream ID is valid EUI-64
   - **Implementation**: `ValidateStreamId()` parses EUI-64, checks MAC prefix, validates uniqueness

8. **AVTP Padding Validation** (verify padding rules per IEEE 1722-2016 Section 5.4)
   - **Objective**: Validate padding bytes when `stream_data_length` < max payload
   - **Preconditions**: Short AVTP packet with padding
   - **Procedure**:
     1. Calculate padding size = `max_payload - stream_data_length`
     2. Verify padding bytes = 0x00
     3. Check padding does not exceed Ethernet frame size
   - **Expected**: Padding follows IEEE 1722 rules
   - **Implementation**: `ValidatePadding()` checks padding bytes, verifies zero fill

9. **AVTP CRF (Clock Reference Format) Support** (validate CRF packets per IEEE 1722-2016 Annex C)
   - **Objective**: Verify CRF packet format for clock synchronization
   - **Preconditions**: CRF stream for media clock recovery
   - **Procedure**:
     1. Verify `subtype = 0x05` for CRF
     2. Extract `crf_data_length` field
     3. Parse timestamp triplets: `(avtp_timestamp, crf_timestamp, type)`
     4. Verify CRF timestamp accuracy
   - **Expected**: CRF packets conform to Annex C
   - **Implementation**: `ValidateCrfPacket()` parses CRF-specific fields, validates timestamp triplets

10. **AVTP MAAP Integration** (verify MAAP address allocation per IEEE 1722-2016 Section 5.2)
    - **Objective**: Validate multicast MAC addresses allocated via MAAP (IEEE 1722.1)
    - **Preconditions**: MAAP enabled, multicast stream
    - **Procedure**:
      1. Verify destination MAC in MAAP range: `91:E0:F0:00:00:00` - `91:E0:F0:00:FD:FF`
      2. Check MAAP conflict detection active
      3. Verify MAC uniqueness across streams
    - **Expected**: Multicast addresses allocated correctly
    - **Implementation**: `ValidateMaapAddress()` checks MAC range, verifies no conflicts

### 3 Integration Tests

1. **End-to-End AVTP Stream Compliance** (full talker/listener conformance test)
   - **Objective**: Validate complete AVTP stream from talker to listener
   - **Preconditions**: Talker and listener devices, network switch
   - **Procedure**:
     1. Configure talker to send AAF audio stream
     2. Listener subscribes to stream via AVDECC (IEEE 1722.1)
     3. Capture all AVTP packets at listener
     4. Validate each packet: header, timestamp, sequence, payload
     5. Verify audio playout without glitches
   - **Expected**: Complete stream compliance, no errors
   - **Implementation**: `RunAvtpConformanceTest()` orchestrates talker/listener, validates all packets

2. **AVTP Interoperability Test** (third-party device compatibility)
   - **Objective**: Verify interoperability with IEEE 1722-compliant third-party devices
   - **Preconditions**: Reference talker/listener (e.g., Motu, Presonus, Luminex)
   - **Procedure**:
     1. Connect driver to third-party talker
     2. Listener receives stream and validates format
     3. Driver acts as talker to third-party listener
     4. Verify bidirectional stream compatibility
   - **Expected**: Successful interoperation with third-party devices
   - **Implementation**: Manual test with commercial AVB devices, packet capture analysis

3. **AVTP Stream Recovery** (packet loss and jitter handling)
   - **Objective**: Validate resilience to packet loss and network jitter
   - **Preconditions**: Network simulator for packet loss/delay
   - **Procedure**:
     1. Inject 1% random packet loss
     2. Verify sequence number gap detection
     3. Inject ±500µs jitter
     4. Verify timestamp-based dejitter buffering
     5. Confirm audio playout quality degradation graceful
   - **Expected**: Packet loss detected, jitter compensated
   - **Implementation**: `TestAvtpRecovery()` uses network emulator (NetEm), validates recovery mechanisms

### 2 V&V Tests

1. **IEEE 1722 Test Suite (Official Conformance)** (AVnu Alliance test suite)
   - **Objective**: Pass official IEEE 1722 conformance tests
   - **Preconditions**: AVnu Alliance test suite v2.0+
   - **Procedure**:
     1. Run AVnu test suite against driver
     2. Execute all mandatory test cases
     3. Verify pass rate ≥95%
     4. Document any failures with root cause
   - **Expected**: Pass AVnu certification requirements
   - **Implementation**: Manual execution of AVnu test suite, report generation

2. **AVTP Packet Capture Analysis** (Wireshark validation)
   - **Objective**: Validate AVTP packets with Wireshark protocol analyzer
   - **Preconditions**: Wireshark with IEEE 1722 dissector
   - **Procedure**:
     1. Capture AVTP stream with Wireshark
     2. Enable IEEE 1722 protocol dissector
     3. Verify all packets decoded correctly (zero decode errors)
     4. Check for protocol violations (malformed packets)
     5. Validate timestamp and sequence number trends
   - **Expected**: Zero Wireshark decode errors, all packets valid
   - **Implementation**: Manual Wireshark capture, visual inspection of dissected packets

## 🔧 Implementation Notes

### AVTP Header Structures

```c
/**
 * IEEE 1722 AVTP Common Header (8 bytes)
 * Ref: IEEE 1722-2016 Section 5.3
 */
typedef struct _AVTP_COMMON_HEADER {
    UINT8  subtype_version;       // [0:6] subtype, [7] version (0)
    UINT8  sv_mr_reserved_tv;     // [0] sv, [1] mr, [2:6] reserved, [7] tv
    UINT8  sequence_num;          // [0:7] sequence number (0-255)
    UINT8  reserved_tu;           // [0:6] reserved, [7] tu (timestamp uncertain)
    UINT64 stream_id;             // [0:63] EUI-64 stream identifier
    UINT32 avtp_timestamp;        // [0:31] presentation timestamp
    UINT32 gateway_info;          // [0:31] gateway-specific data (if gv=1)
    UINT16 stream_data_length;    // [0:15] payload length in bytes
} AVTP_COMMON_HEADER;

/**
 * AAF (AVTP Audio Format) Header
 * Ref: IEEE 1722-2016 Section 7
 */
typedef struct _AAF_HEADER {
    AVTP_COMMON_HEADER common;
    UINT8  format;                // [0:7] audio format (0x02 = PCM)
    UINT8  nsr_channels;          // [0:3] nominal sample rate, [4:7] reserved
    UINT8  bit_depth;             // [0:7] sample bit depth code
    UINT16 stream_data_length;    // [0:15] audio data length
    UINT8  sparse_timestamp_mode; // [0:7] sparse timestamp interval
    UINT8  evt;                   // [0:7] event field
} AAF_HEADER;
```

### AVTP Validation Functions

```c
/**
 * @brief Validate AVTP common header fields.
 * 
 * @param packet Pointer to raw AVTP packet data
 * @param packet_len Length of packet in bytes
 * @param errors Output buffer for error descriptions
 * 
 * @return TRUE if header valid, FALSE otherwise
 */
BOOLEAN ValidateAvtpCommonHeader(
    _In_reads_bytes_(packet_len) const UINT8* packet,
    _In_ UINT32 packet_len,
    _Out_writes_(256) CHAR* errors
)
{
    if (packet_len < sizeof(AVTP_COMMON_HEADER)) {
        RtlStringCchPrintfA(errors, 256, "Packet too short: %u bytes", packet_len);
        return FALSE;
    }

    const AVTP_COMMON_HEADER* hdr = (const AVTP_COMMON_HEADER*)packet;

    // Check version (bit 7 of byte 0)
    UINT8 version = (hdr->subtype_version >> 7) & 0x01;
    if (version != 0) {
        RtlStringCchPrintfA(errors, 256, "Invalid version: %u (expected 0)", version);
        return FALSE;
    }

    // Check subtype (bits 0-6 of byte 0)
    UINT8 subtype = hdr->subtype_version & 0x7F;
    if (subtype != 0x00 && subtype != 0x05) {  // 0x00=AAF, 0x05=CRF
        RtlStringCchPrintfA(errors, 256, "Unsupported subtype: 0x%02X", subtype);
        return FALSE;
    }

    // Check stream_data_length
    UINT16 data_len = RtlUshortByteSwap(hdr->stream_data_length);
    if (data_len > (packet_len - sizeof(AVTP_COMMON_HEADER))) {
        RtlStringCchPrintfA(errors, 256, 
            "stream_data_length (%u) exceeds packet size (%u)", 
            data_len, packet_len);
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Process AVTP timestamp and calculate presentation time.
 * 
 * @param avtp_timestamp Raw 32-bit AVTP timestamp field
 * @param frequency Media clock frequency (Hz)
 * @param presentation_ns Output presentation time in nanoseconds
 * 
 * @return STATUS_SUCCESS if timestamp valid
 */
NTSTATUS ProcessAvtpTimestamp(
    _In_ UINT32 avtp_timestamp,
    _In_ UINT32 frequency,
    _Out_ UINT64* presentation_ns
)
{
    if (frequency == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // Convert timestamp to nanoseconds
    // Formula: ns = (timestamp * 1e9) / frequency
    UINT64 timestamp_ns = ((UINT64)avtp_timestamp * 1000000000ULL) / frequency;

    *presentation_ns = timestamp_ns;
    return STATUS_SUCCESS;
}

/**
 * @brief Validate AVTP sequence number and detect gaps.
 * 
 * @param current_seq Current packet sequence number (0-255)
 * @param last_seq Previous packet sequence number
 * @param gap_detected Output flag indicating packet loss
 * 
 * @return TRUE if sequence valid
 */
BOOLEAN ValidateAvtpSequence(
    _In_ UINT8 current_seq,
    _In_ UINT8 last_seq,
    _Out_ BOOLEAN* gap_detected
)
{
    UINT8 expected_seq = (last_seq + 1) % 256;

    if (current_seq != expected_seq) {
        *gap_detected = TRUE;
        UINT8 gap_size = (current_seq - expected_seq) % 256;
        DbgPrint("[AVTP] Packet loss detected: expected seq %u, got %u (gap=%u)\n",
                 expected_seq, current_seq, gap_size);
        return FALSE;
    }

    *gap_detected = FALSE;
    return TRUE;
}

/**
 * @brief Validate MAAP multicast MAC address.
 * 
 * @param mac_address 6-byte MAC address
 * 
 * @return TRUE if address in MAAP range
 */
BOOLEAN ValidateMaapAddress(_In_reads_bytes_(6) const UINT8* mac_address)
{
    // MAAP range: 91:E0:F0:00:00:00 - 91:E0:F0:00:FD:FF
    if (mac_address[0] != 0x91 || 
        mac_address[1] != 0xE0 || 
        mac_address[2] != 0xF0 ||
        mac_address[3] != 0x00) {
        return FALSE;
    }

    // Check if within allocated range
    UINT16 suffix = (mac_address[4] << 8) | mac_address[5];
    if (suffix > 0xFDFF) {
        return FALSE;
    }

    return TRUE;
}
```

### Test Metrics

```c
/**
 * AVTP compliance test results
 */
typedef struct _AVTP_TEST_RESULTS {
    UINT32 total_packets;
    UINT32 valid_packets;
    UINT32 invalid_headers;
    UINT32 timestamp_errors;
    UINT32 sequence_gaps;
    UINT32 length_mismatches;
    DOUBLE compliance_percentage;
} AVTP_TEST_RESULTS;

/**
 * @brief Run comprehensive AVTP conformance test.
 * 
 * @param packet_capture Array of captured AVTP packets
 * @param packet_count Number of packets
 * @param results Output test results
 * 
 * @return STATUS_SUCCESS if test completes
 */
NTSTATUS RunAvtpConformanceTest(
    _In_reads_(packet_count) const UINT8** packet_capture,
    _In_ UINT32 packet_count,
    _Out_ AVTP_TEST_RESULTS* results
)
{
    RtlZeroMemory(results, sizeof(AVTP_TEST_RESULTS));
    results->total_packets = packet_count;

    UINT8 last_seq = 0;
    CHAR error_buffer[256];

    for (UINT32 i = 0; i < packet_count; i++) {
        BOOLEAN valid = ValidateAvtpCommonHeader(
            packet_capture[i], 
            1522,  // Max Ethernet frame size
            error_buffer
        );

        if (valid) {
            results->valid_packets++;

            // Validate sequence
            const AVTP_COMMON_HEADER* hdr = (const AVTP_COMMON_HEADER*)packet_capture[i];
            BOOLEAN gap_detected;
            ValidateAvtpSequence(hdr->sequence_num, last_seq, &gap_detected);
            if (gap_detected) {
                results->sequence_gaps++;
            }
            last_seq = hdr->sequence_num;
        } else {
            results->invalid_headers++;
            DbgPrint("[AVTP Test] Packet %u invalid: %s\n", i, error_buffer);
        }
    }

    results->compliance_percentage = 
        (100.0 * results->valid_packets) / results->total_packets;

    DbgPrint("[AVTP Test] Compliance: %.2f%% (%u/%u packets valid)\n",
             results->compliance_percentage,
             results->valid_packets,
             results->total_packets);

    return STATUS_SUCCESS;
}
```

## 📊 Standards Compliance Targets

| Metric                       | Target              | Measurement Method                        |
|------------------------------|---------------------|-------------------------------------------|
| AVTP Header Conformance      | 100%                | All packets pass header validation        |
| Timestamp Accuracy           | ±100ns              | Compare AVTP timestamp vs. PHC            |
| Sequence Gap Detection       | 100%                | All packet loss events detected           |
| Stream ID Validity           | 100%                | All stream IDs are valid EUI-64           |
| AAF Format Conformance       | 100%                | All AAF fields match stream config        |
| Interoperability Pass Rate   | ≥95%                | Third-party device compatibility          |
| AVnu Test Suite Pass Rate    | ≥95%                | Official conformance test results         |
| Wireshark Decode Errors      | Zero                | All packets decode cleanly                |

## ✅ Acceptance Criteria

### AVTP Packet Format
- ✅ All AVTP common headers valid per IEEE 1722-2016 Section 5.3
- ✅ Subtype field correct (0x00 for AAF, 0x05 for CRF)
- ✅ Version field = 0 for AVTP version 0
- ✅ Stream ID is valid EUI-64 format
- ✅ Stream data length matches actual payload

### Timestamp Processing
- ✅ AVTP timestamps processed with ±100ns accuracy
- ✅ Presentation time calculated correctly from timestamp + frequency
- ✅ Timestamp valid (tv) bit honored

### Sequence Handling
- ✅ Sequence numbers increment modulo 256
- ✅ Packet loss detected via sequence gaps
- ✅ Out-of-order packets logged

### AAF Compliance
- ✅ AAF format fields match stream configuration
- ✅ Sample rate (nsr), channels, bit depth correct
- ✅ Audio payload aligned to sample boundaries

### Interoperability
- ✅ Streams compatible with third-party AVB devices
- ✅ Pass AVnu Alliance conformance test suite (≥95%)
- ✅ Zero Wireshark protocol decode errors

### Recovery Mechanisms
- ✅ Packet loss handled gracefully
- ✅ Network jitter compensated via dejitter buffer
- ✅ Audio playout quality degradation graceful

## 🔗 References

- IEEE 1722-2016: IEEE Standard for a Transport Protocol for Time-Sensitive Applications in Bridged Local Area Networks
- IEEE 1722.1-2013: IEEE Standard for Device Discovery, Connection Management, and Control Protocol for IEEE 1722 Based Devices
- AVnu Alliance Test Plan: https://avnu.org/certification/
- Wireshark IEEE 1722 Dissector: https://www.wireshark.org/
- #60: REQ-NF-COMPATIBILITY-001 - Standards Compliance and Compatibility
- #1: StR-CORE-001 - AVB Filter Driver Core Requirements
