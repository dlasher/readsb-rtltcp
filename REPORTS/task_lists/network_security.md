# Network Security Task List

## Priority 1: Immediate Hardening (High Impact, Low Effort)

### 1. Bounds Checking for Network Reads
- **Location**: `net_io.c`
- **Task**:
  - [ ] Implement maximum message size limits for all network read operations.
  - [ ] Validate message lengths *before* processing in `handle_gpsd`, `processHexMessage`, and `decodeBinMessage`.
  - [ ] Add overflow checks for all internal buffer operations involving network data.

### 2. Standardized Input Validation
- **Task**:
  - [ ] Implement common validation functions for:
    - IP addresses (`inet_pton` verification).
    - Port numbers (range 1-65535).
    - Message lengths.
  - [ ] Apply these functions consistently across all network handlers.

### 3. Integer Overflow Protection
- **Task**:
  - [ ] Replace unsafe `atoi`/`strtol` calls on network data with range-validated conversions.
  - [ ] Implement safe arithmetic wrappers for buffer size calculations (e.g., `safe_mul_size_t` to prevent overflow during `count * size` for `malloc`).

## Priority 2: Systemic Frameworks (Medium Term)

### 4. Network Message Validation Framework
- **Task**:
  - [ ] Implement a `network_protocol_handler` structure containing a `max_length`, a `validator` function, and a `processor` function.
  - [ ] Create a `safe_network_processor` wrapper that enforces length and content validation before invoking the processor.
  - [ ] Migrate existing handlers to this framework.

### 5. Safe String Handling Utilities
- **Task**:
  - [ ] Implement `safe_strncpy` and `safe_strncat` that guarantee null-termination and prevent buffer overruns.
  - [ ] Replace all raw `strncpy`/`strncat` in network-facing code with these utilities.

### 6. Enhanced Observability
- **Task**:
  - [ ] Log security-relevant events: malformed messages, oversized inputs, and failed validation attempts.
  - [ ] Implement basic rate limiting for clients sending malformed data.

## Priority 3: Architectural Hardening (Long Term)

### 7. Stack Usage Monitoring
- **Task**:
  - [ ] Audit recursive parsing functions and implement recursion depth limits.
  - [ ] Replace large `alloca()` calls with heap-based allocation with strict size limits.

### 8. Fuzz Testing Harness
- **Task**:
  - [ ] Create a test harness for network message parsing (e.g., using AFL++ or libFuzzer).
  - [ ] Use the harness to discover edge cases and crashes in `net_io.c` and `api.c`.