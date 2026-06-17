# UIRD OverlayFS Technical Reference

**Source repository:** https://github.com/neobht/uird  
**Commit:** `10711ce350860ab40cb9eee91581ad382095d30c`  
**Key files:**
- [`modules.d/00uird/livekit/livekitlib`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib) — function library
- [`modules.d/00uird/livekit/uird-init`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/uird-init) — init orchestrator
- [`README.md`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/README.md) — parameter reference

---

## 1. OverlayFS Mount Syntax

### 1.1 Root Union Mount (main rootfs)

**Evidence** ([`livekitlib:663`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L663)):

```bash
mount -t overlay \
  -o redirect_dir=on,metacopy=off,index=on,\
lowerdir=${LOWER_DIRS},\
upperdir=${OVERLAY}/changes,\
workdir=${OVERLAY}/workdir \
  overlay $LUNION
```

Where:
- `$LOWER_DIRS` — colon-separated list of squashfs bundle mount points, built up as each module is mounted: `$LBUNDLES/$BUN:$LOWER_DIRS` (newest module prepended, so last-prepended = highest priority)
- `$OVERLAY` = `$MEMORY/ovl` = `/memory/ovl`
- `$OVERLAY/changes` = `/memory/ovl/changes` — upperdir (writable layer)
- `$OVERLAY/workdir` = `/memory/ovl/workdir` — overlay work directory
- `$LUNION` = `$NEWROOT` — dracut's new root target

After the overlay mount, changes are bind-mounted back:

```bash
mount --bind ${OVERLAY}/changes $LCHANGES
```

Where `$LCHANGES` = `$MEMORY/changes` = `/memory/changes`.

### 1.2 Homes Overlay Mount

**Evidence** ([`livekitlib:2226`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L2226)):

```bash
mount -t overlay \
  -o redirect_dir=on,metacopy=off,index=on,\
lowerdir=${HOMES_BR},\
upperdir=${OVERLAY}/homes,\
workdir=${OVERLAY}/homes_workdir \
  overlay $MNT_HOME
```

Where `$MNT_HOME` = `$UNION/home` = `$NEWROOT/home`.

### 1.3 AUFS Equivalent (for reference)

When `uird.union=` is not `overlay`, AUFS is used instead:

**Evidence** ([`livekitlib:667`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L667)):

```bash
mount -t aufs \
  -o nowarn_perm,xino=$MEMORY/.xino_union,trunc_xino,\
br=${LCHANGES}:${LOWER_DIRS} \
  aufs $LUNION
```

### 1.4 Union FS Selection Logic

**Evidence** ([`livekitlib:2662-2678`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L2662-L2678)):

```
uird.union=auto     → try aufs first; fall back to overlay if aufs not in /proc/filesystems
uird.union=overlay  → force overlayfs
uird.union=aufs     → force aufs (default when uird.union not set)
-o (short flag)     → equivalent to uird.union=overlay
```

### 1.5 Module Mount (per-bundle squashfs)

Each `.xzm`/`.pfs`/`.rom` module is individually loop-mounted before being added to `lowerdir`:

**Evidence** ([`livekitlib:641`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L641)):

```bash
mount -o $OPTIONS $FS "$BUNDLE" "$LBUNDLES/$BUN"
# then: LOWER_DIRS="$LBUNDLES/$BUN:$LOWER_DIRS"
```

Where `$LBUNDLES` = `$MEMORY/bundles` = `/memory/bundles`.

---

## 2. Kernel Cmdline Parameters

All parameters use the `uird.` prefix. Two assignment forms:

```
uird.param=value          # set/replace value
uird.param+=value         # append to existing list
```

**Evidence** ([`livekitlib:139-217`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L139-L217), [`README.md`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/README.md)):

### 2.1 Source Parameters

| Parameter | Description |
|-----------|-------------|
| `uird.from[+]=` | Sources containing modules/directories for root FS construction. Searched in order; multiple values separated by `;` or `,` |
| `uird.cache[+]=` | Sources to synchronise modules into (cache layer) |
| `uird.homes[+]=` | Sources cascade-mounted (AUFS/overlay) at `/home` |
| `uird.home=` | Single source mounted at `/home` (no cascade) |
| `uird.changes=` | Source for persistent changes (requires `uird.mode=changes\|clear\|machines`) |
| `uird.mounts[+]=` | Sources mounted at specified mount points |

### 2.2 Module Filter Parameters

| Parameter | Description |
|-----------|-------------|
| `uird.ro[+]=` | Glob filters for modules mounted read-only |
| `uird.rw[+]=` | Glob filters for modules mounted read-write |
| `uird.cp[+]=` | Glob filters for modules whose contents are copied to root |
| `uird.load[+]=` | Glob filters for modules/dirs to include at boot |
| `uird.noload[+]=` | Glob filters to exclude from `uird.load` results |
| `uird.copy2ram[+]=` | Glob filters for modules to copy to RAM before activation |
| `uird.copy2cache[+]=` | Glob filters for modules to copy to cache |
| `uird.run[+]=` | Glob filters for executable modules to run (not mount) |
| `uird.find_params[+]=` | Extra `find` parameters for module search (e.g. `-maxdepth,2`) |

### 2.3 Mode and Behaviour Parameters

| Parameter | Values | Description |
|-----------|--------|-------------|
| `uird.mode=` | `clean`, `clear`, `changes`, `machines`, `hybrid`, `toxzm` | Persistence mode |
| `uird.union=` | `auto`, `overlay`, `aufs` | Union FS driver selection |
| `uird.rootfs=` | `tmpfs`, `zram` | Type of `/memory` tmpfs |
| `uird.swap=` | device/file/`auto`/`zram`/`zswap` | Swap configuration |

### 2.4 Configuration Parameters

| Parameter | Description |
|-----------|-------------|
| `uird.basecfg=` | Path to base config file (`basecfg.ini`) |
| `uird.config=` | Path to system ini file (e.g. `MagOS.ini`) |
| `uird.sgnfiles[+]=` | Marker files for source detection (matched to `uird.from` order) |

### 2.5 Network Parameters

| Parameter | Description |
|-----------|-------------|
| `uird.ip=` | `CLIENT:GW:MASK` — static IP; DHCP if absent |
| `uird.netfsopt[+]=` | Extra mount options for network FSes (sshfs, nfs, curlftpfs, cifs) |
| `uird.aria2ram=` | Network sources to pre-download to RAM before `uird.from` search |

### 2.6 Debug / Control Parameters

| Parameter | Description |
|-----------|-------------|
| `uird.break=STAGE` | Stop boot at named stage, drop to debug shell |
| `uird.scan=` | Run `uird.scan` to auto-detect OS/component parameters |
| `uird.syscp[+]=` | Files/dirs to copy from UIRD into system (`/src:::/dst` syntax) |
| `uird.run[+]=` | External executables to run during init |
| `uird.preinit` | Enable INI file processing from `uird.config` |
| `uird.shutdown` | Create `/run/initramfs` for systemd shutdown handoff |
| `uird.freemedia` | Unmount sources after `uird.copy2ram` |
| `uird.force` | Continue if source not found (no interactive prompt) |
| `uird.hide` | Do not remount `$MEMORY` into new root |
| `uird.mntlinks` | Create `/mnt/live`, `/mnt/livemedia`, `/mnt/livedata` symlinks |
| `uird.parallel` | Mount modules in parallel |
| `uird.silent` | Suppress console output |
| `quickshell` / `qs` | Drop to shell at start of uird-init |
| `qse` | Drop to shell at end of uird-init |
| `debug` | Verbose output + pause at multiple stages |

### 2.7 Short Flag Aliases

**Evidence** ([`livekitlib:478-498`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L478-L498)):

```
-q  → qs (quickshell)
-Q  → qse
-p  → uird.preinit
-S  → uird.shutdown
-C  → uird.copy2ram (all)
-F  → uird.freemedia
-f  → uird.force
-z  → uird.rootfs=zram
-s  → uird.swap=auto
-o  → uird.union=overlay
-c  → uird.mode=clean
-L  → uird.silent
```

---

## 3. `/etc/initvars` Variables

UIRD writes runtime state to `/etc/initvars` via the `update_initvars()` function. At the end of init, this file is copied verbatim into `$NEWROOT/etc/initvars`.

**Evidence** ([`uird-init:240`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/uird-init#L240), [`livekitlib:2689-2699`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L2689-L2699)):

```bash
# update_initvars() — upserts KEY=VALUE into /etc/initvars
echo "KEY=VALUE" | update_initvars
```

### 3.1 Variables Written by UIRD

| Variable | Source | Description |
|----------|--------|-------------|
| `SYSMNT` | Read at startup from existing `/etc/initvars` | Memory mount point; defaults to `/memory` if absent |
| `SECLEVEL` | `/secure` file or `0` | Security level (0–5) |
| `LIVEKITNAME` | Derived from `$BASECFG` basename | Name of the live kit |
| `PATHINI` | `setup_config()` | Resolved path to system INI file |
| `uird.from` | `cfg_parser()` via `update_initvars` | Normalised `uird.from` value |
| `uird.changes` | `cfg_parser()` | Normalised `uird.changes` value |
| `uird.mode` | `cfg_parser()` | Active persistence mode |
| `uird.union` | `cfg_parser()` | Active union FS type |
| `uird.config` | `cfg_parser()` | Active config file path |
| `uird.load` | `cfg_parser()` | Active load filter |
| `uird.ro` | `cfg_parser()` | Active RO filter |
| `uird.rw` | `cfg_parser()` | Active RW filter |
| All `uird.*` params | `cfg_parser()` | Every parsed cmdline/basecfg parameter is written |

**Note:** `cfg_parser()` calls `update_initvars` for every parsed parameter, transforming `uird.param=value` → `uird_param=value` (dots replaced with underscores in the shell variable name, but the key written to `/etc/initvars` retains the dot form).

**Evidence** ([`livekitlib:440`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L440)):

```bash
echo "$par" | sed -re 's/^([^=]*)\.([^=]*\=.*)$/\1_\2/' \
              -e 's/^([[:digit:]].*)$/_&/' | update_initvars
```

### 3.2 Reading `SYSMNT` at Boot

**Evidence** ([`uird-init:10-11`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/uird-init#L10-L11)):

```bash
[ -f /etc/initvars ] && . /etc/initvars
[ "$SYSMNT" ] && MEMORY=$SYSMNT || MEMORY=/memory
```

`SYSMNT` is the primary variable that tells UIRD where its memory/work directory is. If not set, `/memory` is used.

---

## 4. Supported Module Formats

**Evidence** ([`README.md` — MagOS config example](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/README.md), [`livekitlib:589-591`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L589-L591)):

| Extension | Mount Mode | Description |
|-----------|-----------|-------------|
| `.xzm` | RO (squashfs) | Primary squashfs module format (XZ-compressed) |
| `.xzm.cp` | CP (copy to root) | Squashfs module whose contents are extracted to root |
| `.rom` | RO | Read-only layer (squashfs or image) |
| `.rom.enc` | RO encrypted | Encrypted read-only layer (loopaes/LUKS) |
| `.rwm` | RW | Read-write layer |
| `.rwm.enc` | RW encrypted | Encrypted read-write layer |
| `.pfs` | RO (squashfs) | PuppyRus/pfs-utils squashfs format |
| `.sfs` | RO (squashfs) | Generic squashfs (used in some configs) |
| `.iso` / `.img` | RO (loop) | Disk image (ISO, raw block device image) |
| `.vdi` / `.qcow2` | RO (qemu-nbd) | Virtual machine disk images |
| directory | RO/RW (bind) | Plain directory treated as a layer |

The `uird.ro=` and `uird.rw=` filters determine mount mode. Typical MagOS config:

```ini
uird.ro=*.xzm;*.rom;*.rom.enc;*.pfs;*.sfs
uird.rw=*.rwm;*.rwm.enc
uird.cp=*.xzm.cp,*/rootcopy
```

---

## 5. Module Layering Order

### 5.1 Discovery and Sort

**Evidence** ([`livekitlib:2252-2264`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L2252-L2264), [`livekitlib:2237-2246`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L2237-L2246)):

```bash
find_modules() {
    # Runs: find "$1" -path "$filter" $FIND_PARAMS | sort
    # for each filter in uird.ro + uird.rw + uird.run + uird.cp
}

list_modules() {
    find_modules "$1" | sort | uniq | while read LINE; do
        # apply uird.load / uird.noload filters
    done
}
```

Modules are discovered with `find` and **sorted alphabetically** by full path before processing.

### 5.2 lowerdir Construction (overlay)

**Evidence** ([`livekitlib:642-643`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L642-L643)):

```bash
LOWER_DIRS="$LBUNDLES/$BUN:$LOWER_DIRS"
```

Each new module is **prepended** to `LOWER_DIRS`. Since `list_modules` returns modules in ascending alphabetical order, the last module alphabetically is prepended last → it appears **first** (leftmost) in `lowerdir` → it has the **highest priority** in overlay.

**Result:** Modules with names sorting **later alphabetically** (e.g. `z-mymod.xzm`) override modules sorting earlier (e.g. `00-base.xzm`). This is the standard Porteus/Slax convention: `z*` modules sit on top.

### 5.3 Layer Priority Stack (high → low)

```
upperdir: $MEMORY/ovl/changes     ← persistent changes (uird.changes=)
lowerdir[0]: /memory/bundles/z-last-module.xzm   ← highest priority module
lowerdir[1]: /memory/bundles/m-middle.xzm
lowerdir[2]: /memory/bundles/00-base.xzm          ← lowest priority module
```

### 5.4 Source Order

When multiple `uird.from=` sources are specified, they are processed in declaration order. Modules from all sources are collected into a single sorted list before overlay construction — source order does not override alphabetical sort within the combined list.

### 5.5 `uird.load` Filter Priority

**Evidence** ([`livekitlib:2255-2263`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L2255-L2263)):

```
uird.load  → include filter (module path must match)
uird.noload → exclude filter (applied after uird.load match)
Priority: uird.load --> uird.noload
          uird.cp --> uird.rw --> uird.ro
```

---

## 6. Directory Layout at Runtime

**Evidence** ([`README.md` — structure section](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/README.md), [`uird-init:50-61`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/uird-init#L50-L61)):

```
$MEMORY/                        # = /memory (or $SYSMNT)
├── bundles/                    # individual module mount points
│   ├── 00-kernel.xzm/
│   ├── 10-core.xzm/
│   └── z-mymod.pfs/
├── changes/                    # bind-mount of ovl/changes (persistent writes)
├── ovl/                        # overlay helper dirs
│   ├── changes/                # upperdir
│   └── workdir/                # workdir
├── data/
│   ├── from/0, from/1, ...     # mounted uird.from sources
│   ├── cache/0, ...            # mounted uird.cache sources
│   ├── homes/0, ...            # mounted uird.homes sources
│   ├── mounts/0, ...           # mounted uird.mounts sources
│   └── changes/                # mounted uird.changes source
├── copy2ram/                   # RAM copy of modules
├── layer-base/0, 1, ...        # base layer mount points
├── layer-cache/0, 1, ...       # cache layer mount points
├── layer-homes/0, 1, ...       # homes layer mount points
└── layer-mounts/0, 1, ...      # mounts layer mount points
```

---

## 7. Source Type Syntax

**Evidence** ([`README.md`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/README.md)):

```
/path/dir                   directory on any available device
/dev/sdXN/path/dir          directory on specific device
LABEL@/path/dir             device by filesystem label
UUID@/path/dir              device by UUID
file.iso, file.img          disk image (loop-mounted)
file.vdi, file.qcow2        VM disk image (via qemu-nbd)
http://server/path/         HTTP (httpfs)
ssh://server/path/          SSH (sshfs)
ftp://server/path/          FTP (curlftpfs)
nfs://server/path/          NFS
cifs://server/path/         CIFS/SMB
```

Subvalue syntax (double-colon separated):

```
uird.from=/MagOS::MNT=/mnt/magos::MNT_OPTS=ro+noexec::TIMEOUT=5::FORCE=yes::SGN=marker.file
```

---

## 8. `uird.mode` Values

**Evidence** ([`livekitlib:2073-2074`](https://github.com/neobht/uird/blob/10711ce350860ab40cb9eee91581ad382095d30c/modules.d/00uird/livekit/livekitlib#L2073-L2074)):

| Mode | Behaviour |
|------|-----------|
| `clean` | No persistent changes; changes layer is tmpfs only |
| `clear` | Wipe changes directory on each boot, then use it |
| `changes` | Mount `uird.changes=` source as persistent upperdir |
| `machines` | Per-machine changes stored as `$MUID.xzm` in machines dir |
| `hybrid` | Wipe changes on boot but keep machines layer |
| `toxzm` | Save session changes back into a `.xzm` module |

---

## 9. Relation to pfs-utils / modman

- UIRD is the **initrd** layer: it assembles the root FS from modules at boot time.
- `pfsload`/`pfsunload` (pfs-utils) are **AUFS-only** for post-boot system-wide hot attach/detach.
- OverlayFS roots don't support system-wide hot attach/detach after boot. Use `pfsrun` for OverlayFS runtime execution; `pfsexec` is lower-level plumbing behind that flow.
- `modman` CLI wraps pfs-utils for AUFS runtime management; `modman-gui` is the GTK3 frontend.
- UIRD's `uird.from=` and `uird.load=` filters determine which modules are in the **base layer** (Система tab in modman-gui); AUFS `pfsload` adds to the **hot-attached layer** (Подключенные tab).
