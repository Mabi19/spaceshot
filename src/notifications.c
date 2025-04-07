#include "notifications.h"
#include "gio/gio.h"
#include "glib.h"
#include "log.h"
#include <libnotify/notification.h>
#include <libnotify/notify.h>
#include <stdlib.h>
#include <threads.h>

typedef struct {
    char *filepath;
} NotifyData;

static void spawn_process(char **argv) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp(*argv, argv);
        report_error("Couldn't spawn %s", argv[0]);
    } else if (pid == -1) {
        report_error("Couldn't spawn %s", argv[0]);
    }
}

static void handle_notification_open(
    NotifyNotification *n, char * /* action */, void *user_data
) {
    NotifyData *params = user_data;
    // TODO: make this customizable
    char *open_exec[3] = {"xdg-open", params->filepath, NULL};
    spawn_process(open_exec);
    // TODO: Consider making this configurable
    GError *error;
    if (!notify_notification_close(n, &error)) {
        report_error("Couldn't close notification");
    };
}

static void handle_notification_directory(
    NotifyNotification *n, char * /* action */, void *user_data
) {
    NotifyData *params = user_data;
    GError *error = NULL;
    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
            G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
        NULL,
        "org.freedesktop.FileManager1",
        "/org/freedesktop/FileManager1",
        "org.freedesktop.FileManager1",
        NULL,
        &error
    );
    if (error) {
        report_error("Couldn't connect to org.freedesktop.FileManager1");
        return;
    }
    GVariantBuilder *path_array_builder =
        g_variant_builder_new(G_VARIANT_TYPE_STRING_ARRAY);
    g_variant_builder_add(path_array_builder, "s", params->filepath);
    GVariant *result = g_dbus_proxy_call_sync(
        proxy,
        "ShowItems",
        g_variant_new("(ass)", path_array_builder, ""),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );
    g_variant_builder_unref(path_array_builder);
    if (error) {
        report_error("Couldn't open file manager via D-Bus");
        return;
    }
    g_variant_unref(result);

    if (!notify_notification_close(n, &error)) {
        report_error("Couldn't close notification");
    };
}

static void handle_notification_closed(NotifyNotification *, GMainLoop *loop) {
    log_debug("Notification closed\n");
    g_main_loop_quit(loop);
}

static const char *const BODY_TEMPLATE =
    "Image saved in <i>%s</i> and copied to the clipboard.";

static int notify_run_func(void *data) {
    NotifyData *params = data;
    if (!notify_init("Spaceshot")) {
        report_warning("Couldn't initialize libnotify");
        return 0;
    }

    GMainLoop *loop = g_main_loop_new(NULL, false);

    size_t body_buf_size =
        strlen(BODY_TEMPLATE) + strlen(params->filepath) - 2 + 1;
    char *body = malloc(body_buf_size);
    snprintf(body, body_buf_size, BODY_TEMPLATE, params->filepath);
    NotifyNotification *n =
        notify_notification_new("Screenshot saved", body, NULL);

    notify_notification_set_hint(
        n, "image-path", g_variant_new_string(params->filepath)
    );
    notify_notification_add_action(
        n, "default", "Open", handle_notification_open, params, NULL
    );
    // TODO: "Open screenshot editor" action
    notify_notification_add_action(
        n,
        "directory",
        "View in directory",
        handle_notification_directory,
        params,
        NULL
    );

    g_signal_connect(n, "closed", G_CALLBACK(handle_notification_closed), loop);

    if (!notify_notification_show(n, NULL)) {
        report_error("Failed to show notification");
    }
    g_main_loop_run(loop);
    g_main_loop_unref(loop);

    free(params->filepath);
    free(params);

    notify_uninit();

    return 0;
}

bool notify_for_file(thrd_t *thread, char *filepath) {
    NotifyData *data = malloc(sizeof(NotifyData));
    data->filepath = filepath;
    int result = thrd_create(thread, notify_run_func, data);
    return result == thrd_success;
}
