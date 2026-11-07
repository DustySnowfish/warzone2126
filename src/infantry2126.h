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
/** \file
 *  Warzone 2126 infantry (design/infantry.md): cyborgs drawn as fireteams of animated figures.
 *
 *  A cyborg unit stays one game object (position, health, hit box); this module only draws it,
 *  as 2 or 3 figures in a loose formation that walk, run, aim and fire, and that fall (breaking
 *  into physical parts) as the unit loses health. Display only: no game state is read in a way
 *  that changes it, so balance and multiplayer sync are untouched.
 *
 *  Figures are rigid-part skeletons from infantry2126/rigs.json: each joint carries one model, and
 *  clips animate the joints (rotations in degrees, a vertical offset for the pelvis bob).
 */

#pragma once

#include "lib/ivis_opengl/pietypes.h"
#include <glm/fwd.hpp>

struct DROID;

namespace infantry2126
{

/// The Infantry Fireteams setting is on and this droid has a rig (cyborgs).
bool handles(const DROID* psDroid);

/// Draw the fireteam for this droid. unitMatrix: the droid's translation and heading (no pitch
/// or roll; figures stand upright). Returns true if anything was drawn.
bool draw(const DROID* psDroid, const glm::mat4& unitMatrix, const glm::mat4& viewMatrix, uint32_t colour, PIELIGHT brightness);

/// The unit was destroyed: the figures still standing break apart.
void droidDestroyed(const DROID* psDroid);

/// Forget all fireteams (new game, load).
void clear();

/// Re-read the rigs (they are loaded on first use).
void reload();

}
