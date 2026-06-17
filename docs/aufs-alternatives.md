# AUFS Alternatives for Dynamic Module Loading

> Research notes from the modman3 / pfs-utils session, April 2026.
> Companion doc in Russian: `aufs-alternatives.ru.md`.

## TL;DR

PuppyRus / pfs-utils relies on AUFS for **runtime addition of `.pfs` squashfs
modules to the live root filesystem**. AUFS supports
`mount -o remount,append:branch /` which adds a new lowerdir layer
without disturbing running processes. AUFS is deprecated upstream
(out-of-tree kernel patch). We surveyed alternatives that could provide
the same dynamic-load capability on overlayfs-based or AUFS-free systems.

**Bottom line**: there is **no single drop-in replacement** for AUFS
dynamic-add. Each alternative trades off a different axis (system-wide
visibility, kernel-level performance, atomic refresh, scope of
filesystem hierarchy covered). For pfs-utils we recommend a tiered
approach:

| Tier | Approach                                                      | Status                                                  |
| ---- | ------------------------------------------------------------- | ------------------------------------------------------- |
| 1    | Accept limit on overlay root: hard-error + documentation        | Pragmatic, zero kernel work                             |
| 2    | New `pfsrun` tool, backed by `pfsexec` namespace plumbing       | Like Porteus 5+ activate, isolated per-shell            |
| 3    | Investigate **mergerfs** as boot-level replacement (future major) | True dynamic add via xattr API; significant arch change |

## Problem Statement

PuppyRus systems boot with one of two kernel layering choices set by the
`mkinitcpio-rootaufs2` initrd:

- `dir=NAME` — root is **AUFS** union (default, AUFS-capable kernel needed)
- `diro=NAME` — root is **OverlayFS** union (newer, AUFS-free)

After boot the user runs `pfsload module.pfs` to add a new layer. With
AUFS this works in-place via `mount -o remount,append:`. With OverlayFS
the kernel **does not support** adding `lowerdir` to an active mount —
this is enforced at the kernel level and is not a configuration option.

We need a way to provide the equivalent dynamic-load UX on overlay-mode
systems.

## Verified Kernel Constraints

### OverlayFS cannot dynamic-add lowerdir

Confirmed in [Linux Live Kit `livekitlib`](https://github.com/Tomas-M/linux-live/blob/9825e937570072a015e2e35ad3330ead9055d21d/livekitlib#L1009-L1020):
the `union_append_bundles()` function has an AUFS branch but
**intentionally no overlayfs branch**. Tomas Matejicek (Slax author)
[wrote](https://archive.is/SMBjs):

> *"I realized that overlayfs is completely unsuitable for a distro
> such as Slax. It does not provide the necessary functionality at
> all, it is not possible to work with modules on the fly."*

### Kernel `lowerdir+` (≥6.8) — runtime remount explicitly rejected

The `lowerdir+` and `lowerdir-` syntax via `fsconfig()` is for
**initial mount construction only**. A May 2025 patch to allow
remounting overlayfs with new lowerdir was rejected by maintainer
Christian Brauner:

> *"Consider someone passing a valid lowerdir path or other valid
> options then suddenly we're changing the lowerdir parameters for a
> running overlayfs instance which is obviously an immediate security
> issue because we've just managed to create UAFs all over the place."*
>
> Source: https://patchew.org/linux/20250521-ovl._5Fro-v1-1-2350b1493d94@igalia.com/

### `FILESYSTEM_MAX_STACK_DEPTH = 2`

Hardcoded in `include/linux/fs.h` since
[commit 69c433e (2014)](https://github.com/torvalds/linux/commit/69c433ed2ecd2d3264efd7afec4439524b319121).
A rootaufs2 overlay root is depth 1; a sysext overlay on `/usr/` is
depth 2 (at the ceiling); adding confext for `/etc/` would need depth 3
(impossible without kernel patching).

## Comparison Matrix

| Technique                                  | Type         | Dynamic add? | System-wide?          | Production ready? | Notes                                                   |
| ------------------------------------------ | ------------ | ------------ | --------------------- | ----------------- | ------------------------------------------------------- |
| **AUFS** `remount,append`                      | Kernel union | ✅           | ✅                    | Limited to AUFS distros        | Out-of-tree kernel patch; deprecated upstream           |
| **OverlayFS native**                           | Kernel union | ❌           | ✅                    | n/a               | Kernel-impossible to add lowerdir at runtime            |
| **OverlayFS unmount + remount**                | Kernel union | ⚠️           | ✅                    | ⚠️ partial        | EBUSY on root fs; works for sub-mounts only             |
| **Mount namespace** (`unshare -m` + sub-overlay) | Kernel       | ✅           | ❌ per-process        | ✅                | Like Porteus 5+ activate; no host modification          |
| **Symlink injection** (Porteus neko)           | Userspace    | ✅           | ✅                    | ⚠️ fragile        | "Temporary use only" per author; persistence dangerous  |
| **woof-CE sfs_load.overlay**                   | Userspace    | ✅           | ✅ (search-path level) | ⚠️ limited        | Symlinks in upperdir; not a true FS merge               |
| **mergerfs** (FUSE) + xattr API                | FUSE         | ✅           | ✅                    | ✅                | Production-grade; replaces overlay/aufs root entirely   |
| **unionfs-fuse**                               | FUSE         | ❌           | ❌                    | ❌                | No runtime API; mount-time positional args only        |
| **fuse-overlayfs**                             | FUSE         | ❌           | ✅                    | n/a               | `lowerdir=` mount-time only                              |
| **systemd-sysext**                             | Kernel + tool | ⚠️ refresh   | ✅ `/usr/` + `/opt/`   | ✅                | Production in Flatcar/CoreOS; brief unmount gap         |
| **systemd-confext**                            | Kernel + tool | ⚠️ refresh   | ✅ `/etc/`              | ✅                | Doubles overlay stack depth (depth budget concern)      |
| **composefs**                                  | Kernel       | ❌           | ✅                    | ⚠️ emerging       | Per-image mount; no live branch add; depth concerns     |
| **DM-snapshot** (Fedora dmsquash-live)         | Block-level  | ❌           | ✅                    | ✅ boot-time      | Fixed at boot, no runtime add                           |
| **BTRFS subvol + overlayfs**                   | Filesystem   | ❌           | ✅                    | ✅ boot-time      | Snapshot chosen at boot                                 |
| **Slax DynFileFS**                             | FUSE         | ❌           | ✅                    | ✅                | Persistent changes only, not module layers              |
| **Linux Live Kit** (Tomas-M)                   | Init system  | ❌           | ✅                    | ✅                | AUFS-only for runtime; overlay variant has no activate  |
| **Bedrock Linux crossfs**                      | FUSE proxy   | ✅           | ⚠️ path-level         | ✅                | Not a FS merge; exposes binaries/fonts via `$PATH`        |
| **LD_PRELOAD** / linker path                   | Userspace    | ✅           | ❌ per-process        | ❌                | Affects dynamic linker only; no FS view                 |

## Detailed Findings

### mergerfs — the strongest replacement

**Source**: [trapexit/mergerfs](https://github.com/trapexit/mergerfs)
(6K stars, actively maintained as of 2026)

mergerfs exposes a `setfattr`-based runtime API for branch management:

```bash
# Initial mount
mergerfs -o allow_other,use_ino,category.create=ff \
  /mnt/mod1:/mnt/mod2:/mnt/rw \
  /merged

# Add new layer at runtime (highest priority):
mount -o loop new.pfs /mnt/new
setfattr -n user.mergerfs.branches -v "+</mnt/new" /merged/.mergerfs

# Remove:
setfattr -n user.mergerfs.branches -v "-/mnt/new" /merged/.mergerfs
umount /mnt/new
```

Branch operations: `+<` prepend, `+>` append, `-` remove,
`-<` remove first, `->` remove last.

**Strengths**: thread-safe, system-wide visible, true dynamic, no
remount, no per-process namespace.

**Weaknesses**: FUSE overhead (5-15% on metadata-heavy workloads), not
in-kernel, requires `mergerfs` installed in initrd to use as root, not
the kernel default for live distros.

**Adoption path for pfs-utils**: replace overlayfs root with mergerfs
root in `mkinitcpio-rootaufs2` (or new hook). pfsload becomes a thin
wrapper around `setfattr`. Major architectural change but solves
dynamic-add cleanly.

### systemd-sysext — partial solution

**Source**: [systemd man page](https://www.freedesktop.org/software/systemd/man/latest/systemd-sysext.html)

Provides overlayfs merge of squashfs/erofs/ext4 extensions onto `/usr/`
and `/opt/`. Uses `refresh` (unmerge + remerge) cycle for runtime
changes — not truly atomic but production-proven (Flatcar, Fedora
CoreOS).

**Hard limitations for pfs-utils**:
1. **Stacking depth**: rootaufs2 overlay (depth 1) + sysext (depth 2) =
   at kernel ceiling. confext for `/etc/` would push to depth 3
   (impossible).
2. **Path scope**: only `/usr/` and `/opt/`. PFS modules routinely ship
   to `/etc/`, `/var/`, `/usr/local/` — silently dropped by sysext.
3. **Refresh gap**: brief unmount window (sub-millisecond but real);
   processes opening files in `/usr/` during the gap miss extensions.
4. **Identity**: sysext uses filename as identity; needs
   `extension-release.<NAME>` metadata file inside the image.

**Verdict**: usable for `/usr/`-only modules on systems where the
depth budget allows. Not a general replacement.

### Mount namespace (`unshare -m`) — Porteus 5+ pattern

The kernel built-in mount namespace allows each process tree to have
its own view of mounts. A new namespace can stack a new overlay over
the existing root with the additional layer added — but only that
namespace and its children see it.

```sh
unshare --mount --propagation private bash -c '
    mount -t squashfs -o loop new.pfs /mnt/.new
    mount -t overlay overlay \
        -o lowerdir=/mnt/.new:OLD_LOWER,upperdir=...,workdir=... \
        /mnt/.newroot
    cd /mnt/.newroot
    pivot_root . .old_root
    umount -l .old_root
    exec "$@"
'
```

**Strengths**: kernel-builtin, no extra deps, no host modification, safe.

**Weaknesses**: per-process visibility — existing daemons (systemd, dbus,
running browsers) do not see the new module. Suitable for "run this
program with this module" but not for "install this module system-wide".

**Adoption path for pfs-utils**: separate tool `pfsexec` (NOT a flag of
pfsload — different semantics). UX similar to `chroot`/`unshare`/`docker run`:
```sh
sudo pfsexec firefox.pfs firefox    # one-shot
sudo pfsexec firefox.pfs            # interactive shell
```

### Symlink injection (Porteus `ov.act.sh` / woof-CE `sfs_load.overlay`)

Author "neko" of Porteus implemented this in 2020 as
[`001-overlayAct.xzm`](https://forum.porteus.org/viewtopic.php?t=9216).
For each file in the new module, rename existing → `.act.org.<name>`
(into upperdir), create symlink to the loop-mounted squashfs.

```sh
for i in $(find $LOOPMNT/$PKG/); do
    rel="${i#$LOOPMNT/$PKG/}"
    if [ -e /$rel ]; then
        mv /$rel $(dirname /$rel)/.act.org.$(basename /$rel)
    fi
    ln -sf $LOOPMNT/$PKG/$rel /$rel
done
```

woof-CE merged a similar approach in [PR #3810](https://github.com/puppylinux-woof-CE/woof-CE/pull/3810).

**Strengths**: works on running root overlay without remount.

**Weaknesses** (per author and Puppy community):
- Officially marked "temporary use only" by the author
- Symlinks not transparent: `open(O_NOFOLLOW)`, `st_dev` checks see
  squashfs device, not overlay
- DANGEROUS in `changes=` persistence mode: `mv` renames persist into
  save.dat; deactivation must happen before reboot or originals are lost
- Cannot deactivate boot-time modules (only runtime-loaded ones)
- Requires explicit `.act.new.<name>` sentinels; crash leaves dangling
  symlinks

**Verdict**: workable but fragile. Last resort.

### What does not work — verified dead-ends

- **unionfs-fuse**: no runtime API; only mount-time positional branches.
  Source: [unionfs.8 man page](https://github.com/rpodgorny/unionfs-fuse/blob/master/man/unionfs.8).
- **fuse-overlayfs**: `lowerdir=` mount-time only.
  Source: [containers/fuse-overlayfs](https://github.com/containers/fuse-overlayfs).
- **Kernel `lowerdir+` runtime remount**: explicitly rejected by
  maintainers as UAF risk (May 2025).
- **Slax/Linux Live Kit overlay variant**: AUFS-only for runtime
  module add. Tomas Matejicek confirmed this is intentional.
- **DM-snapshot** (Fedora dmsquash-live): boot-time only, fixed at mount.
- **Bedrock crossfs**: not a filesystem merge, only a FUSE path proxy
  exposing binaries/fonts via `$PATH` — does not transparently merge
  `/lib`, `/etc`, `/var`.
- **LD_PRELOAD**: per-process, affects dynamic linker only; not a FS view.

## Reference: ublinux.ru

UBLinux (commercial Russian enterprise distro by Юбитех / UBTech) uses
**the same `.pfs` + AUFS + `changes/` architecture** as PuppyRus. It
ships the rootaufs2 initrd and prefers AUFS for the kernel union. There
is no novel technique for dynamic load on overlay root; UBLinux falls
back to reboot-required workflow when on overlayfs.

UBLinux's value-add (not directly relevant to dynamic load) includes:
- **HTTP/SSHFS/iSCSI module delivery** at boot time
- **Package → module pipeline** (build `.pfs` from installed packages)
- **Sandbox mode** (tmpfs `changes/` for security isolation)
- **Diskless workstation** support (PXE + network modules)

Source not public (proprietary). Documented at
[wiki.ublinux.ru](https://wiki.ublinux.ru/) and the official PDF specs
on [ublinux.ru](https://ublinux.ru/).

## Recommendations for pfs-utils

### Tier 1 — accept limit + document (zero new code)

`pfsload` on overlay-root systems prints a clear hard-error explaining
options:

```
pfsload: dynamic load not supported on overlay-root systems (kernel limit)
pfsload: detected initrd: rootaufs2 (overlay mode via diro=)
pfsload: options:
   1) Reboot with `dir=` instead of `diro=` for AUFS layering
   2) Add module to initrd config and reboot for persistence
   3) Use `pfsrun MODULE.pfs` for per-process isolation
```

Existing AUFS path remains unchanged. Overlay path stops cleanly
instead of trying broken `umount /` + remount.

### Tier 2 — new `pfsrun` tool (per-process namespace)

Add a separate launcher alongside `pfsload`. Semantic: "run a command in
a namespace where this module is overlaid". Like `chroot`/`unshare`/
`docker run`, explicitly NOT a system-wide install. `pfsexec` remains the
lower-level namespace helper behind this flow.

```sh
sudo pfsrun MODULE.pfs [COMMAND [ARGS...]]
```

If `COMMAND` is omitted, drops into `$SHELL`. When the command exits,
the namespace is destroyed, the module is unloaded, the host system
sees no change.

Use cases: testing modules before persist; isolated dev environments;
running self-contained portable apps.

### Tier 3 — mergerfs migration roadmap (future major version)

Investigate replacing overlayfs root with mergerfs root in a new
mkinitcpio hook. `pfsload` becomes:

```sh
mount -o loop module.pfs /mnt/.module.pfs
setfattr -n user.mergerfs.branches -v "+</mnt/.module.pfs" /.mergerfs
```

This restores true dynamic add (system-wide, no remount, no
per-process limitation) at the cost of:
- FUSE overhead in the root fs (~5-15% on metadata workloads)
- Distribution of mergerfs binary in initrd
- New hook, new boot-time orchestration, integration with `dir=`/`diro=`
- Backwards compatibility story for AUFS-mode boots

This is a multi-month architectural change and should land in a major
version (e.g. pfs-utils 6.x).

## References (deduplicated)

- [Linux Live Kit livekitlib (Tomas-M)](https://github.com/Tomas-M/linux-live/blob/master/livekitlib)
- [Slax overlayfs unsuitability statement (archive)](https://archive.is/SMBjs)
- [OverlayFS lowerdir+ remount rejection (May 2025)](https://patchew.org/linux/20250521-ovl._5Fro-v1-1-2350b1493d94@igalia.com/)
- [FILESYSTEM_MAX_STACK_DEPTH commit (2014)](https://github.com/torvalds/linux/commit/69c433ed2ecd2d3264efd7afec4439524b319121)
- [erofs s_stack_depth fix (Jan 2026)](https://github.com/torvalds/linux/commit/072a7c7cdbea4f91df854ee2bb216256cd619f2a)
- [trapexit/mergerfs runtime_interface](https://github.com/trapexit/mergerfs/blob/master/mkdocs/docs/runtime_interface.md)
- [systemd-sysext man page](https://www.freedesktop.org/software/systemd/man/latest/systemd-sysext.html)
- [Flatcar sysext docs](https://flatcar-linux.org/docs/latest/provisioning/sysext)
- [Porteus ov.act.sh forum thread (neko, 2020)](https://forum.porteus.org/viewtopic.php?t=9216)
- [woof-CE sfs_load.overlay PR](https://github.com/puppylinux-woof-CE/woof-CE/pull/3810)
- [Bedrock Linux FAQ (crossfs)](https://bedrocklinux.org/faq.html)
- [unionfs-fuse man page](https://github.com/rpodgorny/unionfs-fuse/blob/master/man/unionfs.8)
- [containers/fuse-overlayfs](https://github.com/containers/fuse-overlayfs)
- [composefs project](https://github.com/composefs/composefs)
- [systemd-sysext nested mounts fix PR #34381](https://github.com/systemd/systemd/pull/34381)
- [uCore stacking depth issue #339](https://github.com/ublue-os/ucore/issues/339)
- [wiki.ublinux.ru](https://wiki.ublinux.ru/)

## Open Questions / Future Research

- **Custom kernel patch** for `s_stack_depth` increase to 3 or 4 (would
  enable sysext + confext stack on overlay root). Risk: not upstream,
  custom kernel maintenance burden.
- **Mergerfs in initrd phase**: feasibility of FUSE in early boot
  without systemd. Some distros do this (rclone-mount Ubuntu,
  ZFS-on-root); evaluation needed.
- **Hybrid**: AUFS-mode default + overlay-mode as opt-in. Most users
  get dynamic load; overlay-mode users get pfsexec for sandboxing.

## Implementation Status (April 2026)

This section tracks which alternative paths from this document have been implemented in pfs-utils.

### Tier 1 — Hard error in pfsload on overlay-root

**Status**: IMPLEMENTED as an overlay-root refusal guard with `pfsrun` guidance.

On overlay-root systems `pfsload` refuses the system-wide hot-load path instead of attempting unsupported lowerdir insertion. The documentation recommends `pfsrun` as the user-facing alternative. `pfsexec` remains the lower-level namespace helper for cases that need direct plumbing.

### Tier 2 — Mount-namespace based per-process module loader

**Status**: IMPLEMENTED as `pfsrun`, backed by `pfsexec`.

- Primary binary: `pfs-utils/usr/bin/pfsrun`
- Lower-level helper: `pfs-utils/usr/bin/pfsexec`
- Tests: `pfs-utils/tests/bats/pfsrun-overlay.bats` and `pfs-utils/tests/bats/pfsexec.bats`
- Works on aufs-root AND overlay-root (uniform overlay-over-anything pattern)
- Uses `unshare --mount --propagation private` + `pivot_root` for proper isolation
- CLI: `pfsrun [OPTIONS] MODULE.pfs [COMMAND [ARGS...]]`; `pfsexec` keeps the lower-level namespace interface
- Live-verified on overlay-boot test PC (commit 63f448c hardens old-root detach for kernels with submount edge cases)

### Tier 3 — mergerfs/FUSE full replacement of AUFS

**Status**: DEFERRED to a future major version.

mergerfs would require replacing the entire layering substrate (not just adding a parallel tool), which is out of scope for this incremental work. Tier 2 (`pfsrun`, backed by `pfsexec`) covers the immediate need on overlay-root systems without breaking the existing AUFS-based flow.

### Symlink Injection Implementation

**Status**: IMPLEMENTED as `pfsactivate` / `pfsdeactivate` (opt-in companion tools).

- Binaries: `pfs-utils/usr/bin/pfsactivate`, `pfs-utils/usr/bin/pfsdeactivate`
- Tests: `pfs-utils/tests/bats/pfsactivate.bats`
- Based on Porteus neko's `ov.act.sh` / `ov.deact.sh` pattern
- Loop-mounts the module read-only and creates `ln -sf` symlinks for every file in the module
- Originals are preserved as `.pfs.org.<name>` and restored on deactivate
- Safety guards refuse to overlay `/etc/passwd`, `/etc/shadow`, `/etc/sudoers`, `/etc/fstab`, `/etc/group`, and anything under `/dev`, `/proc`, `/sys`, `/boot`
- Rootfs-agnostic: works on aufs-root, overlay-root, or plain ext4 root (no `/proc/cmdline` check)
- State stored ephemerally under `/run/pfs-utils/symlink-activations/<modname>/` (or `--persist DIR` for durable)
- EXPERIMENTAL caveat in `--help`: symlinks are not transparent (O_NOFOLLOW, st_dev); on aufs-root prefer `pfsload` for kernel-level transparency

### chroot2pfs --overlay (companion change)

**Status**: IMPLEMENTED.

- Files: `pfs-utils/usr/bin/chroot2pfs` + new `mkoverlay_chroot()` helper in `pfs-utils/usr/bin/pfs`
- New flags: `--overlay` (force overlay backend), `--aufs` (force aufs backend)
- Default: auto-detect via `pfs --layering-mode` → falls back to `aufs` for compat
- mkoverlay_chroot uses `aufs$N.lock` naming for delaufs() compatibility (NOT overlay$N.lock)
