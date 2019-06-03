/* Copyright (C) 2018 - 2019 adlo
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

#include "server.h"
#include "xfway.h"
#include <protocol/xfway-shell-server-protocol.h>
#include <protocol/window-switcher-unstable-v1-server-protocol.h>
#include <protocol/wlr-foreign-toplevel-management-unstable-v1-protocol.h>
#include "wlr_foreign_toplevel_management_v1.h"

/**
 * Returns the smaller of two values.
 *
 * @param x the first item to compare.
 * @param y the second item to compare.
 * @return the value that evaluates to lesser than the other.
 */
#ifndef MIN
#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#endif

/**
 * Returns the bigger of two values.
 *
 * @param x the first item to compare.
 * @param y the second item to compare.
 * @return the value that evaluates to more than the other.
 */
#ifndef MAX
#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({				\
	const __typeof__( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

struct _CWindowWayland
{
  struct wl_signal destroy_signal;
  struct weston_desktop_surface *desktop_surface;
  struct weston_surface *surface;
  struct weston_view *view;
  int32_t last_width, last_height;
  int32_t saved_x, saved_y;
  bool saved_position_valid;
  int grabbed;

  uint32_t resize_edges;

  DisplayInfo *server;

  struct weston_output *output;
  struct wl_listener output_destroy_listener;

  struct wlr_foreign_toplevel_handle_v1 *toplevel_handle;
  struct wl_listener toplevel_handle_request_activate;

  struct wl_listener desktop_surface_metadata_signal;

  bool maximized;
};

typedef struct _CWindowWayland CWindowWayland;

struct _Shell
{
  DisplayInfo *display_info;

  struct {
		struct wl_client *client;
		struct wl_resource *desktop_shell;
		struct wl_listener client_destroy_listener;

		unsigned deathcount;
		struct timespec deathstamp;
	} child;
};

typedef struct _Shell Shell;

struct ShellGrab
{
  struct weston_pointer_grab grab;
  CWindowWayland *cw;
  struct wl_listener shsurf_destroy_listener;
};

struct ShellMoveGrab
{
  struct ShellGrab base;
  wl_fixed_t dx, dy;
};

WL_EXPORT int
weston_window_switcher_module_init (struct weston_compositor *compositor,
                                    struct weston_window_switcher **out_switcher,
                                    int argc, char *argv[]);

void
_weston_window_switcher_window_create (struct weston_window_switcher *switcher,
                                       struct weston_surface         *surface);

void
activate (DisplayInfo        *display_info,
          struct weston_view *view,
          struct weston_seat *seat,
          uint32_t            flags);

struct weston_output *
get_default_output(struct weston_compositor *compositor)
{
	if (wl_list_empty(&compositor->output_list))
		return NULL;

	return container_of(compositor->output_list.next,
			    struct weston_output, link);
}

static void
notify_output_destroy(struct wl_listener *listener, void *data)
{
	CWindowWayland *cw =
		container_of(listener,
			     CWindowWayland, output_destroy_listener);

	cw->output = NULL;
	cw->output_destroy_listener.notify = NULL;
}

static void
shell_surface_set_output(CWindowWayland *cw,
                         struct weston_output *output)
{
	struct weston_surface *es =
		weston_desktop_surface_get_surface(cw->desktop_surface);

	/* get the default output, if the client set it as NULL
	   check whether the output is available */
	if (output)
		cw->output = output;
	else if (es->output)
		cw->output = es->output;
	else
		cw->output = get_default_output(es->compositor);

	if (cw->output_destroy_listener.notify) {
		wl_list_remove(&cw->output_destroy_listener.link);
		cw->output_destroy_listener.notify = NULL;
	}

	if (!cw->output)
		return;

	cw->output_destroy_listener.notify = notify_output_destroy;
	wl_signal_add(&cw->output->destroy_signal,
		      &cw->output_destroy_listener);
}

static void
weston_view_set_initial_position(struct weston_view *view,
				 DisplayInfo *shell)
{
	struct weston_compositor *compositor = shell->compositor;
	int ix = 0, iy = 0;
	int32_t range_x, range_y;
	int32_t x, y;
	struct weston_output *output, *target_output = NULL;
	struct weston_seat *seat;
	pixman_rectangle32_t area;

	/* As a heuristic place the new window on the same output as the
	 * pointer. Falling back to the output containing 0, 0.
	 *
	 * TODO: Do something clever for touch too?
	 */
	wl_list_for_each(seat, &compositor->seat_list, link) {
		struct weston_pointer *pointer = weston_seat_get_pointer(seat);

		if (pointer) {
			ix = wl_fixed_to_int(pointer->x);
			iy = wl_fixed_to_int(pointer->y);
			break;
		}
	}

	wl_list_for_each(output, &compositor->output_list, link) {
		if (pixman_region32_contains_point(&output->region, ix, iy, NULL)) {
			target_output = output;
			break;
		}
	}

	if (!target_output) {
		weston_view_set_position(view, 10 + random() % 400,
					 10 + random() % 400);
		return;
	}

	/* Valid range within output where the surface will still be onscreen.
	 * If this is negative it means that the surface is bigger than
	 * output.
	 */

	x = target_output->x;
	y = target_output->y;
	range_x = target_output->width - view->surface->width;
	range_y = target_output->height - view->surface->height;

	if (range_x > 0)
		x += random() % range_x;

	if (range_y > 0)
		y += random() % range_y;

	weston_view_set_position(view, x, y);
}

static void
unset_maximized(CWindowWayland *cw)
{
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(cw->desktop_surface);

	/* undo all maximized things here */
	cw->output = get_default_output(surface->compositor);

	if (cw->saved_position_valid)
		weston_view_set_position(cw->view,
					 cw->saved_x, cw->saved_y);
	else
		weston_view_set_initial_position(cw->view, cw->server);
	cw->saved_position_valid = false;
}

CWindowWayland *
get_shell_surface(struct weston_surface *surface)
{
	if (weston_surface_is_desktop_surface(surface)) {
		struct weston_desktop_surface *desktop_surface =
			weston_surface_get_desktop_surface(surface);
		return weston_desktop_surface_get_user_data(desktop_surface);
	}
	return NULL;
}

static void handle_toplevel_handle_request_activate (struct wl_listener *listener,
                                                     void               *data)
{
  CWindowWayland *cw = wl_container_of (listener, cw, toplevel_handle_request_activate);

  struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;

  struct weston_seat *s;
  wl_list_for_each (s, &cw->server->compositor->seat_list, link)
    {
      activate (NULL, cw->view, s, 0);
    }

}

static void handle_desktop_surface_metadata_signal (struct wl_listener *listener,
                                                    void               *data)
{
  const char *title, *app_id;

  CWindowWayland *cw = wl_container_of (listener, cw, desktop_surface_metadata_signal);

  title = weston_desktop_surface_get_title (cw->desktop_surface);
  if (title)
    wlr_foreign_toplevel_handle_v1_set_title (cw->toplevel_handle, title);

  app_id = weston_desktop_surface_get_app_id (cw->desktop_surface);
  if (app_id)
    wlr_foreign_toplevel_handle_v1_set_app_id (cw->toplevel_handle, app_id);
}

void surface_added (struct weston_desktop_surface *desktop_surface,
                    void                   *user_data)
{
  DisplayInfo *server = user_data;

  CWindowWayland *self;

  self = calloc (1, sizeof (CWindowWayland));

  self->desktop_surface = desktop_surface;
  self->server = server;

  self->saved_position_valid = false;

  shell_surface_set_output (self, get_default_output (self->server->compositor));

  weston_desktop_surface_set_user_data (self->desktop_surface, self);

  self->surface = weston_desktop_surface_get_surface (self->desktop_surface);
  self->view = weston_desktop_surface_create_view (self->desktop_surface);

  weston_layer_entry_insert (&server->surfaces_layer.view_list, &self->view->layer_link);

  weston_surface_damage (self->surface);
  weston_compositor_schedule_repaint (server->compositor);

  self->toplevel_handle = wlr_foreign_toplevel_handle_v1_create (server->manager);

  self->toplevel_handle_request_activate.notify =
    handle_toplevel_handle_request_activate;
  wl_signal_add (&self->toplevel_handle->events.request_activate,
                 &self->toplevel_handle_request_activate);

  self->desktop_surface_metadata_signal.notify = handle_desktop_surface_metadata_signal;
  weston_desktop_surface_add_metadata_listener (desktop_surface,
                                                &self->desktop_surface_metadata_signal);



  weston_desktop_surface_set_activated (desktop_surface, true);

  struct weston_seat *s;
  wl_list_for_each (s, &server->compositor->seat_list, link)
    {
      weston_view_activate (self->view, s,
                            WESTON_ACTIVATE_FLAG_CLICKED |
                            WESTON_ACTIVATE_FLAG_CONFIGURE);

    }

  wl_signal_init (&self->destroy_signal);
}

void surface_removed (struct weston_desktop_surface *desktop_surface,
                      void                   *user_data)
{
  DisplayInfo *server = user_data;

  CWindowWayland *self = weston_desktop_surface_get_user_data (desktop_surface);

  if (!self)
    return;

  wl_signal_emit (&self->destroy_signal, self);

  if (self->toplevel_handle)
    {
      wlr_foreign_toplevel_handle_v1_destroy (self->toplevel_handle);
      self->toplevel_handle = NULL;
    }

  weston_desktop_surface_unlink_view (self->view);
  weston_view_destroy (self->view);
  weston_desktop_surface_set_user_data (desktop_surface, NULL);

  if (self->output_destroy_listener.notify)
    {
      wl_list_remove (&self->output_destroy_listener.link);
      self->output_destroy_listener.notify = NULL;
    }
  free (self);
}

static void
get_output_work_area(DisplayInfo *shell,
		     struct weston_output *output,
		     pixman_rectangle32_t *area)
{
	int32_t panel_width = 0, panel_height = 0;

	if (!output) {
		area->x = 0;
		area->y = 0;
		area->width = 0;
		area->height = 0;

		return;
	}

	area->x = output->x;
	area->y = output->y;

  area->width = output->width;
	area->height = output->height;
}

static void
set_maximized_position (CWindowWayland *cw)
{
	pixman_rectangle32_t area;
	struct weston_geometry geometry;

  area.x = cw->surface->output->x;
  area.y = cw->surface->output->y;

	get_output_work_area(NULL, cw->output, &area);
	geometry = weston_desktop_surface_get_geometry(cw->desktop_surface);

	weston_view_set_position(cw->view,
				 area.x - geometry.x,
				 area.y - geometry.y);
}

static void
map(DisplayInfo *shell, CWindowWayland *cw,
    int32_t sx, int32_t sy)
{
  const char *title, *app_id;
  struct weston_surface *surface = weston_desktop_surface_get_surface (cw->desktop_surface);
  if (cw->maximized)
    set_maximized_position (cw);
  else
    weston_view_set_initial_position (cw->view, shell);

	weston_view_update_transform(cw->view);
  cw->view->is_mapped = true;

  if (cw->maximized)
    {
      surface->output = cw->output;
      weston_view_set_output (cw->view, cw->output);
    }

  title = weston_desktop_surface_get_title (cw->desktop_surface);
  app_id = weston_desktop_surface_get_app_id (cw->desktop_surface);
  if (title)
    wlr_foreign_toplevel_handle_v1_set_title (cw->toplevel_handle, title);
  if (app_id)
    wlr_foreign_toplevel_handle_v1_set_app_id (cw->toplevel_handle, app_id);

}

static void
desktop_surface_committed(struct weston_desktop_surface *desktop_surface,
			  int32_t sx, int32_t sy, void *data)
{
	CWindowWayland *cw =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = cw->view;
	DisplayInfo *shell = data;
	bool was_fullscreen;
	bool was_maximized;

	if (surface->width == 0)
		return;

  was_maximized = cw->maximized;

  cw->maximized =
    weston_desktop_surface_get_maximized (desktop_surface);

	if (!weston_surface_is_mapped(surface))
    {
      map(shell, cw, sx, sy);
      surface->is_mapped = true;
    }

  if (sx == 0 && sy == 0 &&
	    cw->last_width == surface->width &&
	    cw->last_height == surface->height &&
	    was_maximized == cw->maximized)
	    return;

	if (was_maximized)
		unset_maximized(cw);

	if (cw->maximized &&
	    !cw->saved_position_valid) {
		cw->saved_x = cw->view->geometry.x;
		cw->saved_y = cw->view->geometry.y;
		cw->saved_position_valid = true;
	}

  if (cw->maximized)
    {
		  set_maximized_position(cw);
      surface->output = cw->output;
    }

  cw->last_width = surface->width;
	cw->last_height = surface->height;

}

static struct weston_layer_entry *
shell_surface_calculate_layer_link (CWindowWayland *cw)
{
	return &cw->server->surfaces_layer.view_list;
}

void
activate (DisplayInfo        *display_info,
          struct weston_view *view,
          struct weston_seat *seat,
          uint32_t            flags)
{
  struct weston_surface *main_surface;
  CWindowWayland *cw;
  struct weston_layer_entry *new_layer_link;

  main_surface = weston_surface_get_main_surface (view->surface);
  cw = get_shell_surface (main_surface);
  if (cw == NULL)
    return;

  new_layer_link = shell_surface_calculate_layer_link (cw);

  if (new_layer_link == NULL)
    return;
  if (new_layer_link == &cw->view->layer_link)
    return;

      weston_view_activate (view, seat,
                            WESTON_ACTIVATE_FLAG_CLICKED |
                            WESTON_ACTIVATE_FLAG_CONFIGURE);
      weston_view_geometry_dirty (cw->view);
      weston_layer_entry_remove (&cw->view->layer_link);
      weston_layer_entry_insert (new_layer_link, &cw->view->layer_link);
      weston_view_geometry_dirty (cw->view);
      weston_surface_damage (main_surface);
      weston_desktop_surface_propagate_layer (cw->desktop_surface);
}
static void click_to_activate_binding (struct weston_pointer *pointer,
                                       const struct timespec *time,
                                       uint32_t               button,
                                       void                  *data)
{
  DisplayInfo *server = data;

  struct weston_surface *main_surface;


  main_surface = weston_surface_get_main_surface (pointer->focus->surface);
  if (!get_shell_surface (main_surface))
    return;

  activate (server, pointer->focus, pointer->seat, 0);

}

static void
destroy_shell_grab_shsurf(struct wl_listener *listener, void *data)
{
	struct ShellGrab *grab;

	grab = container_of(listener, struct ShellGrab,
			    shsurf_destroy_listener);

	grab->cw = NULL;
}

static void
shell_grab_start (struct ShellGrab                     *grab,
                        const struct weston_pointer_grab_interface *interface,
                        CWindowWayland                   *cw,
                        struct weston_pointer                      *pointer)
{
  weston_seat_break_desktop_grabs (pointer->seat);

  grab->grab.interface = interface;
  grab->cw = cw;
  grab->shsurf_destroy_listener.notify = destroy_shell_grab_shsurf;
  wl_signal_add (&cw->destroy_signal,
                 &grab->shsurf_destroy_listener);
  cw->grabbed = 1;

  weston_pointer_start_grab (pointer, &grab->grab);
}

static void
noop_grab_focus(struct weston_pointer_grab *grab)
{
}

static void
noop_grab_axis(struct weston_pointer_grab *grab,
	       const struct timespec *time,
	       struct weston_pointer_axis_event *event)
{
}

static void
noop_grab_axis_source(struct weston_pointer_grab *grab,
		      uint32_t source)
{
}

static void
noop_grab_frame(struct weston_pointer_grab *grab)
{
}

static void
constrain_position(struct ShellMoveGrab *move, int *cx, int *cy)
{
	CWindowWayland *cw = move->base.cw;
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(cw->desktop_surface);
	struct weston_pointer *pointer = move->base.grab.pointer;
	int x, y, bottom;
	const int safety = 50;
	pixman_rectangle32_t area;
	struct weston_geometry geometry;

	x = wl_fixed_to_int(pointer->x + move->dx);
	y = wl_fixed_to_int(pointer->y + move->dy);

	*cx = x;
	*cy = y;
}

static void
move_grab_motion(struct weston_pointer_grab *grab,
		 const struct timespec *time,
		 struct weston_pointer_motion_event *event)
{
	struct ShellMoveGrab *move = (struct ShellMoveGrab *) grab;
	struct weston_pointer *pointer = grab->pointer;
	CWindowWayland *cw = move->base.cw;
	struct weston_surface *surface;
	int cx, cy;

	weston_pointer_move(pointer, event);
	if (!cw)
		return;

	surface = weston_desktop_surface_get_surface(cw->desktop_surface);

	constrain_position(move, &cx, &cy);

	weston_view_set_position(cw->view, cx, cy);

	weston_compositor_schedule_repaint(surface->compositor);
}

static void
shell_grab_end(struct ShellGrab *grab)
{
  if (grab->cw)
  {
		wl_list_remove(&grab->shsurf_destroy_listener.link);
    grab->cw->grabbed = 0;
	}
	weston_pointer_end_grab(grab->grab.pointer);
}

static void
move_grab_button(struct weston_pointer_grab *grab,
		 const struct timespec *time, uint32_t button, uint32_t state_w)
{
	struct ShellGrab *shell_grab = container_of(grab, struct ShellGrab,
						    grab);
	struct weston_pointer *pointer = grab->pointer;
	enum wl_pointer_button_state state = state_w;

	if (pointer->button_count == 0 &&
	    state == WL_POINTER_BUTTON_STATE_RELEASED) {
		shell_grab_end(shell_grab);
		free(grab);
	}
}

static void
move_grab_cancel(struct weston_pointer_grab *grab)
{
  struct ShellGrab *shell_grab =
        container_of(grab, struct ShellGrab, grab);

	shell_grab_end(shell_grab);
	free(grab);
}

static const struct weston_pointer_grab_interface move_grab_interface = {
	noop_grab_focus,
	move_grab_motion,
	move_grab_button,
	noop_grab_axis,
	noop_grab_axis_source,
	noop_grab_frame,
	move_grab_cancel,
};

static void
desktop_surface_move (struct weston_desktop_surface *desktop_surface,
                      struct weston_seat            *seat,
                      uint32_t                      *serial,
                      void                          *data)
{
  struct weston_pointer *pointer = weston_seat_get_pointer (seat);
  DisplayInfo *server = data;
  CWindowWayland *cw = weston_desktop_surface_get_user_data (desktop_surface);
  struct ShellMoveGrab *move;
  int x, y, dx, dy;

  if (!cw)
    return;

  if (cw->grabbed)
    return;

  move = malloc (sizeof (*move));

  move->dx = wl_fixed_from_double (cw->view->geometry.x) - pointer->grab_x;
  move->dy = wl_fixed_from_double (cw->view->geometry.y) - pointer->grab_y;

  shell_grab_start (&move->base, &move_grab_interface, cw,
                          pointer);
}

struct ShellResizeGrab {
	struct ShellGrab base;
	uint32_t edges;
	int32_t width, height;
};

static void
resize_grab_motion(struct weston_pointer_grab *grab,
		   const struct timespec *time,
		   struct weston_pointer_motion_event *event)
{
	struct ShellResizeGrab *resize = (struct ShellResizeGrab *) grab;
	struct weston_pointer *pointer = grab->pointer;
	CWindowWayland *shsurf = resize->base.cw;
	int32_t width, height;
	struct weston_size min_size, max_size;
	wl_fixed_t from_x, from_y;
	wl_fixed_t to_x, to_y;

	weston_pointer_move(pointer, event);

	if (!shsurf)
		return;

	weston_view_from_global_fixed(shsurf->view,
				      pointer->grab_x, pointer->grab_y,
				      &from_x, &from_y);
	weston_view_from_global_fixed(shsurf->view,
				      pointer->x, pointer->y, &to_x, &to_y);

	width = resize->width;
	if (resize->edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
		width += wl_fixed_to_int(from_x - to_x);
	} else if (resize->edges & WL_SHELL_SURFACE_RESIZE_RIGHT) {
		width += wl_fixed_to_int(to_x - from_x);
	}

	height = resize->height;
	if (resize->edges & WL_SHELL_SURFACE_RESIZE_TOP) {
		height += wl_fixed_to_int(from_y - to_y);
	} else if (resize->edges & WL_SHELL_SURFACE_RESIZE_BOTTOM) {
		height += wl_fixed_to_int(to_y - from_y);
	}

	max_size = weston_desktop_surface_get_max_size(shsurf->desktop_surface);
	min_size = weston_desktop_surface_get_min_size(shsurf->desktop_surface);

	min_size.width = MAX(1, min_size.width);
	min_size.height = MAX(1, min_size.height);

	if (width < min_size.width)
		width = min_size.width;
	else if (max_size.width > 0 && width > max_size.width)
		width = max_size.width;
	if (height < min_size.height)
		height = min_size.height;
	else if (max_size.width > 0 && width > max_size.width)
		width = max_size.width;
	weston_desktop_surface_set_size(shsurf->desktop_surface, width, height);
}

static void
resize_grab_button(struct weston_pointer_grab *grab,
		   const struct timespec *time,
		   uint32_t button, uint32_t state_w)
{
	struct ShellResizeGrab *resize = (struct ShellResizeGrab *) grab;
	struct weston_pointer *pointer = grab->pointer;
	enum wl_pointer_button_state state = state_w;
	struct weston_desktop_surface *desktop_surface =
		resize->base.cw->desktop_surface;

	if (pointer->button_count == 0 &&
	    state == WL_POINTER_BUTTON_STATE_RELEASED) {
		weston_desktop_surface_set_resizing(desktop_surface, false);
		shell_grab_end(&resize->base);
		free(grab);
	}
}

static void
resize_grab_cancel(struct weston_pointer_grab *grab)
{
	struct ShellResizeGrab *resize = (struct ShellResizeGrab *) grab;
	struct weston_desktop_surface *desktop_surface =
		resize->base.cw->desktop_surface;

	weston_desktop_surface_set_resizing(desktop_surface, false);
	shell_grab_end(&resize->base);
	free(grab);
}

static const struct weston_pointer_grab_interface resize_grab_interface = {
	noop_grab_focus,
	resize_grab_motion,
	resize_grab_button,
	noop_grab_axis,
	noop_grab_axis_source,
	noop_grab_frame,
	resize_grab_cancel,
};



static void
desktop_surface_resize (struct weston_desktop_surface    *desktop_surface,
                        struct weston_seat               *seat,
                        uint32_t                          serial,
                        enum weston_desktop_surface_edge  edges,
                        void                             *server)
{
  struct weston_pointer *pointer = weston_seat_get_pointer(seat);
	CWindowWayland *cw =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(cw->desktop_surface);
	struct wl_resource *resource = surface->resource;
	struct weston_surface *focus;

	if (!pointer ||
	    pointer->button_count == 0 ||
	    pointer->grab_serial != serial ||
	    pointer->focus == NULL)
		return;

	focus = weston_surface_get_main_surface(pointer->focus->surface);
	if (focus != surface)
		return;

  struct ShellResizeGrab *resize;
	const unsigned resize_topbottom =
		WL_SHELL_SURFACE_RESIZE_TOP | WL_SHELL_SURFACE_RESIZE_BOTTOM;
	const unsigned resize_leftright =
		WL_SHELL_SURFACE_RESIZE_LEFT | WL_SHELL_SURFACE_RESIZE_RIGHT;
	const unsigned resize_any = resize_topbottom | resize_leftright;
	struct weston_geometry geometry;

  if (cw->grabbed)
    return;

	/* Check for invalid edge combinations. */
	if (edges == WL_SHELL_SURFACE_RESIZE_NONE || edges > resize_any ||
	    (edges & resize_topbottom) == resize_topbottom ||
	    (edges & resize_leftright) == resize_leftright)
		return;

	resize = malloc(sizeof *resize);
	if (!resize)
		return;

	resize->edges = edges;

	geometry = weston_desktop_surface_get_geometry(cw->desktop_surface);
	resize->width = geometry.width;
	resize->height = geometry.height;

	cw->resize_edges = edges;
	weston_desktop_surface_set_resizing(cw->desktop_surface, true);
	shell_grab_start(&resize->base, &resize_grab_interface, cw,
			 pointer);
}

static struct weston_output *
get_focused_output(struct weston_compositor *compositor)
{
	struct weston_seat *seat;
	struct weston_output *output = NULL;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		struct weston_touch *touch = weston_seat_get_touch(seat);
		struct weston_pointer *pointer = weston_seat_get_pointer(seat);
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);

		/* Priority has touch focus, then pointer and
		 * then keyboard focus. We should probably have
		 * three for loops and check frist for touch,
		 * then for pointer, etc. but unless somebody has some
		 * objections, I think this is sufficient. */
		if (touch && touch->focus)
			output = touch->focus->output;
		else if (pointer && pointer->focus)
			output = pointer->focus->output;
		else if (keyboard && keyboard->focus)
			output = keyboard->focus->output;

		if (output)
			break;
	}

	return output;
}

static void
get_maximized_size(CWindowWayland *cw, int32_t *width, int32_t *height)
{
	DisplayInfo *shell = NULL;
	pixman_rectangle32_t area;

	//shell = shell_surface_get_shell(cw);
	get_output_work_area(shell, cw->output, &area);

	*width = area.width;
	*height = area.height;
}

static void
set_maximized (CWindowWayland *cw,
               bool                 maximized)
{
  struct weston_surface *surface =
          weston_desktop_surface_get_surface (cw->desktop_surface);

  int32_t width = 0, height = 0;

  if (maximized) {
		struct weston_output *output;

		if (!weston_surface_is_mapped(surface))
			output = get_focused_output(surface->compositor);
		else
			output = surface->output;

		shell_surface_set_output(cw, output);

		get_maximized_size(cw, &width, &height);
	}
	weston_desktop_surface_set_maximized(cw->desktop_surface, maximized);
	weston_desktop_surface_set_size(cw->desktop_surface, width, height);
}

static void
desktop_surface_maximized_requested (struct weston_desktop_surface *desktop_surface,
                                     bool                           maximized,
                                     void                          *server)
{
  CWindowWayland *shsurf =
          weston_desktop_surface_get_user_data (desktop_surface);

  set_maximized (shsurf, maximized);

}

static const struct weston_desktop_api desktop_api =
{
  .struct_size = sizeof (struct weston_desktop_api),

  .surface_added = surface_added,
  .surface_removed = surface_removed,
  .committed = desktop_surface_committed,
  .move = desktop_surface_move,
  .resize = desktop_surface_resize,
  .maximized_requested = desktop_surface_maximized_requested,

};

static const struct xfway_shell_interface xfway_desktop_shell_implementation =
{
  NULL
};

static void
unbind_desktop_shell(struct wl_resource *resource)
{
	Shell *shell = wl_resource_get_user_data(resource);

	shell->child.desktop_shell = NULL;
}

static void
bind_desktop_shell(struct wl_client *client,
		   void *data, uint32_t version, uint32_t id)
{
	Shell *shell = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &xfway_shell_interface,
				      1, id);

  weston_log ("\nbind desktop shell\n");

	if (client == shell->child.client) {
		wl_resource_set_implementation(resource,
					       &xfway_desktop_shell_implementation,
					       shell, unbind_desktop_shell);
		shell->child.desktop_shell = resource;
		return;
	}

	wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
			       "permission to bind desktop_shell denied");
}



static void
desktop_shell_client_destroy(struct wl_listener *listener, void *data)
{
	Shell *shell;

	shell = container_of(listener, Shell,
			     child.client_destroy_listener);

	wl_list_remove(&shell->child.client_destroy_listener.link);
	shell->child.client = NULL;
	/*
	 * unbind_desktop_shell() will reset shell->child.desktop_shell
	 * before the respawned process has a chance to create a new
	 * desktop_shell object, because we are being called from the
	 * wl_client destructor which destroys all wl_resources before
	 * returning.
	 */

	//if (!check_desktop_shell_crash_too_early(shell))
		//respawn_desktop_shell_process(shell);

	//shell_fade_startup(shell);
}

static void
launch_desktop_shell_process(void *data)
{
	Shell *shell = data;
  struct wl_client *client;
  DisplayInfo *display_info = shell->display_info;

  shell->child.client = weston_client_start (display_info->compositor, "src/xfway-shell");

	if (!shell->child.client) {
		weston_log("not able to start client");
		return;
	}

	shell->child.client_destroy_listener.notify =
		desktop_shell_client_destroy;
	wl_client_add_destroy_listener(shell->child.client,
				       &shell->child.client_destroy_listener);
}

void xfway_server_shell_init (DisplayInfo *server, int argc, char *argv[])
{
  Shell *shell;
  struct weston_desktop *desktop;
  int ret;
  struct weston_client *client;
  struct wl_event_loop *loop;

  shell = zalloc (sizeof (Shell));
  shell->display_info = server;

  desktop = weston_desktop_create (server->compositor, &desktop_api, server);

  ret = weston_window_switcher_module_init (server->compositor, &server->window_switcher, argc, argv);

  server->manager = wlr_foreign_toplevel_manager_v1_create (server->compositor->wl_display);

  wl_global_create (server->compositor->wl_display,
                    &xfway_shell_interface, 1,
                    shell, bind_desktop_shell);

  loop = wl_display_get_event_loop(server->compositor->wl_display);
	wl_event_loop_add_idle(loop, launch_desktop_shell_process, shell);

  weston_layer_init (&server->surfaces_layer, server->compositor);
  weston_layer_set_position (&server->surfaces_layer, WESTON_LAYER_POSITION_NORMAL);

  weston_compositor_add_button_binding (server->compositor, BTN_LEFT, 0,
                                        click_to_activate_binding,
                                        server);
  weston_compositor_add_button_binding (server->compositor, BTN_RIGHT, 0,
                                        click_to_activate_binding,
                                        server);
}
