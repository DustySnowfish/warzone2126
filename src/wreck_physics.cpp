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
/** @file wreck_physics.cpp
 * Physical destruction of vehicles (see wreck_physics.h). Display only.
 */

#include "wreck_physics.h"

#include "lib/framework/frame.h"
#include "lib/framework/fixedpoint.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piepalette.h"

#include "component.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "miscimd.h"
#include "objectdef.h"
#include "particles.h"
#include "stats.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <random>
#include <vector>

namespace wreck_physics
{

namespace
{

constexpr float kGravity = 900.f;          // world units per second squared
constexpr float kRestitution = 0.3f;       // fraction of the impact speed bounced back
constexpr float kFriction = 0.55f;         // fraction of the sliding speed kept per bounce
constexpr float kLife = 28.f;              // seconds before a wreck is gone
constexpr float kSinkTime = 6.f;           // seconds of sinking into the ground at the end
constexpr float kSinkDepth = 60.f;         // world units
constexpr float kSmokeTime = 14.f;         // seconds a hull keeps smoking
constexpr size_t kMaxPieces = 600;

struct Piece
{
	const iIMDShape* shape = nullptr;
	int team = 0;
	glm::vec3 pos { 0.f };         // render space (x, height, -map y)
	glm::vec3 vel { 0.f };
	glm::quat rot { 1.f, 0.f, 0.f, 0.f };
	glm::vec3 angVel { 0.f };      // radians per second, world axes
	float age = 0.f;
	float bottom = 0.f;            // distance from the origin to the lowest point
	bool resting = false;
	bool smokes = false;
	float nextSmoke = 0.f;
};

std::vector<Piece> pieces;
std::minstd_rand rng(2126u);

float rand01()
{
	return std::uniform_real_distribution<float>(0.f, 1.f)(rng);
}

float randRange(float a, float b)
{
	return a + (b - a) * rand01();
}

float groundAt(const glm::vec3& renderPos)
{
	const int x = std::max(0, std::min(static_cast<int>(renderPos.x), world_coord(gameWorld.map.width) - 1));
	const int y = std::max(0, std::min(static_cast<int>(-renderPos.z), world_coord(gameWorld.map.height) - 1));
	return static_cast<float>(map_Height(gameWorld.map, x, y));
}

void addPiece(const iIMDBaseShape* base, int team, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& vel, const glm::vec3& angVel, bool smokes)
{
	if (base == nullptr || pieces.size() >= kMaxPieces)
	{
		return;
	}
	const iIMDShape* shape = base->displayModel();
	if (shape == nullptr)
	{
		return;
	}
	Piece p;
	p.shape = shape;
	p.team = team;
	p.pos = pos;
	p.vel = vel;
	p.rot = rot;
	p.angVel = angVel;
	p.bottom = std::max(0.f, -static_cast<float>(shape->min.y));
	p.smokes = smokes;
	pieces.push_back(p);
}

} // anonymous namespace

void droidDestroyed(const DROID* psDroid)
{
	if (psDroid == nullptr || psDroid->isCyborg() || psDroid->isTransporter() || psDroid->droidType == DROID_PERSON
	    || !psDroid->visibleForLocalDisplay())
	{
		return;
	}
	const int team = getPlayerColour(psDroid->player);
	const glm::vec3 base(static_cast<float>(psDroid->pos.x), static_cast<float>(psDroid->pos.z), -static_cast<float>(psDroid->pos.y));
	const float heading = -UNDEG(psDroid->rot.direction);
	const glm::quat hullRot = glm::angleAxis(heading, glm::vec3(0.f, 1.f, 0.f));
	const float flying = psDroid->isFlying() ? 1.f : 0.f;

	// Hull: a short hop and a lazy tumble, then it settles as the wreck
	const iIMDBaseShape* body = BODY_IMD(psDroid, psDroid->player);
	addPiece(body, team, base + glm::vec3(0.f, 4.f, 0.f), hullRot,
	         glm::vec3(randRange(-60.f, 60.f), randRange(90.f, 180.f) + flying * 50.f, randRange(-60.f, 60.f)),
	         glm::vec3(randRange(-1.2f, 1.2f), randRange(-0.6f, 0.6f), randRange(-1.2f, 1.2f)), true);

	// Turret and gun: blown upwards, spinning
	if (psDroid->numWeaps > 0 && psDroid->asWeaps[0].nStat > 0)
	{
		glm::vec3 mountPos = base + glm::vec3(0.f, 20.f, 0.f);
		if (body != nullptr && body->displayModel() != nullptr && !body->displayModel()->connectors.empty())
		{
			const Vector3i c = body->displayModel()->connectors[0];
			mountPos = base + glm::rotate(hullRot, glm::vec3(static_cast<float>(c.x), static_cast<float>(c.z), static_cast<float>(c.y)));
		}
		const glm::quat turretRot = hullRot * glm::angleAxis(-UNDEG(psDroid->asWeaps[0].rot.direction), glm::vec3(0.f, 1.f, 0.f));
		const glm::vec3 kick(randRange(-150.f, 150.f), randRange(320.f, 520.f), randRange(-150.f, 150.f));
		const glm::vec3 spin(randRange(-7.f, 7.f), randRange(-5.f, 5.f), randRange(-7.f, 7.f));
		addPiece(WEAPON_MOUNT_IMD(psDroid, 0), team, mountPos, turretRot, kick, spin, false);
		addPiece(WEAPON_IMD(psDroid, 0), team, mountPos + glm::vec3(0.f, 6.f, 0.f), turretRot, kick * 1.05f, spin * 1.1f, false);
	}

	// A few chunks of plating
	const int chunks = 2 + static_cast<int>(rand01() * 3.f);
	for (int i = 0; i < chunks; ++i)
	{
		const float a = rand01() * 6.2832f;
		addPiece(getRandomDebrisImd(), team, base + glm::vec3(0.f, 12.f, 0.f),
		         glm::angleAxis(rand01() * 6.2832f, glm::normalize(glm::vec3(rand01() - 0.5f, rand01(), rand01() - 0.5f))),
		         glm::vec3(std::cos(a) * randRange(150.f, 350.f), randRange(200.f, 420.f), std::sin(a) * randRange(150.f, 350.f)),
		         glm::vec3(randRange(-9.f, 9.f), randRange(-9.f, 9.f), randRange(-9.f, 9.f)), false);
	}
}

void update()
{
	const float dt = std::min(0.1f, static_cast<float>(deltaGraphicsTime) / 1000.f);
	if (dt <= 0.f)
	{
		return;
	}
	for (size_t i = 0; i < pieces.size();)
	{
		Piece& p = pieces[i];
		p.age += dt;
		if (p.age >= kLife)
		{
			p = pieces.back();
			pieces.pop_back();
			continue;
		}
		if (!p.resting)
		{
			p.vel.y -= kGravity * dt;
			p.pos += p.vel * dt;
			const float spin = glm::length(p.angVel);
			if (spin > 1e-4f)
			{
				p.rot = glm::normalize(glm::angleAxis(spin * dt, p.angVel / spin) * p.rot);
			}
			const float ground = groundAt(p.pos) + p.bottom;
			if (p.pos.y < ground)
			{
				p.pos.y = ground;
				if (p.vel.y < 0.f)
				{
					p.vel.y = -p.vel.y * kRestitution;
				}
				p.vel.x *= kFriction;
				p.vel.z *= kFriction;
				p.angVel *= 0.55f;
				if (p.vel.y < 40.f && glm::length(glm::vec2(p.vel.x, p.vel.z)) < 30.f)
				{
					p.resting = true;
					p.vel = glm::vec3(0.f);
					p.angVel = glm::vec3(0.f);
				}
				else if (p.vel.y > 60.f)
				{
					// Dust where it hits the ground
					particles::spawn("bullet_dirt", Vector3f(p.pos.x, -p.pos.z, p.pos.y), Vector3f(0.f, 0.f, 1.f), 0.8f);
				}
			}
		}
		else
		{
			// Follow the ground if it changes under the wreck (craters, earthworks)
			const float ground = groundAt(p.pos) + p.bottom;
			if (std::abs(p.pos.y - ground) > 2.f)
			{
				p.pos.y = ground;
			}
		}
		if (p.smokes && p.age < kSmokeTime && p.age >= p.nextSmoke)
		{
			p.nextSmoke = p.age + randRange(0.35f, 0.7f);
			particles::spawn(p.age < 4.f ? "damage_fire" : "damage_smoke", Vector3f(p.pos.x, -p.pos.z, p.pos.y + 10.f));
		}
		++i;
	}
}

void draw(const glm::mat4& viewMatrix)
{
	const PIELIGHT burnt = pal_RGBA(150, 138, 128, 255);
	for (const Piece& p : pieces)
	{
		glm::vec3 pos = p.pos;
		if (p.age > kLife - kSinkTime)
		{
			pos.y -= kSinkDepth * (p.age - (kLife - kSinkTime)) / kSinkTime;
		}
		const glm::mat4 model = glm::translate(pos) * glm::toMat4(p.rot);
		pie_Draw3DShape(p.shape, 0, p.team, burnt, pie_SHADOW, 0, model, viewMatrix);
	}
}

void clear()
{
	pieces.clear();
}

} // namespace wreck_physics
