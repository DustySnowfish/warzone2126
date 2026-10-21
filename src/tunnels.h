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
/** @file tunnels.h
 * Tunnel network (prototype). Trucks build Tunnel Entrances; all of a player's entrances are
 * joined underground. Ordering ground units to one of your entrances sends them to the entrance
 * nearest to them, down into the network, and out at the one you clicked. Underground they
 * cannot be seen, shot or hit by artillery. Travel takes time by distance.
 *
 * If the exit is destroyed while units are on their way, they come up at the nearest other
 * entrance; with none left, they come up where they went in.
 *
 * Game state: entering, travelling and leaving run in the game logic with integer maths, so all
 * players stay in sync. Units underground are taken off the map like units in a transporter.
 */

#pragma once

#include "lib/framework/vector.h"
#include "orderdef.h"

#include <cstdint>
#include <vector>

struct DROID;
struct STRUCTURE;
struct GameWorld;

namespace tunnels
{

/// Stats id of the Tunnel Entrance structure.
constexpr const char* kEntranceId = "A0TunnelEntrance";

/// Whether this structure is a tunnel entrance.
bool isEntrance(const STRUCTURE* psStruct);

/// Whether this droid can use tunnels (ground units only).
bool canUse(const DROID* psDroid);

/// Start a DORDER_TUNNEL order: psOrder->psObj is the exit entrance.
void orderStart(DROID* psDroid, DROID_ORDER_DATA* psOrder);

/// Advance a DORDER_TUNNEL order (from orderUpdateDroid). Returns true when the order is over.
bool orderUpdate(DROID* psDroid);

/// Move units in and out of the network (call once per game tick, after the droid updates).
void update(GameWorld& world);

/// Put every unit still underground back on the map (before the droid lists are freed).
void reset(GameWorld& world);

/// For display: one unit travelling underground.
struct TransitView
{
	unsigned player;
	Vector2i from;      // world position of the entrance it went in at
	Vector2i to;        // world position of the exit it is heading for
	float progress;     // 0..1
};

/// Units underground now (for drawing their blips).
std::vector<TransitView> transitsForDisplay();

/// How many of this player's units are underground.
unsigned undergroundCount(unsigned player);

} // namespace tunnels
