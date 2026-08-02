/*
 * See tray.h for why this hand-rolls org.kde.StatusNotifierItem +
 * com.canonical.dbusmenu instead of linking a library for it.
 *
 * The menu is deliberately flat -- ids 1-6, no submenus, no dynamic
 * add/remove -- which keeps GetLayout/GetGroupProperties trivial and
 * sidesteps most of the DBusMenu spec's more elaborate machinery
 * (revisions only bump when label/enabled text actually changes, and a
 * flat menu never needs AboutToShow to report "needs update").
 */

#include "tray.h"
#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>

#define SNI_IFACE "org.kde.StatusNotifierItem"
#define SNI_PATH "/StatusNotifierItem"
#define MENU_IFACE "com.canonical.dbusmenu"
#define MENU_PATH "/StatusNotifierItem/Menu"

enum { ITEM_OPEN = 1, ITEM_SEP1 = 2, ITEM_BATTERY = 3, ITEM_SEP2 = 4, ITEM_REFRESH = 5, ITEM_QUIT = 6 };

struct AjazzTray {
    GDBusConnection *connection;
    GDBusNodeInfo *sni_node;
    GDBusNodeInfo *menu_node;
    guint sni_reg_id;
    guint menu_reg_id;
    guint own_name_id;
    char *icon_name;
    char *title;
    char device_line[128];
    char battery_line[128];
    guint32 menu_revision;
    AjazzTrayCallbacks cb;
};

static const char *SNI_XML =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='WindowId' type='i' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='OverlayIconName' type='s' access='read'/>"
    "    <property name='AttentionIconName' type='s' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "    <method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "    <method name='ContextMenu'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "    <method name='Scroll'><arg type='i' direction='in'/><arg type='s' direction='in'/></method>"
    "    <signal name='NewIcon'/>"
    "    <signal name='NewToolTip'/>"
    "    <signal name='NewStatus'><arg type='s'/></signal>"
    "  </interface>"
    "</node>";

static const char *MENU_XML =
    "<node>"
    "  <interface name='com.canonical.dbusmenu'>"
    "    <property name='Version' type='u' access='read'/>"
    "    <property name='TextDirection' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='IconThemePath' type='as' access='read'/>"
    "    <method name='GetLayout'>"
    "      <arg type='i' direction='in'/><arg type='i' direction='in'/><arg type='as' direction='in'/>"
    "      <arg type='u' direction='out'/><arg type='(ia{sv}av)' direction='out'/>"
    "    </method>"
    "    <method name='GetGroupProperties'>"
    "      <arg type='ai' direction='in'/><arg type='as' direction='in'/>"
    "      <arg type='a(ia{sv})' direction='out'/>"
    "    </method>"
    "    <method name='GetProperty'>"
    "      <arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/>"
    "    </method>"
    "    <method name='Event'>"
    "      <arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='in'/><arg type='u' direction='in'/>"
    "    </method>"
    "    <method name='EventGroup'>"
    "      <arg type='a(isvu)' direction='in'/><arg type='ai' direction='out'/>"
    "    </method>"
    "    <method name='AboutToShow'>"
    "      <arg type='i' direction='in'/><arg type='b' direction='out'/>"
    "    </method>"
    "    <method name='AboutToShowGroup'>"
    "      <arg type='ai' direction='in'/><arg type='ai' direction='out'/><arg type='ai' direction='out'/>"
    "    </method>"
    "    <signal name='ItemsPropertiesUpdated'><arg type='a(ia{sv})'/><arg type='a(ias)'/></signal>"
    "    <signal name='LayoutUpdated'><arg type='u'/><arg type='i'/></signal>"
    "    <signal name='ItemActivationRequested'><arg type='i'/><arg type='u'/></signal>"
    "  </interface>"
    "</node>";

/* ---- menu item property helpers ---- */

static void add_item_props(GVariantBuilder *props, AjazzTray *tray, int id)
{
    switch (id) {
        case ITEM_OPEN:
            g_variant_builder_add(props, "{sv}", "label", g_variant_new_string(_("Open Ajazzy")));
            g_variant_builder_add(props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
            break;
        case ITEM_SEP1:
        case ITEM_SEP2:
            g_variant_builder_add(props, "{sv}", "type", g_variant_new_string("separator"));
            break;
        case ITEM_BATTERY:
            g_variant_builder_add(props, "{sv}", "label",
                g_variant_new_string(tray->battery_line[0] ? tray->battery_line : _("Not connected")));
            g_variant_builder_add(props, "{sv}", "enabled", g_variant_new_boolean(FALSE));
            break;
        case ITEM_REFRESH:
            g_variant_builder_add(props, "{sv}", "label", g_variant_new_string(_("Refresh now")));
            g_variant_builder_add(props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
            break;
        case ITEM_QUIT:
            g_variant_builder_add(props, "{sv}", "label", g_variant_new_string(_("Quit")));
            g_variant_builder_add(props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
            break;
        default:
            break;
    }
    g_variant_builder_add(props, "{sv}", "visible", g_variant_new_boolean(TRUE));
}

static GVariant *build_menu_item(AjazzTray *tray, int id)
{
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    add_item_props(&props, tray, id);
    GVariant *props_v = g_variant_builder_end(&props);

    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    GVariant *children_v = g_variant_builder_end(&children);

    return g_variant_new("(i@a{sv}@av)", id, props_v, children_v);
}

static const int ALL_ITEM_IDS[] = {ITEM_OPEN, ITEM_SEP1, ITEM_BATTERY, ITEM_SEP2, ITEM_REFRESH, ITEM_QUIT};

/* ---- com.canonical.dbusmenu method handler ---- */

static void menu_method_call(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *method_name,
    GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)connection; (void)sender; (void)object_path; (void)interface_name;
    AjazzTray *tray = user_data;

    if (strcmp(method_name, "GetLayout") == 0) {
        GVariantBuilder root_children;
        g_variant_builder_init(&root_children, G_VARIANT_TYPE("av"));
        for (size_t i = 0; i < G_N_ELEMENTS(ALL_ITEM_IDS); i++)
            g_variant_builder_add(&root_children, "v", build_menu_item(tray, ALL_ITEM_IDS[i]));
        GVariant *root_children_v = g_variant_builder_end(&root_children);

        GVariantBuilder root_props;
        g_variant_builder_init(&root_props, G_VARIANT_TYPE("a{sv}"));
        GVariant *root_props_v = g_variant_builder_end(&root_props);

        GVariant *layout = g_variant_new("(i@a{sv}@av)", 0, root_props_v, root_children_v);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(u@(ia{sv}av))", tray->menu_revision, layout));

    } else if (strcmp(method_name, "GetGroupProperties") == 0) {
        GVariant *ids_v = g_variant_get_child_value(parameters, 0);
        GVariantIter id_iter;
        g_variant_iter_init(&id_iter, ids_v);
        GVariantBuilder out;
        g_variant_builder_init(&out, G_VARIANT_TYPE("a(ia{sv})"));
        gint32 id;
        while (g_variant_iter_next(&id_iter, "i", &id)) {
            GVariantBuilder props;
            g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
            add_item_props(&props, tray, id);
            g_variant_builder_add(&out, "(i@a{sv})", id, g_variant_builder_end(&props));
        }
        g_variant_unref(ids_v);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&out)));

    } else if (strcmp(method_name, "GetProperty") == 0) {
        gint32 id; const char *name;
        g_variant_get(parameters, "(i&s)", &id, &name);
        GVariantBuilder props;
        g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
        add_item_props(&props, tray, id);
        GVariant *dict = g_variant_builder_end(&props);
        GVariant *value = g_variant_lookup_value(dict, name, NULL);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(v)", value ? value : g_variant_new_string("")));
        g_variant_unref(dict);

    } else if (strcmp(method_name, "Event") == 0) {
        GVariant *id_v = g_variant_get_child_value(parameters, 0);
        GVariant *event_v = g_variant_get_child_value(parameters, 1);
        gint32 id = g_variant_get_int32(id_v);
        const char *event_id = g_variant_get_string(event_v, NULL);
        if (strcmp(event_id, "clicked") == 0) {
            if (id == ITEM_OPEN && tray->cb.on_activate) tray->cb.on_activate(tray->cb.user_data);
            else if (id == ITEM_REFRESH && tray->cb.on_refresh) tray->cb.on_refresh(tray->cb.user_data);
            else if (id == ITEM_QUIT && tray->cb.on_quit) tray->cb.on_quit(tray->cb.user_data);
        }
        g_variant_unref(id_v);
        g_variant_unref(event_v);
        g_dbus_method_invocation_return_value(invocation, NULL);

    } else if (strcmp(method_name, "EventGroup") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, NULL, 0)));

    } else if (strcmp(method_name, "AboutToShow") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));

    } else if (strcmp(method_name, "AboutToShowGroup") == 0) {
        GVariant *empty = g_variant_new_array(G_VARIANT_TYPE_INT32, NULL, 0);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai@ai)", empty, g_variant_ref(empty)));

    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method %s", method_name);
    }
}

static GVariant *menu_get_property(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *property_name,
    GError **error, gpointer user_data)
{
    (void)connection; (void)sender; (void)object_path; (void)interface_name; (void)error; (void)user_data;
    if (strcmp(property_name, "Version") == 0) return g_variant_new_uint32(3);
    if (strcmp(property_name, "TextDirection") == 0) return g_variant_new_string("ltr");
    if (strcmp(property_name, "Status") == 0) return g_variant_new_string("normal");
    if (strcmp(property_name, "IconThemePath") == 0) return g_variant_new_array(G_VARIANT_TYPE_STRING, NULL, 0);
    return NULL;
}

static const GDBusInterfaceVTable menu_vtable = {
    .method_call = menu_method_call,
    .get_property = menu_get_property,
    .set_property = NULL,
};

/* ---- org.kde.StatusNotifierItem method/property handlers ---- */

static void sni_method_call(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *method_name,
    GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data)
{
    (void)connection; (void)sender; (void)object_path; (void)interface_name; (void)parameters;
    AjazzTray *tray = user_data;

    if (strcmp(method_name, "Activate") == 0 || strcmp(method_name, "SecondaryActivate") == 0) {
        if (tray->cb.on_activate) tray->cb.on_activate(tray->cb.user_data);
    }
    /* ContextMenu / Scroll: the host manages its own menu popup and
     * scroll gestures for us, nothing to do here. */
    g_dbus_method_invocation_return_value(invocation, NULL);
}

static GVariant *sni_get_property(GDBusConnection *connection, const gchar *sender,
    const gchar *object_path, const gchar *interface_name, const gchar *property_name,
    GError **error, gpointer user_data)
{
    (void)connection; (void)sender; (void)object_path; (void)interface_name; (void)error;
    AjazzTray *tray = user_data;

    if (strcmp(property_name, "Category") == 0) return g_variant_new_string("Hardware");
    if (strcmp(property_name, "Id") == 0) return g_variant_new_string("ajazzy");
    if (strcmp(property_name, "Title") == 0) return g_variant_new_string(tray->title);
    if (strcmp(property_name, "Status") == 0) return g_variant_new_string("Active");
    if (strcmp(property_name, "WindowId") == 0) return g_variant_new_int32(0);
    if (strcmp(property_name, "IconName") == 0) return g_variant_new_string(tray->icon_name);
    if (strcmp(property_name, "OverlayIconName") == 0) return g_variant_new_string("");
    if (strcmp(property_name, "AttentionIconName") == 0) return g_variant_new_string("");
    if (strcmp(property_name, "ItemIsMenu") == 0) return g_variant_new_boolean(FALSE);
    if (strcmp(property_name, "Menu") == 0) return g_variant_new_object_path(MENU_PATH);
    if (strcmp(property_name, "ToolTip") == 0) {
        const char *desc = tray->device_line[0] ? tray->device_line : _("No AJAZZ mouse detected");
        GVariant *pixmaps = g_variant_new_array(G_VARIANT_TYPE("(iiay)"), NULL, 0);
        return g_variant_new("(s@a(iiay)ss)", tray->icon_name, pixmaps, tray->title,
            tray->battery_line[0] ? tray->battery_line : desc);
    }
    return NULL;
}

static const GDBusInterfaceVTable sni_vtable = {
    .method_call = sni_method_call,
    .get_property = sni_get_property,
    .set_property = NULL,
};

/* ---- registration ---- */

static void on_bus_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    (void)name;
    AjazzTray *tray = user_data;
    tray->connection = g_object_ref(connection);

    GError *error = NULL;
    tray->sni_reg_id = g_dbus_connection_register_object(connection, SNI_PATH,
        tray->sni_node->interfaces[0], &sni_vtable, tray, NULL, &error);
    if (error) { g_warning("ajazz tray: SNI register failed: %s", error->message); g_clear_error(&error); }

    tray->menu_reg_id = g_dbus_connection_register_object(connection, MENU_PATH,
        tray->menu_node->interfaces[0], &menu_vtable, tray, NULL, &error);
    if (error) { g_warning("ajazz tray: menu register failed: %s", error->message); g_clear_error(&error); }
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    (void)user_data;
    /* Announce ourselves to whichever tray host is listening (the GNOME
     * AppIndicator extension, KDE's own, etc). If nothing is listening,
     * this call just fails quietly -- not an error worth surfacing, it
     * just means no tray icon shows up, same as any other SNI app on a
     * desktop without a compatible host. */
    g_dbus_connection_call(connection, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
        g_variant_new("(s)", name), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
    (void)connection; (void)name; (void)user_data;
}

AjazzTray *ajazz_tray_new(const char *icon_name, const char *title, const AjazzTrayCallbacks *cb)
{
    GDBusNodeInfo *sni_node = g_dbus_node_info_new_for_xml(SNI_XML, NULL);
    GDBusNodeInfo *menu_node = g_dbus_node_info_new_for_xml(MENU_XML, NULL);
    if (!sni_node || !menu_node) {
        g_clear_pointer(&sni_node, g_dbus_node_info_unref);
        g_clear_pointer(&menu_node, g_dbus_node_info_unref);
        return NULL;
    }

    AjazzTray *tray = g_new0(AjazzTray, 1);
    tray->sni_node = sni_node;
    tray->menu_node = menu_node;
    tray->icon_name = g_strdup(icon_name);
    tray->title = g_strdup(title);
    tray->menu_revision = 1;
    if (cb) tray->cb = *cb;

    char *bus_name = g_strdup_printf("org.freedesktop.StatusNotifierItem-%d-1", (int)getpid());
    tray->own_name_id = g_bus_own_name(G_BUS_TYPE_SESSION, bus_name, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, on_name_acquired, on_name_lost, tray, NULL);
    g_free(bus_name);

    return tray;
}

void ajazz_tray_set_status(AjazzTray *tray, const char *device_line, const char *battery_line)
{
    if (!tray) return;

    g_strlcpy(tray->device_line, device_line ? device_line : "", sizeof(tray->device_line));
    g_strlcpy(tray->battery_line, battery_line ? battery_line : "", sizeof(tray->battery_line));
    tray->menu_revision++;

    if (!tray->connection) return;

    g_dbus_connection_emit_signal(tray->connection, NULL, SNI_PATH, SNI_IFACE, "NewToolTip", NULL, NULL);

    GVariantBuilder updated;
    g_variant_builder_init(&updated, G_VARIANT_TYPE("a(ia{sv})"));
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    add_item_props(&props, tray, ITEM_BATTERY);
    g_variant_builder_add(&updated, "(i@a{sv})", ITEM_BATTERY, g_variant_builder_end(&props));
    GVariantBuilder removed;
    g_variant_builder_init(&removed, G_VARIANT_TYPE("a(ias)"));
    g_dbus_connection_emit_signal(tray->connection, NULL, MENU_PATH, MENU_IFACE, "ItemsPropertiesUpdated",
        g_variant_new("(@a(ia{sv})@a(ias))", g_variant_builder_end(&updated), g_variant_builder_end(&removed)), NULL);
}

void ajazz_tray_set_icon(AjazzTray *tray, const char *icon_name)
{
    if (!tray || !icon_name) return;
    if (g_strcmp0(tray->icon_name, icon_name) == 0) return; /* nothing changed, don't spam the signal */

    g_free(tray->icon_name);
    tray->icon_name = g_strdup(icon_name);

    if (tray->connection)
        g_dbus_connection_emit_signal(tray->connection, NULL, SNI_PATH, SNI_IFACE, "NewIcon", NULL, NULL);
}

void ajazz_tray_free(AjazzTray *tray)
{
    if (!tray) return;
    if (tray->own_name_id) g_bus_unown_name(tray->own_name_id);
    if (tray->connection) {
        if (tray->sni_reg_id) g_dbus_connection_unregister_object(tray->connection, tray->sni_reg_id);
        if (tray->menu_reg_id) g_dbus_connection_unregister_object(tray->connection, tray->menu_reg_id);
        g_object_unref(tray->connection);
    }
    g_clear_pointer(&tray->sni_node, g_dbus_node_info_unref);
    g_clear_pointer(&tray->menu_node, g_dbus_node_info_unref);
    g_free(tray->icon_name);
    g_free(tray->title);
    g_free(tray);
}
