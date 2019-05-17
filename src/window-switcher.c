/* Copyright (C) 2019 adlo
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include <wayland-server.h>
#include <compositor.h>
#include <libweston-desktop.h>
#include <protocol/window-switcher-unstable-v1-server-protocol.h>
#include <gtk/gtk.h>

struct weston_window_switcher
{
  struct weston_compositor *compositor;
  struct wl_client *client;
  struct wl_resource *binding;
  struct wl_list windows;
};

struct weston_window_switcher_window
{
  struct wl_list link;
  struct weston_window_switcher *switcher;
  struct wl_resource *resource;
  struct weston_desktop_surface *surface;
  struct weston_view *view;
  struct wl_listener surface_destroy_listener;
  struct wl_listener view_destroy_listener;
};

static void _weston_window_switcher_request_destroy (struct wl_client   *client,
                                                     struct wl_resource *resource)
{

}

static const struct zww_window_switcher_v1_interface weston_window_switcher_implementation =
{
  .destroy = _weston_window_switcher_request_destroy,
};

static void
_weston_window_switcher_window_surface_destroyed (struct wl_listener *listener,
                                                  void               *data)
{

}

static void
_weston_window_switcher_window_destroy (struct wl_resource *resource)
{

}

static void
_weston_window_switcher_window_create (struct weston_window_switcher *switcher,
                                       struct weston_surface         *surface)
{
  struct weston_window_switcher_window *self;
  struct weston_desktop_surface *dsurface = weston_surface_get_desktop_surface (surface);

  if (dsurface == NULL)
    return;

  weston_log ("\nserver: window create\n");

  wl_list_for_each (self,&switcher->windows, link)
    {
      if (self->surface == dsurface)
        return;
    }

  self = zalloc (sizeof (struct weston_window_switcher_window));
  if (self == NULL)
    {
      wl_client_post_no_memory (switcher->client);
      return;
    }

  self->switcher = switcher;
  self->surface = dsurface;

  self->resource = wl_resource_create (switcher->client, &zww_window_switcher_window_v1_interface,
                                       wl_resource_get_version (switcher->binding), 0);
  if (self->resource == NULL)
    {
      wl_client_post_no_memory (switcher->client);
      return;
    }

  self->surface_destroy_listener.notify = _weston_window_switcher_window_surface_destroyed;
  wl_signal_add (&surface->destroy_signal, &self->surface_destroy_listener);
  wl_resource_set_implementation(self->resource, &weston_window_switcher_implementation,
                                 self, _weston_window_switcher_window_destroy);
  zww_window_switcher_v1_send_window (switcher->binding, self->resource);

}

static void
_weston_window_switcher_bind (struct wl_client *client,
                              void             *data,
                              uint32_t          version,
                              uint32_t          id)
{
  struct weston_window_switcher *self = data;
  struct wl_resource *resource;

  weston_log ("\nserver: switcher bind\n");

  resource = wl_resource_create (client, &zww_window_switcher_v1_interface, version, id);
  wl_resource_set_implementation (resource, &weston_window_switcher_implementation,
                                  self, NULL);

  if (self->binding != NULL)
    {
      wl_resource_post_error (resource, ZWW_WINDOW_SWITCHER_V1_ERROR_BOUND,
                              "interface object already bound");
      wl_resource_destroy (resource);
      return;
    }

  self->client = client;
  self->binding = resource;

  struct weston_view *view;
  wl_list_for_each (view, &self->compositor->view_list, link)
    _weston_window_switcher_window_create (self, view->surface);
}

WL_EXPORT int
weston_window_switcher_module_init (struct weston_compositor *compositor,
                                    int argc, char *argv[])
{
  struct weston_window_switcher *window_switcher;
  window_switcher = zalloc (sizeof (struct weston_window_switcher));

  window_switcher->compositor = compositor;

  wl_list_init (&window_switcher->windows);

  if (wl_global_create (window_switcher->compositor->wl_display,
                        &zww_window_switcher_v1_interface, 1,
                        window_switcher, _weston_window_switcher_bind) == NULL)
    return -1;

  return 0;
}
