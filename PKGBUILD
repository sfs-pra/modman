# Maintainer: sfs <sfslinux@gmail.com>

pkgbase=modman
pkgname=('modman' 'modman-gui' 'modman-tui')
pkgver=2026.04
pkgrel=53
pkgdesc="Module manager for any frugal / live-CD Linux"
arch=('x86_64' 'aarch64')
url="http://mirror.yandex.ru/puppyrus/"
license=('MIT')
makedepends=('gcc' 'pkgconf' 'check' 'bats' 'shellcheck' 'gettext')

# Only flat (top-level) loose files in source[]. Sub-directory trees
# (systemd/, include/, src/, tests/, docs/) are staged into $srcdir by
# prepare() below so that makepkg variants that reject path-in-source
# entries still build correctly.
source=('modman'
        'modman-tui'
        'modman-selftest'
        'modman-update-check'
        'modman.conf'
        'modman.desktop'
        'modman-open.desktop'
        'modman-update-check.desktop'
        '40-modman.rules'
        'LICENSE')
sha256sums=('SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP')

prepare() {
    # Stage subdirectory trees into $srcdir. Two build modes:
    # 1. Out-of-tree (default): $srcdir is a fresh dir; cp subdirs into it.
    # 2. In-place / archroot: $srcdir IS $startdir/src/; the .c/.h files
    #    already live AT $srcdir/*.c (not $srcdir/src/*.c), so we expose
    #    them as $srcdir/src/*.c via symlinks so build()/check() find them
    #    at the hard-coded "src/" relative path.
    local srcdir_real startdir_real src_real f base
    srcdir_real="$(realpath "$srcdir" 2>/dev/null)"
    startdir_real="$(realpath "$startdir" 2>/dev/null)"

    for dir in systemd include src tests docs po data; do
        [ -d "$startdir/$dir" ] || continue
        src_real="$(realpath "$startdir/$dir" 2>/dev/null)"

        # In-place build: $startdir/$dir IS $srcdir → re-expose top-level
        # source files as $srcdir/$dir/<file> via symlinks so build paths
        # like "src/main.c" resolve. Currently only .c/.h are needed.
        if [ -n "$src_real" ] && [ "$src_real" = "$srcdir_real" ]; then
            mkdir -p "$srcdir/$dir"
            for f in "$srcdir"/*.c "$srcdir"/*.h; do
                [ -f "$f" ] || continue
                base="$(basename "$f")"
                ln -sfn "$f" "$srcdir/$dir/$base"
            done
            continue
        fi

        # Skip if already populated (re-prepare or earlier iteration)
        [ -d "$srcdir/$dir" ] && [ -n "$(ls -A "$srcdir/$dir" 2>/dev/null)" ] && continue

        cp -a "$startdir/$dir" "$srcdir/"
    done
}

build() {
    cd "$srcdir"

    local -a CFLAGS_EX=(
        -Wall
        -Wextra
        -O2
        -pipe
        -Iinclude
        "-DDEFAULT_MODMAN_BIN=\"/usr/bin/modman\""
        "-DDEFAULT_DOWNLOAD_DIR=\"/var/lib/modman/modules\""
        "-DMODMAN_LOCALEDIR=\"/usr/share/locale\""
    )

    gcc "${CFLAGS_EX[@]}" \
        src/main.c src/ui.c src/backend.c src/model.c src/validators.c src/capabilities.c src/ui_debug.c \
        $(pkg-config --cflags --libs gtk+-3.0 gio-2.0) \
        -o modman-gui

    gcc "${CFLAGS_EX[@]}" \
        src/open_main.c src/open_ui.c src/backend.c src/model.c src/validators.c \
        $(pkg-config --cflags --libs gtk+-3.0 gio-2.0) \
        -o modman-open

    local lang
    for lang in $(cat po/LINGUAS); do
        msgfmt "po/${lang}.po" -o "po/${lang}.mo"
    done
    for lang in $(cat po/modman-tui/LINGUAS); do
        msgfmt "po/modman-tui/${lang}.po" -o "po/modman-tui/${lang}.mo"
    done
}

check() {
    cd "$srcdir"

    bash -n modman modman-tui modman-selftest modman-update-check

    if command -v bats >/dev/null 2>&1; then
        bats tests/bats/modman-file-info.bats
        bats tests/bats/modman-open-contracts.bats
    else
        printf '%s\n' 'check(): skipping bats (tool missing in environment)'
    fi

    if command -v desktop-file-validate >/dev/null 2>&1; then
        desktop-file-validate modman-open.desktop
    else
        printf '%s\n' 'check(): skipping desktop-file-validate (tool missing in environment)'
    fi

    if command -v update-mime-database >/dev/null 2>&1; then
        local mime_root
        mime_root="$(mktemp -d)"
        mkdir -p "$mime_root/packages"
        cp data/mime/packages/modman-open.xml "$mime_root/packages/"
        update-mime-database -n "$mime_root"
        rm -rf "$mime_root"
    else
        printf '%s\n' 'check(): skipping update-mime-database (tool missing in environment)'
    fi
}

package_modman() {
    pkgdesc="CLI module manager backend for any frugal / live-CD Linux"
    depends=('bash' 'pfs-utils5>=2026.04')
    optdepends=('erofs-utils: erofs format support'
                'modman-gui: optional GTK3 graphical frontend'
                'modman-tui: optional dialog terminal frontend')
    conflicts=('pfs-utils' 'pfs-utils-cli<2026.04')
    provides=('modman')
    backup=('etc/modman.conf')

    cd "$srcdir"
    local lang

    install -dm755 "$pkgdir/usr/bin"
    install -dm755 "$pkgdir/etc"
    install -dm755 "$pkgdir/usr/share/man/man5"
    install -dm755 "$pkgdir/usr/share/man/man8"
    install -dm755 "$pkgdir/usr/share/man/ru/man5"
    install -dm755 "$pkgdir/usr/share/man/ru/man8"
    install -dm755 "$pkgdir/usr/share/licenses/$pkgname"

    install -dm755 "$pkgdir/etc/xdg/autostart"

    # Runtime state directories (owned by package, created on install)
    install -dm755 "$pkgdir/var/cache/modman"
    install -dm755 "$pkgdir/var/lib/modman/modules"

    install -m755 modman "$pkgdir/usr/bin/modman"
    install -m755 modman-selftest "$pkgdir/usr/bin/modman-selftest"
    install -m755 modman-update-check "$pkgdir/usr/bin/modman-update-check"

    install -m644 modman.conf "$pkgdir/etc/modman.conf"
    install -m644 modman-update-check.desktop "$pkgdir/etc/xdg/autostart/modman-update-check.desktop"
    install -m644 docs/modman.conf.5 "$pkgdir/usr/share/man/man5/modman.conf.5"
    install -m644 docs/modman.8 "$pkgdir/usr/share/man/man8/modman.8"
    install -m644 docs/ru/modman.conf.5 "$pkgdir/usr/share/man/ru/man5/modman.conf.5"
    install -m644 docs/ru/modman.8 "$pkgdir/usr/share/man/ru/man8/modman.8"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"

    for lang in $(cat "$srcdir/po/LINGUAS"); do
        install -Dm644 "$srcdir/po/${lang}.mo" \
            "$pkgdir/usr/share/locale/${lang}/LC_MESSAGES/modman.mo"
    done
}

package_modman-gui() {
    pkgdesc="GTK3 graphical frontend and file opener for modman"
    depends=('modman' 'gtk3' 'glib2')
    optdepends=('squashfs-tools: show SquashFS details in modman-open'
                'erofs-utils: show EROFS details in modman-open')

    cd "$srcdir"

    install -dm755 "$pkgdir/usr/bin"
    install -dm755 "$pkgdir/usr/share/applications"
    install -dm755 "$pkgdir/usr/share/polkit-1/rules.d"
    install -dm755 "$pkgdir/usr/share/mime/packages"
    install -dm755 "$pkgdir/usr/share/man/man1"
    install -dm755 "$pkgdir/usr/share/licenses/$pkgname"

    install -m755 modman-gui "$pkgdir/usr/bin/modman-gui"
    install -m755 modman-open "$pkgdir/usr/bin/modman-open"
    install -m644 modman.desktop "$pkgdir/usr/share/applications/modman.desktop"
    install -m644 modman-open.desktop "$pkgdir/usr/share/applications/modman-open.desktop"
    install -m644 data/mime/packages/modman-open.xml \
        "$pkgdir/usr/share/mime/packages/modman-open.xml"
    install -m644 docs/modman-open.1 "$pkgdir/usr/share/man/man1/modman-open.1"
    install -m644 40-modman.rules "$pkgdir/usr/share/polkit-1/rules.d/40-modman.rules"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}

package_modman-tui() {
    pkgdesc="Dialog terminal frontend for modman"
    depends=('bash' 'dialog' 'gettext' 'modman')
    optdepends=('squashfs-tools: show SquashFS compression details for local modules'
                'erofs-utils: show EROFS compression details for local modules')

    cd "$srcdir"

    install -dm755 "$pkgdir/usr/bin"
    install -dm755 "$pkgdir/usr/share/man/man8"
    install -dm755 "$pkgdir/usr/share/man/ru/man8"
    install -dm755 "$pkgdir/usr/share/licenses/$pkgname"

    install -m755 modman-tui "$pkgdir/usr/bin/modman-tui"
    install -m644 docs/modman-tui.8 "$pkgdir/usr/share/man/man8/modman-tui.8"
    install -m644 docs/ru/modman-tui.8 "$pkgdir/usr/share/man/ru/man8/modman-tui.8"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"

    local lang
    for lang in $(cat "$srcdir/po/modman-tui/LINGUAS"); do
        install -Dm644 "$srcdir/po/modman-tui/${lang}.mo" \
            "$pkgdir/usr/share/locale/${lang}/LC_MESSAGES/modman-tui.mo"
    done
}
