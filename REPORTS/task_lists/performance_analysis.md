# Performance Analysis Task List

## Priority 1: High ROI / Low Risk (Immediate Wins)

### 1. Linear Search Removal (Complexity Reduction)
- **Issue**: $O(n)$ linear searches where $O(1)$ hash lookups are expected in aircraft tracking.
- **Task**:
  - [ ] Audit aircraft tracking paths for linear searches on aircraft data structures.
  - [ ] Replace linear searches with proper hash table lookups.
  - [ ] Verify hash function distribution to ensure $O(1)$ average case.
  - [ ] Measure performance gain with large aircraft counts.

### 2. Demodulation Hot-Spot Optimization
- **Location**: `demod_2400.c`
- **Issue**: High CPU usage in tight loops processing raw samples.
- **Task**:
  - [ ] Profile `demodulate2400` and `slice_byte` to identify exact hotspots.
  - [ ] Replace branch-heavy logic (e.g., `switch` in `slice_byte`) with branchless masks or look-up tables.
  - [ ] Evaluate and implement fixed-point arithmetic to replace `float` and `sqrtf` in high-frequency loops.
  - [ ] Verify that decoding accuracy (demod_accepted stats) remains unchanged.

## Priority 2: Architectural Hardening (Medium Risk/Impact)

### 3. SIMD Vectorization
- **Issue**: Sample processing is performed scalar-wise.
- **Task**:
  - [ ] Identify candidate functions for SIMD (SSE/AVX or ARM NEON) in `demod_2400.c` (e.g., phase scoring).
  - [ ] Implement vectorized versions of sample processing loops.
  - [ ] Use a fallback path for non-SIMD compatible CPUs.
  - [ ] Measure throughput increase (samples per second).

### 4. Lock Contention Analysis & Reduction
- **Issue**: High contention on aircraft hash table locks under multi-threaded decode.
- **Task**:
  - [ ] Use `perf lock` or similar tools to quantify lock contention.
  - [ ] Implement fine-grained locking (per-bucket/striped locks) as defined in the Threading Task List.
  - [ ] Explore lock-free read paths for non-modifying API queries.

## Priority 3: Data Locality & Cache Optimization

### 5. Cache Locality Improvement
- **Issue**: Poor data layout in tracking structures causing frequent cache misses.
- **Task**:
  - [ ] Analyze aircraft data structure layout and identify "hot" vs "cold" fields.
  - [ ] Reorganize structures to group frequently accessed fields together.
  - [ ] Evaluate a Structure of Arrays (SoA) approach for high-frequency update fields.
  - [ ] Measure cache miss rate reduction using `perf stat`.

### 6. False Sharing Mitigation
- **Issue**: Concurrent updates to adjacent aircraft structures in global arrays triggering cache line invalidations.
- **Task**:
  - [ ] Audit aircraft array access patterns across threads.
  - [ ] Align critical aircraft structures to cache line boundaries (e.g., using `__attribute__((aligned(64)))`).
  - [ ] Measure parallelism improvement on multi-core systems.

## Measurement & Verification Pipeline

### 7. Performance Baseline & Validation
- **Task**:
  - [ ] **Baseline**: Establish a performance baseline using `perf record -g` and FlameGraphs on a high-load capture file (`--ifile`).
  - [ ] **Iterative Testing**: Apply one optimization $\to$ Verify accuracy $\to$ Measure CPU/Throughput $\to$ Commit.
  - [ ] **Final Validation**: Compare "Before" vs "After" CPU usage and message throughput under identical load.