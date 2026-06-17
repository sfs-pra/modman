# bats smoke harness

## Что проверяется

- базовый `--help`
- машинный вывод для `--list-loaded-after`
- пустой результат для заведомо несуществующего поиска
- валидация ошибки для `-S invalid;name`
- локальный список при подмене `DOWNLOAD_DIR`

## Запуск

```bash
bats tests/bats/modman-smoke.bats
```

## Переменные окружения

- `MODMAN_BIN` — путь до тестируемого бинаря/скрипта (по умолчанию `/workspace/modman`)
- `DOWNLOAD_DIR` — каталог локальных `.pfs` для теста `--list-local`

Если `MODMAN_BIN` недоступен, harness автоматически переключится на
`tests/bats/fixtures/fake-modman.sh`.
