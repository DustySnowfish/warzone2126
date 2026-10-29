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
 * Ambient life around the camera: flocks of birds over land that scatter at gunfire and
 * explosions, gulls along the coast, surf breaking on the shore, and grass fires.
 *
 * Render-only: it keeps its own random numbers and never touches the game state, so each
 * player sees their own birds without any effect on multiplayer sync.
 */

#pragma once

#include "lib/framework/vector.h"

namespace ambient
{

/// Advance and draw this frame's ambient life (call once per frame, after particles::update).
void update();

/// Something loud happened at `position` (world): shots and explosions. `strength` 1 for an
/// ordinary impact; larger for big explosions. Birds within reach scatter.
void disturb(const Vector3f& position, float strength);

/// A shot or explosion landed at `position` (world); with probability `chance` (times how readily
/// the ground there burns) it sets the grass alight. Grass fires are visual only for now: they
/// spread downwind across grass and dry scrub on Arizona maps, then leave charred ground.
void ignite(const Vector3f& position, float chance);

/// Tiles burning now (for tests).
size_t fireCount();

/// Forget everything (level change).
void reset();

/// Live birds (for benchmark results and tests).
size_t birdCount();

} // namespace ambient
