/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
/// Warzone 2126: aircraft carriers (see carrier2126.h).

#include "carrier2126.h"

#include "lib/framework/frame.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/sync_debug.h"

#include "action.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "move.h"
#include "order.h"
#include "stats.h"
#include "structure.h"
#include "qtscript.h"

#include <algorithm>
#include <cstdint>

namespace carrier2126
{

namespace
{

constexpr int kDockRange = 200;          // world units: close enough to land on the deck
constexpr unsigned kRepathMs = 1500;     // how often a VTOL re-aims at its moving carrier
constexpr int kDeckHeight = 52;          // world units above the carrier's position (the flight deck)
// Deck slots: (across, along) in world units from the carrier's centre; along is towards the bow
constexpr int kSlots[][2] = {
	{ -30, 95 }, { 0, 95 },
	{ -30, 15 }, { 0, 15 },
	{ -30, -65 }, { 0, -65 },
	{ -30, -145 }, { 0, -145 },
};
constexpr int kMaxSlots = static_cast<int>(sizeof(kSlots) / sizeof(kSlots[0]));

DROID *carrierOf(const DROID *psDroid)
{
	if (psDroid->order.type != DORDER_CARRIER || psDroid->order.psObj == nullptr || psDroid->order.psObj->type != OBJ_DROID)
	{
		return nullptr;
	}
	return castDroid(psDroid->order.psObj);
}

/// The player's rearming pad stats (their research upgrades set the carrier's rearm and repair rates)
const STRUCTURE_STATS *padStats()
{
	for (unsigned i = 0; i < numStructureStats; ++i)
	{
		if (asStructureStats[i].type == REF_REARM_PAD)
		{
			return &asStructureStats[i];
		}
	}
	return nullptr;
}

/// A free deck slot (1-based), or 0 if the deck is full
int freeSlot(const DROID *psCarrier, const DROID *psExcept)
{
	const int cap = std::min(capacity(psCarrier), kMaxSlots);
	bool used[kMaxSlots] = {};
	for (const DROID *psCurr : gameWorld.objects.droids[psCarrier->player])
	{
		if (psCurr != psExcept && !psCurr->died && psCurr->order.type == DORDER_CARRIER && psCurr->order.psObj == psCarrier
		    && psCurr->order.pos2.x >= 1 && psCurr->order.pos2.x <= cap)
		{
			used[psCurr->order.pos2.x - 1] = true;
		}
	}
	for (int i = 0; i < cap; ++i)
	{
		if (!used[i])
		{
			return i + 1;
		}
	}
	return 0;
}

Vector3i slotPosition(const DROID *psCarrier, int slot)
{
	const int across = kSlots[slot - 1][0];
	const int along = kSlots[slot - 1][1];
	const uint16_t dir = psCarrier->rot.direction;
	// Forward is (sin, cos) of the heading (as in moveCalcDroidSpeed); across is perpendicular to it
	const int x = psCarrier->pos.x + iSinR(dir, along) + iCosR(dir, across);
	const int y = psCarrier->pos.y + iCosR(dir, along) - iSinR(dir, across);
	return Vector3i(x, y, psCarrier->pos.z + kDeckHeight);
}

void land(DROID *psDroid, const DROID *psCarrier, int slot)
{
	psDroid->order.pos2 = Vector2i(slot, 0);
	psDroid->order.pos = Vector2i(static_cast<int>(gameTime), 0);
	actionDroid(psDroid, DACTION_NONE);
	moveStopDroid(psDroid);
	psDroid->sMove.Status = MOVEINACTIVE;
	psDroid->sMove.speed = 0;
	syncDebugDroid(psDroid, 'C');
	(void)psCarrier;
}

/// Leave the carrier's order and look for a rearming pad instead (deck full or carrier lost)
void goElsewhere(DROID *psDroid)
{
	psDroid->order = DroidOrder(DORDER_NONE);
	actionDroid(psDroid, DACTION_NONE);
	if (!vtolHappy(psDroid))
	{
		STRUCTURE *psPad = findNearestReArmPad(psDroid, psDroid->psBaseStruct, false);
		if (psPad)
		{
			orderDroidObj(psDroid, DORDER_REARM, psPad, ModeImmediate);
		}
		else
		{
			orderDroid(psDroid, DORDER_RTB, ModeImmediate);
		}
	}
}

void rearmAndRepair(DROID *psDroid)
{
	if (vtolHappy(psDroid))
	{
		return;
	}
	const STRUCTURE_STATS *pad = padStats();
	if (!pad)
	{
		fillVtolDroid(psDroid);
		psDroid->body = psDroid->originalBody;
		return;
	}
	const uint64_t rate = static_cast<uint64_t>(pad->upgrade[psDroid->player].rearm);
	const uint32_t landed = static_cast<uint32_t>(psDroid->order.pos.x);
	const uint32_t now = gameTime - landed;
	const uint32_t before = (gameTime - deltaGameTime >= landed) ? gameTime - deltaGameTime - landed : 0;
	const uint64_t pointsToAdd = rate * now / GAME_TICKS_PER_SEC;
	const uint64_t pointsAlreadyAdded = rate * before / GAME_TICKS_PER_SEC;
	if (pointsToAdd >= psDroid->weight)
	{
		fillVtolDroid(psDroid);
	}
	else
	{
		for (unsigned i = 0; i < psDroid->numWeaps; i++) // one weapon at a time, as on a pad
		{
			if (psDroid->asWeaps[i].usedAmmo > 0 && psDroid->getWeaponStats(i)->upgrade[psDroid->player].numRounds > 0)
			{
				const uint64_t runs = getNumAttackRuns(psDroid, i);
				const unsigned ammoToAdd = static_cast<unsigned>(pointsToAdd * runs / psDroid->weight - pointsAlreadyAdded * runs / psDroid->weight);
				psDroid->asWeaps[i].usedAmmo -= std::min<unsigned>(ammoToAdd, psDroid->asWeaps[i].usedAmmo);
				if (ammoToAdd)
				{
					psDroid->asWeaps[i].ammo = psDroid->getWeaponStats(i)->upgrade[psDroid->player].numRounds;
					psDroid->asWeaps[i].lastFired = 0;
					break;
				}
			}
		}
	}
	if (psDroid->isDamaged())
	{
		psDroid->body = std::min<uint32_t>(psDroid->originalBody, psDroid->body + gameTimeAdjustedAverage(pad->upgrade[psDroid->player].repair));
	}
	if (vtolHappy(psDroid))
	{
		syncDebugDroid(psDroid, 'H');
		triggerEventDroidIdle(psDroid);
	}
}

} // anonymous namespace

int capacity(const BASE_OBJECT *psObj)
{
	if (psObj == nullptr || psObj->type != OBJ_DROID)
	{
		return 0;
	}
	const BODY_STATS *body = castDroid(psObj)->getBodyStats();
	return body ? static_cast<int>(body->carrierCapacity2126) : 0;
}

bool isCarrier(const BASE_OBJECT *psObj)
{
	return capacity(psObj) > 0;
}

bool isDocked(const DROID *psDroid)
{
	return psDroid->order.type == DORDER_CARRIER && psDroid->order.pos2.x > 0;
}

int dockedCount(const DROID *psCarrier)
{
	int n = 0;
	for (const DROID *psCurr : gameWorld.objects.droids[psCarrier->player])
	{
		n += (!psCurr->died && psCurr->order.type == DORDER_CARRIER && psCurr->order.psObj == psCarrier && psCurr->order.pos2.x > 0) ? 1 : 0;
	}
	return n;
}

bool canUse(const DROID *psDroid, const BASE_OBJECT *psCarrier)
{
	return psDroid->isVtol() && !psDroid->isTransporter() && isCarrier(psCarrier) && !psCarrier->died && psCarrier->player == psDroid->player;
}

void orderStart(DROID *psDroid, const DROID_ORDER_DATA *psOrder)
{
	if (!canUse(psDroid, psOrder->psObj))
	{
		return;
	}
	psDroid->order = DroidOrder(DORDER_CARRIER, psOrder->psObj);
	psDroid->order.pos2 = Vector2i(0, 0);
	psDroid->carrierId2126 = psOrder->psObj->id;
	actionDroid(psDroid, DACTION_MOVE, psOrder->psObj->pos.x, psOrder->psObj->pos.y);
	syncDebugDroid(psDroid, 'c');
}

bool orderUpdate(DROID *psDroid)
{
	DROID *psCarrier = carrierOf(psDroid);
	const bool docked = isDocked(psDroid);
	if (psCarrier == nullptr || psCarrier->died)
	{
		if (docked)
		{
			// Went down with the carrier
			syncDebugDroid(psDroid, 'X');
			destroyDroid(psDroid, gameTime, gameWorld);
			return false;
		}
		goElsewhere(psDroid);
		return true;
	}
	if (docked)
	{
		rearmAndRepair(psDroid);
		return true;
	}
	const int64_t dx = psCarrier->pos.x - psDroid->pos.x;
	const int64_t dy = psCarrier->pos.y - psDroid->pos.y;
	if (dx * dx + dy * dy <= static_cast<int64_t>(kDockRange) * kDockRange)
	{
		const int slot = freeSlot(psCarrier, psDroid);
		if (slot > 0)
		{
			land(psDroid, psCarrier, slot);
		}
		else
		{
			goElsewhere(psDroid); // deck full
		}
		return true;
	}
	// Chase the moving carrier
	if (psDroid->action != DACTION_MOVE || gameTime / kRepathMs != (gameTime - deltaGameTime) / kRepathMs)
	{
		actionDroid(psDroid, DACTION_MOVE, psCarrier->pos.x, psCarrier->pos.y);
	}
	return true;
}

bool holdOnDeck(DROID *psDroid)
{
	if (!isDocked(psDroid))
	{
		return false;
	}
	const DROID *psCarrier = carrierOf(psDroid);
	if (psCarrier == nullptr || psCarrier->died || psDroid->order.pos2.x > kMaxSlots)
	{
		return false;
	}
	const Vector3i p = slotPosition(psCarrier, psDroid->order.pos2.x);
	psDroid->pos = p;
	psDroid->rot = psCarrier->rot;
	psDroid->sMove.Status = MOVEINACTIVE;
	psDroid->sMove.speed = 0;
	return true;
}

/// VTOLs on their way to, or aboard, this carrier
static int reservedCount(const DROID *psCarrier, const DROID *psExcept)
{
	int n = 0;
	for (const DROID *psCurr : gameWorld.objects.droids[psCarrier->player])
	{
		n += (psCurr != psExcept && !psCurr->died && psCurr->order.type == DORDER_CARRIER && psCurr->order.psObj == psCarrier) ? 1 : 0;
	}
	return n;
}

bool returnToCarrier(DROID *psDroid, QUEUE_MODE mode)
{
	if (!psDroid->isVtol() || psDroid->isTransporter())
	{
		return false;
	}
	if (psDroid->order.type == DORDER_CARRIER)
	{
		return true; // already on its way (or aboard)
	}
	// Its own carrier if it has one with room, otherwise the nearest carrier with room
	DROID *psCarrier = nullptr;
	int64_t best = INT64_MAX;
	for (DROID *psCurr : gameWorld.objects.droids[psDroid->player])
	{
		if (!canUse(psDroid, psCurr) || reservedCount(psCurr, psDroid) >= capacity(psCurr))
		{
			continue;
		}
		const int64_t dx = psCurr->pos.x - psDroid->pos.x, dy = psCurr->pos.y - psDroid->pos.y;
		const int64_t d = (psCurr->id == psDroid->carrierId2126) ? -1 : dx * dx + dy * dy;
		if (d < best)
		{
			best = d;
			psCarrier = psCurr;
		}
	}
	if (psCarrier == nullptr)
	{
		return false;
	}
	// Whichever is nearer: the carrier or a rearming pad
	const STRUCTURE *psPad = findNearestReArmPad(psDroid, psDroid->psBaseStruct, false);
	if (psPad)
	{
		const int64_t cx = psCarrier->pos.x - psDroid->pos.x, cy = psCarrier->pos.y - psDroid->pos.y;
		const int64_t px = psPad->pos.x - psDroid->pos.x, py = psPad->pos.y - psDroid->pos.y;
		if (px * px + py * py < cx * cx + cy * cy)
		{
			return false;
		}
	}
	orderDroidObj(psDroid, DORDER_CARRIER, psCarrier, mode);
	return true;
}

} // namespace carrier2126
