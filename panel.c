// MIT License
// Lukas Veselovsky, lukve
//
// Created with magician help of the AI.
//

#include "gtk/gtk.h"
#include <time.h>
#include <libdbusmenu-gtk/menu.h>
#include <gio/gio.h> // Potrebné pre GDBus komunikáciu

#define WIDTH 1920
#define HEIGHT 24

const char *CSS_STYLE = 
    "#my-panel-window {"
    "   background-color: rgba(200, 200, 200, 0.75);"
    "   border-radius: 4px;"
    "   border-top-left: 1px solid rgba(0, 0, 0, 0.75);"
    "	border-top-righ: 1px solid rgba(0, 0, 0, 0.75);"
    "	color: black;"
    "}"
    "#my-panel-window :hover { opacity: 0.1; }";

// Globálne ukazovatele, aby sme k nim mali prístup pri zmene aplikácie
GtkWidget *menu_container = NULL;
GtkWidget *current_menu = NULL;
GtkWidget *placeholder_label = NULL; // Label, ktorý sa zobrazí, ak aplikácia nemá menu

/* 
   Funkcia na aktualizáciu hodín. 
   Spúšťa sa automaticky každú sekundu.
*/
static gboolean update_clock(gpointer label) {
    time_t rawtime;
    struct tm *timeinfo;
    char buffer[40]; // Opravené: pridaná dostatočná veľkosť poľa znakov

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    // Formát: "Pon 21:45:00"
    strftime(buffer, sizeof(buffer), "%a %H:%M:%S", timeinfo);
    
    gtk_label_set_text(GTK_LABEL(label), buffer);
    return TRUE; // TRUE zabezpečí, že časovač pokračuje ďalej
}

/*
   Aktualizácia globálneho menu na základe D-Bus dát.
   Túto funkciu volá D-Bus subsystém pri zmene okna.
*/
void update_global_menu(const char *bus_name, const char *object_path) {
    if (!menu_container) return;

    // Ak už nejaké menu svieti, odstránime ho z panela
    if (current_menu) {
        gtk_widget_destroy(current_menu);
        current_menu = NULL;
    }

    // Ak máme platné dáta o D-Bus menu novej aplikácie, vytvoríme ho
    if (bus_name && object_path && strlen(bus_name) > 0 && strlen(object_path) > 0) {
        // Skryjeme text "AppMenu", keďže ideme zobraziť reálne menu aplikácie
        if (placeholder_label) {
            gtk_widget_hide(placeholder_label);
        }

        // Vytvorenie GTK Menu z D-Bus zdroja pomocou libdbusmenu-gtk
        current_menu = GTK_WIDGET(dbusmenu_gtkmenu_new((char *)bus_name, (char *)object_path));
        
        if (current_menu) {
            gtk_box_pack_start(GTK_BOX(menu_container), current_menu, FALSE, FALSE, 0);
            gtk_widget_show_all(current_menu);
        }
    } else {
        // Ak žiadna aplikácia nemá menu, vrátime na panel pôvodný text "AppMenu"
        if (placeholder_label) {
            gtk_widget_show(placeholder_label);
        }
    }
}

/*
   Callback funkcia, ktorá zachytáva D-Bus signál 'WindowChanged'
   z rozhrania com.canonical.AppMenu.Registrar.
*/
static void on_dbus_signal(GDBusConnection *connection,
                           const gchar *sender_name,
                           const gchar *object_path,
                           const gchar *interface_name,
                           const gchar *signal_name,
                           GVariant *parameters,
                           gpointer user_data) {
    
    // Sledujeme signál zmeny aktívneho okna
    if (g_strcmp0(signal_name, "WindowChanged") == 0) {
        guint32 window_id;
        const gchar *bus_name = NULL;
        const gchar *menu_path = NULL;

        // Formát parametrov pre WindowChanged je (uay) -> WindowID, BusName, ObjectPath
        g_variant_get(parameters, "(u&s&s)", &window_id, &bus_name, &menu_path);

        // Zavoláme aktualizáciu menu s novými adresami
        update_global_menu(bus_name, menu_path);
    }
}

/*
   Pomocná funkcia na asynchrónne získanie aktuálneho menu hneď po štarte panelu.
*/
static void get_current_menu_on_start(GDBusConnection *connection) {
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        connection,
        "com.canonical.AppMenu.Registrar",
        "/com/canonical/AppMenu/Registrar",
        "com.canonical.AppMenu.Registrar",
        "GetMenuForWindow",
        g_variant_new("(u)", 0), // 0 reprezentuje aktuálne aktívne okno
        G_VARIANT_TYPE("(ss)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1, NULL, &error
    );

    if (result) {
        const gchar *bus_name = NULL;
        const gchar *menu_path = NULL;
        g_variant_get(result, "(&s&s)", &bus_name, &menu_path);
        update_global_menu(bus_name, menu_path);
        g_variant_unref(result);
    } else {
        g_clear_error(&error);
    }
}

/*
   Inicializácia D-Bus pripojenia a registrácia odberu signálov.
*/
static void setup_dbus_menu(void) {
    GError *error = NULL;
    
    // Pripojíme sa na používateľskú Session zbernicu
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

    if (error) {
        g_printerr("Chyba pripojenia k D-Bus: %s\n", error->message);
        g_clear_error(&error);
        return;
    }

    // Prihlásime sa na odber signálov od globálneho AppMenu registra systému
    g_dbus_connection_signal_subscribe(
        connection,
        "com.canonical.AppMenu.Registrar", // Bus name odosielateľa
        "com.canonical.AppMenu.Registrar", // Názov rozhrania
        NULL,                              // Sledujeme akýkoľvek signál (napr. WindowChanged)
        "/com/canonical/AppMenu/Registrar",// Cesta k objektu
        NULL,                              // Bez dodatočného filtrovania argumentov
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_dbus_signal,                    // Naša funkcia na spracovanie správ
        NULL, NULL
    );

    // Pokúsime sa zistiť menu aplikácie, ktorá je aktívna práve teraz pri štarte panelu
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
    // Inicializácia GTK
    gtk_init(&argc, &argv);
 
    // 1. Vytvorenie hlavného okna panelu
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    enable_alpha_channel(window);
    gtk_widget_set_name(window, "my-panel-window");
    gtk_window_set_title(GTK_WINDOW(window), "AppMenu Panel");
    
    gtk_window_move(GTK_WINDOW(window), 0, 0);

    apply_css_style();

    // Nastavenie rozmerov
    gtk_window_set_default_size(GTK_WINDOW(window), WIDTH, HEIGHT);
    
    // Nastavenie vlastnosti DOCK (odstráni okraje a integruje okno ako panel)
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DOCK);

    // 2. Hlavný horizontálny box
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    // 3. ĽAVÁ STRANA: Kontajner pre globálne menu aplikácie
    menu_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(main_box), menu_container, FALSE, FALSE, 10); // 10px odstup zľava

    // Východiskový text na paneli
    placeholder_label = gtk_label_new("AppMenu");
    gtk_box_pack_start(GTK_BOX(menu_container), placeholder_label, FALSE, FALSE, 0);

    // 4. PRAVÁ STRANA: Vytvorenie hodín
    GtkWidget *clock_label = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(main_box), clock_label, FALSE, FALSE, 10); // 10px odstup sprava

    // Spustenie časovača pre hodiny (každých 1000 ms / 1 sekunda)
    update_clock(clock_label);
    g_timeout_add(1000, update_clock, clock_label);

    // Inicializácia D-Bus prijímača pre menu
    setup_dbus_menu();

    // Reakcia na zatvorenie okna
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Zobrazenie všetkých prvkov
    gtk_widget_show_all(window);

    // Spustenie hlavnej slučky GTK
    gtk_main();

    return 0;
}
