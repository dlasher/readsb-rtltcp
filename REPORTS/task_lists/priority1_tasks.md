# Priority 1 Security Tasks

## Threading & Race Conditions (threading_race_conditions.md)
### Lock Hierarchy Audit
- **Issue**: Potential for deadlocks if locks are acquired out of order.
- **Task**:
  - [ ] Document the definitive lock order based on `readsb.c:1284-1290`.
  - [ ] Grep the entire codebase to identify any functions acquiring locks in a different order.
  - [ ] Refactor out-of-order lock acquisitions to match the documented hierarchy.

## RTL-TCP Client (rtl_tcp_client.md)
### Professional Recovery Strategy
- **Issue**: Fixed 5-second reconnect delay is inefficient and risky.
- **Task**:
  - [ ] Implement **Exponential Backoff with Jitter** (e.g., 1s, 2s, 4s... up to 60s).
  - [ ] Implement a **Circuit Breaker** state machine (Closed → Open → Half-Open) to stop aggressive retries after N consecutive failures.
  - [ ] Classify errors into transient (retry) and permanent (log and stop) failures.

## Performance Analysis (performance_analysis.md)
### Linear Search Removal
- **Issue**: $O(n)$ linear searches where $O(1)$ hash lookups are expected in aircraft tracking.
- **Task**:
  - [ ] Audit aircraft tracking paths for linear searches on aircraft data structures.
  - [ ] Replace linear searches with proper hash table lookups.
  - [ ] Verify hash function distribution to ensure $O(1)$ average case.
  - [ ] Measure performance gain with large aircraft counts.

### Demodulation Hot-Spot Optimization
- **Location**: `demod_2400.c`
- **Issue**: High CPU usage in tight loops processing raw samples.
- **Task**:
  - [ ] Profile `demodulate2400` and `slice_byte` to identify exact hotspots.
  - [ ] Replace branch-heavy logic (e.g., `switch` in `slice_byte`) with branchless masks or look-up tables.
  - [ ] Evaluate and implement fixed-point arithmetic to replace `float` and `sqrtf` in high-frequency loops.
  - [ ] Verify that decoding accuracy (demod_accepted stats) remains unchanged.

## Network Security (network_security.md)
### Bounds Checking for Network Reads
- **Location**: `net_io.c`
- **Task**:
  - [ ] Implement maximum message size limits for all network read operations.
  - [ ] Validate message lengths *before* processing in `handle_gpsd`, `processHexMessage`, and `decodeBinMessage`.
  - [ ] Add overflow checks for all internal buffer operations involving network data.

### Standardized Input Validation
- **Task**:
  - [ ] Implement common validation functions for:
    - IP addresses (`inet_pton` verification).
    - Port numbers (range 1-65535).
    - Message lengths.
  - [ ] Apply these functions consistently across all network handlers.

### Integer Overflow Protection
- **Task**:
  - [ ] Replace unsafe `atoi`/`strtol` calls on network data with range-validated conversions.
  - [ ] Implement safe arithmetic wrappers for buffer size calculations (e.g., `safe_mul_size_t` to prevent overflow during `count * size` for `malloc`).