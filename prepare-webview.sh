#!/bin/bash
set -euo pipefail
device_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$(cd "$device_dir/../../.." && pwd)"
prebuilt_dir="$source_root/vendor/xiaomi/mocha/prebuilt/webview"
target_dir="$source_root/external/chromium-webview/prebuilt/arm"
test -d "$target_dir"
(cd "$prebuilt_dir" && sha256sum --check SHA256SUMS)
if ! cmp -s "$prebuilt_dir/webview.apk" "$target_dir/webview.apk"; then
    install -m 0644 "$prebuilt_dir/webview.apk" "$target_dir/webview.apk"
fi
echo 'Validated WebView 115.0.5781.0 prebuilt is ready.'
