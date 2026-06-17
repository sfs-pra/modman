# Test harness

## Требования

### C unit tests (`make -C tests`)

- `gcc`
- `check` (`libcheck`)
- `pkg-config`, `glib-2.0`, `gio-2.0`, `gtk+-3.0`

> **Примечание:** В текущем окружении GLib/GIO/GTK pkg-config недоступны,
> поэтому `make -C tests` заблокирован. Bats-тесты работают без этих зависимостей.

### Bats integration tests

- `bats` (Bash Automated Testing System)
- `bash` 5.x
- Стандартные утилиты: `awk`, `mktemp`, `sort`, `wc`

### Packaging and MIME validation

- `desktop-file-validate` для `modman-open.desktop`, если установлен.
- `update-mime-database` для `data/mime/packages/modman-open.xml`, если установлен.
- `markdownlint` для новых Markdown checklist/docs, если установлен.
- `xgettext`, `msgmerge` и `msgfmt` для gettext-проверок, если установлены.

Если один из этих инструментов отсутствует, проверка считается
environment-gated. Запишите точный результат `command -v <tool>` в
`.omo/evidence/`, не отмечайте его как успешный запуск.

### Source-contract and manual QA checks

Для GUI-формулировок и manual QA обновлений запускайте focused source checks
перед финальным регрессионным проходом:

```bash
rg -n "Модули внутри|Локальные|Система|Инет|Подключенные" tests/manual-qa-checklist.md
```

Run the stale-wording scan from task evidence against `src include po tests`.
It should only match intentional negative source-contract patterns. Active
source or translation matches require a scoped fix before release.

## Запуск C unit tests

```bash
make -C tests run
```

## Покрытие C unit tests

```bash
make -C tests coverage
```

## Очистка

```bash
make -C tests clean
```

## Bats integration tests

### Запуск всех bats-тестов

```bash
bats tests/bats
```

### Запуск отдельных наборов

```bash
bats tests/bats/modman-smoke.bats
bats tests/bats/modman-updates.bats
bats tests/bats/modman-update-check.bats
bats tests/bats/modman-remove-local.bats
bats tests/bats/modman-loadcmd.bats
bats tests/bats/modman-loaded-mode.bats
bats tests/bats/modman-pfsload-mode.bats
bats tests/bats/modman-progress-file.bats
```

## Описание bats-наборов

### `modman-smoke.bats` (5 тестов)

Базовые smoke-тесты: `--help`, machine-вывод, поиск, валидация ошибок, `--list-local`.

### `modman-updates.bats` (44 теста)

Полный набор тестов для системы обновлений модулей (`--check-updates`, `--update`, `--update-all`).

**Покрытые сценарии:**

| Категория | Тесты |
|-----------|-------|
| Обнаружение модулей | DOWNLOAD_DIR, base, modules, optional, EXTRAMOD, runtime (losetup/layer-state) |
| Дедупликация | realpath-дедупликация симлинков |
| Классификация риска | `system` (base/loaded-before), `loaded` (runtime), `normal` (download) |
| Сравнение версий | table-driven: splitname, pkgrel, multi-part, downgrade-ignored |
| Verbose-skips | `version-unparsed` row, 4-field schema |
| Blacklist | add/remove/list, идемпотентность, сортировка, следующий незаблокированный кандидат, `--include-blacklisted` |
| Безопасная ротация | `.old`, `.old.1`, `.old.2` суффиксы, `update.log` |
| Отклонение системных | exit 8 без `--confirm-system-updates`, успех с флагом |
| Целостность оригинала | failed download, failed apply (empty file), rollback при mv-ошибке |
| Concurrent lock | второй процесс получает lock-timeout, оригинал не тронут |
| `--update-all` | нормальные + системные, blacklist, все успешны (exit 0), ошибка одного не блокирует другие |
| Поля строк | `update` (11 полей), `applied` (8 полей, old_path/new_path/backup_path), `skipped` (4 поля), `error` (4 поля) |
| Граничные случаи | неизвестный id, слэш в имени кандидата, пустой кэш, нет установленных модулей |

### `modman-update-check.bats` (21 тест)

Тесты для `modman-update-check` — login-time уведомителя об обновлениях (check-only, без мутаций).

**Покрытые сценарии:**

| Категория | Тесты |
|-----------|-------|
| Базовая работа | `--help`, `bash -n`, существование и исполняемость |
| Нет обновлений | тихий exit 0, `notify-send` не вызывается |
| Есть обновления | `notify-send` вызывается, содержит счётчик/текст |
| `--open-gui` | `modman-gui --updates` запускается при наличии обновлений, не запускается без них |
| `UPDATE_CHECK_OPEN_GUI=1` | `modman-gui` запускается из конфига |
| `UPDATE_CHECK_ON_LOGIN=0` | `modman` не вызывается вообще, `notify-send` не вызывается |
| Только check-only | `--update` никогда не передаётся в `modman`, `--update-all` тоже |
| `MODMAN_BIN` | используется альтернативный путь |
| `MODMAN_CONF` | sourcing конфига, `UPDATE_CHECK_ON_LOGIN=0` из конфига, отсутствующий конфиг |
| Деградация | non-zero exit modman → helper exit 0, отсутствие `notify-send` → exit 0 |
| Множественные строки | `notify-send` вызывается ровно один раз |

### `modman-remove-local.bats` (13 тестов)

Тесты для `--remove-local`: удаление `.pfs` файлов, `.old` файлов, machine-режим.

### `modman-loadcmd.bats` (4 теста)

Тесты для `--load-cmd`: переопределение команды загрузки.

### `modman-loaded-mode.bats` (4 теста)

Тесты для `--list-loaded-before`/`--list-loaded-after` в machine-режиме.

### `modman-pfsload-mode.bats` (4 теста)

Тесты для `--lower`/`--upper`/`--toram` флагов.

### `modman-progress-file.bats` (5 тестов)

Тесты для `--progress-file` флага.

## Переменные окружения для bats-тестов

### Общие

| Переменная | Описание |
|------------|----------|
| `MODMAN_BIN` | Путь до тестируемого `modman` (по умолчанию `tests/bats/../../modman`) |
| `DOWNLOAD_DIR` | Каталог локальных `.pfs` для `--list-local` |

### Для `modman-updates.bats`

| Переменная | Описание |
|------------|----------|
| `MODMAN_TEST_ROOTAUFS2_ROOT` | Корень fake rootaufs2 (base/, modules/, optional/, runtime/) |
| `MODMAN_PROC_CMDLINE` | Путь к fake `/proc/cmdline` |
| `MODMAN_PROC_MOUNTS` | Путь к fake `/proc/mounts` |
| `PFS_LAYER_STATE_DIR` | Каталог `.layer`-маркеров для overlay runtime |
| `PFS_BIN` | Имя/путь fake `pfs` бинаря |
| `PFSINFO_BIN` | Имя/путь fake `pfsinfo` бинаря |
| `EXTRAMOD` | Имя extra-модульного каталога (из cmdline `extramod=`) |
| `UPDATE_BLACKLIST_FILE` | Путь к файлу blacklist (изолирован на тест) |

### Для `modman-update-check.bats`

| Переменная | Описание |
|------------|----------|
| `MODMAN_BIN` | Путь до fake `modman` (логирует вызовы) |
| `MODMAN_CONF` | Путь до fake `modman.conf` |
| `UPDATE_CHECK_ON_LOGIN` | `0` = пропустить проверку полностью |
| `UPDATE_CHECK_OPEN_GUI` | `1` = открыть `modman-gui --updates` при наличии обновлений |

## Текущее состояние тестов

```
bats tests/bats: 101 тест, 0 ошибок (2 skip — pfs недоступен в окружении)
  modman-loadcmd.bats:       4 теста
  modman-loaded-mode.bats:   4 теста
  modman-pfsload-mode.bats:  4 теста
  modman-progress-file.bats: 5 тестов
  modman-remove-local.bats: 13 тестов
  modman-selftest-runner.bats: 1 тест
  modman-smoke.bats:         5 тестов
  modman-update-check.bats: 21 тест
  modman-updates.bats:      44 теста
```
