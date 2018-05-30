/* Copyright (C) 2018 adlo
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

#define _GNU_SOURCE
#include <wayland-server.h>
#include <compositor.h>
#include <compositor-drm.h>
#include <compositor-wayland.h>
#include <libweston-desktop.h>
#include <libinput.h>
#include <string.h>
#include <windowed-output-api.h>
#include <stdio.h>
#include <stdlib.h>

struct TestServer
{
  struct wl_listener new_output;
  const struct weston_windowed_output_api *api;
};

void surface_added (struct weston_desktop_surface *desktop_surface,
                    void                   *data)
{

}

void surface_removed (struct weston_desktop_surface *desktop_surface,
                      void                   *data)
{

}
static int vlog (const char *fmt,
                 va_list     ap)
{
  return vfprintf (stderr, fmt, ap);
}

static int vlog_continue (const char *fmt,
                          va_list     argp)
{
  return vfprintf (stderr, fmt, argp);
}

static void new_output_notify (struct wl_listener *listener,
                            void               *data)
{
  struct weston_output *output = data;
  struct TestServer *server = wl_container_of (listener, server, new_output);

  weston_output_set_scale (output, 1);
  weston_output_set_transform (output, WL_OUTPUT_TRANSFORM_NORMAL);
  server->api->output_set_size (output, 800, 600);
  weston_output_enable (output);

}
int main (int    argc,
          char **argv)
{
	struct wl_display *display;
	struct weston_compositor *ec = NULL;
	int ret = 0;
  const char *socket_name = NULL;
  struct weston_desktop_api desktop_api;
  struct weston_desktop *desktop;
  struct TestServer *server;

  server = malloc (sizeof(struct TestServer));

	display = wl_display_create ();
	ec = weston_compositor_create (display, NULL);

	if (!ec)
		return 0;

  weston_log_set_handler (vlog, vlog_continue);

	ec->default_pointer_grab = NULL;
	ec->vt_switching = true;

	ec->repaint_msec = 16;
	ec->idle_time = 300;

	struct weston_wayland_backend_config config = {{0, }};

	config.base.struct_version = WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
	config.base.struct_size = sizeof (struct weston_wayland_backend_config);

	config.cursor_size = 32;
	config.display_name = 0;
	config.use_pixman = 0;
	config.sprawl = 0;
	config.fullscreen = 0;
	config.cursor_theme = NULL;


	ret = weston_compositor_load_backend (ec, WESTON_BACKEND_WAYLAND, &config.base);

  server->api = weston_windowed_output_get_api (ec);
  server->new_output.notify = new_output_notify;
  wl_signal_add (&ec->output_created_signal, &server->new_output);

  server->api->create_head (ec, "W1");

  desktop_api.surface_added = surface_added;
  desktop_api.surface_removed = surface_removed;

  desktop = weston_desktop_create (ec, &desktop_api, NULL);

  socket_name = wl_display_add_socket_auto (display);
  if (socket_name)
  {
    weston_log ("Compositor running on %s", socket_name);
    setenv ("WAYLAND_DISPLAY", socket_name, 1);
  }

  weston_compositor_flush_heads_changed (ec);

  weston_compositor_wake (ec);
  wl_display_run (display);



	return 0;
}
