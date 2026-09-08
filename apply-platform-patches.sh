#!/bin/bash
set -e

device_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$(cd "$device_dir/../../.." && pwd)"

apply_project_patches() {
    local project_dir="$1"
    local patch_dir="$2"
    local patch
    for patch in "$device_dir/patches/$patch_dir"/*.patch; do
        if git -C "$project_dir" apply --reverse --check "$patch" 2>/dev/null; then
            echo "Platform patch already applied: $(basename "$patch")"
        else
            git -C "$project_dir" apply --check "$patch"
            git -C "$project_dir" apply "$patch"
            echo "Applied platform patch: $(basename "$patch")"
        fi
    done
}

apply_project_patches "$source_root/frameworks/native" frameworks_native
apply_project_patches "$source_root/frameworks/base" frameworks_base
apply_project_patches "$source_root/hardware/interfaces" hardware_interfaces
