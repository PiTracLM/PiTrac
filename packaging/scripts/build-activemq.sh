#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
POC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ARTIFACT_DIR="$POC_DIR/deps-artifacts"

GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}Building ActiveMQ-CPP 3.9.5 for ARM64...${NC}"

if [ -f "$ARTIFACT_DIR/activemq-cpp-3.9.5-arm64.tar.gz" ]; then
    echo -e "${GREEN}Artifact already exists. Delete it to rebuild.${NC}"
    echo "Location: $ARTIFACT_DIR/activemq-cpp-3.9.5-arm64.tar.gz"
    exit 0
fi

# Ensure artifact directory exists
mkdir -p "$ARTIFACT_DIR"

# Build using Docker
cd "$POC_DIR"
docker build \
    --platform=linux/arm64 \
    -f Dockerfile.activemq \
    -t activemq-builder:arm64 \
    .

# Extract artifact from container
echo -e "${BLUE}Extracting artifact...${NC}"
docker run --rm activemq-builder:arm64 > "$ARTIFACT_DIR/activemq-cpp-3.9.5-arm64.tar.gz"

# Get metadata
docker run --rm --entrypoint cat activemq-builder:arm64 /activemq-metadata.txt > "$ARTIFACT_DIR/activemq-cpp-3.9.5-arm64.metadata"

# Display info
SIZE=$(du -h "$ARTIFACT_DIR/activemq-cpp-3.9.5-arm64.tar.gz" | cut -f1)
echo -e "${GREEN}ActiveMQ-CPP artifact created successfully!${NC}"
echo "  Location: $ARTIFACT_DIR/activemq-cpp-3.9.5-arm64.tar.gz"
echo "  Size: $SIZE"
echo "  Metadata: $ARTIFACT_DIR/activemq-cpp-3.9.5-arm64.metadata"