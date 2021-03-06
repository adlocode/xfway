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

#include <stdio.h>
#include <wayland-client.h>
#include <protocol/window-switcher-unstable-v1-client-protocol.h>
#include <protocol/xfway-shell-client-protocol.h>
#include <gtk/gtk.h>

struct wl_display *display = NULL;
static struct wl_registry *registry = NULL;
struct zww_window_switcher_v1 *switcher = NULL;

void switcher_window (void                                 *data,
                      struct zww_window_switcher_v1        *zww_window_switcher_v1,
                      struct zww_window_switcher_window_v1 *window)
{
  fprintf (stderr, "client: window\n");
}

struct zww_window_switcher_v1_listener switcher_listener = {
  .window = switcher_window,
};

void global_add(void *our_data,
        struct wl_registry *registry,
        uint32_t name,
        const char *interface,
        uint32_t version) {

          if (strcmp(interface, "zww_window_switcher_v1") == 0) {
          if (switcher == NULL)
              {
          switcher = wl_registry_bind (registry, name, &zww_window_switcher_v1_interface,
                               1);
            printf ("%s", "\nclient: bind switcher\n");

            zww_window_switcher_v1_add_listener(switcher,
				      &switcher_listener, NULL);
              }
          }
          else if (strcmp(interface, "xfway_shell") == 0) {
          struct xfway_shell *xfshell = NULL;
          xfshell = wl_registry_bind (registry, name, &xfway_shell_interface,
                               1);
            printf ("%s", "\nclient: bind shell\n");
          }

}

void global_remove(void *our_data,
        struct wl_registry *registry,
        uint32_t name) {
    // TODO
}

struct wl_registry_listener registry_listener = {
    .global = global_add,
    .global_remove = global_remove
};


int main (int    argc,
          char **argv)
{
  display = wl_display_connect (NULL);
  int ret = 0;

  if (display == NULL)
    {
      fprintf (stderr, "Can't connect to display");
    }

  registry = wl_display_get_registry (display);

  wl_registry_add_listener(registry, &registry_listener, NULL);

  wl_display_roundtrip (display);
  wl_display_roundtrip (display);

  while (ret != -1)
    ret = wl_display_dispatch (display);

  return 0;
}
