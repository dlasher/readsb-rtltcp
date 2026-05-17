# Memory Safety Task List

## Phase 1: Tactical (Overflow Prevention)

### 1. Stack Buffer Overflow
- **Location**: `uat2esnt/uat_decode.c:840`
- **Issue**: Unbounded `strcpy` when no record separator found in UAT data.
- **Task**:
  - [x] Replace `strcpy` with `strncpy` or `strlcpy`.
  - [x] Determine maximum safe length for the destination buffer.
  - [x] Ensure explicit null-termination at `buffer[max_len - 1] = 0`.
  - [x] Test with malformed UAT data to verify fix.

### 2. Format String Vulnerabilities
- **Location**: Multiple locations in `net_io.c`
- **Issue**: Unvalidated network data passed to `sprintf`/`printf`.
- **Task**:
  - [x] Replace all `sprintf` with `snprintf` using explicit format strings.
  - [x] Ensure no network-derived data is used as the format string itself.
  - [x] Add length limits to all formatted output.

### 3. Memory Leak in realloc Pattern
- **Location**: `api.c:1624`
- **Issue**: Direct assignment of `realloc` result loses original pointer on failure.
- **Task**:
  - [x] Implement temporary pointer pattern: `tmp = realloc(...); if(!tmp) handle_error;`.
  - [x] Audit codebase for similar `realloc` patterns and fix them.

## Phase 2: Structural (Ownership & Validation)

### 4. "Length-Aware" Buffer Pattern
- **Issue**: Reliance on null-terminated strings for network data is fragile.
- **Task**:
  - [ ] Define a `struct { char *ptr; size_t len; size_t capacity; }` for dynamic buffers.
  - [ ] Implement a consistent `buffer_append` utility with automatic resizing and bounds checking.

### 5. Input Validation at the Edge
- **Issue**: Data is processed before being validated for size/format.
- **Task**:
  - [ ] Implement strict length checks for every byte received from SDR or network.
  - [ ] Validate against maximum expected lengths *before* passing to internal processing.

### 6. Arena Allocation for Request Cycles
- **Issue**: Manual `malloc`/`free` in per-message processing (e.g., `uat_decode`) leads to leaks in error paths.
- **Task**:
  - [ ] Implement a "Scratchpad/Arena" allocator for the duration of a single message decode.
  - [ ] Free the entire arena block once the message is processed/discarded.

## Phase 3: Automated (Tooling)

### 7. Static Analysis Integration
- **Task**:
  - [ ] Integrate `Clang-Tidy` and `Cppcheck` into the build process.
  - [ ] Enable specific checks for out-of-bounds indices and null pointer dereferences.
  - [ ] Forbid use of `strcpy` and `strcat` via linting rules.

### 8. Runtime Safeguards
- **Task**:
  - [ ] Add `-fsanitize=address,undefined` to `CFLAGS` for debug builds (ASan/UBSan).
  - [ ] Create a suite of "Memory Stress Tests" to be run under `Valgrind` to detect slow leaks in aircraft tracking.

## Phase 4: Strategic (Architecture)

### 9. Reference Counting for Shared Objects
- **Issue**: Multiple threads referencing `aircraft` objects make `free()` dangerous (use-after-free).
- **Task**:
  - [ ] Implement a simple reference counter for `aircraft` structures.
  - [ ] Ensure objects are only freed when the reference count reaches zero.