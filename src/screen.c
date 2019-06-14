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

#include "screen.h"

ScreenInfo *
myScreenInit (GdkScreen *gscr)
{
    ScreenInfo *screen_info;

    screen_info = g_new0 (ScreenInfo, 1);

    screen_info->gscr = gscr;

    screen_info->toplevel_manager = NULL;

    return (screen_info);
}

gint
myScreenGetNumMonitors (ScreenInfo *screen_info)
{
    return 1;
}

gint
myScreenGetMonitorIndex (ScreenInfo *screen_info, gint idx)
{
    return 1;
}
