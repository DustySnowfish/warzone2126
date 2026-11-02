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

/**
 * Warzone 2126 submarines (naval N9).
 *
 * A submarine (a hull with "submarine": 1) runs dived. Dived, it is hidden from other players
 * except where sonar hears it (sonar buoys, ships with a sonar turret), it passes under other
 * units, and only torpedoes and depth charges can aim at it or hurt it. Firing brings it to the
 * surface for a few seconds: then it is visible and every weapon can hit it. All of this is game
 * state (deterministic); the surfaced time is saved with the droid.
 */

#pragma once

#include <cstdint>

struct BASE_OBJECT;
struct DROID;
struct WEAPON_STATS;
struct BODY_STATS;
struct COMPONENT_STATS;
struct PROPULSION_STATS;

namespace submarine2126
{

/// How long a submarine stays surfaced after it fires, game ms
constexpr uint32_t kSurfacedMs = 8000;

bool isSubmarine(const DROID *psDroid);
bool isSubmarine(const BASE_OBJECT *psObj);

/// A submarine that is dived now (not surfaced after firing)
bool isSubmerged(const BASE_OBJECT *psObj);

/// Call when a submarine fires: it surfaces for kSurfacedMs
void surfaceAfterFiring(DROID *psDroid);

/// Whether a weapon can aim at and hurt a dived submarine (torpedoes, depth charges)
bool weaponHitsSubmerged(const WEAPON_STATS &stats);

/// Whether a turret may go on this hull and propulsion (null = not chosen yet). Submarine hulls take
/// torpedoes (and "subTurret" weapons) only; depth charges, the sonar turret and torpedoes need a
/// ship (depth charges and sonar a surface ship).
bool turretAllowed(const COMPONENT_STATS &turret, const BODY_STATS *body, const PROPULSION_STATS *propulsion);

/// The range at which this object hears ships and dived submarines (0 when it has no sonar)
int sonarRange(const BASE_OBJECT *psViewer);

/// Whether the submarine option is on for this game (skirmish and multiplayer setting; always on in
/// the campaign). Off: the submarine hull cannot be researched, built or designed.
bool allowed();

} // namespace submarine2126
