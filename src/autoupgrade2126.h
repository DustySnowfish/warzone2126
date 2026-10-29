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
/// Warzone 2126: designs queued in the local player's factories upgrade themselves when better
/// parts are researched.
///
/// For each design waiting in a production queue: the best available weapon of the same family
/// (same weapon class, direct or indirect fire, same ground/air/VTOL/ship role) if it is at least
/// 15% stronger and not much heavier, and the best available body of the same size and kind if it
/// is at least 10% tougher and has the mounts the design needs. The upgraded design must pass
/// the usual design checks; it is added to the player's designs with an automatic name and the
/// queue switches to it (the unit already being built finishes as it was). Production queues are
/// local and each item is sent to the other players when it starts, as for a player's own
/// changes, so multiplayer games stay in step.

#ifndef __INCLUDED_SRC_AUTOUPGRADE2126_H__
#define __INCLUDED_SRC_AUTOUPGRADE2126_H__

/// Call every frame from the interface update (throttled inside).
void autoUpgrade2126Update();

#endif // __INCLUDED_SRC_AUTOUPGRADE2126_H__
