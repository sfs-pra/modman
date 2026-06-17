# Build Handoff

## Task 10 GUI QA Checks

Task 10 adds documentation and translation checks for the GUI work completed in
Tasks 4 through 9. It does not change runtime behavior.

Run these checks from the repository root when preparing the next regression
pass:

```bash
rg -n "Модули внутри|Локальные|Система|Инет|Подключенные" tests/manual-qa-checklist.md
```

```bash
rg -n "Загружен|Выгружен|загружен|выгружен|_Загрузить|_Выгрузить" \
  src include po tests
```

The second command should only report intentional negative source-contract test
patterns. Treat active source or translation matches as stale wording.

## Environment-Gated Tools

Record exact `command -v` output for `xgettext`, `msgmerge`, `msgfmt`, and
`markdownlint` in `.omo/evidence/`. Run gettext and Markdown checks only for
tools that are present. Missing tools are environment gates, not passing checks.

If C tests still link-fail because `check` is absent, keep that as an
environment gate in evidence. Bats source-contract checks can still validate the
GUI string and helper-boundary contracts.
