#include <stdio.h>
#include <string.h>
#include <wayland-client.h>
#include <protocol/xfway-shell-client-protocol.h>

struct wl_display *display = NULL;
static struct wl_registry *registry = NULL;

void global_add (void               *data,
                 struct wl_registry *registry,
                 uint32_t            name,
                 const char         *interface,
                 uint32_t            version)
{

  if (strcmp (interface, "xfway_shell") == 0)
    {
      struct xfway_shell *shell = NULL;
      shell = wl_registry_bind (registry, name, &xfway_shell_interface, 1);
    }
}
void global_remove (void               *data,
                    struct wl_registry *registry,
                    uint32_t            name)
{

}

struct wl_registry_listener registry_listener =
{
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

  wl_registry_add_listener (registry, &registry_listener, NULL);

  wl_display_roundtrip (display);
  wl_display_roundtrip (display);

  while (ret != -1)
    ret = wl_display_dispatch (display);

  return 0;
}
