
# Initialize global flags and logging (libraries are embedded by bashly)
initialize_global_flags


backup="${args[--backup]:-1}"
reset_config "$backup"
