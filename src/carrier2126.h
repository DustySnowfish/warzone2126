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
/// Warzone 2126: aircraft carriers (naval N6).
///
/// A carrier is a ship whose hull has a carrierCapacity (6). VTOLs are assigned to a carrier by
/// right-clicking it (order DORDER_CARRIER). They fly to it, land on a free deck slot, ride with it
/// and are rearmed and repaired there at the same rates as a rearming pad. A docked VTOL takes off
/// with its next order. A VTOL that runs out of ammunition goes to its own carrier, or to the
/// nearest friendly carrier with room, or to the nearest rearming pad, whichever is closest.
/// Docked VTOLs go down with their carrier.
///
/// Game state, deterministic. Order data for DORDER_CARRIER: psObj is the carrier, pos2.x the deck
/// slot (1..capacity, 0 while flying to it), pos.x the game time the VTOL landed. The assignment
/// is kept in DROID::carrierId2126 (saved with the game).

#ifndef __INCLUDED_SRC_CARRIER2126_H__
#define __INCLUDED_SRC_CARRIER2126_H__

#include "lib/framework/frame.h"
#include "orderdef.h"

struct DROID;
struct BASE_OBJECT;

namespace carrier2126
{

/// Deck slots of a carrier (0 for any other unit)
int capacity(const BASE_OBJECT *psObj);
bool isCarrier(const BASE_OBJECT *psObj);
/// Whether this VTOL is parked on a carrier's deck
bool isDocked(const DROID *psDroid);
/// The number of VTOLs parked on this carrier
int dockedCount(const DROID *psCarrier);
/// Whether psDroid may be sent to psCarrier
bool canUse(const DROID *psDroid, const BASE_OBJECT *psCarrier);

/// orderDroidBase: start a DORDER_CARRIER
void orderStart(DROID *psDroid, const DROID_ORDER_DATA *psOrder);
/// orderUpdateDroid: run a DORDER_CARRIER. Returns false if the droid was destroyed.
bool orderUpdate(DROID *psDroid);
/// moveUpdateDroid: keeps a docked VTOL on its deck slot. Returns true if it is docked (and must not move).
bool holdOnDeck(DROID *psDroid);
/// moveToRearm: send a VTOL that needs rearming to a carrier with room (its own first, otherwise the
/// nearest) if that is closer than the nearest rearming pad. Returns true if sent.
bool returnToCarrier(DROID *psDroid, QUEUE_MODE mode = ModeImmediate);

}

#endif // __INCLUDED_SRC_CARRIER2126_H__
