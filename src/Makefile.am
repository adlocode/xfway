bin_PROGRAMS = xfway xfway-shell

AM_CPPFLAGS = 					\
	-DBINDIR='"$(bindir)"'			\
	-DLIBEXECDIR='"$(libexecdir)"'		

xfway_SOURCES = \
$(top_srcdir)/protocol/xfway-shell-client-protocol.c \
$(top_srcdir)/protocol/xfway-shell-client-protocol.h \
$(top_srcdir)/protocol/xfway-shell-server-protocol.h \
$(top_srcdir)/protocol/window-switcher-unstable-v1-server-protocol.c \
$(top_srcdir)/protocol/window-switcher-unstable-v1-server-protocol.h \
$(top_srcdir)/util/signal.c \
$(top_srcdir)/util/signal.h \
$(top_srcdir)/protocol/wlr-foreign-toplevel-management-unstable-v1-protocol.c \
$(top_srcdir)/wlr-foreign-toplevel-management-unstable-v1-protocol.h \
wlr_foreign_toplevel_management_v1.c \
wlr_foreign_toplevel_management_v1.h \
$(top_srcdir)/protocol/wlr-layer-shell-unstable-v1-protocol.c \
$(top_srcdir)/protocol/wlr-layer-shell-unstable-v1-protocol.h \
wlr_layer_shell_v1.c \
wlr_layer_shell_v1.h \
$(top_srcdir)/protocol/xdg-shell.c \
$(top_srcdir)/protocol/xdg-shell.h \
os-compatibility.c \
os-compatibility.h \
$(top_srcdir)/util/helpers.h \
xfway.h \
window-switcher.c \
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
$(GTK_CFLAGS) \
$(LIBXFCONF_CFLAGS)

xfway_LDADD = \
$(WAYLAND_SERVER_LIBS) \
$(LIBWESTON_LIBS) \
$(LIBWESTON_DESKTOP_LIBS) \
$(LIBINPUT_LIBS) \
$(XKBCOMMON_LIBS) \
$(EGL_LIBS) \
$(EVDEV_LIBS) \
$(PIXMAN_LIBS) \
$(GTK_LIBS) \
$(LIBXFCONF_LIBS)

xfway_shell_SOURCES = \
$(top_srcdir)/protocol/xfway-shell-client-protocol.c \
$(top_srcdir)/protocol/xfway-shell-client-protocol.h \
$(top_srcdir)/protocol/wlr-foreign-toplevel-management-unstable-v1-protocol.c \
$(top_srcdir)/protocol/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h \
display.h \
screen.c \
screen.h \
client.c \
client.h \
stacking.c \
stacking.h \
settings.h \
tabwin.c \
tabwin.h \
$(top_srcdir)/common/xfwm-common.c \
$(top_srcdir)/common/xfwm-common.h \
$(top_srcdir)/util/libgwater-wayland.c \
$(top_srcdir)/util/libgwater-wayland.h \
$(top_srcdir)/util/helpers.h \
main-shell-client.c

xfway_shell_CFLAGS = \
$(WAYLAND_CLIENT_CFLAGS) \
$(WAYLAND_PROTOCOLS_CFLAGS) \
$(GTK_CFLAGS)

xfway_shell_LDADD = \
$(WAYLAND_CLIENT_LIBS) \
$(WAYLAND_PROTOCOLS_LIBS) \
$(GTK_LIBS)

