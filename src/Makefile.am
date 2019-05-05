bin_PROGRAMS = xfway

xfway_SOURCES = \
$(top_srcdir)/protocol/xfway-shell-client-protocol.c \
$(top_srcdir)/protocol/xfway-shell-client-protocol.h \
$(top_srcdir)/protocol/xfway-shell-server-protocol.h \
shell.c \
shell.h \
server.h \
main-wayland.c

xfway_CFLAGS = \
$(WAYLAND_SERVER_CFLAGS) \
$(LIBWESTON_CFLAGS) \
$(LIBWESTON_DESKTOP_CFLAGS) \
$(LIBINPUT_CFLAGS) \
$(XKBCOMMON_CFLAGS) \
$(EGL_CFLAGS) \
$(EVDEV_CFLAGS) \
$(GTK_CFLAGS)

xfway_LDADD = \
$(WAYLAND_SERVER_LIBS) \
$(LIBWESTON_LIBS) \
$(LIBWESTON_DESKTOP_LIBS) \
$(LIBINPUT_LIBS) \
$(XKBCOMMON_LIBS) \
$(EGL_LIBS) \
$(EVDEV_LIBS) \
$(PIXMAN_LIBS) \
$(GTK_LIBS)
