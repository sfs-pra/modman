#!/usr/bin/env bats

MODMAN_BIN="${BATS_TEST_DIRNAME}/../../modman"

load './helpers.bash'

setup() {
  export MODMAN="${MODMAN_BIN}"
}

@test "modman --machine -Qp missing file returns error tsv" {
  run "${MODMAN}" --machine -Qp /nonexistent.pfs
  [ "${status}" -eq 1 ]
  [[ "${output}" == $'error\t'* ]]
}

@test "modman --machine -Qp wrong extension returns error tsv" {
  local wrong_file
  wrong_file="${BATS_TEST_TMPDIR}/not-module.txt"
  printf 'hello\n' >"${wrong_file}"

  run "${MODMAN}" --machine -Qp "${wrong_file}"
  [ "${status}" -eq 1 ]
  [[ "${output}" == $'error\t'* ]]
}

@test "modman --machine -Qp valid pfs returns file row with metadata" {
  local fixture="${BATS_TEST_DIRNAME}/../fixtures/sample.pfs"
  local expected_path

  run "${MODMAN}" --machine -Qp "${fixture}"
  [ "${status}" -eq 0 ]
  [[ "${output}" == $'file\t'* ]]

  expected_path="$(realpath "${fixture}")"

  IFS=$'\t' read -r c1 c2 c3 c4 c5 c6 c7 c8 c9 extra <<<"${output}"
  [ "${c1}" = "file" ]
  [ "${c2}" = "${expected_path}" ]
  [ "${c3}" = "sample" ]
  case "${c4}" in
    squashfs|erofs|unknown) ;;
    *) return 1 ;;
  esac
  case "${c5}" in
    loaded|local) ;;
    *) return 1 ;;
  esac
  [ -n "${c6}" ]
  [ -z "${extra}" ]
}

@test "modman -Qp human mode localizes labels and preserves compression level" {
  local fixture="${BATS_TEST_DIRNAME}/../fixtures/sample.pfs"
  local fake_bin="${BATS_TEST_TMPDIR}/bin"
  local cache_dep="${BATS_TEST_TMPDIR}/dependencies.txt"
  mkdir -p "${fake_bin}"
  printf 'core : gtk3 libfoo\n' >"${cache_dep}"
  cat >"${fake_bin}/unsquashfs" <<'EOF'
#!/usr/bin/env bash
case "$1" in
  -s)
    printf 'Found a valid SQUASHFS 4:0 superblock\n'
    printf 'Compression zstd\n'
    printf 'Compression level 18\n'
    ;;
  -l)
    printf 'squashfs-root/var/lib/pfs/mount/app/core/pfs.files\n'
    printf 'squashfs-root/var/lib/pfs/mount/app/core/pfs.depends\n'
    ;;
  -cat)
    exit 99
    ;;
esac
EOF
  chmod +x "${fake_bin}/unsquashfs"

  PATH="${fake_bin}:$PATH" CACHE_DEP="${cache_dep}" run "${MODMAN}" -Qp "${fixture}"
  [ "${status}" -eq 0 ]
  [[ "${output}" == *"Файл:"* ]]
  [[ "${output}" == *"FS:       squashfs zstd 18"* ]]
  [[ "${output}" == *$'Модули:\n  core'* ]]
  [[ "${output}" == *$'Зависимости:\n  gtk3\n  libfoo'* ]]
  [[ "${output}" != *"app:core"* ]]
  [[ "${output}" != *"File:"* ]]
}

@test "modman -Qp human mode reads legacy /etc/packages module manifests" {
  local fixture="${BATS_TEST_DIRNAME}/../fixtures/sample.pfs"
  local fake_bin="${BATS_TEST_TMPDIR}/legacy-bin"
  mkdir -p "${fake_bin}"
  cat >"${fake_bin}/unsquashfs" <<'EOF'
#!/usr/bin/env bash
case "$1" in
  -s)
    printf 'Found a valid SQUASHFS 4:0 superblock\n'
    printf 'Compression zstd\n'
    printf 'Compression level 18\n'
    ;;
  -l)
    printf '     0 2026-01-01 00:00 squashfs-root/etc/packages/mount/040-de-lwde2-2601-sf04/pfs.files\n'
    printf '     0 2026-01-01 00:00 squashfs-root/etc/packages/mount/labwc-0.9.5-1-x86_64/pfs.files\n'
    ;;
esac
EOF
  chmod +x "${fake_bin}/unsquashfs"

  PATH="${fake_bin}:$PATH" run "${MODMAN}" -Qp "${fixture}"
  [ "${status}" -eq 0 ]
  [[ "${output}" == *$'Модули:\n  040-de-lwde2-2601-sf04\n  labwc-0.9.5-1-x86_64'* ]]
  [[ "${output}" != *"etc/packages"* ]]
}

@test "modman -Qp human mode reads live pfsinfo-style module manifests" {
  local fixture="${BATS_TEST_DIRNAME}/../fixtures/sample.pfs"
  local fake_bin="${BATS_TEST_TMPDIR}/live-bin"
  local cache_dep="${BATS_TEST_TMPDIR}/live-dependencies.txt"
  mkdir -p "${fake_bin}"
  printf 'labwc : wayland wlroots\n' >"${cache_dep}"
  cat >"${fake_bin}/unsquashfs" <<'EOF'
#!/usr/bin/env bash
case "$1" in
  -s)
    printf 'Found a valid SQUASHFS 4:0 superblock\n'
    printf 'Compression zstd\n'
    printf 'Compression level 18\n'
    ;;
  -l)
    printf 'var/lib/pfs/mount/labwc-0.9.5-1-x86_64/labwc-0.9.5-1-x86_64/pfs.files\n'
    printf 'var/lib/pfs/mount/040-de/040-de-lwde2-2601-sf04/pfs.files\n'
    ;;
  -cat)
    exit 99
    ;;
esac
EOF
  chmod +x "${fake_bin}/unsquashfs"

  PATH="${fake_bin}:$PATH" CACHE_DEP="${cache_dep}" run "${MODMAN}" -Qp "${fixture}"
  [ "${status}" -eq 0 ]
  [[ "${output}" == *$'Модули:\n  040-de-lwde2-2601-sf04\n  labwc-0.9.5-1-x86_64'* ]]
  [[ "${output}" == *$'Зависимости:\n  wayland\n  wlroots'* ]]
  [[ "${output}" != *"labwc-0.9.5-1-x86_64/labwc-0.9.5-1-x86_64"* ]]
}

@test "modman -Qp human mode returns non-tsv output" {
  local fixture="${BATS_TEST_DIRNAME}/../fixtures/sample.pfs"

  run "${MODMAN}" -Qp "${fixture}"
  [ "${status}" -eq 0 ]
  [[ "${output}" != $'file\t'* ]]
  [[ "${output}" != $'error\t'* ]]
}
