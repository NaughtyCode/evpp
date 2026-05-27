#!/usr/bin/env bash
# pre-commit.sh -- Fast local validation before each commit.
# Install: ln -s ../../scripts/pre-commit.sh .git/hooks/pre-commit
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

CHANGED_CPP=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cc|h|cpp|hpp)$' || true)
CHANGED_LUA=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.lua$' || true)

EXIT=0

# -- clang-format on changed C++ files --
if [ -n "$CHANGED_CPP" ] && command -v clang-format &>/dev/null; then
    if ! echo "$CHANGED_CPP" | xargs -r clang-format --dry-run --Werror 2>&1; then
        echo -e "${RED}[FAIL]${NC} clang-format: some files need formatting"
        echo "  Run: clang-format -i \$(git diff --cached --name-only -- '*.cc' '*.h')"
        EXIT=1
    else
        echo -e "${GREEN}[PASS]${NC} clang-format"
    fi
fi

# -- Forbidden patterns on changed runtime files --
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
