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

#include "server.h"

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

struct ShellSurface
{
  struct wl_signal destroy_signal;
  struct weston_desktop_surface *desktop_surface;
  struct weston_surface *surface;
  struct weston_view *view;
  int grabbed;

  uint32_t resize_edges;

  struct TestServer *server;

  bool maximized;
};

struct ShellGrab
{
  struct weston_pointer_grab grab;
  struct ShellSurface *shsurf;
  struct wl_listener shsurf_destroy_listener;
};

struct ShellMoveGrab
{
  struct ShellGrab base;
  wl_fixed_t dx, dy;
};

struct ShellSurface *
get_shell_surface(struct weston_surface *surface)
{
	if (weston_surface_is_desktop_surface(surface)) {
		struct weston_desktop_surface *desktop_surface =
			weston_surface_get_desktop_surface(surface);
		return weston_desktop_surface_get_user_data(desktop_surface);
	}
	return NULL;
}

void surface_added (struct weston_desktop_surface *desktop_surface,
                    void                   *user_data)
{
  struct TestServer *server = user_data;

  struct ShellSurface *self;

  self = calloc (1, sizeof (struct ShellSurface));

  self->desktop_surface = desktop_surface;
  self->server = server;

  weston_desktop_surface_set_user_data (self->desktop_surface, self);

  self->surface = weston_desktop_surface_get_surface (self->desktop_surface);
  self->view = weston_desktop_surface_create_view (self->desktop_surface);

  weston_layer_entry_insert (&server->surfaces_layer.view_list, &self->view->layer_link);

  weston_surface_damage (self->surface);
  weston_compositor_schedule_repaint (server->compositor);

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
  struct TestServer *server = user_data;

  struct ShellSurface *self = weston_desktop_surface_get_user_data (desktop_surface);

  if (!self)
    return;

  wl_signal_emit (&self->destroy_signal, self);

  weston_desktop_surface_unlink_view (self->view);
  weston_view_destroy (self->view);
  weston_desktop_surface_set_user_data (desktop_surface, NULL);
  free (self);
}

static void
set_maximized_position (struct ShellSurface *shsurf)
{
	pixman_rectangle32_t area;
	struct weston_geometry geometry;

  area.x = shsurf->surface->output->x;
  area.y = shsurf->surface->output->y;

	//get_output_work_area(shell, shsurf->output, &area);
	geometry = weston_desktop_surface_get_geometry(shsurf->desktop_surface);

	weston_view_set_position(shsurf->view,
				 area.x - geometry.x,
				 area.y - geometry.y);
}

static void
map(struct TestServer *shell, struct ShellSurface *shsurf,
    int32_t sx, int32_t sy)
{
  if (shsurf->maximized)
    set_maximized_position (shsurf);
	weston_view_update_transform(shsurf->view);

}

static void
desktop_surface_committed(struct weston_desktop_surface *desktop_surface,
			  int32_t sx, int32_t sy, void *data)
{
	struct ShellSurface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(desktop_surface);
	struct weston_view *view = shsurf->view;
	struct TestServer *shell = data;
	bool was_fullscreen;
	bool was_maximized;

	if (surface->width == 0)
		return;

  shsurf->maximized =
    weston_desktop_surface_get_maximized (desktop_surface);

	if (!weston_surface_is_mapped(surface))
		map(shell, shsurf, sx, sy);

}

static struct weston_layer_entry *
shell_surface_calculate_layer_link (struct ShellSurface *shsurf)
{
	return &shsurf->server->surfaces_layer.view_list;
}

static void click_to_activate_binding (struct weston_pointer *pointer,
                                       const struct timespec *time,
                                       uint32_t               button,
                                       void                  *data)
{
  struct TestServer *server = data;
  struct ShellSurface *shsurf;
  struct weston_seat *s;
  struct weston_surface *main_surface;
  struct weston_layer_entry *new_layer_link;

  main_surface = weston_surface_get_main_surface (pointer->focus->surface);
  shsurf = get_shell_surface (main_surface);

  if (shsurf == NULL)
    return;

  struct weston_surface *surface = weston_desktop_surface_get_surface (shsurf->desktop_surface);

  new_layer_link = shell_surface_calculate_layer_link (shsurf);

  if (new_layer_link == NULL)
    return;
  if (new_layer_link == &shsurf->view->layer_link)
    return;

      weston_view_activate (pointer->focus, pointer->seat,
                            WESTON_ACTIVATE_FLAG_CLICKED |
                            WESTON_ACTIVATE_FLAG_CONFIGURE);
      weston_view_geometry_dirty (shsurf->view);
      weston_layer_entry_remove (&shsurf->view->layer_link);
      weston_layer_entry_insert (new_layer_link, &shsurf->view->layer_link);
      weston_view_geometry_dirty (shsurf->view);
      weston_surface_damage (main_surface);
      weston_desktop_surface_propagate_layer (shsurf->desktop_surface);
}

static void
destroy_shell_grab_shsurf(struct wl_listener *listener, void *data)
{
	struct ShellGrab *grab;

	grab = container_of(listener, struct ShellGrab,
			    shsurf_destroy_listener);

	grab->shsurf = NULL;
}

static void
shell_grab_start (struct ShellGrab                     *grab,
                        const struct weston_pointer_grab_interface *interface,
                        struct ShellSurface                   *shsurf,
                        struct weston_pointer                      *pointer)
{
  weston_seat_break_desktop_grabs (pointer->seat);

  grab->grab.interface = interface;
  grab->shsurf = shsurf;
  grab->shsurf_destroy_listener.notify = destroy_shell_grab_shsurf;
  wl_signal_add (&shsurf->destroy_signal,
                 &grab->shsurf_destroy_listener);
  shsurf->grabbed = 1;

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
	struct ShellSurface *shsurf = move->base.shsurf;
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(shsurf->desktop_surface);
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
	struct ShellSurface *shsurf = move->base.shsurf;
	struct weston_surface *surface;
	int cx, cy;

	weston_pointer_move(pointer, event);
	if (!shsurf)
		return;

	surface = weston_desktop_surface_get_surface(shsurf->desktop_surface);

	constrain_position(move, &cx, &cy);

	weston_view_set_position(shsurf->view, cx, cy);

	weston_compositor_schedule_repaint(surface->compositor);
}

static void
shell_grab_end(struct ShellGrab *grab)
{
  if (grab->shsurf)
  {
		wl_list_remove(&grab->shsurf_destroy_listener.link);
    grab->shsurf->grabbed = 0;
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
  struct TestServer *server = data;
  struct ShellSurface *shsurf = weston_desktop_surface_get_user_data (desktop_surface);
  struct ShellMoveGrab *move;
  int x, y, dx, dy;

  if (!shsurf)
    return;

  if (shsurf->grabbed)
    return;

  move = malloc (sizeof (*move));

  move->dx = wl_fixed_from_double (shsurf->view->geometry.x) - pointer->grab_x;
  move->dy = wl_fixed_from_double (shsurf->view->geometry.y) - pointer->grab_y;

  shell_grab_start (&move->base, &move_grab_interface, shsurf,
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
	struct ShellSurface *shsurf = resize->base.shsurf;
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
		resize->base.shsurf->desktop_surface;

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
		resize->base.shsurf->desktop_surface;

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
	struct ShellSurface *shsurf =
		weston_desktop_surface_get_user_data(desktop_surface);
	struct weston_surface *surface =
		weston_desktop_surface_get_surface(shsurf->desktop_surface);
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

  if (shsurf->grabbed)
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

	geometry = weston_desktop_surface_get_geometry(shsurf->desktop_surface);
	resize->width = geometry.width;
	resize->height = geometry.height;

	shsurf->resize_edges = edges;
	weston_desktop_surface_set_resizing(shsurf->desktop_surface, true);
	shell_grab_start(&resize->base, &resize_grab_interface, shsurf,
			 pointer);
}

static void
set_maximized (struct ShellSurface *shsurf,
               bool                 maximized)
{
  struct weston_surface *surface =
          weston_desktop_surface_get_surface (shsurf->desktop_surface);

  int32_t width = 0, height = 0;

  weston_desktop_surface_set_maximized (shsurf->desktop_surface, maximized);

  if (maximized)
    {
      width = surface->output->width;
      height = surface->output->height;
    }

  weston_desktop_surface_set_size (shsurf->desktop_surface, width, height);
}

static void
desktop_surface_maximized_requested (struct weston_desktop_surface *desktop_surface,
                                     bool                           maximized,
                                     void                          *server)
{
  struct ShellSurface *shsurf =
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

void test_server_shell_init (struct TestServer *server)
{
  struct weston_desktop *desktop;

  desktop = weston_desktop_create (server->compositor, &desktop_api, server);
  weston_layer_init (&server->surfaces_layer, server->compositor);
  weston_layer_set_position (&server->surfaces_layer, WESTON_LAYER_POSITION_NORMAL);

  weston_compositor_add_button_binding (server->compositor, BTN_LEFT, 0,
                                        click_to_activate_binding,
                                        server);
  weston_compositor_add_button_binding (server->compositor, BTN_RIGHT, 0,
                                        click_to_activate_binding,
                                        server);
}
