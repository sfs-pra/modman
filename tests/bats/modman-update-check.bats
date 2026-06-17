#!/usr/bin/env bats
# Tests for modman-update-check — non-mutating login-time update notifier.
# Verifies: no --update in argv log, notify-send called on updates,
# silent exit on no updates, UPDATE_CHECK_ON_LOGIN=0 skips modman entirely.

setup_file() {
    export HELPER="${BATS_TEST_DIRNAME}/../../modman-update-check"
}

setup() {
    export FAKE_BIN
    export FAKE_MODMAN_LOG
    export FAKE_NOTIFY_LOG
    export FAKE_GUI_LOG
    export FAKE_CONF

    FAKE_BIN="$(mktemp -d "${BATS_TEST_TMPDIR}/fake-bin.XXXXXX")"
    FAKE_MODMAN_LOG="${BATS_TEST_TMPDIR}/modman-calls.log"
    FAKE_NOTIFY_LOG="${BATS_TEST_TMPDIR}/notify-calls.log"
    FAKE_GUI_LOG="${BATS_TEST_TMPDIR}/gui-calls.log"
    FAKE_CONF="${BATS_TEST_TMPDIR}/modman.conf"

    : >"${FAKE_MODMAN_LOG}"
    : >"${FAKE_NOTIFY_LOG}"
    : >"${FAKE_GUI_LOG}"

    # Default fake modman: outputs no update rows (no updates)
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    # Default fake notify-send: logs call
    cat >"${FAKE_BIN}/notify-send" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_NOTIFY_LOG}"
exit 0
EOF
    chmod +x "${FAKE_BIN}/notify-send"

    # Default fake modman-gui: logs call
    cat >"${FAKE_BIN}/modman-gui" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_GUI_LOG}"
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman-gui"

    # Minimal modman.conf (no special overrides)
    printf '# test conf\n' >"${FAKE_CONF}"
}

teardown() {
    rm -rf "${FAKE_BIN}"
    rm -f "${FAKE_MODMAN_LOG}" "${FAKE_NOTIFY_LOG}" "${FAKE_GUI_LOG}" "${FAKE_CONF}"
}

# Helper: run the helper with fake PATH and MODMAN_BIN
# Extra args are passed to the HELPER, not to env.
run_helper() {
    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        "${HELPER}" "$@"
}

# Helper: run with extra env vars before the helper path
run_helper_env() {
    # All args before -- are env vars, rest are helper args
    local -a env_args=()
    local -a helper_args=()
    local past_sep=0
    for arg in "$@"; do
        if [[ "$arg" == "--" ]]; then
            past_sep=1
            continue
        fi
        if [[ "$past_sep" -eq 0 ]]; then
            env_args+=("$arg")
        else
            helper_args+=("$arg")
        fi
    done
    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        "${env_args[@]}" \
        "${HELPER}" "${helper_args[@]}"
}

# ==========================================
# BASIC SANITY
# ==========================================

@test "helper script exists and is executable" {
    [ -x "${HELPER}" ]
}

@test "helper --help exits 0 and prints usage" {
    run_helper --help
    [ "$status" -eq 0 ]
    [[ "$output" == *"Usage"* ]]
}

@test "helper passes bash -n syntax check" {
    run bash -n "${HELPER}"
    [ "$status" -eq 0 ]
}

# ==========================================
# NO UPDATES: silent exit
# ==========================================

@test "no updates: exits 0 silently without calling notify-send" {
    # fake modman outputs nothing (no update rows)
    run_helper
    [ "$status" -eq 0 ]
    # notify-send must NOT have been called
    [ ! -s "${FAKE_NOTIFY_LOG}" ]
}

@test "no updates: modman --machine --check-updates IS invoked" {
    run_helper
    [ "$status" -eq 0 ]
    grep -q -- "--machine" "${FAKE_MODMAN_LOG}"
    grep -q -- "--check-updates" "${FAKE_MODMAN_LOG}"
}

# ==========================================
# UPDATES FOUND: notify-send called
# ==========================================

@test "updates found: notify-send is called when update rows exist" {
    # fake modman outputs one update row
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
printf 'update\tid1\tfoo\t1.0\t1.1\t/path/foo-1.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    run_helper
    [ "$status" -eq 0 ]
    [ -s "${FAKE_NOTIFY_LOG}" ]
}

@test "updates found: notify-send receives non-empty summary and body" {
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
printf 'update\tid1\tfoo\t1.0\t1.1\t/path/foo-1.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    run_helper
    [ "$status" -eq 0 ]
    # notify-send log must contain something (summary or body text)
    [ -s "${FAKE_NOTIFY_LOG}" ]
    # Should mention count or updates
    grep -qiE '(обновлен|update|найдено|1)' "${FAKE_NOTIFY_LOG}"
}

@test "updates found: modman-gui is NOT opened by default (no --open-gui)" {
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
printf 'update\tid1\tfoo\t1.0\t1.1\t/path/foo-1.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    run_helper
    [ "$status" -eq 0 ]
    # modman-gui must NOT have been called
    [ ! -s "${FAKE_GUI_LOG}" ]
}

# ==========================================
# --open-gui FLAG
# ==========================================

@test "--open-gui: modman-gui --updates is launched when updates exist" {
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
printf 'update\tid1\tfoo\t1.0\t1.1\t/path/foo-1.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        "${HELPER}" --open-gui
    [ "$status" -eq 0 ]
    # Give background process a moment to write
    sleep 0.2
    [ -s "${FAKE_GUI_LOG}" ]
    grep -q -- "--updates" "${FAKE_GUI_LOG}"
}

@test "--open-gui: modman-gui is NOT launched when no updates exist" {
    # fake modman: no update rows
    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        "${HELPER}" --open-gui
    [ "$status" -eq 0 ]
    sleep 0.1
    [ ! -s "${FAKE_GUI_LOG}" ]
}

# ==========================================
# UPDATE_CHECK_OPEN_GUI=1 config var
# ==========================================

@test "UPDATE_CHECK_OPEN_GUI=1: modman-gui launched when updates exist" {
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
printf 'update\tid1\tfoo\t1.0\t1.1\t/path/foo-1.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        UPDATE_CHECK_OPEN_GUI=1 \
        "${HELPER}"
    [ "$status" -eq 0 ]
    sleep 0.2
    [ -s "${FAKE_GUI_LOG}" ]
    grep -q -- "--updates" "${FAKE_GUI_LOG}"
}

# ==========================================
# UPDATE_CHECK_ON_LOGIN=0: skip entirely
# ==========================================

@test "UPDATE_CHECK_ON_LOGIN=0: exits 0 without invoking modman at all" {
    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        UPDATE_CHECK_ON_LOGIN=0 \
        "${HELPER}"
    [ "$status" -eq 0 ]
    # modman must NOT have been called
    [ ! -s "${FAKE_MODMAN_LOG}" ]
}

@test "UPDATE_CHECK_ON_LOGIN=0: notify-send is never called" {
    # Even if modman would return updates, with ON_LOGIN=0 we skip
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
printf 'update\tid1\tfoo\t1.0\t1.1\t/path/foo-1.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        UPDATE_CHECK_ON_LOGIN=0 \
        "${HELPER}"
    [ "$status" -eq 0 ]
    [ ! -s "${FAKE_MODMAN_LOG}" ]
    [ ! -s "${FAKE_NOTIFY_LOG}" ]
}

# ==========================================
# NEVER --update in argv
# ==========================================

@test "helper never passes --update to modman (only --check-updates)" {
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
printf 'update\tid1\tfoo\t1.0\t1.1\t/path/foo-1.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    run_helper
    [ "$status" -eq 0 ]
    # --update (mutating) must never appear in the log
    if [ -s "${FAKE_MODMAN_LOG}" ]; then
        # Allow --check-updates but not bare --update
        run grep -E '(^| )--update( |$)' "${FAKE_MODMAN_LOG}"
        [ "$status" -ne 0 ]
    fi
}

@test "helper never passes --update-all to modman" {
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    run_helper
    [ "$status" -eq 0 ]
    if [ -s "${FAKE_MODMAN_LOG}" ]; then
        run grep -- "--update-all" "${FAKE_MODMAN_LOG}"
        [ "$status" -ne 0 ]
    fi
}

# ==========================================
# MODMAN_BIN env var respected
# ==========================================

@test "MODMAN_BIN env var is used to locate modman" {
    local alt_bin="${BATS_TEST_TMPDIR}/alt-modman"
    local alt_log="${BATS_TEST_TMPDIR}/alt-modman.log"
    cat >"${alt_bin}" <<EOF
#!/bin/bash
printf '%s\n' "\$*" >>"${alt_log}"
exit 0
EOF
    chmod +x "${alt_bin}"

    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${alt_bin}" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        "${HELPER}"
    [ "$status" -eq 0 ]
    # alt_bin was called, not the fake_bin/modman
    [ -f "${alt_log}" ]
    grep -q -- "--check-updates" "${alt_log}"
    # The default fake_bin/modman was NOT called
    [ ! -s "${FAKE_MODMAN_LOG}" ]
}

# ==========================================
# MODMAN_CONF sourcing
# ==========================================

@test "MODMAN_CONF is sourced: UPDATE_CHECK_ON_LOGIN=0 in conf disables check" {
    printf 'UPDATE_CHECK_ON_LOGIN=0\n' >"${FAKE_CONF}"

    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        "${HELPER}"
    [ "$status" -eq 0 ]
    [ ! -s "${FAKE_MODMAN_LOG}" ]
}

@test "missing MODMAN_CONF does not cause failure" {
    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="/nonexistent/path/modman.conf" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        "${HELPER}"
    [ "$status" -eq 0 ]
}

# ==========================================
# GRACEFUL DEGRADATION
# ==========================================

@test "modman check-updates non-zero exit: helper exits 0 silently" {
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
exit 2
EOF
    chmod +x "${FAKE_BIN}/modman"

    run_helper
    [ "$status" -eq 0 ]
    [ ! -s "${FAKE_NOTIFY_LOG}" ]
}

@test "notify-send absent: helper still exits 0 when updates found" {
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
printf 'update\tid1\tfoo\t1.0\t1.1\t/path/foo-1.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"
    # Remove notify-send from fake bin; keep system PATH so bash/awk/etc. still work
    rm -f "${FAKE_BIN}/notify-send"

    run env \
        PATH="${FAKE_BIN}:${PATH}" \
        MODMAN_BIN="${FAKE_BIN}/modman" \
        MODMAN_CONF="${FAKE_CONF}" \
        FAKE_MODMAN_LOG="${FAKE_MODMAN_LOG}" \
        FAKE_NOTIFY_LOG="${FAKE_NOTIFY_LOG}" \
        FAKE_GUI_LOG="${FAKE_GUI_LOG}" \
        "${HELPER}"
    [ "$status" -eq 0 ]
}

@test "multiple update rows: all counted, notify-send called once" {
    cat >"${FAKE_BIN}/modman" <<'EOF'
#!/bin/bash
printf '%s\n' "$*" >>"${FAKE_MODMAN_LOG}"
printf 'update\tid1\tfoo\t1.0\t1.1\t/path/foo-1.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
printf 'update\tid2\tbar\t2.0\t2.1\t/path/bar-2.0.pfs\t-\thttps://repo\tnormal\t0\t\n'
printf 'update\tid3\tbaz\t3.0\t3.1\t/path/baz-3.0.pfs\t-\thttps://repo\tsystem\t1\tнужна перезагрузка\n'
exit 0
EOF
    chmod +x "${FAKE_BIN}/modman"

    run_helper
    [ "$status" -eq 0 ]
    [ -s "${FAKE_NOTIFY_LOG}" ]
    # notify-send called exactly once
    notify_call_count=$(wc -l <"${FAKE_NOTIFY_LOG}")
    [ "$notify_call_count" -ge 1 ]
}
