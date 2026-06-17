#!/usr/bin/env bats

setup() {
    export PROJECT_ROOT="${BATS_TEST_DIRNAME}/../.."
    export DLD="$BATS_TEST_TMPDIR/dl-$$"
    rm -rf "$DLD"
    mkdir -p "$DLD"

    export WORKDIR="$BATS_TEST_TMPDIR/workdir-$$"
    rm -rf "$WORKDIR"
    mkdir -p "$WORKDIR"

    cp "$PROJECT_ROOT/modman" "$WORKDIR/modman"
    chmod +x "$WORKDIR/modman"

    cat > "$WORKDIR/modman.conf" <<CONF
DOWNLOAD_DIR="$DLD"
CACHE_DIR="$BATS_TEST_TMPDIR/cache-$$"
CACHE_FILE="$BATS_TEST_TMPDIR/cache-$$/db.txt"
CACHE_DEP="$BATS_TEST_TMPDIR/cache-$$/dependencies.txt"
CLI_STYLE="pacman"
REPO_URLS=()
EXT=pfs
CONF
    mkdir -p "$BATS_TEST_TMPDIR/cache-$$"
    touch "$BATS_TEST_TMPDIR/cache-$$/db.txt"
    touch "$BATS_TEST_TMPDIR/cache-$$/dependencies.txt"

    export MODMAN="$WORKDIR/modman"

    export FAKE_PFSINFO="$BATS_TEST_TMPDIR/fake-pfsinfo-$$"
    cat > "$FAKE_PFSINFO" <<'PFSINFO'
#!/bin/bash
exit 0
PFSINFO
    chmod +x "$FAKE_PFSINFO"
}

teardown() {
    rm -rf "$DLD" "$WORKDIR" "$FAKE_PFSINFO" "$BATS_TEST_TMPDIR/cache-$$"
}

run_modman() {
    PFSINFO_BIN="$FAKE_PFSINFO" run "$MODMAN" --machine "$@"
}

@test "remove-local: happy path single file" {
    touch "$DLD/foo.pfs"
    [ -f "$DLD/foo.pfs" ]

    run_modman --remove-local foo

    [ "$status" -eq 0 ]
    [[ "$output" == *"removed"*"foo"* ]]
    [ ! -f "$DLD/foo.pfs" ]
}

@test "remove-local: machine parity foo vs foo.pfs" {
    touch "$DLD/bar.pfs"

    run_modman --remove-local bar
    local without_suffix_status=$status
    local without_suffix_output="$output"

    touch "$DLD/bar.pfs"

    run_modman --remove-local bar.pfs
    local with_suffix_status=$status
    local with_suffix_output="$output"

    [ "$without_suffix_status" -eq "$with_suffix_status" ]
    [ "$without_suffix_output" = "$with_suffix_output" ]
    [ ! -f "$DLD/bar.pfs" ]
}

@test "remove-local: mounted source-path match refused even when mounted name differs" {
    touch "$DLD/path-only.pfs"

    local proc_mounts="$BATS_TEST_TMPDIR/proc-mounts-$$"
    local fake_bin="$BATS_TEST_TMPDIR/fake-bin-$$"
    mkdir -p "$fake_bin"

    cat > "$proc_mounts" <<'MOUNTS'
/dev/loop7 /mnt/.mounted-path-only squashfs ro,relatime 0 0
MOUNTS

    cat > "$fake_bin/losetup" <<EOF
#!/bin/bash
printf '/dev/loop7: [2065]:932177 (%s)\n' "$DLD/path-only.pfs"
EOF
    chmod +x "$fake_bin/losetup"

    cat > "$FAKE_PFSINFO" <<'PFSINFO'
#!/bin/bash
case "$1" in
    --machine) printf 'other-name\taufs\t/mnt/.mounted-path-only\t\t\t\n' ;;
esac
exit 0
PFSINFO
    chmod +x "$FAKE_PFSINFO"

    PATH="$fake_bin:$PATH" MODMAN_PROC_MOUNTS="$proc_mounts" run_modman --remove-local path-only

    [ "$status" -eq 8 ]
    [[ "$output" == *"error"*"mounted"*"path-only"* ]]
    [ -f "$DLD/path-only.pfs" ]
}

@test "remove-local: mounted file refused" {
    touch "$DLD/mounted-mod.pfs"

    local proc_mounts="$BATS_TEST_TMPDIR/proc-mounts-mounted-$$"
    local fake_bin="$BATS_TEST_TMPDIR/fake-bin-mounted-$$"
    mkdir -p "$fake_bin"

    cat > "$proc_mounts" <<'MOUNTS'
/dev/loop0 /mnt/.mounted-mod squashfs ro,relatime 0 0
MOUNTS

    cat > "$fake_bin/losetup" <<EOF
#!/bin/bash
printf '/dev/loop0: [2065]:932177 (%s)\n' "$DLD/mounted-mod.pfs"
EOF
    chmod +x "$fake_bin/losetup"

    cat > "$FAKE_PFSINFO" <<'PFSINFO'
#!/bin/bash
case "$1" in
    --machine) printf 'mounted-mod\taufs\t/mnt/.mounted-mod\t\t\t\n' ;;
esac
exit 0
PFSINFO
    chmod +x "$FAKE_PFSINFO"

    PATH="$fake_bin:$PATH" MODMAN_PROC_MOUNTS="$proc_mounts" run_modman --remove-local mounted-mod

    [ "$status" -eq 8 ]
    [[ "$output" == *"error"*"mounted"*"mounted-mod"* ]]
    [ -f "$DLD/mounted-mod.pfs" ]
}

@test "remove-local: invalid name rejected" {
    run_modman --remove-local 'bad name with spaces'

    [ "$status" -eq 7 ]
    [[ "$output" == *"error"*"invalid_name"* ]]
}

@test "remove-local: not found" {
    run_modman --remove-local nonexistent

    [ "$status" -eq 9 ]
    [[ "$output" == *"error"*"not_found"*"nonexistent"* ]]
}

@test "remove-local: variadic mixed (removed + mounted + removed)" {
    touch "$DLD/a.pfs" "$DLD/b.pfs" "$DLD/c.pfs"

    local proc_mounts="$BATS_TEST_TMPDIR/proc-mounts-variadic-$$"
    local fake_bin="$BATS_TEST_TMPDIR/fake-bin-variadic-$$"
    mkdir -p "$fake_bin"

    cat > "$proc_mounts" <<'MOUNTS'
/dev/loop3 /mnt/.mounted-b squashfs ro,relatime 0 0
MOUNTS

    cat > "$fake_bin/losetup" <<EOF
#!/bin/bash
printf '/dev/loop3: [2065]:932177 (%s)\n' "$DLD/b.pfs"
EOF
    chmod +x "$fake_bin/losetup"

    cat > "$FAKE_PFSINFO" <<'PFSINFO'
#!/bin/bash
case "$1" in
    --machine) printf 'b\taufs\t/mnt/.mounted-b\t\t\t\n' ;;
esac
exit 0
PFSINFO
    chmod +x "$FAKE_PFSINFO"

    PATH="$fake_bin:$PATH" MODMAN_PROC_MOUNTS="$proc_mounts" run_modman --remove-local a b c

    [ "$status" -eq 8 ]
    [ ! -f "$DLD/a.pfs" ]
    [ -f "$DLD/b.pfs" ]
    [ ! -f "$DLD/c.pfs" ]
    [[ "$output" == *"removed"*"a"* ]]
    [[ "$output" == *"error"*"mounted"*"b"* ]]
    [[ "$output" == *"removed"*"c"* ]]
}

@test "remove-local: no args produces error" {
    run_modman --remove-local

    [ "$status" -ne 0 ]
}

@test "remove-local: machine parity --remove-local vs -Rl" {
    touch "$DLD/aliasmod.pfs"

    run_modman --remove-local aliasmod
    local long_status=$status
    local long_output="$output"

    touch "$DLD/aliasmod.pfs"

    run_modman -Rl aliasmod
    local short_status=$status
    local short_output="$output"

    [ "$long_status" -eq "$short_status" ]
    [ "$long_output" = "$short_output" ]
    [ ! -f "$DLD/aliasmod.pfs" ]
}

@test "alias -L works same as --list-loaded" {
    PFSINFO_BIN="$FAKE_PFSINFO" run "$MODMAN" --machine -L
    local short_status=$status
    local short_output="$output"

    PFSINFO_BIN="$FAKE_PFSINFO" run "$MODMAN" --machine --list-loaded
    local long_status=$status
    local long_output="$output"

    [ "$short_status" -eq "$long_status" ]
    [ "$short_output" = "$long_output" ]
}

@test "list-loaded-after keeps AUFS runtime mount after backing file was rotated to .old" {
    local fake_bin="$BATS_TEST_TMPDIR/fake-bin-runtime-old-$$"
    local aufs_root="$BATS_TEST_TMPDIR/aufs-runtime-old-$$"
    local proc_mounts="$BATS_TEST_TMPDIR/proc-mounts-runtime-old-$$"

    mkdir -p "$fake_bin" "$aufs_root/si_test"
    cat > "$proc_mounts" <<'MOUNTS'
aufs / aufs rw,relatime,si=test 0 0
/dev/loop9 /run/bundles/.premote-p_64-sf04.pfs squashfs ro,relatime 0 0
MOUNTS
    printf '/run/bundles/.premote-p_64-sf04.pfs=ro\n' > "$aufs_root/si_test/br1"

    cat > "$fake_bin/mountpoint" <<'MOUNTPOINT'
#!/usr/bin/env bash
if [ "$1" = "-q" ] && [ "$2" = "/mnt/.premote-p_64-sf04.pfs" ]; then
    exit 0
fi
exit 1
MOUNTPOINT
    chmod +x "$fake_bin/mountpoint"

    cat > "$fake_bin/losetup" <<'LOSETUP'
#!/usr/bin/env bash
if [ "$1" = "-a" ]; then
    printf '/dev/loop9: [2065]:9 (/modules/premote-p_64-sf04.pfs.old)\n'
    exit 0
fi
if [ "$1" = "/dev/loop9" ]; then
    printf '/dev/loop9: [2065]:9 (/modules/premote-p_64-sf04.pfs.old)\n'
    exit 0
fi
exit 1
LOSETUP
    chmod +x "$fake_bin/losetup"

    PATH="$fake_bin:$PATH" \
        MODMAN_PROC_MOUNTS="$proc_mounts" \
        MODMAN_AUFS_SYS_ROOT="$aufs_root" \
        RUNTIME_MOUNT_PREFIX="/mnt/." \
        PFSINFO_BIN="$FAKE_PFSINFO" \
        run "$MODMAN" --machine --list-loaded-after

    [ "$status" -eq 0 ]
    [ "$output" = $'premote-p_64-sf04.pfs\t1\t/run/bundles/.premote-p_64-sf04.pfs\t-\t\t' ]
}

@test "alias -Lb works same as --list-loaded-before" {
    PFSINFO_BIN="$FAKE_PFSINFO" run "$MODMAN" --machine -Lb
    local short_status=$status
    local short_output="$output"

    PFSINFO_BIN="$FAKE_PFSINFO" run "$MODMAN" --machine --list-loaded-before
    local long_status=$status
    local long_output="$output"

    [ "$short_status" -eq "$long_status" ]
    [ "$short_output" = "$long_output" ]
}

@test "alias -La works same as --list-loaded-after" {
    PFSINFO_BIN="$FAKE_PFSINFO" run "$MODMAN" --machine -La
    local short_status=$status
    local short_output="$output"

    PFSINFO_BIN="$FAKE_PFSINFO" run "$MODMAN" --machine --list-loaded-after
    local long_status=$status
    local long_output="$output"

    [ "$short_status" -eq "$long_status" ]
    [ "$short_output" = "$long_output" ]
}

@test "alias -Ql works same as --list-local" {
    touch "$DLD/xyz.pfs"

    PFSINFO_BIN="$FAKE_PFSINFO" run "$MODMAN" --machine -Ql
    local short_status=$status
    local short_output="$output"

    PFSINFO_BIN="$FAKE_PFSINFO" run "$MODMAN" --machine --list-local
    local long_status=$status
    local long_output="$output"

    [ "$short_status" -eq "$long_status" ]
    [ "$short_output" = "$long_output" ]
}
