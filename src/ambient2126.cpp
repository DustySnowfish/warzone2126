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

#include "ambient2126.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include "display3d.h"
#include "game_world.h"
#include "loop.h"
#include "map.h"
#include "particles.h"
#include "time_of_day.h"
#include "warzoneconfig.h"
#include "weather.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace ambient
{

namespace
{

constexpr float kPi = 3.14159265f;

std::minstd_rand rng(0x2126b1u);

float rand01()
{
	return static_cast<float>(rng() % 100000u) / 100000.f;
}

float randRange(float a, float b)
{
	return a + (b - a) * rand01();
}

struct Bird
{
	glm::vec3 pos { 0.f };        // world: map x, map y, height
	glm::vec3 vel { 0.f };
	float orbitAngle = 0.f;       // its place on the flock's circle
	float orbitRadius = 300.f;
	float heightOffset = 0.f;
	float flapPhase = 0.f;        // 0..3 through the wing beat
	float flapRate = 7.f;         // wing beats per second while flapping
	float glideLeft = 0.f;        // seconds of gliding left (wings level)
	float flapLeft = 1.f;         // seconds of flapping left
	glm::vec2 scatter { 0.f };    // its own escape direction while the flock flees
	float size = 28.f;
};

struct Flock
{
	bool gulls = false;
	glm::vec2 centre { 0.f };     // map x, y the flock circles
	float altitude = 320.f;       // above the ground
	float orbitSpeed = 0.5f;      // radians per second (sign: direction)
	float fleeLeft = 0.f;         // seconds of fleeing left
	glm::vec2 fleeDir { 1.f, 0.f };
	bool leaving = false;         // flying off for good (storm, night, too far away)
	std::vector<Bird> birds;
};

struct State
{
	std::vector<Flock> flocks;
	float surfCarry = 0.f;
	float clock = 0.f;
	std::vector<glm::vec3> noises;   // recent loud places: map x, y and the clock time
	bool started = false;
};

State s;

float mapWidthWorld()
{
	return static_cast<float>(world_coord(gameWorld.map.width));
}

float mapHeightWorld()
{
	return static_cast<float>(world_coord(gameWorld.map.height));
}

bool onMap(int tx, int ty)
{
	return tx >= 1 && ty >= 1 && tx < gameWorld.map.width - 1 && ty < gameWorld.map.height - 1;
}

bool waterTile(int tx, int ty)
{
	return terrainType(mapTile(gameWorld.map, tx, ty)) == TER_WATER;
}

float groundAt(float x, float y)
{
	const int gx = std::max(0, std::min(static_cast<int>(x), world_coord(gameWorld.map.width) - 1));
	const int gy = std::max(0, std::min(static_cast<int>(y), world_coord(gameWorld.map.height) - 1));
	return static_cast<float>(map_Height(gameWorld.map, gx, gy));
}

/// Is this tile on the coast (water with land next to it)?
bool shoreTile(int tx, int ty, glm::vec2* towardLand = nullptr)
{
	if (!onMap(tx, ty) || !waterTile(tx, ty))
	{
		return false;
	}
	static const int dx[4] = { 1, -1, 0, 0 };
	static const int dy[4] = { 0, 0, 1, -1 };
	const int start = static_cast<int>(rng() % 4u);
	for (int k = 0; k < 4; ++k)
	{
		const int i = (start + k) % 4;
		if (!waterTile(tx + dx[i], ty + dy[i]))
		{
			if (towardLand != nullptr)
			{
				*towardLand = glm::vec2(static_cast<float>(dx[i]), static_cast<float>(dy[i]));
			}
			return true;
		}
	}
	return false;
}

glm::vec2 cameraCentre()
{
	return glm::vec2(static_cast<float>(playerPos.p.x), static_cast<float>(playerPos.p.z));
}

float viewRadius()
{
	return std::max(1400.f, getViewDistance() * 0.9f);
}

/// The point at `altitude` above the ground point `ground` that the camera sees in front of it, so a
/// flock placed there shows over that spot on screen (high birds otherwise appear far up the screen)
glm::vec2 liftTowardCamera(const glm::vec2& ground, float altitude)
{
	Vector3f cam;
	if (!particles::cameraPosition(cam))
	{
		return ground;
	}
	const float g = groundAt(ground.x, ground.y);
	const float above = cam.z - g;
	if (above < altitude + 100.f)
	{
		return ground;
	}
	return ground + (glm::vec2(cam.x, cam.y) - ground) * (altitude / above);
}

/// A place for a new flock: land for birds, near the coast for gulls. `minDist`..`maxDist` from the
/// camera centre. Returns false if none was found.
bool findHome(bool gulls, float minDist, float maxDist, glm::vec2& out)
{
	const glm::vec2 c = cameraCentre();
	for (int attempt = 0; attempt < 40; ++attempt)
	{
		const float a = randRange(0.f, 2.f * kPi);
		const float d = randRange(minDist, maxDist);
		const glm::vec2 p = c + glm::vec2(std::cos(a), std::sin(a)) * d;
		if (p.x < 256.f || p.y < 256.f || p.x > mapWidthWorld() - 256.f || p.y > mapHeightWorld() - 256.f)
		{
			continue;
		}
		// Not where there has been shooting lately
		bool noisy = false;
		for (const glm::vec3& n : s.noises)
		{
			if (glm::length(glm::vec2(n) - p) < 1300.f)
			{
				noisy = true;
				break;
			}
		}
		if (noisy)
		{
			continue;
		}
		const int tx = map_coord(static_cast<int>(p.x));
		const int ty = map_coord(static_cast<int>(p.y));
		if (gulls)
		{
			// Near a shore: within 3 tiles of one
			for (int r = 0; r <= 3; ++r)
			{
				for (int k = 0; k < 6; ++k)
				{
					if (shoreTile(tx + static_cast<int>(rng() % (2u * r + 1u)) - r, ty + static_cast<int>(rng() % (2u * r + 1u)) - r))
					{
						out = p;
						return true;
					}
				}
			}
		}
		else if (onMap(tx, ty) && !waterTile(tx, ty))
		{
			out = p;
			return true;
		}
	}
	return false;
}

void makeFlock(bool gulls, const glm::vec2& home, bool arriving)
{
	Flock f;
	f.gulls = gulls;
	f.centre = home;
	f.altitude = gulls ? randRange(120.f, 220.f) : randRange(200.f, 330.f);
	f.centre = liftTowardCamera(home, f.altitude);
	f.orbitSpeed = randRange(0.25f, 0.5f) * (rand01() < 0.5f ? -1.f : 1.f);
	const int count = gulls ? 3 + static_cast<int>(rng() % 4u) : 6 + static_cast<int>(rng() % 9u);
	// Arriving flocks start off to one side, heading in
	const glm::vec2 c = cameraCentre();
	const glm::vec2 away = glm::length(home - c) > 1.f ? glm::normalize(home - c) : glm::vec2(1.f, 0.f);
	for (int i = 0; i < count; ++i)
	{
		Bird b;
		b.orbitAngle = randRange(0.f, 2.f * kPi);
		b.orbitRadius = gulls ? randRange(200.f, 520.f) : randRange(120.f, 330.f);
		b.heightOffset = randRange(-40.f, 40.f);
		b.flapPhase = randRange(0.f, 3.f);
		b.flapRate = gulls ? randRange(3.f, 4.f) : randRange(7.f, 10.f);
		b.flapLeft = randRange(0.5f, 2.f);
		b.size = gulls ? randRange(36.f, 44.f) : randRange(32.f, 40.f);
		glm::vec2 start = f.centre + glm::vec2(std::cos(b.orbitAngle), std::sin(b.orbitAngle)) * b.orbitRadius;
		if (arriving)
		{
			start += away * randRange(900.f, 1300.f) + glm::vec2(randRange(-150.f, 150.f), randRange(-150.f, 150.f));
		}
		b.pos = glm::vec3(start, groundAt(start.x, start.y) + f.altitude + b.heightOffset);
		b.vel = glm::vec3(-away * 300.f, 0.f);
		f.birds.push_back(b);
	}
	s.flocks.push_back(std::move(f));
}

/// How many flocks of each kind the view should have now (0 at night and in storms)
void wantedFlocks(int& land, int& gulls)
{
	static const int kLand[3] = { 1, 2, 3 };
	static const int kGulls[3] = { 1, 1, 2 };
	land = kLand[war_getEffectsDetail()];
	gulls = kGulls[war_getEffectsDetail()];
	const float phase = time_of_day::phase();
	const bool night = phase < 0.21f || phase > 0.8f;
	if (night || weather::intensity() > 0.6f)
	{
		land = 0;
		gulls = 0;
	}
}

void updateBird(Flock& f, Bird& b, float dt)
{
	glm::vec3 target;
	float maxSpeed = f.gulls ? 330.f : 420.f;
	if (f.fleeLeft > 0.f || f.leaving)
	{
		// Scatter: away from the disturbance (each bird its own way), climbing
		const glm::vec2 dir = f.leaving ? f.fleeDir : b.scatter;
		target = b.pos + glm::vec3(dir * 900.f, 0.f);
		target.z = groundAt(b.pos.x, b.pos.y) + f.altitude + 220.f;
		maxSpeed *= f.leaving ? 1.2f : 1.9f;
	}
	else
	{
		b.orbitAngle += f.orbitSpeed * dt * (300.f / std::max(120.f, b.orbitRadius));
		const glm::vec2 t2 = f.centre + glm::vec2(std::cos(b.orbitAngle), std::sin(b.orbitAngle)) * b.orbitRadius;
		target = glm::vec3(t2, groundAt(t2.x, t2.y) + f.altitude + b.heightOffset);
	}
	// Steer towards the target with momentum
	const glm::vec3 want = target - b.pos;
	const float wantLen = glm::length(want);
	glm::vec3 desired = wantLen > 1e-3f ? want / wantLen * std::min(maxSpeed, wantLen * 1.6f + 120.f) : b.vel;
	const float steer = 1.f - std::exp(-(f.fleeLeft > 0.f ? 4.f : 1.6f) * dt);
	b.vel += (desired - b.vel) * steer;
	const float speed = glm::length(glm::vec2(b.vel));
	if (speed < 110.f && speed > 1e-3f)
	{
		b.vel.x *= 110.f / speed; // keep flying
		b.vel.y *= 110.f / speed;
	}
	b.pos += b.vel * dt;
	// Never into the ground
	const float floor = groundAt(b.pos.x, b.pos.y) + 90.f;
	if (b.pos.z < floor)
	{
		b.pos.z = floor;
		b.vel.z = std::max(b.vel.z, 0.f);
	}

	// Wing beats: bursts of flapping between glides (fleeing birds flap hard all the time)
	const bool fleeing = f.fleeLeft > 0.f;
	if (b.glideLeft > 0.f && !fleeing)
	{
		b.glideLeft -= dt;
		b.flapPhase = 1.f;
	}
	else
	{
		b.flapPhase = std::fmod(b.flapPhase + dt * b.flapRate * (fleeing ? 1.6f : 1.f) * 3.f, 3.f);
		b.flapLeft -= dt;
		if (b.flapLeft <= 0.f && !fleeing)
		{
			b.glideLeft = f.gulls ? randRange(2.5f, 6.f) : randRange(0.6f, 2.2f);
			b.flapLeft = f.gulls ? randRange(0.6f, 1.4f) : randRange(0.8f, 2.4f);
		}
	}
}

void drawBird(const Flock& f, const Bird& b)
{
	// Wing beat as up, level, down, level
	static const int kFrames[4] = { 0, 1, 2, 1 };
	const int flap = b.glideLeft > 0.f ? 1 : kFrames[static_cast<int>(b.flapPhase * 4.f / 3.f) % 4];
	const float heading = std::atan2(b.vel.y, b.vel.x);
	const glm::vec3 rgb = f.gulls ? glm::vec3(0.86f, 0.87f, 0.9f) : glm::vec3(0.05f, 0.045f, 0.04f);
	const float above = b.pos.z - groundAt(b.pos.x, b.pos.y);
	const float shadow = std::max(0.f, 0.3f - above * 0.0002f);
	particles::frameBird(Vector3f(b.pos.x, b.pos.y, b.pos.z), b.size, heading, flap, rgb, 1.f, shadow);
}

void updateSurf(float dt)
{
	static const float kRate[3] = { 3.f, 6.f, 10.f }; // breaking waves per second around the view
	// Waves come in sets: a slow swell in the rate
	const float swell = 0.45f + 0.55f * std::max(0.f, std::sin(s.clock * 2.f * kPi / 8.f));
	s.surfCarry += dt * kRate[war_getEffectsDetail()] * swell * (1.f + weather::intensity());
	const glm::vec2 c = cameraCentre();
	const float r = viewRadius() * 0.6f; // on screen, roughly
	int budget = 6;
	while (s.surfCarry >= 1.f && budget-- > 0)
	{
		s.surfCarry -= 1.f;
		for (int attempt = 0; attempt < 24; ++attempt)
		{
			const int tx = map_coord(static_cast<int>(c.x + randRange(-r, r)));
			const int ty = map_coord(static_cast<int>(c.y + randRange(-r, r)));
			glm::vec2 land;
			if (!shoreTile(tx, ty, &land))
			{
				continue;
			}
			// On the waterline: from the water tile's centre halfway to the land, a little jitter along it
			const glm::vec2 along(-land.y, land.x);
			const glm::vec2 p = (glm::vec2(static_cast<float>(tx), static_cast<float>(ty)) + 0.5f + land * 0.42f) * static_cast<float>(TILE_UNITS)
				+ along * randRange(-50.f, 50.f);
			const float h = groundAt(p.x, p.y);
			particles::spawn("surf_break", Vector3f(p.x, p.y, h + 4.f), Vector3f(land.x, land.y, 0.9f), randRange(0.8f, 1.2f));
			break;
		}
	}
	s.surfCarry = std::min(s.surfCarry, 1.f);
}

} // namespace

void update()
{
	if (gameWorld.map.width <= 0 || gameWorld.map.height <= 0)
	{
		return;
	}
	const float dt = std::min(0.5f, static_cast<float>(deltaGraphicsTime) / 1000.f);
	const bool paused = gamePaused();
	if (!paused)
	{
		s.clock += dt;
	}

	const glm::vec2 c = cameraCentre();
	const float r = viewRadius();
	int wantLand = 0, wantGulls = 0;
	wantedFlocks(wantLand, wantGulls);

	if (!paused && dt > 0.f)
	{
		// Flocks far from the view leave; too many of a kind (night, storm, lower detail) fly off
		int haveLand = 0, haveGulls = 0;
		for (Flock& f : s.flocks)
		{
			if (f.leaving)
			{
				continue;
			}
			int& have = f.gulls ? haveGulls : haveLand;
			const int want = f.gulls ? wantGulls : wantLand;
			if (glm::length(f.centre - c) > r * 0.9f || have >= want)
			{
				f.leaving = true;
				const glm::vec2 out = f.centre - c;
				f.fleeDir = glm::length(out) > 1.f ? glm::normalize(out) : glm::vec2(1.f, 0.f);
				continue;
			}
			++have;
		}
		// New flocks: in view on the first frame, otherwise flying in from beyond the edge
		for (int kind = 0; kind < 2; ++kind)
		{
			const bool gulls = kind == 1;
			int have = gulls ? haveGulls : haveLand;
			const int want = gulls ? wantGulls : wantLand;
			for (int tries = 0; have < want && tries < 2; ++tries)
			{
				glm::vec2 home;
				if (findHome(gulls, s.started ? r * 0.2f : 0.f, s.started ? r * 0.55f : r * 0.5f, home))
				{
					makeFlock(gulls, home, s.started);
					++have;
				}
			}
		}
		s.started = true;

		for (Flock& f : s.flocks)
		{
			f.fleeLeft = std::max(0.f, f.fleeLeft - dt);
			for (Bird& b : f.birds)
			{
				updateBird(f, b, dt);
			}
		}
		// Remove flocks that have flown out of range
		s.flocks.erase(std::remove_if(s.flocks.begin(), s.flocks.end(), [&](const Flock& f) {
			if (!f.leaving)
			{
				return false;
			}
			for (const Bird& b : f.birds)
			{
				if (glm::length(glm::vec2(b.pos) - c) < r * 2.f)
				{
					return false;
				}
			}
			return true;
		}), s.flocks.end());

		s.noises.erase(std::remove_if(s.noises.begin(), s.noises.end(), [](const glm::vec3& n) { return s.clock - n.z > 25.f; }), s.noises.end());
		updateSurf(dt);
	}

	for (const Flock& f : s.flocks)
	{
		for (const Bird& b : f.birds)
		{
			drawBird(f, b);
		}
	}
}

void disturb(const Vector3f& position, float strength)
{
	const glm::vec2 p(position.x, position.y);
	// Remember it for a while (one entry per area), so new flocks keep away
	bool known = false;
	for (glm::vec3& n : s.noises)
	{
		if (glm::length(glm::vec2(n) - p) < 600.f)
		{
			n.z = s.clock;
			known = true;
			break;
		}
	}
	if (!known && s.noises.size() < 32)
	{
		s.noises.emplace_back(p, s.clock);
	}
	const float reach = 1100.f * std::max(0.3f, strength);
	for (Flock& f : s.flocks)
	{
		if (f.leaving)
		{
			continue;
		}
		// Close to the flock's centre or to any of its birds
		float nearest = glm::length(f.centre - p);
		for (const Bird& b : f.birds)
		{
			nearest = std::min(nearest, glm::length(glm::vec2(b.pos) - p));
		}
		if (nearest > reach)
		{
			continue;
		}
		const bool wasCalm = f.fleeLeft <= 0.f;
		f.fleeLeft = std::max(f.fleeLeft, randRange(4.f, 7.f));
		glm::vec2 away = f.centre - p;
		away = glm::length(away) > 1.f ? glm::normalize(away) : glm::vec2(std::cos(randRange(0.f, 2.f * kPi)), std::sin(randRange(0.f, 2.f * kPi)));
		f.fleeDir = away;
		for (Bird& b : f.birds)
		{
			if (wasCalm || rand01() < 0.3f)
			{
				// Each bird bursts off on its own line, broadly away
				const float a = std::atan2(away.y, away.x) + randRange(-1.1f, 1.1f);
				b.scatter = glm::vec2(std::cos(a), std::sin(a));
				b.glideLeft = 0.f;
				b.vel.z += randRange(80.f, 180.f);
			}
		}
		if (!wasCalm)
		{
			continue; // already fleeing: it keeps the new home it picked
		}
		// Settle somewhere new, further away (back over land for birds, the coast for gulls)
		glm::vec2 home = f.centre + away * randRange(1200.f, 1900.f);
		const int tx = map_coord(static_cast<int>(home.x));
		const int ty = map_coord(static_cast<int>(home.y));
		const bool ok = onMap(tx, ty) && (f.gulls ? true : !waterTile(tx, ty));
		if (ok)
		{
			f.centre = liftTowardCamera(home, f.altitude);
		}
		else
		{
			f.leaving = true; // nowhere to go: off it goes
		}
	}
}

void reset()
{
	s = State();
}

size_t birdCount()
{
	size_t n = 0;
	for (const Flock& f : s.flocks)
	{
		n += f.birds.size();
	}
	return n;
}

} // namespace ambient
