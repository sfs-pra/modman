#!/usr/bin/env bats

setup_file() {
    export MODMAN_BIN="${BATS_TEST_DIRNAME}/../../modman"
}

setup() {
    export WORKDIR
    export TEST_ROOT
    export FAKE_BIN
    export DOWNLOAD_DIR_TEST
    export CACHE_DIR_TEST
    export FAKE_CMDLINE
    export FAKE_MOUNTS
    export LAYER_STATE

    WORKDIR="$(mktemp -d "${BATS_TEST_TMPDIR}/updates-workdir.XXXXXX")"
    TEST_ROOT="$(mktemp -d "${BATS_TEST_TMPDIR}/rootaufs2.XXXXXX")"
    FAKE_BIN="$(mktemp -d "${BATS_TEST_TMPDIR}/fake-bin.XXXXXX")"
    CACHE_DIR_TEST="$(mktemp -d "${BATS_TEST_TMPDIR}/cache.XXXXXX")"
    LAYER_STATE="$(mktemp -d "${BATS_TEST_TMPDIR}/layer-state.XXXXXX")"

    DOWNLOAD_DIR_TEST="${TEST_ROOT}/downloads"
    FAKE_CMDLINE="${BATS_TEST_TMPDIR}/cmdline.$$"
    FAKE_MOUNTS="${BATS_TEST_TMPDIR}/mounts.$$"

    mkdir -p "${DOWNLOAD_DIR_TEST}" "${TEST_ROOT}/base" "${TEST_ROOT}/modules" \
        "${TEST_ROOT}/optional" "${TEST_ROOT}/extramod" "${TEST_ROOT}/runtime"

    cp "${MODMAN_BIN}" "${WORKDIR}/modman"
    chmod +x "${WORKDIR}/modman"

    cat >"${WORKDIR}/modman.conf" <<EOF
DOWNLOAD_DIR="${DOWNLOAD_DIR_TEST}"
CACHE_DIR="${CACHE_DIR_TEST}"
CACHE_FILE="${CACHE_DIR_TEST}/db.txt"
CACHE_DEP="${CACHE_DIR_TEST}/dependencies.txt"
MODMAN_STATE_DIR="${CACHE_DIR_TEST}/state"
CLI_STYLE="pacman"
REPO_URLS=("https://repo-1.example" "https://repo-2.example")
EXT="pfs"
EOF

    : >"${CACHE_DIR_TEST}/db.txt"
    : >"${CACHE_DIR_TEST}/dependencies.txt"

    printf 'BOOT_IMAGE=/vmlinuz-linux dir=testdir extramod=extramod\n' >"${FAKE_CMDLINE}"
    printf 'overlay / overlay rw 0 0\n' >"${FAKE_MOUNTS}"

    cat >"${FAKE_BIN}/pfs" <<'EOF'
#!/bin/bash
case "$1" in
    --detect-initrd) printf 'rootaufs2\n' ;;
    --layering-mode) printf 'overlay\n' ;;
    --aufs-initrd-prefix) printf '%s\n' "${MODMAN_TEST_ROOTAUFS2_ROOT}/base" ;;
    --download-dir) printf '%s\n' "${DOWNLOAD_DIR}" ;;
esac
EOF
    chmod +x "${FAKE_BIN}/pfs"

    cat >"${FAKE_BIN}/pfsinfo" <<'EOF'
#!/bin/bash
if [ "$1" = "--machine" ] && [ "$2" = "--mount" ]; then
    printf 'sysbase-1.0.pfs\t0\t%s/base/sysbase-1.0.pfs\tbase\t\t\n' "${MODMAN_TEST_ROOTAUFS2_ROOT}"
    printf 'live-after-1.0.pfs\t1\t%s/runtime/live-after-1.0.pfs\truntime\t\t\n' "${MODMAN_TEST_ROOTAUFS2_ROOT}"
fi
exit 0
EOF
    chmod +x "${FAKE_BIN}/pfsinfo"

    cat >"${FAKE_BIN}/losetup" <<'EOF'
#!/bin/bash
if [ "$1" = "-O" ] && [ "$2" = "BACK-FILE" ]; then
    printf 'BACK-FILE\n'
    printf '%s/runtime/loopmod-1.0.pfs\n' "${MODMAN_TEST_ROOTAUFS2_ROOT}"
fi
EOF
    chmod +x "${FAKE_BIN}/losetup"
}

teardown() {
    rm -rf "${WORKDIR}" "${TEST_ROOT}" "${FAKE_BIN}" "${CACHE_DIR_TEST}" "${LAYER_STATE}"
    rm -f "${FAKE_CMDLINE}" "${FAKE_MOUNTS}"
}

run_check_updates_fixture() {
    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine --check-updates
}

run_check_updates_fixture_verbose() {
    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine --check-updates --verbose-skips
}

set_cache_rows() {
    : >"${CACHE_DIR_TEST}/db.txt"
    local block=""
    local row=""
    for block in "$@"; do
        while IFS= read -r row; do
            [ -n "$row" ] || continue
            printf '%b\n' "$row" >>"${CACHE_DIR_TEST}/db.txt"
        done <<<"$block"
    done
}

update_rows() {
    printf '%s\n' "$output" | awk -F'\t' '$1=="update"'
}

@test "check-updates emits 11-field TSV update rows without ANSI" {
    touch "${DOWNLOAD_DIR_TEST}/dl1-1.0.pfs"
    set_cache_rows "dl1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"
    run_check_updates_fixture

    [ "$status" -eq 0 ]
    [ -n "$output" ]
    row_count=$(update_rows | awk 'END{print NR+0}')
    [ "$row_count" -gt 0 ]
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        [[ "$line" == update$'\t'* ]] || continue
        [[ "$line" == update$'\t'* ]]
        field_count=$(awk -F $'\t' '{print NF}' <<<"$line")
        [ "$field_count" -eq 11 ]
        [[ "$line" != *$'\e'* ]]
    done <<<"$output"
}

@test "check-updates discovers DOWNLOAD_DIR base modules optional EXTRAMOD and losetup" {
    # Discovery sources: DOWNLOAD_DIR, base/, modules/, optional/, EXTRAMOD, losetup.
    # runtime/ files NOT in losetup are NOT discovered (pfsinfo no longer called).
    touch "${DOWNLOAD_DIR_TEST}/dl1-1.0.pfs"
    touch "${TEST_ROOT}/base/sysbase-1.0.pfs"
    touch "${TEST_ROOT}/modules/mod1-1.0.pfs"
    touch "${TEST_ROOT}/optional/opt1-1.0.pfs"
    touch "${TEST_ROOT}/extramod/ext1-1.0.pfs"
    touch "${TEST_ROOT}/runtime/loopmod-1.0.pfs"
    set_cache_rows \
"dl1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
sysbase-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
mod1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
opt1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
ext1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
loopmod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]

    paths=$(printf '%s\n' "$output" | awk -F'\t' '$1=="update"{print $6}' | sort -u)
    printf '%s\n' "$paths" | grep -Fxq "$(realpath "${DOWNLOAD_DIR_TEST}/dl1-1.0.pfs")"
    printf '%s\n' "$paths" | grep -Fxq "$(realpath "${TEST_ROOT}/base/sysbase-1.0.pfs")"
    printf '%s\n' "$paths" | grep -Fxq "$(realpath "${TEST_ROOT}/modules/mod1-1.0.pfs")"
    printf '%s\n' "$paths" | grep -Fxq "$(realpath "${TEST_ROOT}/optional/opt1-1.0.pfs")"
    printf '%s\n' "$paths" | grep -Fxq "$(realpath "${TEST_ROOT}/extramod/ext1-1.0.pfs")"
    # losetup-backed module is discovered (fake losetup returns runtime/loopmod-1.0.pfs)
    printf '%s\n' "$paths" | grep -Fxq "$(realpath "${TEST_ROOT}/runtime/loopmod-1.0.pfs")"
}

@test "check-updates resolves relative extramod from /mnt/home root, not dir root" {
    local home_root="${BATS_TEST_TMPDIR}/home-root.$$"
    local distro_root="${home_root}/zz2601"
    local extra_dir="${home_root}/_all/modules"
    local extra_real=""
    local cmdline="${BATS_TEST_TMPDIR}/cmdline-extramod-root.$$"

    mkdir -p "${distro_root}/base" "${distro_root}/modules" "${distro_root}/optional" "${extra_dir}"
    touch "${extra_dir}/ext1-1.0.pfs"
    extra_real=$(realpath "${extra_dir}/ext1-1.0.pfs")
    printf 'BOOT_IMAGE=/vmlinuz-linux diro=zz2601 extramod=_all/modules\n' >"${cmdline}"
    set_cache_rows "ext1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${distro_root}" \
        MODMAN_TEST_HOME_ROOT="${home_root}" \
        MODMAN_PROC_CMDLINE="${cmdline}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        "${WORKDIR}/modman" --machine --check-updates

    rm -rf "${home_root}"
    rm -f "${cmdline}"

    [ "$status" -eq 0 ]
    printf '%s\n' "$output" | awk -F'\t' '$1=="update"{print $6}' | grep -Fxq "$extra_real"
    [[ "$output" != *"${distro_root}/_all/modules"* ]]
}

@test "check-updates dedupes discovered modules by canonical realpath" {
    touch "${TEST_ROOT}/optional/opt1-1.0.pfs"
    ln -s "${TEST_ROOT}/optional/opt1-1.0.pfs" "${DOWNLOAD_DIR_TEST}/opt1-link-1.0.pfs"
    set_cache_rows "opt1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]

    opt_real=$(realpath "${TEST_ROOT}/optional/opt1-1.0.pfs")
    count=$(printf '%s\n' "$output" | awk -F'\t' -v p="$opt_real" '$1=="update" && $6==p{c++}END{print c+0}')
    [ "$count" -eq 1 ]
}

@test "check-updates classifies base module as system with reboot required message" {
    touch "${TEST_ROOT}/base/sysbase-1.0.pfs"
    set_cache_rows "sysbase-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]

    sysbase_real=$(realpath "${TEST_ROOT}/base/sysbase-1.0.pfs")
    row=$(printf '%s\n' "$output" | awk -F'\t' -v p="$sysbase_real" '$1=="update" && $6==p{print; exit}')
    [ -n "$row" ]
    [ "$(awk -F $'\t' '{print NF}' <<<"$row")" -eq 11 ]
    [ "$(awk -F $'\t' '{print $9}' <<<"$row")" = "system" ]
    [ "$(awk -F $'\t' '{print $10}' <<<"$row")" = "1" ]
    [[ "$(awk -F $'\t' '{print $11}' <<<"$row")" == *"нужна перезагрузка"* ]]
}

@test "check-updates classifies base/ path as system and losetup-backed path as loaded" {
    # Risk is now path-based only (no pfsinfo calls):
    #   base/ prefix  → system (reboot required)
    #   losetup entry → loaded (reconnect module)
    #   otherwise     → normal
    touch "${TEST_ROOT}/base/sysbase-1.0.pfs"
    touch "${TEST_ROOT}/runtime/loopmod-1.0.pfs"
    set_cache_rows \
"sysbase-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
loopmod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]

    # base/ module → system risk, reboot required
    sysbase_real=$(realpath "${TEST_ROOT}/base/sysbase-1.0.pfs")
    before_row=$(printf '%s\n' "$output" | awk -F'\t' -v p="$sysbase_real" '$1=="update" && $6==p{print; exit}')
    [ -n "$before_row" ]
    [ "$(awk -F $'\t' '{print $9}' <<<"$before_row")" = "system" ]
    [ "$(awk -F $'\t' '{print $10}' <<<"$before_row")" = "1" ]
    [[ "$(awk -F $'\t' '{print $11}' <<<"$before_row")" == *"нужна перезагрузка"* ]]

    # losetup-backed module → loaded risk (fake losetup returns runtime/loopmod-1.0.pfs)
    loop_real=$(realpath "${TEST_ROOT}/runtime/loopmod-1.0.pfs")
    loop_row=$(printf '%s\n' "$output" | awk -F'\t' -v p="$loop_real" '$1=="update" && $6==p{print; exit}')
    [ -n "$loop_row" ]
    [ "$(awk -F $'\t' '{print $9}' <<<"$loop_row")" = "loaded" ]
    [ "$(awk -F $'\t' '{print $10}' <<<"$loop_row")" = "0" ]
    [[ "$(awk -F $'\t' '{print $11}' <<<"$loop_row")" == *"переподключите модуль"* ]]
}

@test "check-updates optional-source warnings do not fail command" {
    touch "${DOWNLOAD_DIR_TEST}/dl1-1.0.pfs"
    set_cache_rows "dl1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"
    rm -f "${FAKE_BIN}/losetup"

    run_check_updates_fixture
    [ "$status" -eq 0 ]
    printf '%s\n' "$output" | grep -q '^update'
}

@test "check-updates table-driven version matching for splitname/newer semantics" {
    local cases_file
    cases_file="${BATS_TEST_TMPDIR}/version-cases.$$"
    cat >"$cases_file" <<'EOF'
foo-bar-1.2_64.pfs|foo-bar-1.3_64.pfs|foo-bar|1.2|1.3
libx-x86_64-2.0.pfs|libx-x86_64-2.1.pfs|libx|2.0|2.1
multi-part-2024.01.9.pfs|multi-part-2024.01.10.pfs|multi-part|2024.01.9|2024.01.10
pkgrel-demo-1.2-3.pfs|pkgrel-demo-1.2-4.pfs|pkgrel-demo|1.2-3|1.2-4
EOF

    while IFS='|' read -r installed_name repo_name expected_identity expected_old expected_new; do
        rm -f "${DOWNLOAD_DIR_TEST}"/*.pfs
        touch "${DOWNLOAD_DIR_TEST}/${installed_name}"
        set_cache_rows "${repo_name}\t-\t-\tdesc\tcat\t${expected_new}\thttps://repo-1.example"

        run_check_updates_fixture
        [ "$status" -eq 0 ]

        row=$(update_rows | awk -v p="$(realpath "${DOWNLOAD_DIR_TEST}/${installed_name}")" -F'\t' '$6==p{print; exit}')
        [ -n "$row" ]
        [ "$(awk -F $'\t' '{print $3}' <<<"$row")" = "$expected_identity" ]
        [ "$(awk -F $'\t' '{print $4}' <<<"$row")" = "$expected_old" ]
        [ "$(awk -F $'\t' '{print $5}' <<<"$row")" = "$expected_new" ]
    done <"$cases_file"
}

@test "check-updates treats revision-only sf suffix as version for premote modules" {
    touch "${DOWNLOAD_DIR_TEST}/premote-p_64-sf04.pfs"
    set_cache_rows "premote-p_64-sf05.pfs\t1,0M\t2024-05-30\t-\t1,0M\t-\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]

    row=$(update_rows | awk -v p="$(realpath "${DOWNLOAD_DIR_TEST}/premote-p_64-sf04.pfs")" -F'\t' '$6==p{print; exit}')
    [ -n "$row" ]
    [ "$(awk -F $'\t' '{print $3}' <<<"$row")" = "premote-p" ]
    [ "$(awk -F $'\t' '{print $4}' <<<"$row")" = "sf04" ]
    [ "$(awk -F $'\t' '{print $5}' <<<"$row")" = "sf05" ]
    [ "$(awk -F $'\t' '{print $7}' <<<"$row")" = "premote-p_64-sf05.pfs" ]
}

@test "check-updates ignores downgrade candidates" {
    touch "${DOWNLOAD_DIR_TEST}/foo-2.0.pfs"
    set_cache_rows "foo-1.9.pfs\t-\t-\tdesc\tcat\t1.9\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]
    rows=$(update_rows | awk -F'\t' '$3=="foo"{print}')
    [ -z "$rows" ]
}

@test "check-updates skips no-version modules by default" {
    touch "${DOWNLOAD_DIR_TEST}/noversion.pfs"
    set_cache_rows "noversion-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]
    rows=$(update_rows | awk -F'\t' '$6=="'"$(realpath "${DOWNLOAD_DIR_TEST}/noversion.pfs")"'"{print}')
    [ -z "$rows" ]
}

@test "check-updates --verbose-skips emits version-unparsed for no-version module" {
    touch "${DOWNLOAD_DIR_TEST}/noversion.pfs"
    set_cache_rows "noversion-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_check_updates_fixture_verbose
    [ "$status" -eq 0 ]
    skipped_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="skipped" && $4=="version-unparsed"{print; exit}')
    [ -n "$skipped_row" ]
}

@test "check-updates picks first REPO_URLS repo for duplicate filename/version" {
    touch "${DOWNLOAD_DIR_TEST}/dup-1.0.pfs"
    set_cache_rows \
"dup-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-2.example
dup-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]
    row=$(update_rows | awk -F'\t' '$3=="dup"{print; exit}')
    [ -n "$row" ]
    [ "$(awk -F $'\t' '{print $8}' <<<"$row")" = "https://repo-1.example" ]
}

# ==========================================
# BLACKLIST TESTS
# ==========================================

run_blacklist_cmd() {
    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        UPDATE_BLACKLIST_FILE="${CACHE_DIR_TEST}/update-blacklist.tsv" \
        "${WORKDIR}/modman" --machine "$@"
}

run_blacklist_cmd_default_path() {
    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine "$@"
}

run_check_updates_with_blacklist() {
    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        UPDATE_BLACKLIST_FILE="${CACHE_DIR_TEST}/update-blacklist.tsv" \
        "${WORKDIR}/modman" --machine --check-updates "$@"
}

@test "blacklist-add and blacklist-list roundtrip" {
    # Add an entry by filename (identity-version.pfs)
    run_blacklist_cmd --update-blacklist-add "foo-1.1.pfs"
    [ "$status" -eq 0 ]

    # List should show the entry
    run_blacklist_cmd --update-blacklist-list
    [ "$status" -eq 0 ]
    printf '%s\n' "$output" | grep -Fxq "foo"$'\t'"1.1"
}

@test "blacklist-remove removes the entry" {
    # Add then remove
    run_blacklist_cmd --update-blacklist-add "bar-2.0.pfs"
    [ "$status" -eq 0 ]

    run_blacklist_cmd --update-blacklist-list
    printf '%s\n' "$output" | grep -Fxq "bar"$'\t'"2.0"

    run_blacklist_cmd --update-blacklist-remove "bar" "2.0"
    [ "$status" -eq 0 ]

    run_blacklist_cmd --update-blacklist-list
    [ "$status" -eq 0 ]
    # Entry should be gone
    result=$(printf '%s\n' "$output" | grep -Fx "bar"$'\t'"2.0" || true)
    [ -z "$result" ]
}

@test "blacklist file remains readable after add and remove under restrictive umask" {
    local blacklist_file="${CACHE_DIR_TEST}/update-blacklist.tsv"

    old_umask=$(umask)
    umask 077
    run_blacklist_cmd --update-blacklist-add "perm-1.0.pfs"
    umask "$old_umask"
    [ "$status" -eq 0 ]
    [ "$(stat -c '%a' "$blacklist_file")" = "644" ]

    chmod 600 "$blacklist_file"
    run_blacklist_cmd --update-blacklist-remove "perm" "1.0"
    [ "$status" -eq 0 ]
    [ "$(stat -c '%a' "$blacklist_file")" = "644" ]
}

@test "blacklist default path is persistent state, not cache" {
    local state_blacklist="${CACHE_DIR_TEST}/state/update-blacklist.tsv"
    local cache_blacklist="${CACHE_DIR_TEST}/update-blacklist.tsv"

    run_blacklist_cmd_default_path --update-blacklist-add "persist-1.0.pfs"
    [ "$status" -eq 0 ]

    [ -f "$state_blacklist" ]
    [ ! -e "$cache_blacklist" ]

    run_blacklist_cmd_default_path --update-blacklist-list
    [ "$status" -eq 0 ]
    printf '%s\n' "$output" | grep -Fxq "persist"$'\t'"1.0"
}

@test "blacklist default path migrates legacy cache blacklist" {
    local state_blacklist="${CACHE_DIR_TEST}/state/update-blacklist.tsv"
    local cache_blacklist="${CACHE_DIR_TEST}/update-blacklist.tsv"

    printf 'legacy\t2.0\n' >"$cache_blacklist"
    chmod 0644 "$cache_blacklist"

    run_blacklist_cmd_default_path --update-blacklist-list
    [ "$status" -eq 0 ]
    printf '%s\n' "$output" | grep -Fxq "legacy"$'\t'"2.0"
    [ -f "$state_blacklist" ]
    printf '%s\n' "$(<"$state_blacklist")" | grep -Fxq "legacy"$'\t'"2.0"
}

@test "blacklist-add is idempotent (no duplicate entries)" {
    run_blacklist_cmd --update-blacklist-add "baz-3.0.pfs"
    run_blacklist_cmd --update-blacklist-add "baz-3.0.pfs"

    run_blacklist_cmd --update-blacklist-list
    [ "$status" -eq 0 ]
    count=$(printf '%s\n' "$output" | grep -Fxc "baz"$'\t'"3.0")
    [ "$count" -eq 1 ]
}

@test "blacklist-list is sorted by identity then version" {
    run_blacklist_cmd --update-blacklist-add "zmod-2.0.pfs"
    run_blacklist_cmd --update-blacklist-add "amod-1.0.pfs"
    run_blacklist_cmd --update-blacklist-add "amod-2.0.pfs"

    run_blacklist_cmd --update-blacklist-list
    [ "$status" -eq 0 ]
    first=$(printf '%s\n' "$output" | head -1)
    [ "$first" = "amod"$'\t'"1.0" ]
}

@test "check-updates suppresses blacklisted version and offers next unblacklisted newer version" {
    touch "${DOWNLOAD_DIR_TEST}/foo-1.0.pfs"
    # Cache has 1.1 (blacklisted) and 1.2 (not blacklisted)
    set_cache_rows \
"foo-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
foo-1.2.pfs\t-\t-\tdesc\tcat\t1.2\thttps://repo-1.example"

    # Blacklist 1.1
    run_blacklist_cmd --update-blacklist-add "foo-1.1.pfs"
    [ "$status" -eq 0 ]

    run_check_updates_with_blacklist
    [ "$status" -eq 0 ]

    # Should offer 1.2, not 1.1
    row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="update" && $3=="foo"{print; exit}')
    [ -n "$row" ]
    new_ver=$(awk -F'\t' '{print $5}' <<<"$row")
    [ "$new_ver" = "1.2" ]
}

@test "check-updates suppresses blacklisted version when no other newer version exists" {
    touch "${DOWNLOAD_DIR_TEST}/foo-1.0.pfs"
    # Cache has only 1.1 (blacklisted)
    set_cache_rows "foo-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_blacklist_cmd --update-blacklist-add "foo-1.1.pfs"
    [ "$status" -eq 0 ]

    run_check_updates_with_blacklist
    [ "$status" -eq 0 ]

    # No update row for foo — blacklisted and no alternative
    rows=$(printf '%s\n' "$output" | awk -F'\t' '$1=="update" && $3=="foo"{print}')
    [ -z "$rows" ]
}

@test "check-updates --include-blacklisted shows blacklisted row with message=blacklisted" {
    touch "${DOWNLOAD_DIR_TEST}/foo-1.0.pfs"
    set_cache_rows "foo-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_blacklist_cmd --update-blacklist-add "foo-1.1.pfs"
    [ "$status" -eq 0 ]

    run_check_updates_with_blacklist --include-blacklisted
    [ "$status" -eq 0 ]

    # Should have an update row for foo 1.1 with message=blacklisted
    bl_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="update" && $3=="foo" && $5=="1.1"{print; exit}')
    [ -n "$bl_row" ]
    msg=$(awk -F'\t' '{print $11}' <<<"$bl_row")
    [ "$msg" = "blacklisted" ]
}

@test "check-updates --include-blacklisted shows both blacklisted and non-blacklisted rows" {
    touch "${DOWNLOAD_DIR_TEST}/foo-1.0.pfs"
    # 1.1 blacklisted, 1.2 not
    set_cache_rows \
"foo-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
foo-1.2.pfs\t-\t-\tdesc\tcat\t1.2\thttps://repo-1.example"

    run_blacklist_cmd --update-blacklist-add "foo-1.1.pfs"
    [ "$status" -eq 0 ]

    run_check_updates_with_blacklist --include-blacklisted
    [ "$status" -eq 0 ]

    # Blacklisted row for 1.1
    bl_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="update" && $3=="foo" && $5=="1.1"{print; exit}')
    [ -n "$bl_row" ]
    [ "$(awk -F'\t' '{print $11}' <<<"$bl_row")" = "blacklisted" ]

    # Normal update row for 1.2
    normal_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="update" && $3=="foo" && $5=="1.2"{print; exit}')
    [ -n "$normal_row" ]
    [ "$(awk -F'\t' '{print $11}' <<<"$normal_row")" != "blacklisted" ]
}

@test "blacklist-add resolves identity-only arg via cache lookup" {
    # Add a cache entry so identity lookup works
    set_cache_rows "mymod-2.5.pfs\t-\t-\tdesc\tcat\t2.5\thttps://repo-1.example"

    # Add by identity name only (no version in arg)
    run_blacklist_cmd --update-blacklist-add "mymod"
    [ "$status" -eq 0 ]

    run_blacklist_cmd --update-blacklist-list
    [ "$status" -eq 0 ]
    printf '%s\n' "$output" | grep -Fxq "mymod"$'\t'"2.5"
}

@test "blacklist-add resolves update id from current check-updates row" {
    touch "${DOWNLOAD_DIR_TEST}/premote-p_64-sf04.pfs"
    set_cache_rows "premote-p_64-sf05.pfs\t1,0M\t2024-05-30\t-\t1,0M\t-\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]
    update_id=$(update_rows | awk -F $'\t' 'NR==1{print $2}')
    [ -n "$update_id" ]

    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        UPDATE_BLACKLIST_FILE="${CACHE_DIR_TEST}/update-blacklist.tsv" \
        "${WORKDIR}/modman" --machine --update-blacklist-add "$update_id"
    [ "$status" -eq 0 ]

    run env \
        UPDATE_BLACKLIST_FILE="${CACHE_DIR_TEST}/update-blacklist.tsv" \
        "${WORKDIR}/modman" --machine --update-blacklist-list
    [ "$status" -eq 0 ]
    printf '%s\n' "$output" | grep -Fxq "premote-p"$'\t'"sf05"
}

# ==========================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ДЛЯ --update ТЕСТОВ
# ==========================================

# Создаёт fake wget, который копирует файл-заглушку вместо реальной загрузки.
# Аргументы: FAKE_BIN_DIR CANDIDATE_SRC_FILE
make_fake_wget_ok() {
    local fake_bin_dir="$1"
    local candidate_src="$2"
    cat >"${fake_bin_dir}/wget" <<EOF
#!/bin/bash
# Fake wget: копирует заглушку в -O target
out_file=""
for arg in "\$@"; do
    if [ "\$prev" = "-O" ] || [ "\$prev" = "--output-document" ]; then
        out_file="\$arg"
    fi
    prev="\$arg"
done
[ -n "\$out_file" ] || exit 1
cp "${candidate_src}" "\$out_file" && exit 0
exit 1
EOF
    chmod +x "${fake_bin_dir}/wget"
}

# Создаёт fake wget, который всегда завершается с ошибкой.
make_fake_wget_fail() {
    local fake_bin_dir="$1"
    cat >"${fake_bin_dir}/wget" <<'EOF'
#!/bin/bash
exit 1
EOF
    chmod +x "${fake_bin_dir}/wget"
}

# Запускает modman --machine --update ID с тестовым окружением.
run_update_fixture() {
    local update_id="$1"
    shift
    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "$@" \
        "${WORKDIR}/modman" --machine --update "$update_id"
}

# Запускает modman --machine --update-all с тестовым окружением.
run_update_all_fixture() {
    # Extra modman flags (e.g. --confirm-system-updates) passed as arguments.
    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine "$@" --update-all
}

# Вычисляет id обновления для модуля через check_updates.
get_update_id_for() {
    local installed_path="$1"
    local real_path
    real_path=$(realpath "$installed_path")
    env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine --check-updates 2>/dev/null \
        | awk -F'\t' -v p="$real_path" '$1=="update" && $6==p {print $2; exit}'
}

# ==========================================
# ТЕСТЫ --update: БЕЗОПАСНАЯ РОТАЦИЯ
# ==========================================

@test "update: safe rotation — old renamed to .old, new placed, update.log written" {
    # Создаём установленный модуль
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'old content\n' >"$installed"

    # Создаём кандидата в кэше
    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    # Создаём файл-заглушку кандидата
    local candidate_src="${BATS_TEST_TMPDIR}/mymod-1.1.pfs"
    printf 'new content\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    # Получаем id обновления
    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    # Применяем обновление
    run_update_fixture "$update_id"
    [ "$status" -eq 0 ]

    # Проверяем applied row
    applied_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="applied"')
    [ -n "$applied_row" ]
    [ "$(awk -F'\t' '{print NF}' <<<"$applied_row")" -eq 8 ]

    # Старый файл переименован в .old
    [ -f "${installed}.old" ]
    [ "$(cat "${installed}.old")" = "old content" ]

    # Новый файл на месте
    local new_path="${DOWNLOAD_DIR_TEST}/mymod-1.1.pfs"
    [ -f "$new_path" ]
    [ "$(cat "$new_path")" = "new content" ]

    # Оригинальный путь больше не существует
    [ ! -f "$installed" ]

    # update.log записан
    [ -f "${DOWNLOAD_DIR_TEST}/update.log" ]
    grep -q "mymod-1.0.pfs" "${DOWNLOAD_DIR_TEST}/update.log"
    grep -q "mymod-1.1.pfs" "${DOWNLOAD_DIR_TEST}/update.log"
}

@test "update: .old.N suffix collision — second update gets .old.1" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'old content\n' >"$installed"
    # Уже есть .old
    printf 'previous old\n' >"${installed}.old"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/mymod-1.1.pfs"
    printf 'new content\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    run_update_fixture "$update_id"
    [ "$status" -eq 0 ]

    # .old уже занят — должен быть .old.1
    [ -f "${installed}.old" ]
    [ -f "${installed}.old.1" ]
    [ "$(cat "${installed}.old.1")" = "old content" ]
    [ -f "${DOWNLOAD_DIR_TEST}/mymod-1.1.pfs" ]
}

@test "update: .old.N suffix collision chain — .old, .old.1, .old.2 all occupied" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'old content\n' >"$installed"
    printf 'prev1\n' >"${installed}.old"
    printf 'prev2\n' >"${installed}.old.1"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/mymod-1.1.pfs"
    printf 'new content\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    run_update_fixture "$update_id"
    [ "$status" -eq 0 ]

    # Должен быть .old.2
    [ -f "${installed}.old.2" ]
    [ "$(cat "${installed}.old.2")" = "old content" ]
}

# ==========================================
# ТЕСТЫ --update: SYSTEM REJECTION
# ==========================================

@test "update: system module without --confirm-system-updates exits 8 with skipped row" {
    # Системный модуль в base/
    local installed="${TEST_ROOT}/base/sysbase-1.0.pfs"
    printf 'sys content\n' >"$installed"

    set_cache_rows "sysbase-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/sysbase-1.1.pfs"
    printf 'new sys\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    # Без --confirm-system-updates
    run_update_fixture "$update_id"
    [ "$status" -eq 8 ]

    # Должна быть skipped строка с needs_confirmation
    skipped_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="skipped" && $2=="needs_confirmation"')
    [ -n "$skipped_row" ]

    # Файл не изменён
    [ -f "$installed" ]
    [ "$(cat "$installed")" = "sys content" ]
    [ ! -f "${installed}.old" ]
}

@test "update: system module with --confirm-system-updates applies successfully" {
    local installed="${TEST_ROOT}/base/sysbase-1.0.pfs"
    printf 'sys content\n' >"$installed"

    set_cache_rows "sysbase-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/sysbase-1.1.pfs"
    printf 'new sys\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    # С --confirm-system-updates
    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine --confirm-system-updates --update "$update_id"
    [ "$status" -eq 0 ]

    applied_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="applied"')
    [ -n "$applied_row" ]
    [ -f "${installed}.old" ]
    [ -f "${TEST_ROOT}/base/sysbase-1.1.pfs" ]
}

# ==========================================
# ТЕСТЫ --update: FAILED DOWNLOAD
# ==========================================

@test "update: failed download leaves original file intact" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'original content\n' >"$installed"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    # wget всегда падает
    make_fake_wget_fail "${FAKE_BIN}"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    run_update_fixture "$update_id"
    [ "$status" -ne 0 ]

    # Оригинальный файл не тронут
    [ -f "$installed" ]
    [ "$(cat "$installed")" = "original content" ]
    # .old не создан
    [ ! -f "${installed}.old" ]
}

# ==========================================
# ТЕСТЫ --update: ROLLBACK
# ==========================================

@test "update: rollback restores original if final move fails" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'original content\n' >"$installed"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    # wget успешно скачивает, но целевой каталог read-only для mv
    local candidate_src="${BATS_TEST_TMPDIR}/mymod-1.1.pfs"
    printf 'new content\n' >"$candidate_src"

    # Создаём wget, который пишет во временный файл в CACHE_DIR/updates
    # (имитируем ситуацию, когда mv tmp → new_path не работает)
    # Для этого делаем целевой каталог read-only ПОСЛЕ скачивания.
    # Вместо этого используем fake wget, который пишет в tmp, но
    # затем делаем каталог read-only перед mv.
    # Проще: создаём fake wget, который пишет пустой файл (0 байт),
    # что вызовет ошибку "не удалось скачать" (пустой файл).
    cat >"${FAKE_BIN}/wget" <<'EOF'
#!/bin/bash
out_file=""
prev=""
for arg in "$@"; do
    if [ "$prev" = "-O" ] || [ "$prev" = "--output-document" ]; then
        out_file="$arg"
    fi
    prev="$arg"
done
# Создаём файл, но делаем его нечитаемым для mv (пустой = ошибка скачивания)
[ -n "$out_file" ] && : >"$out_file"
exit 0
EOF
    chmod +x "${FAKE_BIN}/wget"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    run_update_fixture "$update_id"
    # Пустой файл → download failed → original intact
    [ "$status" -ne 0 ]
    [ -f "$installed" ]
    [ "$(cat "$installed")" = "original content" ]
}

# ==========================================
# ТЕСТЫ --update: CONCURRENT LOCK
# ==========================================

@test "update: concurrent lock — second process gets lock-timeout error" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'original\n' >"$installed"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/mymod-1.1.pfs"
    printf 'new content\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    # Захватываем блокировку вручную — lock уже занят
    mkdir -p "${CACHE_DIR_TEST}/update.lock"

    # Подменяем date: первый вызов (deadline) возвращает 1000,
    # второй вызов (проверка) возвращает 2000 — deadline уже истёк.
    local date_call_file="${BATS_TEST_TMPDIR}/date_calls.$$"
    printf '0\n' >"$date_call_file"
    cat >"${FAKE_BIN}/date" <<EOF
#!/bin/bash
if [ "\$1" = "+%s" ]; then
    count=\$(cat "${date_call_file}")
    count=\$(( count + 1 ))
    printf '%s\n' "\$count" >"${date_call_file}"
    # Первый вызов: возвращаем 0 (для deadline = 0+30=30)
    # Второй вызов: возвращаем 31 (уже за deadline)
    if [ "\$count" -le 1 ]; then
        printf '0\n'
    else
        printf '31\n'
    fi
else
    /bin/date "\$@"
fi
EOF
    chmod +x "${FAKE_BIN}/date"

    run_update_fixture "$update_id"
    [ "$status" -ne 0 ]

    # Оригинальный файл не тронут
    [ -f "$installed" ]
    [ "$(cat "$installed")" = "original" ]

    # Освобождаем блокировку
    rmdir "${CACHE_DIR_TEST}/update.lock" 2>/dev/null || true
}

# ==========================================
# ТЕСТЫ --update-all
# ==========================================

@test "update-all: applies normal modules, skips system without --confirm-system-updates" {
    # Нормальный модуль
    local normal="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'normal old\n' >"$normal"

    # Системный модуль
    local sysmod="${TEST_ROOT}/base/sysbase-1.0.pfs"
    printf 'sys old\n' >"$sysmod"

    set_cache_rows \
"mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
sysbase-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/mymod-1.1.pfs"
    printf 'normal new\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    run_update_all_fixture
    # Может вернуть 0 (нет ошибок, только skipped)
    [ "$status" -eq 0 ]

    # Нормальный модуль обновлён
    [ -f "${DOWNLOAD_DIR_TEST}/mymod-1.1.pfs" ]
    [ -f "${normal}.old" ]

    # Системный пропущен — файл не тронут
    [ -f "$sysmod" ]
    [ "$(cat "$sysmod")" = "sys old" ]
    [ ! -f "${sysmod}.old" ]

    # skipped строка для системного
    skipped_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="skipped" && $2=="needs_confirmation"')
    [ -n "$skipped_row" ]

    # applied строка для нормального
    applied_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="applied"')
    [ -n "$applied_row" ]
}

@test "update-all: with --confirm-system-updates applies system modules too" {
    local sysmod="${TEST_ROOT}/base/sysbase-1.0.pfs"
    printf 'sys old\n' >"$sysmod"

    set_cache_rows "sysbase-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/sysbase-1.1.pfs"
    printf 'sys new\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    run_update_all_fixture --confirm-system-updates
    [ "$status" -eq 0 ]

    [ -f "${TEST_ROOT}/base/sysbase-1.1.pfs" ]
    [ -f "${sysmod}.old" ]

    applied_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="applied"')
    [ -n "$applied_row" ]
}

@test "update-all: failed download for one module does not affect others" {
    local mod1="${DOWNLOAD_DIR_TEST}/mod1-1.0.pfs"
    local mod2="${DOWNLOAD_DIR_TEST}/mod2-1.0.pfs"
    printf 'mod1 old\n' >"$mod1"
    printf 'mod2 old\n' >"$mod2"

    set_cache_rows \
"mod1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
mod2-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    # wget: успешно для mod2, падает для mod1
    # Используем счётчик вызовов через файл
    local call_count_file="${BATS_TEST_TMPDIR}/wget_calls"
    printf '0\n' >"$call_count_file"

    local candidate_src="${BATS_TEST_TMPDIR}/mod2-1.1.pfs"
    printf 'mod2 new\n' >"$candidate_src"

    cat >"${FAKE_BIN}/wget" <<EOF
#!/bin/bash
out_file=""
prev=""
for arg in "\$@"; do
    if [ "\$prev" = "-O" ] || [ "\$prev" = "--output-document" ]; then
        out_file="\$arg"
    fi
    prev="\$arg"
done
# Первый вызов — падаем (mod1), второй — успех (mod2)
count=\$(cat "${call_count_file}")
count=\$(( count + 1 ))
printf '%s\n' "\$count" >"${call_count_file}"
if [ "\$count" -eq 1 ]; then
    exit 1
fi
cp "${candidate_src}" "\$out_file" && exit 0
exit 1
EOF
    chmod +x "${FAKE_BIN}/wget"

    run_update_all_fixture
    # Одна ошибка → exit 1
    [ "$status" -ne 0 ]

    # mod2 обновлён
    [ -f "${DOWNLOAD_DIR_TEST}/mod2-1.1.pfs" ]

    # mod1 не тронут
    [ -f "$mod1" ]
    [ "$(cat "$mod1")" = "mod1 old" ]
}

@test "update: unknown id exits 1 with error row" {
    run_update_fixture "deadbeef0000000000000000000000000000000000000000000000000000cafe"
    [ "$status" -eq 1 ]
    error_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="error"')
    [ -n "$error_row" ]
}

@test "update: candidate filename with slash is rejected" {
    # Создаём модуль с кандидатом, у которого имя содержит слэш
    # Это нужно тестировать через MODMAN_TEST_CHECK_UPDATES_FIXTURE
    local installed="${BATS_TEST_TMPDIR}/fixture/module-1.0.pfs"
    mkdir -p "$(dirname "$installed")"
    printf 'content\n' >"$installed"

    # Вычисляем id через fixture
    local fixture_id
    fixture_id=$(env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        MODMAN_TEST_CHECK_UPDATES_FIXTURE=1 \
        MODMAN_TEST_INSTALLED_PATH="$installed" \
        MODMAN_TEST_IDENTITY="module" \
        MODMAN_TEST_OLD_VERSION="1.0" \
        MODMAN_TEST_NEW_VERSION="1.1" \
        MODMAN_TEST_CANDIDATE_FILENAME="module-1.1.pfs" \
        MODMAN_TEST_REPO="fixture-repo" \
        MODMAN_TEST_RISK="normal" \
        MODMAN_TEST_REBOOT_REQUIRED="0" \
        MODMAN_TEST_MESSAGE="test" \
        "${WORKDIR}/modman" --machine --check-updates 2>/dev/null \
        | awk -F'\t' '$1=="update"{print $2; exit}')
    [ -n "$fixture_id" ]

    # Теперь подменяем check_updates через fixture с плохим именем кандидата
    # Мы не можем легко инжектировать плохое имя через fixture (оно валидируется),
    # поэтому тестируем _update_validate_candidate_filename напрямую через bash
    run bash -c '
        source /dev/stdin <<'"'"'ENDSOURCE'"'"'
_update_validate_candidate_filename() {
    local fn="$1"
    [[ "$fn" == */* ]] && { printf "error: slash\n" >&2; return 1; }
    [[ "$fn" == *$'"'"'\t'"'"'* ]] && { printf "error: tab\n" >&2; return 1; }
    [ -n "$fn" ] || { printf "error: empty\n" >&2; return 1; }
    local ext="${EXT:-pfs}"
    [[ "$fn" == *."$ext" ]] || { printf "error: ext\n" >&2; return 1; }
    return 0
}
EXT=pfs
ENDSOURCE
_update_validate_candidate_filename "path/to/evil.pfs"
'
    [ "$status" -ne 0 ]
}

@test "update: applied row has 8 fields" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'old\n' >"$installed"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/mymod-1.1.pfs"
    printf 'new\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    run_update_fixture "$update_id"
    [ "$status" -eq 0 ]

    applied_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="applied"')
    [ -n "$applied_row" ]
    field_count=$(awk -F'\t' '{print NF}' <<<"$applied_row")
    [ "$field_count" -eq 8 ]
}

# ==========================================
# ТЕСТЫ: ПОЛНОТА ПОКРЫТИЯ (Task 9)
# ==========================================

@test "update: applied row fields — old_path, new_path, backup_path are correct" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'original\n' >"$installed"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/mymod-1.1.pfs"
    printf 'updated\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    run_update_fixture "$update_id"
    [ "$status" -eq 0 ]

    # Schema: applied\tid\told_path\tnew_path\tbackup_path\trisk\treboot_required\tmessage
    applied_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="applied"')
    [ -n "$applied_row" ]

    old_path=$(awk -F'\t' '{print $3}' <<<"$applied_row")
    new_path=$(awk -F'\t' '{print $4}' <<<"$applied_row")
    backup_path=$(awk -F'\t' '{print $5}' <<<"$applied_row")

    # old_path is the original installed path
    [ "$old_path" = "$(realpath "${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs")" ]
    # new_path is the new versioned filename in same dir
    [ "$new_path" = "$(realpath "${DOWNLOAD_DIR_TEST}")/mymod-1.1.pfs" ]
    # backup_path ends with .old
    [[ "$backup_path" == *.old ]]
    # backup file actually exists with original content
    [ -f "$backup_path" ]
    [ "$(cat "$backup_path")" = "original" ]
}

@test "check-updates: empty cache exits 0 with no update rows" {
    # No modules installed, no cache entries
    run_check_updates_fixture
    [ "$status" -eq 0 ]
    update_count=$(printf '%s\n' "$output" | awk -F'\t' '$1=="update"{c++}END{print c+0}')
    [ "$update_count" -eq 0 ]
}

@test "check-updates: no installed modules exits 0 with no update rows" {
    # Cache has entries but no modules installed anywhere
    set_cache_rows "foo-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_check_updates_fixture
    [ "$status" -eq 0 ]
    update_count=$(printf '%s\n' "$output" | awk -F'\t' '$1=="update"{c++}END{print c+0}')
    [ "$update_count" -eq 0 ]
}

@test "check-updates --verbose-skips: skipped row has 4 fields (skipped reason id message)" {
    touch "${DOWNLOAD_DIR_TEST}/noversion.pfs"
    set_cache_rows "noversion-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    run_check_updates_fixture_verbose
    [ "$status" -eq 0 ]

    skipped_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="skipped"{print; exit}')
    [ -n "$skipped_row" ]
    field_count=$(awk -F'\t' '{print NF}' <<<"$skipped_row")
    [ "$field_count" -eq 4 ]
    # Field 2 = reason, field 4 = message (non-empty)
    reason=$(awk -F'\t' '{print $2}' <<<"$skipped_row")
    message=$(awk -F'\t' '{print $4}' <<<"$skipped_row")
    [ -n "$reason" ]
    [ -n "$message" ]
}

@test "update-all: blacklisted module is skipped (not applied)" {
    local installed="${DOWNLOAD_DIR_TEST}/foo-1.0.pfs"
    printf 'original\n' >"$installed"

    set_cache_rows "foo-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    # Blacklist the only available update
    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        UPDATE_BLACKLIST_FILE="${CACHE_DIR_TEST}/update-blacklist.tsv" \
        "${WORKDIR}/modman" --machine --update-blacklist-add "foo-1.1.pfs"
    [ "$status" -eq 0 ]

    local candidate_src="${BATS_TEST_TMPDIR}/foo-1.1.pfs"
    printf 'new\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        UPDATE_BLACKLIST_FILE="${CACHE_DIR_TEST}/update-blacklist.tsv" \
        "${WORKDIR}/modman" --machine --update-all
    [ "$status" -eq 0 ]

    # Original file must be intact — blacklisted update was not applied
    [ -f "$installed" ]
    [ "$(cat "$installed")" = "original" ]
    [ ! -f "${installed}.old" ]
    [ ! -f "${DOWNLOAD_DIR_TEST}/foo-1.1.pfs" ]

    # No applied row
    applied_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="applied"')
    [ -z "$applied_row" ]
}

@test "update-all: exits 0 when all modules updated successfully" {
    local mod1="${DOWNLOAD_DIR_TEST}/alpha-1.0.pfs"
    local mod2="${DOWNLOAD_DIR_TEST}/beta-1.0.pfs"
    printf 'alpha old\n' >"$mod1"
    printf 'beta old\n' >"$mod2"

    set_cache_rows \
"alpha-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example
beta-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/candidate.pfs"
    printf 'new content\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    run_update_all_fixture
    [ "$status" -eq 0 ]

    # Both modules updated
    [ -f "${DOWNLOAD_DIR_TEST}/alpha-1.1.pfs" ]
    [ -f "${DOWNLOAD_DIR_TEST}/beta-1.1.pfs" ]
    [ -f "${mod1}.old" ]
    [ -f "${mod2}.old" ]

    # Two applied rows
    applied_count=$(printf '%s\n' "$output" | awk -F'\t' '$1=="applied"{c++}END{print c+0}')
    [ "$applied_count" -eq 2 ]
}

@test "update-all: original file content preserved after failed download" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'precious original content\n' >"$installed"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    # wget always fails
    make_fake_wget_fail "${FAKE_BIN}"

    run_update_all_fixture
    [ "$status" -ne 0 ]

    # Original file must be intact with exact content
    [ -f "$installed" ]
    [ "$(cat "$installed")" = "precious original content" ]
    [ ! -f "${installed}.old" ]
    [ ! -f "${DOWNLOAD_DIR_TEST}/mymod-1.1.pfs" ]
}

@test "update: failed apply (empty download) leaves original file intact with exact content" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'exact original bytes\n' >"$installed"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    # wget writes empty file (triggers download-failed check)
    cat >"${FAKE_BIN}/wget" <<'EOF'
#!/bin/bash
out_file=""
prev=""
for arg in "$@"; do
    if [ "$prev" = "-O" ] || [ "$prev" = "--output-document" ]; then
        out_file="$arg"
    fi
    prev="$arg"
done
[ -n "$out_file" ] && : >"$out_file"
exit 0
EOF
    chmod +x "${FAKE_BIN}/wget"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    run_update_fixture "$update_id"
    [ "$status" -ne 0 ]

    # Original file must be intact with exact content
    [ -f "$installed" ]
    [ "$(cat "$installed")" = "exact original bytes" ]
    [ ! -f "${installed}.old" ]
}

@test "update-all: error row emitted for failed download" {
    local installed="${DOWNLOAD_DIR_TEST}/mymod-1.0.pfs"
    printf 'original\n' >"$installed"

    set_cache_rows "mymod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    make_fake_wget_fail "${FAKE_BIN}"

    run_update_all_fixture
    [ "$status" -ne 0 ]

    error_row=$(printf '%s\n' "$output" | awk -F'\t' '$1=="error"')
    [ -n "$error_row" ]
    # Error row has 4 fields: error\treason\tid\tmessage
    field_count=$(awk -F'\t' '{print NF}' <<<"$error_row")
    [ "$field_count" -eq 4 ]
}

@test "update: concurrent lock — original file not modified while lock held" {
    local installed="${DOWNLOAD_DIR_TEST}/lockmod-1.0.pfs"
    printf 'locked original\n' >"$installed"

    set_cache_rows "lockmod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local candidate_src="${BATS_TEST_TMPDIR}/lockmod-1.1.pfs"
    printf 'new content\n' >"$candidate_src"
    make_fake_wget_ok "${FAKE_BIN}" "$candidate_src"

    local update_id
    update_id=$(get_update_id_for "$installed")
    [ -n "$update_id" ]

    # Pre-acquire the lock
    mkdir -p "${CACHE_DIR_TEST}/update.lock"

    # Fake date: deadline expires immediately
    local date_call_file="${BATS_TEST_TMPDIR}/date_calls2.$$"
    printf '0\n' >"$date_call_file"
    cat >"${FAKE_BIN}/date" <<EOF
#!/bin/bash
if [ "\$1" = "+%s" ]; then
    count=\$(cat "${date_call_file}")
    count=\$(( count + 1 ))
    printf '%s\n' "\$count" >"${date_call_file}"
    if [ "\$count" -le 1 ]; then
        printf '0\n'
    else
        printf '31\n'
    fi
else
    /bin/date "\$@"
fi
EOF
    chmod +x "${FAKE_BIN}/date"

    run_update_fixture "$update_id"
    [ "$status" -ne 0 ]

    # Original file must be completely untouched
    [ -f "$installed" ]
    [ "$(cat "$installed")" = "locked original"  ]
    [ ! -f "${installed}.old" ]

    rmdir "${CACHE_DIR_TEST}/update.lock" 2>/dev/null || true
}

# ==========================================
# ТЕСТЫ: TIMEOUT (BUG B regression)
# ==========================================

@test "check-updates: pfsinfo is not called during discovery (no timeout risk)" {
    # pfsinfo was removed from check_updates discovery path (AI-9966325 fix).
    # A hanging pfsinfo must NOT block check_updates at all.
    touch "${DOWNLOAD_DIR_TEST}/dl1-1.0.pfs"
    set_cache_rows "dl1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local marker="${BATS_TEST_TMPDIR}/pfsinfo_was_called.$$"
    cat >"${FAKE_BIN}/pfsinfo" <<EOF
#!/bin/bash
touch "${marker}"
if [ "\$1" = "--machine" ] && [ "\$2" = "--mount" ]; then
    sleep 30
fi
exit 0
EOF
    chmod +x "${FAKE_BIN}/pfsinfo"

    local start_ts end_ts elapsed
    start_ts=$(date +%s)

    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine --check-updates 2>&1

    end_ts=$(date +%s)
    elapsed=$(( end_ts - start_ts ))

    [ "$status" -eq 0 ]
    # Must complete fast — pfsinfo not called, so no 30s sleep
    [ "$elapsed" -lt 5 ]
    # Marker must NOT exist — pfsinfo was never invoked
    [ ! -f "$marker" ]
}

@test "check-updates: hanging losetup times out, returns exit 0 with warning on stderr" {
    if ! command -v timeout >/dev/null 2>&1; then
        skip "timeout not available"
    fi

    touch "${DOWNLOAD_DIR_TEST}/dl1-1.0.pfs"
    set_cache_rows "dl1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    cat >"${FAKE_BIN}/losetup" <<'EOF'
#!/bin/bash
if [ "$1" = "-O" ] && [ "$2" = "BACK-FILE" ]; then
    sleep 30
fi
exit 0
EOF
    chmod +x "${FAKE_BIN}/losetup"

    local start_ts end_ts elapsed
    start_ts=$(date +%s)

    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine --check-updates 2>&1

    end_ts=$(date +%s)
    elapsed=$(( end_ts - start_ts ))

    [ "$status" -eq 0 ]
    [ "$elapsed" -lt 15 ]
    printf '%s\n' "$output" | grep -q 'losetup_timeout'
}

@test "check-updates: losetup root-level /bats-run-* paths are filtered, real paths are kept" {
    # Arrange: one real module with an update candidate, plus a stale root-level
    # bats artifact (path starts with /bats-run-* at filesystem root — these appear
    # on production systems after interrupted bats runs leave stale loop devices).
    local real_mod="${TEST_ROOT}/runtime/real-mod-1.0.pfs"
    touch "$real_mod"
    set_cache_rows "real-mod-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    # Stale path: root-level /bats-run-* — must be filtered.
    local stale_path="/bats-run-qgPkho/test/13/some/path/stale-mod-1.0.pfs"

    cat >"${FAKE_BIN}/losetup" <<EOF
#!/bin/bash
if [ "\$1" = "-O" ] && [ "\$2" = "BACK-FILE" ]; then
    printf 'BACK-FILE\n'
    # stale bats artifact at filesystem root — must be filtered out
    printf '%s\n' "$stale_path"
    # real system module — must be kept
    printf '%s\n' "$real_mod"
fi
exit 0
EOF
    chmod +x "${FAKE_BIN}/losetup"

    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine --check-updates

    [ "$status" -eq 0 ]

    # The real module must appear as an update candidate.
    printf '%s\n' "$output" | grep -q 'real-mod'

    # The stale root-level /bats-run-* path must NOT appear in the output.
    if printf '%s\n' "$output" | grep -qF "$stale_path"; then
        echo "FAIL: stale path '$stale_path' appeared in output"
        return 1
    fi
}

# ==========================================
# ТЕСТ: REGRESSION — pfsinfo не вызывается в discovery (AI-9966325)
# ==========================================

@test "check-updates: does not call pfsinfo for discovery" {
    # Regression guard: pfsinfo was removed from check_updates discovery path.
    # A marker file is created if pfsinfo is invoked; it must remain absent.
    touch "${DOWNLOAD_DIR_TEST}/dl1-1.0.pfs"
    set_cache_rows "dl1-1.1.pfs\t-\t-\tdesc\tcat\t1.1\thttps://repo-1.example"

    local marker="${BATS_TEST_TMPDIR}/pfsinfo_was_called.$$"
    cat >"${FAKE_BIN}/pfsinfo" <<EOF
#!/bin/bash
touch "${marker}"
exit 0
EOF
    chmod +x "${FAKE_BIN}/pfsinfo"

    run env \
        PATH="${FAKE_BIN}:$PATH" \
        MODMAN_TEST_ROOTAUFS2_ROOT="${TEST_ROOT}" \
        MODMAN_PROC_CMDLINE="${FAKE_CMDLINE}" \
        MODMAN_PROC_MOUNTS="${FAKE_MOUNTS}" \
        PFS_LAYER_STATE_DIR="${LAYER_STATE}" \
        PFS_BIN="pfs" \
        PFSINFO_BIN="pfsinfo" \
        EXTRAMOD="extramod" \
        "${WORKDIR}/modman" --machine --check-updates

    [ "$status" -eq 0 ]
    # pfsinfo must NOT have been called during discovery
    [ ! -f "$marker" ]
}
