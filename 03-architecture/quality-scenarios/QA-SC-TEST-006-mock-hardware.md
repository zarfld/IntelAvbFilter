# Quality Attribute Scenario: QA-SC-TEST-006 - Mock Hardware Event Sources

**Scenario ID**: QA-SC-TEST-006  
**Quality Attribute**: Testability  
**Priority**: P1 (High)  
**Status**: Proposed  
**Created**: 2025-01-15  
**Related Issues**: TBD (will be created)

---

## ATAM Quality Attribute Scenario Elements

### 1. Source of Stimulus
- **Who/What**: Test engineer writing unit tests for event subsystem
- **Context**: CI/CD pipeline running on build server without physical NICs
- **Expertise**: Windows driver testing, Google Test framework, mocking patterns
- **Tools**: Visual Studio Test Runner, WinDbg, Driver Verifier

### 2. Stimulus
- **Event**: Developer needs to unit test event dispatcher logic without hardware
- **Trigger**: Pull request submitted, CI pipeline runs automated tests
- **Test Scenarios**:
  - Observer registration and unregistration
  - Event delivery to multiple observers
  - Error handling (observer faults, ring buffer overflow)
  - Performance benchmarks (event dispatch latency)
  - Schema versioning (v1/v2 translation)
- **Constraint**: No physical I225-V NICs available on test machines (VMs or build agents)

### 3. Artifact
- **System Element**: Event subsystem (EventDispatcher, Observers, RingBuffer, ISR/DPC)
- **Components to Mock**:
  - Hardware interrupt generation (ISR trigger)
  - PTP timestamp registers (TXSTMPL, TXSTMPH, ICR)
  - DPC scheduling (kernel DPC queue)
  - MMIO BAR0 access (register reads/writes)
- **Test Harness**:
  - User-mode test application (GoogleTest framework)
  - Mock ISR generator (software timer)
  - Fake hardware state (simulated registers)

### 4. Environment
- **Test Environment**: Windows 10 VM without physical NICs
- **Build Server**: GitHub Actions / Azure DevOps agent
- **Test Framework**: GoogleTest 1.14.0, WDK Test Framework
- **Isolation**: Tests run in parallel without interference

### 5. Response
1. **Dependency Injection Interface** (Week 1):
   - Define `IHardwareAbstraction` interface for hardware access
   - Real implementation: `I225Hardware` (reads BAR0 MMIO registers)
   - Mock implementation: `MockHardware` (returns canned values)
2. **Mock ISR Generator** (Week 1):
   - Software timer triggers mock ISR every 10ms (simulates 100 events/sec)
   - Writes fake timestamp values to mock register state
   - Queues DPC exactly like real ISR
3. **Mock DPC Scheduler** (Week 2):
   - Capture DPC function pointer during `IoInitializeDpcRequest()`
   - Execute DPC immediately in-line (synchronous for deterministic tests)
   - OR schedule on thread pool (asynchronous for performance tests)
4. **Mock Hardware State** (Week 2):
   - In-memory register bank (128 KB mirroring BAR0 layout)
   - Read/write hooks for ICR, TXSTMPL, TXSTMPH, TSICR
   - Simulate interrupt conditions (set ICR.TXSW bit)
5. **Test Harness Setup** (Week 3):
   - Initialize mock hardware before each test
   - Inject mock via `EventDispatcher_Create(..., mockHardware)`
   - Verify events without needing physical NIC
6. **Automated Test Execution** (Week 4):
   - Integrate with CI/CD pipeline (run on every commit)
   - Test coverage report (≥80% line coverage)
   - Performance benchmarks (measure latency, throughput)

### 6. Response Measure
| Metric | Target | Measurement |
|--------|--------|-------------|
| **Test Setup Time** | <1 hour | Time to write new test using mock |
| **Determinism** | 100% | Tests produce same results every run |
| **Test Execution Time** | <5 seconds | Run all unit tests (50+ tests) |
| **Code Coverage** | ≥80% | Line coverage for event subsystem |
| **Test Isolation** | 100% | No cross-test interference |
| **CI/CD Integration** | Automated | Tests run on every PR |
| **Mock Complexity** | <500 LOC | Mock implementation size |

---

## Detailed Implementation

### 1. Hardware Abstraction Interface

```c
// IHardwareAbstraction.h - Dependency injection for hardware access
typedef struct _IHardwareAbstraction IHardwareAbstraction;

typedef struct _IHardwareAbstraction_Vtbl {
    // Read 32-bit register
    UINT32 (*ReadRegister)(IHardwareAbstraction* self, ULONG offset);
    
    // Write 32-bit register
    VOID (*WriteRegister)(IHardwareAbstraction* self, ULONG offset, UINT32 value);
    
    // Trigger mock interrupt (test-only)
    VOID (*TriggerInterrupt)(IHardwareAbstraction* self, UINT32 interruptMask);
    
    // Get timestamp (TXSTMPL + TXSTMPH)
    UINT64 (*GetTimestamp)(IHardwareAbstraction* self);
} IHardwareAbstraction_Vtbl;

struct _IHardwareAbstraction {
    const IHardwareAbstraction_Vtbl* Vtbl;
    PVOID Context;  // Implementation-specific data
};
```

### 2. Real Hardware Implementation

```c
// I225Hardware.c - Production implementation using BAR0 MMIO
typedef struct _I225_HARDWARE {
    IHardwareAbstraction Base;  // Inherits interface
    PVOID Bar0BaseAddress;      // MMIO base address (128 KB)
    ULONG Bar0Length;
} I225_HARDWARE;

UINT32 I225Hardware_ReadRegister(IHardwareAbstraction* self, ULONG offset)
{
    I225_HARDWARE* hw = (I225_HARDWARE*)self->Context;
    volatile UINT32* reg = (volatile UINT32*)((UCHAR*)hw->Bar0BaseAddress + offset);
    return *reg;  // Direct MMIO read
}

VOID I225Hardware_WriteRegister(IHardwareAbstraction* self, ULONG offset, UINT32 value)
{
    I225_HARDWARE* hw = (I225_HARDWARE*)self->Context;
    volatile UINT32* reg = (volatile UINT32*)((UCHAR*)hw->Bar0BaseAddress + offset);
    *reg = value;  // Direct MMIO write
}

UINT64 I225Hardware_GetTimestamp(IHardwareAbstraction* self)
{
    UINT32 low = self->Vtbl->ReadRegister(self, OFFSET_TXSTMPL);   // 0x03C04
    UINT32 high = self->Vtbl->ReadRegister(self, OFFSET_TXSTMPH);  // 0x03C08
    return ((UINT64)high << 32) | low;
}

// Factory function
IHardwareAbstraction* CreateI225Hardware(PVOID bar0, ULONG length)
{
    I225_HARDWARE* hw = malloc(sizeof(I225_HARDWARE));
    hw->Bar0BaseAddress = bar0;
    hw->Bar0Length = length;
    
    static const IHardwareAbstraction_Vtbl vtbl = {
        .ReadRegister = I225Hardware_ReadRegister,
        .WriteRegister = I225Hardware_WriteRegister,
        .GetTimestamp = I225Hardware_GetTimestamp,
        .TriggerInterrupt = NULL  // Real hardware doesn't support software triggers
    };
    
    hw->Base.Vtbl = &vtbl;
    hw->Base.Context = hw;
    return &hw->Base;
}
```

### 3. Mock Hardware Implementation

```c
// MockHardware.c - Test implementation with simulated registers
typedef struct _MOCK_HARDWARE {
    IHardwareAbstraction Base;
    UINT32 Registers[32768];    // 128 KB / 4 bytes = 32768 registers
    UINT64 MockTimestamp;       // Simulated PTP timestamp
    PKDPC MockDpc;              // Captured DPC for manual triggering
    PVOID MockDpcContext;
} MOCK_HARDWARE;

UINT32 MockHardware_ReadRegister(IHardwareAbstraction* self, ULONG offset)
{
    MOCK_HARDWARE* mock = (MOCK_HARDWARE*)self->Context;
    ULONG index = offset / sizeof(UINT32);
    
    if (index >= 32768) {
        return 0xFFFFFFFF;  // Invalid offset
    }
    
    return mock->Registers[index];
}

VOID MockHardware_WriteRegister(IHardwareAbstraction* self, ULONG offset, UINT32 value)
{
    MOCK_HARDWARE* mock = (MOCK_HARDWARE*)self->Context;
    ULONG index = offset / sizeof(UINT32);
    
    if (index >= 32768) {
        return;  // Invalid offset
    }
    
    mock->Registers[index] = value;
    
    // Simulate hardware behavior: Writing to TSICR clears interrupt
    if (offset == OFFSET_TSICR) {
        ULONG icrIndex = OFFSET_ICR / sizeof(UINT32);
        mock->Registers[icrIndex] &= ~value;  // Clear bits
    }
}

VOID MockHardware_TriggerInterrupt(IHardwareAbstraction* self, UINT32 interruptMask)
{
    MOCK_HARDWARE* mock = (MOCK_HARDWARE*)self->Context;
    ULONG icrIndex = OFFSET_ICR / sizeof(UINT32);
    
    // Set interrupt bits in ICR register
    mock->Registers[icrIndex] |= interruptMask;
    
    // Write mock timestamp to TXSTMPL/TXSTMPH
    mock->MockTimestamp += 10000000;  // +10ms in nanoseconds
    
    ULONG txstmplIndex = OFFSET_TXSTMPL / sizeof(UINT32);
    ULONG txstmphIndex = OFFSET_TXSTMPH / sizeof(UINT32);
    mock->Registers[txstmplIndex] = (UINT32)(mock->MockTimestamp & 0xFFFFFFFF);
    mock->Registers[txstmphIndex] = (UINT32)(mock->MockTimestamp >> 32);
    
    // Trigger DPC (if registered)
    if (mock->MockDpc) {
        KeInsertQueueDpc(mock->MockDpc, NULL, NULL);
    }
}

UINT64 MockHardware_GetTimestamp(IHardwareAbstraction* self)
{
    UINT32 low = self->Vtbl->ReadRegister(self, OFFSET_TXSTMPL);
    UINT32 high = self->Vtbl->ReadRegister(self, OFFSET_TXSTMPH);
    return ((UINT64)high << 32) | low;
}

// Factory function
IHardwareAbstraction* CreateMockHardware()
{
    MOCK_HARDWARE* mock = calloc(1, sizeof(MOCK_HARDWARE));
    mock->MockTimestamp = 1000000000000;  // Start at 1 second
    
    static const IHardwareAbstraction_Vtbl vtbl = {
        .ReadRegister = MockHardware_ReadRegister,
        .WriteRegister = MockHardware_WriteRegister,
        .GetTimestamp = MockHardware_GetTimestamp,
        .TriggerInterrupt = MockHardware_TriggerInterrupt  // Test-only method
    };
    
    mock->Base.Vtbl = &vtbl;
    mock->Base.Context = mock;
    return &mock->Base;
}
```

### 4. Inject Mock into EventDispatcher

```c
// EventDispatcher.c - Modified to accept hardware abstraction
typedef struct _EVENT_DISPATCHER {
    OBSERVER_NODE* ObserverList;
    KSPIN_LOCK ObserverLock;
    IHardwareAbstraction* Hardware;  // NEW: Injected dependency
    KDPC Dpc;
    OBSERVER_WATCHDOG Watchdog;
} EVENT_DISPATCHER;

// Factory function with dependency injection
EVENT_DISPATCHER* EventDispatcher_Create(IHardwareAbstraction* hardware)
{
    EVENT_DISPATCHER* dispatcher = ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(EVENT_DISPATCHER),
        'psiD'  // 'Disp'
    );
    
    if (!dispatcher) {
        return NULL;
    }
    
    dispatcher->ObserverList = NULL;
    KeInitializeSpinLock(&dispatcher->ObserverLock);
    dispatcher->Hardware = hardware;  // Inject real or mock hardware
    
    // Initialize DPC
    IoInitializeDpcRequest(GetDeviceObject(), DpcForIsr);
    dispatcher->Dpc = GetDpcObject();
    
    // Capture DPC in mock hardware (for test triggering)
    if (hardware->Vtbl->TriggerInterrupt != NULL) {
        MOCK_HARDWARE* mock = (MOCK_HARDWARE*)hardware->Context;
        mock->MockDpc = &dispatcher->Dpc;
        mock->MockDpcContext = dispatcher;
    }
    
    return dispatcher;
}

// DPC now uses abstracted hardware
VOID DpcForIsr(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
)
{
    EVENT_DISPATCHER* dispatcher = (EVENT_DISPATCHER*)DeferredContext;
    IHardwareAbstraction* hw = dispatcher->Hardware;
    
    // Read timestamp (abstracted - works with real or mock)
    UINT64 timestamp = hw->Vtbl->GetTimestamp(hw);
    
    // Create event
    PTP_TX_TIMESTAMP_EVENT event = {
        .Header = { .Type = EVENT_TYPE_PTP_TX_TIMESTAMP, .Timestamp = timestamp },
        .HardwareTimestamp = timestamp,
        // ... populate other fields ...
    };
    
    // Dispatch to observers
    EventDispatcher_EmitEvent(dispatcher, EVENT_TYPE_PTP_TX_TIMESTAMP, &event, sizeof(event));
}
```

### 5. Unit Test Example (GoogleTest)

```cpp
// EventDispatcherTest.cpp - GoogleTest unit tests
#include <gtest/gtest.h>
#include "EventDispatcher.h"
#include "MockHardware.h"
#include "MockObserver.h"

class EventDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create mock hardware
        mockHardware = CreateMockHardware();
        
        // Create event dispatcher with injected mock
        dispatcher = EventDispatcher_Create(mockHardware);
        ASSERT_NE(dispatcher, nullptr);
    }
    
    void TearDown() override {
        EventDispatcher_Destroy(dispatcher);
        DestroyMockHardware(mockHardware);
    }
    
    IHardwareAbstraction* mockHardware;
    EVENT_DISPATCHER* dispatcher;
};

// Test: Observer receives event after hardware interrupt
TEST_F(EventDispatcherTest, ObserverReceivesEventOnInterrupt)
{
    // Arrange: Register mock observer
    MockObserver* observer = CreateMockObserver();
    EventDispatcher_RegisterObserver(dispatcher, (IEventObserver*)observer);
    
    // Act: Trigger mock interrupt
    mockHardware->Vtbl->TriggerInterrupt(mockHardware, ICR_TXSW);
    
    // DPC is queued, execute it synchronously for deterministic test
    ExecuteMockDpc(dispatcher->Dpc);
    
    // Assert: Observer received exactly 1 event
    EXPECT_EQ(observer->EventCount, 1);
    EXPECT_EQ(observer->LastEventType, EVENT_TYPE_PTP_TX_TIMESTAMP);
    EXPECT_GT(observer->LastTimestamp, 0);
    
    DestroyMockObserver(observer);
}

// Test: Multiple observers receive same event
TEST_F(EventDispatcherTest, MultipleObserversReceiveSameEvent)
{
    // Arrange: Register 3 observers
    MockObserver* obs1 = CreateMockObserver();
    MockObserver* obs2 = CreateMockObserver();
    MockObserver* obs3 = CreateMockObserver();
    
    EventDispatcher_RegisterObserver(dispatcher, (IEventObserver*)obs1);
    EventDispatcher_RegisterObserver(dispatcher, (IEventObserver*)obs2);
    EventDispatcher_RegisterObserver(dispatcher, (IEventObserver*)obs3);
    
    // Act: Trigger interrupt
    mockHardware->Vtbl->TriggerInterrupt(mockHardware, ICR_TXSW);
    ExecuteMockDpc(dispatcher->Dpc);
    
    // Assert: All 3 observers received event
    EXPECT_EQ(obs1->EventCount, 1);
    EXPECT_EQ(obs2->EventCount, 1);
    EXPECT_EQ(obs3->EventCount, 1);
    
    // All observers see same timestamp
    EXPECT_EQ(obs1->LastTimestamp, obs2->LastTimestamp);
    EXPECT_EQ(obs2->LastTimestamp, obs3->LastTimestamp);
    
    DestroyMockObserver(obs1);
    DestroyMockObserver(obs2);
    DestroyMockObserver(obs3);
}

// Test: Event dispatch latency under load
TEST_F(EventDispatcherTest, DispatchLatencyUnder100Microseconds)
{
    // Arrange: Register 10 observers
    MockObserver* observers[10];
    for (int i = 0; i < 10; i++) {
        observers[i] = CreateMockObserver();
        EventDispatcher_RegisterObserver(dispatcher, (IEventObserver*)observers[i]);
    }
    
    // Act: Trigger 1000 interrupts, measure latency
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    
    for (int i = 0; i < 1000; i++) {
        mockHardware->Vtbl->TriggerInterrupt(mockHardware, ICR_TXSW);
        ExecuteMockDpc(dispatcher->Dpc);
    }
    
    QueryPerformanceCounter(&end);
    
    // Assert: Average latency <100µs per event
    double totalSeconds = (double)(end.QuadPart - start.QuadPart) / frequency.QuadPart;
    double avgLatencyUs = (totalSeconds / 1000.0) * 1000000.0;
    
    EXPECT_LT(avgLatencyUs, 100.0);  // <100µs average
    
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(observers[i]->EventCount, 1000);
        DestroyMockObserver(observers[i]);
    }
}

// Test: Observer fault isolation (one observer crashes)
TEST_F(EventDispatcherTest, FaultedObserverIsolated)
{
    // Arrange: Register 2 observers (1 faulty, 1 healthy)
    MockObserver* healthyObs = CreateMockObserver();
    MockObserver* faultyObs = CreateMockObserver();
    faultyObs->ThrowExceptionOnEvent = true;  // Simulate crash
    
    EventDispatcher_RegisterObserver(dispatcher, (IEventObserver*)healthyObs);
    EventDispatcher_RegisterObserver(dispatcher, (IEventObserver*)faultyObs);
    
    // Act: Trigger interrupt (faulty observer will crash)
    mockHardware->Vtbl->TriggerInterrupt(mockHardware, ICR_TXSW);
    ExecuteMockDpc(dispatcher->Dpc);
    
    // Assert: Healthy observer still received event
    EXPECT_EQ(healthyObs->EventCount, 1);
    
    // Faulty observer was removed from list
    // Trigger second interrupt, only healthy observer receives it
    mockHardware->Vtbl->TriggerInterrupt(mockHardware, ICR_TXSW);
    ExecuteMockDpc(dispatcher->Dpc);
    
    EXPECT_EQ(healthyObs->EventCount, 2);  // Received second event
    EXPECT_EQ(faultyObs->EventCount, 0);   // Didn't receive second event (removed)
    
    DestroyMockObserver(healthyObs);
    DestroyMockObserver(faultyObs);
}
```

---

## Validation Criteria

### 1. Test Setup Simplicity
- ✅ **Setup Time**: <1 hour to write new test using mock
- ✅ **Boilerplate**: Minimal setup code (3-5 lines)
- ✅ **Readability**: Tests are self-documenting (Arrange-Act-Assert pattern)

### 2. Determinism and Isolation
- ✅ **Repeatability**: Tests produce same results every run (no race conditions)
- ✅ **Isolation**: No cross-test interference (independent mock instances)
- ✅ **Parallelization**: Tests can run concurrently on different threads

### 3. Execution Speed
- ✅ **Unit Tests**: <5 seconds for 50+ tests
- ✅ **Integration Tests**: <30 seconds for 10+ tests
- ✅ **CI/CD Pipeline**: <2 minutes total (build + test)

### 4. Code Coverage
- ✅ **Line Coverage**: ≥80% for event subsystem
- ✅ **Branch Coverage**: ≥75% for error handling paths
- ✅ **Critical Paths**: 100% coverage for ISR/DPC/EmitEvent

### 5. Mock Complexity
- ✅ **LOC**: <500 lines for mock implementation
- ✅ **Maintainability**: Easy to extend (add new register behaviors)
- ✅ **Fidelity**: Simulates realistic hardware behavior (timing, state transitions)

---

## Test Cases

### TC-1: Basic Event Delivery
**Goal**: Verify observer receives event after interrupt  
**Steps**:
1. Create mock hardware + event dispatcher
2. Register mock observer
3. Trigger interrupt (set ICR.TXSW bit)
4. Execute DPC
5. Verify observer received 1 event

**Expected**: Observer callback invoked with correct event type and timestamp

---

### TC-2: Multiple Observers
**Goal**: Verify all observers receive same event  
**Steps**:
1. Register N observers (N = 10)
2. Trigger interrupt
3. Execute DPC
4. Verify all observers received event

**Expected**: All observers see identical timestamp, N callbacks invoked

---

### TC-3: Performance Benchmark
**Goal**: Measure event dispatch latency  
**Steps**:
1. Register 10 observers
2. Trigger 1000 interrupts
3. Measure total time (QPC)
4. Calculate average latency

**Expected**: <100µs per event (including 10 observer callbacks)

---

### TC-4: Observer Fault Isolation
**Goal**: Verify faulty observer doesn't crash system  
**Steps**:
1. Register 1 healthy + 1 faulty observer (throws exception)
2. Trigger interrupt
3. Faulty observer crashes, dispatcher catches exception
4. Trigger second interrupt
5. Verify healthy observer still receives events

**Expected**: Healthy observer receives both events, faulty observer removed after first

---

### TC-5: Schema Versioning
**Goal**: Test v1/v2 translation logic  
**Steps**:
1. Create mock observer requesting v1 schema
2. Trigger interrupt (kernel populates v2 event)
3. Verify observer receives translated v1 event (48 bytes, not 64)

**Expected**: Observer receives v1 event with only v1 fields populated

---

## Risk Analysis

### Risk 1: Mock Doesn't Match Real Hardware Behavior
**Likelihood**: Medium  
**Impact**: Medium (tests pass but production fails)  
**Mitigation**:
- Cross-validate mock behavior against real hardware (hardware-in-the-loop tests)
- Document assumptions in mock implementation
- Run integration tests on real NICs weekly

### Risk 2: Deterministic Tests Hide Race Conditions
**Likelihood**: High  
**Impact**: High (concurrency bugs in production)  
**Mitigation**:
- Add stress tests with async DPC execution (thread pool)
- Use Driver Verifier to detect race conditions
- Run tests under various thread counts (1, 4, 16 cores)

### Risk 3: Test Setup Complexity Grows Over Time
**Likelihood**: Medium  
**Impact**: Low (slower test development)  
**Mitigation**:
- Provide test fixtures (SetUp/TearDown common boilerplate)
- Create helper functions (RegisterMockObserver, TriggerAndExecute)
- Refactor tests quarterly to reduce duplication

### Risk 4: CI/CD Pipeline Timeout (Tests Too Slow)
**Likelihood**: Low  
**Impact**: Medium (slows down development)  
**Mitigation**:
- Parallelize test execution (GoogleTest --gtest_shuffle)
- Optimize mock hardware (reduce unnecessary checks)
- Set timeout: 5 minutes for all tests

### Risk 5: Mock Drift from Real Implementation
**Likelihood**: Medium  
**Impact**: Medium (obsolete tests)  
**Mitigation**:
- Version mock alongside driver (update together)
- Integration tests validate real hardware behavior
- Code review: Ensure mock changes match driver changes

---

## Trade-offs

### User-Mode Mock vs. Kernel-Mode Mock
- **Chosen**: User-mode mock (easier debugging, faster iteration)
- **Alternative**: Kernel-mode mock (exact driver environment)
- **Justification**: Most logic is portable, kernel-mode integration tests for final validation

### Synchronous DPC vs. Asynchronous DPC
- **Chosen**: Synchronous (deterministic tests)
- **Alternative**: Asynchronous (realistic concurrency)
- **Justification**: Deterministic tests easier to debug, stress tests cover async scenarios

### Mock All Registers vs. Selective Mock
- **Chosen**: Selective mock (only TXSTMPL, TXSTMPH, ICR, TSICR)
- **Alternative**: Full 128 KB register bank
- **Justification**: Faster, simpler, covers 95% of use cases

---

## Traceability

### Requirements Verified
- **#68** (Event Logging): Unit tests validate event delivery
- **#53** (Error Handling): Unit tests validate fault isolation
- **#19** (Ring Buffer): Unit tests validate ring buffer writes

### Architecture Decisions Satisfied
- **#147** (Observer Pattern): Unit tests validate observer registration/dispatch
- **#93** (Lock-Free Ring Buffer): Unit tests validate lock-free operations
- **#91** (BAR0 MMIO Access): Mock hardware abstracts MMIO access

### Architecture Views Referenced
- [Logical View - Event Subsystem](../views/logical/logical-view-event-subsystem.md): Testable observer pattern
- [Development View - Module Structure](../views/development/development-view-module-structure.md): Test harness architecture

---

## Implementation Checklist

- [ ] **Phase 1**: Define hardware abstraction interface (~2 hours)
  - `IHardwareAbstraction` interface
  - Real implementation (`I225Hardware`)
  - Test: Verify interface works with real BAR0

- [ ] **Phase 2**: Implement mock hardware (~4 hours)
  - `MockHardware` with register bank
  - `TriggerInterrupt` method
  - Test: Verify register reads/writes

- [ ] **Phase 3**: Modify EventDispatcher for DI (~2 hours)
  - Accept `IHardwareAbstraction*` in constructor
  - Update DPC to use abstracted hardware
  - Test: Inject real hardware, verify no regressions

- [ ] **Phase 4**: Create mock observer (~2 hours)
  - `MockObserver` with event counter
  - Configurable fault injection (throw exception)
  - Test: Verify observer callback invoked

- [ ] **Phase 5**: Write unit tests (~8 hours)
  - TC-1: Basic event delivery
  - TC-2: Multiple observers
  - TC-3: Performance benchmark
  - TC-4: Fault isolation
  - TC-5: Schema versioning

- [ ] **Phase 6**: Integrate with CI/CD (~2 hours)
  - Add GoogleTest runner to CMakeLists.txt
  - Configure GitHub Actions to run tests
  - Set up code coverage reporting

**Estimated Total**: ~20 hours

---

## Acceptance Criteria

| Criterion | Threshold | Measurement |
|-----------|-----------|-------------|
| Test setup time | <1 hour | Time to write new test using mock |
| Determinism | 100% | Tests produce same results every run |
| Test execution time | <5 seconds | Run all unit tests (50+ tests) |
| Code coverage | ≥80% | Line coverage for event subsystem |
| Test isolation | 100% | No cross-test interference |
| CI/CD integration | Automated | Tests run on every PR |
| Mock complexity | <500 LOC | Mock implementation size |

---

**Scenario Status**: Ready for implementation  
**Next Steps**: Create GitHub issue linking to Requirements #68, #53, #19
