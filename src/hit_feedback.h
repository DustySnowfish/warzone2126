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
/** @file hit_feedback.h
 * Hit feedback: a short flash on units and structures when they lose a noticeable amount of
 * health, and a "recent damage" trail on health bars that drains after a moment.
 *
 * Display only: it watches body points from the renderer and never touches game state.
 * Continuous small damage (burning) does not flash; single hits do, harder for bigger hits.
 */

#pragma once

#include <cstdint>

struct BASE_OBJECT;

namespace hit_feedback
{

/// Note the object's current health (call when it is drawn or its health bar is drawn).
void observe(const BASE_OBJECT* psObj, uint32_t body, uint32_t maxBody);

/// Flash strength for the object right now, 0 to 1.
float flash(const BASE_OBJECT* psObj);

/// Health fraction (0 to 1) the recent-damage trail reaches, at least the current health.
float trailFraction(const BASE_OBJECT* psObj, float currentFraction);

/// Forget everything (level change).
void clear();

/// Sets the model hit flash for one object while in scope (drawing code).
class ScopedFlash
{
public:
	explicit ScopedFlash(const BASE_OBJECT* psObj, uint32_t body, uint32_t maxBody);
	~ScopedFlash();
	ScopedFlash(const ScopedFlash&) = delete;
	ScopedFlash& operator=(const ScopedFlash&) = delete;
private:
	bool active = false;
};

} // namespace hit_feedback
