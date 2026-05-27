# P3-2: CI/CD Pipeline Configuration

## Objective

Set up automated CI/CD pipelines for build, test, lint, and (optionally) deploy.

## Current State

No CI/CD configuration exists. All builds and tests are run manually on developer machines. No automated verification of pull requests.

## Implementation Steps

### Step 1: GitHub Actions CI

**File**: `.github/workflows/ci.yml`

```yaml
name: CI

on:
  push:
    branches: [master]
  pull_request:
    branches: [master]

jobs:
  build-and-test-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential cmake libevent-dev libssl-dev \
            libmongoc-dev libmsgpack-dev lua5.5 liblua5.5-dev
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Test
        run: cd build && ctest --output-on-failure
      - name: ASAN Test
        run: |
          cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
          cmake --build build-asan -j$(nproc)
          cd build-asan && ctest --output-on-failure

  build-and-test-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
      - name: Build
        run: cmake --build build --config Debug
      - name: Test
        run: cd build && ctest --output-on-failure -C Debug

  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Check fprintf usage
        run: |
          if grep -rn "fprintf(stderr" src/runtime/ --include="*.cc"; then
            echo "ERROR: fprintf(stderr) found"
            exit 1
          fi
      - name: Check std::cout usage
        run: |
          if grep -rn "std::cout" src/runtime/ --include="*.cc"; then
            echo "ERROR: std::cout found"
            exit 1
          fi
      - name: Check TODO markers
        run: grep -rn "TODO\|FIXME\|HACK\|XXX" src/ --include="*.cc" --include="*.h" || true
```

### Step 2: Add clang-format Check

```yaml
- name: clang-format
  run: |
    find src/ -name "*.cc" -o -name "*.h" | xargs clang-format --dry-run --Werror
```

### Step 3: Coverage (Optional)

```yaml
- name: Coverage
  run: |
    cmake -B build-cov -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
    cmake --build build-cov
    cd build-cov && ctest
    gcovr -r .. --xml -o coverage.xml
```

## Acceptance Criteria

1. GitHub Actions (or equivalent) CI pipeline on push and PR
2. Builds on Linux and Windows
3. Runs all tests (unit + integration)
4. Lint check for fprintf/cout violations
5. ASAN build passes on Linux
6. CI badge in README

## Dependencies: None | Estimated Effort: CI config files only
