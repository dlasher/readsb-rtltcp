# RTL-TCP Client Implementation Task List

## Priority 0: Stability & Safety (Immediate Fixes)

### 1. Thread-Safe Socket Access
- **Location**: `sdr_rtlsdr.c`
- **Issue**: Shared socket variable accessed from multiple threads without synchronization.
- **Task**:
  - [x] Identify all shared socket accesses in `sdr_rtlsdr.c`.
  - [x] Encapsulate socket operations using `Modes.sdrControlMutex`.
  - [x] Ensure atomic handover of socket handles during reconnection cycles.
  - [x] Verify stability during network flapping.

### 2. Interruptible Shutdown & Lifecycle
- **Location**: `sdr_rtlsdr.c`
- **Issue**: `pthread_join()` in `rtlsdrRun()` blocks caller; threads may hang on TCP timeouts during shutdown.
- **Task**:
  - [x] Move `pthread_join` to the global cleanup routine.
  - [x] Implement an interruptible shutdown mechanism (e.g., non-blocking reads or a "self-pipe" trick) so the thread exits immediately when `Modes.exit` is true.
  - [x] Verify non-blocking application shutdown.

## Priority 1: Production-Grade Connectivity

### 3. Robust I/O Management
- **Issue**: Blocking `connect()` and `recv(..., MSG_WAITALL)` can cause application hangs.
- **Task**:
  - [x] Implement `connect()` with a configurable timeout using `select()` or `poll()`.
  - [x] Replace `MSG_WAITALL` with a loop using `SO_RCVTIMEO` and `SO_SNDTIMEO` to detect half-open connections.
  - [x] Enable `SO_KEEPALIVE` to detect dead peers at the OS level.

### 4. Professional Recovery Strategy
- **Issue**: Fixed 5-second reconnect delay is inefficient and risky.
- **Task**:
  - [ ] Implement **Exponential Backoff with Jitter** (e.g., 1s, 2s, 4s... up to 60s).
  - [ ] Implement a **Circuit Breaker** state machine (Closed $\to$ Open $\to$ Half-Open) to stop aggressive retries after $N$ consecutive failures.
  - [ ] Classify errors into transient (retry) and permanent (log and stop) failures.

## Priority 2: Observability & Diagnostics

### 5. Network Stream Metrics
- **Issue**: The RTL-TCP stream is currently a "black box."
- **Task**:
  - [ ] Implement counters for:
    - Total bytes received vs. bytes dropped.
    - Number of reconnection attempts and success rate.
    - Average latency/jitter of incoming sample blocks.
  - [ ] Integrate the RTL-TCP connection state (Connected/Connecting/Failed) into the `readsb` status reporting system.

### 6. Debugging & Logging
- **Task**:
  - [ ] Add a debug mode to log TCP window sizes and specific socket error codes during failures.
  - [ ] Implement "Degraded" status logging when the TCP stream is unstable.

## Priority 3: Resource Hardening

### 7. Buffer Management Audit
- **Issue**: Potential leaks in error paths during socket reconfiguration.
- **Task**:
  - [ ] Audit all allocation points in `sdr_rtlsdr.c`.
  - [ ] Trace all error paths to ensure every `malloc` has a corresponding `free`.
  - [ ] Verify no leaks using Valgrind during reconnect cycles.