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
/** @file colour_grade.h
 * Per-map colour grading.
 *
 * Each tileset (and optionally each map) has a grade file, colourgrade/<tileset>.json or
 * colourgrade/maps/<map>.json. The grade is baked on the CPU into a 32x32x32 colour cube
 * (a lookup table, LUT) that the tonemap pass applies to every pixel. A grade may also
 * name a LUT image made in an image editor. Grade files are re-read when they change,
 * so they can be tuned while the game runs.
 */

#pragma once

#include <string>

namespace gfx_api
{
struct texture;
}

namespace colour_grade
{

/// LUT cells per axis.
constexpr int kLutSize = 32;

/// The grade LUT for the current map (loaded or rebuilt when the map, the grade file or
/// the setting changes). Never null once the renderer is up: falls back to a neutral LUT.
gfx_api::texture* currentLut();

/// Strength (0..1) to blend the grade in with: the user setting times the grade's own.
float currentStrength();

/// Name of the grade in use ("none" if neutral), for logs and benchmark results.
const std::string& currentName();

/// Release GPU resources (before the graphics context shuts down).
void shutdown();

} // namespace colour_grade
