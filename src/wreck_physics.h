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
/** @file wreck_physics.h
 * Physical destruction of vehicles: when a unit dies its hull, turret and debris become rigid
 * pieces that fly, tumble, bounce and skid on the terrain (craters and earthworks included),
 * settle as a smoking wreck, and later sink into the ground.
 *
 * Display only: pieces never touch game state and use their own random numbers, so they
 * cannot affect multiplayer sync (and nothing can collide with them).
 */

#pragma once

#include "lib/framework/vector.h"

#include <glm/fwd.hpp>

struct DROID;
struct STRUCTURE;

namespace wreck_physics
{

/// Whether physical wrecks are switched on (Physical Wrecks setting).
bool enabled();

/// A vehicle was destroyed: break it into pieces.
void droidDestroyed(const DROID* psDroid);

/// A structure was destroyed: it collapses into the ground, throws chunks and leaves smoking rubble.
void structureDestroyed(const STRUCTURE* psStruct);

/// Advance the pieces (call once per frame).
void update();

/// Draw the pieces (call in the 3D object pass).
void draw(const glm::mat4& viewMatrix);

/// Remove all pieces (level change).
void clear();

} // namespace wreck_physics
