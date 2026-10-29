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
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "truckhelp2126.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"

#include "droid.h"
#include "game_world.h"
#include "group.h"
#include "loop.h"
#include "map.h"
#include "multiplay.h"
#include "order.h"
#include "structure.h"
#include "warzoneconfig.h"

#include <unordered_map>

namespace
{

constexpr uint32_t kIdleBeforeHelping = 3000; // ms of game time a truck must stand idle first
constexpr int kHelpRange = 8 * TILE_UNITS;

std::unordered_map<uint32_t, uint32_t> idleSince; // truck id -> game time it was first seen idle
uint32_t nextCheck = 0;

bool idleTruck(const DROID* d)
{
	return !d->died && (d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT)
		&& d->order.type == DORDER_NONE && d->action == DACTION_NONE && !d->isTransporter()
		&& (d->psGroup == nullptr || d->psGroup->type != GT_TRANSPORTER)
		&& secondaryGetState(d, DSO_HALTTYPE) != DSS_HALT_HOLD;
}

} // anonymous namespace

void truckHelp2126Update()
{
	if (!war_getTruckAutoHelp() || selectedPlayer >= MAX_PLAYERS || gamePaused()
		|| (bMultiPlayer && selectedPlayer < NetPlay.players.size() && NetPlay.players[selectedPlayer].isSpectator))
	{
		return;
	}
	if (gameTime < nextCheck && nextCheck - gameTime < 2000)
	{
		return;
	}
	nextCheck = gameTime + 1000;

	// Buildings of ours waiting for work
	std::vector<STRUCTURE*> sites;
	for (STRUCTURE* st : gameWorld.objects.structures[selectedPlayer])
	{
		if (!st->died && st->status == SS_BEING_BUILT)
		{
			sites.push_back(st);
		}
	}
	std::unordered_map<uint32_t, uint32_t> stillIdle;
	for (DROID* d : gameWorld.objects.droids[selectedPlayer])
	{
		if (!idleTruck(d))
		{
			continue;
		}
		const auto it = idleSince.find(d->id);
		const uint32_t since = it != idleSince.end() ? it->second : gameTime;
		stillIdle[d->id] = since;
		if (sites.empty() || gameTime - since < kIdleBeforeHelping)
		{
			continue;
		}
		STRUCTURE* best = nullptr;
		int64_t bestDist = static_cast<int64_t>(kHelpRange) * kHelpRange;
		for (STRUCTURE* st : sites)
		{
			const int64_t dx = st->pos.x - d->pos.x;
			const int64_t dy = st->pos.y - d->pos.y;
			const int64_t dist = dx * dx + dy * dy;
			if (dist <= bestDist)
			{
				best = st;
				bestDist = dist;
			}
		}
		if (best != nullptr)
		{
			orderDroidObj(d, DORDER_HELPBUILD, best, ModeQueue);
			stillIdle.erase(d->id);
		}
	}
	idleSince.swap(stillIdle);
}
