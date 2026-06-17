#!/usr/bin/env bash
# Fake modman for modman-open tests.
# Records argv to FAKE_MODMAN_LOG, then emits canned responses.
set -euo pipefail

LOG="${FAKE_MODMAN_LOG:-/tmp/fake-modman-open.log}"
printf '%s\n' "$*" >> "$LOG"

# Parse args to decide response
machine_mode=0
if [[ "${1:-}" == "--machine" ]]; then
    machine_mode=1
    shift
fi

while [[ "${1:-}" == "--lower" || "${1:-}" == "--upper" || "${1:-}" == "--toram" ]]; do
    shift
done

case "${1:-}" in
    "-Qp")
        path="${2:-}"
        if [ -f "$path" ]; then
            name=$(basename "$path" .pfs)
            printf 'file\t%s\t%s\tsquashfs\tlocal\t4.0K\tsquashfs xz\tapp:core\tgtk3 libfoo\n' "$path" "$name"
            exit 0
        else
            printf 'error\tfile not found: %s\n' "$path"
            exit 1
        fi
        ;;
    "-S")
        printf 'ok\tattached\n'
        exit 0
        ;;
    "-R")
        printf 'ok\tunloaded\n'
        exit 0
        ;;
    *)
        printf 'error\tunknown command\n'
        exit 1
        ;;
esac
