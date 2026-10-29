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

#include "heat_shimmer.h"

#include "display3d.h"
#include "display3d_render_graph.h"
#include "display3d_render_internal.h"
#include "map.h"
#include "particles.h"
#include "time_of_day.h"
#include "warzoneconfig.h"
#include "weather.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/piestate.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>

namespace heat_shimmer
{

namespace
{

constexpr float kSpriteOffset = 0.012f;  // screen uv at full strength, near fires
constexpr float kGroundOffset = 0.0028f; // screen uv, over distant hot ground at noon

float shimmerTime()
{
	return static_cast<float>(graphicsTime % 600000u) / 1000.f;
}

} // namespace

float groundShimmer()
{
	if (!war_getHeatShimmer() || currentMapTileset != MAP_TILESET::ARIZONA)
	{
		return 0.f;
	}
	// Strongest around noon, none before mid-morning or after mid-afternoon
	const float phase = time_of_day::phase();
	const float day = std::max(0.f, std::sin((phase - 0.32f) / 0.38f * 3.14159265f));
	const float dry = 1.f - std::max(0.f, std::min(1.f, weather::groundState() * 4.f));
	return kGroundOffset * day * dry;
}

bool activeThisFrame()
{
	return war_getHeatShimmer() && (particles::heatActive() || groundShimmer() > 0.f);
}

void recordSprites(const gfx_api::RenderPassContext&)
{
	if (!pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	const auto& fc = pie_GetInGame3DFrameContext();
	particles::drawHeat(fc.viewMatrix, fc.perspectiveMatrix, shimmerTime(), kSpriteOffset);
}

void recordApply(const gfx_api::RenderPassContext& passCtx)
{
	ASSERT(passCtx.readCount() == 3, "HeatShimmerApply: 0 color, 1 heat offset, 2 prepass depth");
	gfx_api::abstract_texture* scene = passCtx.getRead(0);
	gfx_api::abstract_texture* offset = passCtx.getRead(1);
	gfx_api::abstract_texture* depth = passCtx.getRead(2);
	if (scene == nullptr || offset == nullptr || depth == nullptr || !pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	const auto& fc = pie_GetInGame3DFrameContext();
	gfx_api::constant_buffer_type<SHADER_HEAT_SHIMMER> constants {};
	constants.invProjectionMatrix = glm::inverse(fc.perspectiveMatrix);
	display3d_fillPassReadUvScaleClamp(passCtx, 0, constants.uvScaleClamp);
	display3d_fillPassReadUvScaleClamp(passCtx, 1, constants.offsetUvScaleClamp);
	// Distant ground shimmers: from a little beyond the view's focus towards the horizon
	const float viewDistance = std::max(1000.f, getViewDistance());
	constants.heatParams = glm::vec4(shimmerTime(), groundShimmer(), viewDistance * 1.15f, viewDistance * 2.4f);
	display3d_drawFullscreenTriangle<gfx_api::HeatShimmerPSO>(constants, scene, offset, depth);
}

} // namespace heat_shimmer
