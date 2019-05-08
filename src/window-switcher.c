#include <wayland-server.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <protocol/window-switcher-unstable-v1-server-protocol.h>

struct weston_window_switcher
{
  struct weston_compositor *compositor;
  struct wl_client *client;
  struct wl_resource *binding;
  struct wl_list windows;
};

static const struct zww_window_switcher_v1_interface weston_window_switcher_implementation =
{
  .destroy = NULL
};

static void
_weston_window_switcher_bind (struct wl_client *client,
                              void             *data,
                              uint32_t          version,
                              uint32_t          id)
{
  struct weston_window_switcher *self = data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &zww_window_switcher_v1_interface, version, id);
  wl_resource_set_implementation (resource, &weston_window_switcher_implementation,
                                  self, NULL);
}

WL_EXPORT int
weston_window_switcher_module_init (struct weston_compositor *compositor)
{
  struct weston_window_switcher *window_switcher;
  window_switcher = zalloc (sizeof (struct weston_window_switcher));

  window_switcher->compositor = compositor;

  wl_list_init (&window_switcher->windows);

  if (wl_global_create (window_switcher->compositor->wl_display,
                        &zww_window_switcher_window_v1_interface, 1,
                        window_switcher, _weston_window_switcher_bind) == NULL)
    return -1;

  return 0;
}