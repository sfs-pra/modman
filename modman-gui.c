// mod-man.c
// Менеджер модулей (GUI Frontend на GTK3)

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#ifndef MODMAN_BIN
#define MODMAN_BIN "./modman"
#endif

#ifndef DOWNLOAD_DIR
#define DOWNLOAD_DIR "/home/ai/modman/modules"
#endif

// Структура для хранения виджетов
typedef struct {
    GtkWidget *window;
    GtkWidget *tree_view;
    GtkListStore *store;
    GtkWidget *notebook;
    GtkWidget *search_entry;
    GtkWidget *btn_action;
    GtkWidget *status_label;
    GtkTreeViewColumn *columns[4];
    GString *last_error;
} AppWidgets;

AppWidgets *widgets;

// Перечисления колонок таблицы
enum {
    COL_NAME = 0,
    COL_LAYER,
    COL_PATH,
    COL_DESC,
    COL_SIZE_NUM,
    COL_IS_LOCAL,    // TRUE если модуль локальный
    NUM_COLS
};

// Парсинг размера (K/M/G) в мегабайты
static gdouble parse_size_mb(const gchar *str) {
    if (!str || !*str) return 0.0;
    gdouble val = g_ascii_strtod(str, NULL);
    char suffix = '\0';
    const gchar *p = str;
    while (*p) { if (*p >= 'A' && *p <= 'Z') suffix = *p; p++; }
    if (suffix == 'G') return val * 1024.0;
    if (suffix == 'M') return val;
    if (suffix == 'K') return val / 1024.0;
    return val;
}

// Forward declaration
static gboolean is_module_in_store(const gchar *name);

// Функция для запуска bash-скрипта и парсинга TSV вывода
static void load_data(const gchar *command) {
    gtk_list_store_clear(widgets->store);
    
    gchar *full_cmd = g_strdup_printf("%s %s --machine", MODMAN_BIN, command);
    FILE *fp = popen(full_cmd, "r");
    g_free(full_cmd);
    
    if (!fp) {
        g_printerr("Ошибка запуска %s\n", MODMAN_BIN);
        return;
    }
    
    gchar line[2048];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0; // Удаляем перенос строки
        
        gchar **parts = g_strsplit(line, "\t", -1);
        if (g_strv_length(parts) >= 4) {
            // Пропускаем, если модуль уже есть в store (локальный)
            if (is_module_in_store(parts[0])) {
                g_strfreev(parts);
                continue;
            }
            
            GtkTreeIter iter;
            gdouble size_mb = parse_size_mb(parts[1]);
            gtk_list_store_append(widgets->store, &iter);
            gtk_list_store_set(widgets->store, &iter,
                               COL_NAME, parts[0],
                               COL_LAYER, parts[1],
                               COL_PATH, parts[2],
                               COL_DESC, parts[3],
                               COL_SIZE_NUM, size_mb,
                               COL_IS_LOCAL, FALSE,
                               -1);
            count++;
        }
        g_strfreev(parts);
    }
    pclose(fp);
}

// Загрузка локальных .pfs файлов с фильтрацией по поисковому запросу
static void load_local_files(const gchar *search_text) {
    gchar *cmd = g_strdup_printf("ls -1 \"%s\"/*.pfs 2>/dev/null", DOWNLOAD_DIR);
    FILE *fp = popen(cmd, "r");
    g_free(cmd);
    
    if (!fp) return;
    
    gchar line[1024];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        
        // line содержит полный путь: /home/ai/modman/modules/filename.pfs
        // Извлекаем только имя файла для фильтрации и отображения
        gchar *basename_ptr = strrchr(line, '/');
        if (!basename_ptr) basename_ptr = line;
        else basename_ptr++;  // Пропускаем сам '/'
        
        // Фильтрация по поисковому запросу
        if (search_text && strlen(search_text) > 0) {
            if (!strstr(basename_ptr, search_text)) {
                continue;
            }
        }
        
        // Получаем размер и дату файла (используем полный путь из line)
        struct stat st;
        gchar size_str[32] = "?";
        gchar date_str[32] = "?";
        
        if (stat(line, &st) == 0) {
            // Форматируем размер
            gdouble size_mb = st.st_size / (1024.0 * 1024.0);
            if (size_mb >= 1024.0) {
                g_snprintf(size_str, sizeof(size_str), "%.1fG", size_mb / 1024.0);
            } else {
                g_snprintf(size_str, sizeof(size_str), "%.1fM", size_mb);
            }
            
            // Форматируем дату
            struct tm *tm_info = localtime(&st.st_mtime);
            strftime(date_str, sizeof(date_str), "%Y-%m-%d", tm_info);
        }
        
        // Добавляем в store с флагом is_local=TRUE (используем basename для отображения)
        GtkTreeIter iter;
        gtk_list_store_append(widgets->store, &iter);
        gtk_list_store_set(widgets->store, &iter,
                           COL_NAME, basename_ptr,
                           COL_LAYER, size_str,
                           COL_PATH, date_str,
                           COL_DESC, "[Локально]",
                           COL_SIZE_NUM, 0.0,
                           COL_IS_LOCAL, TRUE,
                           -1);
    }
    pclose(fp);
}

// Проверка, есть ли модуль с таким именем уже в store
static gboolean is_module_in_store(const gchar *name) {
    GtkTreeModel *model = GTK_TREE_MODEL(widgets->store);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    
    while (valid) {
        gchar *existing_name;
        gtk_tree_model_get(model, &iter, COL_NAME, &existing_name, -1);
        gboolean match = (g_strcmp0(existing_name, name) == 0);
        g_free(existing_name);
        
        if (match) return TRUE;
        valid = gtk_tree_model_iter_next(model, &iter);
    }
    return FALSE;
}

static void update_column_headers(gint active) {
    const gchar *titles[4];
    
    if (active == 0) {
        titles[0] = "Название";
        titles[1] = "Слой";
        titles[2] = "Путь";
        titles[3] = "";
        gtk_tree_view_column_set_visible(widgets->columns[3], FALSE);
        gtk_tree_view_column_set_fixed_width(widgets->columns[1], 50);
    } else if (active == 1) {
        titles[0] = "Название";
        titles[1] = "Размер";
        titles[2] = "Дата";
        titles[3] = "Описание";
        gtk_tree_view_column_set_visible(widgets->columns[3], TRUE);
        gtk_tree_view_column_set_fixed_width(widgets->columns[1], 80);
        gtk_tree_view_column_set_fixed_width(widgets->columns[2], 100);
    } else if (active == 2) {
        titles[0] = "Название";
        titles[1] = "Размер";
        titles[2] = "Дата";
        titles[3] = "Путь";
        gtk_tree_view_column_set_visible(widgets->columns[3], TRUE);
        gtk_tree_view_column_set_fixed_width(widgets->columns[1], 80);
        gtk_tree_view_column_set_fixed_width(widgets->columns[2], 100);
    } else {
        titles[0] = "Название";
        titles[1] = "Слой";
        titles[2] = "Путь";
        titles[3] = "";
        gtk_tree_view_column_set_visible(widgets->columns[3], FALSE);
        gtk_tree_view_column_set_fixed_width(widgets->columns[1], 50);
    }
    
    for (int i = 0; i < 4; i++) {
        gtk_tree_view_column_set_title(widgets->columns[i], titles[i]);
    }
}

static void clear_status() {
    gtk_label_set_text(GTK_LABEL(widgets->status_label), "");
    g_string_free(widgets->last_error, TRUE);
    widgets->last_error = NULL;
    gtk_widget_queue_draw(widgets->status_label);
    while (gtk_events_pending()) gtk_main_iteration();
}

// Обработчик переключения вкладок - сброс поиска и статуса
static void on_tab_switched(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer data) {
    (void)notebook; (void)page; (void)data;
    gtk_entry_set_text(GTK_ENTRY(widgets->search_entry), "");
    clear_status();
}

// Обработчик переключения вкладок (Notebook)
static void on_page_changed(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer data) {
    (void)notebook; (void)page; (void)data;
    gint active = (gint)page_num;
    const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(widgets->search_entry));
    gboolean has_search = (search_text && strlen(search_text) > 0);
    
    if (active == 0) {
        gtk_button_set_label(GTK_BUTTON(widgets->btn_action), "Отключить");
        load_data("--list-loaded-after");
     } else if (active == 1) {
        gtk_button_set_label(GTK_BUTTON(widgets->btn_action), "Скачать и подключить");
        
        // Сначала загружаем из репозитория (очищает store)
        gchar *cmd = g_strdup_printf("-Ss \"%s\"", search_text);
        load_data(cmd);
        g_free(cmd);
        
        // ЗАТЕМ добавляем локальные файлы (если есть поиск)
        // Они будут добавлены в конец списка через append
        if (has_search) {
            load_local_files(search_text);
        }
    } else if (active == 2) {
        gtk_button_set_label(GTK_BUTTON(widgets->btn_action), "Подключить");
        if (has_search) {
            gchar *cmd = g_strdup_printf("ls -1 \"%s\"/*.pfs 2>/dev/null | xargs -I{} basename {}", DOWNLOAD_DIR);
            FILE *fp = popen(cmd, "r");
            if (fp) {
                gtk_list_store_clear(widgets->store);
                gchar line[1024];
                while (fgets(line, sizeof(line), fp)) {
                    line[strcspn(line, "\r\n")] = 0;
                    if (strstr(line, search_text)) {
                        GtkTreeIter iter;
                        gtk_list_store_append(widgets->store, &iter);
                        gtk_list_store_set(widgets->store, &iter, 0, line, -1);
                    }
                }
                pclose(fp);
            }
            g_free(cmd);
        } else {
            load_data("--list-local");
        }
    } else if (active == 3) {
        gtk_button_set_label(GTK_BUTTON(widgets->btn_action), "Отключить");
        load_data("--list-loaded-before");
    }
    
    // Локальная фильтрация для Подключенные (0) и Система (3)
    if (has_search && (active == 0 || active == 3)) {
        GtkTreeModel *model = GTK_TREE_MODEL(widgets->store);
        GtkTreeIter iter;
        gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
        while (valid) {
            gchar *name;
            gtk_tree_model_get(model, &iter, 0, &name, -1);
            gboolean match = (name && strstr(name, search_text));
            g_free(name);
            if (!match) {
                gtk_list_store_remove(widgets->store, &iter);
                valid = gtk_tree_model_get_iter_first(model, &iter);
            } else {
                valid = gtk_tree_model_iter_next(model, &iter);
            }
        }
    }
    
    update_column_headers(active);
}

// Обработчик изменения выбора в списке
static void on_tree_selection_changed(GtkTreeView *tree_view, gpointer data) {
    (void)tree_view; (void)data;
    clear_status();
}

// Обработчик ввода в поиск
static void on_search_changed(GtkSearchEntry *entry, gpointer data) {
    (void)entry; (void)data;
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(widgets->notebook));
    on_page_changed(GTK_NOTEBOOK(widgets->notebook), NULL, page, NULL);
}

// Обработчик кнопки Действие (Установить/Отключить)
static void on_action_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    clear_status();
    
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widgets->tree_view));
    GtkTreeModel *model;
    GList *paths = gtk_tree_selection_get_selected_rows(selection, &model);
    
    if (!paths) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(widgets->window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_WARNING,
                                                   GTK_BUTTONS_OK,
                                                   "Выберите модуль из списка!");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    gint active = gtk_notebook_get_current_page(GTK_NOTEBOOK(widgets->notebook));
    
    for (GList *l = paths; l != NULL; l = l->next) {
        GtkTreeIter iter;
        GtkTreePath *path = (GtkTreePath*) l->data;
        if (gtk_tree_model_get_iter(model, &iter, path)) {
            gchar *name;
            gtk_tree_model_get(model, &iter, COL_NAME, &name, -1);
            
            gtk_label_set_text(GTK_LABEL(widgets->status_label), "[...] Выполняется...");
            gtk_widget_set_sensitive(widgets->btn_action, FALSE);
            while (gtk_events_pending()) gtk_main_iteration();
            
            gchar *cmd = NULL;
            gchar *executed_cmd = NULL;
            gint exit_code = 0;
            
             if (active == 0) {
                 executed_cmd = g_strdup_printf("pfsunload \"%s\"", name);
                 cmd = g_strdup_printf("%s 2>&1", executed_cmd);
             } else if (active == 1) {
                 // Проверяем, является ли модуль локальным
                 gboolean is_local;
                 gtk_tree_model_get(model, &iter, COL_IS_LOCAL, &is_local, -1);
                 
                 if (is_local) {
                     // Локальный модуль - только подключить (не скачивать)
                     executed_cmd = g_strdup_printf("pfsload \"%s/%s\"", DOWNLOAD_DIR, name);
                     cmd = g_strdup_printf("%s 2>&1", executed_cmd);
                 } else {
                     // Удаленный модуль - скачать и подключить
                     executed_cmd = g_strdup_printf("%s -S \"%s\"", MODMAN_BIN, name);
                     cmd = g_strdup_printf("%s 2>&1", executed_cmd);
                 }
             } else if (active == 2) {
                 executed_cmd = g_strdup_printf("pfsload \"%s/%s\"", DOWNLOAD_DIR, name);
                 cmd = g_strdup_printf("%s 2>&1", executed_cmd);
             } else {
                 executed_cmd = g_strdup_printf("pfsunload \"%s\"", name);
                 cmd = g_strdup_printf("%s 2>&1", executed_cmd);
             }
            
            FILE *fp = popen(cmd, "r");
            g_free(cmd);
            
            if (fp) {
                GString *output = g_string_new("");
                gchar line[1024];
                while (fgets(line, sizeof(line), fp)) {
                    g_string_append(output, line);
                }
                exit_code = pclose(fp);
                
                g_string_free(widgets->last_error, TRUE);
                widgets->last_error = NULL;
                
                if (exit_code == 0) {
                    gtk_label_set_text(GTK_LABEL(widgets->status_label), "[OK] Готово");
                    while (gtk_events_pending()) gtk_main_iteration();
                    g_string_free(output, TRUE);
                    on_page_changed(GTK_NOTEBOOK(widgets->notebook), NULL,
                                    gtk_notebook_get_current_page(GTK_NOTEBOOK(widgets->notebook)), NULL);
                } else {
                    widgets->last_error = g_string_new("");
                    g_string_append_printf(widgets->last_error, "Команда: %s\n\n%s", 
                                           executed_cmd ? executed_cmd : "unknown", 
                                           output->str);
                    
                    // Обрезаем текст до 2 строк
                    gchar *short_err = g_strdup(output->str);
                    for (gchar *p = short_err; *p; p++) {
                        if (*p == '\n' || *p == '\r') *p = ' ';
                    }
                    g_strstrip(short_err);
                    // Ограничиваем ~400 символами (2 строки)
                    gsize len = strlen(short_err);
                    if (len > 400) {
                        short_err[397] = '.';
                        short_err[398] = '.';
                        short_err[399] = '.';
                        short_err[400] = '\0';
                    }
                    gtk_label_set_text(GTK_LABEL(widgets->status_label), short_err);
                    g_free(short_err);
                    while (gtk_events_pending()) gtk_main_iteration();
                    g_string_free(output, TRUE);
                }
            } else {
                gtk_label_set_text(GTK_LABEL(widgets->status_label), "[ERR] Ошибка запуска");
            }
            
            g_free(executed_cmd);
            gtk_widget_set_sensitive(widgets->btn_action, TRUE);
            g_free(name);
        }
    }
    
    g_list_free_full(paths, (GDestroyNotify)gtk_tree_path_free);
}

// Обработчик клика на статус (показать ошибку)
static gboolean on_status_clicked(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget; (void)event; (void)data;
    
    if (widgets->last_error && widgets->last_error->len > 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(widgets->window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "%s", widgets->last_error->str);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    return FALSE;
}

static void on_help_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(widgets->window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_INFO,
                                               GTK_BUTTONS_OK,
                                               "mod-man - Менеджер модулей\n\n"
                                               "Архитектура: Bash backend + GTK3 C frontend\n"
                                                "Используйте вкладки сверху для переключения режимов.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_clear_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(widgets->window),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Удалить все .old файлы из локального хранилища?");
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        system("rm -f /home/ai/modman/modules/*.old"); // Путь заглушка, лучше читать из конфига
        // Обновить список, если открыты локальные файлы
        if (gtk_notebook_get_current_page(GTK_NOTEBOOK(widgets->notebook)) == 2) {
            on_page_changed(GTK_NOTEBOOK(widgets->notebook), NULL, 2, NULL);
        }
    }
}

static void on_refresh_clicked(GtkButton *btn, gpointer data) {
    (void)btn; (void)data;
    system(MODMAN_BIN " -Sy");
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(widgets->notebook));
    on_page_changed(GTK_NOTEBOOK(widgets->notebook), NULL, page, NULL);
}

// Функция сравнения для сортировки (локальные сверху)
static gint compare_local_first(GtkTreeModel *model,
                                 GtkTreeIter *a,
                                 GtkTreeIter *b,
                                 gpointer user_data) {
    (void)user_data;
    
    gboolean is_local_a, is_local_b;
    gtk_tree_model_get(model, a, COL_IS_LOCAL, &is_local_a, -1);
    gtk_tree_model_get(model, b, COL_IS_LOCAL, &is_local_b, -1);
    
    // Локальные (TRUE) должны быть сверху, поэтому возвращаем -1 если a локальный
    if (is_local_a && !is_local_b) return -1;  // a сверху
    if (!is_local_a && is_local_b) return 1;   // b сверху
    
    // Если оба локальные или оба удаленные, сортируем по имени (COL_NAME)
    gchar *name_a, *name_b;
    gtk_tree_model_get(model, a, COL_NAME, &name_a, -1);
    gtk_tree_model_get(model, b, COL_NAME, &name_b, -1);
    
    gint result = g_strcmp0(name_a, name_b);
    
    g_free(name_a);
    g_free(name_b);
    
    return result;
}

// Callback для установки цвета фона строки в зависимости от is_local
static void cell_data_func_background(GtkTreeViewColumn *col,
                                       GtkCellRenderer *renderer,
                                       GtkTreeModel *model,
                                       GtkTreeIter *iter,
                                       gpointer user_data) {
    (void)col; (void)user_data;
    
    gboolean is_local;
    gtk_tree_model_get(model, iter, COL_IS_LOCAL, &is_local, -1);
    
    if (is_local) {
        // Светло-зеленый фон для локальных модулей
        g_object_set(renderer, "cell-background", "#d4edda", 
                     "cell-background-set", TRUE, NULL);
    } else {
        // Обычный фон для удаленных
        g_object_set(renderer, "cell-background-set", FALSE, NULL);
    }
}

// Создание главного окна
static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    widgets = g_new0(AppWidgets, 1);
    
    // Окно
    widgets->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(widgets->window), "modman - Менеджер модулей");
    gtk_window_set_default_size(GTK_WINDOW(widgets->window), 800, 500);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(widgets->window), vbox);
    
    // --- ШАПКА ---
    GtkWidget *hbox_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_top, FALSE, FALSE, 0);
    
    widgets->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(widgets->search_entry), "Поиск модулей...");
    g_signal_connect(widgets->search_entry, "search-changed", G_CALLBACK(on_search_changed), NULL);
    gtk_box_pack_start(GTK_BOX(hbox_top), widgets->search_entry, TRUE, TRUE, 0);
    
    GtkWidget *btn_refresh = gtk_button_new_with_label("🔄 Обновить базу");
    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox_top), btn_refresh, FALSE, FALSE, 0);
    
    // --- ВКЛАДКИ ---
    widgets->notebook = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(widgets->notebook), gtk_label_new(""), gtk_label_new("Подключенные"));
    gtk_notebook_append_page(GTK_NOTEBOOK(widgets->notebook), gtk_label_new(""), gtk_label_new("Инет"));
    gtk_notebook_append_page(GTK_NOTEBOOK(widgets->notebook), gtk_label_new(""), gtk_label_new("Локальные"));
    gtk_notebook_append_page(GTK_NOTEBOOK(widgets->notebook), gtk_label_new(""), gtk_label_new("Система"));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(widgets->notebook), 0);
    g_signal_connect(widgets->notebook, "switch-page", G_CALLBACK(on_tab_switched), NULL);
    g_signal_connect(widgets->notebook, "switch-page", G_CALLBACK(on_page_changed), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), widgets->notebook, FALSE, FALSE, 0);
    
    // --- ТАБЛИЦА ---
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    
    widgets->store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_BOOLEAN);
    widgets->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(widgets->store));
    
    // Установка сортировки (локальные сверху)
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(widgets->store);
    gtk_tree_sortable_set_sort_func(sortable, 0, compare_local_first, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, 0, GTK_SORT_ASCENDING);
    
    GtkTreeSelection *select = gtk_tree_view_get_selection(GTK_TREE_VIEW(widgets->tree_view));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_MULTIPLE);
    g_signal_connect(widgets->tree_view, "cursor-changed", G_CALLBACK(on_tree_selection_changed), NULL);
    
    for (int i = 0; i < NUM_COLS - 2; i++) {  // -2 чтобы пропустить COL_SIZE_NUM и COL_IS_LOCAL
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("", renderer, "text", i, NULL);
        
        // Устанавливаем callback для изменения цвета фона
        gtk_tree_view_column_set_cell_data_func(column, renderer, 
                                                 (GtkTreeCellDataFunc)cell_data_func_background, 
                                                 NULL, NULL);
        
        gtk_tree_view_column_set_sort_column_id(column, i);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(widgets->tree_view), column);
        widgets->columns[i] = column;
    }
    gtk_tree_view_column_set_sort_column_id(widgets->columns[1], COL_SIZE_NUM);
    gtk_container_add(GTK_CONTAINER(scroll), widgets->tree_view);
    
    // --- НИЖНЯЯ ПАНЕЛЬ - Статус ---
    GtkWidget *status_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), status_hbox, FALSE, FALSE, 0);
    
    // Создать label + event_box для кликов
    // Viewport с фиксированной высотой физически обрезает контент за пределами
    GtkWidget *clip_vp = gtk_viewport_new(NULL, NULL);
    gtk_viewport_set_shadow_type(GTK_VIEWPORT(clip_vp), GTK_SHADOW_NONE);
    gtk_widget_set_size_request(clip_vp, -1, 36);
    gtk_widget_set_vexpand(clip_vp, FALSE);
    gtk_widget_set_valign(clip_vp, GTK_ALIGN_START);
    
    widgets->status_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(widgets->status_label), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(widgets->status_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(widgets->status_label), 0.0);
    gtk_misc_set_padding(GTK_MISC(widgets->status_label), 10, 0);
    gtk_widget_set_vexpand(widgets->status_label, FALSE);
    gtk_widget_set_hexpand(widgets->status_label, TRUE);
    
    GtkWidget *event_box = gtk_event_box_new();
    gtk_widget_set_vexpand(event_box, FALSE);
    gtk_container_add(GTK_CONTAINER(event_box), clip_vp);
    gtk_container_add(GTK_CONTAINER(clip_vp), widgets->status_label);
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(on_status_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(status_hbox), event_box, TRUE, TRUE, 0);
    
    // --- НИЖНЯЯ ПАНЕЛЬ - Кнопки ---
    GtkWidget *btn_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), btn_hbox, FALSE, FALSE, 0);
    
    GtkWidget *btn_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(btn_box), GTK_BUTTONBOX_END);
    gtk_box_pack_end(GTK_BOX(btn_hbox), btn_box, TRUE, TRUE, 0);
    
    widgets->btn_action = gtk_button_new_with_label("Скачать и подключить");
    gtk_style_context_add_class(gtk_widget_get_style_context(widgets->btn_action), "suggested-action");
    g_signal_connect(widgets->btn_action, "clicked", G_CALLBACK(on_action_clicked), NULL);
    
    GtkWidget *btn_clear = gtk_button_new_with_label("Очистить");
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(on_clear_clicked), NULL);
    
    GtkWidget *btn_help = gtk_button_new_with_label("Справка");
    g_signal_connect(btn_help, "clicked", G_CALLBACK(on_help_clicked), NULL);
    
    GtkWidget *btn_exit = gtk_button_new_with_label("Выход");
    g_signal_connect_swapped(btn_exit, "clicked", G_CALLBACK(gtk_widget_destroy), widgets->window);
    
    gtk_container_add(GTK_CONTAINER(btn_box), btn_help);
    gtk_container_add(GTK_CONTAINER(btn_box), btn_clear);
    gtk_container_add(GTK_CONTAINER(btn_box), btn_exit);
    gtk_container_add(GTK_CONTAINER(btn_box), widgets->btn_action);
    
    // Загружаем начальные данные
    on_page_changed(GTK_NOTEBOOK(widgets->notebook), NULL, 3, NULL);
    
    gtk_widget_show_all(widgets->window);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("org.puppyrus.modman", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    if (widgets) g_free(widgets);
    return status;
}
