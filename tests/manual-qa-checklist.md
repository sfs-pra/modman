# Manual QA Checklist for modman GUI

## Scope

This checklist covers `modman-open`, the GTK single-file opener for local
`.pfs` modules, plus focused smoke checks for the main `modman-gui` manager
changes from Tasks 4 through 9.

It does not cover package extraction, package install flows, safe mode, upper
layer, or batch selection.

All attach and unload actions must go through the `modman` backend. The GTK
opener must not call pfs runtime helpers directly.

## Environment Notes

- Backend-only checks can run in a headless agent shell when `modman` or
  `MODMAN_BIN` test fixtures are available.
- GTK dialog checks need a working display, for example a desktop session or
  `xvfb-run` with GTK installed.
- File-manager open-with checks need the desktop entry and MIME cache installed
  in the test session.
- Use a disposable `.pfs` fixture. Don't attach or unload a system module from
  a daily-driver session.

## Scenario 1, Help Shows Single-File Usage

- Preconditions: `modman-open` is built or installed.
- Steps: Run `modman-open --help`, then read the usage text.
- Expected result: Output includes `modman-open FILE` or
  `Usage: modman-open FILE.pfs`. Help describes one local `.pfs` file. Help
  does not mention batch mode, package extraction, or full manager tabs.
- Evidence: Capture command output and exit status.

## Scenario 2, Valid File Opens Dialog

- Preconditions: A valid local `.pfs` fixture exists, for example
  `/tmp/modman-open-valid.pfs`. GTK display is available. `modman` or
  `MODMAN_BIN` can return file information for the fixture.
- Steps: Run `modman-open /tmp/modman-open-valid.pfs`, then observe the dialog
  file path, derived module name, format, size, lower/RAM choices, and action
  buttons.
- Expected result: The dialog opens for the selected file. The file is treated
  as one local PFS module. The dialog offers Attach when the backend reports
  the module is not loaded. It shows size and filesystem detail when available.
  No package extraction or install checkbox is shown.
- Evidence: Screenshot or widget dump with the file path, derived module name,
  and Attach action.

## Scenario 3, Invalid File Is Rejected

- Preconditions: GTK display is available. `/tmp/modman-open-invalid.txt`
  exists and is not a `.pfs` file, or `/tmp/modman-open-missing.pfs` does not
  exist.
- Steps: Run `modman-open /tmp/modman-open-invalid.txt`, then run
  `modman-open /tmp/modman-open-missing.pfs`.
- Expected result: The invalid file path is rejected. The missing file path is
  rejected. Attach and unload actions are not offered for either input. The
  error message comes from the backend validation path or matches it.
- Evidence: Screenshot or captured stderr for each rejection.

## Scenario 4, Attach Uses modman Backend

- Preconditions: A valid local `.pfs` fixture exists. The backend reports the
  fixture as not loaded. `MODMAN_BIN` points to a fixture backend that logs
  argv, or system logs can show backend calls. GTK display is available.
- Steps: Run
  `MODMAN_BIN=/path/to/fake-modman modman-open /tmp/modman-open-valid.pfs`,
  click Attach, then inspect the fake backend log.
- Expected result: The Attach action calls `modman` with the selected local file
  path. If lower/RAM choices are selected, they are passed as `--lower` and
  `--toram` before `-S`. No direct pfs runtime helper is called by
  `modman-open`. The dialog reports success or the backend's attach failure.
- Evidence: Fake backend argv log and screenshot after Attach.

## Scenario 5, Unload Uses Derived Module Name

- Preconditions: A valid local `.pfs` fixture exists. The backend reports the
  fixture as loaded and returns a derived module name. `MODMAN_BIN` points to a
  fixture backend that logs argv. GTK display is available.
- Steps: Run
  `MODMAN_BIN=/path/to/fake-modman modman-open /tmp/modman-open-valid.pfs`,
  click Unload, then inspect the fake backend log.
- Expected result: The dialog offers Unload instead of Attach when the file is
  already loaded. The unload request uses the derived module name, not a guessed
  UI string. The operation goes through `modman` only.
- Evidence: Fake backend argv log showing the unload command and derived module
  name.

## Scenario 6, File-Manager Open-With Uses application/x-pfs

- Preconditions: `modman-open.desktop` is installed. Shared MIME XML is
  installed and MIME caches are updated. A graphical file manager is available.
- Steps: In the file manager, select one `.pfs` file. Open the file's Open With
  menu. Choose `modman-open`. Check the installed desktop entry if the file
  manager does not show it.
- Expected result: The `.pfs` file is associated with `application/x-pfs`. The
  open-with handler is `modman-open.desktop`. The desktop entry uses
  `Exec=modman-open %f`, not `%F`. Selecting one file opens the same single-file
  dialog as the command-line flow.
- Evidence: Screenshot of the Open With menu or command output from
  `xdg-mime query filetype` and desktop entry inspection.

## Scenario 7, Busy Unload Failure Is Reported

- Preconditions: A valid local `.pfs` fixture exists. The backend reports the
  fixture as loaded. `MODMAN_BIN` points to a fixture backend that fails unload
  with a busy or dependency error. GTK display is available.
- Steps: Run
  `MODMAN_BIN=/path/to/fake-busy-modman modman-open /tmp/modman-open-valid.pfs`,
  click Unload, then observe the dialog state after the backend failure.
- Expected result: The busy unload failure is shown to the user. The module
  remains treated as loaded. The dialog does not claim that unload succeeded.
  The utility does not retry with direct pfs runtime calls.
- Evidence: Fake backend argv log, failure output, and screenshot of the error
  state.

## Scenario 8, Packaging Validators Are Recorded

- Preconditions: The source tree contains `modman-open.desktop` and
  `data/mime/packages/modman-open.xml`.
- Steps: Run `command -v desktop-file-validate`. If present, run
  `desktop-file-validate modman-open.desktop`. Run
  `command -v update-mime-database`. If present, copy the MIME XML into a
  temporary `packages/` directory and run `update-mime-database -n` against
  that temporary MIME root. Run `git diff --check` for the changed docs.
- Expected result: Present validators run and exit 0. Missing validators are
  recorded as environment-gated blockers with their exact `command -v` result.
  The diff check exits 0.
- Evidence: Save command output to `.omo/evidence/task-9-packaging-docs.txt`.

## Scenario 9, Local and System Details Show Nested Modules

- Preconditions: `modman-gui` is built with a fake `MODMAN_BIN` that returns
  `--machine -Qp <path>` file info. The fake Local and System rows point to
  `.pfs` files with nested module names in unsorted order.
- Steps: Open the Local tab, select the fake `.pfs`, then open the System tab
  and select the fake system `.pfs`.
- Expected result: Each details pane shows `Модули внутри:` followed by direct
  nested entries sorted by display name. If the backend returns no nested
  modules, the label is exactly `Модули внутри: —`.
- Evidence: Screenshot or widget dump for both tabs, plus fake backend argv
  showing `--machine -Qp <path>` for Local and System selections.

## Scenario 10, Loaded and Online Details Do Not Inspect Nested Modules

- Preconditions: `modman-gui` is built with a fake `MODMAN_BIN` that logs argv.
  Loaded and Online rows are available.
- Steps: Select one row on Подключенные, then select one row on Инет.
- Expected result: The nested modules row is hidden for both selections. The
  fake backend log contains no `-Qp` calls for these tabs.
- Evidence: Widget dump or screenshot for both selections and the fake backend
  log.

## Scenario 11, Protected System Action Smoke

- Preconditions: System tab has at least one selectable base module row.
- Steps: Open Система, select the base module, then inspect the unload toolbar
  action.
- Expected result: `_Отключить` is disabled. Its tooltip is exactly
  `Системный слой нельзя отключить из этого списка`. A runtime module on
  Подключенные still enables `_Отключить` and calls `modman --machine -R`.
- Evidence: Screenshot or widget dump for System, then fake backend argv for
  the Подключенные unload smoke.

## Scenario 12, Search States Smoke

- Preconditions: Online search uses a fake backend that can return one result,
  no results, and a search error.
- Steps: Open Инет. Submit a blank query, then `alpha`, then a query with no
  matches, then a query that makes the fake backend fail.
- Expected result: Blank input shows `Введите запрос` and does not call the
  backend. Pending search shows `Поиск…`. Empty success shows
  `Ничего не найдено`. Backend failure shows `Ошибка поиска: …` in the neutral
  status area.
- Evidence: Widget dump for each state and fake backend argv proving blank
  input did not run `-Ss`.

## Scenario 13, Local Open and Reveal Actions Smoke

- Preconditions: Local tab has one `.pfs` row with a path containing a space.
  `MODMAN_OPEN_BIN` points to an argv-logging fake `modman-open`. `PATH` can be
  switched between an argv-logging fake `xdg-open` and an empty directory.
- Steps: Select the Local row, click the open action, then click reveal with
  fake `xdg-open` present. Repeat reveal with `xdg-open` absent.
- Expected result: Open calls `modman-open <path>` through argv and shows
  `Открытие файла…`, then `Готово` or `Ошибка: …`. Reveal calls
  `xdg-open <directory>` through argv. When `xdg-open` is absent, reveal is
  disabled with tooltip `Не найден xdg-open`.
- Evidence: Fake argv logs and widget dump for the missing `xdg-open` state.

## Scenario 14, Icon and sfwbar Identity Smoke

- Preconditions: `modman-gui`, `modman-open.desktop`, and the source tree are
  available.
- Steps: Inspect `modman-open.desktop`, then launch `modman-open` and inspect
  the GTK program class and window title through the desktop shell or widget
  dump.
- Expected result: `modman-open.desktop` keeps `Name=modman-open`,
  `Name[ru]=modman-open`, `Icon=package-x-generic`, and
  `StartupWMClass=modman-open`. GTK source identity for the opener matches
  `modman-gui` where sfwbar groups windows.
- Evidence: Desktop entry grep output and screenshot or widget dump.

## Scenario 15, Neutral Error and Status Smoke

- Preconditions: Fake backend can fail attach, detach, Local open, reveal, and
  Online search without crashing.
- Steps: Trigger one failure for each path.
- Expected result: Operation paths use neutral status text such as `Готово`,
  `Ошибка: …`, and `Отменено`. Search failure uses `Ошибка поиска: …`. No path
  reintroduces legacy load or unload wording from the pre-canonical UI.
- Evidence: Widget dump for each failed path and source grep output for stale
  wording.
