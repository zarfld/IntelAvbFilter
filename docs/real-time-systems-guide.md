# Real-Time Systems Programming Guide

**Phase**: 04-Design (Architecture) and 05-Implementation  
**Standards**: ISO/IEC/IEEE 12207:2017, IEC 61508 (Safety-Critical Systems)  
**Purpose**: Achieve predictability, low latency, and deterministic execution in time-critical systems  
**XP Integration**: TDD with temporal validation, Empirical proof through measurement

## 🎯 Overview

Real-time systems demand **temporal correctness** in addition to logical correctness. Meeting a deadline is as critical as computing the correct result. This guide provides instructions for building systems where timing guarantees are essential.

**Core Principle**: Replace vague timing aspirations with **measurable, provable temporal constraints**.

## 📋 Real-Time System Types

### Hard Real-Time Systems

**Definition**: Missing a deadline causes **system failure or catastrophe**.

**Examples**:
- Aircraft flight control systems
- Medical device controllers (pacemakers, infusion pumps)
- Automotive safety systems (anti-lock brakes, airbag deployment)
- Industrial safety interlocks

**Requirements**:
- ✅ Timing guarantees in **ALL** circumstances
- ✅ Worst-case execution time (WCET) analysis
- ✅ Formal verification of temporal constraints
- ✅ No unbounded operations

### Soft Real-Time Systems

**Definition**: Missing a deadline causes **degraded but acceptable performance**.

**Examples**:
- Video streaming (dropped frames tolerable)
- Audio processing (occasional glitches acceptable)
- Network routing (delayed packets acceptable)
- User interface responsiveness

**Requirements**:
- ✅ Statistical timing guarantees (e.g., 95% meet deadline)
- ✅ Graceful degradation under load
- ✅ Prioritization mechanisms
- ✅ Bounded response time

## I. Define and Architect for Temporal Constraints

### 1. State Requirements in Measurable Terms

**Anti-Pattern (Vague)**:
```markdown
❌ "The system shall respond quickly"
❌ "Operator shall not have to wait"
❌ "Real-time performance required"
```

**Best Practice (Measurable)**:
```markdown
✅ "95% of transactions shall complete in <100ms"
✅ "Emergency stop signal shall be processed within 5ms (worst case)"
✅ "Display refresh rate shall maintain 60fps (16.67ms frame time)"
✅ "Sensor sampling shall occur at exactly 1kHz (±10µs jitter)"
```

**Requirements Template**:
```markdown
## Temporal Requirement: [REQ-RT-XXX]

**Operation**: [Describe the operation]
**Deadline**: [Time value with units]
**Tolerance**: [Acceptable jitter/variance]
**Guarantee Level**: 
- [ ] Hard (100% must meet deadline)
- [ ] Soft (statistical, specify percentile)

**Measurement Method**: [How to verify]
**Failure Impact**: [What happens if deadline missed]
**Priority**: [Relative priority vs other operations]

**Traceability**: #[GitHub issue]
```

**Example Implementation**:
```markdown
## Temporal Requirement: REQ-RT-EMSTOP-001

**Operation**: Emergency stop button processing
**Deadline**: 5ms maximum from button press to motor shutdown
**Tolerance**: ±0ms (hard deadline)
**Guarantee Level**: Hard (100% must meet deadline)

**Measurement Method**: 
- GPIO toggle + oscilloscope measurement
- Automated test with timestamp logging

**Failure Impact**: 
- CRITICAL: Potential safety hazard
- Machine may not stop in time to prevent injury

**Priority**: P0 (highest)

**Traceability**: #456 (REQ-F-SAFETY-001)
```

### 2. Determine Real-Time Type and Architecture

**Decision Tree**:
```
Can system tolerate missed deadlines?
├─ NO → Hard Real-Time
│   ├─ Use time-frame-based architecture
│   ├─ Prove WCET for all operations
│   └─ Formal verification required
└─ YES → Soft Real-Time
    ├─ Define acceptable miss rate
    ├─ Implement priority scheduling
    └─ Statistical testing sufficient
```

### 3. Define Runtime Limits for Priority Classes

**Priority Class Structure**:

```cpp
/**
 * Real-Time Priority Classes
 * 
 * Temporal Requirements:
 * - High Priority ISR: <5µs execution time
 * - Low Priority ISR: <50µs execution time  
 * - High Priority Task: <1ms execution time
 * - Low Priority Task: <10ms execution time
 */
enum class PriorityClass {
  HIGH_PRIORITY_ISR,    // Emergency stop, safety-critical
  LOW_PRIORITY_ISR,     // Sensor sampling, communication
  HIGH_PRIORITY_TASK,   // Control loop, real-time processing
  LOW_PRIORITY_TASK,    // Logging, diagnostics, UI
  BACKGROUND_TASK       // Non-critical housekeeping
};

/**
 * Runtime Limits (enforced by runtime monitoring)
 */
struct RuntimeLimits {
  static constexpr auto HIGH_ISR_TARGET = 3us;
  static constexpr auto HIGH_ISR_MAX = 5us;
  
  static constexpr auto LOW_ISR_TARGET = 30us;
  static constexpr auto LOW_ISR_MAX = 50us;
  
  static constexpr auto HIGH_TASK_TARGET = 500us;
  static constexpr auto HIGH_TASK_MAX = 1ms;
  
  static constexpr auto LOW_TASK_TARGET = 5ms;
  static constexpr auto LOW_TASK_MAX = 10ms;
};
```

### 4. Use Time-Frame-Based Architecture

**Time-Frame Scheduling**: Divide time into fixed-length frames for rigorous event ordering.

**Implementation**:

```cpp
/**
 * Time-Frame-Based Scheduler
 * 
 * Frame Structure: 1ms main frame divided into 4 × 250µs subframes
 * 
 * Temporal Guarantee: Each task runs exactly once per frame at fixed offset
 */
class TimeFrameScheduler {
public:
  static constexpr auto FRAME_DURATION = 1ms;
  static constexpr auto SUBFRAME_DURATION = 250us;
  static constexpr size_t SUBFRAMES_PER_FRAME = 4;
  
  struct ScheduledTask {
    void (*function)();
    uint8_t subframe_offset;  // Which subframe (0-3)
    uint16_t execution_order; // Order within subframe
  };
  
  void tick() {
    current_time_ += SUBFRAME_DURATION;
    current_subframe_ = (current_subframe_ + 1) % SUBFRAMES_PER_FRAME;
    
    // Execute all tasks scheduled for this subframe
    for (const auto& task : tasks_) {
      if (task.subframe_offset == current_subframe_) {
        const auto start = get_timestamp();
        task.function();
        const auto duration = get_timestamp() - start;
        
        // Verify temporal constraint
        assert(duration < SUBFRAME_DURATION);
      }
    }
  }
  
private:
  uint8_t current_subframe_{0};
  std::chrono::microseconds current_time_{0};
  std::array<ScheduledTask, MAX_TASKS> tasks_;
};
```

**Benefits**:
- ✅ **Predictable execution order** (no race conditions)
- ✅ **Bounded latency** (tasks run at fixed intervals)
- ✅ **Deterministic behavior** (same sequence every frame)
- ✅ **Easy analysis** (can prove timing via simple arithmetic)

### 5. Isolate Hardware with Classes

**Hardware Abstraction Layer (HAL)**: Insulate software from low-level hardware details.

```cpp
/**
 * System Timer HAL
 * 
 * Isolates software from MCU-specific timer peripheral
 * Provides virtual machine parallel to problem domain
 */
class SystemTimer {
public:
  using microseconds = std::chrono::microseconds;
  using milliseconds = std::chrono::milliseconds;
  
  // Virtual interface (problem domain)
  static void initialize(microseconds tick_period);
  static microseconds get_elapsed_time();
  static void register_callback(void (*callback)(), milliseconds interval);
  
private:
  // Hardware-specific implementation (hidden)
  static void configure_timer_peripheral();
  static void enable_timer_interrupt();
};

/**
 * Usage (no hardware knowledge needed)
 */
void application_code() {
  SystemTimer::initialize(1ms);
  
  SystemTimer::register_callback([]() {
    // Control loop at 1kHz
    update_control_algorithm();
  }, 1ms);
  
  auto elapsed = SystemTimer::get_elapsed_time();
}
```

### 6. Implement Robust System-Tick Timebase

**System Tick**: Fundamental timebase for entire software system.

```cpp
/**
 * System Tick Implementation
 * 
 * Provides:
 * - High-resolution timebase for std::chrono
 * - Interrupt-safe timestamp capture
 * - Microsecond precision
 */
class SystemTick {
public:
  using duration = std::chrono::microseconds;
  using time_point = std::chrono::time_point<SystemTick, duration>;
  
  static constexpr bool is_steady = true;
  
  static void initialize() {
    // Configure hardware timer for 1µs resolution
    configure_timer(1'000'000); // 1MHz clock
    enable_interrupt();
  }
  
  static time_point now() noexcept {
    // Interrupt-safe timestamp capture
    const auto guard = interrupt_guard();
    return time_point(duration(tick_counter_));
  }
  
  // Interrupt Service Routine
  static void ISR_timer_tick() {
    ++tick_counter_;
  }
  
private:
  static inline std::atomic<uint64_t> tick_counter_{0};
};

// Integrate with std::chrono
using high_resolution_clock = SystemTick;
using steady_clock = SystemTick;

/**
 * Usage in Application Code
 */
void measure_execution_time() {
  const auto start = high_resolution_clock::now();
  
  perform_operation();
  
  const auto end = high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  // Verify temporal constraint
  assert(duration < 100us);
}
```

## II. Implementation Instructions for Speed and Predictability

### 7. Hold Interrupt Service Routines Terse and Efficient

**ISR Golden Rules**:
- ✅ Keep ISRs under 5µs (hard real-time) or 50µs (soft real-time)
- ✅ No blocking operations (malloc, printf, mutex locks)
- ✅ No external function calls (except inline)
- ✅ Minimal stack usage
- ✅ Defer complex work to task level

**Anti-Pattern (Bloated ISR)**:
```cpp
// ❌ BAD: Complex logic in ISR
void USART_RX_IRQHandler() {
  char c = USART->DR;
  
  // Parse incoming message
  if (c == START_BYTE) {
    rx_state = RECEIVING_HEADER;
  } else if (rx_state == RECEIVING_HEADER) {
    parse_header(c);  // External function call!
  } else if (rx_state == RECEIVING_DATA) {
    rx_buffer[rx_index++] = c;
    if (rx_index >= rx_length) {
      process_message(rx_buffer);  // Blocking operation!
      rx_state = IDLE;
    }
  }
  
  // Clear interrupt flag
  USART->SR &= ~USART_SR_RXNE;
}
```

**Best Practice (Terse ISR)**:
```cpp
// ✅ GOOD: Minimal ISR, defer work to task
void USART_RX_IRQHandler() {
  // Read data (must do in ISR to prevent overrun)
  const uint8_t data = USART->DR;
  
  // Store in lock-free queue for task-level processing
  rx_queue.push_from_isr(data);
  
  // Signal task to process queue
  rx_semaphore.give_from_isr();
  
  // Clear interrupt flag
  USART->SR &= ~USART_SR_RXNE;
}

// Task-level processing (can be preempted, uses more time)
void rx_processing_task() {
  while (true) {
    rx_semaphore.take();  // Block until data available
    
    while (!rx_queue.empty()) {
      const uint8_t data = rx_queue.pop();
      message_parser.process_byte(data);  // Complex logic here
    }
  }
}
```

**Measurement**:
```cpp
// Instrument ISR execution time
void USART_RX_IRQHandler() {
  GPIO_SET(DEBUG_PIN);  // Set pin high at ISR entry
  
  // ISR code here
  
  GPIO_CLEAR(DEBUG_PIN);  // Set pin low at ISR exit
}

// Measure with oscilloscope:
// - Pulse width = ISR execution time
// - Pulse-to-pulse variance = jitter
```

### 8. Avoid Blocking Calls and Design Short Processes

**Anti-Pattern (Blocking)**:
```cpp
// ❌ BAD: Blocking call holds CPU for milliseconds
void update_display() {
  display.clear();  // Blocks for 2ms
  display.write_string("Temperature: ");  // Blocks for 5ms
  display.write_number(temperature);  // Blocks for 3ms
  // Total: 10ms blocking time!
}
```

**Best Practice (Non-Blocking State Machine)**:
```cpp
// ✅ GOOD: State machine processes in small chunks
class DisplayUpdater {
  enum class State { IDLE, CLEARING, WRITING_LABEL, WRITING_VALUE };
  
  State state_{State::IDLE};
  std::string pending_text_;
  
public:
  void start_update(float temperature) {
    pending_text_ = format("Temperature: {:.1f}", temperature);
    state_ = State::CLEARING;
  }
  
  // Called every 1ms (non-blocking)
  void tick() {
    switch (state_) {
      case State::IDLE:
        // Nothing to do
        break;
        
      case State::CLEARING:
        if (display.clear_async()) {  // Returns immediately
          state_ = State::WRITING_LABEL;
        }
        break;
        
      case State::WRITING_LABEL:
        if (display.write_char_async(pending_text_[char_index_++])) {
          if (char_index_ >= pending_text_.size()) {
            state_ = State::IDLE;
          }
        }
        break;
    }
  }
};
```

### 9. Prefer Integer Math Over Floating-Point (No FPU)

**Motivation**: Software floating-point emulation can be 100× slower than integer math.

**Anti-Pattern (Floating-Point)**:
```cpp
// ❌ SLOW: Floating-point without FPU
float apply_low_pass_filter(float input) {
  static float y_prev = 0.0f;
  const float alpha = 0.1f;  // Cutoff frequency
  
  float y = alpha * input + (1.0f - alpha) * y_prev;
  y_prev = y;
  
  return y;
}
// Execution time: ~500µs (software FP emulation)
```

**Best Practice (Fixed-Point Integer)**:
```cpp
// ✅ FAST: Fixed-point integer math
int32_t apply_low_pass_filter(int32_t input) {
  static int32_t y_prev = 0;
  
  // alpha = 0.1 = 6554 / 65536 (Q16 fixed-point)
  constexpr int32_t ALPHA_Q16 = 6554;
  constexpr int32_t ONE_MINUS_ALPHA_Q16 = 65536 - ALPHA_Q16;
  
  // Fixed-point multiplication with proper rounding
  int32_t y = ((ALPHA_Q16 * input) + (ONE_MINUS_ALPHA_Q16 * y_prev)) >> 16;
  y_prev = y;
  
  return y;
}
// Execution time: ~5µs (pure integer)
```

**Fixed-Point Template**:
```cpp
/**
 * Fixed-Point Number Template
 * 
 * Q: Fractional bits
 * Example: Q16 means 16.16 fixed-point
 */
template<size_t Q>
class FixedPoint {
  static constexpr int32_t ONE = 1 << Q;
  int32_t value_;
  
public:
  static FixedPoint from_float(float f) {
    return FixedPoint{static_cast<int32_t>(f * ONE)};
  }
  
  float to_float() const {
    return static_cast<float>(value_) / ONE;
  }
  
  FixedPoint operator*(const FixedPoint& rhs) const {
    return FixedPoint{(value_ * rhs.value_) >> Q};
  }
  
  FixedPoint operator+(const FixedPoint& rhs) const {
    return FixedPoint{value_ + rhs.value_};
  }
};
```

### 10. Minimize Computational Overhead

**Bound Iteration Counts**:
```cpp
// ❌ BAD: Unbounded iteration (worst case unknown)
float calculate_median(const std::vector<float>& data) {
  auto sorted = data;
  std::sort(sorted.begin(), sorted.end());  // O(n log n), unbounded
  return sorted[sorted.size() / 2];
}

// ✅ GOOD: Bounded iteration (worst case known)
float calculate_median_bounded(const std::array<float, 16>& data) {
  std::array<float, 16> sorted = data;
  
  // Insertion sort: O(n²) but bounded at 16 elements
  for (size_t i = 1; i < sorted.size(); ++i) {
    // Max 15 iterations outer, max 15 inner = 225 comparisons worst case
    float key = sorted[i];
    size_t j = i;
    while (j > 0 && sorted[j - 1] > key) {
      sorted[j] = sorted[j - 1];
      --j;
    }
    sorted[j] = key;
  }
  
  return sorted[8];  // Middle element
}
```

**Eliminate Indirection**:
```cpp
// ❌ SLOWER: Multiple indirections
class SensorReader {
  ISensorDriver* driver_;  // Virtual dispatch
  
public:
  float read_temperature() {
    auto raw = driver_->read_raw();  // Virtual call
    return converter_->convert(raw);  // Another virtual call
  }
};

// ✅ FASTER: Direct access in critical path
class SensorReaderOptimized {
  SPI_TypeDef* spi_peripheral_;  // Direct hardware access
  
public:
  float read_temperature() {
    // Inline hardware access (no virtual dispatch)
    spi_peripheral_->DR = READ_TEMP_CMD;
    while (!(spi_peripheral_->SR & SPI_SR_RXNE));
    uint16_t raw = spi_peripheral_->DR;
    
    // Inline conversion (compiler can optimize)
    return static_cast<float>(raw) * 0.0625f - 50.0f;
  }
};
```

### 11. Use Compile-Time Constants

**constexpr for Register Addresses**:
```cpp
// ❌ WORSE: Preprocessor macros (no type safety)
#define GPIOA_BASE 0x40020000
#define GPIOA_ODR (*(volatile uint32_t*)(GPIOA_BASE + 0x14))

// ✅ BETTER: constexpr (type-safe, compile-time)
namespace registers {
  constexpr uintptr_t GPIOA_BASE = 0x40020000;
  constexpr volatile uint32_t& GPIOA_ODR = 
    *reinterpret_cast<volatile uint32_t*>(GPIOA_BASE + 0x14);
}

// Usage (same efficiency as macro, but type-safe)
void set_pin_high() {
  registers::GPIOA_ODR |= (1 << 5);
}
```

**constexpr for Configuration**:
```cpp
// Compile-time configuration (zero runtime overhead)
struct SystemConfig {
  static constexpr uint32_t CPU_FREQ_HZ = 168'000'000;
  static constexpr auto SYSTEM_TICK_PERIOD = 1ms;
  static constexpr uint32_t UART_BAUD_RATE = 115'200;
  
  // Computed at compile time
  static constexpr uint32_t UART_BRR = 
    (CPU_FREQ_HZ + UART_BAUD_RATE / 2) / UART_BAUD_RATE;
};
```

### 12. Choose Appropriate Polymorphism

**Static Polymorphism for Time-Critical Code**:
```cpp
/**
 * Static Polymorphism (Templates)
 * 
 * Use for: ISRs, high-frequency callbacks, inner loops
 * Benefit: Zero runtime overhead, inlining possible
 */
template<typename SensorDriver>
class TemperatureController {
  SensorDriver sensor_;  // Compile-time type resolution
  
public:
  void update() {
    // Direct call (can be inlined)
    float temp = sensor_.read_temperature();
    
    if (temp > setpoint_) {
      turn_on_cooling();
    }
  }
};

// Instantiate with specific driver
TemperatureController<MAX31855Driver> controller;
```

**Dynamic Polymorphism for Application Layer**:
```cpp
/**
 * Dynamic Polymorphism (Virtual Functions)
 * 
 * Use for: Application logic, UI, non-critical paths
 * Benefit: Flexibility, extensibility
 */
class IDisplayDriver {
public:
  virtual ~IDisplayDriver() = default;
  virtual void draw_text(const std::string& text) = 0;
  virtual void draw_graph(const std::vector<float>& data) = 0;
};

class UserInterface {
  std::unique_ptr<IDisplayDriver> display_;  // Runtime polymorphism
  
public:
  void update() {
    // Virtual call acceptable in UI code (not time-critical)
    display_->draw_text(get_status_message());
    display_->draw_graph(get_trend_data());
  }
};
```

## III. Concurrency and Synchronization

### 13. Model Concurrency Explicitly

**Concurrent Design Principles**:
```cpp
/**
 * Explicit Concurrency Model
 * 
 * Components designed as if executing concurrently
 * Avoids inessential timing dependencies
 */

// ❌ BAD: Assumes specific execution order
class SensorManager {
  void update() {
    read_sensor_a();
    read_sensor_b();  // Assumes A completes before B
    calculate_result();  // Assumes both complete before this
  }
};

// ✅ GOOD: Explicit synchronization
class SensorManagerConcurrent {
  std::atomic<bool> sensor_a_ready_{false};
  std::atomic<bool> sensor_b_ready_{false};
  
  void on_sensor_a_complete() {
    sensor_a_ready_ = true;
    check_if_ready_to_calculate();
  }
  
  void on_sensor_b_complete() {
    sensor_b_ready_ = true;
    check_if_ready_to_calculate();
  }
  
  void check_if_ready_to_calculate() {
    if (sensor_a_ready_ && sensor_b_ready_) {
      calculate_result();
      sensor_a_ready_ = false;
      sensor_b_ready_ = false;
    }
  }
};
```

### 14. Use Exclusive Access Mechanisms

**Mutex for Shared Resources**:
```cpp
/**
 * Shared Resource Protection
 */
class SharedBuffer {
  std::array<uint8_t, 256> buffer_;
  size_t write_index_{0};
  mutable std::mutex mutex_;
  
public:
  void write(const uint8_t* data, size_t length) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (size_t i = 0; i < length; ++i) {
      buffer_[write_index_++] = data[i];
      if (write_index_ >= buffer_.size()) {
        write_index_ = 0;
      }
    }
  }
  
  std::vector<uint8_t> read_all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<uint8_t>(buffer_.begin(), buffer_.begin() + write_index_);
  }
};
```

**Lock-Free Queue for ISR-Task Communication**:
```cpp
/**
 * Lock-Free Single-Producer Single-Consumer Queue
 * 
 * Safe for ISR→Task communication (no locks needed)
 */
template<typename T, size_t N>
class LockFreeQueue {
  std::array<T, N> buffer_;
  std::atomic<size_t> read_index_{0};
  std::atomic<size_t> write_index_{0};
  
public:
  bool push_from_isr(const T& item) {
    const size_t current_write = write_index_.load(std::memory_order_relaxed);
    const size_t next_write = (current_write + 1) % N;
    
    if (next_write == read_index_.load(std::memory_order_acquire)) {
      return false;  // Queue full
    }
    
    buffer_[current_write] = item;
    write_index_.store(next_write, std::memory_order_release);
    return true;
  }
  
  bool pop(T& item) {
    const size_t current_read = read_index_.load(std::memory_order_relaxed);
    
    if (current_read == write_index_.load(std::memory_order_acquire)) {
      return false;  // Queue empty
    }
    
    item = buffer_[current_read];
    read_index_.store((current_read + 1) % N, std::memory_order_release);
    return true;
  }
};
```

### 15. Adopt Command or Active Object Patterns

**Command Pattern for Asynchronous Execution**:
```cpp
/**
 * Command Pattern
 * 
 * Package computation for later execution (possibly by different thread)
 */
class ICommand {
public:
  virtual ~ICommand() = default;
  virtual void execute() = 0;
};

class SensorReadCommand : public ICommand {
  ISensorDriver& sensor_;
  std::function<void(float)> callback_;
  
public:
  SensorReadCommand(ISensorDriver& sensor, std::function<void(float)> callback)
    : sensor_(sensor), callback_(std::move(callback)) {}
    
  void execute() override {
    float value = sensor_.read();
    callback_(value);
  }
};

// Scheduler executes commands asynchronously
class CommandScheduler {
  std::queue<std::unique_ptr<ICommand>> commands_;
  std::mutex mutex_;
  
public:
  void schedule(std::unique_ptr<ICommand> command) {
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.push(std::move(command));
  }
  
  void execute_pending() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!commands_.empty()) {
      commands_.front()->execute();
      commands_.pop();
    }
  }
};
```

**Active Object Pattern**:
```cpp
/**
 * Active Object Pattern
 * 
 * Object with own thread of control for asynchronous method invocation
 */
class DataLogger {
  std::queue<LogEntry> pending_logs_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_thread_;
  std::atomic<bool> running_{true};
  
public:
  DataLogger() {
    worker_thread_ = std::thread([this]() { worker_loop(); });
  }
  
  ~DataLogger() {
    running_ = false;
    cv_.notify_one();
    worker_thread_.join();
  }
  
  // Asynchronous method (returns immediately)
  void log_async(const LogEntry& entry) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_logs_.push(entry);
    }
    cv_.notify_one();
  }
  
private:
  void worker_loop() {
    while (running_) {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return !pending_logs_.empty() || !running_; });
      
      while (!pending_logs_.empty()) {
        auto entry = pending_logs_.front();
        pending_logs_.pop();
        lock.unlock();
        
        // Write to SD card (slow operation)
        write_to_storage(entry);
        
        lock.lock();
      }
    }
  }
};
```

### 16. Select Appropriate Multitasking

**Cooperative Scheduler**:
```cpp
/**
 * Cooperative Multitasking
 * 
 * Advantages: Compact, portable, predictable
 * Disadvantages: Tasks must voluntarily yield
 */
class CooperativeScheduler {
  struct Task {
    void (*function)();
    bool enabled;
  };
  
  std::array<Task, MAX_TASKS> tasks_;
  
public:
  void add_task(void (*function)()) {
    // Find free slot and add task
  }
  
  [[noreturn]] void run() {
    while (true) {
      for (auto& task : tasks_) {
        if (task.enabled) {
          task.function();  // Task must return quickly!
        }
      }
    }
  }
};

// Tasks designed to return quickly
void sensor_task() {
  if (sensor_ready()) {
    read_sensor();  // Returns in <1ms
  }
  // Yield by returning
}
```

**Preemptive RTOS**:
```cpp
/**
 * Preemptive Multitasking (RTOS)
 * 
 * Advantages: True concurrency, priority-based scheduling
 * Disadvantages: Higher overhead, re-entrance issues
 */

// FreeRTOS example
void sensor_task(void* params) {
  while (true) {
    // Block until sensor ready (other tasks can run)
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    
    read_sensor();
    process_data();
    
    // Sleep for 100ms (other tasks can run)
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ISR notifies task
void SENSOR_IRQHandler() {
  BaseType_t higher_priority_woken = pdFALSE;
  vTaskNotifyGiveFromISR(sensor_task_handle, &higher_priority_woken);
  portYIELD_FROM_ISR(higher_priority_woken);
}
```

## IV. Verification and Testing (Empirical Proof)

### 17. Instrument and Monitor CPU Load

**GPIO Toggle for Timing Measurement**:
```cpp
/**
 * Execution Time Measurement via GPIO
 * 
 * Method: Toggle debug pin at start/end of code sequence
 * Tools: Digital oscilloscope or logic analyzer
 */
class ExecutionTimer {
  volatile uint32_t* gpio_bsrr_;  // Set/Reset register
  uint32_t set_mask_;
  uint32_t reset_mask_;
  
public:
  ExecutionTimer(volatile uint32_t* gpio_bsrr, uint8_t pin)
    : gpio_bsrr_(gpio_bsrr),
      set_mask_(1 << pin),
      reset_mask_(1 << (pin + 16)) {}
      
  void start() {
    *gpio_bsrr_ = set_mask_;  // Set pin HIGH
  }
  
  void stop() {
    *gpio_bsrr_ = reset_mask_;  // Set pin LOW
  }
};

// Usage in critical code
ExecutionTimer timer(&GPIOA->BSRR, 5);

void control_loop() {
  timer.start();
  
  // Critical code here
  update_pid_controller();
  
  timer.stop();
}

// Oscilloscope measurements:
// - Pulse width = execution time
// - Pulse period = loop frequency
// - Pulse width variance = jitter
```

**Runtime CPU Load Monitoring**:
```cpp
/**
 * CPU Load Measurement
 */
class CPULoadMonitor {
  std::chrono::microseconds idle_time_{0};
  std::chrono::microseconds total_time_{0};
  std::chrono::steady_clock::time_point last_update_;
  
public:
  void enter_idle() {
    last_update_ = std::chrono::steady_clock::now();
  }
  
  void exit_idle() {
    const auto now = std::chrono::steady_clock::now();
    idle_time_ += std::chrono::duration_cast<std::chrono::microseconds>(now - last_update_);
  }
  
  float get_cpu_load_percent() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      now - measurement_start_);
    
    total_time_ = elapsed;
    
    float load = 100.0f * (1.0f - static_cast<float>(idle_time_.count()) / total_time_.count());
    
    // Reset counters
    idle_time_ = std::chrono::microseconds{0};
    measurement_start_ = now;
    
    return load;
  }
  
private:
  std::chrono::steady_clock::time_point measurement_start_;
};
```

### 18. Build with Automated Tests (TDD)

**Temporal Test Case Template**:
```cpp
/**
 * TDD for Real-Time Systems
 * 
 * Tests verify both logical AND temporal correctness
 */
TEST(ControlLoop, MeetsTiming Deadline) {
  ControlLoop control;
  
  // Arrange
  control.set_target_temperature(25.0f);
  constexpr auto DEADLINE = 1ms;
  
  // Act
  const auto start = high_resolution_clock::now();
  control.update();
  const auto end = high_resolution_clock::now();
  const auto duration = end - start;
  
  // Assert - Logical correctness
  EXPECT_TRUE(control.is_output_valid());
  
  // Assert - Temporal correctness
  EXPECT_LT(duration, DEADLINE) << "Control loop exceeded 1ms deadline";
}

TEST(SensorReader, MaximumJitter) {
  SensorReader sensor;
  
  // Measure jitter over 1000 samples
  std::vector<std::chrono::microseconds> durations;
  durations.reserve(1000);
  
  for (int i = 0; i < 1000; ++i) {
    const auto start = high_resolution_clock::now();
    sensor.read();
    const auto end = high_resolution_clock::now();
    durations.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start));
  }
  
  // Calculate statistics
  const auto [min, max] = std::minmax_element(durations.begin(), durations.end());
  const auto jitter = *max - *min;
  
  // Assert - Jitter must be <10µs
  EXPECT_LT(jitter, 10us) << "Sensor read jitter exceeds specification";
}
```

**Mock Hardware for Unit Testing**:
```cpp
/**
 * Hardware Mock for Testing
 */
class MockTimer : public ISystemTimer {
  std::chrono::microseconds simulated_time_{0};
  
public:
  // Simulate time passage
  void advance_time(std::chrono::microseconds delta) {
    simulated_time_ += delta;
  }
  
  std::chrono::microseconds now() const override {
    return simulated_time_;
  }
};

// Test uses mock instead of real hardware
TEST(PIDController, ResponseTime) {
  MockTimer timer;
  PIDController<MockTimer> pid(timer);
  
  pid.set_setpoint(100.0f);
  
  // Simulate 100ms of operation
  for (int i = 0; i < 100; ++i) {
    pid.update(50.0f);  // Current measurement
    timer.advance_time(1ms);
  }
  
  // Verify settling time <100ms
  EXPECT_NEAR(pid.get_output(), 100.0f, 1.0f);
}
```

### 19. Walking Skeleton for Real-Time Architecture

**Minimal End-to-End Validation**:
```markdown
# Walking Skeleton for Real-Time Motor Controller

## Goal
Prove architecture meets basic temporal requirements before full implementation

## Minimal Functionality
- [ ] System tick at 1kHz (1ms period)
- [ ] Control loop executes at 10kHz (100µs period)
- [ ] Sensor sampling at 1kHz
- [ ] Emergency stop latency <5ms
- [ ] System boots in <500ms

## Implementation Steps
1. Configure system timer (1kHz tick)
2. Implement bare-bones control loop
   - Read one sensor
   - Simple proportional control
   - Output to one motor
3. Implement emergency stop ISR
4. Measure timing with oscilloscope

## Success Criteria
- [ ] All timing measurements within spec
- [ ] No stack overflows
- [ ] No missed deadlines (100 iterations)
- [ ] Jitter <10µs

## Results
- System tick: 1000Hz ± 0.1Hz ✅
- Control loop: 9.95kHz (100.5µs period) ✅
- Emergency stop: 4.2µs latency ✅
- Boot time: 287ms ✅
- Jitter: 3.8µs ✅

## Decision: PROCEED with full implementation
Architecture proven to meet temporal constraints.
```

## ✅ Real-Time Development Checklist

### Requirements Phase
- [ ] Temporal requirements stated in measurable terms
- [ ] Hard vs. soft real-time classification determined
- [ ] Worst-case execution time (WCET) requirements defined
- [ ] Priority classes and runtime limits established

### Architecture Phase
- [ ] Time-frame-based architecture designed (if hard real-time)
- [ ] Hardware abstraction layer planned
- [ ] System tick timebase architecture defined
- [ ] Concurrency model explicitly designed

### Implementation Phase
- [ ] ISRs kept terse (<5µs hard, <50µs soft)
- [ ] No blocking calls in time-critical code
- [ ] Integer math used where FPU unavailable
- [ ] Computational overhead minimized (bounded iterations)
- [ ] Static polymorphism used for critical paths
- [ ] Synchronization mechanisms properly applied

### Verification Phase
- [ ] GPIO instrumentation for timing measurement
- [ ] CPU load monitoring implemented
- [ ] Automated tests verify temporal constraints
- [ ] Walking skeleton validates architecture
- [ ] Jitter measured and within specification
- [ ] Worst-case scenarios tested

## 📚 Integration with Framework

### Standards Compliance
- **ISO/IEC/IEEE 12207:2017**: Implementation Process with temporal validation
- **IEC 61508**: Functional Safety (real-time safety-critical systems)
- **DO-178C**: Airborne software (if applicable)

### XP Integration
- **TDD**: Tests verify timing constraints, not just logic
- **Empirical Proof**: Oscilloscope measurements replace speculation
- **Walking Skeleton**: Proves architecture before full implementation
- **Simple Design**: YAGNI prevents premature optimization

### DDD Integration
- **Domain Model**: Temporal constraints are domain concepts
- **Ubiquitous Language**: Use terms like "deadline," "jitter," "latency"
- **Bounded Context**: Real-time subsystems have explicit temporal boundaries

## 📖 References

- **Related Documents**:
  - `docs/tdd-empirical-proof.md` - Empirical validation practices
  - `docs/ddd-implementation-guide.md` - Domain-driven design patterns
  - `.github/instructions/phase-05-implementation.instructions.md` - Implementation standards

- **External Standards**:
  - IEC 61508 - Functional Safety of Electrical/Electronic Systems
  - DO-178C - Software Considerations in Airborne Systems
  - MISRA C++ - Guidelines for Use of C++ in Critical Systems

---

**Remember: In real-time systems, meeting the deadline IS part of correctness. Prove temporal guarantees empirically through measurement!** ⏱️✅
