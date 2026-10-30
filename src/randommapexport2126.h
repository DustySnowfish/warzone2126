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
 * Save the lobby's generated Random map (its seed and settings) as an ordinary map package in
 * the user's maps folder ("<players>c-Gen-<map code>.wz"), so it can be shared as a file, played
 * without the generator or opened in a map editor. It appears in the map list straight away.
 */

#pragma once

#include <string>

struct RandomMapExportResult2126
{
	bool ok = false;
	bool alreadySaved = false; ///< the same map was saved before (same file name)
	std::string name;          ///< the map's name in the map list
	std::string path;          ///< the file on disk
	std::string error;         ///< why it failed, for the player
};

RandomMapExportResult2126 exportRandomMap2126();

/// The map name a map code is saved under
std::string randomMapExportName2126(const std::string &code);
