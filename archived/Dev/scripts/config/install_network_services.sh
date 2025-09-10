#!/usr/bin/env bash
set -euo pipefail

# Network Services Configuration Script for PiTrac
# Handles NAS mounting, Samba server setup, and SSH key configuration
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/../common.sh"

# Load defaults from config file
load_defaults "network-services" "$@"

# Configuration
FORCE="${FORCE:-0}"
FSTAB_FILE="/etc/fstab"
SAMBA_CONFIG="/etc/samba/smb.conf"

# NAS defaults
ENABLE_NAS="${ENABLE_NAS:-0}"
NAS_SERVER="${NAS_SERVER:-}"
NAS_SHARE="${NAS_SHARE:-PiTrac}"
NAS_MOUNT_POINT="${NAS_MOUNT_POINT:-/mnt/nas}"
NAS_USERNAME="${NAS_USERNAME:-}"
NAS_PASSWORD="${NAS_PASSWORD:-}"
NAS_MOUNT_OPTIONS="${NAS_MOUNT_OPTIONS:-vers=3.0,uid=1000,gid=1000,iocharset=utf8}"

# Samba defaults
ENABLE_SAMBA="${ENABLE_SAMBA:-0}"
SAMBA_SHARE_NAME="${SAMBA_SHARE_NAME:-PiTrac}"
SAMBA_SHARE_PATH="${SAMBA_SHARE_PATH:-/home/pi/LM_Shares}"
SAMBA_USER="${SAMBA_USER:-}"
SAMBA_GUEST_OK="${SAMBA_GUEST_OK:-0}"

# SSH defaults
SETUP_SSH_KEYS="${SETUP_SSH_KEYS:-0}"
GITHUB_USERNAME="${GITHUB_USERNAME:-}"
GENERATE_SSH_KEY="${GENERATE_SSH_KEY:-1}"
SSH_KEY_TYPE="${SSH_KEY_TYPE:-ed25519}"


# Prompt for user input with default
prompt_with_default() {
  local prompt="$1"
  local default="$2"
  local result
  
  read -p "$prompt [$default]: " result
  echo "${result:-$default}"
}

# Prompt for yes/no with default
prompt_yes_no() {
  local prompt="$1"
  local default="${2:-n}"
  local result
  
  read -p "$prompt [y/N]: " result
  result="${result:-$default}"
  [[ "$result" =~ ^[Yy]$ ]]
}

# Configure NAS mounting
configure_nas_mounting() {
  echo ""
  echo "=== NAS Drive Configuration ==="
  echo "Configure remote storage mounting (optional but recommended)"
  echo ""
  
  if ! prompt_yes_no "Do you want to configure NAS/remote storage mounting?"; then
    echo "Skipping NAS configuration"
    return 0
  fi
  
  # Detect local network
  local local_ip
  local_ip=$(hostname -I | awk '{print $1}' 2>/dev/null || echo "10.0.0.100")
  local subnet
  subnet=$(echo "$local_ip" | sed 's/\.[0-9]*$//')
  
  echo ""
  echo "Configure your NAS connection:"
  
  # Get NAS configuration
  local nas_ip
  local nas_share
  local mount_point
  local username
  local password
  local mount_type
  
  nas_ip=$(prompt_with_default "NAS IP address" "${subnet}.100")
  nas_share=$(prompt_with_default "NAS share name" "PiTracShare")
  mount_point=$(prompt_with_default "Local mount point" "/mnt/PiTracShare")
  
  echo ""
  echo "Choose mounting method:"
  echo "  1) NFS (recommended - simpler, no credentials needed)"
  echo "  2) CIFS/SMB (requires username/password)"
  
  read -p "Select method [1]: " method_choice
  method_choice="${method_choice:-1}"
  
  case "$method_choice" in
    "2")
      mount_type="cifs"
      username=$(prompt_with_default "Username" "$(whoami)")
      read -s -p "Password: " password
      echo ""
      ;;
    *)
      mount_type="nfs"
      ;;
  esac
  
  # Create mount point
  echo "Creating mount point: $mount_point"
  $SUDO mkdir -p "$mount_point"
  
  # Backup fstab
  if [ ! -f "${FSTAB_FILE}.ORIGINAL" ]; then
    $SUDO cp "$FSTAB_FILE" "${FSTAB_FILE}.ORIGINAL"
  fi
  
  # Remove any existing entry for this mount point
  $SUDO sed -i "\|$mount_point|d" "$FSTAB_FILE"
  
  # Add new mount entry
  local fstab_entry
  case "$mount_type" in
    "nfs")
      fstab_entry="$nas_ip:/$nas_share $mount_point nfs _netdev,auto 0 0"
      ;;
    "cifs")
      fstab_entry="//$nas_ip/$nas_share $mount_point cifs username=$username,password=$password,workgroup=WORKGROUP,users,exec,auto,rw,file_mode=0777,dir_mode=0777,user_xattr 0 0"
      # Set restrictive permissions on fstab for password security
      $SUDO chmod 600 "$FSTAB_FILE"
      ;;
  esac
  
  echo "Adding mount entry to fstab..."
  echo "$fstab_entry" | $SUDO tee -a "$FSTAB_FILE" >/dev/null
  
  # Install required packages
  case "$mount_type" in
    "nfs")
      apt_ensure nfs-common
      ;;
    "cifs")
      apt_ensure cifs-utils
      ;;
  esac
  
  # Test the mount
  echo "Testing NAS connection..."
  $SUDO systemctl daemon-reload
  
  if $SUDO mount -a 2>/dev/null; then
    if mountpoint -q "$mount_point"; then
      echo "NAS mounted successfully at $mount_point"
      ls -la "$mount_point" | head -5
    else
      log_warn "Mount command succeeded but mount point is not active"
    fi
  else
    log_warn "Mount test failed. Please check:"
    echo "  - NAS IP address and share name are correct"
    echo "  - Network connectivity to NAS"
    echo "  - NAS permissions allow access from this Pi"
    echo "  - For CIFS: username and password are correct"
  fi
  
  echo "NAS configuration completed"
}

# Configure Samba server for inter-Pi sharing
configure_samba_server() {
  echo ""
  echo "=== Samba Server Configuration ==="
  echo "Configure file sharing between Pis (recommended for multi-Pi setups)"
  echo ""
  
  if ! prompt_yes_no "Do you want to configure Samba file sharing?"; then
    echo "Skipping Samba configuration"
    return 0
  fi
  
  # Install Samba
  log_info "Installing Samba packages..."
  apt_ensure samba samba-common-bin
  
  # Get configuration
  local share_name
  local share_path
  local username
  
  username="$(whoami)"
  share_name=$(prompt_with_default "Share name" "LM_Shares")
  share_path=$(prompt_with_default "Directory to share" "/home/$username/LM_Shares")
  
  # Create shared directory structure
  echo "Creating shared directories..."
  mkdir -p "$share_path/WebShare"
  mkdir -p "$share_path/Images"
  
  # Backup Samba config
  if [ ! -f "${SAMBA_CONFIG}.ORIGINAL" ]; then
    $SUDO cp "$SAMBA_CONFIG" "${SAMBA_CONFIG}.ORIGINAL"
  fi
  
  # Remove existing share configuration if present
  $SUDO sed -i "/^\[$share_name\]/,/^$/d" "$SAMBA_CONFIG"
  
  # Add new share configuration
  echo "Adding Samba share configuration..."
  cat << EOF | $SUDO tee -a "$SAMBA_CONFIG" >/dev/null

[$share_name]
path = $share_path
writeable = Yes
create mask = 0777
directory mask = 0777
public = no
EOF
  
  # Set up Samba user
  echo "Setting up Samba user: $username"
  echo "Please enter a password for Samba access (can be same as login password):"
  $SUDO smbpasswd -a "$username"
  
  # Start and enable Samba service
  echo "Starting Samba service..."
  $SUDO systemctl restart smbd
  $SUDO systemctl enable smbd
  
  # Verify service
  if $SUDO systemctl is-active smbd >/dev/null; then
    echo "Samba service is running"
    
    # Show connection information
    local pi_ip
    pi_ip=$(hostname -I | awk '{print $1}')
    echo ""
    echo "Samba share configured successfully!"
    echo "Share path: $share_path"
    echo "Access from other Pi: //$pi_ip/$share_name"
    echo "Username: $username"
    echo ""
    echo "To mount from another Pi, add to /etc/fstab:"
    echo "//$pi_ip/$share_name /home/<user>/LM_Shares cifs username=$username,password=<pwd>,workgroup=WORKGROUP,users,exec,auto,rw,file_mode=0777,dir_mode=0777,user_xattr 0 0"
  else
    log_warn "Samba service failed to start"
    echo "Check logs with: sudo systemctl status smbd"
  fi
  
  echo "Samba configuration completed"
}

# Configure SSH key authentication
configure_ssh_keys() {
  echo ""
  echo "=== SSH Key Configuration ==="
  echo "Configure SSH key authentication for password-less login"
  echo ""
  
  if ! prompt_yes_no "Do you want to configure SSH key authentication?"; then
    echo "Skipping SSH key configuration"
    return 0
  fi
  
  # Create SSH directory
  local ssh_dir="$HOME/.ssh"
  mkdir -p "$ssh_dir"
  chmod 700 "$ssh_dir"
  
  local authorized_keys="$ssh_dir/authorized_keys"
  
  echo ""
  echo "SSH Key Setup Options:"
  echo "  1) Paste public key manually"
  echo "  2) Generate new key pair"
  echo "  3) Copy from existing file"
  
  read -p "Select option [1]: " key_option
  key_option="${key_option:-1}"
  
  case "$key_option" in
    "2")
      # Generate new key pair
      local key_file="$ssh_dir/pitrac_key"
      if [ ! -f "$key_file" ]; then
        echo "Generating new SSH key pair..."
        ssh-keygen -t rsa -b 4096 -f "$key_file" -N "" -C "pitrac@$(hostname)"
        echo "Key pair generated:"
        echo "  Private key: $key_file"
        echo "  Public key: $key_file.pub"
        echo ""
        echo "Public key to share with remote machines:"
        cat "$key_file.pub"
      else
        echo "Key pair already exists at $key_file"
      fi
      ;;
      
    "3")
      # Copy from existing file
      read -p "Path to public key file: " pub_key_file
      if [ -f "$pub_key_file" ]; then
        echo "Adding public key from $pub_key_file"
        cat "$pub_key_file" >> "$authorized_keys"
        echo "Public key added"
      else
        log_warn "File not found: $pub_key_file"
      fi
      ;;
      
    *)
      # Manual paste
      echo "Paste your public key (starts with ssh-rsa, ssh-ed25519, etc.):"
      echo "Press Enter when done, Ctrl+D to finish:"
      
      # Read public key from stdin
      local public_key
      public_key=$(cat)
      
      if [ -n "$public_key" ]; then
        echo "$public_key" >> "$authorized_keys"
        echo "Public key added"
      else
        echo "No key provided"
      fi
      ;;
  esac
  
  # Set proper permissions
  if [ -f "$authorized_keys" ]; then
    chmod 644 "$authorized_keys"
    echo "SSH key configuration completed"
    echo "Authorized keys file: $authorized_keys"
  fi
}

# Check if network services are configured
is_network_services_installed() {
  # Check if at least one network service is configured
  # (This is a basic check - more sophisticated detection could be added)
  systemctl is-enabled smbd >/dev/null 2>&1 && return 0
  grep -q "/mnt/" /etc/fstab 2>/dev/null && return 0
  [ -f "$HOME/.ssh/authorized_keys" ] && return 0
  return 1
}

# Main configuration
configure_network_services() {
  echo "=== Network Services Configuration ==="
  echo "Configure network services for PiTrac multi-Pi setup"
  echo ""
  
  # Check if already configured
  if is_network_services_installed && [ "$FORCE" != "1" ]; then
    echo "Network services appear to already be configured."
    echo "Set FORCE=1 to reconfigure"
    echo ""
    
    if prompt_yes_no "Show current configuration?"; then
      echo ""
      echo "Current mount points:"
      mount | grep -E "(nfs|cifs)" || echo "  None found"
      echo ""
      echo "Samba service status:"
      systemctl is-active smbd >/dev/null 2>&1 && echo "  Active" || echo "  Inactive"
      echo ""
      echo "SSH authorized keys:"
      [ -f "$HOME/.ssh/authorized_keys" ] && echo "  Configured" || echo "  Not configured"
    fi
    return 0
  fi
  
  # Run configuration steps
  configure_nas_mounting
  configure_samba_server  
  configure_ssh_keys
  
  echo ""
  echo "Network services configuration completed!"
  echo ""
  echo "Services configured:"
  echo "- NAS mounting: $(grep -q "/mnt/" /etc/fstab && echo "Yes" || echo "No")"
  echo "- Samba server: $(systemctl is-enabled smbd >/dev/null 2>&1 && echo "Yes" || echo "No")"
  echo "- SSH keys: $([ -f "$HOME/.ssh/authorized_keys" ] && echo "Yes" || echo "No")"
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  configure_network_services
fi