#!/usr/bin/env bats

setup() {
  export MODMAN_REAL="${BATS_TEST_DIRNAME}/../../modman"
  [ -x "${MODMAN_REAL}" ] || skip "real modman not executable at ${MODMAN_REAL}"
}

@test "modman --progress-file=PATH --help parses without error" {
  run "${MODMAN_REAL}" --progress-file="/tmp/modman-test-progress-$$" --help
  [ "${status}" -eq 0 ]
  [[ "${output}" == *"Использование"* ]] || [[ "${output}" == *"Usage"* ]]
}

@test "modman --progress-file PATH --help (split form) parses without error" {
  run "${MODMAN_REAL}" --progress-file "/tmp/modman-test-progress-$$" --help
  [ "${status}" -eq 0 ]
  [[ "${output}" == *"Использование"* ]] || [[ "${output}" == *"Usage"* ]]
}

@test "modman --progress-file does not change -Ss exit/output (no leak into filtered_args)" {
  run "${MODMAN_REAL}" --machine -Ss nonexistent_xyzzy_12345
  local baseline_status="${status}"
  local baseline_output="${output}"

  run "${MODMAN_REAL}" --progress-file="/tmp/modman-test-progress-$$" --machine -Ss nonexistent_xyzzy_12345
  [ "${status}" -eq "${baseline_status}" ]
  [ "${output}" = "${baseline_output}" ]
}

@test "modman --progress-file accepts --machine before it (split form)" {
  run "${MODMAN_REAL}" --machine -Ss nonexistent_xyzzy_12345
  local baseline_status="${status}"
  local baseline_output="${output}"

  run "${MODMAN_REAL}" --machine --progress-file "/tmp/modman-test-progress-$$" -Ss nonexistent_xyzzy_12345
  [ "${status}" -eq "${baseline_status}" ]
  [ "${output}" = "${baseline_output}" ]
}

@test "modman --progress-file=PATH exports MODMAN_PROGRESS_FILE for download_file()" {
  local sentinel="${BATS_TEST_TMPDIR}/sentinel-progress.txt"
  local probe_log="${BATS_TEST_TMPDIR}/wget-probe.log"
  local cache_dir="${BATS_TEST_TMPDIR}/cache"
  local download_dir="${BATS_TEST_TMPDIR}/dl"

  mkdir -p "${cache_dir}" "${download_dir}"
  printf 'testmod-1.0.pfs\t100M\t2026-01-01\ttest module description\textra\n' \
    >"${cache_dir}/db.txt"

  cat >"${BATS_TEST_TMPDIR}/wget" <<PROBE
#!/bin/sh
printf 'MODMAN_PROGRESS_FILE=%s\n' "\${MODMAN_PROGRESS_FILE:-<unset>}" >>"${probe_log}"
exit 1
PROBE
  chmod +x "${BATS_TEST_TMPDIR}/wget"

  PATH="${BATS_TEST_TMPDIR}:${PATH}" \
    CACHE_FILE="${cache_dir}/db.txt" \
    DOWNLOAD_DIR="${download_dir}" \
    run "${MODMAN_REAL}" \
      --progress-file="${sentinel}" \
      -S testmod

  [ -f "${probe_log}" ]
  grep -q "MODMAN_PROGRESS_FILE=${sentinel}" "${probe_log}"
}
