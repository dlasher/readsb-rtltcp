# Threading and Race Conditions Task List

## Priority 0: Stability (Prevent Crashes)

### 1. Aircraft Hash Table Race (Architectural Hardening)
- **Location**: `aircraft.c:282-288`
- **Issue**: Hash table insertion lock commented out, causing corrupted linked lists under multi-threaded decode.
- **Architectural Fix**: Implement **Per-Bucket (Striped) Locking** instead of a global mutex to prevent performance serialization of decode threads.
- **Tasks**:
  - [x] Define a set of bucket-specific mutexes (lock striping).
  - [x] Replace the commented-out global lock with bucket-level lock acquisition.
  - [x] Verify that only the relevant bucket is locked during insertion/lookup.
  - [x] Test with high-concurrency multi-threaded scenarios to ensure no corruption and improved throughput.

### 2. Non-atomic Exit Flags
- **Location**: `readsb.h:581-582`
- **Issue**: `Modes.exit`/`Modes.exitSoon` use `volatile` not `atomic_int`.
- **Fix**: Standardize on `stdatomic.h` + Signal Pattern to ensure visibility across cores.
- **Tasks**:
  - [x] Change type of `Modes.exit` and `Modes.exitSoon` to `atomic_int`.
  - [x] Replace all accesses with `atomic_load` and `atomic_store`.
  - [x] Pair with `eventfd` or similar mechanism to wake sleeping threads immediately on shutdown.
  - [x] Verify clean and immediate shutdown across all worker threads.

### 3. RTL-TCP Socket Race
- **Location**: `sdr_rtlsdr.c`
- **Issue**: Shared socket variable accessed from multiple threads without synchronization.
- **Fix**: Encapsulate socket in a thread-safe handle and use `Modes.sdrControlMutex` to wrap all `RTLSDR` struct accesses.
- **Tasks**:
  - [x] Identify all shared socket accesses in `sdr_rtlsdr.c`.
  - [x] Implement wrapping of socket operations using `Modes.sdrControlMutex`.
  - [x] Ensure atomic handover of socket handles during reconnection.
  - [x] Verify stability during network flapping/reconnect cycles.

## Priority 1: Correctness (Prevent Deadlocks)

### 4. Lock Hierarchy Audit
- **Issue**: Potential for deadlocks if locks are acquired out of order.
- **Task**:
  - [ ] Document the definitive lock order based on `readsb.c:1284-1290`.
  - [ ] Grep the entire codebase to identify any functions acquiring locks in a different order.
  - [ ] Refactor out-of-order lock acquisitions to match the documented hierarchy.

## Priority 2: Performance (Reduce Latency)

### 5. Global State Atomic Migration
- **Location**: `struct _Modes`
- **Issue**: Shared counters/flags (e.g., `messageRate`, `total_aircraft_count`) accessed without synchronization.
- **Task**:
  - [ ] Identify all shared counters and flags in `struct _Modes`.
  - [ ] Convert these fields to `atomic_int` or `atomic_size_t`.
  - [ ] Replace non-atomic increments/decrements with `atomic_fetch_add` and `atomic_fetch_sub`.

### 6. Remove "Stop-the-World" Dependency
- **Issue**: `lockThreads()` creates massive latency spikes and is a high-risk deadlock pattern.
- **Task**:
  - [ ] Refactor `priorityTasksRun` and `trackRemoveStale` to use granular locks instead of `lockThreads()`.
  - [ ] Verify that "stop-the-world" events are eliminated from the hot path.

## Priority 3: Hardening (Maximize Throughput)

### 7. Lock-Free SDR FIFO
- **Issue**: `lockReader()`/`unlockReader()` creates contention in the high-frequency `rtlsdrCallback`.
- **Task**:
  - [ ] Replace the current reader lock mechanism with a lock-free Single-Producer Multi-Consumer (SPMC) ring buffer.
  - [ ] Measure throughput increase in the SDR sample pipeline.

### 8. Lock-Free Aircraft Lookups
- **Task**:
  - [ ] Research and implement lock-free read paths for the aircraft hash table for non-modifying queries.
  - [ ] Verify correctness and measure latency reduction for API calls.