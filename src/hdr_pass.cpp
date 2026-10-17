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
/** @file hdr_pass.cpp
 * HDR bloom chain and tonemap passes.
 *
 * The scene is rendered into a half-float target (values may exceed 1.0). The bright
 * parts are extracted at half resolution, downsampled to 1/16, then upsampled back,
 * each level adding the next coarser one (a wide, smooth glow). The tonemap pass adds
 * the bloom to the scene and rolls highlights off smoothly into the displayable range.
 */

#include "hdr_pass.h"

#include "display3d_render_graph.h"
#include "display3d_render_internal.h"

#include "lib/framework/frame.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/piestate.h"

namespace hdr_pass
{

namespace
{

// Linear brightness above which light starts to bloom (1.0 = brighter than "white")
constexpr float kBloomThreshold = 1.0f;
constexpr float kBloomKnee = 0.5f;
// Tonemap: unchanged below this level, then a smooth roll-off to 1.0
constexpr float kShoulderKnee = 0.9f;
constexpr float kExposure = 1.0f;
// Bloom levels summed by the upsample chain (normalised in the tonemap pass)
constexpr float kBloomLevels = 4.0f;
constexpr float kUpsampleRadius = 1.0f;

} // anonymous namespace

void recordBloomPrefilter(const gfx_api::RenderPassContext& passCtx)
{
	ASSERT(passCtx.readCount() == 1, "BloomPrefilter: 0 scene");
	gfx_api::abstract_texture* scene = passCtx.getRead(0);
	if (scene == nullptr || !pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	gfx_api::constant_buffer_type<SHADER_BLOOM_PREFILTER> constants {};
	display3d_fillPassReadUvScaleClamp(passCtx, 0, constants.uvScaleClamp);
	constants.params = glm::vec4(kBloomThreshold, kBloomKnee, 0.f, 0.f);
	display3d_drawFullscreenTriangle<gfx_api::BloomPrefilterPSO>(constants, scene);
}

void recordBloomDownsample(const gfx_api::RenderPassContext& passCtx)
{
	ASSERT(passCtx.readCount() == 1, "BloomDown: 0 source");
	gfx_api::abstract_texture* source = passCtx.getRead(0);
	if (source == nullptr || !pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	gfx_api::constant_buffer_type<SHADER_BLOOM_DOWNSAMPLE> constants {};
	display3d_drawFullscreenTriangle<gfx_api::BloomDownsamplePSO>(constants, source);
}

void recordBloomUpsample(const gfx_api::RenderPassContext& passCtx)
{
	ASSERT(passCtx.readCount() == 2, "BloomUp: 0 coarser, 1 same level");
	gfx_api::abstract_texture* coarser = passCtx.getRead(0);
	gfx_api::abstract_texture* level = passCtx.getRead(1);
	if (coarser == nullptr || level == nullptr || !pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	gfx_api::constant_buffer_type<SHADER_BLOOM_UPSAMPLE> constants {};
	constants.params = glm::vec4(kUpsampleRadius, 0.f, 0.f, 0.f);
	display3d_drawFullscreenTriangle<gfx_api::BloomUpsamplePSO>(constants, coarser, level);
}

void recordTonemap(const gfx_api::RenderPassContext& passCtx)
{
	ASSERT(passCtx.readCount() == 2, "Tonemap: 0 scene, 1 bloom");
	gfx_api::abstract_texture* scene = passCtx.getRead(0);
	gfx_api::abstract_texture* bloom = passCtx.getRead(1);
	if (scene == nullptr || bloom == nullptr || !pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	gfx_api::constant_buffer_type<SHADER_TONEMAP> constants {};
	display3d_fillPassReadUvScaleClamp(passCtx, 0, constants.uvScaleClamp);
	constants.params = glm::vec4(gfx_api::context::get().bloomIntensity(), kExposure, kShoulderKnee, kBloomLevels);
	display3d_drawFullscreenTriangle<gfx_api::TonemapPSO>(constants, scene, bloom);
}

} // namespace hdr_pass
