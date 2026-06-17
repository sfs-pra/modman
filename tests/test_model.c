#if __has_include(<check.h>)
#include <check.h>
#include <glib.h>

#include "../include/model.h"

START_TEST(test_update_row_parses_normal_risk)
{
    GError *error = NULL;
    const char *line =
        "update\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\t"
        "app\t1.0\t1.1\t/opt/modules/app-1.0.pfs\tapp-1.1.pfs\trepo-main\tnormal\t0\tready";

    ModuleUpdateInfo *info = module_update_info_from_tsv_line(line, &error);
    ck_assert_ptr_nonnull(info);
    ck_assert_ptr_null(error);
    ck_assert_str_eq(info->name, "app");
    ck_assert_str_eq(info->old_version, "1.0");
    ck_assert_str_eq(info->new_version, "1.1");
    ck_assert_str_eq(info->risk, "normal");
    ck_assert_int_eq(info->reboot_required, FALSE);

    module_update_info_free(info);
}
END_TEST

START_TEST(test_update_row_parses_system_with_reboot_message)
{
    GError *error = NULL;
    const char *line =
        "update\tabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd\t"
        "kernel\t6.1\t6.2\t/mnt/home/base/kernel-6.1.pfs\tkernel-6.2.pfs\trepo-base\tsystem\t1\t"
        "нужна перезагрузка";

    ModuleUpdateInfo *info = module_update_info_from_tsv_line(line, &error);
    ck_assert_ptr_nonnull(info);
    ck_assert_ptr_null(error);
    ck_assert_str_eq(info->risk, "system");
    ck_assert_int_eq(info->reboot_required, TRUE);
    ck_assert_ptr_nonnull(strstr(info->message, "нужна перезагрузка"));

    module_update_info_free(info);
}
END_TEST

START_TEST(test_update_row_parses_empty_trailing_message)
{
    GError *error = NULL;
    const char *line =
        "update\tcd095cb335c021e50154c34f4c70855dd86247b8db4112ce4e1ec7955a01ebf4\t"
        "firefox-bin-gtk3-p\t130.0.0-sf01\t150.0.1-sf01\t"
        "/run/archroot/root_ro/zz2601/optional/firefox-bin-gtk3-p-130.0.0_64-sf01.pfs\t"
        "firefox-bin-gtk3-p-150.0.1_64-sf01.pfs\t"
        "http://mirror.yandex.ru/puppyrus/puppyrus-a64/pfs-portable\tnormal\t0";

    ModuleUpdateInfo *info = module_update_info_from_tsv_line(line, &error);
    ck_assert_ptr_nonnull(info);
    ck_assert_ptr_null(error);
    ck_assert_str_eq(info->name, "firefox-bin-gtk3-p");
    ck_assert_str_eq(info->old_version, "130.0.0-sf01");
    ck_assert_str_eq(info->new_version, "150.0.1-sf01");
    ck_assert_str_eq(info->message, "");

    module_update_info_free(info);
}
END_TEST

START_TEST(test_update_row_unescapes_message_tabs_and_newlines)
{
    GError *error = NULL;
    const char *line =
        "update\t1111111111111111111111111111111111111111111111111111111111111111\t"
        "gfx\t2.0\t2.1\t/opt/modules/gfx-2.0.pfs\tgfx-2.1.pfs\trepo-gfx\tloaded\t0\t"
        "line1\\tline2\\nline3";

    ModuleUpdateInfo *info = module_update_info_from_tsv_line(line, &error);
    ck_assert_ptr_nonnull(info);
    ck_assert_ptr_null(error);
    ck_assert_str_eq(info->message, "line1\tline2\nline3");

    module_update_info_free(info);
}
END_TEST

START_TEST(test_update_row_rejects_malformed_field_count)
{
    GError *error = NULL;
    const char *line =
        "update\t2222222222222222222222222222222222222222222222222222222222222222\t"
        "app\t1.0\t1.1\t/path\tfile.pfs\trepo\tnormal";

    ModuleUpdateInfo *info = module_update_info_from_tsv_line(line, &error);
    ck_assert_ptr_null(info);
    ck_assert_ptr_nonnull(error);
    ck_assert_ptr_nonnull(strstr(error->message, "10 or 11 fields"));
    g_clear_error(&error);
}
END_TEST

START_TEST(test_update_row_rejects_invalid_risk)
{
    GError *error = NULL;
    const char *line =
        "update\t3333333333333333333333333333333333333333333333333333333333333333\t"
        "app\t1.0\t1.1\t/path\tfile.pfs\trepo\tunsafe\t0\tmsg";

    ModuleUpdateInfo *info = module_update_info_from_tsv_line(line, &error);
    ck_assert_ptr_null(info);
    ck_assert_ptr_nonnull(error);
    ck_assert_ptr_nonnull(strstr(error->message, "risk"));
    g_clear_error(&error);
}
END_TEST

START_TEST(test_update_row_rejects_invalid_reboot_required)
{
    GError *error = NULL;
    const char *line =
        "update\t4444444444444444444444444444444444444444444444444444444444444444\t"
        "app\t1.0\t1.1\t/path\tfile.pfs\trepo\tnormal\t2\tmsg";

    ModuleUpdateInfo *info = module_update_info_from_tsv_line(line, &error);
    ck_assert_ptr_null(info);
    ck_assert_ptr_nonnull(error);
    ck_assert_ptr_nonnull(strstr(error->message, "reboot_required"));
    g_clear_error(&error);
}
END_TEST

static Suite *model_suite(void)
{
    Suite *suite = suite_create("model");
    TCase *core = tcase_create("update-parser");

    tcase_add_test(core, test_update_row_parses_normal_risk);
    tcase_add_test(core, test_update_row_parses_system_with_reboot_message);
    tcase_add_test(core, test_update_row_parses_empty_trailing_message);
    tcase_add_test(core, test_update_row_unescapes_message_tabs_and_newlines);
    tcase_add_test(core, test_update_row_rejects_malformed_field_count);
    tcase_add_test(core, test_update_row_rejects_invalid_risk);
    tcase_add_test(core, test_update_row_rejects_invalid_reboot_required);
    suite_add_tcase(suite, core);

    return suite;
}

int main(void)
{
    int failed = 0;
    Suite *suite = model_suite();
    SRunner *runner = srunner_create(suite);

    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return (failed == 0) ? 0 : 1;
}

#else

int main(void)
{
    return 0;
}

#endif
