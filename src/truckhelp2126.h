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
/// Warzone 2126: the local player's idle trucks help nearby construction by themselves.
///
/// Runs on the local client for selectedPlayer only and gives ordinary (queued, networked) help-
/// build orders, exactly as if the player had clicked, so multiplayer games stay in step.

#ifndef __INCLUDED_SRC_TRUCKHELP2126_H__
#define __INCLUDED_SRC_TRUCKHELP2126_H__

/// Call every frame from the interface update (throttled inside).
void truckHelp2126Update();

#endif // __INCLUDED_SRC_TRUCKHELP2126_H__
