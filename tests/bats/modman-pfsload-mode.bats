#!/usr/bin/env bats

setup() {
  export MODMAN_REAL="${BATS_TEST_DIRNAME}/../../modman"
  [ -x "${MODMAN_REAL}" ] || skip "real modman not executable at ${MODMAN_REAL}"
}

@test "modman --lower --help parses without error" {
  run "${MODMAN_REAL}" --lower --help
  [ "${status}" -eq 0 ]
  [[ "${output}" == *"Использование"* ]] || [[ "${output}" == *"Usage"* ]]
}

@test "modman --upper --download-dir --progress-file -S invokes CMD_LOAD stub with -u as first arg" {
  local cache_dir="${BATS_TEST_TMPDIR}/cache"
  local download_dir="${BATS_TEST_TMPDIR}/dl"
  local probe_log="${BATS_TEST_TMPDIR}/cmdload-probe.log"
  local progress_file="${BATS_TEST_TMPDIR}/progress.txt"

  mkdir -p "${cache_dir}" "${download_dir}"
  printf 'testmod-1.0.pfs\t100M\t2026-01-01\ttest module description\textra\n' \
    >"${cache_dir}/db.txt"

  cat >"${BATS_TEST_TMPDIR}/pfsload" <<PROBE
#!/bin/sh
printf '%s\n' "\$@" >>"${probe_log}"
exit 0
PROBE
  chmod +x "${BATS_TEST_TMPDIR}/pfsload"

  cat >"${BATS_TEST_TMPDIR}/wget" <<STUB
#!/bin/sh
cp "${download_dir}/../cache/db.txt" "${download_dir}/testmod-1.0.pfs" 2>/dev/null || touch "${download_dir}/testmod-1.0.pfs"
exit 0
STUB
  chmod +x "${BATS_TEST_TMPDIR}/wget"

  PATH="${BATS_TEST_TMPDIR}:${PATH}" \
    CMD_LOAD="pfsload" \
    CACHE_FILE="${cache_dir}/db.txt" \
    DOWNLOAD_DIR="${download_dir}" \
    run "${MODMAN_REAL}" \
      --upper \
      --download-dir "${download_dir}" \
      --progress-file "${progress_file}" \
      -S testmod

  [ -f "${probe_log}" ]
  local first_arg
  first_arg=$(head -n1 "${probe_log}")
  [ "${first_arg}" = "-u" ]
}

@test "modman --lower --toram both forwarded to CMD_LOAD as separate argv tokens" {
  local cache_dir="${BATS_TEST_TMPDIR}/cache"
  local download_dir="${BATS_TEST_TMPDIR}/dl"
  local probe_log="${BATS_TEST_TMPDIR}/cmdload-probe.log"

  mkdir -p "${cache_dir}" "${download_dir}"
  printf 'testmod-1.0.pfs\t100M\t2026-01-01\ttest module description\textra\n' \
    >"${cache_dir}/db.txt"

  cat >"${BATS_TEST_TMPDIR}/pfsload" <<PROBE
#!/bin/sh
printf '%s\n' "\$@" >>"${probe_log}"
exit 0
PROBE
  chmod +x "${BATS_TEST_TMPDIR}/pfsload"

  cat >"${BATS_TEST_TMPDIR}/wget" <<STUB
#!/bin/sh
cp "${download_dir}/../cache/db.txt" "${download_dir}/testmod-1.0.pfs" 2>/dev/null || touch "${download_dir}/testmod-1.0.pfs"
exit 0
STUB
  chmod +x "${BATS_TEST_TMPDIR}/wget"

  PATH="${BATS_TEST_TMPDIR}:${PATH}" \
    CMD_LOAD="pfsload" \
    CACHE_FILE="${cache_dir}/db.txt" \
    DOWNLOAD_DIR="${download_dir}" \
    run "${MODMAN_REAL}" \
      --lower \
      --toram \
      --download-dir "${download_dir}" \
      -S testmod

  [ -f "${probe_log}" ]
  local log_content
  log_content=$(cat "${probe_log}")
  [[ "${log_content}" == *"-l"* ]]
  [[ "${log_content}" == *"-r"* ]]
  local first_arg second_arg
  first_arg=$(sed -n '1p' "${probe_log}")
  second_arg=$(sed -n '2p' "${probe_log}")
  [ "${first_arg}" = "-l" ]
  [ "${second_arg}" = "-r" ]
}

@test "modman --lower --upper exits with error code 2" {
  run "${MODMAN_REAL}" --lower --upper --help
  [ "${status}" -eq 2 ]
}
