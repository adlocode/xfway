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
#include <protocol/xfway-shell-server-protocol.h>
#include <protocol/window-switcher-unstable-v1-server-protocol.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include "os-compatibility.h"

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

struct weston_process;
typedef void (*weston_process_cleanup_func_t)(struct weston_process *process,
					    int status);

struct weston_process {
	pid_t pid;
	weston_process_cleanup_func_t cleanup;
	struct wl_list link;
};

struct wl_client *
weston_client_launch(struct weston_compositor *compositor,
		     struct weston_process *proc,
		     const char *path,
		     weston_process_cleanup_func_t cleanup);

struct wl_client *
weston_client_start(struct weston_compositor *compositor, const char *path);

void
weston_watch_process(struct weston_process *process);

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

  bool maximized;
};

typedef struct _CWindowWayland CWindowWayland;

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

struct weston_output *
get_default_output(struct weston_compositor *compositor)
{
	if (wl_list_empty(&compositor->output_list))
		return NULL;

	return container_of(compositor->output_list.next,
			    struct weston_output, link);
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

void surface_added (struct weston_desktop_surface *desktop_surface,
                    void                   *user_data)
{
  DisplayInfo *server = user_data;

  CWindowWayland *self;

  self = calloc (1, sizeof (CWindowWayland));

  self->desktop_surface = desktop_surface;
  self->server = server;

  self->saved_position_valid = false;

  weston_desktop_surface_set_user_data (self->desktop_surface, self);

  self->surface = weston_desktop_surface_get_surface (self->desktop_surface);
  self->view = weston_desktop_surface_create_view (self->desktop_surface);

  weston_layer_entry_insert (&server->surfaces_layer.view_list, &self->view->layer_link);

  weston_surface_damage (self->surface);
  weston_compositor_schedule_repaint (server->compositor);

  _weston_window_switcher_window_create (server->window_switcher, self->surface);

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

  weston_desktop_surface_unlink_view (self->view);
  weston_view_destroy (self->view);
  weston_desktop_surface_set_user_data (desktop_surface, NULL);
  free (self);
}

static void
set_maximized_position (CWindowWayland *cw)
{
	pixman_rectangle32_t area;
	struct weston_geometry geometry;

  area.x = cw->surface->output->x;
  area.y = cw->surface->output->y;

	//get_output_work_area(shell, cw->output, &area);
	geometry = weston_desktop_surface_get_geometry(cw->desktop_surface);

	weston_view_set_position(cw->view,
				 area.x - geometry.x,
				 area.y - geometry.y);
}

static void
map(DisplayInfo *shell, CWindowWayland *cw,
    int32_t sx, int32_t sy)
{
  if (cw->maximized)
    set_maximized_position (cw);
  else
    weston_view_set_initial_position (cw->view, shell);

	weston_view_update_transform(cw->view);
  cw->view->is_mapped = true;

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
		set_maximized_position(cw);

  cw->last_width = surface->width;
	cw->last_height = surface->height;

}

static struct weston_layer_entry *
shell_surface_calculate_layer_link (CWindowWayland *cw)
{
	return &cw->server->surfaces_layer.view_list;
}

static void click_to_activate_binding (struct weston_pointer *pointer,
                                       const struct timespec *time,
                                       uint32_t               button,
                                       void                  *data)
{
  DisplayInfo *server = data;
  CWindowWayland *cw;
  struct weston_seat *s;
  struct weston_surface *main_surface;
  struct weston_layer_entry *new_layer_link;

  main_surface = weston_surface_get_main_surface (pointer->focus->surface);
  cw = get_shell_surface (main_surface);

  if (cw == NULL)
    return;

  struct weston_surface *surface = weston_desktop_surface_get_surface (cw->desktop_surface);

  new_layer_link = shell_surface_calculate_layer_link (cw);

  if (new_layer_link == NULL)
    return;
  if (new_layer_link == &cw->view->layer_link)
    return;

      weston_view_activate (pointer->focus, pointer->seat,
                            WESTON_ACTIVATE_FLAG_CLICKED |
                            WESTON_ACTIVATE_FLAG_CONFIGURE);
      weston_view_geometry_dirty (cw->view);
      weston_layer_entry_remove (&cw->view->layer_link);
      weston_layer_entry_insert (new_layer_link, &cw->view->layer_link);
      weston_view_geometry_dirty (cw->view);
      weston_surface_damage (main_surface);
      weston_desktop_surface_propagate_layer (cw->desktop_surface);
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

static void
set_maximized (CWindowWayland *cw,
               bool                 maximized)
{
  struct weston_surface *surface =
          weston_desktop_surface_get_surface (cw->desktop_surface);

  int32_t width = 0, height = 0;

  weston_desktop_surface_set_maximized (cw->desktop_surface, maximized);

  if (maximized)
    {
      width = surface->output->width;
      height = surface->output->height;
    }

  weston_desktop_surface_set_size (cw->desktop_surface, width, height);
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
bind_desktop_shell(struct wl_client *client,
		   void *data, uint32_t version, uint32_t id)
{
	struct DisplayInfo *shell = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &xfway_shell_interface,
				      1, id);

  weston_log ("\nbind desktop shell\n");

	/*if (client == shell->child.client) {
		wl_resource_set_implementation(resource,
					       &desktop_shell_implementation,
					       shell, unbind_desktop_shell);
		shell->child.desktop_shell = resource;
		return;
	}*/

	//wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
			       //"permission to bind desktop_shell denied");
}

static struct wl_list child_process_list;
static struct weston_compositor *segv_compositor;

static int
sigchld_handler(int signal_number, void *data)
{
	struct weston_process *p;
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		wl_list_for_each(p, &child_process_list, link) {
			if (p->pid == pid)
				break;
		}

		if (&p->link == &child_process_list) {
			weston_log("unknown child process exited\n");
			continue;
		}

		wl_list_remove(&p->link);
		p->cleanup(p, status);
	}

	if (pid < 0 && errno != ECHILD)
		weston_log("waitpid error %m\n");

	return 1;
}

static void
child_client_exec(int sockfd, const char *path)
{
	int clientfd;
	char s[32];
	sigset_t allsigs;

	/* do not give our signal mask to the new process */
	sigfillset(&allsigs);
	sigprocmask(SIG_UNBLOCK, &allsigs, NULL);

	/* Launch clients as the user. Do not lauch clients with wrong euid.*/
	if (seteuid(getuid()) == -1) {
		weston_log("compositor: failed seteuid\n");
		return;
	}

	/* SOCK_CLOEXEC closes both ends, so we dup the fd to get a
	 * non-CLOEXEC fd to pass through exec. */
	clientfd = dup(sockfd);
	if (clientfd == -1) {
		weston_log("compositor: dup failed: %m\n");
		return;
	}

	snprintf(s, sizeof s, "%d", clientfd);
	setenv("WAYLAND_SOCKET", s, 1);

	if (execl(path, path, NULL) < 0)
		weston_log("compositor: executing '%s' failed: %m\n",
			path);
}

WL_EXPORT struct wl_client *
weston_client_launch(struct weston_compositor *compositor,
		     struct weston_process *proc,
		     const char *path,
		     weston_process_cleanup_func_t cleanup)
{
	int sv[2];
	pid_t pid;
	struct wl_client *client;

	weston_log("launching '%s'\n", path);

	if (os_socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		weston_log("weston_client_launch: "
			"socketpair failed while launching '%s': %m\n",
			path);
		return NULL;
	}

	pid = fork();
	if (pid == -1) {
		close(sv[0]);
		close(sv[1]);
		weston_log("weston_client_launch: "
			"fork failed while launching '%s': %m\n", path);
		return NULL;
	}

	if (pid == 0) {
		child_client_exec(sv[1], path);
		_exit(-1);
	}

	close(sv[1]);

	client = wl_client_create(compositor->wl_display, sv[0]);
	if (!client) {
		close(sv[0]);
		weston_log("weston_client_launch: "
			"wl_client_create failed while launching '%s'.\n",
			path);
		return NULL;
	}

	proc->pid = pid;
	proc->cleanup = cleanup;
	weston_watch_process(proc);

	return client;
}

WL_EXPORT void
weston_watch_process(struct weston_process *process)
{
	wl_list_insert(&child_process_list, &process->link);
}

struct process_info {
	struct weston_process proc;
	char *path;
};

static void
process_handle_sigchld(struct weston_process *process, int status)
{
	struct process_info *pinfo =
		container_of(process, struct process_info, proc);

	/*
	 * There are no guarantees whether this runs before or after
	 * the wl_client destructor.
	 */

	if (WIFEXITED(status)) {
		weston_log("%s exited with status %d\n", pinfo->path,
			   WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		weston_log("%s died on signal %d\n", pinfo->path,
			   WTERMSIG(status));
	} else {
		weston_log("%s disappeared\n", pinfo->path);
	}

	free(pinfo->path);
	free(pinfo);
}

WL_EXPORT struct wl_client *
weston_client_start(struct weston_compositor *compositor, const char *path)
{
	struct process_info *pinfo;
	struct wl_client *client;

	pinfo = zalloc(sizeof *pinfo);
	if (!pinfo)
		return NULL;

	pinfo->path = strdup(path);
	if (!pinfo->path)
		goto out_free;

	client = weston_client_launch(compositor, &pinfo->proc, path,
				      process_handle_sigchld);
	if (!client)
		goto out_str;

	return client;

out_str:
	free(pinfo->path);

out_free:
	free(pinfo);

	return NULL;
}

static void
launch_desktop_shell_process(void *data)
{
	DisplayInfo *server = data;
  struct wl_client *client;

  client = weston_client_start (server->compositor, "src/xfway-shell");

	/*if (!shell->child.client) {
		weston_log("not able to start %s\n", shell->client);
		return;
	}

	shell->child.client_destroy_listener.notify =
		desktop_shell_client_destroy;
	wl_client_add_destroy_listener(shell->child.client,
				       &shell->child.client_destroy_listener);*/
}

void xfway_server_shell_init (DisplayInfo *server, int argc, char *argv[])
{
  struct weston_desktop *desktop;
  int ret;
  struct weston_client *client;
  struct wl_event_loop *loop;

  desktop = weston_desktop_create (server->compositor, &desktop_api, server);

  ret = weston_window_switcher_module_init (server->compositor, &server->window_switcher, argc, argv);

  wl_global_create (server->compositor->wl_display,
                    &xfway_shell_interface, 1,
                    server, bind_desktop_shell);

  /*This loads the client successfully but then segfaults
   * loop = wl_display_get_event_loop(server->compositor->wl_display);
	wl_event_loop_add_idle(loop, launch_desktop_shell_process, server);*/

  weston_layer_init (&server->surfaces_layer, server->compositor);
  weston_layer_set_position (&server->surfaces_layer, WESTON_LAYER_POSITION_NORMAL);

  weston_compositor_add_button_binding (server->compositor, BTN_LEFT, 0,
                                        click_to_activate_binding,
                                        server);
  weston_compositor_add_button_binding (server->compositor, BTN_RIGHT, 0,
                                        click_to_activate_binding,
                                        server);
}
