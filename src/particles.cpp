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
/** @file particles.cpp
 * Data-driven particle effects (see particles.h and data/base/effects/README.md).
 *
 * Particles live in render space (x east, y up, z = -map y). Each frame they are moved,
 * then drawn as camera-facing quads in two instanced draws: lit / alpha-blended particles
 * (smoke, dust, debris) sorted back to front, then additive ones (fire, flashes, sparks).
 * The textures are generated at start-up (soft glows, sparks, noise-based smoke and fire
 * puffs, debris chunks), so no art is needed.
 */

#include "particles.h"

#include "objectdef.h"
#include "lighting.h"
#include "stats.h"
#include "statsdef.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/gfx_pipelines.h"
#include "lib/ivis_opengl/pielighting.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/pietypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <nlohmann/json.hpp>

namespace particles
{

namespace
{

constexpr const char* kEffectsFile = "effects/particles.json";
constexpr const char* kWeaponFile = "effects/weapon_vfx.json";
constexpr size_t kMaxParticles = 24000;
constexpr size_t kMaxLights = 96;
constexpr int kAtlasColumns = 8;
constexpr int kAtlasRows = 4;
constexpr int kCell = 64;
constexpr float kPi = 3.14159265f;

// ---------------------------------------------------------------------------------------
// Definitions

struct FrameSet
{
	int first = 0;
	int count = 1;
};

const std::unordered_map<std::string, FrameSet>& frameSets()
{
	static const std::unordered_map<std::string, FrameSet> sets = {
		{ "glow", { 0, 1 } },
		{ "core", { 1, 1 } },
		{ "spark", { 2, 1 } },
		{ "ring", { 3, 1 } },
		{ "star", { 4, 1 } },
		{ "dust", { 5, 3 } },
		{ "smoke", { 8, 4 } },
		{ "fire", { 12, 4 } },
		{ "debris", { 16, 4 } },
	};
	return sets;
}

struct Range
{
	float lo = 0.f;
	float hi = 0.f;
};

struct ColourKey
{
	float t = 0.f;
	glm::vec3 rgb { 1.f, 1.f, 1.f }; // linear, times intensity
	float alpha = 1.f;
};

enum class Direction { Emitter, Up, Sphere, Hemisphere };

struct Layer
{
	FrameSet frames;
	bool additive = true;
	Range count { 1.f, 1.f };
	Range life { 300.f, 300.f };     // ms
	Range speed { 0.f, 0.f };        // world units per second
	Direction direction = Direction::Sphere;
	float spreadCos = -1.f;          // cone half angle (cosine) for Emitter / Up
	float inherit = 0.f;             // not used for one-shot effects yet
	float gravity = 0.f;             // world units per second squared (down)
	float drag = 0.f;                // per second
	Range sizeStart { 10.f, 10.f };
	Range sizeEnd { 10.f, 10.f };
	std::vector<ColourKey> colour;
	Range rotation { 0.f, 360.f };   // degrees
	Range spin { 0.f, 0.f };         // degrees per second
	float stretch = 0.f;             // length added per unit of speed (sparks)
	bool lit = false;
	float offset = 0.f;              // spawn radius
	float rise = 0.f;                // constant upward speed added (buoyant smoke)
};

struct LightDef
{
	bool enabled = false;
	glm::vec3 colour { 1.f, 0.8f, 0.5f };
	float range = 256.f;
	float intensity = 1.f;
	float life = 200.f;
};

struct Effect
{
	std::vector<Layer> layers;
	LightDef light;
};

struct WeaponVfx
{
	std::string muzzle;
	std::string trail;
	std::string hit;
	std::string miss;
	std::string water;
	float trailSpacing = 24.f;
	float scale = 1.f;
	bool replaceMuzzle = false;
	bool replaceImpact = false;
};

// ---------------------------------------------------------------------------------------
// Runtime state

struct Particle
{
	glm::vec3 position;
	glm::vec3 velocity;
	float age;
	float life;
	float sizeStart;
	float sizeEnd;
	float rotation;
	float spin;
	float frame;
	float scale;
	const Layer* layer;
};

struct EffectLight
{
	glm::vec3 position; // render space
	glm::vec3 colour;
	float range;
	float intensity;
	float age;
	float life;
};

struct TrailState
{
	glm::vec3 last;      // render space
	float carry = 0.f;   // distance since the last spawn
	uint32_t seenFrame = 0;
};

struct State
{
	bool loaded = false;
	std::unordered_map<std::string, Effect> effects;
	std::unordered_map<std::string, WeaponVfx> bySubclass;
	std::unordered_map<std::string, WeaponVfx> byWeaponId;
	std::unordered_map<const WEAPON_STATS*, const WeaponVfx*> weaponCache;
	std::vector<Particle> alive;
	std::vector<EffectLight> lights;
	std::unordered_map<uint32_t, TrailState> trails;
	uint32_t frameCounter = 0;
	std::mt19937 rng { 2126u };

	gfx_api::texture* atlas = nullptr;
	gfx_api::buffer* quad = nullptr;
	std::vector<gfx_api::buffer*> instanceBuffers;
	size_t bufferIndex = 0;
	std::vector<gfx_api::ParticleInstance> alphaInstances;
	std::vector<gfx_api::ParticleInstance> additiveInstances;
	std::vector<std::pair<float, size_t>> sortKeys;
};

State s;

float random01()
{
	return std::uniform_real_distribution<float>(0.f, 1.f)(s.rng);
}

float pick(const Range& r)
{
	return r.lo + (r.hi - r.lo) * random01();
}

glm::vec3 randomUnit()
{
	const float z = random01() * 2.f - 1.f;
	const float a = random01() * 2.f * kPi;
	const float r = std::sqrt(std::max(0.f, 1.f - z * z));
	return glm::vec3(r * std::cos(a), z, r * std::sin(a));
}

/// Random direction within a cone around `axis` (unit), given the cosine of its half angle.
glm::vec3 randomInCone(const glm::vec3& axis, float spreadCos)
{
	const float cosT = 1.f - random01() * (1.f - spreadCos);
	const float sinT = std::sqrt(std::max(0.f, 1.f - cosT * cosT));
	const float phi = random01() * 2.f * kPi;
	const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
	const glm::vec3 u = glm::normalize(glm::cross(axis, helper));
	const glm::vec3 v = glm::cross(axis, u);
	return axis * cosT + (u * std::cos(phi) + v * std::sin(phi)) * sinT;
}

/// Game world (x, map y, height) to render space (x, height, -map y)
glm::vec3 toRender(const Vector3f& world)
{
	return glm::vec3(world.x, world.z, -world.y);
}

// ---------------------------------------------------------------------------------------
// JSON

float srgbToLinear(float v)
{
	return std::pow(std::max(0.f, std::min(v, 1.f)), 2.2f);
}

Range readRange(const nlohmann::json& j, const char* key, Range fallback)
{
	auto it = j.find(key);
	if (it == j.end())
	{
		return fallback;
	}
	if (it->is_number())
	{
		const float v = it->get<float>();
		return { v, v };
	}
	if (it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number())
	{
		return { (*it)[0].get<float>(), (*it)[1].get<float>() };
	}
	return fallback;
}

float readFloat(const nlohmann::json& j, const char* key, float fallback)
{
	auto it = j.find(key);
	return (it != j.end() && it->is_number()) ? it->get<float>() : fallback;
}

glm::vec3 readRgb(const nlohmann::json& j, const char* key, glm::vec3 fallback)
{
	auto it = j.find(key);
	if (it == j.end() || !it->is_array() || it->size() != 3)
	{
		return fallback;
	}
	glm::vec3 out;
	for (int i = 0; i < 3; ++i)
	{
		out[i] = (*it)[static_cast<size_t>(i)].is_number() ? (*it)[static_cast<size_t>(i)].get<float>() : 1.f;
	}
	return out;
}

Layer parseLayer(const nlohmann::json& j, const std::string& effectName)
{
	Layer l;
	const std::string texture = j.value("texture", std::string("glow"));
	auto frameIt = frameSets().find(texture);
	if (frameIt == frameSets().end())
	{
		debug(LOG_ERROR, "Particles: effect %s uses unknown texture \"%s\"", effectName.c_str(), texture.c_str());
	}
	else
	{
		l.frames = frameIt->second;
	}
	l.additive = j.value("blend", std::string("additive")) != "alpha";
	l.count = readRange(j, "count", l.count);
	l.life = readRange(j, "life", l.life);
	l.speed = readRange(j, "speed", l.speed);
	const std::string dir = j.value("direction", std::string("sphere"));
	l.direction = dir == "emitter" ? Direction::Emitter : dir == "up" ? Direction::Up : dir == "hemisphere" ? Direction::Hemisphere : Direction::Sphere;
	l.spreadCos = std::cos(std::max(0.f, std::min(readFloat(j, "spread", 180.f), 180.f)) * kPi / 180.f);
	l.gravity = readFloat(j, "gravity", 0.f);
	l.drag = readFloat(j, "drag", 0.f);
	auto sizeIt = j.find("size");
	if (sizeIt != j.end() && sizeIt->is_array() && sizeIt->size() == 2)
	{
		const float a = (*sizeIt)[0].get<float>();
		const float b = (*sizeIt)[1].get<float>();
		const float jitter = readFloat(j, "sizeJitter", 0.2f);
		l.sizeStart = { a * (1.f - jitter), a * (1.f + jitter) };
		l.sizeEnd = { b * (1.f - jitter), b * (1.f + jitter) };
	}
	auto colIt = j.find("colour");
	if (colIt != j.end() && colIt->is_array())
	{
		const size_t n = colIt->size();
		for (size_t i = 0; i < n; ++i)
		{
			const nlohmann::json& k = (*colIt)[i];
			ColourKey key;
			key.t = readFloat(k, "t", n > 1 ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.f);
			const glm::vec3 srgb = readRgb(k, "rgb", glm::vec3(1.f));
			const float intensity = readFloat(k, "intensity", 1.f);
			key.rgb = glm::vec3(srgbToLinear(srgb.r), srgbToLinear(srgb.g), srgbToLinear(srgb.b)) * intensity;
			key.alpha = readFloat(k, "alpha", 1.f);
			l.colour.push_back(key);
		}
	}
	if (l.colour.empty())
	{
		l.colour.push_back(ColourKey());
	}
	l.rotation = readRange(j, "rotation", l.rotation);
	l.spin = readRange(j, "spin", l.spin);
	l.stretch = readFloat(j, "stretch", 0.f);
	l.lit = j.value("lit", !l.additive);
	l.offset = readFloat(j, "offset", 0.f);
	l.rise = readFloat(j, "rise", 0.f);
	return l;
}

bool readJsonFile(const char* path, nlohmann::json& out)
{
	PHYSFS_file* f = PHYSFS_openRead(path);
	if (f == nullptr)
	{
		debug(LOG_ERROR, "Particles: cannot open %s", path);
		return false;
	}
	const PHYSFS_sint64 len = PHYSFS_fileLength(f);
	std::vector<char> data(len > 0 ? static_cast<size_t>(len) : 0);
	const bool ok = len >= 0 && (len == 0 || WZ_PHYSFS_readBytes(f, data.data(), static_cast<PHYSFS_uint32>(len)) == len);
	PHYSFS_close(f);
	if (!ok)
	{
		return false;
	}
	out = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
	if (out.is_discarded() || !out.is_object())
	{
		debug(LOG_ERROR, "Particles: %s is not valid JSON", path);
		return false;
	}
	return true;
}

WeaponVfx parseWeaponVfx(const nlohmann::json& j)
{
	WeaponVfx w;
	w.muzzle = j.value("muzzle", std::string());
	w.trail = j.value("trail", std::string());
	w.hit = j.value("hit", std::string());
	w.miss = j.value("miss", w.hit);
	w.water = j.value("water", std::string());
	w.trailSpacing = std::max(4.f, readFloat(j, "trailSpacing", 24.f));
	w.scale = readFloat(j, "scale", 1.f);
	w.replaceMuzzle = j.value("replaceMuzzle", false);
	w.replaceImpact = j.value("replaceImpact", false);
	return w;
}

bool loadDefinitions()
{
	s.effects.clear();
	s.bySubclass.clear();
	s.byWeaponId.clear();
	s.weaponCache.clear();
	nlohmann::json effects;
	if (!readJsonFile(kEffectsFile, effects))
	{
		return false;
	}
	for (auto it = effects.begin(); it != effects.end(); ++it)
	{
		if (!it.value().is_object())
		{
			continue;
		}
		Effect e;
		auto layersIt = it.value().find("layers");
		if (layersIt != it.value().end() && layersIt->is_array())
		{
			for (const auto& layer : *layersIt)
			{
				e.layers.push_back(parseLayer(layer, it.key()));
			}
		}
		auto lightIt = it.value().find("light");
		if (lightIt != it.value().end() && lightIt->is_object())
		{
			e.light.enabled = true;
			const glm::vec3 srgb = readRgb(*lightIt, "rgb", glm::vec3(1.f, 0.8f, 0.5f));
			e.light.colour = srgb;
			e.light.range = readFloat(*lightIt, "range", 256.f);
			e.light.intensity = readFloat(*lightIt, "intensity", 1.f);
			e.light.life = std::max(1.f, readFloat(*lightIt, "life", 200.f));
		}
		s.effects.emplace(it.key(), std::move(e));
	}
	nlohmann::json weapons;
	if (readJsonFile(kWeaponFile, weapons))
	{
		auto sub = weapons.find("subclass");
		if (sub != weapons.end() && sub->is_object())
		{
			for (auto it = sub->begin(); it != sub->end(); ++it)
			{
				s.bySubclass.emplace(it.key(), parseWeaponVfx(it.value()));
			}
		}
		auto ids = weapons.find("weapons");
		if (ids != weapons.end() && ids->is_object())
		{
			for (auto it = ids->begin(); it != ids->end(); ++it)
			{
				s.byWeaponId.emplace(it.key(), parseWeaponVfx(it.value()));
			}
		}
	}
	debug(LOG_INFO, "Particles: %zu effects, %zu weapon classes, %zu weapon overrides", s.effects.size(), s.bySubclass.size(), s.byWeaponId.size());
	return true;
}

const WeaponVfx* weaponVfx(const WEAPON_STATS* weapon)
{
	if (weapon == nullptr)
	{
		return nullptr;
	}
	auto cached = s.weaponCache.find(weapon);
	if (cached != s.weaponCache.end())
	{
		return cached->second;
	}
	const WeaponVfx* found = nullptr;
	auto byId = s.byWeaponId.find(weapon->id.toUtf8());
	if (byId != s.byWeaponId.end())
	{
		found = &byId->second;
	}
	else
	{
		auto bySub = s.bySubclass.find(getWeaponSubClass(weapon->weaponSubClass));
		if (bySub != s.bySubclass.end())
		{
			found = &bySub->second;
		}
	}
	s.weaponCache.emplace(weapon, found);
	return found;
}

// ---------------------------------------------------------------------------------------
// Procedural textures

float hash2(int x, int y, int seed)
{
	uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(seed) * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return static_cast<float>(h & 0xffffffu) / 16777215.f;
}

float valueNoise(float x, float y, int seed)
{
	const int xi = static_cast<int>(std::floor(x));
	const int yi = static_cast<int>(std::floor(y));
	const float fx = x - static_cast<float>(xi);
	const float fy = y - static_cast<float>(yi);
	const float ux = fx * fx * (3.f - 2.f * fx);
	const float uy = fy * fy * (3.f - 2.f * fy);
	const float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
	const float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
	return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}

float fbm(float x, float y, int seed, int octaves)
{
	float sum = 0.f, amp = 0.5f, freq = 1.f;
	for (int i = 0; i < octaves; ++i)
	{
		sum += amp * valueNoise(x * freq, y * freq, seed + i * 17);
		amp *= 0.5f;
		freq *= 2.f;
	}
	return sum;
}

float smoothstep(float e0, float e1, float x)
{
	const float t = std::max(0.f, std::min((x - e0) / (e1 - e0), 1.f));
	return t * t * (3.f - 2.f * t);
}

/// Alpha of frame `frame` at cell coordinates u, v in [-1, 1]
float frameAlpha(int frame, float u, float v)
{
	const float r = std::sqrt(u * u + v * v);
	switch (frame)
	{
	case 0: // soft glow
		return std::exp(-r * r * 4.5f);
	case 1: // hot core
		return std::exp(-r * r * 12.f);
	case 2: // spark streak (long along u)
		return std::exp(-(u * u * 3.f + v * v * 60.f)) * (1.f - smoothstep(0.85f, 1.f, std::abs(u)));
	case 3: // ring
		return std::exp(-(r - 0.62f) * (r - 0.62f) * 90.f) * (1.f - smoothstep(0.9f, 1.f, r));
	case 4: // flare star
		return std::min(1.f, std::exp(-r * r * 10.f) + 0.6f * std::exp(-(u * u * 150.f)) * std::exp(-v * v * 2.f) + 0.6f * std::exp(-(v * v * 150.f)) * std::exp(-u * u * 2.f));
	case 5: case 6: case 7: // dust: thin, wide noise
	{
		const float n = fbm(u * 2.5f + 10.f, v * 2.5f + 3.f, frame * 31, 4);
		return std::max(0.f, n - 0.35f) * 1.6f * (1.f - smoothstep(0.45f, 1.f, r));
	}
	case 8: case 9: case 10: case 11: // smoke puff
	{
		const float n = fbm(u * 2.2f + 7.f * frame, v * 2.2f, frame * 13, 5);
		const float edge = 1.f - smoothstep(0.35f + 0.25f * n, 1.f, r);
		return std::max(0.f, std::min(1.f, (n * 1.4f - 0.15f) * edge * 1.3f));
	}
	case 12: case 13: case 14: case 15: // fire puff: brighter, ragged edge
	{
		const float n = fbm(u * 3.f + 5.f * frame, v * 3.f, frame * 7, 5);
		const float edge = 1.f - smoothstep(0.2f + 0.45f * n, 0.95f, r);
		return std::max(0.f, std::min(1.f, edge * (0.6f + n)));
	}
	case 16: case 17: case 18: case 19: // debris chunk: irregular polygon, hard edge
	{
		const float angle = std::atan2(v, u);
		const float radius = 0.45f + 0.35f * valueNoise(angle * 1.3f + 10.f, static_cast<float>(frame), frame * 3);
		return 1.f - smoothstep(radius - 0.04f, radius, r);
	}
	default:
		return 0.f;
	}
}

bool createAtlas()
{
	iV_Image image;
	const int width = kAtlasColumns * kCell;
	const int height = kAtlasRows * kCell;
	image.allocate(static_cast<unsigned>(width), static_cast<unsigned>(height), 4, true);
	unsigned char* px = image.bmp_w();
	for (int frame = 0; frame < kAtlasColumns * kAtlasRows; ++frame)
	{
		const int cx = (frame % kAtlasColumns) * kCell;
		const int cy = (frame / kAtlasColumns) * kCell;
		for (int y = 0; y < kCell; ++y)
		{
			for (int x = 0; x < kCell; ++x)
			{
				const float u = (static_cast<float>(x) + 0.5f) / kCell * 2.f - 1.f;
				const float v = (static_cast<float>(y) + 0.5f) / kCell * 2.f - 1.f;
				// Keep a transparent border so bilinear filtering never bleeds between cells
				const bool border = x == 0 || y == 0 || x == kCell - 1 || y == kCell - 1;
				const float a = border ? 0.f : std::max(0.f, std::min(frameAlpha(frame, u, v), 1.f));
				unsigned char* p = px + (static_cast<size_t>(cy + y) * static_cast<size_t>(width) + static_cast<size_t>(cx + x)) * 4;
				p[0] = p[1] = p[2] = 255;
				p[3] = static_cast<unsigned char>(std::lround(a * 255.f));
			}
		}
	}
	s.atlas = gfx_api::context::get().create_texture(1, static_cast<size_t>(width), static_cast<size_t>(height), gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8, "particleAtlas");
	return s.atlas != nullptr && s.atlas->upload(0, image);
}

bool createBuffers()
{
	static const float corners[8] = { -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
	s.quad = gfx_api::context::get().create_buffer_object(gfx_api::buffer::usage::vertex_buffer, gfx_api::context::buffer_storage_hint::static_draw, "particleQuad");
	if (s.quad == nullptr)
	{
		return false;
	}
	s.quad->upload(sizeof(corners), corners);
	s.instanceBuffers.resize(gfx_api::context::get().maxFramesInFlight() + 1);
	for (size_t i = 0; i < s.instanceBuffers.size(); ++i)
	{
		s.instanceBuffers[i] = gfx_api::context::get().create_buffer_object(gfx_api::buffer::usage::vertex_buffer, gfx_api::context::buffer_storage_hint::stream_draw, "particleInstances[" + std::to_string(i) + "]");
	}
	return true;
}

// ---------------------------------------------------------------------------------------
// Simulation

glm::vec4 colourAt(const Layer& layer, float t)
{
	const auto& keys = layer.colour;
	if (keys.size() == 1 || t <= keys.front().t)
	{
		return glm::vec4(keys.front().rgb, keys.front().alpha);
	}
	for (size_t i = 1; i < keys.size(); ++i)
	{
		if (t <= keys[i].t)
		{
			const float span = std::max(keys[i].t - keys[i - 1].t, 1e-4f);
			const float f = (t - keys[i - 1].t) / span;
			return glm::mix(glm::vec4(keys[i - 1].rgb, keys[i - 1].alpha), glm::vec4(keys[i].rgb, keys[i].alpha), f);
		}
	}
	return glm::vec4(keys.back().rgb, keys.back().alpha);
}

void spawnRender(const Effect& effect, const glm::vec3& pos, const glm::vec3& dir, float scale)
{
	const float dirLen = glm::length(dir);
	const glm::vec3 axis = dirLen > 1e-4f ? dir / dirLen : glm::vec3(0.f, 1.f, 0.f);
	for (const Layer& layer : effect.layers)
	{
		const int count = static_cast<int>(std::lround(pick(layer.count)));
		for (int i = 0; i < count && s.alive.size() < kMaxParticles; ++i)
		{
			glm::vec3 d;
			switch (layer.direction)
			{
			case Direction::Emitter: d = randomInCone(axis, layer.spreadCos); break;
			case Direction::Up: d = randomInCone(glm::vec3(0.f, 1.f, 0.f), layer.spreadCos); break;
			case Direction::Hemisphere: d = randomUnit(); d.y = std::abs(d.y); break;
			case Direction::Sphere: default: d = randomUnit(); break;
			}
			Particle p;
			p.position = pos + randomUnit() * (layer.offset * scale * random01());
			p.velocity = d * (pick(layer.speed) * scale) + glm::vec3(0.f, layer.rise * scale, 0.f);
			p.age = 0.f;
			p.life = std::max(0.016f, pick(layer.life) / 1000.f);
			p.sizeStart = pick(layer.sizeStart) * scale;
			p.sizeEnd = pick(layer.sizeEnd) * scale;
			p.rotation = pick(layer.rotation) * kPi / 180.f;
			p.spin = pick(layer.spin) * kPi / 180.f * (random01() < 0.5f ? -1.f : 1.f);
			p.frame = static_cast<float>(layer.frames.first + std::min(layer.frames.count - 1, static_cast<int>(random01() * static_cast<float>(layer.frames.count))));
			p.scale = scale;
			p.layer = &layer;
			s.alive.push_back(p);
		}
	}
	if (effect.light.enabled && s.lights.size() < kMaxLights)
	{
		EffectLight l;
		l.position = pos + glm::vec3(0.f, 24.f * scale, 0.f);
		l.colour = effect.light.colour;
		l.range = effect.light.range * scale;
		l.intensity = effect.light.intensity;
		l.age = 0.f;
		l.life = effect.light.life / 1000.f;
		s.lights.push_back(l);
	}
}

void spawnNamed(const std::string& name, const glm::vec3& pos, const glm::vec3& dir, float scale)
{
	if (name.empty() || !s.loaded)
	{
		return;
	}
	auto it = s.effects.find(name);
	if (it == s.effects.end())
	{
		static std::unordered_map<std::string, bool> warned;
		if (!warned[name])
		{
			warned[name] = true;
			debug(LOG_ERROR, "Particles: unknown effect \"%s\"", name.c_str());
		}
		return;
	}
	spawnRender(it->second, pos, dir, scale);
}

} // anonymous namespace

// ---------------------------------------------------------------------------------------
// Public interface

bool initialise()
{
	shutdown();
	s.loaded = loadDefinitions();
	if (gfx_api::context::isInitialized())
	{
		if (!createAtlas() || !createBuffers())
		{
			debug(LOG_ERROR, "Particles: could not create GPU resources");
		}
	}
	return s.loaded;
}

void shutdown()
{
	clear();
	delete s.atlas;
	s.atlas = nullptr;
	delete s.quad;
	s.quad = nullptr;
	for (auto* b : s.instanceBuffers)
	{
		delete b;
	}
	s.instanceBuffers.clear();
	s.effects.clear();
	s.bySubclass.clear();
	s.byWeaponId.clear();
	s.weaponCache.clear();
	s.loaded = false;
}

void clear()
{
	s.alive.clear();
	s.lights.clear();
	s.trails.clear();
}

bool hasEffect(const std::string& effectName)
{
	return s.loaded && s.effects.count(effectName) != 0;
}

void spawn(const std::string& effectName, const Vector3f& position, const Vector3f& direction, float scale)
{
	spawnNamed(effectName, toRender(position), toRender(direction), scale);
}

void weaponFired(const WEAPON_STATS* weapon, const Vector3f& muzzlePosition, const Vector3f& muzzleDirection)
{
	const WeaponVfx* vfx = weaponVfx(weapon);
	if (vfx != nullptr)
	{
		spawnNamed(vfx->muzzle, toRender(muzzlePosition), toRender(muzzleDirection), vfx->scale);
	}
}

void weaponTrail(const WEAPON_STATS* weapon, uint32_t id, const Vector3f& position, const Vector3f&)
{
	const WeaponVfx* vfx = weaponVfx(weapon);
	if (vfx == nullptr || vfx->trail.empty())
	{
		return;
	}
	const glm::vec3 pos = toRender(position);
	auto it = s.trails.find(id);
	if (it == s.trails.end())
	{
		TrailState t;
		t.last = pos;
		t.seenFrame = s.frameCounter;
		s.trails.emplace(id, t);
		return;
	}
	TrailState& t = it->second;
	const glm::vec3 delta = pos - t.last;
	const float dist = glm::length(delta);
	if (dist > 1e-3f && dist < 2048.f)
	{
		const glm::vec3 dir = delta / dist;
		float along = vfx->trailSpacing - t.carry;
		int guard = 0;
		while (along <= dist && guard++ < 32)
		{
			// Puffs drift backwards a little, so the trail opens out behind the projectile
			spawnNamed(vfx->trail, t.last + dir * along, -dir, vfx->scale);
			along += vfx->trailSpacing;
		}
		t.carry = dist - (along - vfx->trailSpacing);
	}
	t.last = pos;
	t.seenFrame = s.frameCounter;
}

void weaponImpact(const WEAPON_STATS* weapon, const Vector3f& position, ImpactKind kind)
{
	const WeaponVfx* vfx = weaponVfx(weapon);
	if (vfx == nullptr)
	{
		return;
	}
	const std::string& name = kind == ImpactKind::Hit ? vfx->hit : kind == ImpactKind::Water && !vfx->water.empty() ? vfx->water : vfx->miss;
	spawnNamed(name, toRender(position), glm::vec3(0.f, 1.f, 0.f), vfx->scale);
}

bool replacesClassicMuzzle(const WEAPON_STATS* weapon)
{
	const WeaponVfx* vfx = weaponVfx(weapon);
	return vfx != nullptr && vfx->replaceMuzzle && !vfx->muzzle.empty();
}

bool replacesClassicImpact(const WEAPON_STATS* weapon)
{
	const WeaponVfx* vfx = weaponVfx(weapon);
	return vfx != nullptr && vfx->replaceImpact && !vfx->hit.empty();
}

size_t liveCount()
{
	return s.alive.size();
}

void update(LightingData& lightData)
{
	++s.frameCounter;
	const float dt = static_cast<float>(deltaGraphicsTime) / 1000.f;
	if (dt > 0.f)
	{
		for (size_t i = 0; i < s.alive.size();)
		{
			Particle& p = s.alive[i];
			p.age += dt;
			if (p.age >= p.life)
			{
				p = s.alive.back();
				s.alive.pop_back();
				continue;
			}
			const Layer& layer = *p.layer;
			if (layer.drag > 0.f)
			{
				p.velocity *= std::exp(-layer.drag * dt);
			}
			p.velocity.y -= layer.gravity * p.scale * dt;
			p.position += p.velocity * dt;
			p.rotation += p.spin * dt;
			++i;
		}
		for (size_t i = 0; i < s.lights.size();)
		{
			s.lights[i].age += dt;
			if (s.lights[i].age >= s.lights[i].life)
			{
				s.lights[i] = s.lights.back();
				s.lights.pop_back();
				continue;
			}
			++i;
		}
	}
	// Trails of projectiles no longer drawn
	for (auto it = s.trails.begin(); it != s.trails.end();)
	{
		it = (s.frameCounter - it->second.seenFrame > 3) ? s.trails.erase(it) : std::next(it);
	}
	// Effect lights go to the engine's point lights (render space back to light coordinates)
	for (const EffectLight& l : s.lights)
	{
		const float remaining = 1.f - l.age / l.life;
		LIGHT light;
		light.position = Vector3i(static_cast<int>(l.position.x), static_cast<int>(l.position.y), static_cast<int>(-l.position.z));
		light.range = static_cast<UDWORD>(std::max(8.f, l.range));
		light.colour = pal_Colour(static_cast<UBYTE>(std::min(255.f, l.colour.r * 255.f)), static_cast<UBYTE>(std::min(255.f, l.colour.g * 255.f)), static_cast<UBYTE>(std::min(255.f, l.colour.b * 255.f)));
		light.intensity = l.intensity * remaining * remaining;
		lightData.lights.push_back(light);
	}
}

void draw(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
	if (s.alive.empty() || s.atlas == nullptr || s.quad == nullptr || s.instanceBuffers.empty())
	{
		return;
	}
	const glm::mat4 invView = glm::inverse(viewMatrix);
	const glm::vec3 cameraPos(invView[3]);
	const glm::vec3 right(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
	const glm::vec3 up(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
	// The engine's projection flips z (pie_PerspectiveGet), so the camera looks down +z in view space
	const glm::vec3 forward = glm::vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]);

	s.alphaInstances.clear();
	s.additiveInstances.clear();
	s.sortKeys.clear();
	for (size_t i = 0; i < s.alive.size(); ++i)
	{
		const Particle& p = s.alive[i];
		const float t = p.age / p.life;
		gfx_api::ParticleInstance inst;
		inst.positionSize = glm::vec4(p.position, p.sizeStart + (p.sizeEnd - p.sizeStart) * t);
		inst.colour = colourAt(*p.layer, t);
		const float speed = glm::length(p.velocity);
		inst.params = glm::vec4(p.rotation, p.frame, p.layer->stretch / std::max(p.scale, 0.01f), p.layer->lit ? 1.f : 0.f);
		inst.velocity = glm::vec4(speed > 1e-3f ? p.velocity / speed * std::min(speed / 100.f, 8.f) : glm::vec3(0.f), 0.f);
		if (p.layer->additive)
		{
			s.additiveInstances.push_back(inst);
		}
		else
		{
			s.sortKeys.emplace_back(glm::dot(p.position - cameraPos, forward), s.alphaInstances.size());
			s.alphaInstances.push_back(inst);
		}
	}
	// Alpha-blended particles back to front
	std::sort(s.sortKeys.begin(), s.sortKeys.end(), [](const std::pair<float, size_t>& a, const std::pair<float, size_t>& b) { return a.first > b.first; });
	std::vector<gfx_api::ParticleInstance> combined;
	combined.reserve(s.alphaInstances.size() + s.additiveInstances.size());
	for (const auto& k : s.sortKeys)
	{
		combined.push_back(s.alphaInstances[k.second]);
	}
	const size_t alphaCount = combined.size();
	combined.insert(combined.end(), s.additiveInstances.begin(), s.additiveInstances.end());

	s.bufferIndex = (s.bufferIndex + 1) % s.instanceBuffers.size();
	gfx_api::buffer* instances = s.instanceBuffers[s.bufferIndex];
	instances->upload(combined.size() * sizeof(gfx_api::ParticleInstance), combined.data());

	gfx_api::constant_buffer_type<SHADER_PARTICLE> constants {};
	constants.viewProjection = projectionMatrix * viewMatrix;
	constants.cameraRight = glm::vec4(right, 0.f);
	constants.cameraUp = glm::vec4(up, 0.f);
	constants.cameraForward = glm::vec4(forward, 0.f);
	constants.atlas = glm::vec4(static_cast<float>(kAtlasColumns), static_cast<float>(kAtlasRows), 0.f, 0.f);
	constants.ambient = glm::vec4(0.38f, 0.36f, 0.35f, 0.f);
	constants.sunColour = glm::vec4(0.75f, 0.68f, 0.58f, 0.f);
	const Vector3f sun = getTheSun();
	constants.sunDir = glm::vec4(glm::normalize(glm::vec3(-sun.x, -sun.y, -sun.z)), 0.f);

	auto drawBatch = [&](auto& pso, size_t first, size_t count) {
		if (count == 0)
		{
			return;
		}
		pso.bind();
		pso.bind_constants(constants);
		gfx_api::context::get().bind_vertex_buffers(0, {
			std::make_tuple(s.quad, static_cast<size_t>(0)),
			std::make_tuple(instances, first * sizeof(gfx_api::ParticleInstance)) });
		pso.bind_textures(s.atlas);
		pso.draw_instanced(4, 0, count);
	};
	drawBatch(gfx_api::ParticleAlphaPSO::get(), 0, alphaCount);
	drawBatch(gfx_api::ParticleAdditivePSO::get(), alphaCount, combined.size() - alphaCount);
}

} // namespace particles
