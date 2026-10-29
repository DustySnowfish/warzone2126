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

#include "autoupgrade2126.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"

#include "console.h"
#include "design.h"
#include "droid.h"
#include "game_world.h"
#include "hci.h"
#include "loop.h"
#include "multiplay.h"
#include "objmem.h"
#include "stats.h"
#include "structure.h"
#include "template.h"
#include "warzoneconfig.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <string>

namespace
{

uint32_t nextCheck = 0;
constexpr int kMaxInRun = 9; // as structure.cpp MAX_IN_RUN

bool available(int player, COMPONENT_TYPE type, size_t index)
{
	return apCompLists[player][type][index] == AVAILABLE;
}

/// Rough firepower of a weapon (base stats): damage per second, weighted by range
double weaponScore(const WEAPON_STATS& w)
{
	const auto& b = w.base;
	const double rounds = std::max<unsigned>(1, b.numRounds);
	const double cycleMs = std::max<double>(100.0, b.numRounds > 1 ? (b.firePause * (rounds - 1) + b.reloadTime) / rounds : std::max(b.firePause, b.reloadTime));
	const double dps = (b.damage + b.radiusDamage * 0.5 + b.periodicalDamage) * 1000.0 / cycleMs;
	return dps * std::sqrt(std::max(128.0, static_cast<double>(b.maxRange)));
}

/// Same family: a like-for-like replacement
bool sameFamily(const WEAPON_STATS& a, const WEAPON_STATS& b)
{
	return a.weaponSubClass == b.weaponSubClass && a.movementModel == b.movementModel
		&& (a.vtolAttackRuns > 0) == (b.vtolAttackRuns > 0)
		&& ((a.surfaceToAir & SHOOT_IN_AIR) != 0) == ((b.surfaceToAir & SHOOT_IN_AIR) != 0)
		&& ((a.surfaceToAir & SHOOT_ON_GROUND) != 0) == ((b.surfaceToAir & SHOOT_ON_GROUND) != 0)
		&& a.shipsOnly2126 == b.shipsOnly2126 && a.torpedo2126 == b.torpedo2126;
}

uint32_t betterWeapon(int player, uint32_t current)
{
	const WEAPON_STATS& cur = asWeaponStats[current];
	uint32_t best = current;
	double bestScore = weaponScore(cur) * 1.15;
	for (size_t i = 0; i < asWeaponStats.size(); ++i)
	{
		const WEAPON_STATS& w = asWeaponStats[i];
		if (i == current || !w.designable || !available(player, COMP_WEAPON, i) || !sameFamily(cur, w))
		{
			continue;
		}
		if (w.weight > cur.weight * 3 / 2 + 200)
		{
			continue; // a much heavier gun changes what the design is
		}
		const double score = weaponScore(w);
		if (score > bestScore)
		{
			best = static_cast<uint32_t>(i);
			bestScore = score;
		}
	}
	return best;
}

uint8_t betterBody(int player, uint8_t current, int weapons)
{
	const BODY_STATS& cur = asBodyStats[current];
	uint8_t best = current;
	double bestScore = (cur.base.hitpoints + cur.base.armour * 10.0) * 1.1;
	for (size_t i = 0; i < asBodyStats.size() && i < 256; ++i)
	{
		const BODY_STATS& b = asBodyStats[i];
		if (i == current || !b.designable || !available(player, COMP_BODY, i) || b.size != cur.size
			|| b.isNaval != cur.isNaval || (b.carrierCapacity2126 > 0) != (cur.carrierCapacity2126 > 0)
			|| static_cast<int>(b.weaponSlots) < weapons || b.bodyClass != cur.bodyClass)
		{
			continue;
		}
		const double score = b.base.hitpoints + b.base.armour * 10.0;
		if (score > bestScore)
		{
			best = static_cast<uint8_t>(i);
			bestScore = score;
		}
	}
	return best;
}

/// The upgraded version of a design (itself when nothing better is available)
DROID_TEMPLATE* upgradeOf(int player, DROID_TEMPLATE* tmpl)
{
	if (tmpl == nullptr || (tmpl->droidType != DROID_WEAPON && tmpl->droidType != DROID_DEFAULT) || isTransporter(tmpl))
	{
		return tmpl;
	}
	DROID_TEMPLATE candidate = *tmpl;
	bool changed = false;
	for (int w = 0; w < candidate.numWeaps; ++w)
	{
		const uint32_t better = betterWeapon(player, candidate.asWeaps[w]);
		changed = changed || better != candidate.asWeaps[w];
		candidate.asWeaps[w] = better;
	}
	const uint8_t body = betterBody(player, candidate.asParts[COMP_BODY], candidate.numWeaps);
	changed = changed || body != candidate.asParts[COMP_BODY];
	candidate.asParts[COMP_BODY] = body;
	if (!changed)
	{
		return tmpl;
	}
	// Already have this design? Use it
	for (DROID_TEMPLATE& t : localTemplates)
	{
		if (templatesHaveSameComponents(t, candidate))
		{
			return &t;
		}
	}
	candidate.multiPlayerID = generateNewObjectId();
	candidate.stored = false;
	candidate.enabled = true;
	candidate.name = WzString::fromUtf8(GetDefaultTemplateName(&candidate));
	if (!intValidTemplate(&candidate, candidate.name.toUtf8().c_str(), false, player))
	{
		return tmpl; // too heavy or otherwise not buildable: keep the old design
	}
	localTemplates.push_back(candidate);
	DROID_TEMPLATE* added = &localTemplates.back();
	copyTemplate(player, added);
	return added;
}

} // anonymous namespace

void autoUpgrade2126Update()
{
	if (!war_getAutoUpgradeDesigns() || selectedPlayer >= MAX_PLAYERS || gamePaused()
		|| (bMultiPlayer && selectedPlayer < NetPlay.players.size() && NetPlay.players[selectedPlayer].isSpectator))
	{
		return;
	}
	if (gameTime < nextCheck && nextCheck - gameTime < 5000)
	{
		return;
	}
	nextCheck = gameTime + 3000;

	for (STRUCTURE* st : gameWorld.objects.structures[selectedPlayer])
	{
		if (st->died || st->status != SS_BUILT || !st->isFactory() || st->pFunctionality == nullptr)
		{
			continue;
		}
		const FACTORY& f = st->pFunctionality->factory;
		if (f.psAssemblyPoint == nullptr || f.psAssemblyPoint->factoryType >= NUM_FACTORY_TYPES
			|| f.psAssemblyPoint->factoryInc >= asProductionRun[f.psAssemblyPoint->factoryType].size())
		{
			continue;
		}
		ProductionRun& run = asProductionRun[f.psAssemblyPoint->factoryType][f.psAssemblyPoint->factoryInc];
		for (ProductionRunEntry& entry : run)
		{
			DROID_TEMPLATE* up = upgradeOf(selectedPlayer, entry.psTemplate);
			if (up == entry.psTemplate)
			{
				continue;
			}
			// The same design may already be queued here: merge into it
			bool merged = false;
			for (ProductionRunEntry& other : run)
			{
				if (&other != &entry && other.psTemplate == up)
				{
					other.quantity = std::min(other.quantity + entry.quantity - entry.built, kMaxInRun);
					entry.quantity = entry.built; // emptied; removed below
					merged = true;
					break;
				}
			}
			addConsoleMessage(astringf(_("Design upgraded: %s is now %s"), entry.psTemplate->name.toUtf8().c_str(), up->name.toUtf8().c_str()).c_str(), LEFT_JUSTIFY, SYSTEM_MESSAGE);
			if (!merged)
			{
				entry.psTemplate = up;
			}
		}
		run.erase(std::remove_if(run.begin(), run.end(), [](const ProductionRunEntry& e) { return e.quantity <= 0; }), run.end());
	}
}
