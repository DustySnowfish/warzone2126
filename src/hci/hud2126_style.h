/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
/// Warzone 2126 interface: shared colours (the A2 design: dark slate panels, a light-blue accent).

#ifndef __INCLUDED_SRC_HCI_HUD2126_STYLE_H__
#define __INCLUDED_SRC_HCI_HUD2126_STYLE_H__

#include "lib/ivis_opengl/piepalette.h"

namespace hud2126
{
inline PIELIGHT bg() { return pal_RGBA(18, 24, 30, 225); }
inline PIELIGHT tab() { return pal_RGBA(27, 34, 40, 235); }
inline PIELIGHT tabHover() { return pal_RGBA(34, 44, 52, 240); }
inline PIELIGHT tabActive() { return pal_RGBA(42, 58, 70, 245); }
inline PIELIGHT accent() { return pal_RGBA(79, 179, 232, 255); }
inline PIELIGHT border() { return pal_RGBA(255, 255, 255, 34); }
inline PIELIGHT label() { return pal_RGBA(140, 156, 168, 255); }
inline PIELIGHT value() { return pal_RGBA(232, 238, 242, 255); }
inline PIELIGHT text() { return pal_RGBA(196, 206, 214, 255); }
inline PIELIGHT disabled() { return pal_RGBA(90, 100, 108, 255); }
inline PIELIGHT good() { return pal_RGBA(111, 207, 122, 255); }
inline PIELIGHT warn() { return pal_RGBA(232, 193, 61, 255); }
inline PIELIGHT bad() { return pal_RGBA(229, 87, 79, 255); }

/// Health colour: green above 60%, amber above 30%, red below
inline PIELIGHT health(float fraction)
{
	return fraction > 0.6f ? good() : (fraction > 0.3f ? warn() : bad());
}
} // namespace hud2126

#endif // __INCLUDED_SRC_HCI_HUD2126_STYLE_H__
