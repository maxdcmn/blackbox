#!/bin/bash
# Common functions for all scripts

# Get project root directory (looks for .env or scripts/blackbox-server structure)
get_project_root() {
    local current_dir="$(pwd)"
    local check_dir="$current_dir"
    
    # Go up to 5 levels looking for project root
    for i in {0..5}; do
        if [ -f "$check_dir/.env" ] || ([ -d "$check_dir/scripts" ] && [ -d "$check_dir/blackbox-server" ]); then
            echo "$check_dir"
            return 0
        fi
        if [ "$check_dir" = "/" ]; then
            break
        fi
        check_dir="$(cd "$check_dir/.." && pwd)"
    done
    
    # Fallback: assume scripts are in scripts/ subdirectory
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)"
    local project_root="$(cd "$script_dir/../.." && pwd)"
    echo "$project_root"
}

# Load .env file from project root
load_env() {
    local project_root="$(get_project_root)"
    local env_file="$project_root/.env"
    
    if [ -f "$env_file" ]; then
        set -a
        while IFS= read -r line || [ -n "$line" ]; do
            # Skip empty lines and comments
            line=$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
            if [ -z "$line" ] || [[ "$line" =~ ^# ]]; then
                continue
            fi
            # Export the variable
            export "$line" 2>/dev/null || true
        done < "$env_file"
        set +a
        return 0
    fi
    
    return 1
}
