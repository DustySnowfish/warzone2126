/*
	This file is part of Warzone 2126, based on Warzone 2100.
	Copyright (C) 2026  Warzone 2126 contributors

	Warzone 2126 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2126 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2126; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/**
 * Colours of the generic widgets that are not in palette.txt (button and tab fills). The game sets
 * them to match its interface style (Warzone 2126: dark slate with a light-blue accent); the
 * defaults are the classic purples.
 */

#ifndef __INCLUDED_LIB_WIDGET_WIDGTHEME_H__
#define __INCLUDED_LIB_WIDGET_WIDGTHEME_H__

#include "lib/ivis_opengl/piepalette.h"

namespace widget_theme
{

struct Theme
{
	bool flat = false;                                   ///< flat panels and thin borders instead of bevels
	PIELIGHT fill = pal_RGBA(25, 0, 110, 220);           ///< button or tab
	PIELIGHT fillDark = pal_RGBA(10, 0, 70, 250);        ///< pressed, selected or disabled
	PIELIGHT border = pal_RGBA(255, 255, 255, 150);      ///< hovered or selected edge
	PIELIGHT panel = pal_RGBA(20, 0, 88, 230);           ///< table headers and similar panels
};

const Theme &get();
void set(const Theme &theme);

} // namespace widget_theme

#endif // __INCLUDED_LIB_WIDGET_WIDGTHEME_H__
