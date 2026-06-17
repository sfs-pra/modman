#if __has_include(<check.h>)
#include <check.h>

START_TEST(test_arithmetic_sanity)
{
    ck_assert_int_eq(1 + 1, 2);
}
END_TEST

static Suite *sample_suite(void)
{
    Suite *suite = suite_create("sample");
    TCase *tcase = tcase_create("core");

    tcase_add_test(tcase, test_arithmetic_sanity);
    suite_add_tcase(suite, tcase);

    return suite;
}

int main(void)
{
    int failed = 0;
    Suite *suite = sample_suite();
    SRunner *runner = srunner_create(suite);

    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);

    return (failed == 0) ? 0 : 1;
}

#else

int main(void)
{
    return (1 + 1 == 2) ? 0 : 1;
}

#endif
