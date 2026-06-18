# pfs-utils v5 Integration Contract

**Version**: 2026.04-01 (CalVer)
**Status**: Authoritative spec — all downstream implementation tasks reference this document.
**Scope**: pfs-utils v5 binaries, modman bash CLI, modman-gui C frontend.

Three-tier call chain:

```text
pfs-utils v5  (upstream, backward-compat with v4)
    called by
modman (bash)  — pacman/apt-style CLI, --machine TSV protocol
    called by
modman-gui (C/GTK3)  — thin frontend, popen() only
```

---

## v4-compat verbs (unchanged)

All v4 verbs remain functional. Exit codes and `pfsinfo` stdout are byte-exact with v4. Stderr may gain additional diagnostic strings without breaking callers.

### pfsload

**Purpose**: AUFS-only. Mount a squashfs/erofs/ext2/ext3/ext4 file, block device, or directory into the AUFS root.

**Synopsis**:
```text
pfsload [OPTIONS] FILESYSTEM
```

**Options**:

| Flag | Long form | Effect |
|------|-----------|--------|
| `-u` | `--upper` | Mount to upper AUFS layer (default) |
| `-l` | `--lower` | Mount to lower AUFS layer |
| `-n` | `--no-update` | Skip cache updates (ldconfig, icon cache, etc.) |
| `-r` | `--toram` | Copy module to tmpfs before loading |
| `--ro` | `--read-only` | Mount read-only |

**Stdout**: `need: <depname>` lines for each unmet dependency (from `pfs.depends` inside module). Empty if no deps.

**Stderr**: Error messages on failure. May include diagnostic strings in v5 (e.g., deprecation warnings).

**Exit codes**:

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Usage / no file given |
| 2 | File/object not found |
| 3 | Not enough RAM (--toram) OR unknown filesystem |
| 4 | Kernel does not support PFS (no squashfs/aufs in `/proc/filesystems`) |
| 5 | Unsupported FS type for directory input OR AUFS remount error |
| 6 | Already mounted |
| 7 | No free loop devices |

**Example**:
```bash
pfsload /mnt/data/firefox.pfs
pfsload --toram /mnt/data/libreoffice.pfs
pfsload --lower /mnt/data/base-layer.pfs
```

---

### pfsunload

**Purpose**: AUFS-only. Remove a module from the AUFS root and unmount it.

**Synopsis**:
```text
pfsunload [OPTIONS] FILESYSTEM
```

**Options**:

| Flag | Long form | Effect |
|------|-----------|--------|
| `-n` | `--no-update` | Skip cache updates |
| `-s` | `--saveram` | Do not remove module from RAM after unmounting |
| `-v` | | Verbose: list open files from the module |

**Stdout**: On verbose (`-v`), prints open files from the module. Empty otherwise.

**Stderr**: `Object "<name>" not mounted.` if not found. Error messages on failure.

**Exit codes**:

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Invalid option |
| 2 | Usage / no file given |
| 3 | Kernel does not support PFS |
| 22 | Object not mounted (POSIX EINVAL) |
| other | Unmount failure (propagated from `umount`) |

**Example**:
```bash
pfsunload firefox.pfs
pfsunload -v libreoffice.pfs
```

---

### pfsinfo

**Purpose**: List packages/submodules inside a `.pfs` file, or list currently mounted/installed modules.

**Synopsis**:
```text
pfsinfo [OPTIONS] [FILE]
```

**Options**:

| Flag | Long form | Effect |
|------|-----------|--------|
| `-d` | `--delimeter` | Set delimiter character (default `/`) |
| `-s` | `--stat` | Show compression stats, size, dependencies |
| `-m` | `--mount` | List mounted submodules only |
| `-i` | `--install` | List installed submodules only |

**Stdout (no FILE arg)**: Newline-separated list of module names from `/etc/packages/mount/` and/or `/etc/packages/install/`. Sorted case-insensitively.

**Stdout (with FILE, no --stat)**: Newline-separated list of submodule names inside the squashfs, using the delimiter character.

**Stdout (with FILE, --stat)**: Human-readable block: `Dependenses:`, `Modules:`, `Compression algorithm:`, `Module size:`, `Uncompressed size:`, `Compression ratio:`.

**Stderr**: `File "<name>" not found!` if file missing. `File "<name>" is not PFS!` if not squashfs v4.

**Exit codes**:

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Invalid option |
| 13 | pfs library not found |

**Note**: v4's `pfsinfo` has NO `--machine` flag. The `--machine` TSV output is v5-new. See [v5-new verbs](#v5-new-verbs).

---

### mkpfs

**Purpose**: Create a `.pfs` squashfs module from directories or merge existing modules.

**Synopsis**:
```text
mkpfs <sources...> -o output.pfs
mkpfs -d <dir>     # directories only
mkpfs -m <dir>     # modules only
```

**Options**:

| Flag | Effect |
|------|--------|
| `-o` / `--out-file` | Output file path |
| `-w` | Exclude AUFS whiteout files |
| `-l` / `--local` | Build in-place without AUFS |
| `-f` / `--fast` | Fast compression (lz4 instead of xz) |
| `--mklist` | Add file list to simple (non-container) module |
| `--mksqfs <args>` | Pass extra args to mksquashfs (must be last) |

**Stdout**: mksquashfs progress output.

**Stderr**: Error messages on failure.

**Exit codes**:

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | No sources found / usage error |

---

### pfsextract

**Purpose**: Extract packages from a module or install packages into the system.

**Synopsis**:
```text
pfsextract <module.pfs> [package-name]
```

**Note**: Out of v5 scope. No changes planned. Defer to v5.1.

---

### pfsfind

**Purpose**: Find files across loaded modules.

**Note**: Out of v5 scope. No changes planned. Defer to v5.1.

---

### mountfile

**Purpose**: Mount a filesystem image (ext2/3/4, squashfs, ISO 9660) to a path.

**Synopsis**:
```text
mountfile <file> [mount-point]
```

**Stdout**: Empty on success.

**Stderr**: `Filesystem not supported!` if type unrecognized. `File "<name>" is mounted!` if already mounted.

**Exit codes**: Propagated from `mount`. 0 = success.

**v5 change**: erofs support added via `fs_type()` extension. Behavior otherwise identical.

---

### umountfile

**Purpose**: Unmount a previously mounted image.

**Synopsis**:
```text
umountfile <file-or-mount-point>
```

**Exit codes**: Propagated from `umount`. 0 = success.

---

### pfs (library / dispatcher)

**Purpose**: Shared function library sourced by all pfs-utils scripts. Also callable directly as a dispatcher.

**Synopsis**:
```text
pfs <function-name> [args...]
# OR
. $(which pfs) ; <function-name> [args...]
```

**Key internal functions** (not part of the public CLI contract, but referenced by v5 new verbs):

| Function | Role |
|----------|------|
| `addlayer` | Add AUFS layer; v5 replaces with `addlayer_dispatch` |
| `fs_type` | Detect filesystem type; v5 adds erofs |
| `pfs_update_caches` | Refresh ldconfig, icon cache, etc. after load/unload |
| `checkdeps` | Check module dependencies against loaded modules |
| `mklist` | Build canonical `pfs.files` / `pfs.dirs.empty` / `pfs.specs` mount indexes and legacy compatibility mirrors |
| `mksqmod` | Wrapper around mksquashfs |
| `mkaufs` | Create a new AUFS session |
| `delaufs` / `delaufs3` / `delaufsl` | Remove AUFS sessions |
| `pfsramfree` | Clean up tmpfs copies of modules |
| `allow_only_root` | Abort if not root |
| `exitmsg` | Print colored error and exit |

**v5 new functions** added to this library: `_detect_initrd`, `addlayer_dispatch`, `addlayer_aufs`, `addlayer_overlay`. See [v5-new verbs](#v5-new-verbs).

---

### pfsrebuild

**Purpose**: Rebuild an AUFS-loaded module with current running-root changes, or rebuild an installed/offline registry entry from its recorded file manifest.

Runtime-root rebuild is AUFS-only: it depends on a system-wide hot-loaded root view. OverlayFS roots do not support that view because post-boot hot attach is refused. For OverlayFS, use build/offline workflows such as `mkpfs` or `chroot2pfs`, or run modules privately with `pfsrun`.

Installed/offline registry rebuild remains separate from the runtime hot-load path and does not imply OverlayFS system-wide hot attach support.

---

### sync2layer

**Purpose**: Create a new AUFS layer and move changes from the top RW layer into it.

**Note**: No changes in v5 scope.

---

### pfsuninstall

**Purpose**: Remove package files from the system (installed via pfsextract).

**Note**: No changes in v5 scope.

---

## v5-new verbs

All v5-new verbs are additive. They do not replace v4 verbs. Callers that only use v4 verbs are unaffected.

---

### pfs --detect-initrd

**Purpose**: Detect which initrd/boot stack is running. Used by modman to select the correct layering and path strategy.

**Synopsis**:
```
pfs --detect-initrd
```

**Stdout**: Exactly one of the following strings, followed by a newline:

| Value | Condition |
|-------|-----------|
| `rootaufs2` | `/sys/fs/aufs/` exists AND `/proc/cmdline` contains `dir=` or `changes=` |
| `uird` | `/etc/initvars` exists (UIRD sets this at boot) |
| `livekit` | `/run/initramfs/livekit` exists |
| `porteus` | `/mnt/live/memory/` exists AND not rootaufs2 |
| `unknown` | None of the above match |

**Stderr**: Empty.

**Exit codes**: Always 0. Detection failure is not an error; callers handle `unknown`.

**Example**:
```bash
initrd=$(pfs --detect-initrd)
# initrd = "rootaufs2"
```

---

### pfs --layering-mode

**Purpose**: Report which layering backend is active. Respects the `$PFS_LAYERING` environment override.

**Synopsis**:
```
pfs --layering-mode
```

**Stdout**: Exactly one of:

| Value | Condition |
|-------|-----------|
| `aufs` | `$PFS_LAYERING=aufs` OR AUFS available in `/proc/filesystems` AND `$PFS_LAYERING` unset |
| `overlay` | `$PFS_LAYERING=overlay` OR overlay available AND AUFS not available |

**Stderr**: Empty.

**Exit codes**: Always 0.

**Example**:
```bash
PFS_LAYERING=overlay pfs --layering-mode
# overlay
```

---

### OverlayFS runtime execution

**Purpose**: Define the supported runtime path for OverlayFS roots.

AUFS-only: system-wide hot attach/detach is not supported on OverlayFS roots; use `pfsrun` for OverlayFS runtime execution. `pfsexec` may be used as the lower-level namespace helper behind `pfsrun`, but `pfsrun` is the primary user-facing command.

**Synopsis**:
```bash
pfsrun MODULE.pfs [COMMAND [ARGS...]]
pfsexec MODULE.pfs COMMAND [ARGS...]
```

`pfsload` and `pfsunload` remain the AUFS system-wide hot attach/detach tools. OverlayFS boot-time layering, `mkpfs` or `chroot2pfs` build-time OverlayFS support, and private namespace execution don't imply root remount insertion or hot-unload support.

**Example**:
```bash
pfsrun firefox.pfs
pfsrun firefox.pfs --new-instance
```

---

### mkpfs-erofs

**Purpose**: Create an erofs module from a directory. Wraps `mkfs.erofs`.

**Synopsis**:
```
mkpfs-erofs <source-dir> <output.erofs>
mkpfs-erofs [--compress <algo>] <source-dir> <output.erofs>
```

**Options**:

| Flag | Default | Effect |
|------|---------|--------|
| `--compress` | `lz4hc` | Compression algorithm passed to mkfs.erofs |

**Stdout**: mkfs.erofs progress output.

**Stderr**: Error messages. `mkfs.erofs not found — install erofs-utils` if binary missing.

**Exit codes**:

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Usage error / missing args |
| 2 | Source directory not found |
| 3 | mkfs.erofs not found (erofs-utils not installed) |
| other | Propagated from mkfs.erofs |

**Runtime dependency**: `erofs-utils` (declared as `optdepend` in PKGBUILD — not hard required).

**Example**:
```bash
mkpfs-erofs ./firefox-rootfs firefox.erofs
mkpfs-erofs --compress zstd ./libreoffice-rootfs libreoffice.erofs
```

---

### pfsinfo --machine

**Purpose**: Machine-readable TSV listing of currently loaded or installed modules. Used by modman to populate `--list-loaded-after`, `--list-loaded-before`, `--list-local` output.

**Synopsis**:
```
pfsinfo --machine [--mount | --install]
```

**This verb is v5-NEW. v4's pfsinfo has no `--machine` flag.**

**Stdout format**: One module per line, fields separated by TAB (`\t`), terminated by `\n`. UTF-8 encoding.

**Field order** (6 fields):

| Index | Name | Type | Description |
|-------|------|------|-------------|
| 0 | `name` | string | Module name (basename without extension) |
| 1 | `layer` | string | Layer identifier or size string (e.g., `1.5G`, `200M`, `500K`) |
| 2 | `path` | string | Absolute path to the `.pfs` / `.erofs` file or mount point |
| 3 | `desc` | string | Human-readable description (from `pfs.specs` or empty string) |
| 4 | `category` | string | Category tag (from `pfs.specs` or empty string) — **v5 new** |
| 5 | `version` | string | Module version string (from `pfs.specs` or empty string) — **v5 new** |

**Backward compatibility**: Clients that read only the first 4 fields (like the current modman-gui `load_data()` parser at line 81: `g_strv_length(parts) >= 4`) continue to work. Extra fields are ignored.

**Escape rules** (apply to all field values):

| Sequence | Meaning |
|----------|---------|
| `\t` | Literal TAB character in value |
| `\n` | Literal newline character in value |
| `\\` | Literal backslash in value |

**Empty fields**: Represented as empty string between TABs (e.g., `name\tlayer\tpath\t\t\t\n` for empty desc/category/version).

**Example output**:
```
firefox	1.2G	/mnt/.firefox.pfs	Mozilla Firefox browser	browsers	115.0
libreoffice	450M	/mnt/.libreoffice.pfs	LibreOffice suite	office	
base-system	2.1G	/mnt/live/memory/bundles/base-system.pfs		system	
```

**Example call**:
```bash
pfsinfo --machine --mount
pfsinfo --machine --install
```

---

### Mount index identity and layout

**Purpose**: Define the on-disk source of truth for mounted-module
metadata and the stable identity form shared by lookup tools.

**Canonical layout**: The v5 mount index source of truth is:

```text
/var/lib/pfs/mount/<container-module>/<submodule>/
```

Each canonical index directory contains `pfs.files`, `pfs.dirs.empty`,
`pfs.specs`, and may contain `pfs.depends`. Single-module builds use the
default identity `<module>/<module>`, so a simple `firefox.pfs` writes
`/var/lib/pfs/mount/firefox/firefox/` unless a caller passes a different
container name.

**Identity tuple**: A mounted submodule is identified by
`(container-module, submodule)`. User-facing display and lookup use
`container:submodule` when the container is known. Tools may accept an
unqualified `submodule` only when it resolves to one canonical match or to a
legacy-only index.

**Duplicate names**: Duplicate submodule names are valid across containers,
for example `container-a:common` and `container-b:common`. In that case
callers must use the qualified `container:submodule` form. An unqualified
duplicate must fail as ambiguous and must not silently choose an arbitrary
match.

**Legacy compatibility**: v5 still mirrors and reads the v4 flat compatibility path:

```text
/etc/packages/mount/<submodule>/
```

The legacy path is a compatibility mirror and fallback only. It can't safely
disambiguate duplicate canonical submodule names because it stores only
`<submodule>` and has no container component. Legacy-only entries may still
appear as unqualified names, but new metadata should use the canonical
two-level layout.

**Current lookup behavior**: `pfsinfo`, `pfsextract`, `pfsfind`,
`pfsfindlibs`, `pfsuninstall`, `pfsrebuild`, and `pfsdepends` consume the
canonical layout first and keep the legacy fallback for old indexes. `mklist`
writes both canonical indexes and legacy mirrors so current tools and v4-era
consumers can coexist.

**Migration bridge**: `pfsmigrate-mount` copies legacy flat mount indexes from
`/etc/packages/mount/<submodule>/` into the canonical
`/var/lib/pfs/mount/legacy/<submodule>/` provenance namespace. Migrated entries
use `legacy:<submodule>` as their qualified identity. The command keeps the
legacy source directories in place for fallback readers, is idempotent when the
destination already matches the source, and reports a non-destructive conflict
when destination content differs. Legacy flat paths still cannot disambiguate
duplicate submodule names and must not become the canonical source of truth.

---

### modman ↔ pfs-utils v5 boundary

`modman` is a CLI client of pfs-utils v5. It MUST consume the public process boundary only; it MUST NOT source internal `pfs` shell functions.

Required contract points:

| modman flow | Required pfs-utils boundary | Notes |
|-------------|-----------------------------|-------|
| `--list-loaded-before` / `--list-loaded-after` | `pfs --detect-initrd` | `modman` uses this as the single source of truth for boot-stack detection before applying before/after filters. |
| `--list-loaded-before` / `--list-loaded-after` | `pfs --layering-mode` | `modman` MUST not reinterpret `$PFS_LAYERING`; it passes the environment through unchanged and consumes the reported mode (`aufs` / `overlay`). |
| loaded-module inventory | `pfsinfo --machine --mount` | Preferred v5 inventory surface for both AUFS and OverlayFS. A legacy AUFS-only fallback is acceptable only for mixed deployments where `pfsinfo --machine` is still unavailable. |
| `-S` / `install` | `$CMD_LOAD` (default `pfsload`) | AUFS-only system-wide hot attach. `modman` keeps its CLI stable and delegates the actual load operation to the configured backend command. |
| `-R` / `remove` | `$CMD_UNLOAD` (default `pfsunload`) | AUFS-only system-wide hot detach. `modman` does not change `$PFS_LAYERING`; load/unload helpers observe the caller environment directly. |

Failure contract:

- If `pfs --detect-initrd` returns `unknown`, `modman --list-loaded-before` and `modman --list-loaded-after` MUST fail with a clear diagnostic instead of guessing a base-layer split.
- When layering mode is `overlay`, `modman` MUST NOT claim post-boot system-wide hot attach/detach support. It should direct users to `pfsrun` for OverlayFS runtime execution.

---

### addlayer_dispatch (internal library function)

**Purpose**: v5 replacement dispatch point for `addlayer`. Selects AUFS layer insertion or private OverlayFS namespace setup based on `$PFS_LAYERING` and runtime detection.

**Not a CLI verb.** Called internally by pfs-utils runtime helpers. Documented here because modman and pfs-utils depend on its AUFS-only hot attach boundary.

**Deprecation warning**: Direct calls to `addlayer()` from external scripts emit a deprecation warning to stderr (once per run, gated by `$XDG_RUNTIME_DIR/pfs-utils.deprecations`). See [Deprecation map](#deprecation-map).

---

## Deprecation map

| v4 verb / function | v5 equivalent | Deprecation behavior |
|--------------------|---------------|----------------------|
| `addlayer <N> <path>` (direct call) | `addlayer_dispatch <path>` | Stderr warning once per run; still works |
| `pfsinfo` (no `--machine`) | `pfsinfo --machine` for machine consumers | v4 form still works; `--machine` is additive |
| `pfsload` (no explicit mode) | `pfsload` on AUFS roots, `pfsrun` on OverlayFS roots | v4 form still works for AUFS hot attach; OverlayFS runtime execution is private namespace only |
| `fs_type()` squashfs-only | `fs_type()` with erofs branch | Transparent; callers unaffected |

**Deprecation warning format** (stderr):
```
pfs-utils: DEPRECATED: direct addlayer() call from <caller>. Use addlayer_dispatch(). See pfs-utils.8.
```

**Once-per-run gate**: The warning is written to stderr only if `$XDG_RUNTIME_DIR/pfs-utils.deprecations` does NOT already contain the caller's name. After warning, the caller name is appended to that file. File is not created if `$XDG_RUNTIME_DIR` is unset (warning always emits in that case).

---

## TSV output shapes

This section is the byte-level contract for all `--machine` output. modman-gui's `load_data()` (modman-gui.c:63-104) parses this format directly.

### General rules

- **Separator**: TAB character (`0x09`)
- **Line terminator**: LF (`0x0A`). CRLF is NOT produced; callers strip both (`strcspn(line, "\r\n")` at modman-gui.c:78).
- **Encoding**: UTF-8
- **Line buffer**: modman-gui allocates 2048 bytes per line (modman-gui.c:75). Lines MUST NOT exceed 2047 bytes including the terminating `\n`.
- **Minimum fields**: 4 (name, layer, path, desc). Clients check `g_strv_length(parts) >= 4` and skip lines with fewer fields.
- **Extra fields**: Allowed. Clients that only read 4 fields ignore fields 4+.
- **Empty lines**: Skipped by callers. Do not emit blank lines.
- **Comments**: Not supported. Do not emit `#` lines.

### pfsinfo --machine field spec

```
<name> TAB <layer> TAB <path> TAB <desc> TAB <category> TAB <version> LF
```

| Field | Max length | Notes |
|-------|-----------|-------|
| name | 255 bytes | Module basename, no extension |
| layer | 64 bytes | Layer ID or human size string (e.g., `1.5G`) |
| path | 4096 bytes | Absolute path |
| desc | 512 bytes | May be empty |
| category | 64 bytes | May be empty |
| version | 64 bytes | May be empty |

### modman --machine field spec (consumed by modman-gui)

modman wraps pfsinfo output and may synthesize rows for local files.

For metadata search (`modman -Ss <query> --machine`), the CLI emits **5 fields**:

```
<name> TAB <layer> TAB <path> TAB <desc> TAB <category> LF
```

`category` may be empty for legacy cache rows that do not carry category metadata.
Parsers must accept both 4-field legacy rows and 5-field rows (extra trailing fields are optional and must be ignored by 4-field clients).

For the `--list-local` command, `layer` contains the file size string (parsed by `parse_size_mb()` at modman-gui.c:47 for numeric sort). Accepted size formats:

| Format | Example | Parsed as |
|--------|---------|-----------|
| `<N>G` | `1.5G` | N * 1024 MB |
| `<N>M` | `200M` | N MB |
| `<N>K` | `500K` | N / 1024 MB |
| `<N>` | `1048576` | N / 1024 MB (bytes assumed) |

### Escape encoding

Applied to field values before emission. Applied in this order:

1. Replace `\` with `\\`
2. Replace TAB with `\t`
3. Replace LF with `\n`

Decoding (consumer side): reverse order.

---

## Exit codes

Consolidated table across all pfs-utils binaries.

| Code | Symbolic | Used by | Meaning |
|------|----------|---------|---------|
| 0 | `SUCCESS` | all | Operation completed successfully |
| 1 | `ERR_USAGE` | pfsload, pfsunload, mkpfs, mkpfs-erofs, pfsinfo | Usage error or no arguments |
| 2 | `ERR_NOTFOUND` | pfsload, pfsunload, mkpfs-erofs | File/object not found |
| 3 | `ERR_NOSPACE` | pfsload | Not enough RAM (--toram) |
| 3 | `ERR_FSTYPE` | pfsload | Unknown filesystem type |
| 3 | `ERR_NOEROFS` | mkpfs-erofs | mkfs.erofs not installed |
| 4 | `ERR_NOKERNEL` | pfsload, pfsunload | Kernel lacks squashfs/aufs support |
| 5 | `ERR_AUFS` | pfsload | AUFS remount failed OR unsupported FS for dir input |
| 6 | `ERR_MOUNTED` | pfsload | Already mounted |
| 7 | `ERR_NOLOOP` | pfsload | No free loop devices |
| 8 | `ERR_BADLAYER` | pfsload | Unknown `-L` value (v5 new) |
| 13 | `ERR_NOPFS` | pfsinfo | pfs library not found |
| 22 | `ERR_NOTMOUNTED` | pfsunload | Object not mounted (POSIX EINVAL) |

**Note**: Codes 3 and 5 are reused across different error conditions in different binaries. Callers must check which binary produced the exit code.

---

## Env vars

Environment variables that affect pfs-utils v5 behavior.

| Variable | Default | Scope | Effect |
|----------|---------|-------|--------|
| `PFS_LAYERING` | (unset) | pfsload, pfs library | Force backend selection: `aufs` for AUFS hot attach, `overlay` only for private namespace and build-time helpers. Overrides auto-detection. |
| `PFS_DEBUG` | (unset) | all | If set to any non-empty value, enables verbose debug output to stderr. |
| `AUFS_INITRD_PREFIX` | `/run/archroot/live/memory/images` | modman, pfs library | Per-initrd path prefix to the directory holding base (pre-boot) modules. For `mkinitcpio-rootaufs2` this is `/run/archroot/live/memory/images`; for UIRD/livekit/porteus the equivalent path is auto-derived from `SYSMNT`. modman's `_is_base_module` matches `bundle == ${AUFS_INITRD_PREFIX}*` to classify --list-loaded-before vs --list-loaded-after. |
| `SYSMNT` | (set by initrd) | pfs library | System mount prefix. Set by `mkinitcpio-rootaufs2` or read from `/etc/initvars` (UIRD). Used as prefix for `bundles/`, `changes/`, `aufs*` paths. |
| `PFSDIR_MOUNT_NEW` | `/var/lib/pfs/mount` | pfs library, tests | Canonical mount index root for `<container-module>/<submodule>/` entries. |
| `PFSDIR_MOUNT_LEGACY` | `/etc/packages/mount` | pfs library, tests | Legacy flat mount index root for `<submodule>/` fallback entries. |
| `XDG_RUNTIME_DIR` | (set by PAM/systemd) | pfs library | Directory for the deprecation gate file `pfs-utils.deprecations`. If unset, deprecation warnings always emit. |

**Config file**: `/etc/pfs.cfg` — sourced by the `pfs` library if present. Can set any of the above variables. Takes effect before argument parsing.

**modman.conf** (`/etc/modman.conf` or path from compile-time `-DMODMAN_CONF`): Shell-sourced by modman. Relevant variables:

| Variable | Default | Effect |
|----------|---------|--------|
| `AUFS_INITRD_PREFIX` | `/run/archroot/live/memory/images` | Passed through to pfs-utils calls |
| `AUFS_SYSTEM_PATH` | `/mnt/.` | pfs-utils `prefixmp` — individual modules mount at `<AUFS_SYSTEM_PATH><modname>` |
| `CMD_LOAD` | `pfsload` | Indirection for AUFS hot attach helpers without code change |
| `CMD_UNLOAD` | `pfsunload` | Indirection: swap to alternate unload command |
| `DOWNLOAD_DIR` | (compile-time `-DDOWNLOAD_DIR`) | Where modman downloads `.pfs` files; also used by modman-gui for local file listing |

---

## Sibling binaries

v4 binaries are installed alongside v5 under versioned names for rollback. They are NOT the default; the unversioned names (`pfsload`, `pfsunload`, `pfs`) point to v5.

| Sibling binary | Source | Purpose |
|----------------|--------|---------|
| `pfsload-v4` | v4 `pfsload` | Rollback: invoke v4 load behavior explicitly |
| `pfsunload-v4` | v4 `pfsunload` | Rollback: invoke v4 unload behavior explicitly |
| `pfs-v4` | v4 `pfs` library | Rollback: source v4 library functions |

**Install layout** (all under `usr/bin/`):

```
pfsload        -> v5 (default)
pfsload-v4     -> v4 copy (rollback)
pfsunload      -> v5 (default)
pfsunload-v4   -> v4 copy (rollback)
pfs            -> v5 (default)
pfs-v4         -> v4 copy (rollback)
mkpfs          -> v5 (unchanged from v4 for squashfs; erofs via mkpfs-erofs)
mkpfs-erofs    -> v5 new binary
pfsinfo        -> v5 (adds --machine; v4 behavior preserved)
```

**PKGBUILD constraints**:
```
pkgname=pfs-utils5
pkgver=2026.04
conflicts=('pfs-utils' 'pfs-utils-cli')
provides=("pfs-utils=$pkgver" "pfs-utils-cli=$pkgver")
```

**Rollback procedure**:
```bash
# Temporarily use v4 for a single load
pfsload-v4 /mnt/data/module.pfs

# Source v4 library in a script
. pfs-v4
addlayer 0 /mnt/.module.pfs
```

---

## Appendix: modman-gui TSV parser (byte-level reference)

The C parser in `modman-gui.c:63-104` that all `--machine` output must satisfy:

```c
gchar line[2048];
while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\r\n")] = 0;          // strips LF or CRLF
    gchar **parts = g_strsplit(line, "\t", -1); // split on TAB, unlimited fields
    if (g_strv_length(parts) >= 4) {           // skip lines with < 4 fields
        // parts[0] = name
        // parts[1] = layer  (also parsed by parse_size_mb() for sort)
        // parts[2] = path
        // parts[3] = desc
        // parts[4+] = ignored (category, version in v5)
    }
}
```

**Constraints derived from this parser**:

1. Lines longer than 2047 bytes (including `\n`) are silently truncated by `fgets`. Keep lines short.
2. `g_strsplit(line, "\t", -1)` splits on every TAB. A value containing a literal TAB must be escaped as `\t` before emission.
3. `g_strv_length(parts) >= 4` means a line with 3 or fewer TABs is silently dropped.
4. `parse_size_mb()` in `parts[1]` accepts `G`, `M`, `K` suffixes (case-sensitive uppercase). Other formats fall back to 0.0 for sort purposes.
