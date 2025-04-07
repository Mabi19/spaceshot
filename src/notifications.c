#include "notifications.h"
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
    NotifyNotification * /* n */, char * /* action */, void *user_data
) {
    NotifyData *params = user_data;
    // TODO: call dbus FileManager1
    log_debug("Notification directory('%s')\n", params->filepath);
}

static void handle_notification_closed(NotifyNotification *, GMainLoop *loop) {
    log_debug("Notification closed\n");
    g_main_loop_quit(loop);
}

static const char *const BODY_TEMPLATE =
    "Image saved in %s and copied to the clipboard";

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
