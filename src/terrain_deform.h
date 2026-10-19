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
/** @file terrain_deform.h
 * Deformable terrain: craters from heavy weapons (and, later, raised earthworks).
 *
 * Terrain height is game state (line of sight, indirect fire, unit heights), so every
 * change here runs in the game logic, in integer maths, with no random numbers: all
 * players and replays compute exactly the same terrain. Changed heights are saved with
 * the map in savegames.
 */

#pragma once

#include "lib/framework/vector.h"

struct WEAPON_STATS;
struct WorldMapState;
struct GameWorld;
struct BASE_OBJECT;

namespace terrain_deform
{

/// Forget per-map state (call when the map is shut down).
void reset();

/// A projectile landed at `position` (world x, y and height). Heavy indirect weapons, bombs
/// and the laser satellite dig a crater; everything else does nothing.
void projectileImpact(GameWorld& world, const WEAPON_STATS* weapon, unsigned player, const Vector3i& position);

/// Dig a crater (negative depth) or raise a mound (positive height) centred on world (x, y).
/// `radius` and `depth` are world units. Returns whether any height changed.
bool deform(GameWorld& world, int x, int y, int radius, int depth);

/// Direct fire at a ground unit that sits behind a ridge or inside a crater (hull-down) does
/// less damage. `origin` is where the shot came from. Returns the damage to apply.
unsigned coverDamage(const GameWorld& world, const WEAPON_STATS* weapon, const Vector3i& origin, const BASE_OBJECT* target, unsigned damage);

/// Total craters dug this game (for tests and benchmarks).
unsigned craterCount();

} // namespace terrain_deform
