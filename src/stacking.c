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


        xfwm4    - (c) 2002-2011 Olivier Fourdan

 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>

#include "display.h"
#include "screen.h"
#include "client.h"
#include "stacking.h"

void
clientAddToList (Client * c)
{
    ScreenInfo *screen_info;
    //DisplayInfo *display_info;

    g_return_if_fail (c != NULL);
    //TRACE ("client \"%s\" (0x%lx)", c->name, c->window);

    screen_info = c->screen_info;
    //display_info = screen_info->display_info;
    //myDisplayAddClient (display_info, c);

    screen_info->client_count++;
    if (screen_info->clients)
    {
        c->prev = screen_info->clients->prev;
        c->next = screen_info->clients;
        screen_info->clients->prev->next = c;
        screen_info->clients->prev = c;
    }
    else
    {
        screen_info->clients = c;
        c->next = c;
        c->prev = c;
    }

    screen_info->windows = g_list_append (screen_info->windows, c);
    screen_info->windows_stack = g_list_append (screen_info->windows_stack, c);

    //clientSetNetClientList (screen_info, display_info->atoms[NET_CLIENT_LIST], screen_info->windows);

    //FLAG_SET (c->xfwm_flags, XFWM_FLAG_MANAGED);
}

void
clientRemoveFromList (Client * c)
{
    ScreenInfo *screen_info;
    //DisplayInfo *display_info;

    g_return_if_fail (c != NULL);
    //TRACE ("client \"%s\" (0x%lx)", c->name, c->window);

    //FLAG_UNSET (c->xfwm_flags, XFWM_FLAG_MANAGED);

    screen_info = c->screen_info;
    //display_info = screen_info->display_info;
   // myDisplayRemoveClient (display_info, c);

    g_assert (screen_info->client_count > 0);
    screen_info->client_count--;
    if (screen_info->client_count == 0)
    {
        screen_info->clients = NULL;
    }
    else
    {
        c->next->prev = c->prev;
        c->prev->next = c->next;
        if (c == screen_info->clients)
        {
            screen_info->clients = screen_info->clients->next;
        }
    }

    screen_info->windows = g_list_remove (screen_info->windows, c);
    screen_info->windows_stack = g_list_remove (screen_info->windows_stack, c);

    //clientSetNetClientList (screen_info, display_info->atoms[NET_CLIENT_LIST], screen_info->windows);
    //clientSetNetClientList (screen_info, display_info->atoms[NET_CLIENT_LIST_STACKING], screen_info->windows_stack);

    //FLAG_UNSET (c->xfwm_flags, XFWM_FLAG_MANAGED);
}
