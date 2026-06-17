CC = gcc
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LDFLAGS := $(shell pkg-config --libs glib-2.0)
GIO_CFLAGS := $(shell pkg-config --cflags gio-2.0)
GIO_LDFLAGS := $(shell pkg-config --libs gio-2.0)
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LDFLAGS := $(shell pkg-config --libs gtk+-3.0)

CFLAGS = -Wall -Wextra -g -fprofile-arcs -ftest-coverage -I../include $(GLIB_CFLAGS) $(GIO_CFLAGS)
LDFLAGS = -lcheck -lgcov -lm -lpthread $(GLIB_LDFLAGS) $(GIO_LDFLAGS)

TEST_SRCS = $(wildcard test_*.c)
TEST_BINS = $(TEST_SRCS:.c=)

all: $(TEST_BINS)

test_validators: test_validators.c ../src/validators.c
	$(CC) $(CFLAGS) $< ../src/validators.c -o $@ $(LDFLAGS)

test_model: test_model.c ../src/model.c
	$(CC) $(CFLAGS) $< ../src/model.c -o $@ $(LDFLAGS)

test_sample: test_sample.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_backend: test_backend.c ../src/backend.c ../src/model.c ../src/validators.c
	$(CC) $(CFLAGS) $< ../src/backend.c ../src/model.c ../src/validators.c -o $@ $(LDFLAGS)

test_conf: test_conf.c ../src/backend.c ../src/model.c ../src/validators.c
	$(CC) $(CFLAGS) $< ../src/backend.c ../src/model.c ../src/validators.c -o $@ $(LDFLAGS)

test_cancellable: test_cancellable.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_debounce: test_debounce.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test_details: test_details.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_confirm: test_confirm.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_infobar: test_infobar.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_a11y: test_a11y.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_shortcuts: test_shortcuts.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_resize: test_resize.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_highlight: test_highlight.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_autoload_ui: test_autoload_ui.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_capabilities: test_capabilities.c ../src/capabilities.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/capabilities.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

test_ui_debug: test_ui_debug.c ../src/ui_debug.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $< ../src/ui_debug.c ../src/ui.c ../src/backend.c ../src/model.c ../src/validators.c ../src/capabilities.c -o $@ $(LDFLAGS) $(GTK_LDFLAGS)

run: all
	@for t in $(TEST_BINS); do ./$$t || exit 1; done

coverage: run
	@gcov -r $(TEST_SRCS) 2>/dev/null | grep -E '(Lines|File)'

clean:
	rm -f $(TEST_BINS) *.gcda *.gcno *.gcov
