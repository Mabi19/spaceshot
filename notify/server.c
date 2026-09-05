#include "server.h"
#include <config/config.h>
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    NOTIFICATION_ACTION_OPEN,
    NOTIFICATION_ACTION_EDIT,
    NOTIFICATION_ACTION_DIRECTORY,
} NotificationAction;

/**
 * The table mapping notification actions to their IDs and human-readable names.
 */
static const struct {
    const char *key;
    const char *label;
} action_info[] = {
    [NOTIFICATION_ACTION_OPEN] = {"open", "Open"},
    [NOTIFICATION_ACTION_EDIT] = {"edit", "Edit"},
    [NOTIFICATION_ACTION_DIRECTORY] = {"directory", "View in directory"},
};

/**
 * Convert the config value to an action. CONFIG_NOTIFY_DEFAULT_ACTION_NONE
 * has no matching action and must be handled by the caller.
 */
static NotificationAction
default_action_to_action(ConfigNotifyDefaultAction default_action) {
    switch (default_action) {
    case CONFIG_NOTIFY_DEFAULT_ACTION_OPEN:
        return NOTIFICATION_ACTION_OPEN;
    case CONFIG_NOTIFY_DEFAULT_ACTION_EDIT:
        return NOTIFICATION_ACTION_EDIT;
    case CONFIG_NOTIFY_DEFAULT_ACTION_DIRECTORY:
        return NOTIFICATION_ACTION_DIRECTORY;
    case CONFIG_NOTIFY_DEFAULT_ACTION_NONE:
    default:
        g_assert_not_reached();
    }
}

static guint dbus_registration_id = 0;
static GDBusProxy *notification_service = NULL;
// Maps notification IDs (as GUINT_TO_POINTER) to their paths (char *, owned).
static GHashTable *active_notifications = NULL;
static GList *config_monitors = NULL;
static guint config_reload_timeout_id = 0;

static GDBusNodeInfo *introspection_data = NULL;
static const GDBusInterfaceInfo *introspection_interface = NULL;

static const char introspection_xml[] =
    "<node>"
    "  <interface name='land.mabi.SpaceshotNotify'>"
    "    <method name='NotifyForFile'>"
    "      <arg type='s' name='path' direction='in'/>"
    "      <arg type='b' name='did_copy' direction='in'/>"
    "    </method>"
    "    <method name='ReloadConfig'>"
    "    </method>"
    "  </interface>"
    "</node>";

static void spawn_process(char **argv) {
    GError *error = NULL;
    if (!g_spawn_async(
            NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error
        )) {
        g_printerr("Couldn't spawn %s: %s\n", argv[0], error->message);
        g_clear_error(&error);
    }
}

static void
file_manager_call_done(GObject *source, GAsyncResult *res, void *user_data) {
    char *path = user_data;
    GError *error = NULL;
    GVariant *result =
        g_dbus_proxy_call_finish(G_DBUS_PROXY(source), res, &error);
    if (result == NULL) {
        g_printerr(
            "Couldn't invoke file manager through D-Bus: %s\n", error->message
        );
        g_clear_error(&error);
    } else {
        g_variant_unref(result);
    }
    g_object_unref(source);
    g_free(path);
}

static void file_manager_proxy_created(
    GObject *source, GAsyncResult *res, void *user_data
) {
    char *path = user_data;
    GError *error = NULL;
    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
    (void)source;
    if (proxy == NULL) {
        g_printerr(
            "Couldn't invoke file manager through D-Bus: %s\n", error->message
        );
        g_clear_error(&error);
        g_free(path);
        return;
    }

    GVariantBuilder uri_builder;
    g_variant_builder_init(&uri_builder, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&uri_builder, "s", path);
    GVariant *uris = g_variant_builder_end(&uri_builder);

    g_dbus_proxy_call(
        proxy,
        "ShowItems",
        g_variant_new("(@ass)", uris, ""),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        file_manager_call_done,
        path
    );
}

static void exec_action(NotificationAction action, const char *path) {
    switch (action) {
    case NOTIFICATION_ACTION_OPEN: {
        char *argv[] = {(char *)"xdg-open", (char *)path, NULL};
        spawn_process(argv);
        break;
    }
    case NOTIFICATION_ACTION_EDIT: {
        Config *conf = config_get();
        char **argvp = NULL;
        int argcp = 0;
        GError *error = NULL;
        if (!g_shell_parse_argv(
                conf->notify.edit_command, &argcp, &argvp, &error
            )) {
            g_printerr("Couldn't parse command line: %s\n", error->message);
            g_clear_error(&error);
            break;
        }
        bool has_found = false;
        for (int i = 0; i < argcp; i++) {
            if (strcmp(argvp[i], "{{path}}") == 0) {
                g_free(argvp[i]);
                argvp[i] = g_strdup(path);
                has_found = true;
            }
        }
        if (!has_found) {
            g_warning("Edit command template has no {{path}} placeholders");
        }
        spawn_process(argvp);
        g_strfreev(argvp);
        break;
    }
    case NOTIFICATION_ACTION_DIRECTORY: {
        g_dbus_proxy_new_for_bus(
            G_BUS_TYPE_SESSION,
            G_DBUS_PROXY_FLAGS_NONE,
            NULL,
            "org.freedesktop.FileManager1",
            "/org/freedesktop/FileManager1",
            "org.freedesktop.FileManager1",
            NULL,
            file_manager_proxy_created,
            g_strdup(path)
        );
        break;
    }
    }
}

static void handle_notification_closed(guint id, guint reason) {
    if (g_hash_table_remove(active_notifications, GUINT_TO_POINTER(id))) {
        printf("notification closed: %u (reason = %u)\n", id, reason);
    }
}

static void handle_notification_action(guint id, const char *key) {
    printf("notification action %s for %u\n", key, id);
    const char *path =
        g_hash_table_lookup(active_notifications, GUINT_TO_POINTER(id));
    if (path == NULL) {
        // this is not ours
        return;
    }

    if (strcmp(key, "default") == 0) {
        Config *conf = config_get();
        if (conf->notify.default_action != CONFIG_NOTIFY_DEFAULT_ACTION_NONE) {
            exec_action(
                default_action_to_action(conf->notify.default_action), path
            );
        }
        // otherwise, this may be an old notification from before a config
        // reload; don't explode
        return;
    }
    for (size_t i = 0; i < G_N_ELEMENTS(action_info); i++) {
        if (strcmp(key, action_info[i].key) == 0) {
            exec_action((NotificationAction)i, path);
            return;
        }
    }
    g_assert_not_reached();
}

static void on_notification_proxy_signal(
    GDBusProxy * /* proxy */,
    const char * /* sender_name */,
    const char *signal_name,
    GVariant *parameters,
    void * /* user_data */
) {
    if (strcmp(signal_name, "NotificationClosed") == 0) {
        guint id = 0;
        guint reason = 0;
        g_variant_get(parameters, "(uu)", &id, &reason);
        handle_notification_closed(id, reason);
    } else if (strcmp(signal_name, "ActionInvoked") == 0) {
        guint id = 0;
        const char *action_key = NULL;
        g_variant_get(parameters, "(u&s)", &id, &action_key);
        handle_notification_action(id, action_key);
    }
}

static bool notify_for_file(const char *path, bool did_copy, GError **error) {
    Config *conf = config_get();
    size_t button_count = conf->notify.actions.count;
    bool has_default =
        conf->notify.default_action != CONFIG_NOTIFY_DEFAULT_ACTION_NONE;

    GVariantBuilder actions_builder;
    g_variant_builder_init(&actions_builder, G_VARIANT_TYPE("as"));
    if (has_default) {
        NotificationAction default_action =
            default_action_to_action(conf->notify.default_action);
        g_variant_builder_add(&actions_builder, "s", "default");
        g_variant_builder_add(
            &actions_builder, "s", action_info[default_action].label
        );
    }
    for (size_t i = 0; i < button_count; i++) {
        NotificationAction action;
        switch (conf->notify.actions.items[i]) {
        case CONFIG_NOTIFY_ACTIONS_ITEM_OPEN:
            action = NOTIFICATION_ACTION_OPEN;
            break;
        case CONFIG_NOTIFY_ACTIONS_ITEM_EDIT:
            action = NOTIFICATION_ACTION_EDIT;
            break;
        case CONFIG_NOTIFY_ACTIONS_ITEM_DIRECTORY:
            action = NOTIFICATION_ACTION_DIRECTORY;
            break;
        default:
            g_assert_not_reached();
        }
        g_variant_builder_add(&actions_builder, "s", action_info[action].key);
        g_variant_builder_add(&actions_builder, "s", action_info[action].label);
    }
    GVariant *actions = g_variant_builder_end(&actions_builder);

    GVariantBuilder hints_builder;
    g_variant_builder_init(&hints_builder, G_VARIANT_TYPE("a{sv}"));
    // The "{sv}" format consumes the variant as the v's contents, so the
    // string must not be wrapped in a variant here.
    g_variant_builder_add(
        &hints_builder, "{sv}", "image-path", g_variant_new_string(path)
    );
    GVariant *hints = g_variant_builder_end(&hints_builder);

    const char *body_template =
        did_copy ? conf->notify.body_copy : conf->notify.body_nocopy;
    GString *body = g_string_new(body_template);
    g_string_replace(body, "{{path}}", path, 0);

    GVariant *result = g_dbus_proxy_call_sync(
        notification_service,
        "Notify",
        g_variant_new(
            "(susss@as@a{sv}i)",
            "Spaceshot",          // app_name
            (guint32)0,           // replaces_id
            "",                   // app_icon
            conf->notify.summary, // summary
            body->str,            // body
            actions,              // actions
            hints,                // hints
            (gint32)-1            // expire_timeout
        ),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        error
    );
    g_string_free(body, TRUE);
    if (result == NULL) {
        return false;
    }

    guint id = 0;
    g_variant_get(result, "(u)", &id);
    g_variant_unref(result);

    g_hash_table_insert(
        active_notifications, GUINT_TO_POINTER(id), g_strdup(path)
    );
    printf("notify for %s produced id %u\n", path, id);
    return true;
}

static gboolean reload_timeout_cb(void * /* user_data */) {
    config_reload_timeout_id = 0;
    printf("Reloading config...\n");
    config_load();
    return G_SOURCE_REMOVE;
}

static void on_config_file_changed(
    GFileMonitor * /* monitor */,
    GFile *file,
    GFile * /* other_file */,
    GFileMonitorEvent event_type,
    void * /* user_data */
) {
    char *file_path = g_file_get_path(file);
    g_print(
        "config file changed! %s, event: %d\n",
        file_path != NULL ? file_path : "(null)",
        (int)event_type
    );
    g_free(file_path);
    if (config_reload_timeout_id != 0) {
        g_source_remove(config_reload_timeout_id);
    }
    config_reload_timeout_id = g_timeout_add(500, reload_timeout_cb, NULL);
}

static void server_init(void) {
    active_notifications =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);

    GError *error = NULL;
    notification_service = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        "org.freedesktop.Notifications",
        "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications",
        NULL,
        &error
    );
    if (notification_service == NULL) {
        g_printerr(
            "Couldn't connect to notification service: %s\n", error->message
        );
        g_clear_error(&error);
        exit(1);
    }
    g_signal_connect(
        notification_service,
        "g-signal",
        G_CALLBACK(on_notification_proxy_signal),
        NULL
    );

    config_load();

    const char **config_locations = config_get_locations();
    for (size_t i = 0; config_locations[i] != NULL; i++) {
        const char *location = config_locations[i];
        g_print("config location: %s\n", location);
        GFile *file = g_file_new_for_path(location);
        GError *monitor_error = NULL;
        GFileMonitor *monitor = g_file_monitor_file(
            file, G_FILE_MONITOR_NONE, NULL, &monitor_error
        );
        if (monitor == NULL) {
            g_printerr("Couldn't watch config file %s\n", location);
            g_clear_error(&monitor_error);
        } else {
            g_signal_connect(
                monitor, "changed", G_CALLBACK(on_config_file_changed), NULL
            );
            // Keep a reference for the lifetime of the program.
            config_monitors = g_list_append(config_monitors, monitor);
        }
        g_object_unref(file);
    }
}

static void handle_method_call(
    GDBusConnection * /* connection */,
    const char * /* sender */,
    const char * /* object_path */,
    const char * /* interface_name */,
    const char *method_name,
    GVariant *parameters,
    GDBusMethodInvocation *invocation,
    void * /* user_data */
) {
    if (strcmp(method_name, "NotifyForFile") == 0) {
        const char *path = NULL;
        gboolean did_copy = FALSE;
        g_variant_get(parameters, "(&sb)", &path, &did_copy);
        GError *error = NULL;
        if (!notify_for_file(path, did_copy, &error)) {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_clear_error(&error);
        } else {
            g_dbus_method_invocation_return_value(invocation, NULL);
        }
    } else if (strcmp(method_name, "ReloadConfig") == 0) {
        config_load();
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else {
        g_dbus_method_invocation_return_error(
            invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method %s",
            method_name
        );
    }
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

static void on_bus_acquired(
    GDBusConnection *connection, const char * /* name */, void * /* user_data */
) {
    GError *error = NULL;
    dbus_registration_id = g_dbus_connection_register_object(
        connection,
        "/land/mabi/spaceshot",
        (GDBusInterfaceInfo *)introspection_interface,
        &interface_vtable,
        NULL,
        NULL,
        &error
    );
    if (dbus_registration_id == 0) {
        g_printerr("Couldn't register D-Bus service: %s\n", error->message);
        g_clear_error(&error);
        exit(1);
    }
}

static void on_name_lost(
    GDBusConnection * /* connection */,
    const char * /* name */,
    void * /* user_data */
) {
    if (dbus_registration_id) {
        g_printerr("D-Bus name was lost\n");
    } else {
        g_printerr("Couldn't acquire D-Bus name\n");
    }
    exit(1);
}

void run_server(void) {
    GError *error = NULL;
    introspection_data =
        g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (introspection_data == NULL) {
        g_printerr("Couldn't parse introspection XML: %s\n", error->message);
        g_clear_error(&error);
        exit(1);
    }
    introspection_interface = g_dbus_node_info_lookup_interface(
        introspection_data, "land.mabi.SpaceshotNotify"
    );

    server_init();

    g_bus_own_name(
        G_BUS_TYPE_SESSION,
        "land.mabi.spaceshot",
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        NULL,
        on_name_lost,
        NULL,
        NULL
    );

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
}
