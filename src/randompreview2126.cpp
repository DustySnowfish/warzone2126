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

#include "randompreview2126.h"

#include <wzmaplib/map.h>
#include <wzmaplib/map_preview.h>

#include <algorithm>
#include <cmath>

namespace
{

RandomPreview2126 current;

constexpr int kBaseReach = 13;   // tiles: wells this close to a start position belong to the base
constexpr int kCentreReach = 9;  // tiles from the middle of the map

int tileDist(const Vector2i &a, const Vector2i &b)
{
	const float dx = static_cast<float>(a.x - b.x), dy = static_cast<float>(a.y - b.y);
	return static_cast<int>(std::lround(std::sqrt(dx * dx + dy * dy)));
}

Vector2i tileOf(const WzMap::WorldPos &p)
{
	return Vector2i(static_cast<int>(p.x / TILE_UNITS), static_cast<int>(p.y / TILE_UNITS));
}

} // anonymous namespace

void randomPreview2126Build(WzMap::Map &map, const WzMap::MapPreviewImage &image)
{
	auto data = map.mapData();
	auto types = map.mapTerrainTypes();
	if (!data || !types || data->width == 0 || data->height == 0 || image.width != data->width || image.height != data->height || image.channels < 3)
	{
		randomPreview2126Clear();
		return;
	}
	const int w = static_cast<int>(data->width), h = static_cast<int>(data->height);
	RandomPreview2126 p;
	p.width = w;
	p.height = h;
	p.version = current.version + 1;
	p.rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 255);

	auto typeAt = [&](int x, int y) -> int {
		x = std::max(0, std::min(w - 1, x));
		y = std::max(0, std::min(h - 1, y));
		const unsigned t = TileNumber_tile(data->mMapTiles[static_cast<size_t>(y * w + x)].texture);
		return t < types->terrainTypes.size() ? static_cast<int>(types->terrainTypes[t]) : static_cast<int>(TER_SAND);
	};
	auto heightAt = [&](int x, int y) -> float {
		x = std::max(0, std::min(w - 1, x));
		y = std::max(0, std::min(h - 1, y));
		return static_cast<float>(data->mMapTiles[static_cast<size_t>(y * w + x)].height);
	};

	// Terrain colours from the lobby preview, lit from the top left by the slope, cliffs darker,
	// water lighter along the shore
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			const size_t i = static_cast<size_t>(y * w + x);
			const uint8_t *src = image.imageData.data() + i * image.channels;
			const int type = typeAt(x, y);
			float shade;
			if (type == TER_WATER)
			{
				bool shore = false;
				for (int dy = -1; dy <= 1 && !shore; ++dy)
				{
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (typeAt(x + dx, y + dy) != TER_WATER)
						{
							shore = true;
							break;
						}
					}
				}
				shade = shore ? 1.35f : 1.0f;
			}
			else
			{
				const float sx = heightAt(x + 1, y) - heightAt(x - 1, y);
				const float sy = heightAt(x, y + 1) - heightAt(x, y - 1);
				shade = 1.0f - (sx + sy) / 260.0f;
				shade = std::max(0.5f, std::min(1.45f, shade));
				if (type == TER_CLIFFFACE)
				{
					shade *= 0.72f;
				}
			}
			for (int c = 0; c < 3; ++c)
			{
				p.rgba[i * 4 + static_cast<size_t>(c)] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(src[c]) * shade));
			}
		}
	}

	// Start positions: the middle of each player's starting structures and units
	std::vector<Vector2i> sum(16, Vector2i(0, 0));
	std::vector<int> count(16, 0);
	auto addStart = [&](int player, const WzMap::WorldPos &pos) {
		if (player >= 0 && player < 16)
		{
			const Vector2i t = tileOf(pos);
			sum[static_cast<size_t>(player)] += t;
			++count[static_cast<size_t>(player)];
		}
	};
	if (auto structures = map.mapStructures())
	{
		for (const auto &s : *structures)
		{
			addStart(s.player, s.position);
			if (s.player < 0 && s.name == "A0BaBaFactory")
			{
				p.camps.push_back(tileOf(s.position));
			}
		}
	}
	if (auto droids = map.mapDroids())
	{
		for (const auto &d : *droids)
		{
			addStart(d.player, d.position);
		}
	}
	for (int player = 0; player < 16; ++player)
	{
		if (count[static_cast<size_t>(player)] > 0)
		{
			RandomPreview2126::Base b;
			b.player = player;
			b.tile = sum[static_cast<size_t>(player)] / count[static_cast<size_t>(player)];
			p.bases.push_back(b);
		}
	}

	// Oil wells
	std::vector<Vector2i> extractorsOfScavs;
	if (auto structures = map.mapStructures())
	{
		for (const auto &s : *structures)
		{
			if (s.player < 0 && s.name == "A0ResourceExtractor")
			{
				extractorsOfScavs.push_back(tileOf(s.position));
			}
		}
	}
	const Vector2i centre(w / 2, h / 2);
	if (auto features = map.mapFeatures())
	{
		for (const auto &f : *features)
		{
			if (f.name != "OilResource")
			{
				continue;
			}
			RandomPreview2126::Oil o;
			o.tile = tileOf(f.position);
			o.kind = RandomPreview2126::OilKind::Field;
			if (std::find(extractorsOfScavs.begin(), extractorsOfScavs.end(), o.tile) != extractorsOfScavs.end())
			{
				o.kind = RandomPreview2126::OilKind::Camp;
			}
			else if (typeAt(o.tile.x, o.tile.y) == TER_WATER)
			{
				o.kind = RandomPreview2126::OilKind::Offshore;
			}
			else if (tileDist(o.tile, centre) <= kCentreReach)
			{
				o.kind = RandomPreview2126::OilKind::Centre;
			}
			else
			{
				for (const auto &b : p.bases)
				{
					if (tileDist(o.tile, b.tile) <= kBaseReach)
					{
						o.kind = RandomPreview2126::OilKind::Base;
					}
				}
			}
			p.oil.push_back(o);
		}
	}

	// Numbers for each base
	for (auto &b : p.bases)
	{
		int nearestOther = -1;
		for (const auto &o : p.bases)
		{
			if (&o != &b)
			{
				const int d = tileDist(b.tile, o.tile);
				nearestOther = nearestOther < 0 ? d : std::min(nearestOther, d);
			}
		}
		b.neighbourTiles = std::max(0, nearestOther);
		for (const auto &c : p.camps)
		{
			const int d = tileDist(b.tile, c);
			b.campTiles = b.campTiles < 0 ? d : std::min(b.campTiles, d);
		}
	}
	for (const auto &o : p.oil)
	{
		// The base each well is nearest to
		RandomPreview2126::Base *nearest = nullptr;
		int best = 0;
		for (auto &b : p.bases)
		{
			const int d = tileDist(o.tile, b.tile);
			if (!nearest || d < best)
			{
				nearest = &b;
				best = d;
			}
		}
		if (!nearest)
		{
			continue;
		}
		switch (o.kind)
		{
		case RandomPreview2126::OilKind::Base:
			++nearest->oilAtBase;
			break;
		case RandomPreview2126::OilKind::Field:
			++nearest->oilNearest;
			break;
		case RandomPreview2126::OilKind::Offshore:
			++nearest->oilNearest;
			++nearest->offshore;
			break;
		default:
			break;
		}
	}
	current = std::move(p);
}

void randomPreview2126Clear()
{
	const uint32_t version = current.version + 1;
	current = RandomPreview2126();
	current.version = version;
}

const RandomPreview2126 &randomPreview2126()
{
	return current;
}
