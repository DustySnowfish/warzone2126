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

/**
 * Heat shimmer: the scene bends behind fires, burning wrecks and explosions, and faintly over
 * distant hot ground on Arizona maps by day. A scene post-effect: heat sources (from effects
 * with "heat" in particles.json) draw signed screen offsets into a half-size field, then a
 * fullscreen pass samples the lit scene through it, before the transparents are drawn.
 */

#pragma once

#include "lib/ivis_opengl/gfx_api.h"

namespace heat_shimmer
{

void recordSprites(const gfx_api::RenderPassContext& passCtx);
void recordApply(const gfx_api::RenderPassContext& passCtx);

/// Strength of the faint shimmer over distant ground this frame (0 when none: not Arizona, night, rain).
float groundShimmer();

/// Whether the effect has anything to draw this frame (heat sources alive, or ground shimmer).
bool activeThisFrame();

} // namespace heat_shimmer
