#include "server.h"
#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>

int main(int argc, char **argv) {
    gboolean is_server = FALSE;
    gboolean did_copy = FALSE;
    char *screenshot_path = NULL;

    GOptionEntry entries[] = {
        {
            .long_name = "server",
            .short_name = 's',
            .flags = G_OPTION_FLAG_NONE,
            .arg = G_OPTION_ARG_NONE,
            .arg_data = &is_server,
            .description =
                "Run the server. This is normally done automatically by D-Bus",
            .arg_description = NULL,
        },
        {
            .long_name = "path",
            .short_name = 'p',
            .flags = G_OPTION_FLAG_NONE,
            .arg = G_OPTION_ARG_FILENAME,
            .arg_data = &screenshot_path,
            .description = "Saved screenshot path",
            .arg_description = NULL,
        },
        {
            .long_name = "copied",
            .short_name = 'c',
            .flags = G_OPTION_FLAG_NONE,
            .arg = G_OPTION_ARG_NONE,
            .arg_data = &did_copy,
            .description = "Indicate that the screenshot was copied",
            .arg_description = NULL,
        },
        {},
    };

    GOptionContext *context =
        g_option_context_new("- notification service for spaceshot");
    g_option_context_add_main_entries(context, entries, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("Option parsing failed: %s\n", error->message);
        g_clear_error(&error);
        g_option_context_free(context);
        g_free(screenshot_path);
        return 1;
    }
    g_option_context_free(context);

    if (is_server) {
        g_free(screenshot_path);
        run_server();
    } else {
        if (screenshot_path == NULL) {
            g_printerr("error: Path is required in client mode\n");
            return 1;
        }

        GError *proxy_error = NULL;
        GDBusProxy *client = g_dbus_proxy_new_for_bus_sync(
            G_BUS_TYPE_SESSION,
            G_DBUS_PROXY_FLAGS_NONE,
            NULL,
            "land.mabi.spaceshot",
            "/land/mabi/spaceshot",
            "land.mabi.SpaceshotNotify",
            NULL,
            &proxy_error
        );
        if (client == NULL) {
            g_printerr(
                "error: Couldn't connect to D-Bus: %s\n", proxy_error->message
            );
            g_clear_error(&proxy_error);
            g_free(screenshot_path);
            return 1;
        }

        GError *call_error = NULL;
        GVariant *result = g_dbus_proxy_call_sync(
            client,
            "NotifyForFile",
            g_variant_new("(sb)", screenshot_path, did_copy),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &call_error
        );
        g_free(screenshot_path);
        if (result == NULL) {
            if (call_error->domain == G_DBUS_ERROR) {
                g_printerr(
                    "error: Couldn't invoke spaceshot notify service (D-Bus "
                    "error): %s\n",
                    call_error->message
                );
                if (call_error->code == G_DBUS_ERROR_SERVICE_UNKNOWN) {
                    g_printerr(
                        "note: this usually means the D-Bus service definition "
                        "wasn't installed correctly\n"
                    );
                }
            } else {
                g_printerr(
                    "error: Couldn't invoke spaceshot notify service (IO "
                    "error): %s\n",
                    call_error->message
                );
            }
            g_clear_error(&call_error);
            g_object_unref(client);
            return 1;
        }
        g_variant_unref(result);
        g_object_unref(client);
    }
    return 0;
}
