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
/// Warzone 2126: the lobby's Random map settings panel (landscape, sliders, map code).

#ifndef __INCLUDED_SRC_TITLEUI_WIDGETS_RANDOMMAPSETTINGS2126_H__
#define __INCLUDED_SRC_TITLEUI_WIDGETS_RANDOMMAPSETTINGS2126_H__

#include "lib/widget/widget.h"
#include <functional>
#include <memory>

/// The panel shown from the lobby's "Random map" row. The host can change everything; other
/// players see the settings and can copy the code. `onChanged` runs after the host's changes have
/// been sent (so the lobby row can update).
std::shared_ptr<WIDGET> makeRandomMapSettingsPanel2126(const std::function<void()> &onChanged);

/// Short summary for the lobby row's button and tip ("Plateaus" / "Random")
std::string randomMapSettingsSummary2126();

#endif // __INCLUDED_SRC_TITLEUI_WIDGETS_RANDOMMAPSETTINGS2126_H__
