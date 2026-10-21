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
/** @file tunnels.cpp
 * Tunnel network prototype (see tunnels.h).
 */

#include "tunnels.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/sync_debug.h"

#include "action.h"
#include "console.h"
#include "droid.h"
#include "fpath.h"
#include "pathfinding_backend.h"
#include "game_world.h"
#include "map.h"
#include "move.h"
#include "multiplay.h"
#include "objmem.h"
#include "order.h"
#include "particles.h"
#include "structure.h"
#include "visibility.h"

#include <algorithm>
#include <limits>

namespace tunnels
{

namespace
{

constexpr int kSpeed = 3 * TILE_UNITS;          // world units per second underground
constexpr uint32_t kGoingDownMs = 1500;         // time to get in and out
constexpr int kEnterReach = TILE_UNITS * 3 / 2; // how close a unit must get to its entrance

struct Transit
{
	DROID* droid = nullptr;
	uint32_t exitId = 0;
	Vector2i from { 0, 0 };
	Vector2i to { 0, 0 };
	uint32_t start = 0;
	uint32_t arrive = 0;
	bool wasSelected = false; // local interface state, restored when it comes up
};

std::vector<Transit> transits;
std::vector<DROID*> enterRequests;

int64_t distSq(Vector2i a, Vector2i b)
{
	const int64_t dx = a.x - b.x;
	const int64_t dy = a.y - b.y;
	return dx * dx + dy * dy;
}

/// Integer square root (game state: no floating point)
int64_t isqrt(int64_t v)
{
	if (v <= 0)
	{
		return 0;
	}
	int64_t r = static_cast<int64_t>(iSqrt(static_cast<uint32_t>(std::min<int64_t>(v, 0xFFFFFFFFLL))));
	while (r * r > v)
	{
		--r;
	}
	while ((r + 1) * (r + 1) <= v)
	{
		++r;
	}
	return r;
}

STRUCTURE* nearestEntrance(unsigned player, Vector2i pos, const STRUCTURE* exclude)
{
	STRUCTURE* best = nullptr;
	int64_t bestDist = 0;
	for (STRUCTURE* psStruct : gameWorld.objects.structures[player])
	{
		if (psStruct == exclude || psStruct->died || psStruct->status != SS_BUILT || !isEntrance(psStruct))
		{
			continue;
		}
		const int64_t d = distSq(psStruct->pos.xy(), pos);
		if (best == nullptr || d < bestDist || (d == bestDist && psStruct->id < best->id))
		{
			best = psStruct;
			bestDist = d;
		}
	}
	return best;
}

void dustAt(Vector2i pos)
{
	if (particles::hasEffect("collapse_dust"))
	{
		const int z = map_Height(gameWorld.map, pos.x, pos.y);
		particles::spawn("collapse_dust", Vector3f(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(z + 8)), Vector3f(0.f, 0.f, 1.f), 0.6f);
	}
}

void comeUp(GameWorld& world, Transit& t)
{
	DROID* psDroid = t.droid;
	STRUCTURE* exit = IdToStruct(t.exitId, psDroid->player);
	if (exit == nullptr || exit->died || exit->status != SS_BUILT)
	{
		exit = nearestEntrance(psDroid->player, t.to, nullptr);
	}
	const Vector2i at = exit ? exit->pos.xy() : t.from;
	Vector2i tile = map_coord(at);
	if (!pickATileGen(world, &tile, LOOK_FOR_EMPTY_TILE, zonedPAT))
	{
		tile = map_coord(at);
	}
	droidSetPosition(psDroid, world.map, world_coord(tile.x) + TILE_UNITS / 2, world_coord(tile.y) + TILE_UNITS / 2);
	addDroid(psDroid, world.objects.droids);
	updateDroidOrientation(psDroid, world.map);
	initDroidMovement(psDroid);
	psDroid->order = DroidOrder(DORDER_NONE);
	actionDroid(psDroid, DACTION_NONE);
	psDroid->selected = t.wasSelected;
	syncDebug("Tunnel out droid %u at (%d,%d)", psDroid->id, psDroid->pos.x, psDroid->pos.y);
	dustAt(psDroid->pos.xy());
}

} // anonymous namespace

bool isEntrance(const STRUCTURE* psStruct)
{
	return psStruct != nullptr && psStruct->pStructureType != nullptr && psStruct->pStructureType->id.compare(kEntranceId) == 0;
}

bool canUse(const DROID* psDroid)
{
	return psDroid != nullptr && !psDroid->isVtol() && !psDroid->isTransporter();
}

void orderStart(DROID* psDroid, DROID_ORDER_DATA* psOrder)
{
	STRUCTURE* exit = psOrder->psObj && psOrder->psObj->type == OBJ_STRUCTURE ? castStructure(psOrder->psObj) : nullptr;
	if (!canUse(psDroid) || exit == nullptr || !isEntrance(exit) || exit->player != psDroid->player || exit->status != SS_BUILT)
	{
		return;
	}
	// In at the entrance nearest the unit, unless walking straight to the exit is about as short
	STRUCTURE* entry = nearestEntrance(psDroid->player, psDroid->pos.xy(), nullptr);
	psDroid->order = *psOrder;
	if (entry == nullptr || entry == exit || distSq(psDroid->pos.xy(), exit->pos.xy()) <= distSq(psDroid->pos.xy(), entry->pos.xy()) * 4)
	{
		psDroid->order.pos = exit->pos.xy();
		psDroid->order.index = 0; // walking
		actionDroid(psDroid, DACTION_MOVE, exit->pos.x, exit->pos.y);
		if (psDroid->player == selectedPlayer)
		{
			// Local message only (no game state)
			static uint32_t lastWalkMessageTime = 0;
			if (gameTime != lastWalkMessageTime)
			{
				lastWalkMessageTime = gameTime;
				addConsoleMessage(entry == nullptr || entry == exit
					? _("No other tunnel entrance: units drive there. Build a second entrance to use the tunnels.")
					: _("That entrance is close: units drive there directly"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
			}
		}
		return;
	}
	psDroid->order.pos = entry->pos.xy();
	psDroid->order.index = 1; // going underground
	actionDroid(psDroid, DACTION_MOVE, entry->pos.x, entry->pos.y);
	if (psDroid->player == selectedPlayer)
	{
		// Local message only (no game state), once per order however many units were sent
		static uint32_t lastMessageTime = 0;
		if (gameTime != lastMessageTime)
		{
			lastMessageTime = gameTime;
			addConsoleMessage(_("Units heading for the nearest tunnel entrance"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
		}
	}
}

bool orderUpdate(DROID* psDroid)
{
	BASE_OBJECT* target = psDroid->order.psObj;
	if (target == nullptr || target->died)
	{
		return true;
	}
	const int64_t d2 = distSq(psDroid->pos.xy(), psDroid->order.pos);
	if (psDroid->order.index == 0)
	{
		// Walking to the exit: done when close
		if (d2 <= static_cast<int64_t>(kEnterReach) * kEnterReach * 4)
		{
			return true;
		}
		if (psDroid->action == DACTION_NONE)
		{
			actionDroid(psDroid, DACTION_MOVE, psDroid->order.pos.x, psDroid->order.pos.y);
		}
		return false;
	}
	if (d2 <= static_cast<int64_t>(kEnterReach) * kEnterReach)
	{
		if (std::find(enterRequests.begin(), enterRequests.end(), psDroid) == enterRequests.end())
		{
			enterRequests.push_back(psDroid);
		}
		return false;
	}
	if (psDroid->action == DACTION_NONE)
	{
		actionDroid(psDroid, DACTION_MOVE, psDroid->order.pos.x, psDroid->order.pos.y);
	}
	return false;
}

void update(GameWorld& world)
{
	// Down: units that reached their entrance this tick
	for (DROID* psDroid : enterRequests)
	{
		if (psDroid->died || psDroid->order.type != DORDER_TUNNEL || psDroid->order.psObj == nullptr || psDroid->order.psObj->died)
		{
			continue;
		}
		Transit t;
		t.droid = psDroid;
		t.exitId = psDroid->order.psObj->id;
		t.from = psDroid->order.pos;
		t.to = psDroid->order.psObj->pos.xy();
		t.start = gameTime;
		t.arrive = gameTime + kGoingDownMs + static_cast<uint32_t>(isqrt(distSq(t.from, t.to)) * 1000 / kSpeed);
		t.wasSelected = psDroid->selected;
		moveStopDroid(psDroid);
		orderDroidListEraseRange(psDroid, 0, psDroid->listSize);
		psDroid->order = DroidOrder(DORDER_NONE);
		actionDroid(psDroid, DACTION_NONE);
		psDroid->selected = false;
		dustAt(psDroid->pos.xy());
		if (droidRemove(psDroid, world.objects.droids))
		{
			visRemoveVisibility(psDroid, world.map);
			fpathActiveBackend().removeDroidData(psDroid->id);
			syncDebug("Tunnel in droid %u exit %u arrive %u", psDroid->id, t.exitId, t.arrive);
			transits.push_back(t);
		}
	}
	enterRequests.clear();

	// Up: units that have reached the far end
	for (size_t i = 0; i < transits.size();)
	{
		if (gameTime >= transits[i].arrive)
		{
			Transit t = transits[i];
			transits.erase(transits.begin() + static_cast<std::ptrdiff_t>(i));
			comeUp(world, t);
			continue;
		}
		++i;
	}
}

void reset(GameWorld& world)
{
	for (Transit& t : transits)
	{
		if (t.droid != nullptr)
		{
			addDroid(t.droid, world.objects.droids);
		}
	}
	transits.clear();
	enterRequests.clear();
}

std::vector<TransitView> transitsForDisplay()
{
	std::vector<TransitView> out;
	out.reserve(transits.size());
	for (const Transit& t : transits)
	{
		TransitView v;
		v.player = t.droid->player;
		v.from = t.from;
		v.to = t.to;
		const float span = static_cast<float>(std::max<uint32_t>(1, t.arrive - t.start));
		v.progress = std::max(0.f, std::min(1.f, static_cast<float>(graphicsTime - t.start) / span));
		out.push_back(v);
	}
	return out;
}

unsigned undergroundCount(unsigned player)
{
	unsigned n = 0;
	for (const Transit& t : transits)
	{
		n += t.droid->player == player ? 1 : 0;
	}
	return n;
}

} // namespace tunnels
