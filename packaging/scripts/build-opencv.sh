#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
POC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ARTIFACT_DIR="$POC_DIR/deps-artifacts"

GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}Building OpenCV 4.11.0 for ARM64...${NC}"

if [ -f "$ARTIFACT_DIR/opencv-4.11.0-arm64.tar.gz" ]; then
    echo -e "${GREEN}Artifact already exists. Delete it to rebuild.${NC}"
    echo "Location: $ARTIFACT_DIR/opencv-4.11.0-arm64.tar.gz"
    exit 0
fi

mkdir -p "$ARTIFACT_DIR"
cd "$POC_DIR"
docker build \
    --platform=linux/arm64 \
    -f Dockerfile.opencv \
    -t opencv-builder:arm64 \
    .

echo -e "${BLUE}Extracting artifact...${NC}"
docker run --rm opencv-builder:arm64 > "$ARTIFACT_DIR/opencv-4.11.0-arm64.tar.gz"

docker run --rm --entrypoint cat opencv-builder:arm64 /opencv-metadata.txt > "$ARTIFACT_DIR/opencv-4.11.0-arm64.metadata"
SIZE=$(du -h "$ARTIFACT_DIR/opencv-4.11.0-arm64.tar.gz" | cut -f1)
echo -e "${GREEN}OpenCV artifact created successfully!${NC}"
echo "  Location: $ARTIFACT_DIR/opencv-4.11.0-arm64.tar.gz"
echo "  Size: $SIZE"
echo "  Metadata: $ARTIFACT_DIR/opencv-4.11.0-arm64.metadata"