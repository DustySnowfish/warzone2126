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
/** @file terrain_deform.cpp
 * Deformable terrain (see terrain_deform.h). Game logic: integer maths only.
 */

#include "terrain_deform.h"

#include "lib/framework/frame.h"
#include "lib/framework/trig.h"

#include "objectdef.h"
#include "droid.h"
#include "feature.h"
#include "game_world.h"
#include "lighting.h"
#include "map.h"
#include "stats.h"
#include "statsdef.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace terrain_deform
{

namespace
{

// A crater may go at most this far below the ground as the map was at the first crater
constexpr int kMaxDepthBelowOriginal = 112;   // world units (3.5 m)
// Crater rims may rise at most this far above it
constexpr int kMaxRiseAboveOriginal = 40;
// Rim width as a fraction of the crater radius, in 1/1024
constexpr int64_t kRimWidth1024 = 360;
// Rim height as a fraction of the crater depth, in 1/1024
constexpr int kRimHeight1024 = 230;
// Impacts this far above the ground (airbursts, hits high on a big target) leave no crater
constexpr int kMaxImpactHeightAboveGround = 160;

struct State
{
	const WorldMapState* map = nullptr;
	int width = 0;
	int height = 0;
	std::vector<int32_t> originalHeights; // tile corner heights before any crater
	unsigned craters = 0;
};

State s;

void ensureOriginal(const WorldMapState& map)
{
	if (s.map == &map && s.width == map.width && s.height == map.height && !s.originalHeights.empty())
	{
		return;
	}
	s.map = &map;
	s.width = map.width;
	s.height = map.height;
	s.originalHeights.resize(static_cast<size_t>(map.width) * static_cast<size_t>(map.height));
	for (int y = 0; y < map.height; ++y)
	{
		for (int x = 0; x < map.width; ++x)
		{
			s.originalHeights[static_cast<size_t>(x + y * map.width)] = map_TileHeight(map, x, y);
		}
	}
}

/// A tile corner can move unless it is at the map border or touches a structure or water.
bool cornerMovable(const WorldMapState& map, int x, int y)
{
	if (x <= 1 || y <= 1 || x >= map.width - 2 || y >= map.height - 2)
	{
		return false;
	}
	for (int ty = y - 1; ty <= y; ++ty)
	{
		for (int tx = x - 1; tx <= x; ++tx)
		{
			const MAPTILE* tile = mapTile(map, tx, ty);
			if (TileHasStructure(tile) || terrainType(tile) == TER_WATER || tile->waterLevel > tile->height)
			{
				return false;
			}
		}
	}
	return true;
}

/// Height change (world units) at squared distance d2 from the centre: a smooth bowl inside the
/// radius and a raised rim just outside it. Negative depth digs, positive depth raises.
int profile(int64_t d2, int64_t r2, int64_t rimOuter2, int depth)
{
	if (d2 < r2)
	{
		const int64_t t = d2 * 1024 / r2;                  // 0 at the centre, 1024 at the edge
		const int64_t w = ((1024 - t) * (1024 - t)) >> 10; // 1024 at the centre, 0 at the edge
		return static_cast<int>((static_cast<int64_t>(depth) * w) / 1024);
	}
	if (depth < 0 && d2 < rimOuter2)
	{
		const int64_t t = (d2 - r2) * 1024 / std::max<int64_t>(rimOuter2 - r2, 1); // 0..1024 across the rim
		const int64_t w = (4 * t * (1024 - t)) >> 10;                              // 0, up to 1024, 0
		return static_cast<int>((static_cast<int64_t>(-depth) * kRimHeight1024 / 1024 * w) / 1024);
	}
	return 0;
}

/// Put ground units and features in the changed area back on the ground.
void resettleObjects(GameWorld& world, int x, int y, int reach)
{
	const int64_t reach2 = static_cast<int64_t>(reach) * reach;
	for (unsigned player = 0; player < MAX_PLAYERS; ++player)
	{
		for (DROID* psDroid : world.objects.droids[player])
		{
			if (psDroid->died || psDroid->isFlying())
			{
				continue;
			}
			const int64_t dx = psDroid->pos.x - x;
			const int64_t dy = psDroid->pos.y - y;
			if (dx * dx + dy * dy <= reach2)
			{
				psDroid->pos.z = map_Height(world.map, psDroid->pos.x, psDroid->pos.y);
			}
		}
	}
	for (FEATURE* psFeature : world.objects.features[0])
	{
		if (psFeature->died)
		{
			continue;
		}
		const int64_t dx = psFeature->pos.x - x;
		const int64_t dy = psFeature->pos.y - y;
		if (dx * dx + dy * dy <= reach2)
		{
			psFeature->pos.z = map_Height(world.map, psFeature->pos.x, psFeature->pos.y);
		}
	}
}

} // anonymous namespace

void reset()
{
	s = State();
}

bool deform(GameWorld& world, int x, int y, int radius, int depth)
{
	WorldMapState& map = world.map;
	if (radius <= 0 || depth == 0 || map.width <= 4 || map.height <= 4 || !worldOnMap(map, x, y))
	{
		return false;
	}
	ensureOriginal(map);
	const int64_t r2 = static_cast<int64_t>(radius) * radius;
	const int64_t rimOuter = static_cast<int64_t>(radius) * (1024 + kRimWidth1024) / 1024;
	const int64_t rimOuter2 = rimOuter * rimOuter;
	const int reach = static_cast<int>(rimOuter) + TILE_UNITS;
	const int x0 = std::max(0, map_coord(x - reach));
	const int y0 = std::max(0, map_coord(y - reach));
	const int x1 = std::min(map.width - 1, map_coord(x + reach) + 1);
	const int y1 = std::min(map.height - 1, map_coord(y + reach) + 1);

	bool changed = false;
	for (int ty = y0; ty <= y1; ++ty)
	{
		for (int tx = x0; tx <= x1; ++tx)
		{
			const int64_t dx = world_coord(tx) - x;
			const int64_t dy = world_coord(ty) - y;
			const int delta = profile(dx * dx + dy * dy, r2, rimOuter2, depth);
			if (delta == 0 || !cornerMovable(map, tx, ty))
			{
				continue;
			}
			const int original = s.originalHeights[static_cast<size_t>(tx + ty * map.width)];
			const int current = map_TileHeight(map, tx, ty);
			const int lowest = std::max(0, original - kMaxDepthBelowOriginal);
			const int highest = original + kMaxRiseAboveOriginal;
			const int target = std::max(lowest, std::min(highest, current + delta));
			if (target != current)
			{
				setTileHeight(map, tx, ty, target);
				changed = true;
			}
		}
	}
	if (changed)
	{
		resettleObjects(world, x, y, reach);
		// Terrain shading follows the new slopes (display only)
		initLighting(map, static_cast<UDWORD>(std::max(0, x0 - 1)), static_cast<UDWORD>(std::max(0, y0 - 1)),
		             static_cast<UDWORD>(std::min(map.width, x1 + 2)), static_cast<UDWORD>(std::min(map.height, y1 + 2)));
	}
	return changed;
}

void projectileImpact(GameWorld& world, const WEAPON_STATS* weapon, unsigned player, const Vector3i& position)
{
	if (weapon == nullptr || player >= MAX_PLAYERS)
	{
		return;
	}
	const bool artillery = weapon->movementModel == MM_INDIRECT || weapon->movementModel == MM_HOMINGINDIRECT;
	const bool heavy = artillery || weapon->weaponSubClass == WSC_BOMB || weapon->weaponSubClass == WSC_LAS_SAT;
	const int blastRadius = static_cast<int>(weapon->upgrade[player].radius);
	if (!heavy || blastRadius <= 0)
	{
		return;
	}
	if (!worldOnMap(world.map, position.x, position.y)
	    || position.z - map_Height(world.map, position.x, position.y) > kMaxImpactHeightAboveGround)
	{
		return;
	}
	// The crater is a little smaller than the blast; depth follows the damage
	const int damage = static_cast<int>(weapon->upgrade[player].damage + weapon->upgrade[player].radiusDamage);
	const int radius = std::max(64, std::min(blastRadius * 3 / 4, 3 * TILE_UNITS));
	const int depth = std::max(6, std::min(damage / 6, weapon->weaponSubClass == WSC_LAS_SAT ? 96 : 48));
	if (deform(world, position.x, position.y, radius, -depth))
	{
		++s.craters;
	}
}

unsigned craterCount()
{
	return s.craters;
}

} // namespace terrain_deform
