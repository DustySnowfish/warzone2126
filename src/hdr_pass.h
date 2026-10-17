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
/** @file hdr_pass.h
 * HDR bloom chain and tonemap passes.
 */

#pragma once

#include "lib/ivis_opengl/gfx_api.h"

namespace hdr_pass
{

void recordBloomPrefilter(const gfx_api::RenderPassContext& passCtx);
void recordBloomDownsample(const gfx_api::RenderPassContext& passCtx);
void recordBloomUpsample(const gfx_api::RenderPassContext& passCtx);
void recordTonemap(const gfx_api::RenderPassContext& passCtx);

} // namespace hdr_pass
