[DBus(name = "land.mabi.SpaceshotNotify")]
public class NotifyServer: Object {
    private NotificationService notification_service;
    // Maps notification IDs to their paths.
    private HashTable<uint, string> active_notifications;

    construct {
        // I'm not sure why this works with direct hash (and not int hash).
        // The vala compiler casts the integers to pointers.
        this.active_notifications = new HashTable<uint, string>(null, null);
        try {
            this.notification_service = Bus.get_proxy_sync(
                BusType.SESSION,
                "org.freedesktop.Notifications",
                "/org/freedesktop/Notifications",
                DBusProxyFlags.NONE
            );
            this.notification_service.notification_closed.connect(this.handle_notification_closed);
            this.notification_service.action_invoked.connect(this.handle_notification_action);
        } catch (IOError e) {
            printerr("Couldn't connect to notification service: %s", e.message);
            Posix.exit(1);
        }
    }

    private void handle_notification_action(uint id, string key) {
        stdout.printf("notification action %s for %u\n", key, id);
        string path = this.active_notifications.get(id);
        if (path == null) {
            // this is not ours
            return;
        }

        switch (key) {
            case "default":
                try {
                    // If the server is set up to ever exit, child processes will need to be cleaned up
                    Process.spawn_async(
                        null,
                        {"xdg-open", path},
                        null,
                        SpawnFlags.SEARCH_PATH | SpawnFlags.DO_NOT_REAP_CHILD,
                        null,
                        null
                    );
                } catch (SpawnError e) {
                    printerr("Couldn't spawn xdg-open: %s", e.message);
                }
                break;
            case "directory":
                Bus.get_proxy.begin<FileManager>(
                    BusType.SESSION,
                    "org.freedesktop.FileManager1",
                    "/org/freedesktop/FileManager1",
                    DBusProxyFlags.NONE,
                    null,
                    (obj, res) => {
                        try {
                            var file_manager = Bus.get_proxy.end<FileManager>(res);
                            file_manager.show_items({path}, "");
                        } catch (Error e) {
                            printerr("Couldn't invoke file manager through D-Bus");
                        }
                    }
                );
                break;
            default:
                assert_not_reached();
        }
    }

    private void handle_notification_closed(uint id, uint reason) {
        if (this.active_notifications.remove(id)) {
            stdout.printf("notification closed: %u (reason = %u)\n", id, reason);
        }
    }

    public void notify_for_file(string path) throws DBusError, IOError {
        const string[] ACTIONS = {"default", "Open", "directory", "View in directory"};
        var hints = new HashTable<string, Variant>(str_hash, str_equal);
        hints.insert("image-path", new Variant("s", path));

        uint id = this.notification_service.notify(
            "Spaceshot",
            0,
            "",
            "Screenshot saved",
            // TODO: make the body more flexible
            // the markup should only be there if the server supports the capability
            // the image isn't necessarily always copied to the clipboard
            @"Image saved in <i>$path</i> and copied to the clipboard.",
            ACTIONS,
            hints,
            -1
        );
        this.active_notifications.insert(id, path);
        stdout.printf("notify for %s produced id %u\n", path, id);
    }
}

void on_bus_acquired(DBusConnection conn) {
    try {
        conn.register_object("/land/mabi/spaceshot", new NotifyServer());
    } catch (IOError e) {
        stderr.printf("Couldn't register D-Bus service");
    }
}

public void run_server() {
    Bus.own_name(
        BusType.SESSION,
        "land.mabi.spaceshot",
        // TODO: investigate whether any flags should be set here
        BusNameOwnerFlags.NONE,
        on_bus_acquired,
        () => {},
        () => stderr.printf("Couldn't acquire D-Bus name")
    );

    new MainLoop().run();
}
