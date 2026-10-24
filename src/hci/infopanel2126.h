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
/// Warzone 2126: details panel for a single selected unit or building, above the minimap.
///
/// Unit: name and parts, health, armour, speed, rank and kills, every weapon (damage, range,
/// rate of fire), system turrets (sensor range, build or repair rate), cargo and current order.
/// Building: name, construction progress, health, armour, modules, what it is producing or
/// researching (with progress), linked derricks, the unit it is repairing or rearming, weapons.
/// Rebuilt every frame from the live object, so it never keeps a pointer to a dead object.

#ifndef __INCLUDED_SRC_HCI_INFOPANEL2126_H__
#define __INCLUDED_SRC_HCI_INFOPANEL2126_H__

#define IDHUD2126_INFO	2126004

/// Called every frame from intRunWidgets: shows, updates or hides the panel.
void infoPanel2126Update();
/// Remove the panel (the in-game interface is torn down).
void infoPanel2126Remove();

#endif // __INCLUDED_SRC_HCI_INFOPANEL2126_H__
