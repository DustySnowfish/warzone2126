// SPDX-License-Identifier: GPL-2.0-or-later

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
 * The main menu background: clips of real battles recorded in the engine (the visual bench's
 * record mode, tools/w2126/menu_footage), played one after another behind the menus with a fade
 * between them. Silent; the clips live in data/base/misc/menufootage (WebM, VP9).
 *
 * Shown on the title screens except the lobby (which shows its map preview), when the Main Menu
 * Background option is Battle Footage and the build can play WebM. Otherwise the classic
 * backdrop pictures show as before.
 */

#pragma once

namespace menu_footage
{

/// Draw the current frame over the whole screen (cover-fit), advancing playback. Returns false
/// when there is nothing to show (option off, no clips, lobby), so the classic backdrop stays.
bool draw();

/// Stop playback and free the decoder and texture (leaving the title screens)
void stop();

} // namespace menu_footage
