CC = gcc
CFLAGS = $(shell pkg-config --cflags gtk+-3.0 dbusmenu-gtk3-0.4 gio-2.0)
LIBS = $(shell pkg-config --libs gtk+-3.0 dbusmenu-gtk3-0.4 gio-2.0)

All:
	$(CC) panel.c -o my_panel $(CFLAGS) $(LIBS)
