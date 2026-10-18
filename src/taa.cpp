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
/** @file taa.cpp
 * Temporal anti-aliasing (see taa.h).
 */

#include "taa.h"

#include "display3d_render_graph.h"
#include "display3d_render_internal.h"

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/piestate.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace taa
{

namespace
{

// History weight: higher is smoother but slower to react
constexpr float kHistoryWeight = 0.9f;
// Colour box around the current neighbourhood, in standard deviations
constexpr float kColourBoxSize = 1.25f;
// Output sharpening (0..1), to restore the texture detail that blending jittered frames softens
constexpr float kSharpenStrength = 0.35f;
// Sub-pixel offsets cycle through this many points of the Halton (2, 3) sequence
constexpr uint32_t kJitterPhases = 8;

struct State
{
	bool active = false;            // TAA runs this frame
	uint64_t frame = 0;
	glm::vec2 jitterNdc { 0.f, 0.f };
	glm::mat4 currentViewProj { 1.f };         // unjittered
	glm::mat4 previousViewProj { 1.f };        // unjittered, last frame
	bool havePrevious = false;
	uint64_t historyWrittenFrame = 0;          // frame whose resolve was stored as history
	std::pair<uint32_t, uint32_t> historySize { 0, 0 };
};

State s;

float halton(uint32_t index, uint32_t base)
{
	float f = 1.f;
	float r = 0.f;
	while (index > 0)
	{
		f /= static_cast<float>(base);
		r += f * static_cast<float>(index % base);
		index /= base;
	}
	return r;
}

bool taaSurfacesPresent()
{
	return gfx_api::context::get().getPipelineSurface(gfx_api::PipelineSurfaceId::TaaColor) != nullptr;
}

} // anonymous namespace

glm::mat4 beginFrame(const glm::mat4& projection, const glm::mat4& view)
{
	++s.frame;
	s.previousViewProj = s.currentViewProj;
	s.havePrevious = s.active;
	s.currentViewProj = projection * view;
	s.active = taaSurfacesPresent();
	if (!s.active)
	{
		s.jitterNdc = glm::vec2(0.f);
		return projection;
	}
	auto extent = gfx_api::context::get().usedPipelineSurfaceExtent(gfx_api::PipelineSurfaceId::TaaColor);
	const float width = static_cast<float>(std::max<uint32_t>(extent.first, 1));
	const float height = static_cast<float>(std::max<uint32_t>(extent.second, 1));
	const uint32_t phase = static_cast<uint32_t>(s.frame % kJitterPhases) + 1;
	// Offset in pixels within [-0.5, 0.5], converted to clip space (2 units per screen)
	s.jitterNdc = glm::vec2((halton(phase, 2) - 0.5f) * 2.f / width, (halton(phase, 3) - 0.5f) * 2.f / height);
	return jitterProjection(projection);
}

glm::mat4 jitterProjection(const glm::mat4& projection)
{
	if (!s.active)
	{
		return projection;
	}
	return glm::translate(glm::mat4(1.f), glm::vec3(s.jitterNdc.x, s.jitterNdc.y, 0.f)) * projection;
}

void recordResolve(const gfx_api::RenderPassContext& passCtx)
{
	ASSERT(passCtx.readCount() == 3, "TaaResolve: 0 current, 1 depth, 2 history");
	gfx_api::abstract_texture* current = passCtx.getRead(0);
	gfx_api::abstract_texture* depth = passCtx.getRead(1);
	gfx_api::abstract_texture* history = passCtx.getRead(2);
	if (current == nullptr || depth == nullptr || history == nullptr || !pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	auto& ctx = gfx_api::context::get();
	const auto size = ctx.getPipelineSurfaceDimensions(gfx_api::PipelineSurfaceId::TaaColor);
	const std::pair<uint32_t, uint32_t> dims = size.has_value() ? size.value() : std::pair<uint32_t, uint32_t>(1, 1);
	// History is usable when last frame stored it, at the same size, and the camera is known
	const bool historyValid = s.havePrevious && s.historyWrittenFrame + 1 == s.frame && s.historySize == dims;

	gfx_api::constant_buffer_type<SHADER_TAA_RESOLVE> constants {};
	// Un-project with the unshifted camera, so a still camera maps every pixel exactly onto
	// itself in the history (the shift only changes what the current frame sampled)
	constants.currentInvViewProj = glm::inverse(s.currentViewProj);
	constants.previousViewProj = s.previousViewProj;
	display3d_fillPassReadUvScaleClamp(passCtx, 0, constants.uvScaleClamp);
	constants.texelSize = glm::vec4(1.f / static_cast<float>(std::max<uint32_t>(dims.first, 1)), 1.f / static_cast<float>(std::max<uint32_t>(dims.second, 1)), 0.f, 0.f);
	constants.params = glm::vec4(kHistoryWeight, historyValid ? 1.f : 0.f, kColourBoxSize, 0.f);
	display3d_drawFullscreenTriangle<gfx_api::TaaResolvePSO>(constants, current, depth, history);
}

void recordHistoryStore(const gfx_api::RenderPassContext& passCtx)
{
	ASSERT(passCtx.readCount() == 1, "TaaHistoryStore: 0 resolved");
	gfx_api::abstract_texture* resolved = passCtx.getRead(0);
	if (resolved == nullptr || !pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	gfx_api::constant_buffer_type<SHADER_TAA_COPY> constants {};
	display3d_fillPassReadUvScaleClamp(passCtx, 0, constants.uvScaleClamp);
	display3d_drawFullscreenTriangle<gfx_api::TaaCopyPSO>(constants, resolved);
	s.historyWrittenFrame = s.frame;
	const auto size = gfx_api::context::get().getPipelineSurfaceDimensions(gfx_api::PipelineSurfaceId::TaaColor);
	s.historySize = size.has_value() ? size.value() : std::pair<uint32_t, uint32_t>(0, 0);
}

void recordSharpen(const gfx_api::RenderPassContext& passCtx)
{
	ASSERT(passCtx.readCount() == 1, "TaaSharpen: 0 resolved");
	gfx_api::abstract_texture* resolved = passCtx.getRead(0);
	if (resolved == nullptr || !pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	const auto size = gfx_api::context::get().getPipelineSurfaceDimensions(gfx_api::PipelineSurfaceId::TaaColor);
	const std::pair<uint32_t, uint32_t> dims = size.has_value() ? size.value() : std::pair<uint32_t, uint32_t>(1, 1);
	gfx_api::constant_buffer_type<SHADER_TAA_SHARPEN> constants {};
	display3d_fillPassReadUvScaleClamp(passCtx, 0, constants.uvScaleClamp);
	constants.texelSize = glm::vec4(1.f / static_cast<float>(std::max<uint32_t>(dims.first, 1)), 1.f / static_cast<float>(std::max<uint32_t>(dims.second, 1)), 0.f, 0.f);
	constants.params = glm::vec4(kSharpenStrength, 0.f, 0.f, 0.f);
	display3d_drawFullscreenTriangle<gfx_api::TaaSharpenPSO>(constants, resolved);
}

} // namespace taa
