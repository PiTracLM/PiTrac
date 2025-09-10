#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGING_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PACKAGING_DIR/.." && pwd)"
WEB_SERVER_DIR="$REPO_ROOT/Software/web-server"
ARTIFACT_DIR="$PACKAGING_DIR/deps-artifacts"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_success() { echo -e "${GREEN}[✓]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

log_info "Building PiTrac Web Server package..."

if [[ ! -d "$WEB_SERVER_DIR" ]]; then
    log_error "Web server source not found at $WEB_SERVER_DIR"
    exit 1
fi

TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

log_info "Copying web server files..."
cp -r "$WEB_SERVER_DIR" "$TEMP_DIR/web-server"

cat > "$TEMP_DIR/install-webserver.sh" <<'EOF'
#!/bin/bash
# Install PiTrac web server

INSTALL_DIR="/usr/lib/pitrac/web-server"

# Create installation directory
mkdir -p "$INSTALL_DIR"

# Copy files
cp -r web-server/* "$INSTALL_DIR/"

# Install Python dependencies
pip3 install -r "$INSTALL_DIR/requirements.txt" --break-system-packages 2>/dev/null || \
pip3 install -r "$INSTALL_DIR/requirements.txt"

# Install systemd service
cp "$INSTALL_DIR/pitrac-web.service" /etc/systemd/system/
systemctl daemon-reload

echo "PiTrac web server installed successfully"
EOF

chmod +x "$TEMP_DIR/install-webserver.sh"

log_info "Creating web server package..."
cd "$TEMP_DIR"
tar czf "$ARTIFACT_DIR/pitrac-webserver-1.0.0-noarch.tar.gz" web-server install-webserver.sh

cat > "$ARTIFACT_DIR/pitrac-webserver-1.0.0-noarch.metadata" <<EOF
name: pitrac-webserver
version: 1.0.0
arch: noarch
type: python
description: Lightweight FastAPI web server for PiTrac
size: $(du -h "$ARTIFACT_DIR/pitrac-webserver-1.0.0-noarch.tar.gz" | cut -f1)
build_date: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
dependencies:
  - python3 >= 3.9
  - python3-pip
EOF

log_success "Web server package created: pitrac-webserver-1.0.0-noarch.tar.gz"