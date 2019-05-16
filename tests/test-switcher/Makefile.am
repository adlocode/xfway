bin_PROGRAMS = test-switcher

test_switcher_SOURCES = \
$(top_srcdir)/protocol/window-switcher-unstable-v1-server-protocol.c \
$(top_srcdir)/protocol/window-switcher-unstable-v1-client-protocol.h \
$(top_srcdir)/protocol/xfway-shell-client-protocol.c \
$(top_srcdir)/protocol/xfway-shell-client-protocol.h \
switcher-test.c

test_switcher_CFLAGS = \
$(WAYLAND_SERVER_CFLAGS) \
$(WAYLAND_CLIENT_CFLAGS) \
$(LIBWESTON_CFLAGS) \
$(LIBWESTON_DESKTOP_CFLAGS) \
$(LIBINPUT_CFLAGS) \
$(XKBCOMMON_CFLAGS) \
$(EGL_CFLAGS) \
$(EVDEV_CFLAGS) \
$(GTK_CFLAGS)

test_switcher_LDADD = \
$(WAYLAND_SERVER_LIBS) \
$(WAYLAND_CLIENT_LIBS) \
$(LIBWESTON_LIBS) \
$(LIBWESTON_DESKTOP_LIBS) \
$(LIBINPUT_LIBS) \
$(XKBCOMMON_LIBS) \
$(EGL_LIBS) \
$(EVDEV_LIBS) \
$(PIXMAN_LIBS) \
$(GTK_LIBS)
