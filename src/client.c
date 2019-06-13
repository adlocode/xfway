/*      $Id$

        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation; either version 2, or (at your option)
        any later version.

        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with this program; if not, write to the Free Software
        Foundation, Inc., Inc., 51 Franklin Street, Fifth Floor, Boston,
        MA 02110-1301, USA.


        oroborus - (c) 2001 Ken Lynch
        xfwm4    - (c) 2002-2011 Olivier Fourdan

 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "client.h"
#include "screen.h"

Client *
clientFrame (ScreenInfo *screen_info,
             struct zwlr_foreign_toplevel_handle_v1 *toplevel_handle,
             gboolean recapture)
{
    Client *c = NULL;
    gboolean shaped;
    unsigned long valuemask;
    long pid;
    int i;

    c = g_new0 (Client, 1);
    if (!c)
    {
        fprintf (stderr, "cannot allocate memory for the window structure\n");
        return NULL;
    }

    c->toplevel_handle = toplevel_handle;
    c->screen_info = screen_info;

    return c;
}

void
clientUnframe (Client *c, gboolean remap)
{

}
