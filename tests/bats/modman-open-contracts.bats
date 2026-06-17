#!/usr/bin/env bats

load './modman-open-fixtures.bash'

setup() {
    setup_modman_open_fixtures
}

teardown() {
    teardown_modman_open_fixtures
}

@test "modman-open contract: file info valid fixture returns machine file row" {
    run "$MODMAN_BIN" --machine -Qp "$SAMPLE_PFS"
    [ "$status" -eq 0 ]

    IFS=$'\t' read -r kind path name format loaded_state size fs_detail modules deps extra <<<"$output"
    [ "$kind" = "file" ]
    [ "$path" = "$SAMPLE_PFS" ]
    [ "$name" = "sample" ]
    [ "$format" = "squashfs" ]
    [ "$loaded_state" = "local" ]
    [ "$size" = "4.0K" ]
    [ "$fs_detail" = "squashfs xz" ]
    [ "$modules" = "app:core" ]
    [ "$deps" = "gtk3 libfoo" ]
    [ -z "$extra" ]
}

@test "modman-open contract: file info missing file returns machine error row" {
    local missing_path
    missing_path="$SANDBOX_DIR/does-not-exist.pfs"

    run "$MODMAN_BIN" --machine -Qp "$missing_path"
    [ "$status" -eq 1 ]
    [[ "$output" == $'error\t'* ]]
}

@test "modman-open contract: attach uses machine -S with absolute path" {
    local attach_path
    local last
    attach_path="$DOWNLOAD_DIR/demo-1.0.pfs"
    cp "$SAMPLE_PFS" "$attach_path"

    run "$MODMAN_BIN" --machine -S "$attach_path"
    [ "$status" -eq 0 ]
    [ "$output" = $'ok\tattached' ]

    mapfile -t _calls <"$FAKE_MODMAN_LOG"
    [ "${#_calls[@]}" -ge 1 ]
    last="${_calls[${#_calls[@]}-1]}"
    [ "$last" = "--machine -S $attach_path" ]

    local attached_arg
    attached_arg="${last#* -S }"
    [ "$attached_arg" = "$attach_path" ]
    [[ "$attached_arg" = /* ]]
}

@test "modman-open contract: attach options are modman flags before -S" {
    local attach_path
    local last
    attach_path="$DOWNLOAD_DIR/demo-lower-ram.pfs"
    cp "$SAMPLE_PFS" "$attach_path"

    run "$MODMAN_BIN" --machine --lower --toram -S "$attach_path"
    [ "$status" -eq 0 ]
    [ "$output" = $'ok\tattached' ]

    mapfile -t _calls <"$FAKE_MODMAN_LOG"
    [ "${#_calls[@]}" -ge 1 ]
    last="${_calls[${#_calls[@]}-1]}"
    [ "$last" = "--machine --lower --toram -S $attach_path" ]
}

@test "modman-open contract: unload uses machine -R derived name without slash" {
    local unload_path
    local unload_name
    local last
    unload_path="$DOWNLOAD_DIR/foo-1.0.pfs"
    unload_name="$(basename "$unload_path" .pfs)"
    cp "$SAMPLE_PFS" "$unload_path"

    run "$MODMAN_BIN" --machine -R "$unload_name"
    [ "$status" -eq 0 ]
    [ "$output" = $'ok\tunloaded' ]

    mapfile -t _calls <"$FAKE_MODMAN_LOG"
    [ "${#_calls[@]}" -ge 1 ]
    last="${_calls[${#_calls[@]}-1]}"
    [ "$last" = "--machine -R $unload_name" ]

    local remove_arg
    remove_arg="${last#* -R }"
    [ "$remove_arg" = "$unload_name" ]
    [[ "$remove_arg" != */* ]]
}

@test "modman-open sources do not call direct pfs runtime helpers" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n "\\b(pfsload|pfsunload|pfsinfo|pfsextract|fixmenus|mkfontscale|mkfontdir|sfs_event_add|sfs_event_rem|klsof)\\b" \
        "$repo_root/src/ui.c" \
        "$repo_root/src/open_ui.c" \
        "$repo_root/include/ui.h" \
        "$repo_root/include/open_ui.h"
    
    [ "$status" -eq 1 ]
}

@test "modman-gui source-contract: uses canonical connected/disconnected wording" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n "Подключен|Отключен|_Подключить|_Отключить" \
        "$repo_root/src/ui.c" \
        "$repo_root/src/open_ui.c"
    [ "$status" -eq 0 ]

    run rg -n "Загруженные|Загружен|загружен|Выгружен|выгружен|_Загрузить|_Выгрузить" \
        "$repo_root/src/ui.c" \
        "$repo_root/src/open_ui.c"
    [ "$status" -eq 1 ]
}

@test "modman-gui Module menu opens selected file through modman-open" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n "MODMAN_OPEN_BIN|UI_MODMAN_OPEN_FALLBACK|ui_open_file_with_modman_open|Opening module in modman-open" \
        "$repo_root/src/ui.c"
    [ "$status" -eq 0 ]

    run rg -n "Loading selected module" \
        "$repo_root/src/ui.c"
    [ "$status" -eq 1 ]
}

@test "modman-gui Local actions use safe opener argv and neutral status" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n "MODMAN_OPEN_BIN|UI_MODMAN_OPEN_FALLBACK|on_toolbar_open_local_clicked|Открытие файла…|g_subprocess_newv" \
        "$repo_root/src/ui.c"
    [ "$status" -eq 0 ]

    [[ "$output" == *"MODMAN_OPEN_BIN"* ]]
    [[ "$output" == *"on_toolbar_open_local_clicked"* ]]
    [[ "$output" == *"Открытие файла…"* ]]
    [[ "$output" == *"g_subprocess_newv"* ]]

    run rg -n "btn-open-local|btn-reveal-local" "$repo_root/src/ui.c"
    [ "$status" -eq 1 ]

    run rg -n "g_spawn_command_line|system\(|modman-open .*%s|xdg-open .*%s" \
        "$repo_root/src/ui.c"
    [ "$status" -eq 1 ]
}

@test "modman-gui Local reveal opens selected file directory safely" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n "on_toolbar_reveal_local_clicked|g_file_test\(.*G_FILE_TEST_EXISTS|g_path_get_dirname|argv\[0\] = \"xdg-open\"|ui_launch_local_opener_argv" \
        "$repo_root/src/ui.c"
    [ "$status" -eq 0 ]

    [[ "$output" == *"on_toolbar_reveal_local_clicked"* ]]
    [[ "$output" == *"argv[0] = \"xdg-open\""* ]]
    [[ "$output" == *"ui_launch_local_opener_argv"* ]]
    [[ "$output" == *"g_path_get_dirname"* ]]
}

@test "modman-gui context menu uses gtk-open and omits RAM attach variants" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n "UI_ACTION_ICON_OPEN_LOCAL_FILE:[[:space:]]*$|return \"gtk-open\"|Attach to RAM|Attach to lower layer \+ RAM|Attach to upper layer \+ RAM" \
        "$repo_root/src/ui.c"
    [ "$status" -eq 0 ]
    [[ "$output" == *"gtk-open"* ]]
    [[ "$output" != *"Attach to RAM"* ]]
    [[ "$output" != *"Attach to lower layer + RAM"* ]]
    [[ "$output" != *"Attach to upper layer + RAM"* ]]
}

@test "modman-gui Inet and Local context menus put attach first and omit lower-layer attach" {
    local repo_root connect_line download_line
    repo_root="${BATS_TEST_DIRNAME}/../.."

    connect_line=$(rg -n 'ui_make_menu_item_with_icon\(ui_action_icon_name\(UI_ACTION_ICON_CONNECT\), _\("Подключить"\)\)' \
        "$repo_root/src/ui.c" | cut -d: -f1 | head -n 1)
    download_line=$(rg -n 'ui_make_menu_item_with_icon\("folder-download", _\("Скачать"\)\)' \
        "$repo_root/src/ui.c" | cut -d: -f1 | head -n 1)

    [ -n "$connect_line" ]
    [ -n "$download_line" ]
    [ "$connect_line" -lt "$download_line" ]

    run rg -n 'Attach to lower layer|pfsload_allow_lower' "$repo_root/src/ui.c" "$repo_root/po/ru.po"
    [ "$status" -eq 1 ]
}

@test "modman-gui Inet context menu can download and open through modman-open" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n 'Скачать и открыть|on_context_menu_download_open_activate|open_after_download|ui_downloaded_module_path_dup|backend_list_local_sync\(NULL\)|g_path_get_basename|g_path_is_absolute|contains_match_count|strstr\(basename, module_name\)|Downloaded module path was not found in the local module list\.|ui_open_file_with_modman_open' \
        "$repo_root/src/ui.c" "$repo_root/po/ru.po"
    [ "$status" -eq 0 ]

    [[ "$output" == *"Скачать и открыть"* ]]
    [[ "$output" == *"on_context_menu_download_open_activate"* ]]
    [[ "$output" == *"open_after_download"* ]]
    [[ "$output" == *"ui_downloaded_module_path_dup"* ]]
    [[ "$output" == *"backend_list_local_sync(NULL)"* ]]
    [[ "$output" == *"g_path_get_basename"* ]]
    [[ "$output" == *"g_path_is_absolute"* ]]
    [[ "$output" == *"contains_match_count"* ]]
    [[ "$output" == *"strstr(basename, module_name)"* ]]
    [[ "$output" == *"Downloaded module path was not found in the local module list."* ]]
    [[ "$output" == *"ui_open_file_with_modman_open"* ]]

    run rg -n 'g_build_filename\(app->conf.download_dir' "$repo_root/src/ui.c"
    [ "$status" -eq 1 ]

    run rg -n 'backend_list_local_sync|run_machine_list_sync_for\(argv, MODULE_TSV_FORMAT_LOCAL|backend_search_sync|run_machine_list_sync_for\(argv, MODULE_TSV_FORMAT_INET' \
        "$repo_root/src/backend.c"
    [ "$status" -eq 0 ]
    [[ "$output" == *"backend_list_local_sync"* ]]
    [[ "$output" == *"MODULE_TSV_FORMAT_LOCAL"* ]]
    [[ "$output" == *"backend_search_sync"* ]]
    [[ "$output" == *"MODULE_TSV_FORMAT_INET"* ]]
}

@test "modman-open desktop name remains file opener identity" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n "^Name=modman-open$|^Name\[ru\]=modman-open$|^Icon=package-x-generic$|^StartupWMClass=modman-open$" \
        "$repo_root/modman-open.desktop"
    [ "$status" -eq 0 ]

    [[ "$output" == *"Name=modman-open"* ]]
    [[ "$output" == *"Name[ru]=modman-open"* ]]
    [[ "$output" == *"Icon=package-x-generic"* ]]
    [[ "$output" == *"StartupWMClass=modman-open"* ]]
}

@test "modman-open GTK source identity matches modman-gui for sfwbar" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n 'g_set_prgname\("modman-gui"\)|g_set_application_name\(_\("PFS Module Manager"\)\)|gdk_set_program_class\("modman-gui"\)|gtk_dialog_new_with_buttons\(_\("PFS Module Manager"\)' \
        "$repo_root/src/open_main.c" \
        "$repo_root/src/open_ui.c"
    [ "$status" -eq 0 ]

    [[ "$output" == *'g_set_prgname("modman-gui")'* ]]
    [[ "$output" == *'g_set_application_name(_("PFS Module Manager"))'* ]]
    [[ "$output" == *'gdk_set_program_class("modman-gui")'* ]]
    [[ "$output" == *'gtk_dialog_new_with_buttons(_("PFS Module Manager")'* ]]
}

@test "modman-gui source-contract: default icon uses package generic" {
    local repo_root
    repo_root="${BATS_TEST_DIRNAME}/../.."

    run rg -n 'gtk_window_set_default_icon_name\("package-x-generic"\)' \
        "$repo_root/src/main.c"
    [ "$status" -eq 0 ]

    [[ "$output" == *'gtk_window_set_default_icon_name("package-x-generic")'* ]]
}
