# modman Architecture Notes

## GUI Contracts From Tasks 4 Through 9

The GUI keeps runtime operations behind the `modman` backend. The main manager
uses source-visible contracts for details, protected actions, search states,
Local file actions, and desktop identity.

Local and System selections may request nested `.pfs` details through
`modman --machine -Qp <path>`. The details pane shows `Модули внутри:` only for
those tabs. Direct nested entries are sorted for display. Empty content uses the
exact placeholder `Модули внутри: —`.

Loaded and Online selections don't request nested `.pfs` details. They hide the
nested modules row and must not call `-Qp` for selection changes.

System rows stay visible, but System unload is protected. The disabled unload
tooltip is `Системный слой нельзя отключить из этого списка`. Runtime rows in
Подключенные keep the normal `_Отключить` path through `modman --machine -R`.

Online search has explicit neutral states: `Введите запрос`, `Поиск…`,
`Ничего не найдено`, and `Ошибка поиска: …`. Empty search input is handled in
the UI and doesn't call the backend.

Local open and reveal actions are Local-only. Open calls `modman-open <path>`
through argv. Reveal calls `xdg-open <directory>` through argv and is disabled
with `Не найден xdg-open` when the tool is absent.

`modman-open.desktop` keeps file-opener identity. The GTK source identity stays
aligned with `modman-gui` for sfwbar grouping, while the desktop entry keeps
`Name=modman-open` and `StartupWMClass=modman-open`.
