#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== Building All PiTrac Dependencies ===${NC}"
echo ""

DEPS=("lgpio" "msgpack" "opencv")
FAILED=()

for dep in "${DEPS[@]}"; do
    echo -e "${YELLOW}Building $dep...${NC}"
    if "$SCRIPT_DIR/build-${dep}.sh"; then
        echo -e "${GREEN}✓ $dep built successfully${NC}"
    else
        echo -e "${RED}✗ $dep build failed${NC}"
        FAILED+=("$dep")
    fi
    echo ""
done

echo -e "${BLUE}=== Build Summary ===${NC}"
if [ ${#FAILED[@]} -eq 0 ]; then
    echo -e "${GREEN}All dependencies built successfully!${NC}"
    echo ""
    echo "Artifacts location: $(dirname "$SCRIPT_DIR")/deps-artifacts/"
    ls -lh "$(dirname "$SCRIPT_DIR")/deps-artifacts/"*.tar.gz 2>/dev/null || true
else
    echo -e "${RED}Failed builds: ${FAILED[*]}${NC}"
    exit 1
fi