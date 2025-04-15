[DBus(name = "org.freedesktop.FileManager1")]
interface FileManager: DBusProxy {
    public abstract void show_items(string[] uri_list, string startup_id) throws DBusError, IOError;
}
