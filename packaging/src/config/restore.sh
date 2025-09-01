#!/usr/bin/env bash

# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags

# config/restore.sh - Restore PiTrac configuration from backup

backup_name="${args[backup_name]}"
force_restore="${args[--force]:-}"
user_config="${HOME}/.pitrac/config/pitrac.yaml"
config_dir="${HOME}/.pitrac/config"
backup_dir="${HOME}/.pitrac/backups"
system_config="/etc/pitrac/golf_sim_config.json"

# Handle special 'list' command
if [[ "$backup_name" == "list" ]]; then
    info "Available backups:"
    if [[ -d "$backup_dir" ]]; then
        ls -1t "$backup_dir"/*.tar.gz 2>/dev/null | while read -r backup_file; do
            if [[ -f "$backup_file" ]]; then
                name=$(basename "$backup_file" .tar.gz)
                size=$(du -h "$backup_file" | cut -f1)
                date=$(stat -c %y "$backup_file" 2>/dev/null | cut -d' ' -f1 || date -r "$backup_file" +%Y-%m-%d 2>/dev/null || echo "")
                echo "  $name ($size, $date)"
            fi
        done
    else
        echo "  No backups found"
    fi
    exit 0
fi

# Sanitize backup name
backup_name="${backup_name//[^a-zA-Z0-9-_]/}"

# Check if backup exists
backup_path="${backup_dir}/${backup_name}.tar.gz"
if [[ ! -f "$backup_path" ]]; then
    error "Backup '$backup_name' not found"
    echo ""
    info "Available backups:"
    ls -1 "$backup_dir"/*.tar.gz 2>/dev/null | while read -r backup_file; do
        basename "$backup_file" .tar.gz
    done | sed 's/^/  /'
    exit 1
fi

log_info "Restoring from backup: $backup_name"

# Create temporary directory for extraction
tmp_dir=$(mktemp -d)
trap "rm -rf $tmp_dir" EXIT

# Extract backup
if ! tar xzf "$backup_path" -C "$tmp_dir" 2>/dev/null; then
    error "Failed to extract backup archive"
    exit 1
fi

# Show backup info
if [[ -f "${tmp_dir}/backup-info.txt" ]]; then
    echo ""
    cat "${tmp_dir}/backup-info.txt"
    echo ""
fi

# Check for existing configuration
config_exists=false
if [[ -f "$user_config" ]]; then
    config_exists=true
    if [[ -z "$force_restore" ]]; then
        warn "Existing configuration found at $user_config"
        echo ""
        echo "This will overwrite your current configuration."
        echo "To proceed, run with --force flag:"
        echo "  pitrac config restore $backup_name --force"
        echo ""
        echo "Or backup current config first:"
        echo "  pitrac config backup current-$(date +%Y%m%d)"
        exit 1
    fi
fi

# Create config directory if needed
mkdir -p "$config_dir"

# Backup current config if it exists
if [[ "$config_exists" == "true" ]]; then
    backup_current="${config_dir}/pitrac.yaml.before-restore-$(date +%Y%m%d-%H%M%S)"
    cp "$user_config" "$backup_current"
    info "Current config backed up to: $backup_current"
fi

# Restore files
restored_count=0

# Restore user configuration
if [[ -f "${tmp_dir}/pitrac.yaml" ]]; then
    cp "${tmp_dir}/pitrac.yaml" "$user_config"
    success "Restored user configuration"
    ((restored_count++))
fi

# Restore system configuration (requires sudo)
if [[ -f "${tmp_dir}/golf_sim_config.json" ]]; then
    if [[ -w "$system_config" ]]; then
        cp "${tmp_dir}/golf_sim_config.json" "$system_config"
        success "Restored system configuration"
        ((restored_count++))
    else
        # Need sudo for system config
        warn "System configuration requires sudo to restore"
        echo "Run the following command to restore system config:"
        echo "  sudo cp ${tmp_dir}/golf_sim_config.json $system_config"
        
        # Save it to user directory for manual restore
        alt_path="${config_dir}/golf_sim_config.json.restore"
        cp "${tmp_dir}/golf_sim_config.json" "$alt_path"
        info "System config saved to: $alt_path"
    fi
fi

# Restore environment settings
if [[ -f "${tmp_dir}/environment" ]]; then
    cp "${tmp_dir}/environment" "${config_dir}/environment"
    success "Restored environment settings"
    ((restored_count++))
fi

# Restore any additional config files
for file in "${tmp_dir}"/*.yaml "${tmp_dir}"/*.yml "${tmp_dir}"/*.json; do
    if [[ -f "$file" ]]; then
        filename=$(basename "$file")
        # Skip already processed files
        if [[ "$filename" != "pitrac.yaml" ]] && \
           [[ "$filename" != "golf_sim_config.json" ]] && \
           [[ "$filename" != "backup-info.txt" ]] && \
           [[ "$filename" != "manifest.txt" ]]; then
            cp "$file" "${config_dir}/"
            info "Restored: $filename"
            ((restored_count++))
        fi
    fi
done

# Summary
echo ""
if [[ $restored_count -gt 0 ]]; then
    success "Restoration complete! Restored $restored_count file(s)"
    echo ""
    info "Next steps:"
    echo "  1. Validate configuration: pitrac config validate"
    echo "  2. Restart PiTrac if running: pitrac restart"
else
    warn "No files were restored"
fi

# Validate the restored configuration
log_info "Validating restored configuration..."
if pitrac config validate 2>/dev/null; then
    success "Configuration is valid"
else
    warn "Configuration validation failed - please review settings"
    echo "Run: pitrac config validate"
fi
