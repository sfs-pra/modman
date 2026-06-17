# modman-open packaging and MIME checks

`modman-open` is a small GTK3 opener for one local `.pfs` file from a
file manager. Its supported actions are inspect, attach and unload through
`modman`. It ships inside the `modman-gui` package, but it is not a replacement
for the full manager. It has no tabs, search view, catalog UI, update flow,
package checkbox install flow, or package extraction flow.

## Installed artifacts

The `modman-gui` package must install these user visible integration files:

- `modman-open` to the executable path used by the package.
- `modman-open.desktop` with `Exec=modman-open %f`.
- `modman-open.desktop` with `MimeType=application/x-pfs;`.
- `data/mime/packages/modman-open.xml` declaring `application/x-pfs`.

`%f` means single-file file-manager integration. Don't document or package
this utility as a batch opener. Don't set it as the global default handler
from package scripts. Let the desktop environment or the user choose the
preferred handler.

## Scope guardrails

`modman-open` may inspect a local `.pfs` path, show status, size and format
metadata, attach it, and unload the derived module name. Lower-layer and RAM
copy choices are allowed only as `modman` flags. All runtime work goes through
`modman`.

It must not promise or expose these legacy `open_pfs` behaviors:

- package checkbox install or partial package install;
- package extraction or `pfsextract` UI;
- upper mode or safe mode controls;
- search, catalog, update, or full module-manager behavior;
- opening several files in one command.

## Validator commands

Run these checks when the tools exist in the build or QA environment:

```bash
desktop-file-validate modman-open.desktop
```

```bash
mkdir -p .omo/tmp/mime-check/packages
cp data/mime/packages/modman-open.xml .omo/tmp/mime-check/packages/
update-mime-database -n .omo/tmp/mime-check
```

```bash
git diff --check -- docs/modman-open-packaging.md \
  docs/modman.dokuwiki tests/README.md tests/manual-qa-checklist.md
```

If `desktop-file-validate`, `update-mime-database`, or `markdownlint` is
absent, record the exact `command -v` miss in task evidence. Don't mark the
check as passed when the tool is missing.

Use `markdownlint` only when it is already installed:

```bash
markdownlint docs/modman-open-packaging.md tests/manual-qa-checklist.md \
  tests/README.md
```

## Search checks

After editing docs, confirm the reduced scope and packaging terms are
searchable:

```bash
rg -n "modman-open.desktop|application/x-pfs|attach|unload" docs tests \
  modman-open.desktop data/mime/packages/modman-open.xml
```

The search result should show `attach` and `unload` for the supported flow.
It may mention package extraction only as an unsupported legacy behavior.
