#include <cstdio>
#include <cstring>
#include <gio/gio.h> // sudo apt install libglib2.0-dev
#include <glib.h> // sudo apt install libglib2.0-dev
#include <iostream>
#include <vector>
#include "wimiso8601.h"

// https://stackoverflow.com/questions/50675797/bluez-d-bus-c-application-ble
// https://www.linumiz.com/bluetooth-list-devices-using-gdbus/
// https://stackoverflow.com/questions/70448584/implementing-bluetooth-client-server-architecture-in-c-dbus
// https://www.mongodb.com/developer/languages/cpp/me-and-the-devil-bluez-1/
// https://www.mongodb.com/developer/languages/cpp/me-and-the-devil-bluez-2/

/*
 * bluez_adapter_filter.c - Set discovery filter, Scan for bluetooth devices
 *  - Control three discovery filter parameter from command line,
 *      - auto/bredr/le
 *      - RSSI (0:very close range to -100:far away)
 *      - UUID (only one as of now)
 *  Example run: ./bin/bluez_adapter_filter bredr 100 00001105-0000-1000-8000-00805f9b34fb
 *  - This example scans for new devices after powering the adapter, if any devices
 *    appeared in /org/hciX/dev_XX_YY_ZZ_AA_BB_CC, it is monitered using "InterfaceAdded"
 *    signal and all the properties of the device is printed
 *  - Device will be removed immediately after it appears in InterfacesAdded signal, so
 *    InterfacesRemoved will be called which quits the main loop
 * gcc `pkg-config --cflags glib-2.0 gio-2.0` -Wall -Wextra -o ./bin/bluez_adapter_filter ./bluez_adapter_filter.c `pkg-config --libs glib-2.0 gio-2.0`
 */

GDBusConnection* con(NULL);

void bluez_property_value(const gchar* key, GVariant* value)
{
    const gchar* type = g_variant_get_type_string(value);
    std::cout << "[                   ] \t" << key << " : "; // << std::endl;
    switch (*type) 
    {
    case 's':
        std::cout << g_variant_get_string(value, NULL) << std::endl;
        break;
    case 'b':
        std::cout << g_variant_get_boolean(value) << std::endl;
        break;
    case 'u':
        std::cout << g_variant_get_uint32(value) << std::endl;
        break;
    case 'a':
        std::cout << std::endl;
        const gchar* uuid;
        GVariantIter i;
        g_variant_iter_init(&i, value);
        while (g_variant_iter_next(&i, "s", &uuid))
            std::cout << "[                   ] \t\t" << uuid << std::endl;
        break;
    default:
        std::cout << "Other" << std::endl;
        break;
    }
}

// This is a callback function
void bluez_list_controllers(GDBusConnection* con,
    GAsyncResult* res,
    gpointer data)
{
#ifdef _DEBUG
    std::cout << "[" << getTimeISO8601(true) << "] " << __FUNCTION__ << std::endl;
#endif
    GVariant* result = g_dbus_connection_call_finish(con, res, NULL);
    if (result == NULL)
        g_print("Unable to get result for GetManagedObjects\n");
    else
    /* Parse the result */
    {
        std::vector<gchar>* ControllerPaths = static_cast<std::vector<gchar> *>(data);
        result = g_variant_get_child_value(result, 0);
        GVariantIter i;
        g_variant_iter_init(&i, result);
        const gchar* object_path;
        GVariant* ifaces_and_properties;
        while (g_variant_iter_next(&i, "{&o@a{sa{sv}}}", &object_path, &ifaces_and_properties))
        {
            GVariantIter ii;
            g_variant_iter_init(&ii, ifaces_and_properties);
            const gchar* interface_name;
            GVariant* properties;
            while (g_variant_iter_next(&ii, "{&s@a{sv}}", &interface_name, &properties))
            {
                if (g_strstr_len(g_ascii_strdown(interface_name, -1), -1, "adapter"))
                {
                    std::cout << "[                   ] using [ " << interface_name << " ] [ " << object_path << " ]" << std::endl;
                    ControllerPaths->push_back(*object_path);
                    GVariantIter iii;
                    g_variant_iter_init(&iii, properties);
                    const gchar* property_name;
                    GVariant* prop_val;
                    while (g_variant_iter_next(&iii, "{&sv}", &property_name, &prop_val))
                        bluez_property_value(property_name, prop_val);
                    g_variant_unref(prop_val);
                }
                else
                    std::cout << "[                   ] not using [ " << interface_name << " ] [ " << object_path << " ]" << std::endl;
                g_variant_unref(properties);
            }
            g_variant_unref(ifaces_and_properties);
        }
        g_variant_unref(result);
    }
}

// https://www.mankier.com/5/org.bluez.Adapter
typedef void (*method_cb_t)(GObject*, GAsyncResult*, gpointer);
static int bluez_adapter_call_method(const char* method, GVariant* param, method_cb_t method_cb, const char * adapter_path = "/org/bluez/hci0")
{
#ifdef _DEBUG
    std::cout << "[" << getTimeISO8601(true) << "] " << __FUNCTION__ << " (" << method << ")" << std::endl;
#endif
    GError* error = NULL;

    g_dbus_connection_call(con,
        "org.bluez",
        adapter_path,
        "org.bluez.Adapter1",
        method,
        param,
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        method_cb,
        &error);
    if (error != NULL)
        return 1;
    return 0;
}

static void bluez_get_discovery_filter_cb(GObject* con,
    GAsyncResult* res,
    gpointer data)
{
#ifdef _DEBUG
    std::cout << "[" << getTimeISO8601(true) << "] " << __FUNCTION__ << std::endl;
#endif
    GVariant* result = NULL;
    result = g_dbus_connection_call_finish((GDBusConnection*)con, res, NULL);
    if (result == NULL)
        std::cout << "Unable to get result for GetDiscoveryFilter" << std::endl;
    else
    {
        result = g_variant_get_child_value(result, 0);
        bluez_property_value("GetDiscoveryFilter", result);
    }
    g_variant_unref(result);
}

static void bluez_device_appeared(GDBusConnection* sig,
    const gchar* sender_name,
    const gchar* object_path,
    const gchar* interface,
    const gchar* signal_name,
    GVariant* parameters,
    gpointer user_data)
{
#ifdef _DEBUG
    std::cout << "[" << getTimeISO8601(true) << "] " << __FUNCTION__ << std::endl;
#endif
    GVariantIter* interfaces;
    const char* object;
    g_variant_get(parameters, "(&oa{sa{sv}})", &object, &interfaces);
    const gchar* interface_name;
    GVariant* properties;
    while (g_variant_iter_next(interfaces, "{&s@a{sv}}", &interface_name, &properties))
    {
        if (g_strstr_len(g_ascii_strdown(interface_name, -1), -1, "device"))
        {
            std::cout << "[                   ] [ " << object << " ]" << std::endl;
            GVariantIter i;
            g_variant_iter_init(&i, properties);
            const gchar* property_name;
            GVariant* prop_val;
            while (g_variant_iter_next(&i, "{&sv}", &property_name, &prop_val))
                bluez_property_value(property_name, prop_val);
            g_variant_unref(prop_val);
            g_main_loop_quit((GMainLoop*)user_data);
        }
        g_variant_unref(properties);
    }
}

#define BT_ADDRESS_STRING_SIZE 18
static void bluez_device_disappeared(GDBusConnection* sig,
    const gchar* sender_name,
    const gchar* object_path,
    const gchar* interface,
    const gchar* signal_name,
    GVariant* parameters,
    gpointer user_data)
{
#ifdef _DEBUG
    std::cout << "[" << getTimeISO8601(true) << "] " << __FUNCTION__ << std::endl;
#endif
    GVariantIter* interfaces;
    const char* object;

    g_variant_get(parameters, "(&oas)", &object, &interfaces);
    const gchar* interface_name;
    while (g_variant_iter_next(interfaces, "s", &interface_name))
    {
        if (g_strstr_len(g_ascii_strdown(interface_name, -1), -1, "device"))
        {
            char* tmp = g_strstr_len(object, -1, "dev_") + 4;
            char address[BT_ADDRESS_STRING_SIZE] = { '\0' };
            for (int i = 0; *tmp != '\0'; i++, tmp++)
            {
                if (*tmp == '_')
                {
                    address[i] = ':';
                    continue;
                }
                address[i] = *tmp;
            }
            std::cout << "[                   ] Device " << address << " removed" << std::endl;
            g_main_loop_quit((GMainLoop*)user_data);
        }
    }
    return;
}

static void bluez_signal_adapter_changed(GDBusConnection* conn,
    const gchar* sender,
    const gchar* path,
    const gchar* interface,
    const gchar* signal,
    GVariant* params,
    void* userdata)
{
#ifdef _DEBUG
    std::cout << "[" << getTimeISO8601(true) << "] " << __FUNCTION__ << std::endl;
#endif
    const gchar* signature = g_variant_get_type_string(params);
    GVariantIter* properties = NULL;
    GVariantIter* unknown = NULL;
    GVariant* value = NULL;
    if (strcmp(signature, "(sa{sv}as)") != 0)
    {
        std::cout << "Invalid signature for " << signal << ": " << signature << " != (sa{sv}as)";
        goto done;
    }

    const char* iface;
    g_variant_get(params, "(&sa{sv}as)", &iface, &properties, &unknown);
    const char* key;
    while (g_variant_iter_next(properties, "{&sv}", &key, &value))
    {
        if (!g_strcmp0(key, "Powered"))
        {
            if (!g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN))
            {
                std::cout << "Invalid argument type for " << key << ": " << g_variant_get_type_string(value) << " != b";
                goto done;
            }
            std::cout << "[                   ] Adapter is Powered " << (g_variant_get_boolean(value) ? "on" : "off") << std::endl;
        }
        if (!g_strcmp0(key, "Discovering"))
        {
            if (!g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN))
            {
                std::cout << "Invalid argument type for " << key << ": " << g_variant_get_type_string(value) << " != b";
                goto done;
            }
            std::cout << "[                   ] Adapter scan " << (g_variant_get_boolean(value) ? "on" : "off") << std::endl;
        }
    }
done:
    if (properties != NULL)
        g_variant_iter_free(properties);
    if (value != NULL)
        g_variant_unref(value);
}

static int bluez_adapter_set_property(const char* prop, GVariant* value)
{
    GVariant* result;
    GError* error = NULL;

    result = g_dbus_connection_call_sync(con,
        "org.bluez",
        "/org/bluez/hci0",
        "org.freedesktop.DBus.Properties",
        "Set",
        g_variant_new("(ssv)", "org.bluez.Adapter1", prop, value),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);
    if (error != NULL)
        return 1;

    g_variant_unref(result);
    return 0;
}

static int bluez_set_discovery_filter_govee(void)
{
#ifdef _DEBUG
    std::cout << "[" << getTimeISO8601(true) << "] " << __FUNCTION__ << std::endl;
#endif
    int rc;
    GVariantBuilder* b = g_variant_builder_new(G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(b, "{sv}", "Transport", g_variant_new_string("le"));
    //g_variant_builder_add(b, "{sv}", "RSSI", g_variant_new_int16(-100));
    g_variant_builder_add(b, "{sv}", "DuplicateData", g_variant_new_boolean(TRUE));

    //GVariantBuilder* u = g_variant_builder_new(G_VARIANT_TYPE_STRING_ARRAY);
    //g_variant_builder_add(u, "s", argv[3]);
    //g_variant_builder_add(b, "{sv}", "UUIDs", g_variant_builder_end(u));

    GVariant* device_dict = g_variant_builder_end(b);
    //g_variant_builder_unref(u);
    g_variant_builder_unref(b);

    rc = bluez_adapter_call_method("SetDiscoveryFilter",
        g_variant_new_tuple(&device_dict, 1),
        NULL);
    if (rc)
    {
        std::cout << "Not able to set discovery filter" << std::endl;
        return 1;
    }

    rc = bluez_adapter_call_method("GetDiscoveryFilters",
        NULL,
        bluez_get_discovery_filter_cb);
    if (rc)
    {
        std::cout << "Not able to get discovery filter" << std::endl;
        return 1;
    }

    return 0;
}

static int bluez_set_discovery_filter(char** argv)
{
#ifdef _DEBUG
    std::cout << "[" << getTimeISO8601(true) << "] " << __FUNCTION__ << std::endl;
#endif
    int rc;
    GVariantBuilder* b = g_variant_builder_new(G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(b, "{sv}", "Transport", g_variant_new_string(argv[1]));
    g_variant_builder_add(b, "{sv}", "RSSI", g_variant_new_int16(-g_ascii_strtod(argv[2], NULL)));
    g_variant_builder_add(b, "{sv}", "DuplicateData", g_variant_new_boolean(FALSE));

    GVariantBuilder* u = g_variant_builder_new(G_VARIANT_TYPE_STRING_ARRAY);
    g_variant_builder_add(u, "s", argv[3]);
    g_variant_builder_add(b, "{sv}", "UUIDs", g_variant_builder_end(u));

    GVariant* device_dict = g_variant_builder_end(b);
    g_variant_builder_unref(u);
    g_variant_builder_unref(b);

    rc = bluez_adapter_call_method("SetDiscoveryFilter", 
        g_variant_new_tuple(&device_dict, 1), 
        NULL);
    if (rc)
    {
        std::cout << "Not able to set discovery filter" << std::endl;
        return 1;
    }

    rc = bluez_adapter_call_method("GetDiscoveryFilters",
        NULL,
        bluez_get_discovery_filter_cb);
    if (rc)
    {
        std::cout << "Not able to get discovery filter" << std::endl;
        return 1;
    }

    return 0;
}

int main(int argc, char** argv)
{
    con = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (con == NULL) 
    {
        std::cout << "Not able to get connection to system bus" << std::endl;
        return 1;
    }

    // https://docs.gtk.org/glib/ctor.MainLoop.new.html
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);

    std::vector<gchar> ControllerPaths;

    // https://docs.gtk.org/gio/method.DBusConnection.call.html
    g_dbus_connection_call(con,
        "org.bluez",
        "/",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects",
        NULL,
        G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        (GAsyncReadyCallback)bluez_list_controllers,
        &ControllerPaths);

    // https://docs.gtk.org/gio/method.DBusConnection.signal_subscribe.html
    guint prop_changed = g_dbus_connection_signal_subscribe(con,
        "org.bluez",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        NULL,
        "org.bluez.Adapter1",
        G_DBUS_SIGNAL_FLAGS_NONE,
        bluez_signal_adapter_changed,
        NULL,
        NULL);

    guint iface_added = g_dbus_connection_signal_subscribe(con,
        "org.bluez",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesAdded",
        NULL,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        bluez_device_appeared,
        loop,
        NULL);

    guint iface_removed = g_dbus_connection_signal_subscribe(con,
        "org.bluez",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesRemoved",
        NULL,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        bluez_device_disappeared,
        loop,
        NULL);

    int rc = bluez_adapter_set_property("Powered", g_variant_new("b", TRUE));
    if (rc) 
    {
        std::cout << "Not able to enable the adapter" << std::endl;
        goto fail;
    }

    if (argc > 3)
    {
        rc = bluez_set_discovery_filter(argv);
        if (rc)
            goto fail;
    }
    else
    {
        bluez_set_discovery_filter_govee();
        bluez_adapter_call_method("GetDiscoveryFilters",
            NULL,
            bluez_get_discovery_filter_cb);
    }

    rc = bluez_adapter_call_method("StartDiscovery", NULL, NULL);
    if (rc)
    {
        std::cout << "Not able to scan for new devices" << std::endl;
        goto fail;
    }

    g_main_loop_run(loop);

    if (argc > 3)
    {
        rc = bluez_adapter_call_method("SetDiscoveryFilter", NULL, NULL);
        if (rc)
            std::cout << "Not able to remove discovery filter" << std::endl;
    }

    rc = bluez_adapter_call_method("StopDiscovery", NULL, NULL);
    if (rc)
        std::cout << "Not able to stop scanning" << std::endl;
    g_usleep(100);

    rc = bluez_adapter_set_property("Powered", g_variant_new("b", FALSE));
    if (rc)
        std::cout << "Not able to disable the adapter" << std::endl;
fail:
    g_dbus_connection_signal_unsubscribe(con, prop_changed);
    g_dbus_connection_signal_unsubscribe(con, iface_added);
    g_dbus_connection_signal_unsubscribe(con, iface_removed);
    g_object_unref(con);
    return 0;
}