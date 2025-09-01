#!/usr/bin/env bash

# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags

# config/backup.sh - Backup PiTrac configuration files

backup_name="${args[--name]:-}"
user_config="${HOME}/.pitrac/config/pitrac.yaml"
config_dir="${HOME}/.pitrac/config"
backup_dir="${HOME}/.pitrac/backups"
system_config="/etc/pitrac/golf_sim_config.json"

# Create backup directory if needed
mkdir -p "$backup_dir"

# Generate backup name if not provided
if [[ -z "$backup_name" ]]; then
    backup_name="pitrac-backup-$(date +%Y%m%d-%H%M%S)"
else
    # Sanitize user-provided name
    backup_name="${backup_name//[^a-zA-Z0-9-_]/}"
fi

backup_path="${backup_dir}/${backup_name}.tar.gz"

# Check if backup already exists
if [[ -f "$backup_path" ]]; then
    error "Backup '$backup_name' already exists"
    info "Use a different name or let the system generate one"
    exit 1
fi

log_info "Creating backup: $backup_name"

# Create temporary directory for staging
tmp_dir=$(mktemp -d)
trap "rm -rf $tmp_dir" EXIT

# Stage files for backup
backup_manifest="${tmp_dir}/manifest.txt"
touch "$backup_manifest"

# Copy user configuration
if [[ -f "$user_config" ]]; then
    cp "$user_config" "${tmp_dir}/pitrac.yaml"
    echo "pitrac.yaml" >> "$backup_manifest"
    success "Backed up user configuration"
else
    warn "No user configuration found at $user_config"
fi

# Copy system configuration (if modified)
if [[ -f "$system_config" ]]; then
    # Check if it differs from default
    if command -v md5sum >/dev/null 2>&1; then
        current_hash=$(md5sum "$system_config" | cut -d' ' -f1)
        # Note: We'd need to store original hash somewhere
        # For now, always backup if it exists
        cp "$system_config" "${tmp_dir}/golf_sim_config.json"
        echo "golf_sim_config.json" >> "$backup_manifest"
        info "Backed up system configuration"
    else
        cp "$system_config" "${tmp_dir}/golf_sim_config.json"
        echo "golf_sim_config.json" >> "$backup_manifest"
        info "Backed up system configuration"
    fi
fi

# Copy any custom settings files
if [[ -d "$config_dir" ]]; then
    for file in "$config_dir"/*.yaml "$config_dir"/*.yml "$config_dir"/*.json; do
        if [[ -f "$file" ]] && [[ "$(basename "$file")" != "pitrac.yaml" ]]; then
            cp "$file" "${tmp_dir}/"
            echo "$(basename "$file")" >> "$backup_manifest"
        fi
    done
fi

# Store environment variables if they exist
if [[ -f "${config_dir}/environment" ]]; then
    cp "${config_dir}/environment" "${tmp_dir}/environment"
    echo "environment" >> "$backup_manifest"
    info "Backed up environment settings"
fi

# Add metadata
cat > "${tmp_dir}/backup-info.txt" << EOF
PiTrac Configuration Backup
Created: $(date)
Hostname: $(hostname)
User: $(whoami)
PiTrac Version: $(pitrac --version 2>/dev/null || echo "unknown")
Backup Name: $backup_name

Files included:
$(cat "$backup_manifest")
EOF

# Create the backup archive
cd "$tmp_dir"
if tar czf "$backup_path" * 2>/dev/null; then
    success "Backup created successfully: $backup_path"
    
    # Show backup size
    backup_size=$(du -h "$backup_path" | cut -f1)
    info "Backup size: $backup_size"
    
    # List recent backups
    echo ""
    info "Recent backups:"
    ls -lht "$backup_dir"/*.tar.gz 2>/dev/null | head -5 | while read -r line; do
        echo "  $line"
    done
    
    echo ""
    info "To restore this backup, run:"
    echo "  pitrac config restore $backup_name"
else
    error "Failed to create backup archive"
    exit 1
fi

# Clean up old backups (keep last 10)
backup_count=$(ls -1 "$backup_dir"/*.tar.gz 2>/dev/null | wc -l)
if [[ $backup_count -gt 10 ]]; then
    old_count=$((backup_count - 10))
    ls -1t "$backup_dir"/*.tar.gz | tail -n "$old_count" | while read -r old_backup; do
        rm -f "$old_backup"
        warn "Removed old backup: $(basename "$old_backup")"
    done
fi
