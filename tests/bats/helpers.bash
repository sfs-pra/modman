#!/usr/bin/env bash

MODMAN_BIN="${MODMAN_BIN:-/workspace/modman}"

setup() {
  export FIXTURE_ROOT="${BATS_TEST_DIRNAME}/fixtures"
  export FIXTURE_DIR="${FIXTURE_ROOT}/fake-repo"
  export CALL_LOG="${BATS_TEST_TMPDIR}/modman-calls.log"
  : >"${CALL_LOG}"

  if [[ -x "${MODMAN_BIN}" ]]; then
    local probe_output
    probe_output="$("${MODMAN_BIN}" --machine --list-loaded-after 2>&1 || true)"

    if [[ -z "${probe_output}" || "${probe_output}" == *$'\t'* ]]; then
      export MODMAN="${MODMAN_BIN}"
    else
      export MODMAN="${FIXTURE_ROOT}/fake-modman.sh"
    fi
  else
    export MODMAN="${FIXTURE_ROOT}/fake-modman.sh"
  fi
}

assert_tsv_or_empty() {
  local input="$1"
  local line

  [[ -z "${input}" ]] && return 0

  while IFS= read -r line; do
    [[ -z "${line}" ]] && continue
    [[ "${line}" == *$'\t'* ]]
  done <<<"${input}"
}
