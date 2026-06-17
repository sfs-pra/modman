# PROJECT KNOWLEDGE BASE

**Generated:** 2026-04-21
**Commit:** (none - empty repo, all files untracked)
**Branch:** master

## OVERVIEW

Two co-located products for hot-attaching/detaching `.pfs` squashfs modules to a running AUFS root on any frugal / live-CD Linux install booted via [`mkinitcpio-rootaufs2`](https://wiki.puppyrus.org/soft/arch-initrd-rootaufs2). Both sit on top of [`pfs-utils` v4](https://wiki.puppyrus.org/puppyrus/pr218/pfs4) (`pfsload`, `pfsunload`, `pfsinfo`, …) — upstream: <https://github.com/pfs-utils/pfs-utils-cli/>.

1. **`sfs-get/`** — **current production backend**. Bash + gtkdialog + yad, 23 scripts. Packaged separately (own `.PKGINFO`, `pkgname=sfs-get`, `pkgver=2026.03-01`). Is already deployed in production. See [`sfs-get/AGENTS.md`](./sfs-get/AGENTS.md).
2. **`modman-gui.c` + `PKGBUILD`** — new GTK3 C rewrite under development; `PKGBUILD` declares `pkgname=modman` and would install `modman-gui` alongside a `modman` CLI script. **`modman` CLI source is absent from this tree**. Intended as a lighter, non-gtkdialog successor — effectively a replacement for the removed-from-builds `manager_pfs` that shipped with pfs-utils.

**Compatibility gap (important)**: `modman-gui.c` expects its backend to speak a `--machine` TSV protocol (see [CODE MAP](#code-map-modman-guic)) with flags `--list-loaded-after/before`, `--list-local`, `-Ss`, `-S`, `-Sy`. Grepped exhaustively: **`sfs-get` does NOT implement any of these** (no `--machine`, no tab-separated output). Running `modman-gui` today requires an adapter `modman` script that wraps `sfs-get` / pfs-utils queries into the TSV shape — otherwise all four tabs render empty.

## DESIGN INTENT (modman ≠ sfs-get copy)

`modman` is meant to be a **lighter, more universal successor** — not a line-by-line port. Two directions:

**Broaden initrd coverage**: besides `mkinitcpio-rootaufs2` (the default frugal initrd), also support [UIRD](https://wiki.puppyrus.org/soft/uird) (Unified Init Ram Disk, upstream: <https://github.com/neobht/uird>). UIRD uses different kernel cmdline (`uird.from=`, `uird.load=`, `uird.ro=`, `uird.changes=`, `uird.mode=`, `uird.config=*.ini`), stores runtime vars in `/etc/initvars` (read `SYSMNT`, `uird.from`), and supports extra module formats: `.xzm`, `.rom`, `.rom.enc`, `.rwm`, `.rwm.enc` alongside `.pfs`. Module layering uses alphabetical sort (`z*` = top). `sfs-get:21-29` has a partial UIRD branch — reference, not template.

**Keep from sfs-get** (essential):
- Hot load/unload of `.pfs` (+ broader `.xzm`/`.squashfs`) via `pfsload`/`pfsunload`
- Repo search with local override; update check (`mod-up` behavior: newer → download → `.old` rotation)
- Event hooks (`start.sh` / `stop.sh` inside modules, per pfs-utils convention)
- Initrd auto-detection for rootaufs2 **and** UIRD (see `sfs-get:18-37` branching)

**Drop from sfs-get** (obsolete / redundant — do NOT port):
- `*0` legacy file copies (`sfs-get0`, `open_pfs0`, `sfs_event_add0`, `*.list0`)
- `manager_pfs` / `loader_fs` reimplementations (upstream removed these)
- 11-field semicolon-backslash `sfs-*.list` catalog (replace with simpler metadata, e.g. TSV/JSON)
- Redundant UI stacks (gtkdialog + yad + ntf + `dmenu-sfs-*`) — modman keeps ONE (GTK3)
- `mmod` alternate manager, `dmenu-sfs-*` keybinding wrappers

**Take-away**: before porting any sfs-get behavior, check the Keep list. Target architecture is **Bash CLI backend (`modman`) exposing `--machine` TSV + initrd-abstraction layer → GTK3 GUI (`modman-gui`)**. Never mirror sfs-get's directory/file layout.

## DOMAIN CONTEXT

**Boot stack** ([`mkinitcpio-rootaufs2`](https://wiki.puppyrus.org/soft/arch-initrd-rootaufs2)): initrd builds root AUFS at `/run/archroot/root_ro` from `.pfs` squashfs modules in `<dir>/base`, `<dir>/modules`, `<dir>/optional` (Porteus-initrd-style layout, `dir=`/`changes=` kernel cmdline). These "loaded-before-user-session" modules form the system base layer.

**Runtime attachment** ([`pfs-utils` v4](https://wiki.puppyrus.org/puppyrus/pr218/pfs4)): after boot, `pfsload file.pfs` adds `.pfs` to top AUFS layer (or tmpfs copy with `-r`); `pfsunload file.pfs` removes it. `modman-gui` wraps both via `popen` — no library linkage, no kernel calls. The whole product is a thin UI over pfs-utils + `modman` Bash CLI.

**Tab semantics** (see `on_page_changed()`, `modman-gui.c:239`):
- **Подключенные** (`modman --list-loaded-after`): hot-attached modules added post-boot — safe to unload
- **Инет** (`modman -Ss <query>`): remote repo search overlaid with matching local `.pfs`
- **Локальные** (`modman --list-local` or `ls DOWNLOAD_DIR/*.pfs`): downloaded `.pfs` not yet attached
- **Система** (`modman --list-loaded-before`): base modules loaded by initrd — unloading here mutates the running system; treat with care

**Config paths (`modman.conf`) — don't rename blindly, they match upstream conventions**:
- `AUFS_INITRD_PREFIX="/run/archroot/root_ro"` — `mkinitcpio-rootaufs2` RO root mount point (`mount -o remount,ro /run/archroot/root_ro` per wiki)
- `AUFS_SYSTEM_PATH="/mnt/."` — pfs-utils `prefixmp=` variable; individual modules end up at `/mnt/.<modname>`
- `CMD_LOAD`/`CMD_UNLOAD` — indirection point, backend may swap `pfsload`↔`pfsramload` or overlayfs variants

## STRUCTURE

```
/workspace/
├── modman-gui.c      # 685-line C/GTK3 GUI (new rewrite, WIP)
├── modman.conf       # Shell-sourced config (used by the missing `modman` CLI, NOT by GUI)
├── modman.desktop    # XDG launcher → Exec=modman-gui
├── PKGBUILD          # Arch build recipe for `modman`; declares `modman` CLI source (absent)
├── sfs-get/          # Production backend (own pkg, 23 scripts); see sfs-get/AGENTS.md
│   ├── usr/local/bin/              # 23 bash/sh scripts (sfs-get, mnt_sfs, open_pfs, …)
│   ├── usr/local/share/applications/  # desktop launchers (sfs-get, mnt_sfs)
│   ├── etc/sfs-get/                # repo config: www.list, sfs.list, links/, list/
│   └── .PKGINFO                    # package metadata (separate from root PKGBUILD)
└── pfs-utils/        # READ-ONLY UPSTREAM REFERENCE — do not edit
    ├── usr/local/bin/              # 25 sh/bash scripts (pfsload, pfsunload, pfsinfo, pfs, mkpfs, …)
    ├── usr/share/man/{man8,ru/man8}/pfs-utils.8  # authoritative man pages (EN + RU)
    └── .PKGINFO                    # pkgname=pfs-utils-cli, pkgver=2026.03-07
```

`.ocp/` is agent state — ignore for product work. `pfs-utils/` is the upstream package snapshot (<https://github.com/pfs-utils/pfs-utils-cli/>) included here for reference; modman/sfs-get invoke its binaries at runtime but do NOT ship them.

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| New GUI behavior / event handlers | `modman-gui.c` | Single-file C/GTK3 rewrite |
| Compile flags / install paths for new GUI | `PKGBUILD` build() line 21 | Hardcodes `MODMAN_BIN="/usr/bin/modman"`, `DOWNLOAD_DIR` via `-D` |
| Current production backend (bash) | `sfs-get/usr/local/bin/` | 23 scripts; main entry `sfs-get` (844 lines). See `sfs-get/AGENTS.md` |
| Current backend config | `sfs-get/etc/sfs-get/` | `www.list`, `sfs.list`, `links/`, structured `list/sfs-*.list` |
| Legacy `modman` CLI source | (absent) | `PKGBUILD:12` lists it but no source in tree; see Compatibility gap above |
| `modman.conf` | root `modman.conf` | Only referenced by the absent `modman` CLI; GUI uses compile-time defines instead |
| How `pfsload`/`pfsunload` actually work | `pfs-utils/usr/local/bin/{pfsload,pfsunload,pfs}` | Upstream source — `pfs` (436L) is the function library with `mkaufs`/`addlayer`/`pfs_update_caches` |
| `.pfs` format + AUFS layer semantics | `pfs-utils/usr/share/man/man8/pfs-utils.8` | Authoritative man page (RU copy under `ru/man8/`) |

## CODE MAP (modman-gui.c)

| Symbol | Line | Role |
|--------|------|------|
| `AppWidgets` struct | 21 | Global UI state, accessed via file-scope `widgets` singleton |
| `enum COL_*` | 36 | 6-col store; only `NUM_COLS - 2 = 4` are rendered (size/is_local hidden) |
| `parse_size_mb()` | 47 | Parses "1.5G"/"200M"/"500K" → MB double for sort |
| `load_data()` | 63 | `popen("$MODMAN_BIN <cmd> --machine")` → parse TSV |
| `load_local_files()` | 107 | `ls "$DOWNLOAD_DIR"/*.pfs` → append with `is_local=TRUE` |
| `on_page_changed()` | 239 | Tab router: 0=Подключенные, 1=Инет, 2=Локальные, 3=Система |
| `on_action_clicked()` | 324 | Runs `pfsload`/`pfsunload`/`modman -S` per active tab |
| `compare_local_first()` | 499 | Sort: `is_local=TRUE` rows float to top |
| `cell_data_func_background()` | 527 | Paints local rows `#d4edda` (light green) |
| `activate()` | 548 | Builds window; initial load triggers **page 3** logic (line 673) despite default page 0 |
| `main()` | 678 | `GtkApplication` id: `org.puppyrus.modman` |

## CONVENTIONS

No lint/format/hooks enforced (no configs present).

Observed style (match when editing):
- 4-space indent, snake_case for functions, `UPPER_SNAKE` for enums/macros
- Russian comments and UI strings throughout - **preserve language** when editing
- Static file-scope functions for handlers; forward-declare when ordering requires (see line 60)
- `(void)param;` to silence unused-param warnings in GTK callbacks

## ANTI-PATTERNS (THIS PROJECT)

- **Hardcoded paths in code** — `modman-gui.c:483` uses literal `/home/ai/modman/modules/*.old` instead of `DOWNLOAD_DIR`. Comment on that line (`Путь заглушка, лучше читать из конфига`) flags it. Do not replicate this; use the `DOWNLOAD_DIR` macro.
- **Do not call `system()` for new user-visible actions** — existing uses (line 483, 493) skip error capture. New work must use `popen` + `pclose` exit-code check like `on_action_clicked()`.

## UNIQUE STYLES

- **Thin-GUI architecture**: all data ops shell out via `popen`/`system` to `modman`, `pfsload`, `pfsunload`. No library linkage to backend.
- **TSV `--machine` protocol**: GUI expects `modman <cmd> --machine` to print tab-separated `name<TAB>layer<TAB>path<TAB>desc`. Preserve on backend work.
- **Compile-time path injection**: `MODMAN_BIN` and `DOWNLOAD_DIR` come from `-D` flags in `PKGBUILD:21`, not runtime config. Changing default paths requires rebuild.
- **Hidden sort column**: `COL_SIZE_NUM` (double) hidden from view, used by column-1 sort via `gtk_tree_view_column_set_sort_column_id(columns[1], COL_SIZE_NUM)` (line 616) so MB sorts numerically while the visible cell shows "1.5G".
- **Dual signal connection**: `switch-page` connects BOTH `on_tab_switched` (clears search/status) AND `on_page_changed` (reloads data) - order-dependent (line 581-582).
- **Two-phase load on tab 1 ("Инет")**: `load_data("-Ss ...")` first clears+populates remote results, then `load_local_files()` appends local matches; `is_module_in_store()` dedupes.

## COMMANDS

```bash
# Build (direct, matches PKGBUILD build())
gcc -Wall -O2 \
  -DMODMAN_BIN=\"/usr/bin/modman\" \
  -DDOWNLOAD_DIR=\"/home/ai/modman/modules\" \
  modman-gui.c -o modman-gui \
  $(pkg-config --cflags --libs gtk+-3.0)

# Arch packaging workflow
makepkg -si

# Run (requires modman + pfsload/pfsunload on PATH)
./modman-gui
```

No test / lint / format commands exist. Build deps: `gcc`, `pkgconf`, `gtk3`.

## NOTES

- **Two products, one repo**: `sfs-get/` is the functioning backend shipping today; `modman-gui.c` + `PKGBUILD` is a parallel rewrite that cannot run yet (missing `modman` CLI, see Compatibility gap in OVERVIEW). Keep work scoped to the right product — do not cross-edit without reason.
- **Runtime deps not checked at build**: GUI calls `pfsload`, `pfsunload`, `ls`, `xargs`, `basename`, `rm` via shell. Missing tools → empty lists or silent failures.
- **Startup quirk**: notebook defaults to page 0 ("Подключенные"), but `activate()` calls `on_page_changed(..., 3, NULL)` to force a system-modules load first. Page 0 handler then re-fires when user clicks the tab. Intentional — do not "fix".
- **Memory**: `widgets->last_error` is a `GString*` that may be NULL; always check before use (see `on_status_clicked`). `widgets` itself is `g_free`'d once in `main`, but its children are not individually freed (GTK destroys the widget tree; acceptable).
- **Empty git repo**: `master` has no commits. First commit should include all 4 root files + `sfs-get/` tree, plus a `.gitignore` for the compiled `modman-gui` binary.
