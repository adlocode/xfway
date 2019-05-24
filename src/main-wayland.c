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
#include <linux/input.h>
#include <pthread.h>
#include "server.h"
#include "shell.h"
#include "xfway.h"
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include "os-compatibility.h"

#ifndef container_of
#define container_of(ptr, type, member) ({				\
	const __typeof__( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

struct _XfwayHeadTracker {
	struct wl_listener head_destroy_listener;
};

typedef struct _XfwayHeadTracker XfwayHeadTracker;

typedef struct
{
  struct weston_output *output;
  struct weston_surface *background;
  struct wl_list link;
} Output;

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

static int new_output_notify_drm (struct weston_output *output)
{
  DisplayInfo *server = weston_compositor_get_user_data (output->compositor);

  server->api.drm->set_mode (output, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, NULL);
  server->api.drm->set_gbm_format (output, NULL);
  server->api.drm->set_seat (output, NULL);
  weston_output_set_scale (output, 1);
  weston_output_set_transform (output, WL_OUTPUT_TRANSFORM_NORMAL);

  Output *c_output;
  c_output = malloc (sizeof(Output));
  c_output->output = NULL;
  c_output->background = NULL;

  c_output->output = output;

  wl_list_insert (&server->outputs, &c_output->link);

  return 0;
}

static int
count_remaining_heads(struct weston_output *output, struct weston_head *to_go)
{
	struct weston_head *iter = NULL;
	int n = 0;

	while ((iter = weston_output_iterate_heads(output, iter))) {
		if (iter != to_go)
			n++;
	}

	return n;
}

static void
xfway_head_tracker_destroy(XfwayHeadTracker *track)
{
	wl_list_remove(&track->head_destroy_listener.link);
	free(track);
}

static void
handle_head_destroy(struct wl_listener *listener, void *data)
{
	struct weston_head *head = data;
	struct weston_output *output;
	XfwayHeadTracker *track =
		container_of(listener, XfwayHeadTracker,
			     head_destroy_listener);

	xfway_head_tracker_destroy(track);

	output = weston_head_get_output(head);

	/* On shutdown path, the output might be already gone. */
	if (!output)
		return;

	if (count_remaining_heads(output, head) > 0)
		return;

	weston_output_destroy(output);
}

static XfwayHeadTracker *
xfway_head_tracker_from_head(struct weston_head *head)
{
	struct wl_listener *lis;

	lis = weston_head_get_destroy_listener(head, handle_head_destroy);
	if (!lis)
		return NULL;

	return container_of(lis, XfwayHeadTracker,
			    head_destroy_listener);
}

static void
xfway_head_tracker_create(DisplayInfo *display_info,
			struct weston_head *head)
{
	XfwayHeadTracker *track;

	track = zalloc(sizeof *track);
	if (!track)
		return;

	track->head_destroy_listener.notify = handle_head_destroy;
	weston_head_add_destroy_listener(head, &track->head_destroy_listener);
}

static void
simple_head_enable(DisplayInfo *wet, struct weston_head *head)
{
	struct weston_output *output;
	int ret = 0;

	output = weston_compositor_create_output_with_head(wet->compositor,
							   head);
	if (!output) {
		weston_log("Could not create an output for head \"%s\".\n",
			   weston_head_get_name(head));

		return;
	}

	if (wet->simple_output_configure)
		ret = wet->simple_output_configure(output);
	if (ret < 0) {
		weston_log("Cannot configure output \"%s\".\n",
			   weston_head_get_name(head));
		weston_output_destroy(output);

		return;
	}

	if (weston_output_enable(output) < 0) {
		weston_log("Enabling output \"%s\" failed.\n",
			   weston_head_get_name(head));
		weston_output_destroy(output);

		return;
	}

  xfway_head_tracker_create (wet, head);
}

static void
simple_head_disable(struct weston_head *head)
{
	struct weston_output *output;
	XfwayHeadTracker *track;

	track = xfway_head_tracker_from_head(head);
	if (track)
		xfway_head_tracker_destroy(track);

	output = weston_head_get_output(head);
	//assert(output);
	weston_output_destroy(output);
}

static void
simple_heads_changed(struct wl_listener *listener, void *arg)
{
	struct weston_compositor *compositor = arg;
	DisplayInfo *wet = weston_compositor_get_user_data (compositor);
	struct weston_head *head = NULL;
	bool connected;
	bool enabled;
	bool changed;

	while ((head = weston_compositor_iterate_heads(wet->compositor, head))) {
		connected = weston_head_is_connected(head);
		enabled = weston_head_is_enabled(head);
		changed = weston_head_is_device_changed(head);

		if (connected && !enabled) {
			simple_head_enable(wet, head);
		} else if (!connected && enabled) {
			simple_head_disable(head);
		} else if (enabled && changed) {
			weston_log("Detected a monitor change on head '%s', "
				   "not bothering to do anything about it.\n",
				   weston_head_get_name(head));
		}
		weston_head_reset_device_changed(head);
	}
}

static void
compositor_set_simple_head_configurator(struct weston_compositor *compositor,
				 int (*fn)(struct weston_output *))
{
	DisplayInfo *wet = weston_compositor_get_user_data (compositor);

	wet->simple_output_configure = fn;

	wet->heads_changed_listener.notify = simple_heads_changed;
	weston_compositor_add_heads_changed_listener(compositor,
						&wet->heads_changed_listener);
}

static int new_output_notify_wayland (struct weston_output *output)
{
  //struct weston_output *output = data;
  DisplayInfo *server = weston_compositor_get_user_data (output->compositor);

  weston_output_set_scale (output, 1);
  weston_output_set_transform (output, WL_OUTPUT_TRANSFORM_NORMAL);
  server->api.windowed->output_set_size (output, 800, 600);

  Output *c_output;
  c_output = malloc (sizeof(Output));
  c_output->output = NULL;
  c_output->background = NULL;

  c_output->output = output;

  wl_list_insert (&server->outputs, &c_output->link);

  return 0;

}

static int load_drm_backend (DisplayInfo *server, int32_t use_pixman)
{
  struct weston_drm_backend_config config = {{ 0, }};
  int ret = 0;

  config.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
	config.base.struct_size = sizeof (struct weston_drm_backend_config);
  config.use_pixman = use_pixman;

  ret = weston_compositor_load_backend (server->compositor, WESTON_BACKEND_DRM, &config.base);

  server->api.drm = weston_drm_output_get_api (server->compositor);
  if (server->api.drm == NULL)
    return 1;

  compositor_set_simple_head_configurator(server->compositor, new_output_notify_drm);

  return ret;
}

static int load_wayland_backend (DisplayInfo *server, int32_t use_pixman)
{

  struct weston_wayland_backend_config config = {{ 0, }};
  int ret = 0;

	config.base.struct_version = WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
	config.base.struct_size = sizeof (struct weston_wayland_backend_config);

	config.cursor_size = 32;
	config.display_name = 0;
	config.use_pixman = use_pixman;
	config.sprawl = 0;
	config.fullscreen = 0;
	config.cursor_theme = NULL;


	ret = weston_compositor_load_backend (server->compositor, WESTON_BACKEND_WAYLAND, &config.base);

  server->api.windowed = weston_windowed_output_get_api (server->compositor);

  compositor_set_simple_head_configurator(server->compositor, new_output_notify_wayland);

  server->api.windowed->create_head (server->compositor, "W1");

  return ret;
}

void background_create (DisplayInfo *server, Output *o)
{
  if (server->background == NULL)
    {
      weston_layer_init (&server->background_layer, server->compositor);
      weston_layer_set_position (&server->background_layer, WESTON_LAYER_POSITION_BACKGROUND - 1);
      server->background = weston_surface_create (server->compositor);
      weston_surface_set_size (server->background, o->output->width, o->output->height);
      weston_surface_set_color (server->background, 0, 0, 0, 1);
      server->background_view = weston_view_create (server->background);
      weston_layer_entry_insert (&server->background_layer.view_list, &server->background_view->layer_link);
    }
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

int main (int    argc,
          char **argv)
{
	struct wl_display *display;
	struct weston_compositor *ec = NULL;
	int ret = 0;
  const char *socket_name = NULL;
  DisplayInfo *server;
  struct weston_output *output;

  server = malloc (sizeof(DisplayInfo));

  server->background = NULL;
  server->window_switcher = NULL;


   /* pid_t id = fork ();

  if (id > 0)
  {
    gtk_init (&argc, &argv);
    server->gdisplay = gdk_display_get_default ();
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_size_request (window, 30,30);
    gtk_widget_show_all (window);
    g_signal_connect (window, "destroy", gtk_main_quit, NULL);
    gtk_main ();
  }*/

  //else
  //{

	display = wl_display_create ();

  wl_list_init(&child_process_list);

	server->compositor = weston_compositor_create (display, NULL);
  weston_compositor_set_xkb_rule_names (server->compositor, NULL);

	if (!server->compositor)
		return 0;

  weston_log_set_handler (vlog, vlog_continue);

  int i;
  int32_t use_pixman = 0;

  for (i = 1; i < argc; i++)
      {
        if (strcmp (argv[i], "--use-pixman") == 0)
          use_pixman = 1;
      }

	server->compositor->default_pointer_grab = NULL;
	server->compositor->vt_switching = true;

	server->compositor->repaint_msec = 16;
	server->compositor->idle_time = 300;

  server->compositor->kb_repeat_rate = 40;
  server->compositor->kb_repeat_delay = 400;

  server->compositor->user_data = server;

  wl_list_init (&server->outputs);

  enum weston_compositor_backend backend = WESTON_BACKEND_DRM;
  if (getenv("WAYLAND_DISPLAY") || getenv("WAYLAND_SOCKET"))
		backend = WESTON_BACKEND_WAYLAND;

  switch (backend)
    {
    case WESTON_BACKEND_DRM:
      ret = load_drm_backend (server, use_pixman);
      if (ret != 0)
        return ret;
      break;
    case WESTON_BACKEND_WAYLAND:
    ret = load_wayland_backend (server, use_pixman);
      if (ret != 0)
        return ret;
      break;
    default:
      return 1;
    }

  weston_compositor_flush_heads_changed (server->compositor);

  Output *o;

  wl_list_for_each (o, &server->outputs, link)
      {
        background_create (server, o);
      }

  socket_name = wl_display_add_socket_auto (display);
  if (socket_name)
  {
    weston_log ("Compositor running on %s", socket_name);
    setenv ("WAYLAND_DISPLAY", socket_name, 1);
    unsetenv ("DISPLAY");
  }

  xfway_server_shell_init (server, &argc, &argv);

  weston_compositor_wake (server->compositor);
  wl_display_run (display);

  weston_compositor_destroy (server->compositor);
    //}

	return 0;
}
