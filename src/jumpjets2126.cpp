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

#include "jumpjets2126.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/sync_debug.h"

#include "droid.h"
#include "fpath.h"
#include "game_world.h"
#include "map.h"
#include "move.h"
#include "objmem.h"
#include "order.h"
#include "orderdef.h"
#include "stats.h"

namespace jumpjets2126
{

namespace
{

bool uiTargeting = false;

constexpr int kPeakHeight = 220;      // world units above the higher of take-off and landing
constexpr uint32_t kBaseTime = 1100;  // ms in the air, plus kTimePerTile for each tile flown
constexpr uint32_t kTimePerTile = 60;

int jumpRange(const DROID* psDroid)
{
	const PROPULSION_STATS* prop = psDroid->getPropulsionStats();
	return prop ? prop->jumpRange2126 : 0;
}

bool canStand(const DROID* psDroid, int tileX, int tileY)
{
	if (tileX < 1 || tileY < 1 || tileX >= gameWorld.map.width - 1 || tileY >= gameWorld.map.height - 1)
	{
		return false;
	}
	const MAPTILE* tile = mapTile(gameWorld.map, tileX, tileY);
	const unsigned char type = terrainType(tile);
	if (type == TER_WATER || type == TER_CLIFFFACE)
	{
		return false;
	}
	return !fpathBlockingTile(gameWorld.map, tileX, tileY, psDroid->getPropulsionStats()->propulsionType);
}

} // namespace

bool hasJumpJets(const DROID* psDroid)
{
	return psDroid != nullptr && jumpRange(psDroid) > 0;
}

bool jumping(const DROID* psDroid)
{
	return psDroid != nullptr && psDroid->jumpStart2126 != 0;
}

bool ready(const DROID* psDroid)
{
	return hasJumpJets(psDroid) && !jumping(psDroid) && gameTime >= psDroid->jumpReady2126 && !isDead(psDroid);
}

uint32_t cooldownLeft(const DROID* psDroid)
{
	if (!hasJumpJets(psDroid) || jumping(psDroid))
	{
		return 0;
	}
	return psDroid->jumpReady2126 > gameTime ? psDroid->jumpReady2126 - gameTime : 0;
}

bool landingSpot(const DROID* psDroid, Vector2i target, Vector2i& landing)
{
	const int range = jumpRange(psDroid) * TILE_UNITS;
	if (range <= 0)
	{
		return false;
	}
	// Clamp to the jump range along the line to the target (integer maths)
	Vector2i delta = target - psDroid->pos.xy();
	const int dist = iHypot(delta);
	if (dist > range)
	{
		delta = delta * range / dist;
	}
	const Vector2i aim = psDroid->pos.xy() + delta;
	const int tx = map_coord(aim.x), ty = map_coord(aim.y);
	// The aimed tile, or the nearest standable tile around it (fixed order, so every player agrees)
	static const int kOrder[9][2] = { { 0, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 } };
	for (const auto& o : kOrder)
	{
		const int x = tx + o[0], y = ty + o[1];
		if (canStand(psDroid, x, y) && iHypot(Vector2i(world_coord(x) + TILE_UNITS / 2, world_coord(y) + TILE_UNITS / 2) - psDroid->pos.xy()) <= range + TILE_UNITS)
		{
			landing = Vector2i(world_coord(x) + TILE_UNITS / 2, world_coord(y) + TILE_UNITS / 2);
			return true;
		}
	}
	return false;
}

bool start(DROID* psDroid, Vector2i target)
{
	if (!ready(psDroid) || psDroid->isTransporter())
	{
		return false;
	}
	Vector2i landing;
	if (!landingSpot(psDroid, target, landing))
	{
		syncDebug("Jump: droid %u has nowhere to land", psDroid->id);
		return false;
	}
	moveStopDroid(psDroid);
	psDroid->sMove.speed = 0;
	psDroid->jumpFrom2126 = psDroid->pos.xy();
	psDroid->jumpTo2126 = landing;
	psDroid->jumpFromZ2126 = psDroid->pos.z;
	psDroid->jumpToZ2126 = map_Height(gameWorld.map, landing.x, landing.y);
	const int tiles = iHypot(landing - psDroid->pos.xy()) / TILE_UNITS;
	psDroid->jumpDuration2126 = kBaseTime + kTimePerTile * static_cast<uint32_t>(tiles);
	psDroid->jumpStart2126 = gameTime;
	psDroid->rot.direction = iAtan2(landing - psDroid->pos.xy());
	syncDebug("Jump: droid %u from (%d,%d) to (%d,%d)", psDroid->id, psDroid->pos.x, psDroid->pos.y, landing.x, landing.y);
	return true;
}

int arcHeight(const DROID* psDroid, uint32_t time)
{
	if (!jumping(psDroid) || psDroid->jumpDuration2126 == 0)
	{
		return 0;
	}
	const int64_t d = psDroid->jumpDuration2126;
	const int64_t t = std::max<int64_t>(0, std::min<int64_t>(d, static_cast<int64_t>(time) - static_cast<int64_t>(psDroid->jumpStart2126)));
	const int64_t peak = kPeakHeight + std::abs(psDroid->jumpToZ2126 - psDroid->jumpFromZ2126) / 2;
	return static_cast<int>(4 * peak * t * (d - t) / (d * d));
}

bool update(DROID* psDroid)
{
	if (!jumping(psDroid))
	{
		return false;
	}
	const uint32_t elapsed = gameTime - psDroid->jumpStart2126;
	if (elapsed >= psDroid->jumpDuration2126)
	{
		// Touch down
		psDroid->pos.x = psDroid->jumpTo2126.x;
		psDroid->pos.y = psDroid->jumpTo2126.y;
		psDroid->pos.z = map_Height(gameWorld.map, psDroid->pos.x, psDroid->pos.y);
		psDroid->jumpStart2126 = 0;
		const PROPULSION_STATS* prop = psDroid->getPropulsionStats();
		psDroid->jumpReady2126 = gameTime + (prop ? prop->jumpCooldown2126 : 25000);
		psDroid->sMove.Status = MOVEINACTIVE;
		syncDebug("Jump: droid %u landed at (%d,%d)", psDroid->id, psDroid->pos.x, psDroid->pos.y);
		return true;
	}
	const int64_t d = psDroid->jumpDuration2126;
	const int64_t t = elapsed;
	psDroid->pos.x = static_cast<int>(psDroid->jumpFrom2126.x + (psDroid->jumpTo2126.x - psDroid->jumpFrom2126.x) * t / d);
	psDroid->pos.y = static_cast<int>(psDroid->jumpFrom2126.y + (psDroid->jumpTo2126.y - psDroid->jumpFrom2126.y) * t / d);
	psDroid->pos.z = static_cast<int>(psDroid->jumpFromZ2126 + (psDroid->jumpToZ2126 - psDroid->jumpFromZ2126) * t / d) + arcHeight(psDroid, gameTime);
	psDroid->sMove.speed = 0;
	return true;
}

void setTargeting(bool on)
{
	uiTargeting = on;
}

bool targeting()
{
	return uiTargeting;
}

bool anySelectedReady(unsigned player)
{
	if (player >= MAX_PLAYERS)
	{
		return false;
	}
	for (const DROID* psDroid : gameWorld.objects.droids[player])
	{
		if (psDroid->selected && ready(psDroid))
		{
			return true;
		}
	}
	return false;
}

bool orderSelected(unsigned player, int x, int y)
{
	uiTargeting = false;
	if (player >= MAX_PLAYERS)
	{
		return false;
	}
	bool any = false;
	for (DROID* psDroid : gameWorld.objects.droids[player])
	{
		if (psDroid->selected && ready(psDroid))
		{
			orderDroidLoc(psDroid, DORDER_JUMP, x, y, ModeQueue);
			any = true;
		}
	}
	return any;
}

}
