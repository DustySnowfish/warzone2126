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
/** \file
 *  Warzone 2126 jump jets (design/walkers.md): biped walkers hop up to a few tiles, over walls,
 *  buildings, units and cliffs, onto any tile they could stand on; then a cooldown.
 *
 *  Game state: the jump runs in the synchronised game logic with integer maths, through the order
 *  DORDER_JUMP (networked like any order), and is saved with the game.
 */

#pragma once

#include "lib/framework/vector.h"

#include <cstdint>

struct DROID;

namespace jumpjets2126
{

/// The droid has jump jets (its propulsion has a jump range).
bool hasJumpJets(const DROID* psDroid);

/// Jump jets ready: has them, not in the air, cooldown over.
bool ready(const DROID* psDroid);

/// In the air.
bool jumping(const DROID* psDroid);

/// Milliseconds of cooldown left (0 when ready or without jump jets).
uint32_t cooldownLeft(const DROID* psDroid);

/// Where a jump towards target (world coordinates) would land; false if nowhere to land.
bool landingSpot(const DROID* psDroid, Vector2i target, Vector2i& landing);

/// DORDER_JUMP: take off towards target (world coordinates). False if the jets are not ready or
/// there is nowhere to land.
bool start(DROID* psDroid, Vector2i target);

/// Called first in moveUpdateDroid: moves a droid in the air along its arc and lands it. Returns
/// true while the droid is in the air (normal movement is skipped).
bool update(DROID* psDroid);

/// Height of the arc above the straight line between take-off and landing, for display (game time).
int arcHeight(const DROID* psDroid, uint32_t time);

// UI: the J key arms a jump for the selected units; the next left click on the map is the target.
void setTargeting(bool on);
bool targeting();
/// The selected units with jump jets jump towards (x, y) (world coordinates). True if any did.
bool orderSelected(unsigned player, int x, int y);
/// Any selected unit of this player has jump jets ready.
bool anySelectedReady(unsigned player);

}
