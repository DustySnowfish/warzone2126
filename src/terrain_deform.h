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
struct DROID;

namespace terrain_deform
{

/// Largest earthwork area side, world units (orders with a bigger rectangle are rejected).
constexpr int kEarthworkMaxSize = 8 * 128;

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

/// One work step of a truck on its DORDER_EARTHWORK order (raise or lower the ground in the
/// order's rectangle a little towards its target). Returns true when the earthwork is complete.
bool earthworkStep(GameWorld& world, DROID* psDroid);

/// One work step on the tile rectangle with world corners a and b (no cadence check).
/// Earthwork modes (the order's direction field): raise, lower, or level to the area's average height.
enum EarthworkMode : int { EARTHWORK_RAISE = 0, EARTHWORK_LOWER = 1, EARTHWORK_LEVEL = 2 };

bool earthworkApply(GameWorld& world, Vector2i a, Vector2i b, int mode);

/// Total craters dug this game (for tests and benchmarks).
unsigned craterCount();

/// Display: counter bumped whenever a terrain height changes (to refresh display data).
unsigned changeCount();

/// Display: how far (world units) a tile corner is below its original height (0 if not dug).
int depthBelowOriginal(const WorldMapState& map, int x, int y);

} // namespace terrain_deform
