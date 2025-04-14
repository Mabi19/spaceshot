[DBus(name = "org.freedesktop.Notifications")]
interface NotificationService: Object {
    public abstract string[] get_capabilities() throws DBusError, IOError;
    public abstract uint notify(
        string app_name,
        uint replaces_id,
        string app_icon,
        string summary,
        string body,
        string[] actions,
        HashTable<string, Variant> hints,
        int expire_timeout
    ) throws DBusError, IOError;
    public abstract void close_notification(uint id) throws DBusError, IOError;

    public signal void notification_closed(uint id, uint reason);
    public signal void action_invoked(uint id, string action_key);
}
