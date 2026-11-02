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

#include "submarine2126.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "droid.h"
#include "multiplay.h"
#include "objmem.h"
#include "stats.h"
#include "structure.h"

#include <algorithm>

namespace submarine2126
{

bool isSubmarine(const DROID *psDroid)
{
	return psDroid != nullptr && psDroid->getBodyStats() != nullptr && psDroid->getBodyStats()->submarine2126;
}

bool isSubmarine(const BASE_OBJECT *psObj)
{
	return psObj != nullptr && psObj->type == OBJ_DROID && isSubmarine(static_cast<const DROID *>(psObj));
}

bool isSubmerged(const BASE_OBJECT *psObj)
{
	if (!isSubmarine(psObj))
	{
		return false;
	}
	const DROID *psDroid = static_cast<const DROID *>(psObj);
	return gameTime >= psDroid->surfacedUntil2126;
}

void surfaceAfterFiring(DROID *psDroid)
{
	if (isSubmarine(psDroid))
	{
		psDroid->surfacedUntil2126 = std::max(psDroid->surfacedUntil2126, gameTime + kSurfacedMs);
	}
}

bool weaponHitsSubmerged(const WEAPON_STATS &stats)
{
	return stats.shipsOnly2126 || stats.antiSub2126;
}

int sonarRange(const BASE_OBJECT *psViewer)
{
	if (psViewer == nullptr)
	{
		return 0;
	}
	if (psViewer->type == OBJ_STRUCTURE)
	{
		const STRUCTURE *psStruct = static_cast<const STRUCTURE *>(psViewer);
		return psStruct->status == SS_BUILT ? psStruct->pStructureType->sonarRange2126 : 0;
	}
	if (psViewer->type == OBJ_DROID)
	{
		const DROID *psDroid = static_cast<const DROID *>(psViewer);
		const SENSOR_STATS *psSensor = psDroid->getSensorStats();
		return psSensor != nullptr ? psSensor->sonarRange2126 : 0;
	}
	return 0;
}

bool turretAllowed(const COMPONENT_STATS &turret, const BODY_STATS *body, const PROPULSION_STATS *propulsion)
{
	const bool ship = propulsion == nullptr || propulsion->propulsionType == PROPULSION_TYPE_PROPELLOR;
	const bool sub = body != nullptr && body->submarine2126;
	if (turret.compType == COMP_WEAPON)
	{
		const WEAPON_STATS &w = static_cast<const WEAPON_STATS &>(turret);
		if (sub)
		{
			return w.torpedo2126 != 0 || w.subTurret2126;
		}
		if ((w.torpedo2126 != 0 || w.shipTurret2126) && !ship)
		{
			return false;
		}
		return true;
	}
	if (sub)
	{
		return false; // no sensors, trucks, repair or ECM on a submarine
	}
	if (turret.compType == COMP_SENSOR && static_cast<const SENSOR_STATS &>(turret).sonarRange2126 > 0)
	{
		return ship;
	}
	return true;
}

bool allowed()
{
	return game.submarines2126 != 0 || !bMultiPlayer;
}

} // namespace submarine2126
