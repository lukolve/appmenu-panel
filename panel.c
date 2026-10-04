// MIT License
// Lukas Veselovsky, lukve
//
// Created with magician help of the AI.
//

#include "gtk/gtk.h"
#include <time.h>
#include <libdbusmenu-gtk/menu.h>
#include <libdbusmenu-gtk/client.h>
#include <gio/gio.h> 
#include <stdio.h>
#include <stdlib.h>

#define WIDTH 1920
#define HEIGHT 24

const char *CSS_STYLE = 
    "#my-panel-window {"
    "   background-color: rgba(200, 200, 200, 0.75);"
    "   border-radius: 4px;"
    "   color: black;"
    "}"
    "#my-panel-window :hover { opacity: 0.1; }";

// Globálne ukazovatele pre prístup pri zmene aktívnej aplikácie
GtkWidget *menu_container = NULL;
GtkWidget *current_menu = NULL;
GtkWidget *placeholder_label = NULL; 
DbusmenuGtkClient *current_client = NULL; 

/* 
   Pomocná funkcia na zistenie stavu batérie zo systému.
*/
static void get_battery_status(char *buffer, size_t max_len) {
    FILE *f_cap = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    FILE *f_stat = fopen("/sys/class/power_supply/BAT0/status", "r");
    
    // Ak BAT0 neexistuje, skúsime BAT1 (časté pri niektorých notebookoch)
    if (!f_cap) {
        f_cap = fopen("/sys/class/power_supply/BAT1/capacity", "r");
        f_stat = fopen("/sys/class/power_supply/BAT1/status", "r");
    }

    if (f_cap && f_stat) {
        int capacity = 0;
        char status[32] = {0};
        
        if (fscanf(f_cap, "%d", &capacity) == 1 && fscanf(f_stat, "%31s", status) == 1) {
            const char *icon = "🔋";
            // Ak sa batéria nabíja, zmeníme ikonu alebo pridáme symbol
            if (g_str_has_prefix(status, "Charg")) {
                icon = "⚡";
            }
            snprintf(buffer, max_len, "%s %d%%", icon, capacity);
        } else {
            snprintf(buffer, max_len, "BAT N/A");
        }
        
        fclose(f_cap);
        fclose(f_stat);
    } else {
        // Ak počítač nemá batériu (napr. PC / VM), nevypíšeme nič
        if (f_cap) fclose(f_cap);
        if (f_stat) fclose(f_stat);
        snprintf(buffer, max_len, "");
    }
}

/* 
   Funkcia na aktualizáciu hodín a batérie (spúšťa sa každú sekundu).
*/
static gboolean update_clock(gpointer user_data) {
    GtkWidget **labels = (GtkWidget **)user_data;
    GtkWidget *clock_label = labels[0];
    GtkWidget *bat_label = labels[1];

    // 1. Aktualizácia hodín
    time_t rawtime;
    struct tm *timeinfo;
    char time_buffer[40]; 

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_buffer, sizeof(time_buffer), "%a %H:%M:%S", timeinfo);
    gtk_label_set_text(GTK_LABEL(clock_label), time_buffer);

    // 2. Aktualizácia batérie
    char bat_buffer[32];
    get_battery_status(bat_buffer, sizeof(bat_buffer));
    gtk_label_set_text(GTK_LABEL(bat_label), bat_buffer);

    return TRUE; 
}

// Pomocný callback, ktorý prenesie položky z D-Bus klienta do GtkMenuBar
static void on_child_added(DbusmenuGtkClient *client, DbusmenuMenuitem *child, guint position, gpointer user_data) {
    GtkMenuShell *menu_bar = GTK_MENU_SHELL(user_data);
    GtkMenuItem *gtk_item = dbusmenu_gtkclient_menuitem_get(client, child);
    
    if (gtk_item) {
        gtk_menu_shell_insert(menu_bar, GTK_WIDGET(gtk_item), position);
        gtk_widget_show_all(GTK_WIDGET(gtk_item));
    }
}

/*
   Aktualizácia globálneho menu na základe D-Bus dát.
*/
void update_global_menu(const char *bus_name, const char *object_path) {
    if (!menu_container) return;

    if (current_menu) {
        gtk_widget_destroy(current_menu);
        current_menu = NULL;
    }
    if (current_client) {
        g_object_unref(current_client);
        current_client = NULL;
    }

    if (bus_name && object_path && strlen(bus_name) > 0 && strlen(object_path) > 0) {
        if (placeholder_label) {
            gtk_widget_hide(placeholder_label);
        }

        current_menu = gtk_menu_bar_new();
        current_client = dbusmenu_gtkclient_new((char *)bus_name, (char *)object_path);
        
        g_signal_connect(current_client, "child-added", G_CALLBACK(on_child_added), current_menu);

        DbusmenuMenuitem *root = dbusmenu_client_get_root(DBUSMENU_CLIENT(current_client));
        if (root) {
            GList *children = dbusmenu_menuitem_get_children(root);
            guint pos = 0;
            for (GList *l = children; l != NULL; l = l->next) {
                on_child_added(current_client, DBUSMENU_MENUITEM(l->data), pos++, current_menu);
            }
        }

        if (current_menu) {
            gtk_box_pack_start(GTK_BOX(menu_container), current_menu, FALSE, FALSE, 0);
            gtk_widget_show_all(current_menu);
        }
    } else {
        if (placeholder_label) {
            gtk_widget_show(placeholder_label);
        }
    }
}

/*
   Sledovanie D-Bus signálu 'WindowChanged'
*/
static void on_dbus_signal(GDBusConnection *connection,
                           const gchar *sender_name,
                           const gchar *object_path,
                           const gchar *interface_name,
                           const gchar *signal_name,
                           GVariant *parameters,
                           gpointer user_data) {
    
    if (g_strcmp0(signal_name, "WindowChanged") == 0) {
        guint32 window_id;
        const gchar *bus_name = NULL;
        const gchar *menu_path = NULL;

        g_variant_get(parameters, "(u&s&s)", &window_id, &bus_name, &menu_path);
        update_global_menu(bus_name, menu_path);
    }
}

/*
   Asynchrónne získanie aktuálneho menu ihneď po štarte panelu.
*/
static void get_current_menu_on_start(GDBusConnection *connection) {
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        connection,
        "com.canonical.AppMenu.Registrar",
        "/com/canonical/AppMenu/Registrar",
        "com.canonical.AppMenu.Registrar",
        "GetMenuForWindow",
        g_variant_new("(u)", 0), 
        G_VARIANT_TYPE("(ss)"),  
        G_DBUS_CALL_FLAGS_NONE,
        -1, NULL, &error
    );

    if (result) {
        const gchar *bus_name = NULL;
        const gchar *menu_path = NULL;
        g_variant_get(result, "(&s&s)", &bus_name, &menu_path);
        g_print("-> Úspech! Načítané menu pre prvé okno: %s, %s\n", bus_name, menu_path);
        update_global_menu(bus_name, menu_path);
        g_variant_unref(result);
    } else {
        g_printerr("-> D-Bus chyba pri štarte: %s\n", error->message);
        g_clear_error(&error);
    }
}

/*
   Inicializácia D-Bus pripojenia a registrácia odberu signálov.
*/
static void setup_dbus_menu(void) {
    GError *error = NULL;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

    if (error) {
        g_printerr("Chyba pripojenia k D-Bus: %s\n", error->message);
        g_clear_error(&error);
        return;
    }

    g_dbus_connection_signal_subscribe(
        connection,
        "com.canonical.AppMenu.Registrar", 
        "com.canonical.AppMenu.Registrar", 
        NULL,                              
        "/com/canonical/AppMenu/Registrar",
        NULL,                              
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_dbus_signal,                    
        NULL, NULL
    );

    get_current_menu_on_start(connection);
}

void enable_alpha_channel(GtkWidget *window) {
    GdkScreen *gdk_screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(gdk_screen);
    if (visual != NULL && gdk_screen_is_composited(gdk_screen)) {
        gtk_widget_set_visual(window, visual);
    }
}

void apply_css_style(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS_STYLE, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
 
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    enable_alpha_channel(window);
    gtk_widget_set_name(window, "my-panel-window");
    gtk_window_set_title(GTK_WINDOW(window), "AppMenu Panel");
    
    gtk_window_move(GTK_WINDOW(window), 0, 0);
    apply_css_style();

    gtk_window_set_default_size(GTK_WINDOW(window), WIDTH, HEIGHT);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DOCK);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    // Ľavá strana: Menu
    menu_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(main_box), menu_container, FALSE, FALSE, 10); 

    placeholder_label = gtk_label_new("AppMenu");
    gtk_box_pack_start(GTK_BOX(menu_container), placeholder_label, FALSE, FALSE, 0);

    // Pravá strana: Hodiny a Batéria
    GtkWidget *clock_label = gtk_label_new("");
    GtkWidget *bat_label = gtk_label_new("");
    
    // Pridáme najprv hodiny úplne doprava, potom batériu vedľa nich (s 10px medzerou)
    gtk_box_pack_end(GTK_BOX(main_box), clock_label, FALSE, FALSE, 10); 
    gtk_box_pack_end(GTK_BOX(main_box), bat_label, FALSE, FALSE, 10); 

    // Pole ukazovateľov, ktoré pošleme do časovača, aby vedel aktualizovať oba prvky naraz
    GtkWidget *status_labels[2] = { clock_label, bat_label };

    update_clock(status_labels);
    g_timeout_add(1000, update_clock, status_labels);

    setup_dbus_menu();

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
