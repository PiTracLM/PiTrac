#!/usr/bin/env bash
set -euo pipefail

# Patch meson.build for x86_64 compatibility

if [ -n "${BASH_SOURCE[0]}" ]; then
    SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
else
    SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
fi
source "${SCRIPT_DIR}/common.sh"

patch_meson_for_x86() {
    local meson_file="${1:-meson.build}"
    
    if [ ! -f "$meson_file" ]; then
        log_error "meson.build not found at: $meson_file"
        return 1
    fi
    
    local arch=$(uname -m)
    if [ "$arch" = "armv7l" ] || [ "$arch" = "aarch64" ]; then
        log_info "ARM architecture detected ($arch), no patching needed"
        return 0
    fi
    
    log_info "Detected non-ARM architecture ($arch), patching for x86_64 compatibility..."
    
    if [ ! -f "${meson_file}.original" ]; then
        cp "$meson_file" "${meson_file}.original"
        log_info "Created backup: ${meson_file}.original"
    else
        cp "${meson_file}.original" "$meson_file"
        log_info "Restored from backup for clean patching"
    fi
    
    cat > /tmp/patch_meson_x86.py << 'EOF'
#!/usr/bin/env python3
import re
import sys

if len(sys.argv) > 1:
    meson_file = sys.argv[1]
else:
    meson_file = 'meson.build'

with open(meson_file, 'r') as f:
    content = f.read()

# Check if already patched
if 'Building for x86_64' in content:
    print("Already patched for x86_64")
    sys.exit(0)

# First check if neon is used but not defined
if 'neon' in content and 'neon = [' not in content:
    # Need to define neon
    print("Found undefined neon variable, adding definition...")
    # Add definition at the beginning after project declaration
    content = re.sub(r"(project\([^)]+\))", r"\1\n\n# Define neon for x86_64 compatibility\nneon = []\nuse_neon = false", content)
    modified = True
    with open(meson_file, 'w') as f:
        f.write(content)
    print("Successfully added neon definition for x86_64 compatibility")
    sys.exit(0)

# Fix line 45 where neon is referenced but not defined for x86_64
# The pattern is looking for where host_machine.cpu() is compared with 'arm64'
pattern = r"if\s+host_machine\.cpu_family\(\)\s*==\s*'aarch64'\s+or\s+host_machine\.cpu\(\)\s*==\s*'arm64'"
replacement = """# Handle different architectures for PiTrac
if host_machine.cpu_family() == 'x86_64' or host_machine.cpu_family() == 'x86'
  neon = []
  use_neon = false
  message('Building for x86_64 - NEON optimizations disabled')
elif host_machine.cpu_family() == 'aarch64' or host_machine.cpu() == 'arm64'"""

modified = False
if re.search(pattern, content):
    content = re.sub(pattern, replacement, content)
    modified = True
    print("Patched ARM architecture detection")

# Also ensure neon is defined before any usage if not already handled
if 'neon' in content and 'neon = []' not in content and not modified:
    # Add a fallback definition after the compiler setup
    pattern2 = r"(cpp = meson\.get_compiler\('cpp'\))"
    if re.search(pattern2, content):
        content = re.sub(pattern2, 
                         r"\1\n\n# Initialize neon for non-ARM builds\nif host_machine.cpu_family() == 'x86_64' or host_machine.cpu_family() == 'x86'\n  neon = []\n  use_neon = false\nelse\n  neon = []  # Define empty for other architectures\n  use_neon = false\nendif", 
                         content)
        modified = True
        print("Added neon initialization for x86_64")

if modified:
    with open(meson_file, 'w') as f:
        f.write(content)
    print("Successfully patched meson.build for x86_64 compatibility")
else:
    print("No modifications needed")
EOF
    
    log_info "Applying patch to define neon variable..."
    
    if grep -q "neon = \[\]" "$meson_file"; then
        log_info "neon already defined, skipping patch"
    else
        
        awk '
        /^project\(/ { in_project = 1 }
        in_project && /\)/ { 
            print $0
            print ""
            print "# Define neon for x86_64 compatibility"
            print "neon = []"
            print "use_neon = false"
            print ""
            in_project = 0
            next
        }
        { print }
        ' "$meson_file" > "${meson_file}.tmp" && mv "${meson_file}.tmp" "$meson_file"
        
        log_success "Added neon definition after project declaration"
    fi
    
    log_info "Fixing array/string comparisons..."
    
    sed -i "s/neon == 'arm64'/false/g" "$meson_file"
    sed -i "s/'arm64' == neon/false/g" "$meson_file"
    sed -i "s/neon == 'armv8-neon'/false/g" "$meson_file"
    sed -i "s/'armv8-neon' == neon/false/g" "$meson_file"
    
    sed -i "s/neon == '[^']*'/false/g" "$meson_file"
    sed -i "s/'[^']*' == neon/false/g" "$meson_file"
    
    sed -i "s/host_machine\.cpu() == 'arm64'/host_machine.cpu_family() == 'aarch64'/g" "$meson_file"
    sed -i "s/host_machine\.cpu() == 'armv8'/host_machine.cpu_family() == 'aarch64'/g" "$meson_file"
    
    sed -i "s/if neon != \[\]/if use_neon/g" "$meson_file"
    sed -i "s/if neon == \[\]/if not use_neon/g" "$meson_file"
    
    sed -i "50s/elif.*neon.*==.*'armv8-neon'/elif false  # x86_64: disabled ARM check/g" "$meson_file"
    sed -i "50s/if.*neon.*==.*'armv8-neon'/if false  # x86_64: disabled ARM check/g" "$meson_file"
    
    log_success "Fixed array/string comparisons"
    
    log_info "Making lgpio dependency optional for x86_64..."
    
    sed -i "64s/dependency('lgpio')/dependency('lgpio', required: false)/g" "$meson_file"
    
    sed -i "/lgpio_dep = dependency('lgpio'/c\\
lgpio_dep = dependency('lgpio', required: false)" "$meson_file"
    
    sed -i "s/find_library('lgpio')/find_library('lgpio', required: false)/g" "$meson_file"
    
    awk '
    /lgpio_dep =/ {
        print $0
        print "if lgpio_dep.found()"
        print "  add_project_arguments('\''-DHAS_LGPIO=1'\'', language: '\''cpp'\'')"
        print "  lgpio_available = true"
        print "else"
        print "  add_project_arguments('\''-DHAS_LGPIO=0'\'', language: '\''cpp'\'')"
        print "  add_project_arguments('\''-DNO_LGPIO'\'', language: '\''cpp'\'')"
        print "  lgpio_available = false"
        print "  lgpio_dep = []"
        print "endif"
        next
    }
    /dependencies.*lgpio_dep/ {
        gsub(/lgpio_dep/, "lgpio_available ? lgpio_dep : []")
    }
    { print }
    ' "$meson_file" > "${meson_file}.tmp" && mv "${meson_file}.tmp" "$meson_file"
    
    sed -i "s/required: false, required: false/required: false/g" "$meson_file"
    
    log_success "Made Pi-specific dependencies optional"
    
    log_info "Handling ARM object files in meson.build..."
    
    
    if grep -q "closed_source_target = custom_target" "$meson_file"; then
        log_info "Replacing closed source object with source compilation for x86_64..."
        
        perl -i -0pe "s/closed_source_target = custom_target\([^)]*\)/closed_source_target = []/gs" "$meson_file"
        
        if ! sed -n '/pitrac_lm_sources += \[/,/\]/p' "$meson_file" | grep -q "'gs_e6_response.cpp'," 2>/dev/null; then
            sed -i "/'gs_e6_results.cpp',/a\\                        'gs_e6_response.cpp'," "$meson_file"
            log_info "Added gs_e6_response.cpp to source files after gs_e6_results.cpp"
        else
            log_info "gs_e6_response.cpp already in main source files"
        fi
    fi
    
    sed -i "s/'gs_e6_response\.cpp\.o',//g" "$meson_file"
    sed -i "s/, 'gs_e6_response\.cpp\.o'//g" "$meson_file"
    sed -i "s/'gs_e6_response\.cpp\.o'//g" "$meson_file"
    
    sed -i "s/closed_source_target,//g" "$meson_file"
    sed -i "s/, closed_source_target//g" "$meson_file"
    
    log_success "Handled ARM object files in build configuration"
    
    if [ "$arch" = "x86_64" ] || [ "$arch" = "x86" ]; then
        log_info "Creating lgpio stub file for x86_64..."
        if [ -f "${SCRIPT_DIR}/lgpio_stubs.c" ]; then
            cp "${SCRIPT_DIR}/lgpio_stubs.c" lgpio_stubs.c
            log_info "Copied lgpio_stubs.c from scripts directory"
        else
            cat > lgpio_stubs.c << 'EOF'
// Stub implementations for lgpio functions on x86_64
#include <stddef.h>
int lgGpiochipOpen(int gpiochip __attribute__((unused))) { return 0; }
int lgGpiochipClose(int handle __attribute__((unused))) { return 0; }
int lgGpioClaimOutput(int handle __attribute__((unused)), int flags __attribute__((unused)), 
                      int gpio __attribute__((unused)), int level __attribute__((unused))) { return 0; }
int lgGpioWrite(int handle __attribute__((unused)), int gpio __attribute__((unused)), 
                int level __attribute__((unused))) { return 0; }
int lgSpiOpen(int spiDev __attribute__((unused)), int spiChan __attribute__((unused)), 
              int spiBaud __attribute__((unused)), int spiFlags __attribute__((unused))) { return 0; }
int lgSpiClose(int handle __attribute__((unused))) { return 0; }
int lgSpiWrite(int handle __attribute__((unused)), const char *txBuf __attribute__((unused)), 
               int count) { return count; }
EOF
        fi
        
        if ! grep -q "lgpio_stubs.c" "$meson_file"; then
            sed -i "/'pulse_strobe.cpp',/a\\  'lgpio_stubs.c'," "$meson_file"
            log_info "Added lgpio_stubs.c to source files"
        fi
    fi
    
    rm -f /tmp/patch_meson_x86.py
    
    return 0
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    patch_meson_for_x86 "${1:-meson.build}"
fi