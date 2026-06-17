# Альтернативы AUFS для динамической загрузки модулей

> Заметки исследования из сессии modman3 / pfs-utils, апрель 2026.
> Companion doc на английском: `aufs-alternatives.md`.

## TL;DR

PuppyRus / pfs-utils использует AUFS для **динамического добавления
`.pfs` squashfs модулей в живую корневую файловую систему**. AUFS поддерживает
`mount -o remount,append:branch /` который добавляет новый lowerdir-слой
не нарушая работающие процессы. AUFS deprecated upstream
(out-of-tree kernel patch). Мы исследовали альтернативы которые могли бы
дать ту же возможность dynamic load на overlayfs-системах или системах
без AUFS.

**Итог**: **нет ни одной drop-in замены** AUFS dynamic-add. Каждая
альтернатива жертвует какой-то осью (system-wide visibility, kernel-уровень
производительности, atomic refresh, область покрытия filesystem hierarchy).
Для pfs-utils мы рекомендуем многоуровневый подход:

| Tier | Подход                                                          | Статус                                                 |
| ---- | --------------------------------------------------------------- | ------------------------------------------------------ |
| 1    | Принять ограничение overlay root: hard-error + документация       | Прагматично, ноль работы с kernel                      |
| 2    | Новый инструмент `pfsrun`, с namespace-plumbing через `pfsexec`   | Как Porteus 5+ activate, изолировано per-shell         |
| 3    | Изучить **mergerfs** как boot-level замену (будущая major версия) | True dynamic add через xattr API; крупное архит. изм.  |

## Постановка задачи

PuppyRus системы загружаются с одним из двух kernel layering выбираемых
`mkinitcpio-rootaufs2` initrd:

- `dir=NAME` — корень это **AUFS** union (default, нужен AUFS-capable kernel)
- `diro=NAME` — корень это **OverlayFS** union (новее, AUFS-free)

После boot пользователь запускает `pfsload module.pfs` чтобы добавить
новый слой. С AUFS это работает in-place через `mount -o remount,append:`.
С OverlayFS kernel **не поддерживает** добавление `lowerdir` к активному
mount — это enforce'ится на kernel level и не является опцией конфигурации.

Нам нужен способ дать эквивалентный dynamic-load UX на overlay-mode системах.

## Подтверждённые kernel-ограничения

### OverlayFS не может dynamic-add lowerdir

Подтверждено в [Linux Live Kit `livekitlib`](https://github.com/Tomas-M/linux-live/blob/9825e937570072a015e2e35ad3330ead9055d21d/livekitlib#L1009-L1020):
функция `union_append_bundles()` имеет AUFS branch но
**намеренно нет overlayfs branch'а**. Tomas Matejicek (автор Slax)
[писал](https://archive.is/SMBjs):

> *"Я понял что overlayfs совершенно непригоден для distro как Slax.
> Он не предоставляет необходимой функциональности вообще, невозможно
> работать с модулями на лету."*

### Kernel `lowerdir+` (≥6.8) — runtime remount явно отклонён

Синтаксис `lowerdir+` и `lowerdir-` через `fsconfig()` — для
**только initial mount construction**. Май 2025: патч позволяющий
remount overlayfs с новым lowerdir был отклонён maintainer'ом
Christian Brauner:

> *"Представьте кто-то передаёт valid lowerdir путь или другие valid
> опции и внезапно мы меняем lowerdir parameters для running overlayfs
> instance что очевидно немедленный security issue потому что мы только
> что создали UAFs повсюду."*
>
> Источник: https://patchew.org/linux/20250521-ovl._5Fro-v1-1-2350b1493d94@igalia.com/

### `FILESYSTEM_MAX_STACK_DEPTH = 2`

Захардкожено в `include/linux/fs.h` с
[коммита 69c433e (2014)](https://github.com/torvalds/linux/commit/69c433ed2ecd2d3264efd7afec4439524b319121).
Rootaufs2 overlay root = depth 1; sysext overlay на `/usr/` = depth 2
(at ceiling); добавление confext для `/etc/` потребовало бы depth 3
(невозможно без kernel patch'а).

## Сводная таблица сравнения

| Техника                                    | Тип          | Dynamic add? | System-wide?          | Production ready? | Заметки                                                |
| ------------------------------------------ | ------------ | ------------ | --------------------- | ----------------- | ------------------------------------------------------ |
| **AUFS** `remount,append`                      | Kernel union | ✅           | ✅                    | Только AUFS distros | Out-of-tree patch; deprecated upstream                 |
| **OverlayFS native**                           | Kernel union | ❌           | ✅                    | n/a               | Kernel-impossible добавить lowerdir в runtime          |
| **OverlayFS unmount + remount**                | Kernel union | ⚠️           | ✅                    | ⚠️ partial        | EBUSY на root fs; работает только для sub-mounts        |
| **Mount namespace** (`unshare -m` + sub-overlay) | Kernel       | ✅           | ❌ per-process        | ✅                | Как Porteus 5+ activate; не модифицирует host          |
| **Symlink injection** (Porteus neko)           | Userspace    | ✅           | ✅                    | ⚠️ fragile        | "Только временное использование" по словам автора       |
| **woof-CE sfs_load.overlay**                   | Userspace    | ✅           | ✅ (search-path level) | ⚠️ limited        | Симлинки в upperdir; не настоящий FS merge             |
| **mergerfs** (FUSE) + xattr API                | FUSE         | ✅           | ✅                    | ✅                | Production-grade; заменяет overlay/aufs root полностью |
| **unionfs-fuse**                               | FUSE         | ❌           | ❌                    | ❌                | Нет runtime API; только mount-time positional args     |
| **fuse-overlayfs**                             | FUSE         | ❌           | ✅                    | n/a               | `lowerdir=` только mount-time                           |
| **systemd-sysext**                             | Kernel + tool | ⚠️ refresh   | ✅ `/usr/` + `/opt/`   | ✅                | Production в Flatcar/CoreOS; короткий unmount gap      |
| **systemd-confext**                            | Kernel + tool | ⚠️ refresh   | ✅ `/etc/`              | ✅                | Удваивает overlay stack depth (depth budget concern)   |
| **composefs**                                  | Kernel       | ❌           | ✅                    | ⚠️ emerging       | Per-image mount; нет live branch add; depth concerns   |
| **DM-snapshot** (Fedora dmsquash-live)         | Block-level  | ❌           | ✅                    | ✅ boot-time      | Fixed at boot, нет runtime add                         |
| **BTRFS subvol + overlayfs**                   | Filesystem   | ❌           | ✅                    | ✅ boot-time      | Snapshot выбирается at boot                            |
| **Slax DynFileFS**                             | FUSE         | ❌           | ✅                    | ✅                | Только persistent changes, не module layers            |
| **Linux Live Kit** (Tomas-M)                   | Init system  | ❌           | ✅                    | ✅                | Только AUFS для runtime; overlay variant без activate  |
| **Bedrock Linux crossfs**                      | FUSE proxy   | ✅           | ⚠️ path-level         | ✅                | Не FS merge; экспозит binaries/fonts через `$PATH`       |
| **LD_PRELOAD** / linker path                   | Userspace    | ✅           | ❌ per-process        | ❌                | Влияет только на dynamic linker; нет FS view           |

## Детальные находки

### mergerfs — самая сильная замена

**Источник**: [trapexit/mergerfs](https://github.com/trapexit/mergerfs)
(6K звёзд, активно поддерживается на 2026)

mergerfs предоставляет `setfattr`-based runtime API для управления branch'ами:

```bash
# Initial mount
mergerfs -o allow_other,use_ino,category.create=ff \
  /mnt/mod1:/mnt/mod2:/mnt/rw \
  /merged

# Добавить новый layer в runtime (highest priority):
mount -o loop new.pfs /mnt/new
setfattr -n user.mergerfs.branches -v "+</mnt/new" /merged/.mergerfs

# Удалить:
setfattr -n user.mergerfs.branches -v "-/mnt/new" /merged/.mergerfs
umount /mnt/new
```

Branch операции: `+<` prepend, `+>` append, `-` remove,
`-<` remove first, `->` remove last.

**Сильные стороны**: thread-safe, system-wide visible, true dynamic, без
remount, без per-process namespace.

**Слабые стороны**: FUSE overhead (5-15% на metadata-heavy workloads),
не in-kernel, требует `mergerfs` установленный в initrd для использования
как root, не kernel default для live distros.

**Adoption path для pfs-utils**: заменить overlayfs root на mergerfs
root в `mkinitcpio-rootaufs2` (или новый hook). pfsload становится
тонкой обёрткой вокруг `setfattr`. Крупное архитектурное изменение но
решает dynamic-add чисто.

### systemd-sysext — частичное решение

**Источник**: [systemd man page](https://www.freedesktop.org/software/systemd/man/latest/systemd-sysext.html)

Предоставляет overlayfs merge squashfs/erofs/ext4 extensions на `/usr/`
и `/opt/`. Использует `refresh` (unmerge + remerge) cycle для runtime
изменений — не truly atomic но production-proven (Flatcar, Fedora
CoreOS).

**Жёсткие ограничения для pfs-utils**:
1. **Stacking depth**: rootaufs2 overlay (depth 1) + sysext (depth 2) =
   at kernel ceiling. Confext для `/etc/` подтолкнул бы к depth 3
   (невозможно).
2. **Path scope**: только `/usr/` и `/opt/`. PFS модули обычно
   шипают в `/etc/`, `/var/`, `/usr/local/` — silently dropped sysext'ом.
3. **Refresh gap**: короткий unmount window (sub-millisecond но real);
   процессы открывающие файлы в `/usr/` во время gap пропускают extensions.
4. **Identity**: sysext использует filename как identity; нужен
   `extension-release.<NAME>` metadata file внутри image.

**Вердикт**: пригодно для `/usr/`-only модулей на системах где depth
budget позволяет. Не general замена.

### Mount namespace (`unshare -m`) — паттерн Porteus 5+

Kernel built-in mount namespace позволяет каждому process tree иметь
свой view mounts. Новый namespace может стэкать новый overlay поверх
существующего root с дополнительным слоем — но только этот namespace
и его дети это видят.

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

**Сильные стороны**: kernel-builtin, нет лишних deps, не модифицирует
host, безопасно.

**Слабые стороны**: per-process visibility — existing daemons (systemd,
dbus, running browsers) не видят новый модуль. Подходит для "запустить
эту программу с этим модулем" но не для "установить модуль system-wide".

**Adoption path для pfs-utils**: отдельный инструмент `pfsexec` (НЕ flag
pfsload — другая семантика). UX похожий на `chroot`/`unshare`/`docker run`:
```sh
sudo pfsexec firefox.pfs firefox    # one-shot
sudo pfsexec firefox.pfs            # interactive shell
```

### Symlink injection (Porteus `ov.act.sh` / woof-CE `sfs_load.overlay`)

Автор "neko" Porteus реализовал это в 2020 как
[`001-overlayAct.xzm`](https://forum.porteus.org/viewtopic.php?t=9216).
Для каждого файла в новом модуле, переименовать existing → `.act.org.<name>`
(в upperdir), создать symlink на loop-mounted squashfs.

```sh
for i in $(find $LOOPMNT/$PKG/); do
    rel="${i#$LOOPMNT/$PKG/}"
    if [ -e /$rel ]; then
        mv /$rel $(dirname /$rel)/.act.org.$(basename /$rel)
    fi
    ln -sf $LOOPMNT/$PKG/$rel /$rel
done
```

woof-CE мерджнул похожий подход в [PR #3810](https://github.com/puppylinux-woof-CE/woof-CE/pull/3810).

**Сильные стороны**: работает на running root overlay без remount.

**Слабые стороны** (по словам автора и сообщества Puppy):
- Официально помечено "только временное использование" автором
- Симлинки не прозрачны: `open(O_NOFOLLOW)`, `st_dev` checks видят
  squashfs device, не overlay
- ОПАСНО в `changes=` persistence mode: `mv` rename'ы persist'ятся в
  save.dat; deactivation должен быть до reboot или originals потеряны
- Невозможно деактивировать boot-time модули (только runtime-loaded)
- Требует explicit `.act.new.<name>` sentinels; crash оставляет dangling
  symlinks

**Вердикт**: рабочее но fragile. Last resort.

### Что не работает — verified dead-ends

- **unionfs-fuse**: нет runtime API; только mount-time positional branches.
  Источник: [unionfs.8 man page](https://github.com/rpodgorny/unionfs-fuse/blob/master/man/unionfs.8).
- **fuse-overlayfs**: `lowerdir=` только mount-time.
  Источник: [containers/fuse-overlayfs](https://github.com/containers/fuse-overlayfs).
- **Kernel `lowerdir+` runtime remount**: явно отклонён
  maintainer'ами как UAF risk (May 2025).
- **Slax/Linux Live Kit overlay variant**: только AUFS для runtime
  module add. Tomas Matejicek подтвердил это намеренно.
- **DM-snapshot** (Fedora dmsquash-live): только boot-time, fixed at mount.
- **Bedrock crossfs**: не filesystem merge, только FUSE path proxy
  экспозящий binaries/fonts через `$PATH` — не сливает прозрачно
  `/lib`, `/etc`, `/var`.
- **LD_PRELOAD**: per-process, влияет только на dynamic linker; не FS view.

## Reference: ublinux.ru

UBLinux (commercial Russian enterprise distro by Юбитех / UBTech) использует
**ту же `.pfs` + AUFS + `changes/` архитектуру** как PuppyRus. Шипает
rootaufs2 initrd и предпочитает AUFS для kernel union. Никакой novel
техники для dynamic load на overlay root; UBLinux откатывается к
reboot-required workflow когда на overlayfs.

UBLinux value-add (не релевантно напрямую к dynamic load) включает:
- **HTTP/SSHFS/iSCSI module delivery** at boot time
- **Package → module pipeline** (build `.pfs` из installed packages)
- **Sandbox mode** (tmpfs `changes/` для security isolation)
- **Diskless workstation** support (PXE + network modules)

Source не публичный (proprietary). Документация на
[wiki.ublinux.ru](https://wiki.ublinux.ru/) и официальные PDF specs
на [ublinux.ru](https://ublinux.ru/).

## Рекомендации для pfs-utils

### Tier 1 — принять ограничение + документация (ноль нового кода)

`pfsload` на overlay-root системах печатает чёткий hard-error объясняющий
варианты:

```
pfsload: dynamic load not supported on overlay-root systems (kernel limit)
pfsload: detected initrd: rootaufs2 (overlay mode via diro=)
pfsload: options:
   1) Reboot with `dir=` instead of `diro=` for AUFS layering
   2) Add module to initrd config and reboot for persistence
   3) Use `pfsrun MODULE.pfs` for per-process isolation
```

Existing AUFS path остаётся unchanged. Overlay path останавливается чисто
вместо попытки сломанного `umount /` + remount.

### Tier 2 — новый инструмент `pfsrun` (per-process namespace)

Добавить отдельный launcher рядом с `pfsload`. Семантика: "запустить
команду в namespace где этот модуль наложен". Как
`chroot`/`unshare`/`docker run`, явно НЕ system-wide install. `pfsexec`
остаётся низкоуровневым namespace-helper за этим потоком.

```sh
sudo pfsrun MODULE.pfs [COMMAND [ARGS...]]
```

Если `COMMAND` опущена, drop в `$SHELL`. Когда команда выходит,
namespace уничтожен, модуль выгружен, host system без изменений.

Use cases: тестирование модулей перед persist; изолированные dev
environments; запуск self-contained portable apps.

### Tier 3 — mergerfs migration roadmap (будущая major версия)

Изучить замену overlayfs root на mergerfs root в новом mkinitcpio
hook. `pfsload` становится:

```sh
mount -o loop module.pfs /mnt/.module.pfs
setfattr -n user.mergerfs.branches -v "+</mnt/.module.pfs" /.mergerfs
```

Это восстанавливает true dynamic add (system-wide, без remount, без
per-process limitation) ценой:
- FUSE overhead в root fs (~5-15% на metadata workloads)
- Distribution mergerfs binary в initrd
- Новый hook, новая boot-time orchestration, integration с `dir=`/`diro=`
- Backwards compatibility story для AUFS-mode boots

Это многомесячное архитектурное изменение и должно landить в major
версии (e.g. pfs-utils 6.x).

## Источники (deduplicated)

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

## Открытые вопросы / будущее исследование

- **Custom kernel patch** для увеличения `s_stack_depth` до 3 или 4
  (позволил бы sysext + confext stack на overlay root). Risk: не
  upstream, custom kernel maintenance burden.
- **Mergerfs в initrd phase**: feasibility FUSE в early boot
  без systemd. Некоторые distros это делают (rclone-mount Ubuntu,
  ZFS-on-root); evaluation needed.
- **Hybrid**: AUFS-mode default + overlay-mode как opt-in. Большинство
  юзеров получают dynamic load; overlay-mode юзеры получают pfsexec
  для sandboxing.

## Реализация (статус, апрель 2026)

Этот раздел отражает какие альтернативные пути из настоящего документа были реализованы в pfs-utils.

### Tier 1 — Жёсткая ошибка pfsload на overlay-root

**Статус**: РЕАЛИЗОВАНО как guard отказа на overlay-root с рекомендацией `pfsrun`.

На overlay-root системах `pfsload` отказывается от системного hot-load пути вместо попытки неподдерживаемого добавления lowerdir. Документация рекомендует `pfsrun` как пользовательскую альтернативу. `pfsexec` остаётся низкоуровневым namespace-helper для сценариев, где нужен прямой plumbing.

### Tier 2 — Per-process module loader на mount-namespace

**Статус**: РЕАЛИЗОВАНО как `pfsrun`, с нижним уровнем через `pfsexec`.

- Основной бинарник: `pfs-utils/usr/bin/pfsrun`
- Низкоуровневый helper: `pfs-utils/usr/bin/pfsexec`
- Тесты: `pfs-utils/tests/bats/pfsrun-overlay.bats` и `pfs-utils/tests/bats/pfsexec.bats`
- Работает на aufs-root И overlay-root (унифицированный паттерн overlay-over-anything)
- Использует `unshare --mount --propagation private` + `pivot_root` для корректной изоляции
- CLI: `pfsrun [OPTIONS] MODULE.pfs [COMMAND [ARGS...]]`; `pfsexec` сохраняет низкоуровневый namespace interface
- Проверено вживую на overlay-boot тестовом PC (коммит 63f448c усиливает обработку детача старого корня для ядер с edge-кейсами submount)

### Tier 3 — mergerfs/FUSE как полная замена AUFS

**Статус**: ОТЛОЖЕНО до будущей мажорной версии.

mergerfs потребовал бы замены всей подложки слоёв (не просто добавления параллельного инструмента), что выходит за рамки текущей инкрементальной работы. Tier 2 (`pfsrun`, с нижним уровнем через `pfsexec`) покрывает срочную потребность на overlay-root системах, не ломая существующий AUFS-поток.

### Реализация symlink injection

**Статус**: РЕАЛИЗОВАНО как `pfsactivate` / `pfsdeactivate` (opt-in companion-инструменты).

- Бинарники: `pfs-utils/usr/bin/pfsactivate`, `pfs-utils/usr/bin/pfsdeactivate`
- Тесты: `pfs-utils/tests/bats/pfsactivate.bats`
- Основано на паттерне Porteus neko `ov.act.sh` / `ov.deact.sh`
- Loop-монтирует модуль read-only и создаёт `ln -sf` симлинки для каждого файла в модуле
- Оригиналы сохраняются как `.pfs.org.<name>` и восстанавливаются при deactivate
- Защитные guards отказываются перекрывать `/etc/passwd`, `/etc/shadow`, `/etc/sudoers`, `/etc/fstab`, `/etc/group`, и всё под `/dev`, `/proc`, `/sys`, `/boot`
- Rootfs-agnostic: работает на aufs-root, overlay-root или обычном ext4 root (без проверки `/proc/cmdline`)
- Состояние эфемерно хранится под `/run/pfs-utils/symlink-activations/<modname>/` (или `--persist DIR` для durable)
- EXPERIMENTAL предупреждение в `--help`: симлинки не прозрачны (O_NOFOLLOW, st_dev); на aufs-root предпочтительнее `pfsload` для kernel-level прозрачности

### chroot2pfs --overlay (сопутствующее изменение)

**Статус**: РЕАЛИЗОВАНО.

- Файлы: `pfs-utils/usr/bin/chroot2pfs` + новый helper `mkoverlay_chroot()` в `pfs-utils/usr/bin/pfs`
- Новые флаги: `--overlay` (принудительно overlay backend), `--aufs` (принудительно aufs backend)
- По умолчанию: автодетект через `pfs --layering-mode` → fallback на `aufs` для совместимости
- mkoverlay_chroot использует именование `aufs$N.lock` для совместимости с delaufs() (НЕ overlay$N.lock)
