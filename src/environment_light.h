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
/** @file environment_light.h
 * Image-based lighting: the light that PBR models receive from the sky and ground.
 *
 * Each tileset (or map) has an environment file, environment/<tileset>.json or
 * environment/maps/<map>.json, describing the sky (zenith and horizon colour), the
 * ground colour and a glow around the sun, or naming a panoramic image. It is turned
 * into a small prefiltered environment texture: blurred reflections for each roughness
 * and the diffuse light for each surface direction. Files are re-read when they change.
 */

#pragma once

#include <string>

#include "lib/framework/vector.h"
#include "lib/ivis_opengl/gfx_api.h"
#include <glm/glm.hpp>

namespace environment_light
{

/// Weather for a tileset or map (the environment file's "weather" block). Display only.
struct WeatherConfig
{
	enum class Type { None, Rain, Snow, Dust };
	Type type = Type::None;
	float clearMin = 240.f;   // seconds of clear weather between storms
	float clearMax = 600.f;
	float stormMin = 90.f;    // seconds a storm lasts
	float stormMax = 220.f;
	float strength = 1.f;     // how heavy the storms get, 0..1
	float startChance = 0.3f; // chance a map starts in a storm
};

/// The weather settings of the current environment (type None without one).
const WeatherConfig& weatherConfig();

/// Time of day: multipliers on the sky colours (zenith; horizon and ground) and the sun's colour.
void setTimeOfDay(const glm::vec3& zenithTint, const glm::vec3& horizonTint, const glm::vec3& sunColour);

/// Weather effect on the sky: more and darker cloud shadows, haze closing in (each 0..1).
void setWeatherShading(float cloudBoost, float hazeBoost);

/// Rebuild the environment if the map, the sun or the file changed, and hand it to the
/// renderer. Call once per frame before 3D models are drawn.
void update();

/// Draw the sky dome for the current environment (behind everything, before the sky band).
/// Returns false, drawing nothing, when the map has no environment.
bool drawSky(const glm::mat4& projectionMatrix, const glm::mat4& viewMatrix);

/// True when distant terrain fades into the sky (an environment with HDR on).
bool hazeActive();

/// Fill the haze part of the tonemap constants (strength 0 when inactive).
void fillHazeConstants(gfx_api::constant_buffer_type<SHADER_TONEMAP>& constants);

/// How much of the top of the classic cloud band fades into the sky dome (0 = none).
float skyBandFade();

/// Name of the environment in use ("none" if there is none), for logs and benchmarks.
const std::string& currentName();

/// Release GPU resources (before the graphics context shuts down).
void shutdown();

} // namespace environment_light
