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
/** @file weather.h
 * Weather: rain, snow and dust storms that come and go.
 *
 * A storm builds up over about twenty seconds, holds, then clears. While it lasts, particles
 * fall or blow around the camera, the ground gets wet (rain) or white (snow), clouds thicken,
 * the haze closes in and the sun dims. The type and rhythm come from the environment file's
 * "weather" block (per tileset or map); a campaign script's setWeather() overrides it.
 *
 * Display only: it uses its own random numbers and never touches game state, so every
 * player can see different weather without any effect on multiplayer sync.
 */

#pragma once

#include "lib/ivis_opengl/pietypes.h"

#include <glm/glm.hpp>

namespace weather
{

/// Advance the weather and emit its particles (call once per frame, after particles::update).
void update();

/// Ground state for the terrain shader: > 0 wet (rain), < 0 snow cover, 0 dry. -1..1.
float groundState();

/// Colour multiplier the weather puts on one light (applied together with the time of day).
glm::vec3 lightTint(LIGHTING_TYPE light);

/// Current storm strength, 0..1 (for logs and benchmarks).
float intensity();

/// Forget the current weather (level change).
void reset();

/// Warzone 2126 recordings (visual bench): hold the weather at `level` (0..1) of a type
/// (0 none, 1 rain, 2 snow, 3 dust) instead of the tileset's storm schedule. type < 0 releases it.
void setForced(int type, float level);

} // namespace weather
