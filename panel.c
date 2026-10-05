// MIT License
// Lukas Veselovsky, lukve
//
// Created with magician help of the AI.
//

#include "gtk/gtk.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#define WIDTH 1920
#define HEIGHT 24

void init_menu_system(GtkWidget *container, GtkWidget *placeholder);
void setup_dbus_menu(void);


const char *CSS_STYLE = 
    "#my-panel-window {"
    "   background-color: #dfdfdf;" /* Pevné tmavé pozadie (elegantná tmavomodrá/sivá)  #2c3e50 */
    "   color: black;"            /* Biely text pre názov aplikácie */
	"	border-color: black;"
  	"	border-top-left-radius: 3px;"
	"	border-top-right-radius: 3px;"
    "   border-bottom: 1px solid rgba(0, 0, 0, 0.15);"
    "   transition: filter 0.2s ease;"
    "}"
    "#my-panel-window menu, #my-panel-window menubar, #my-panel-window menuitem {"
    "   background-color: #dfdfdf;"   /* Vynúti čisté pozadie pre lištu menu */
    "   color: black;"            /* Vynúti jasný biely text pre položky menu (Súbor, Upraviť...) */
    "}"
    "#my-panel-window menuitem:hover {"
    "   background-color: grey;" /* Zvýraznenie položky na sivo pri nabehnutí myšou */
    "}"
    "#my-panel-window label {"
    "   padding: 0 2px;"
    "}";

static void get_battery_status(char *buffer, size_t max_len) {
    FILE *f_cap = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    FILE *f_stat = fopen("/sys/class/power_supply/BAT0/status", "r");
    
    if (!f_cap) {
        f_cap = fopen("/sys/class/power_supply/BAT1/capacity", "r");
        f_stat = fopen("/sys/class/power_supply/BAT1/status", "r");
    }

    if (f_cap && f_stat) {
        int capacity = 0;
        char status[32] = {0};
        
        if (fscanf(f_cap, "%d", &capacity) == 1 && fscanf(f_stat, "%31s", status) == 1) {
            const char *icon = "🔋";
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
        if (f_cap) fclose(f_cap);
        if (f_stat) fclose(f_stat);
        snprintf(buffer, max_len, "");
    }
}

static gboolean update_clock(gpointer user_data) {
    GtkWidget **labels = (GtkWidget **)user_data;
    GtkWidget *clock_label = labels[0];
    GtkWidget *bat_label = labels[1];

    time_t rawtime;
    struct tm *timeinfo;
    char time_buffer[40]; 

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_buffer, sizeof(time_buffer), "%a %H:%M:%S", timeinfo);
    gtk_label_set_text(GTK_LABEL(clock_label), time_buffer);

    char bat_buffer[32];
    get_battery_status(bat_buffer, sizeof(bat_buffer));
    gtk_label_set_text(GTK_LABEL(bat_label), bat_buffer);

    return TRUE; 
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

static void suppress_dbusmenu_warnings(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
    // Schválne prázdne - pohlcuje varovania z libdbusmenu
}

int main(int argc, char *argv[]) {
    g_log_set_handler("LIBDBUSMENU-GLIB", G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL, suppress_dbusmenu_warnings, NULL);

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

    GtkWidget *menu_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(main_box), menu_container, FALSE, FALSE, 10); 

    GtkWidget *placeholder_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(menu_container), placeholder_label, FALSE, FALSE, 0);

    GtkWidget *clock_label = gtk_label_new("");
    GtkWidget *bat_label = gtk_label_new("");
    
    gtk_box_pack_end(GTK_BOX(main_box), clock_label, FALSE, FALSE, 10); 
    gtk_box_pack_end(GTK_BOX(main_box), bat_label, FALSE, FALSE, 10); 

    GtkWidget **status_labels = g_new0(GtkWidget *, 2);
    status_labels[0] = clock_label;
    status_labels[1] = bat_label;

    update_clock(status_labels);
    g_timeout_add(1000, update_clock, status_labels);

    init_menu_system(menu_container, placeholder_label);
    setup_dbus_menu();

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    g_free(status_labels);
    return 0;
}

