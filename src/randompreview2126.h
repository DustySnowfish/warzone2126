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
 * The large preview in the Random map settings panel: the generated map shaded by height, with
 * water, cliffs, start positions, oil wells (by base, out in the field, in the centre, offshore)
 * and scavenger camps, plus a few numbers per base for the hover card.
 *
 * Built from the same map data the lobby preview loads (so the same seed and settings), and only
 * for the generated Random maps.
 */

#pragma once

#include "lib/framework/vector.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace WzMap
{
class Map;
struct MapPreviewImage;
}

struct RandomPreview2126
{
	enum class OilKind
	{
		Base,     ///< within reach of a start position
		Field,    ///< out in the open
		Centre,   ///< the contested field in the middle
		Offshore, ///< in open water (rigs)
		Camp,     ///< held by a scavenger camp
	};
	struct Oil
	{
		Vector2i tile;
		OilKind kind;
	};
	struct Base
	{
		int player = 0;
		Vector2i tile;
		int oilAtBase = 0;      ///< wells within reach of the base
		int oilNearest = 0;     ///< field and offshore wells nearer this base than any other
		int offshore = 0;       ///< of those, offshore
		int neighbourTiles = 0; ///< straight-line distance to the nearest other base
		int campTiles = -1;     ///< to the nearest scavenger camp, -1 for none
	};

	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba; ///< width * height, shaded
	std::vector<Base> bases;
	std::vector<Oil> oil;
	std::vector<Vector2i> camps;
	uint32_t version = 0; ///< changes with every build
};

/// Build the preview from a loaded map and the lobby's 2D preview image (its terrain colours)
void randomPreview2126Build(WzMap::Map &map, const WzMap::MapPreviewImage &image);

/// Not a generated map: no preview
void randomPreview2126Clear();

/// The current preview (empty width when there is none)
const RandomPreview2126 &randomPreview2126();
