#!/usr/bin/env bats

setup() {
  export SELFTEST="${BATS_TEST_DIRNAME}/../../modman-selftest"
  export MODMAN_REAL="${BATS_TEST_DIRNAME}/../../modman"
  [ -x "${SELFTEST}" ] || skip "modman-selftest not executable at ${SELFTEST}"
  [ -x "${MODMAN_REAL}" ] || skip "real modman not executable at ${MODMAN_REAL}"
}

@test "modman-selftest reaches 'Test finished' with no FALSE entries" {
  # Run selftest with modman from the worktree on PATH
  PATH="${BATS_TEST_DIRNAME}/../..:${PATH}" \
    run "${SELFTEST}"

  # Must contain 'Test finished'
  [[ "${output}" == *"Test finished"* ]]

  # Strip ANSI codes and check no literal FALSE remains
  local clean
  clean="$(printf '%s\n' "${output}" | sed 's/\x1b\[[0-9;]*m//g')"
  if printf '%s\n' "${clean}" | grep -q 'FALSE'; then
    # Print the offending lines for debugging
    printf '%s\n' "${clean}" | grep 'FALSE' >&2
    return 1
  fi
}
