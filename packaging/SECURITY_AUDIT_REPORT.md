# PiTrac Installation Security Audit Report

## Executive Summary
This security audit evaluates the PiTrac installation refactoring for security vulnerabilities. The audit identifies several critical and high-severity issues that require immediate attention, particularly around privilege escalation, command injection, and unsafe file operations.

**Overall Risk Level: HIGH**

## Critical Security Findings

### 1. Command Injection Vulnerabilities (CRITICAL)

#### Issue: Unsanitized User Input in Service Installers
**Severity: CRITICAL**  
**OWASP: A03:2021 – Injection**  
**Files Affected:** `web-service-install.sh`, `pitrac-common-functions.sh`

In `web-service-install.sh` lines 94-96, user input is passed through sed with incomplete sanitization:
```bash
escaped_user=$(printf '%s' "$install_user" | sed 's/[[\.*^$()+?{|]/\\&/g')
```

This pattern doesn't escape backslashes or forward slashes properly. An attacker could craft a username containing `/` characters to break out of the sed substitution and inject arbitrary commands.

**Proof of Concept:**
```bash
# Malicious username with injection
USERNAME='test/;touch${IFS}/tmp/pwned;#'
./web-service-install.sh install "$USERNAME"
```

**Recommended Fix:**
```bash
# Use parameter expansion instead of sed for substitution
create_service_from_template() {
    local template="$1"
    local output="$2"
    local user="$3"
    local group="$4"
    local home="$5"
    
    # Read template and replace using bash parameter expansion
    local content
    content=$(<"$template")
    content="${content//@PITRAC_USER@/$user}"
    content="${content//@PITRAC_GROUP@/$group}"
    content="${content//@PITRAC_HOME@/$home}"
    
    echo "$content" > "$output"
}
```

### 2. Path Traversal Vulnerabilities (HIGH)

#### Issue: Insufficient Path Validation
**Severity: HIGH**  
**OWASP: A01:2021 – Broken Access Control**  
**Files Affected:** `web-service-install.sh`, `postinst.sh`

In `web-service-install.sh` line 75-78, the home directory validation is insufficient:
```bash
if [[ ! "$user_home" =~ ^/.+ ]]; then
    echo "Error: Invalid home directory path: $user_home" >&2
    return 1
fi
```

This only checks if the path starts with `/` but doesn't prevent path traversal sequences like `/home/../../../etc/passwd`.

**Recommended Fix:**
```bash
# Resolve to canonical path and validate
validate_home_directory() {
    local user_home="$1"
    local canonical_home
    
    # Get canonical path
    canonical_home=$(readlink -f "$user_home" 2>/dev/null) || return 1
    
    # Ensure it's under acceptable locations
    case "$canonical_home" in
        /home/* | /var/lib/* | /opt/*)
            echo "$canonical_home"
            return 0
            ;;
        *)
            echo "Error: Home directory outside allowed paths: $canonical_home" >&2
            return 1
            ;;
    esac
}
```

### 3. Privilege Escalation Risks (HIGH)

#### Issue: Unsafe SUDO_USER Trust
**Severity: HIGH**  
**OWASP: A04:2021 – Insecure Design**  
**Files Affected:** All installation scripts

The scripts blindly trust the `SUDO_USER` environment variable without validation:
```bash
INSTALL_USER="${SUDO_USER:-$(whoami)}"
```

An attacker could set `SUDO_USER` to another user's name and gain unauthorized access:
```bash
sudo SUDO_USER=root ./build.sh dev
```

**Recommended Fix:**
```bash
get_install_user() {
    local proposed_user="${SUDO_USER:-$(whoami)}"
    
    # Validate SUDO_USER matches actual sudo invocation
    if [[ -n "${SUDO_USER:-}" ]]; then
        # Check if we're actually running under sudo
        if [[ -z "${SUDO_UID:-}" ]] || [[ "${SUDO_UID}" == "0" ]]; then
            echo "Error: SUDO_USER set but not running under proper sudo" >&2
            return 1
        fi
        
        # Verify SUDO_USER exists and matches SUDO_UID
        local sudo_uid_check
        sudo_uid_check=$(id -u "$SUDO_USER" 2>/dev/null) || return 1
        if [[ "$sudo_uid_check" != "$SUDO_UID" ]]; then
            echo "Error: SUDO_USER doesn't match SUDO_UID" >&2
            return 1
        fi
    fi
    
    echo "$proposed_user"
}
```

### 4. Insecure Temporary File Handling (MEDIUM)

#### Issue: Predictable Temp File Names
**Severity: MEDIUM**  
**OWASP: A02:2021 – Cryptographic Failures**  
**File:** `web-service-install.sh` line 82

```bash
temp_service=$(mktemp /tmp/pitrac-web.service.XXXXXX)
```

While `mktemp` is used correctly, the trap cleanup on line 88 doesn't handle all signals:
```bash
trap 'rm -f '"$temp_service"'' RETURN INT TERM
```

Missing signals like HUP, QUIT could leave sensitive files in /tmp.

**Recommended Fix:**
```bash
# Create temp file securely with proper cleanup
temp_service=$(mktemp -t pitrac-web.service.XXXXXX) || exit 1
readonly temp_service

# Comprehensive signal handling
cleanup() {
    local exit_code=$?
    [[ -n "${temp_service:-}" ]] && rm -f "$temp_service"
    exit $exit_code
}
trap cleanup EXIT HUP INT QUIT TERM
```

### 5. Systemd Service Security Issues (MEDIUM)

#### Issue: Weak Service Isolation
**Severity: MEDIUM**  
**File:** `pitrac-web.service.template`

The service lacks modern systemd security features:
```ini
[Service]
Type=simple
User=@PITRAC_USER@
Group=@PITRAC_GROUP@
DynamicUser=no
```

**Recommended Secure Configuration:**
```ini
[Service]
Type=simple
User=@PITRAC_USER@
Group=@PITRAC_GROUP@
DynamicUser=no

# Security hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/pitrac @PITRAC_HOME@/LM_Shares
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictNamespaces=true
LockPersonality=true
MemoryDenyWriteExecute=true
RestrictRealtime=true
RestrictSUIDSGID=true
SystemCallFilter=@system-service
SystemCallErrorNumber=EPERM

# Network restrictions (web server needs network)
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
IPAddressDeny=any
IPAddressAllow=localhost 192.168.0.0/16 10.0.0.0/8
```

### 6. ActiveMQ Security Configuration (HIGH)

#### Issue: Default Credentials and Unsecured Broker
**Severity: HIGH**  
**OWASP: A07:2021 – Identification and Authentication Failures**  
**Files:** `postinst.sh`, configuration templates

ActiveMQ is configured with:
- No authentication required
- Binding to all interfaces (0.0.0.0)
- Default admin credentials

**Recommended Fix:**
```bash
# Generate secure random password
generate_activemq_password() {
    openssl rand -base64 32 | tr -d "=+/" | cut -c1-25
}

# Configure with authentication
configure_activemq_secure() {
    local amq_password
    amq_password=$(generate_activemq_password)
    
    # Store password securely
    echo "ACTIVEMQ_PASSWORD=$amq_password" > /etc/pitrac/activemq.env
    chmod 600 /etc/pitrac/activemq.env
    chown pitrac:pitrac /etc/pitrac/activemq.env
    
    # Configure broker with authentication
    cat > /etc/activemq/activemq.xml << EOF
<beans>
  <broker brokerName="localhost" dataDirectory="\${activemq.data}">
    <plugins>
      <simpleAuthenticationPlugin>
        <users>
          <authenticationUser username="pitrac" password="$amq_password" groups="users,admins"/>
        </users>
      </simpleAuthenticationPlugin>
    </plugins>
    <transportConnectors>
      <transportConnector name="openwire" uri="tcp://127.0.0.1:61616"/>
    </transportConnectors>
  </broker>
</beans>
EOF
}
```

### 7. Python Web Server Security (MEDIUM)

#### Issue: No Input Validation for Web Server
**Severity: MEDIUM**  
**Files:** `build.sh`, `postinst.sh`

The Python web server installation uses pip with `--break-system-packages`:
```bash
pip3 install -r /usr/lib/pitrac/web-server/requirements.txt --break-system-packages
```

This bypasses Python's PEP 668 protection and could compromise system Python packages.

**Recommended Fix:**
```bash
# Use virtual environment for Python dependencies
install_python_webapp() {
    local venv_dir="/usr/lib/pitrac/web-server/.venv"
    
    # Create virtual environment
    python3 -m venv "$venv_dir"
    
    # Install dependencies in venv
    "$venv_dir/bin/pip" install --upgrade pip
    "$venv_dir/bin/pip" install -r /usr/lib/pitrac/web-server/requirements.txt
    
    # Update service to use venv
    sed -i "s|/usr/bin/python3|$venv_dir/bin/python|" \
        /etc/systemd/system/pitrac-web.service
}
```

## Additional Security Recommendations

### 1. Input Validation Framework
Implement a centralized input validation library:
```bash
# validate_lib.sh
validate_username() {
    local user="$1"
    [[ "$user" =~ ^[a-z_][a-z0-9_-]{0,31}$ ]] || return 1
    id "$user" &>/dev/null || return 1
    return 0
}

validate_path() {
    local path="$1"
    local canonical
    canonical=$(readlink -f "$path" 2>/dev/null) || return 1
    [[ "$canonical" =~ ^/[a-zA-Z0-9/_.-]+$ ]] || return 1
    echo "$canonical"
}

validate_ip() {
    local ip="$1"
    [[ "$ip" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]] || return 1
    return 0
}
```

### 2. Least Privilege Principle
- Run web server on high port (8080) without root
- Use capabilities instead of setuid where needed
- Implement proper file permissions (644 for configs, 755 for executables)

### 3. Security Headers for Web Server
Configure the Python Flask app with security headers:
```python
from flask import Flask
from flask_talisman import Talisman

app = Flask(__name__)
Talisman(app, 
    force_https=False,  # Local network only
    strict_transport_security=False,
    content_security_policy={
        'default-src': "'self'",
        'script-src': "'self' 'unsafe-inline'",
        'style-src': "'self' 'unsafe-inline'"
    }
)
```

### 4. Audit Logging
Implement comprehensive logging:
```bash
log_security_event() {
    local event="$1"
    local user="${SUDO_USER:-$(whoami)}"
    local timestamp=$(date -Iseconds)
    
    echo "[$timestamp] SECURITY: $event (user=$user, uid=$UID, euid=$EUID)" \
        >> /var/log/pitrac/security.log
}
```

### 5. Regular Security Updates
- Monitor dependencies for CVEs
- Implement automatic security updates for critical components
- Regular penetration testing of network services

## Compliance Checklist

| OWASP Top 10 2021 | Status | Notes |
|-------------------|--------|-------|
| A01: Broken Access Control | ⚠️ FAIL | Path traversal risks |
| A02: Cryptographic Failures | ⚠️ WARN | Weak temp file handling |
| A03: Injection | ❌ FAIL | Command injection vulnerabilities |
| A04: Insecure Design | ❌ FAIL | SUDO_USER trust issues |
| A05: Security Misconfiguration | ⚠️ WARN | ActiveMQ defaults |
| A06: Vulnerable Components | ⚠️ WARN | System Python packages |
| A07: Auth Failures | ❌ FAIL | No ActiveMQ authentication |
| A08: Data Integrity | ✅ PASS | Package signing possible |
| A09: Logging Failures | ⚠️ WARN | Limited security logging |
| A10: SSRF | ✅ PASS | No external requests |

## Priority Action Items

1. **IMMEDIATE (Critical)**
   - Fix command injection in service installers
   - Implement proper SUDO_USER validation
   - Secure ActiveMQ with authentication

2. **HIGH (Within 1 week)**
   - Add path traversal protection
   - Implement systemd hardening
   - Use Python virtual environments

3. **MEDIUM (Within 1 month)**
   - Enhance temp file handling
   - Add comprehensive audit logging
   - Implement input validation framework

## Testing Recommendations

### Security Test Cases
```bash
# Test 1: Command injection
./test_security.sh injection

# Test 2: Path traversal
./test_security.sh traversal

# Test 3: Privilege escalation
./test_security.sh privesc

# Test 4: Service isolation
./test_security.sh isolation
```

### Automated Security Scanning
```bash
# Static analysis
shellcheck -S error *.sh

# Dependency scanning
pip-audit -r requirements.txt

# Container scanning (if using Docker)
trivy image pitrac:latest
```

## Conclusion

The PiTrac installation refactoring contains several serious security vulnerabilities that must be addressed before production deployment. The most critical issues involve command injection and privilege escalation risks. Implementing the recommended fixes will significantly improve the security posture of the application.

**Risk Assessment: HIGH - Do not deploy to production without addressing critical issues.**

---
*Report Generated: $(date -Iseconds)*
*Auditor: Security Audit Tool v1.0*
*Standard: OWASP Top 10 2021*