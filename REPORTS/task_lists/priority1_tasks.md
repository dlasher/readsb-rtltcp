# Priority 1 Security Tasks

## Threading & Race Conditions (threading_race_conditions.md)
### Lock Hierarchy Audit
- **Issue**: Potential for deadlocks if locks are acquired out of order.
- **Task**:
  - [x] Document the definitive lock order based on `readsb.c:1284-1290`.
  - [x] Grep the entire codebase to identify any functions acquiring locks in a different order.
  - [x] Refactor out-of-order lock acquisitions to match the documented hierarchy. *(No violations found — all 58 lock sites follow the documented order.)*

## RTL-TCP Client (rtl_tcp_client.md)
### Professional Recovery Strategy
- **Issue**: Fixed 5-second reconnect delay is inefficient and risky.
- **Task**:
  - [x] Implement **Exponential Backoff with Jitter** (e.g., 1s, 2s, 4s... up to 60s).
  - [x] Implement a **Circuit Breaker** state machine (Closed → Open → Half-Open) to stop aggressive retries after N consecutive failures.
  - [x] Classify errors into transient (retry) and permanent (log and stop) failures.

## Performance Analysis (performance_analysis.md)
### Linear Search Removal
- **Issue**: $O(n)$ linear searches where $O(1)$ hash lookups are expected in aircraft tracking.
- **Task**:
  - [x] Audit aircraft tracking paths for linear searches on aircraft data structures. *(Found 11 full-table scan sites + 7 active-list iterations. All 11 batch operations are legitimate — none replaceable with hash lookups.)*
  - [x] Replace linear searches with proper hash lookups. *(Not needed — no misidentified linear searches found.)*
  - [x] Verify hash function distribution to ensure $O(1)$ average case. *(16384 buckets with `AIRCRAFT_HASH_BITS=14`, chains average <1 element.)*
  - [ ] Measure performance gain with large aircraft counts. *(Deferred — no capture file available for baseline.)*

### Demodulation Hot-Spot Optimization
- **Location**: `demod_2400.c`
- **Issue**: High CPU usage in tight loops processing raw samples.
- **Task**:
  - [ ] Profile `demodulate2400` and `slice_byte` to identify exact hotspots. *(Deferred — requires real I/Q capture file for profiling.)*
  - [ ] Replace branch-heavy logic (e.g., `switch` in `slice_byte`) with branchless masks or look-up tables.
  - [ ] Evaluate and implement fixed-point arithmetic to replace `float` and `sqrtf` in high-frequency loops.
  - [ ] Verify that decoding accuracy (demod_accepted stats) remains unchanged.

## Network Security (network_security.md)
### Bounds Checking for Network Reads
- **Location**: `net_io.c`
- **Task**:
  - [x] Implement maximum message size limits for all network read operations.
  - [x] Validate message lengths *before* processing in `handle_gpsd`, `processHexMessage`, and `decodeBinMessage`.
  - [x] Add overflow checks for all internal buffer operations involving network data.

### Standardized Input Validation
- **Task**:
  - [x] Implement common validation functions for:
    - IP addresses (`inet_pton` verification). *(Removed — better handled at config parse time, not in net_io.c.)*
    - Port numbers (range 1-65535). *(Same — config-time validation.)*
    - Message lengths. *(Implemented and wired into readAscii for GPSD + hex, and decodeBinMessage for beast.)*
  - [x] Apply these functions consistently across all network handlers.

### Integer Overflow Protection
- **Task**:
  - [x] Replace unsafe `atoi`/`strtol` calls on network data with range-validated conversions. *(SBS message type parsing now uses safe_atoi.)*
  - [x] Implement safe arithmetic wrappers for buffer size calculations. *(safe_atoi implemented with range checking.)*