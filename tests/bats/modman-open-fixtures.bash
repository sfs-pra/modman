# Shared setup for modman-open Bats tests.
# Source this file in setup() of modman-open test suites.

setup_modman_open_fixtures() {
    export FAKE_MODMAN_LOG
    FAKE_MODMAN_LOG=$(mktemp)
    export MODMAN_BIN="${BATS_TEST_DIRNAME}/fixtures/fake-modman-open.sh"
    chmod +x "$MODMAN_BIN"

    export SAMPLE_PFS="${BATS_TEST_DIRNAME}/../fixtures/sample.pfs"
    export SANDBOX_DIR
    SANDBOX_DIR=$(mktemp -d)
    export DOWNLOAD_DIR="$SANDBOX_DIR/modules"
    mkdir -p "$DOWNLOAD_DIR"
}

teardown_modman_open_fixtures() {
    rm -f "$FAKE_MODMAN_LOG" 2>/dev/null || true
    rm -rf "$SANDBOX_DIR" 2>/dev/null || true
}
