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
/** @file taa.h
 * Temporal anti-aliasing: each frame the camera is shifted by a different fraction of a
 * pixel, and the resolve pass blends the new frame with the previous result (moved to
 * where it now is on screen), so edges are sampled at many positions over a few frames.
 */

#pragma once

#include "lib/framework/vector.h"
#include "lib/ivis_opengl/gfx_api.h"

#include <glm/fwd.hpp>

namespace taa
{

/// Start a frame: chooses this frame's sub-pixel offset when TAA runs, and remembers the
/// camera for reprojection next frame. Returns the projection to draw the 3D scene with
/// (unchanged when TAA is off).
glm::mat4 beginFrame(const glm::mat4& projection, const glm::mat4& view);

/// The same offset applied to another projection drawn into the scene (the sky).
glm::mat4 jitterProjection(const glm::mat4& projection);

void recordResolve(const gfx_api::RenderPassContext& passCtx);
void recordHistoryStore(const gfx_api::RenderPassContext& passCtx);
void recordSharpen(const gfx_api::RenderPassContext& passCtx);

} // namespace taa
