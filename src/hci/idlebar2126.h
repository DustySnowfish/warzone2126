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
/// Warzone 2126: idle indicators beside the minimap. Counts of idle trucks, idle factories (all
/// kinds, Shipyards included) and idle research facilities; clicking one (or its key) selects the
/// next idle one and centres the view on it (factories and labs open their menu); right-clicking
/// idle trucks selects them all.

#ifndef __INCLUDED_SRC_HCI_IDLEBAR2126_H__
#define __INCLUDED_SRC_HCI_IDLEBAR2126_H__

#define IDHUD2126_IDLE	2126005

enum class IdleKind2126 { Trucks, Factories, Labs };

/// Create, refresh or hide the bar (call every frame from the interface update).
void idleBar2126Update();
void idleBar2126Remove();

/// Select the next idle object of a kind and centre the view on it (also used by the hotkeys).
void idleBar2126SelectNext(IdleKind2126 kind);
/// Select all idle trucks (without moving the view).
void idleBar2126SelectAllTrucks();

#endif // __INCLUDED_SRC_HCI_IDLEBAR2126_H__
