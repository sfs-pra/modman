#!/usr/bin/env bats

setup() {
  export MODMAN_REAL="${BATS_TEST_DIRNAME}/../../modman"
  [ -x "${MODMAN_REAL}" ] || skip "real modman not executable at ${MODMAN_REAL}"
}

make_fixture() {
  local cache_dir="${BATS_TEST_TMPDIR}/cache"
  local download_dir="${BATS_TEST_TMPDIR}/dl"
  mkdir -p "${cache_dir}" "${download_dir}"
  printf 'testmod-1.0.pfs\t100M\t2026-01-01\ttest module description\textra\n' \
    >"${cache_dir}/db.txt"
  cat >"${BATS_TEST_TMPDIR}/wget" <<'STUB'
#!/bin/sh
touch "${DOWNLOAD_DIR}/testmod-1.0.pfs"
exit 0
STUB
  chmod +x "${BATS_TEST_TMPDIR}/wget"
}

@test "--load-cmd happy path: override command is invoked with file path" {
  make_fixture
  local cache_dir="${BATS_TEST_TMPDIR}/cache"
  local download_dir="${BATS_TEST_TMPDIR}/dl"
  local probe_log="${BATS_TEST_TMPDIR}/loadcmd-probe.log"

  cat >"${BATS_TEST_TMPDIR}/wget" <<STUB
#!/bin/sh
touch "${download_dir}/testmod-1.0.pfs"
exit 0
STUB
  chmod +x "${BATS_TEST_TMPDIR}/wget"

  cat >"${BATS_TEST_TMPDIR}/myload" <<PROBE
#!/bin/sh
printf 'OVERRIDE_RAN %s\n' "\$@" >>"${probe_log}"
exit 0
PROBE
  chmod +x "${BATS_TEST_TMPDIR}/myload"

  PATH="${BATS_TEST_TMPDIR}:${PATH}" \
    CACHE_FILE="${cache_dir}/db.txt" \
    DOWNLOAD_DIR="${download_dir}" \
    run "${MODMAN_REAL}" \
      --load-cmd="myload" \
      --download-dir "${download_dir}" \
      -S testmod

  [ "${status}" -eq 0 ]
  [ -f "${probe_log}" ]
  grep -q "OVERRIDE_RAN" "${probe_log}"
}

@test "--load-cmd with false: exit code propagated non-zero" {
  make_fixture
  local cache_dir="${BATS_TEST_TMPDIR}/cache"
  local download_dir="${BATS_TEST_TMPDIR}/dl"

  cat >"${BATS_TEST_TMPDIR}/wget" <<STUB
#!/bin/sh
touch "${download_dir}/testmod-1.0.pfs"
exit 0
STUB
  chmod +x "${BATS_TEST_TMPDIR}/wget"

  cat >"${BATS_TEST_TMPDIR}/failcmd" <<'PROBE'
#!/bin/sh
exit 1
PROBE
  chmod +x "${BATS_TEST_TMPDIR}/failcmd"

  PATH="${BATS_TEST_TMPDIR}:${PATH}" \
    CACHE_FILE="${cache_dir}/db.txt" \
    DOWNLOAD_DIR="${download_dir}" \
    run "${MODMAN_REAL}" \
      --load-cmd="failcmd" \
      --download-dir "${download_dir}" \
      -S testmod

  [ "${status}" -ne 0 ]
}

@test "--load-cmd empty string falls back to CMD_LOAD stub" {
  make_fixture
  local cache_dir="${BATS_TEST_TMPDIR}/cache"
  local download_dir="${BATS_TEST_TMPDIR}/dl"
  local probe_log="${BATS_TEST_TMPDIR}/cmdload-probe.log"

  cat >"${BATS_TEST_TMPDIR}/wget" <<STUB
#!/bin/sh
touch "${download_dir}/testmod-1.0.pfs"
exit 0
STUB
  chmod +x "${BATS_TEST_TMPDIR}/wget"

  cat >"${BATS_TEST_TMPDIR}/pfsload" <<PROBE
#!/bin/sh
printf 'DEFAULT_INVOKED %s\n' "\$@" >>"${probe_log}"
exit 0
PROBE
  chmod +x "${BATS_TEST_TMPDIR}/pfsload"

  PATH="${BATS_TEST_TMPDIR}:${PATH}" \
    CMD_LOAD="pfsload" \
    CACHE_FILE="${cache_dir}/db.txt" \
    DOWNLOAD_DIR="${download_dir}" \
    run "${MODMAN_REAL}" \
      --load-cmd="" \
      --download-dir "${download_dir}" \
      -S testmod

  [ "${status}" -eq 0 ]
  [ -f "${probe_log}" ]
  grep -q "DEFAULT_INVOKED" "${probe_log}"
}

@test "--load-cmd with multi-word command (echo OVERRIDE_RAN) runs override" {
  make_fixture
  local cache_dir="${BATS_TEST_TMPDIR}/cache"
  local download_dir="${BATS_TEST_TMPDIR}/dl"

  cat >"${BATS_TEST_TMPDIR}/wget" <<STUB
#!/bin/sh
touch "${download_dir}/testmod-1.0.pfs"
exit 0
STUB
  chmod +x "${BATS_TEST_TMPDIR}/wget"

  PATH="${BATS_TEST_TMPDIR}:${PATH}" \
    CACHE_FILE="${cache_dir}/db.txt" \
    DOWNLOAD_DIR="${download_dir}" \
    run "${MODMAN_REAL}" \
      --load-cmd="echo OVERRIDE_RAN" \
      --download-dir "${download_dir}" \
      -S testmod

  [ "${status}" -eq 0 ]
  [[ "${output}" == *"OVERRIDE_RAN"* ]]
}
