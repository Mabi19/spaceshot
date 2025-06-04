[DBus(name = "land.mabi.SpaceshotNotify")]
interface NotifyClient: DBusProxy {
    public abstract void notify_for_file(string path, bool did_copy) throws DBusError, IOError;
}

int main(string[] args) {
    bool is_server = false;
    bool did_copy = false;
    string screenshot_path = null;

    OptionEntry[] entries = {
        {"server", 's', OptionFlags.NONE, OptionArg.NONE, ref is_server, "Run the server. This is normally done automatically by D-Bus", null},
        {"path", 'p', OptionFlags.NONE, OptionArg.FILENAME, ref screenshot_path, "Saved screenshot path", null},
        {"copied", 'c', OptionFlags.NONE, OptionArg.NONE, ref did_copy, "Indicate that the screenshot was copied", null}
    };

    var context = new OptionContext("- notification service for spaceshot");
    context.add_main_entries(entries, null);
    try {
        context.parse(ref args);
    } catch (OptionError e) {
        stdout.printf("Option parsing failed: %s\n", e.message);
        return 1;
    }

    if (is_server) {
        run_server();
    } else {
        if (screenshot_path == null) {
            stdout.printf("error: Path is required in client mode\n");
            return 1;
        }
        try {
            NotifyClient client = Bus.get_proxy_sync(BusType.SESSION, "land.mabi.spaceshot", "/land/mabi/spaceshot", DBusProxyFlags.NONE, null);
            client.notify_for_file(screenshot_path, did_copy);
        } catch (Error e) {
            printerr("error: Couldn't invoke spaceshot notify service: %s\n", e.message);
            return 1;
        }
    }
    return 0;
}
