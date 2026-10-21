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
/** @file time_of_day.h
 * Time of day: the map's own light (default), a fixed dawn, day, dusk or night, or a full
 * day-night cycle. Moves the sun (direction, colour and strength, so shadows swing and
 * lengthen), dims and tints the ambient light, recolours the sky, and swaps the sun for a faint
 * blue moonlight at night. Also applies the weather's light tint on top.
 *
 * Display only: the sun direction it sets is presentation state, not game state.
 */

#pragma once

#include "lib/framework/vector.h"

namespace time_of_day
{

enum class Mode : int { MapDefault = 0, Dawn, Day, Dusk, Night, Cycle };

/// Advance and apply (call once per frame, after weather::update).
void update();

/// Forget the map's base sun (level change; the next update reads it again).
void reset();

/// The map's own sun (what a savegame should keep, not the moved one).
Vector3f sunForSaving();

/// Current time as a 0..1 phase of the day (0.25 sunrise, 0.5 noon, 0.75 sunset), for logs.
float phase();

} // namespace time_of_day
