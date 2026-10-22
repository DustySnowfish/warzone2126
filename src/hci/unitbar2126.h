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
/// Warzone 2126 interface, phase 2: the selected-units bar.
///
/// Shown above the unit groups bar while you have units selected and no build, research, design
/// or map panel is open. Top row: command buttons with their keys (the same orders and secondary
/// states as the keyboard and the classic order panel), with the current state lit when every
/// selected unit shares it. Bottom row: one card per unit type with a model, count, average health
/// and best rank; click a card to keep only that type, Shift-click to drop it, right-click to look
/// at it. With one unit selected its card shows name, weapon, rank, kills and health instead.
/// The classic order panel is hidden in this mode and opens from the bar's "More" button.

#ifndef __INCLUDED_SRC_HCI_UNITBAR2126_H__
#define __INCLUDED_SRC_HCI_UNITBAR2126_H__

#define IDHUD2126_UNITS	2126003

/// Called every frame from intRunWidgets: creates, shows or hides the bar as the selection and
/// interface mode change.
void unitBar2126Update();
/// Remove the bar (interface switched to Classic, or the in-game interface is torn down).
void unitBar2126Remove();
/// Whether the classic order panel should be visible (in 2126 mode only after "More").
bool unitBar2126ShowClassicOrders();

#endif // __INCLUDED_SRC_HCI_UNITBAR2126_H__
