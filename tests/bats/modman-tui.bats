#!/usr/bin/env bats

setup() {
    export REPO_ROOT="${BATS_TEST_DIRNAME}/../.."
    export WORKDIR
    export DIALOG_LOG
    export DIALOG_RESPONSES
    export MODMAN_LOG
    export FAKE_DIALOG
    export FAKE_MODMAN
    export FAKE_UNSQUASHFS
    export FAKE_DUMP_EROFS

    WORKDIR="$(mktemp -d "${BATS_TEST_TMPDIR}/modman-tui.XXXXXX")"
    DIALOG_LOG="${WORKDIR}/dialog.log"
    DIALOG_RESPONSES="${WORKDIR}/dialog.responses"
    MODMAN_LOG="${WORKDIR}/modman.log"
    FAKE_DIALOG="${WORKDIR}/dialog"
    FAKE_MODMAN="${WORKDIR}/modman"
    FAKE_UNSQUASHFS="${WORKDIR}/unsquashfs"
    FAKE_DUMP_EROFS="${WORKDIR}/dump.erofs"
    : >"${DIALOG_LOG}"
    : >"${DIALOG_RESPONSES}"
    : >"${MODMAN_LOG}"

    cat >"${FAKE_DIALOG}" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
printf 'dialog' >>"${DIALOG_LOG}"
for arg in "$@"; do
    printf '\t%s' "$arg" >>"${DIALOG_LOG}"
done
printf '\n' >>"${DIALOG_LOG}"
prev=""
for arg in "$@"; do
    if [[ "${prev}" == "--textbox" && -f "${arg}" ]]; then
        printf 'textbox-content\t' >>"${DIALOG_LOG}"
        tr '\n' '|' <"${arg}" >>"${DIALOG_LOG}"
        printf '\n' >>"${DIALOG_LOG}"
    fi
    prev="${arg}"
done

status=1
payload=""
if [[ -s "${DIALOG_RESPONSES}" ]]; then
    line="$(sed -n '1p' "${DIALOG_RESPONSES}")"
    sed -i '1d' "${DIALOG_RESPONSES}"
    status="${line%%$'\t'*}"
    if [[ "${line}" == *$'\t'* ]]; then
        payload="${line#*$'\t'}"
    fi
fi
if [[ -n "${payload}" ]]; then
    payload="${payload//\\n/$'\n'}"
    payload="${payload//\\t/$'\t'}"
    printf '%b' "${payload}"
fi
exit "${status}"
EOF
    chmod +x "${FAKE_DIALOG}"

    cat >"${FAKE_MODMAN}" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
printf 'modman' >>"${MODMAN_LOG}"
for arg in "$@"; do
    printf '\t%s' "$arg" >>"${MODMAN_LOG}"
done
printf '\n' >>"${MODMAN_LOG}"

if [[ "${MODMAN_UNSUPPORTED:-}" == "$*" ]]; then
    printf 'unknown option\n' >&2
    exit 2
fi

case "$*" in
    "--machine --list-loaded-after")
        printf 'z-after_64-sf04.pfs\t10\t/run/bundles/.z-after_64-sf04.pfs\tz desc\tlower\tno\n'
        printf 'premote-p_64-sf04.pfs\t1\t/run/bundles/very/long/path/that/should/wrap/in/details/instead/of/being/cut/by/dialog/.premote-p_64-sf04.pfs\tdesc\\twith\\nlines\t\t\n'
        ;;
    "--machine --list-loaded-before")
        printf 'base_64-sf04.pfs\t9\t/mnt/home/base/base_64-sf04.pfs\tbase\tupper\tyes\n'
        ;;
    "--machine --list-local")
        printf 'erofs.pfs\t3M\t2026-05-03\t/var/lib/modman/modules/erofs.pfs\n'
        printf 'bar.pfs\t2M\t2026-05-02\t/var/lib/modman/modules/bar.pfs\n'
        printf 'foo.pfs\t1M\t2026-05-01\t/var/lib/modman/modules/foo.pfs\n'
        ;;
    "--machine -Ss browser")
        printf 'zz-selected-module-with-very-long-name-that-must-be-visible.pfs\t1M\t2026-05-01\tBrowser\tnet\thttps://repo.example\n'
        printf 'selected-module.pfs\t1M\t2026-05-01\tBrowser\tnet\thttps://repo.example\n'
        ;;
    "--machine -Ss foo; rm -rf /")
        printf 'selected-module.pfs\t1M\t2026-05-01\tBrowser\tnet\thttps://repo.example\n'
        ;;
    "--machine --check-updates")
        printf 'update\tid-normal\tfoo\t1.0\t1.1\t/var/lib/modman/modules/foo.pfs\tfoo-1.1.pfs\thttps://repo.example\tnormal\t0\t\n'
        printf 'update\tid-system\tbase\t1.0\t1.1\t/mnt/home/base/base.pfs\tbase-1.1.pfs\thttps://repo.example\tsystem\t1\tнужна перезагрузка\n'
        ;;
    "--machine --update-blacklist-list")
        printf 'blacklist\tfoo\t1.1\tfoo-1.1.pfs\thttps://repo.example\n'
        ;;
    "--machine --list-old")
        printf '/mnt/home/base/foo.pfs.old\n'
        printf '/mnt/home/modules/bar.pfs.old\n'
        ;;
    "--machine -R premote-p_64-sf04.pfs"|"--machine -R base_64-sf04.pfs"|"--machine -S /var/lib/modman/modules/foo.pfs"|"--machine -S /var/lib/modman/modules/bar.pfs"|"--machine -S /tmp/file.pfs"|"--machine -S selected-module.pfs"|"--machine --remove-local foo.pfs bar.pfs"|"--machine --update id-normal"|"--machine --confirm-system-updates --update id-system"|"--machine --update-blacklist-add id-normal"|"--machine --update-blacklist-remove foo 1.1"|"--machine --remove-old /mnt/home/base/foo.pfs.old"|"--machine --remove-old /mnt/home/base/foo.pfs.old /mnt/home/modules/bar.pfs.old")
        printf 'ok\n'
        ;;
    *)
        printf 'unexpected command: %s\n' "$*" >&2
        exit 2
        ;;
esac
EOF
    chmod +x "${FAKE_MODMAN}"

    cat >"${FAKE_UNSQUASHFS}" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
if [[ "$*" == "-s /var/lib/modman/modules/foo.pfs" ]]; then
    printf 'Found a valid SQUASHFS 4:0 superblock\n'
    printf 'Compression zstd\n'
    printf '\tcompression-level 18\n'
    exit 0
fi
exit 1
EOF
    chmod +x "${FAKE_UNSQUASHFS}"

    cat >"${FAKE_DUMP_EROFS}" <<'EOF'
#!/usr/bin/env bash
set -Eeuo pipefail
if [[ "$*" == "/var/lib/modman/modules/erofs.pfs" ]]; then
    printf 'Filesystem magic number: EROFS\n'
    printf 'Compression: lz4\n'
    exit 0
fi
exit 1
EOF
    chmod +x "${FAKE_DUMP_EROFS}"
}

teardown() {
    rm -rf "${WORKDIR}"
}

enqueue_dialog() {
    printf '%s\t%s\n' "$1" "${2-}" >>"${DIALOG_RESPONSES}"
}

run_tui() {
    run env PATH="${WORKDIR}:${PATH}" DIALOG_BIN="${FAKE_DIALOG}" MODMAN_BIN="${FAKE_MODMAN}" MODMAN_TUI_TMPDIR="${WORKDIR}/tmp" "${REPO_ROOT}/modman-tui"
}

@test "help works without dialog or modman" {
    run env DIALOG_BIN=/missing MODMAN_BIN=/missing "${REPO_ROOT}/modman-tui" --help
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"Usage"* ]]
    [[ "${output}" == *"modman-tui"* ]]
}

@test "missing dialog fails safely" {
    run env DIALOG_BIN=/missing MODMAN_BIN=/bin/true "${REPO_ROOT}/modman-tui"
    [ "${status}" -ne 0 ]
    [[ "${output}" == *"dialog"* ]]
}

@test "connected list opens from machine output and unescapes fields" {
    enqueue_dialog 0 connected
    enqueue_dialog 0 premote-p_64-sf04.pfs
    enqueue_dialog 0 details
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t--list-loaded-after'* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"premote-p_64-sf04.pfs"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"with"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"01 |"* ]]
}

@test "connected details wrap long path and explain empty ram field" {
    enqueue_dialog 0 connected
    enqueue_dialog 0 premote-p_64-sf04.pfs
    enqueue_dialog 0 details
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${DIALOG_LOG}")" == *"Path:|  /run/bundles/very/long/path/that/should/wrap/in/details/instead/of/b|  eing/cut/by/dialog/.premote-p_64-sf04.pfs"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"In RAM: no data from backend"* ]]
}

@test "connected unload cancel is non-mutating" {
    enqueue_dialog 0 connected
    enqueue_dialog 0 premote-p_64-sf04.pfs
    enqueue_dialog 0 unload
    enqueue_dialog 1 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" != *$'\t-R\t'* ]]
}

@test "connected unload confirm calls backend by module name" {
    enqueue_dialog 0 connected
    enqueue_dialog 0 premote-p_64-sf04.pfs
    enqueue_dialog 0 unload
    enqueue_dialog 0 ""
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t-R\tpremote-p_64-sf04.pfs'* ]]
}

@test "system screen can unload with confirmation" {
    enqueue_dialog 0 system
    enqueue_dialog 0 base_64-sf04.pfs
    enqueue_dialog 0 unload
    enqueue_dialog 0 ""
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t--list-loaded-before'* ]]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t-R\tbase_64-sf04.pfs'* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"09 |"* ]]
}

@test "connected details include path mode ram and backend" {
    enqueue_dialog 0 connected
    enqueue_dialog 0 z-after_64-sf04.pfs
    enqueue_dialog 0 details
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${DIALOG_LOG}")" == *"/run/bundles/.z-after_64-sf04.pfs"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"lower"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"Backend:"* ]]
}

@test "unsupported connected command shows unavailable and does not mutate" {
    export MODMAN_UNSUPPORTED="--machine --list-loaded-after"
    enqueue_dialog 0 connected
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${DIALOG_LOG}")" == *"Feature is unavailable"* ]]
    [[ "$(<"${MODMAN_LOG}")" != *$'\t-R\t'* ]]
}

@test "local attach uses path field" {
    enqueue_dialog 0 local
    enqueue_dialog 0 "foo.pfs"
    enqueue_dialog 0 attach
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t-S\t/var/lib/modman/modules/foo.pfs'* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"      1M |"* ]]
}

@test "local details show metadata and path" {
    enqueue_dialog 0 local
    enqueue_dialog 0 "foo.pfs"
    enqueue_dialog 0 details
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${DIALOG_LOG}")" == *"Name: foo.pfs"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"Size: 1M"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"Path:|  /var/lib/modman/modules/foo.pfs"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"SQUASHFS compression: zstd"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"SQUASHFS compression level: 18"* ]]
}

@test "local details show erofs compression without squashfs level" {
    enqueue_dialog 0 local
    enqueue_dialog 0 "erofs.pfs"
    enqueue_dialog 0 details
    enqueue_dialog 0 ""
    enqueue_dialog 0 back
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${DIALOG_LOG}")" == *"EROFS compression: lz4"* ]]
    [[ "$(<"${DIALOG_LOG}")" != *"SQUASHFS compression level:"* ]]
}

@test "local details returns to local screen instead of main menu" {
    enqueue_dialog 0 local
    enqueue_dialog 0 "foo.pfs"
    enqueue_dialog 0 details
    enqueue_dialog 0 ""
    enqueue_dialog 0 "bar.pfs"
    enqueue_dialog 0 attach
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [ "$(grep -c -- "--list-local" "${MODMAN_LOG}")" -ge 2 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t-S\t/var/lib/modman/modules/bar.pfs'* ]]
}

@test "remove local cancel is safe" {
    enqueue_dialog 0 local
    enqueue_dialog 0 'foo.pfs\nbar.pfs'
    enqueue_dialog 0 remove
    enqueue_dialog 1 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" != *"--remove-local"* ]]
}

@test "remove local confirm passes two names" {
    enqueue_dialog 0 local
    enqueue_dialog 0 'foo.pfs\nbar.pfs'
    enqueue_dialog 0 remove
    enqueue_dialog 0 ""
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t--remove-local\tfoo.pfs\tbar.pfs'* ]]
}

@test "file select cancel logs no install" {
    enqueue_dialog 0 local
    enqueue_dialog 0 "foo.pfs"
    enqueue_dialog 0 file
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" != *$'\t-S\t/tmp/file.pfs'* ]]
}

@test "search query with shell metacharacters is one backend argument" {
    enqueue_dialog 0 search
    enqueue_dialog 0 'foo; rm -rf /'
    enqueue_dialog 1 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t-Ss\tfoo; rm -rf /'* ]]
    [[ ! -e /tmp/modman-tui-should-not-exist ]]
}

@test "install cancel is non-mutating" {
    enqueue_dialog 0 search
    enqueue_dialog 0 browser
    enqueue_dialog 0 001
    enqueue_dialog 1 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t-Ss\tbrowser'* ]]
    [[ "$(<"${MODMAN_LOG}")" != *$'\t-S\tselected-module.pfs'* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"zz-selected-module-with-very-long-name-that-must-be-visible.pfs"* ]]
    [[ "$(<"${DIALOG_LOG}")" == *$'--checklist\tSearch results'*$'\t001\tselected-module.pfs | Browser'* ]]
    [[ "$(<"${DIALOG_LOG}")" == *"Attach selected modules?"* ]]
    [[ "$(<"${DIALOG_LOG}")" != *"Install selected modules?"* ]]
}

@test "install confirm mutates through modman" {
    enqueue_dialog 0 search
    enqueue_dialog 0 browser
    enqueue_dialog 0 001
    enqueue_dialog 0 ""
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t-S\tselected-module.pfs'* ]]
}

@test "system update cancel is safe" {
    enqueue_dialog 0 updates
    enqueue_dialog 0 001
    enqueue_dialog 0 apply
    enqueue_dialog 1 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *"--check-updates"* ]]
    [[ "$(<"${MODMAN_LOG}")" != *"--update"* ]]
}

@test "normal update confirm calls update id" {
    enqueue_dialog 0 updates
    enqueue_dialog 0 002
    enqueue_dialog 0 apply
    enqueue_dialog 0 ""
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t--update\tid-normal'* ]]
}

@test "blacklist add logs selected id" {
    enqueue_dialog 0 updates
    enqueue_dialog 0 002
    enqueue_dialog 0 blacklist
    enqueue_dialog 0 ""
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t--update-blacklist-add\tid-normal'* ]]
}

@test "updates show user text instead of technical id tag" {
    enqueue_dialog 0 updates
    enqueue_dialog 1 ""
    enqueue_dialog 0 back
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${DIALOG_LOG}")" == *$'--checklist\tUpdates'*$'\t001\tbase 1.0→1.1 [system, reboot required]'* ]]
    [[ "$(<"${DIALOG_LOG}")" != *$'\tid-system\t'* ]]
}

@test "blacklist removal refreshes updates" {
    enqueue_dialog 0 updates
    enqueue_dialog 1 ""
    enqueue_dialog 0 edit-blacklist
    enqueue_dialog 0 'foo|1.1'
    enqueue_dialog 0 ""
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t--update-blacklist-remove\tfoo\t1.1'* ]]
    [ "$(grep -c -- "--check-updates" "${MODMAN_LOG}")" -ge 2 ]
}

@test "old cleanup cancel is non-mutating" {
    enqueue_dialog 0 cleanup
    enqueue_dialog 0 /mnt/home/base/foo.pfs.old
    enqueue_dialog 1 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *"--list-old"* ]]
    [[ "$(<"${MODMAN_LOG}")" != *"--remove-old"* ]]
}

@test "old cleanup confirm passes multiple full paths" {
    enqueue_dialog 0 cleanup
    enqueue_dialog 0 '/mnt/home/base/foo.pfs.old\n/mnt/home/modules/bar.pfs.old'
    enqueue_dialog 0 ""
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${MODMAN_LOG}")" == *$'--machine\t--remove-old\t/mnt/home/base/foo.pfs.old\t/mnt/home/modules/bar.pfs.old'* ]]
}

@test "about screen is read-only" {
    enqueue_dialog 0 about
    enqueue_dialog 0 about
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${DIALOG_LOG}")" == *"modman-tui"* ]]
    [ ! -s "${MODMAN_LOG}" ]
}

@test "config screen is read-only" {
    enqueue_dialog 0 about
    enqueue_dialog 0 config
    enqueue_dialog 0 ""
    enqueue_dialog 0 exit
    run_tui
    [ "${status}" -eq 0 ]
    [[ "$(<"${DIALOG_LOG}")" == *"/etc/modman.conf"* ]]
    [ ! -s "${MODMAN_LOG}" ]
}
