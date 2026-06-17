#!/usr/bin/env bash
set -euo pipefail

log_file="${PFSLOAD_LOG:-${BATS_TEST_TMPDIR:-/tmp}/mock-pfsload.log}"
printf '%s\n' "$*" >> "${log_file}"
exit 0
