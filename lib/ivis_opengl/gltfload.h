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
/** \file
 *  glTF 2.0 display-model import.
 *
 *  New art is authored as glTF 2.0 binary (.glb) files (see the Warzone 2126 art
 *  specification). A glTF model never replaces the gameplay ("base") model: the
 *  original PIE stays loaded for all game-state calculations, so multiplayer sync
 *  and balance are untouched. The glTF model only replaces what is drawn.
 *
 *  The override list lives in assets/model_overrides.json:
 *
 *    {
 *      "version": 1,
 *      "overrides": {
 *        "drmbod05.pie": { "model": "assets/units/bodies/cobra.glb" },
 *        "blhq.pie":     { "model": "assets/structures/hq.glb", "scale": 32 }
 *      }
 *    }
 *
 *  Keys are PIE file names (case-insensitive). "scale" is game units per glTF
 *  metre and defaults to 32 (1 tile = 128 units = 4 m).
 *
 *  Conversion conventions (matching the Warzone 2100 Blender PIE exporter):
 *    glTF (+Y up, front faces +Z) -> PIE vertex (x, y, -z) * scale, with triangle
 *    winding reversed; connectors are written as (x, -z, y) * scale.
 */

#pragma once

#include <string>
#include <vector>

#include <nonstd/optional.hpp>
using nonstd::optional;
using nonstd::nullopt;

/// Returns the glTF override model path and scale for a PIE file name, if one is configured.
struct GLTFOverride
{
	std::string modelPath;  ///< PhysFS path of the .glb
	float scale = 32.f;     ///< game units per glTF metre
};
optional<GLTFOverride> gltfFindOverride(const std::string &pieFilename);

/// Forget the cached override list (it is re-read on next use).
void gltfResetOverrides();

/// Material properties that PIE cannot carry; applied to the display model after loading.
struct GLTFExtraMaterial
{
	std::string emissiveFile;                    ///< data-root path ('/'-prefixed), empty if none
	float emissiveFactor[3] = { 0.f, 0.f, 0.f }; ///< glTF emissiveFactor x KHR_materials_emissive_strength
};

/// Converts a glTF 2.0 binary model into PIE 4 text, which the existing PIE loader then
/// turns into a display model. Returns nullopt and fills errorOut on failure.
optional<std::string> gltfConvertToPIE(const std::string &displayName, const std::vector<char> &glbData, float scale, std::string &errorOut, GLTFExtraMaterial &extraOut);
