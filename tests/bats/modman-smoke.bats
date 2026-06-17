#!/usr/bin/env bats

load './helpers.bash'

@test "modman --help exits 0 and prints Usage" {
  run "${MODMAN}" --help
  [ "${status}" -eq 0 ]
  [[ "${output}" == *"Usage"* ]] || [[ "${output}" == *"usage"* ]] || [[ "${output}" == *"Использование"* ]]
}

@test "modman --machine --list-loaded-after on empty system is valid TSV or empty" {
  run "${MODMAN}" --machine --list-loaded-after
  [ "${status}" -eq 0 ]
  assert_tsv_or_empty "${output}"
}

@test "modman --machine -Ss nonexistent returns empty" {
  run "${MODMAN}" --machine -Ss nonexistent_xyzzy_12345
  [ "${status}" -eq 0 ]
  [ -z "${output}" ]
}

@test "modman -S with invalid name exits 2 with error" {
  run "${MODMAN}" -S "invalid;name"
  [ "${status}" -ne 0 ]
  [ "${status}" -eq 2 ] || [[ "${output}" == *"invalid"* ]]
}

@test "modman --machine --list-local with fixture DOWNLOAD_DIR" {
  run env DOWNLOAD_DIR="${FIXTURE_DIR}" "${MODMAN}" --machine --list-local
  [ "${status}" -eq 0 ]
}

@test "modman --machine --list-local emits absolute paths for relative DOWNLOAD_DIR" {
  local expected_path
  local modman_under_test

  mkdir -p "${BATS_TEST_TMPDIR}/work/optional"
  touch "${BATS_TEST_TMPDIR}/work/optional/relative-demo.pfs"
  expected_path=$(realpath "${BATS_TEST_TMPDIR}/work/optional/relative-demo.pfs")
  modman_under_test="${BATS_TEST_DIRNAME}/../../modman"

  run bash -c 'cd "$1" && DOWNLOAD_DIR=optional "$2" --machine --list-local' _ \
    "${BATS_TEST_TMPDIR}/work" "${modman_under_test}"

  [ "${status}" -eq 0 ]
  [[ "${output}" == *$'relative-demo.pfs\t'* ]]
  [[ "${output}" == *"${expected_path}"* ]]
  [[ "${output}" != *$'\toptional/relative-demo.pfs'* ]]
}
