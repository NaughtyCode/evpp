# P3-2: CI/CD Pipeline Configuration

## Objective

Build a multi-stage CI/CD pipeline covering build, test, lint, sanitizer, fuzz, coverage, and performance regression detection across Linux and Windows. Move from manual-only verification to automated quality gates at every stage of the development workflow.

## Current State

CI pipelines already exist (`.github/workflows/ci.yml`, `.github/workflows/nightly.yml`) with the following coverage:

| Job | Trigger | Platform | Status |
|-----|---------|----------|--------|
| Smoke tests (Windows MSVC) | Push + PR | Windows | Done |
| Unit tests (Windows MSVC) | Push + PR | Windows | Done |
| Integration tests (Windows MSVC) | Push + PR | Windows | Done |
| Lua script tests (Windows MSVC) | Push + PR | Windows | Done |
| Performance benchmarks | Manual + `/benchmark` in PR body | Windows | Done |
| Coverage (GCC + lcov + Codecov) | Weekly (Mon 2:37am) + Manual | Ubuntu | Done |
| Build & Test (GCC) | Weekly + Manual | Ubuntu | Done |

**What's working**: Windows MSVC smoke/unit/integration/lua tests on every push and PR. Performance benchmarks available on demand. Nightly coverage uploaded to Codecov.

**What's missing**:
- No **Linux CI on push/PR** — Ubuntu build only runs weekly
- No **sanitizer builds** (ASAN, UBSAN, TSAN, MSAN) in CI
- No **static analysis** (clang-tidy, clang-format, luacheck, cppcheck)
- No **pre-commit hook** for fast local validation
- No **fuzz testing** in CI
- No **performance regression detection** — benchmarks run but results aren't compared against baselines
- No **E2E tests** in CI
- No **quality gates** documentation or enforcement
- No **self-hosted runner** setup for long-running jobs
- No **CI badge** in README

## Root Cause

The original P3-2 plan was written before CI was implemented. The CI that was built focused on Windows (the primary development platform) and left Linux, sanitizers, and static analysis for later. This plan replaces the original and covers the gap from current state to the comprehensive strategy defined in P0-3.

---

## Pipeline Architecture

```
Git Push → Pre-commit Hook (static analysis, < 30s)           [TO BUILD]
         → CI Pipeline (per-commit + per-PR):
              ├── Build (Linux Debug, Linux Release,          [PARTIAL: only Win Release]
              │          Windows Debug, Windows Release)       [parallel]
              ├── Unit Tests (C++ + Lua)                      [PARTIAL: Win only]
              │                                                [parallel per-platform]
              ├── Integration Tests                           [DONE: Win]
              │                                                [sequential]
              ├── Sanitizer Tests (ASAN + UBSAN)              [TO BUILD]
              │                                                [per-PR]
              ├── Static Analysis (clang-tidy, clang-format,  [TO BUILD]
              │                    luacheck, cppcheck)         [per-PR]
              └── Nightly Pipeline:                           [PARTIAL]
                    ├── TSAN Tests                            [TO BUILD]
                    ├── MSAN Tests                            [TO BUILD]
                    ├── E2E Tests                             [TO BUILD]
                    ├── Performance Benchmarks                [TO BUILD: regression detection]
                    │     + Regression Report
                    ├── Fuzz Tests (4hr continuous)           [TO BUILD]
                    ├── Coverage Report                       [DONE: lcov + Codecov]
                    ├── 24hr Soak Test                        [TO BUILD]
                    └── Multi-Platform Build Matrix           [TO BUILD]
```

---

## Quality Gates

### Per-Commit (Pre-Push)

| Gate | Blocking? | Action on Failure |
|------|-----------|-------------------|
| clang-format compliance (changed files) | Yes | Push rejected |
| luacheck (changed Lua files) | Yes | Push rejected |
| Check for leftover fprintf/cout in runtime | Yes | Push rejected |

### Per-PR

| Gate | Blocking? | Action on Failure |
|------|-----------|-------------------|
| Build (Linux Debug + Release) | Yes | PR blocked |
| Build (Windows Debug + Release) | Yes | PR blocked |
| Unit Tests (all platforms) | Yes | PR blocked |
| Integration Tests | Yes | PR blocked |
| Lua Script Tests | Yes | PR blocked |
| ASAN + UBSAN clean | Yes | PR blocked |
| clang-tidy (new warnings only) | Yes | PR blocked |
| clang-format compliance (all files) | Yes | PR blocked |
| luacheck (new warnings only) | Yes | PR blocked |
| Code coverage decrease > 2% | Advisory | PR warning (via Codecov) |
| Performance regression > 10% | Advisory | PR warning (nightly check) |
| Performance regression > 25% | Yes | PR blocked (nightly check) |

### Nightly

| Gate | Blocking? | Action on Failure |
|------|-----------|-------------------|
| TSAN (0 data races) | Advisory | File issue, tag oncall |
| MSAN (0 uninitialized reads) | Advisory | File issue |
| E2E tests | Advisory | File issue |
| Fuzz (0 crashes found) | Yes | File issue, tag security |
| Coverage (≥ 60% line) | Advisory | Dashboard tracking |
| Soak test (24hr no crash) | Advisory | File issue |

---

## Implementation Steps

### Step 1: Add Linux CI to Push/PR Pipeline

**File**: `.github/workflows/ci.yml`

Add Ubuntu builds to the per-push pipeline (currently only Windows):

```yaml
  # ── Linux GCC Release ──────────────────────────────────────────
  linux-build:
    name: "Build (Ubuntu, GCC)"
    runs-on: ubuntu-latest
    timeout-minutes: 15
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libevent-dev
      - name: Configure
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=ON \
            -DCLOUDENGINE_BUILD_BENCHMARKS=OFF
      - name: Build
        run: cmake --build build -j $(nproc)
      - name: Test
        run: cd build && ctest --output-on-failure -j $(nproc)
```

### Step 2: Add Sanitizer Builds

**File**: `.github/workflows/sanitizers.yml` (new)

ASAN + UBSAN on every PR. TSAN and MSAN on nightly schedule.

```yaml
name: Sanitizers

on:
  pull_request:
    branches: [master, main]
  workflow_dispatch:

jobs:
  asan-ubsan:
    name: "ASAN + UBSAN (Ubuntu, Clang)"
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libevent-dev
      - name: Configure with ASAN+UBSAN
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DBUILD_TESTING=ON \
            -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
            -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
      - name: Build
        run: cmake --build build -j $(nproc)
      - name: Test
        run: cd build && ctest --output-on-failure -j $(nproc)
        env:
          ASAN_OPTIONS: detect_leaks=1:detect_stack_use_after_return=1
          UBSAN_OPTIONS: print_stacktrace=1:halt_on_error=1

  tsan:
    name: "TSAN (Ubuntu, Clang, Nightly)"
    if: ${{ github.event_name == 'schedule' || github.event_name == 'workflow_dispatch' }}
    runs-on: ubuntu-latest
    timeout-minutes: 60
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libevent-dev
      - name: Configure with TSAN
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DBUILD_TESTING=ON \
            -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
            -DCMAKE_C_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
      - name: Build
        run: cmake --build build -j $(nproc)
      - name: Test
        run: cd build && ctest --output-on-failure -j 1
        env:
          TSAN_OPTIONS: history_size=7:second_deadlock_stack=1
```

### Step 3: Add Static Analysis

**File**: `.github/workflows/lint.yml` (new)

```yaml
name: Lint

on:
  pull_request:
    branches: [master, main]
  push:
    branches: [master, main]

jobs:
  clang-format:
    name: "clang-format"
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Run clang-format
        run: |
          find src/runtime/ -name '*.cc' -o -name '*.h' | xargs clang-format --dry-run --Werror

  clang-tidy:
    name: "clang-tidy"
    runs-on: ubuntu-latest
    timeout-minutes: 20
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libevent-dev clang-tidy
      - name: Configure (generate compile_commands.json)
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
      - name: Run clang-tidy
        run: |
          run-clang-tidy -p build -j $(nproc) \
            -header-filter='src/runtime/.*' \
            -warnings-as-errors='bugprone-*,performance-*' \
            src/runtime/ 2>&1 | tee clang-tidy.log
          if grep -q "error:" clang-tidy.log; then exit 1; fi

  luacheck:
    name: "luacheck"
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install luacheck
        run: sudo apt-get install -y lua-check
      - name: Run luacheck
        run: luacheck resources/script/ --config resources/.luacheckrc

  forbidden-patterns:
    name: "Forbidden Patterns"
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Check fprintf(stderr)
        run: |
          if grep -rn "fprintf(stderr" src/runtime/ --include="*.cc" --include="*.h"; then
            echo "ERROR: fprintf(stderr, ...) found in runtime code"
            exit 1
          fi
      - name: Check std::cout
        run: |
          if grep -rn "std::cout" src/runtime/ --include="*.cc" --include="*.h"; then
            echo "ERROR: std::cout found in runtime code"
            exit 1
          fi
```

### Step 4: Add Fuzz Testing CI

**File**: `.github/workflows/fuzz.yml` (new)

```yaml
name: Fuzz

on:
  schedule:
    - cron: '13 3 * * *'   # Daily 3:13am UTC
  workflow_dispatch:

jobs:
  fuzz-msgpack:
    name: "Fuzz: msgpack_decode"
    runs-on: ubuntu-latest
    timeout-minutes: 360   # 6 hours
    steps:
      - uses: actions/checkout@v4
      - name: Build fuzz target
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DBUILD_TESTING=ON \
            -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer,address"
          cmake --build build --target fuzz_msgpack_decode -j $(nproc)
      - name: Run fuzzer
        run: build/tests/fuzz/fuzz_msgpack_decode -max_total_time=14400

  fuzz-config:
    name: "Fuzz: config_parse"
    runs-on: ubuntu-latest
    timeout-minutes: 360
    steps:
      - uses: actions/checkout@v4
      - name: Build fuzz target
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DBUILD_TESTING=ON \
            -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer,address"
          cmake --build build --target fuzz_config_parse -j $(nproc)
      - name: Run fuzzer
        run: build/tests/fuzz/fuzz_config_parse -max_total_time=14400

  fuzz-network:
    name: "Fuzz: network_frame"
    runs-on: ubuntu-latest
    timeout-minutes: 360
    steps:
      - uses: actions/checkout@v4
      - name: Build fuzz target
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DBUILD_TESTING=ON \
            -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer,address"
          cmake --build build --target fuzz_network_frame -j $(nproc)
      - name: Run fuzzer
        run: build/tests/fuzz/fuzz_network_frame -max_total_time=14400
```

### Step 5: Add Performance Regression Detection

**File**: `.github/workflows/benchmarks.yml` (new, replaces inline in ci.yml)

Move benchmarks to a dedicated workflow with baseline comparison:

```yaml
name: Benchmarks

on:
  schedule:
    - cron: '7 4 * * 2,5'   # Tue/Fri 4:07am UTC
  workflow_dispatch:
  pull_request:
    # Only when /benchmark is in PR body
    types: [opened, synchronize, reopened]

jobs:
  benchmark:
    name: "Performance Benchmarks"
    if: |
      github.event_name != 'pull_request' ||
      contains(github.event.pull_request.body, '/benchmark')
    runs-on: ubuntu-latest
    timeout-minutes: 60
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0   # Full history for baseline comparison

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libevent-dev

      - name: Build benchmarks (current commit)
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=ON \
            -DCLOUDENGINE_BUILD_BENCHMARKS=ON
          cmake --build build --target all_benchmarks -j $(nproc)

      - name: Run benchmarks (current)
        run: |
          cd build
          for bm in tests/performance/bench_*; do
            $bm --benchmark_format=json --benchmark_out=../current_$(basename $bm).json
          done

      - name: Checkout baseline (master)
        run: git checkout origin/master

      - name: Build benchmarks (baseline)
        run: |
          cmake -B build-baseline \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=ON \
            -DCLOUDENGINE_BUILD_BENCHMARKS=ON
          cmake --build build-baseline --target all_benchmarks -j $(nproc)

      - name: Run benchmarks (baseline)
        run: |
          cd build-baseline
          for bm in tests/performance/bench_*; do
            $bm --benchmark_format=json --benchmark_out=../baseline_$(basename $bm).json
          done

      - name: Compare benchmarks
        run: |
          python3 scripts/ci/compare_benchmarks.py \
            --baseline baseline_*.json \
            --current current_*.json \
            --warning-threshold 10 \
            --blocking-threshold 25
```

### Step 6: Add E2E Tests to Nightly

Update `.github/workflows/nightly.yml` to include E2E tests:

```yaml
  e2e:
    name: "E2E Tests (Ubuntu)"
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libevent-dev
      - name: Configure
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=ON \
            -DENGINE_PHYSICS_ENABLED=ON
      - name: Build
        run: cmake --build build -j $(nproc)
      - name: Run E2E tests
        run: ctest -L e2e --output-on-failure -j 1

  soak:
    name: "24hr Soak Test"
    runs-on: ubuntu-latest
    timeout-minutes: 1500   # 25 hours
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libevent-dev
      - name: Configure
        run: |
          cmake -S src -B build \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=ON
      - name: Build
        run: cmake --build build -j $(nproc)
      - name: Run soak test
        run: build/tests/e2e/long_running_test --duration_hours=24
```

### Step 7: Create Pre-Commit Hook

**File**: `scripts/pre-commit.sh` (new)

```bash
#!/usr/bin/env bash
# pre-commit.sh — Fast local validation before each commit.
# Install: ln -s ../../scripts/pre-commit.sh .git/hooks/pre-commit
set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

CHANGED_CPP=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cc|h|cpp|hpp)$' || true)
CHANGED_LUA=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.lua$' || true)

EXIT=0

# ── clang-format on changed C++ files ──────────────────────────
if [ -n "$CHANGED_CPP" ]; then
    if ! echo "$CHANGED_CPP" | xargs -r clang-format --dry-run --Werror 2>&1; then
        echo -e "${RED}[FAIL]${NC} clang-format: some files need formatting"
        echo "  Run: clang-format -i \$(git diff --cached --name-only -- '*.cc' '*.h')"
        EXIT=1
    else
        echo -e "${GREEN}[PASS]${NC} clang-format"
    fi
fi

# ── luacheck on changed Lua files ──────────────────────────────
if [ -n "$CHANGED_LUA" ]; then
    if ! echo "$CHANGED_LUA" | xargs -r luacheck --config resources/.luacheckrc 2>&1; then
        echo -e "${RED}[FAIL]${NC} luacheck: warnings in Lua files"
        EXIT=1
    else
        echo -e "${GREEN}[PASS]${NC} luacheck"
    fi
fi

# ── Forbidden patterns on changed runtime files ─────────────────
FORBIDDEN=$(echo "$CHANGED_CPP" | grep '^src/runtime/' || true)
if [ -n "$FORBIDDEN" ]; then
    if echo "$FORBIDDEN" | xargs -r grep -Hn "fprintf(stderr" 2>/dev/null; then
        echo -e "${RED}[FAIL]${NC} fprintf(stderr, ...) found in runtime code"
        EXIT=1
    fi
    if echo "$FORBIDDEN" | xargs -r grep -Hn "std::cout" 2>/dev/null; then
        echo -e "${RED}[FAIL]${NC} std::cout found in runtime code"
        EXIT=1
    fi
    if [ $EXIT -eq 0 ]; then
        echo -e "${GREEN}[PASS]${NC} forbidden patterns"
    fi
fi

if [ $EXIT -eq 0 ]; then
    echo -e "${GREEN}Pre-commit checks passed.${NC}"
else
    echo -e "${RED}Pre-commit checks FAILED. Commit aborted.${NC}"
fi
exit $EXIT
```

### Step 8: Add CI Badge + Status Dashboard

Add to `README.md`:

```markdown
## CI Status

| Pipeline | Status |
|----------|--------|
| CI (push/PR) | [![CI](https://github.com/OWNER/REPO/actions/workflows/ci.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/ci.yml) |
| Nightly | [![Nightly](https://github.com/OWNER/REPO/actions/workflows/nightly.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/nightly.yml) |
| Sanitizers | [![Sanitizers](https://github.com/OWNER/REPO/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/sanitizers.yml) |
| Lint | [![Lint](https://github.com/OWNER/REPO/actions/workflows/lint.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/lint.yml) |
| Coverage | [![codecov](https://codecov.io/gh/OWNER/REPO/branch/master/graph/badge.svg)](https://codecov.io/gh/OWNER/REPO) |
```

### Step 9: Self-Hosted Runner Configuration

For jobs exceeding GitHub Actions free tier limits (2000 min/month private, unlimited public):

**File**: `scripts/ci/setup-runner.sh` (new)

```bash
#!/usr/bin/env bash
# Setup self-hosted GitHub Actions runner on a dedicated machine.
# Run this on the runner machine once.

# Install GitHub Actions runner
mkdir -p ~/actions-runner && cd ~/actions-runner
curl -o actions-runner-linux-x64.tar.gz -L \
    https://github.com/actions/runner/releases/download/v2.320.0/actions-runner-linux-x64-2.320.0.tar.gz
tar xzf actions-runner-linux-x64.tar.gz

# Configure (interactive — requires PAT with repo:admin)
./config.sh --url https://github.com/OWNER/REPO --token YOUR_TOKEN \
    --labels self-hosted,linux,x64,nightly

# Install as service
sudo ./svc.sh install
sudo ./svc.sh start
```

Jobs targeting self-hosted runners use `runs-on: [self-hosted, linux, x64, nightly]` for long-running tasks (fuzz, soak, TSAN).

---

## Acceptance Criteria

1. CI pipeline runs on every push and PR (Linux + Windows)
2. Sanitizer builds (ASAN+UBSAN) run per-PR and catch memory/UB errors
3. TSAN and MSAN builds run nightly
4. clang-tidy, clang-format, luacheck run per-PR with blocking enforcement
5. Forbidden pattern checks (fprintf, std::cout) run per-PR
6. Fuzz testing runs daily with ≥ 4 hours per target
7. Performance benchmarks run on schedule with baseline comparison
8. Regression detection: > 10% triggers warning, > 25% blocks PR
9. E2E tests run nightly
10. 24-hour soak test runs weekly
11. Code coverage measured per-PR, reported via Codecov
12. CI badges displayed in README
13. Pre-commit hook script available and documented
14. Quality gates documented and enforced in CI configuration
15. Self-hosted runner setup documented for long-running jobs

## Dependencies

- P0-3 (Test Infrastructure) — defines the test targets, fakes, and benchmarks that CI runs
- P0-2 (Message Framing) — codec tests must exist for CI to run them
- P2-7 (Compiler Warnings) — CI must enforce warning-free builds
- P2-18 (fprintf Cleanup) — CI forbidden-pattern checks depend on this being done
- P2-19 (abort Elimination) — CI death tests depend on abort transitions

## Estimated Effort

| Component | Lines | Complexity |
|-----------|-------|------------|
| Linux CI expansion (ci.yml) | ~50 | Low |
| Sanitizer workflow (sanitizers.yml) | ~80 | Medium |
| Lint workflow (lint.yml) | ~70 | Medium |
| Fuzz workflow (fuzz.yml) | ~70 | Medium |
| Benchmark comparison workflow (benchmarks.yml) | ~60 | Medium |
| E2E + soak in nightly | ~50 | Low |
| compare_benchmarks.py script | ~100 | Medium |
| Pre-commit hook (pre-commit.sh) | ~50 | Low |
| Self-hosted runner setup script | ~30 | Low |
| README badges | ~20 | Low |
| **Total** | **~580 lines** | |

---

## Risks

- **GitHub Actions free tier limits**: 2000 min/month for private repos. With sanitizer + fuzz jobs, this can be exceeded quickly. Mitigation: use self-hosted runners for heavy jobs; keep per-commit jobs fast (< 3 min).
- **Flaky CI**: Network-dependent integration tests and timing-sensitive tests produce intermittent failures, eroding trust in CI. Mitigation: use virtual time and loopback networking; auto-retry flaky tests once; tag known-flaky tests and run them only nightly.
- **clang-tidy false positives**: Strict checks may flag code patterns that are intentional. Mitigation: allow per-file or per-line `// NOLINT` suppressions; review suppression additions in PRs.
- **Sanitizer runtime overhead**: ASAN (2x slowdown) and TSAN (5-15x) make tests significantly slower. Mitigation: run ASAN+UBSAN per-PR but with a reduced test subset; TSAN nightly only.
- **Baseline drift**: Performance baseline comparison depends on a stable reference machine. GitHub Actions runners have variable performance across runs. Mitigation: use relative comparisons within the same CI run (build baseline from prior commit, not stored JSON); flag only large (> 10%) and consistent (2+ consecutive runs) regressions.
- **Maintenance burden**: CI configs rot when not maintained — dependency versions change, checks break, workflows fail silently. Mitigation: treat CI as code with PR reviews; run CI on CI changes; periodically audit for stale configurations.
