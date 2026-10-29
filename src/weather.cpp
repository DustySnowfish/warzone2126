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
/** @file weather.cpp
 * Weather director and particles (see weather.h). Display only.
 */

#include "weather.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/piedef.h"

#include "atmos.h"
#include "display3d.h"
#include "environment_light.h"
#include "game_world.h"
#include "loop.h"
#include "map.h"
#include "particles.h"
#include "ambient2126.h"
#include "warzoneconfig.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace weather
{

namespace
{

using Type = environment_light::WeatherConfig::Type;

constexpr float kRampSeconds = 20.f;     // a storm builds up and clears over this long
constexpr float kWetUpSeconds = 25.f;    // ground gets fully wet in this much heavy rain
constexpr float kDryDownSeconds = 90.f;  // and dries out over this long
constexpr float kSnowUpSeconds = 120.f;  // snow settles over this long
constexpr float kMeltSeconds = 240.f;

struct State
{
	bool started = false;
	Type type = Type::None;
	float level = 0.f;          // current strength, 0..1
	float target = 0.f;         // strength being ramped towards
	float phaseLeft = 0.f;      // seconds until the director's next change
	float wet = 0.f;            // ground wetness 0..1
	float snow = 0.f;           // ground snow cover 0..1
	float carry = 0.f;          // fractional particles carried to the next frame
	float splashCarry = 0.f;
	float puddleCarry = 0.f;
	float rippleCarry = 0.f;
	uint32_t lastTime = 0;
	// Lightning (render-only)
	float nextStrike = 6.f;     // seconds until the next strike
	float boltAge = -1.f;       // seconds since the current strike (< 0: none)
	bool boltVisible = false;   // the bolt is in view (otherwise only the sky flashes, dimmer)
	std::vector<glm::vec3> bolt;          // main channel, sky to ground (world)
	std::vector<std::vector<glm::vec3>> forks;
	float flash = 0.f;          // how lit the scene is by the flash, 0..1
	glm::vec3 tintDiffuse { 1.f };
	glm::vec3 tintSpecular { 1.f };
	glm::vec3 tintAmbient { 1.f };
};

State s;
std::minstd_rand rng(0x2126u);

float rand01()
{
	return std::uniform_real_distribution<float>(0.f, 1.f)(rng);
}

float randRange(float a, float b)
{
	return a + (b - a) * rand01();
}

/// Weather a campaign script asked for, if any
Type scriptedType()
{
	switch (atmosGetWeatherType())
	{
	case WT_RAINING: return Type::Rain;
	case WT_SNOWING: return Type::Snow;
	default: return Type::None;
	}
}

void runDirector(const environment_light::WeatherConfig& cfg, float dt)
{
	const Type scripted = scriptedType();
	if (scripted != Type::None)
	{
		s.type = scripted;
		s.target = 1.f;
		return;
	}
	if (cfg.type == Type::None)
	{
		s.target = 0.f;
		if (s.level <= 0.f)
		{
			s.type = Type::None;
		}
		return;
	}
	if (s.type != cfg.type && s.level <= 0.f)
	{
		s.type = cfg.type;
	}
	if (!s.started)
	{
		s.started = true;
		const bool storm = rand01() < cfg.startChance;
		s.target = storm ? cfg.strength * randRange(0.6f, 1.f) : 0.f;
		s.level = s.target; // a map that starts in a storm starts in it, not ramping up
		s.phaseLeft = storm ? randRange(cfg.stormMin, cfg.stormMax) : randRange(cfg.clearMin, cfg.clearMax) * 0.5f;
		return;
	}
	s.phaseLeft -= dt;
	if (s.phaseLeft <= 0.f)
	{
		if (s.target > 0.f)
		{
			s.target = 0.f;
			s.phaseLeft = randRange(cfg.clearMin, cfg.clearMax);
		}
		else
		{
			s.target = cfg.strength * randRange(0.55f, 1.f);
			s.phaseLeft = randRange(cfg.stormMin, cfg.stormMax);
		}
	}
}

/// Spawn `count` particles of `effect` at random spots around the view, `height` above the ground
void emitAround(const char* effect, float count, float& carry, float radius, float heightLo, float heightHi, const glm::vec3& direction, float scale)
{
	carry += count;
	const int n = static_cast<int>(carry);
	carry -= static_cast<float>(n);
	const float cx = static_cast<float>(playerPos.p.x);
	const float cy = static_cast<float>(playerPos.p.z);
	const float mapW = static_cast<float>(world_coord(gameWorld.map.width) - 1);
	const float mapH = static_cast<float>(world_coord(gameWorld.map.height) - 1);
	for (int i = 0; i < n; ++i)
	{
		const float x = std::max(0.f, std::min(mapW, cx + randRange(-radius, radius)));
		const float y = std::max(0.f, std::min(mapH, cy + randRange(-radius, radius)));
		const float ground = static_cast<float>(map_Height(gameWorld.map, static_cast<int>(x), static_cast<int>(y)));
		particles::spawn(effect, Vector3f(x, y, ground + randRange(heightLo, heightHi)), Vector3f(direction.x, direction.y, direction.z), scale);
	}
}

// ---------------------------------------------------------------------------------------
// Rain on puddles and on water

/// Fixed per tile: whether rain collects there in a puddle (about a quarter of the tiles)
bool puddleTile(int tx, int ty)
{
	uint32_t h = static_cast<uint32_t>(tx) * 73856093u ^ static_cast<uint32_t>(ty) * 19349663u ^ 0x2126u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return (h & 3u) == 0u;
}

void emitRainRipples(float dt, float radius, float detail)
{
	const float cx = static_cast<float>(playerPos.p.x);
	const float cy = static_cast<float>(playerPos.p.z);
	const int w = gameWorld.map.width;
	const int h = gameWorld.map.height;
	auto randomTile = [&](int& tx, int& ty, float& x, float& y) {
		x = cx + randRange(-radius, radius);
		y = cy + randRange(-radius, radius);
		tx = map_coord(static_cast<int>(x));
		ty = map_coord(static_cast<int>(y));
		return tx >= 0 && ty >= 0 && tx < w && ty < h;
	};
	// Puddles on soaked ground: wider, slower rings and droplets jumping up
	s.puddleCarry += dt * 260.f * s.level * std::max(0.f, s.wet - 0.3f) * detail;
	for (int n = 0; s.puddleCarry >= 1.f && n < 40; ++n)
	{
		s.puddleCarry -= 1.f;
		for (int attempt = 0; attempt < 6; ++attempt)
		{
			int tx, ty;
			float x, y;
			if (!randomTile(tx, ty, x, y))
			{
				continue;
			}
			const MAPTILE* tile = mapTile(gameWorld.map, tx, ty);
			if (terrainType(tile) == TER_WATER || terrainType(tile) == TER_CLIFFFACE || !puddleTile(tx, ty))
			{
				continue;
			}
			const float ground = static_cast<float>(map_Height(gameWorld.map, static_cast<int>(x), static_cast<int>(y)));
			particles::spawn("rain_puddle", Vector3f(x, y, ground + 2.f), Vector3f(0.f, 0.f, 1.f), 1.f);
			break;
		}
	}
	s.puddleCarry = std::min(s.puddleCarry, 1.f);
	// Rings spreading on lakes and the sea
	s.rippleCarry += dt * 220.f * s.level * detail;
	for (int n = 0; s.rippleCarry >= 1.f && n < 40; ++n)
	{
		s.rippleCarry -= 1.f;
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			int tx, ty;
			float x, y;
			if (!randomTile(tx, ty, x, y) || terrainType(mapTile(gameWorld.map, tx, ty)) != TER_WATER)
			{
				continue;
			}
			const float surface = static_cast<float>(std::max(map_Height(gameWorld.map, static_cast<int>(x), static_cast<int>(y)), map_TileHeightSurface(gameWorld.map, tx, ty)));
			particles::spawn("rain_ripple_water", Vector3f(x, y, surface + 2.f), Vector3f(0.f, 0.f, 1.f), 1.f);
			break;
		}
	}
	s.rippleCarry = std::min(s.rippleCarry, 1.f);
}

// ---------------------------------------------------------------------------------------
// Lightning

/// A jagged channel from `top` down to `bottom` in `segments` steps
std::vector<glm::vec3> jaggedChannel(const glm::vec3& top, const glm::vec3& bottom, int segments, float jitter)
{
	std::vector<glm::vec3> pts;
	pts.reserve(static_cast<size_t>(segments) + 1);
	glm::vec2 wander(0.f);
	for (int i = 0; i <= segments; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(segments);
		if (i > 0 && i < segments)
		{
			wander += glm::vec2(randRange(-jitter, jitter), randRange(-jitter, jitter));
			wander *= 0.85f;
		}
		const glm::vec3 p = glm::mix(top, bottom, t);
		pts.emplace_back(p.x + wander.x * (i < segments ? 1.f : 0.f), p.y + wander.y * (i < segments ? 1.f : 0.f), p.z);
	}
	return pts;
}

/// Brightness of a strike over its life: two or three quick strokes, then a fading afterglow
float strikeBrightness(float age)
{
	if (age < 0.f)
	{
		return 0.f;
	}
	if (age < 0.07f) { return 1.f; }
	if (age < 0.13f) { return 0.15f; }
	if (age < 0.22f) { return 0.9f; }
	if (age < 0.27f) { return 0.2f; }
	if (age < 0.33f) { return 0.7f; }
	return std::max(0.f, 0.5f * (1.f - (age - 0.33f) / 0.35f));
}

void strike()
{
	const float cx = static_cast<float>(playerPos.p.x);
	const float cy = static_cast<float>(playerPos.p.z);
	const float view = std::max(1200.f, getViewDistance() * 0.9f);
	// Most strikes land in or near the view; the rest are far off (the sky flashes, no bolt in sight)
	s.boltVisible = rand01() < 0.6f;
	const float dist = s.boltVisible ? randRange(0.f, view * 0.6f) : randRange(view * 1.5f, view * 3.f);
	const float angle = randRange(0.f, 6.2831853f);
	const float mapW = static_cast<float>(world_coord(gameWorld.map.width) - 1);
	const float mapH = static_cast<float>(world_coord(gameWorld.map.height) - 1);
	const float x = std::max(0.f, std::min(mapW, cx + std::cos(angle) * dist));
	const float y = std::max(0.f, std::min(mapH, cy + std::sin(angle) * dist));
	const float ground = static_cast<float>(map_Height(gameWorld.map, static_cast<int>(x), static_cast<int>(y)));
	const glm::vec3 bottom(x, y, ground);
	const glm::vec3 top(x + randRange(-500.f, 500.f), y + randRange(-500.f, 500.f), ground + 2600.f);
	s.bolt = jaggedChannel(top, bottom, 28, 70.f);
	s.forks.clear();
	const int forkCount = 1 + static_cast<int>(rng() % 3u);
	for (int f = 0; f < forkCount; ++f)
	{
		const size_t at = 4 + rng() % (s.bolt.size() / 2);
		const glm::vec3 from = s.bolt[at];
		const glm::vec3 to = from + glm::vec3(randRange(-500.f, 500.f), randRange(-500.f, 500.f), -randRange(400.f, 900.f));
		s.forks.push_back(jaggedChannel(from, glm::vec3(to.x, to.y, std::max(to.z, ground + 200.f)), 16, 40.f));
	}
	s.boltAge = 0.f;
	ambient::thunderAfter(dist);
	if (s.boltVisible)
	{
		particles::spawn("lightning_strike", Vector3f(x, y, ground + 45.f), Vector3f(0.f, 0.f, 1.f), 1.f);
		ambient::disturb(Vector3f(x, y, ground), 2.5f); // the birds hear it too
	}
}

void updateLightning(float dt)
{
	// Thunderstorms in heavy rain; now and then dry lightning in a strong dust storm
	float rate = 0.f; // strikes a second
	if (s.type == Type::Rain && s.level > 0.45f)
	{
		rate = 0.12f * (s.level - 0.3f);
	}
	else if (s.type == Type::Dust && s.level > 0.7f)
	{
		rate = 0.02f;
	}
	if (s.boltAge >= 0.f)
	{
		s.boltAge += dt;
		if (s.boltAge > 0.8f)
		{
			s.boltAge = -1.f;
			s.bolt.clear();
			s.forks.clear();
		}
	}
	if (rate > 0.f && s.boltAge < 0.f)
	{
		s.nextStrike -= dt;
		if (s.nextStrike <= 0.f)
		{
			strike();
			s.nextStrike = randRange(0.4f, 1.6f) / rate; // irregular gaps
		}
	}
	const float b = strikeBrightness(s.boltAge);
	s.flash = b * (s.boltVisible ? 1.f : 0.45f);
	// The bolt itself, bright enough to bloom
	if (s.boltVisible && b > 0.12f)
	{
		const glm::vec3 core = glm::vec3(2.6f, 2.8f, 3.6f) * (0.5f + 2.5f * b);
		for (size_t i = 1; i < s.bolt.size(); ++i)
		{
			particles::frameStreak(Vector3f(s.bolt[i - 1].x, s.bolt[i - 1].y, s.bolt[i - 1].z), Vector3f(s.bolt[i].x, s.bolt[i].y, s.bolt[i].z), core, 1.f);
		}
		for (const auto& fork : s.forks)
		{
			for (size_t i = 1; i < fork.size(); ++i)
			{
				particles::frameStreak(Vector3f(fork[i - 1].x, fork[i - 1].y, fork[i - 1].z), Vector3f(fork[i].x, fork[i].y, fork[i].z), core * 0.55f, 0.8f);
			}
		}
	}
}

} // anonymous namespace

void update()
{
	const uint32_t now = graphicsTime;
	const float dt = s.lastTime == 0 ? 0.f : std::min(0.25f, static_cast<float>(now - s.lastTime) / 1000.f);
	s.lastTime = now;

	const environment_light::WeatherConfig& cfg = environment_light::weatherConfig();
	runDirector(cfg, dt);
	if (!atmosGetWeatherEnabled())
	{
		s.target = 0.f;
		s.level = 0.f;
	}

	// Ramp towards the target
	const float step = dt / kRampSeconds;
	s.level = s.level < s.target ? std::min(s.target, s.level + step) : std::max(s.target, s.level - step);

	// The ground follows slowly: wet in rain, white in snow, drying or melting afterwards
	const float rain = s.type == Type::Rain ? s.level : 0.f;
	const float snowing = s.type == Type::Snow ? s.level : 0.f;
	s.wet = rain > s.wet ? std::min(rain, s.wet + dt / kWetUpSeconds) : std::max(0.f, s.wet - dt / kDryDownSeconds);
	s.snow = snowing > s.snow ? std::min(snowing, s.snow + dt / kSnowUpSeconds) : std::max(0.f, s.snow - dt / kMeltSeconds);

	// The sky and the light
	const float gloom = s.type == Type::Dust ? s.level * 0.6f : s.level;
	environment_light::setWeatherShading(gloom * 0.8f, s.type == Type::Dust ? s.level : s.level * 0.35f);
	const float sunDim = 1.f - 0.3f * gloom;
	const glm::vec3 dustTint = s.type == Type::Dust ? glm::mix(glm::vec3(1.f), glm::vec3(1.f, 0.86f, 0.7f), s.level) : glm::vec3(1.f);
	s.tintDiffuse = glm::vec3(sunDim) * dustTint;
	s.tintSpecular = glm::vec3(1.f - 0.6f * gloom);
	s.tintAmbient = glm::vec3(1.f - 0.12f * gloom) * dustTint;

	// Lightning lights up the whole scene for a moment, cold and blue-white
	if (!gamePaused() && dt > 0.f)
	{
		updateLightning(dt);
	}
	if (s.flash > 0.f)
	{
		const glm::vec3 flashTint = glm::vec3(0.9f, 0.95f, 1.15f) * s.flash;
		s.tintAmbient += flashTint * 1.6f;
		s.tintDiffuse += flashTint * 0.7f;
		s.tintSpecular += flashTint * 0.5f;
	}

	if (s.level <= 0.01f || dt <= 0.f || gamePaused())
	{
		return;
	}

	// Particles around what the camera sees
	static const float kDetail[3] = { 0.4f, 0.7f, 1.f };
	const float detail = kDetail[war_getEffectsDetail()];
	const float radius = std::max(1200.f, getViewDistance() * 0.9f);
	const float areaScale = (radius * radius) / (2000.f * 2000.f);
	// Wind leans the rain and blows the snow and dust (east, a little south)
	const glm::vec3 wind(0.35f, 0.12f, 0.f);
	switch (s.type)
	{
	case Type::Rain:
		emitAround("weather_rain", 2600.f * s.level * areaScale * detail * dt, s.carry, radius, 450.f, 950.f, glm::normalize(glm::vec3(wind.x, wind.y, -1.f)), 1.f);
		emitAround("weather_rain_splash", 700.f * s.level * areaScale * detail * dt, s.splashCarry, radius * 0.8f, 2.f, 4.f, glm::vec3(0.f, 0.f, 1.f), 1.f);
		emitRainRipples(dt, radius * 0.75f, detail * areaScale);
		break;
	case Type::Snow:
		emitAround("weather_snow", 520.f * s.level * areaScale * detail * dt, s.carry, radius, 350.f, 900.f, glm::normalize(glm::vec3(wind.x * 1.4f, wind.y * 1.4f, -1.f)), 1.f);
		break;
	case Type::Dust:
		emitAround("weather_dust", 26.f * s.level * areaScale * detail * dt, s.carry, radius * 1.1f, 20.f, 160.f, glm::normalize(glm::vec3(1.f, 0.35f, 0.05f)), 1.f);
		break;
	case Type::None:
		break;
	}
}

float groundState()
{
	if (s.snow > 0.f && s.snow >= s.wet)
	{
		return -s.snow;
	}
	return s.wet;
}

glm::vec3 lightTint(LIGHTING_TYPE light)
{
	switch (light)
	{
	case LIGHT_DIFFUSE: return s.tintDiffuse;
	case LIGHT_SPECULAR: return s.tintSpecular;
	case LIGHT_AMBIENT: return s.tintAmbient;
	default: return glm::vec3(1.f);
	}
}

float intensity()
{
	return s.level;
}

void reset()
{
	s = State();
	environment_light::setWeatherShading(0.f, 0.f);
}

} // namespace weather
