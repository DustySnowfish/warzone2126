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
#include "faction.h"
#include "structure.h"
#include "game_world.h"
#include "map.h"
#include "miscimd.h"
#include "objectdef.h"
#include "particles.h"
#include "stats.h"
#include "warzoneconfig.h"

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
constexpr float kCollapseTime = 2.4f;      // seconds for a building to sink out of sight
constexpr float kRubbleLife = 120.f;       // seconds rubble stays

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
	float scale = 1.f;
	float life = kLife;
	uint8_t shade = 150;           // brightness of the burnt tint
	// Building collapse: sinks and leans instead of free physics
	bool collapse = false;
	float collapseDepth = 0.f;
	glm::vec3 leanAxis { 1.f, 0.f, 0.f };
	float leanAngle = 0.f;
	float footprint = 0.f;         // half size, for dust around the base
	float nextDust = 0.f;
	// Warzone 2126: going down in water (sunk ships, debris that lands in the sea)
	bool sinking = false;
	float sinkStart = 0.f;
	float sinkTime = 3.f;
	float sinkDepth = 50.f;
	glm::vec3 listAxis { 0.f, 0.f, 1.f };
	float listAngle = 0.f;
	float nextBubble = 0.f;
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

Piece* addShape(const iIMDShape* shape, int team, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& vel, const glm::vec3& angVel, bool smokes, float scale = 1.f)
{
	static const size_t kPiecesByDetail[3] = { 200, 400, kMaxPieces };
	if (shape == nullptr || pieces.size() >= kPiecesByDetail[war_getEffectsDetail()])
	{
		return nullptr;
	}
	Piece p;
	p.shape = shape;
	p.team = team;
	p.pos = pos;
	p.vel = vel;
	p.rot = rot;
	p.angVel = angVel;
	p.scale = scale;
	p.bottom = std::max(0.f, -static_cast<float>(shape->min.y)) * scale;
	p.smokes = smokes;
	pieces.push_back(p);
	return &pieces.back();
}

void addPiece(const iIMDBaseShape* base, int team, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& vel, const glm::vec3& angVel, bool smokes, float scale = 1.f)
{
	if (base != nullptr)
	{
		addShape(base->displayModel(), team, pos, rot, vel, angVel, smokes, scale);
	}
}

bool waterAt(const glm::vec3& renderPos)
{
	const int x = std::max(0, std::min(static_cast<int>(renderPos.x), world_coord(gameWorld.map.width) - 1));
	const int y = std::max(0, std::min(static_cast<int>(-renderPos.z), world_coord(gameWorld.map.height) - 1));
	return terrainType(mapTile(gameWorld.map, map_coord(x), map_coord(y))) == TER_WATER;
}

void startSinking(Piece& p, float time, float depth, float listAngle)
{
	p.sinking = true;
	p.resting = true;
	p.vel = glm::vec3(0.f);
	p.angVel = glm::vec3(0.f);
	p.sinkStart = p.age;
	p.sinkTime = time;
	p.sinkDepth = depth;
	p.life = p.age + time;
	const float a = rand01() * 6.2832f;
	p.listAxis = glm::vec3(std::cos(a), 0.f, std::sin(a));
	p.listAngle = listAngle;
	p.nextBubble = p.age;
}

const iIMDBaseShape* shipHullImd(const DROID* psDroid)
{
	const int bodyStat = psDroid->asBits[COMP_BODY];
	const int propStat = psDroid->asBits[COMP_PROPULSION];
	return asBodyStats[bodyStat].ppIMDList[propStat * NUM_PROP_SIDES + LEFT_PROP];
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

	const iIMDBaseShape* body = BODY_IMD(psDroid, psDroid->player);
	if (psDroid->getPropulsionStats()->propulsionType == PROPULSION_TYPE_PROPELLOR)
	{
		// Warzone 2126: a ship stays afloat for a moment, burning, then lists over and goes down
		// with its guns still on (a heavy hull sinks more slowly)
		const BODY_SIZE size = psDroid->getBodyStats()->size;
		const float sinkTime = size >= SIZE_HEAVY ? 11.f : (size == SIZE_MEDIUM ? 9.f : 7.f);
		const float a = rand01() * 6.2832f;
		const glm::vec3 listAxis(std::cos(a), 0.f, std::sin(a));
		const float listAngle = randRange(0.35f, 0.7f);
		auto sinkWith = [&](const iIMDBaseShape* shape, const glm::vec3& at, const glm::quat& rot, bool smokes)
		{
			if (shape == nullptr)
			{
				return;
			}
			if (Piece* p = addShape(shape->displayModel(), team, at, rot, glm::vec3(0.f), glm::vec3(0.f), smokes))
			{
				startSinking(*p, sinkTime, 110.f, listAngle);
				p->listAxis = listAxis;
				p->bottom = 0.f;
				p->shade = 170;
			}
		};
		sinkWith(shipHullImd(psDroid), base + glm::vec3(0.f, -10.f, 0.f), hullRot, false);
		sinkWith(body, base, hullRot, true);
		for (int w = 0; w < static_cast<int>(psDroid->numWeaps); ++w)
		{
			if (psDroid->asWeaps[w].nStat == 0 || body == nullptr || body->displayModel() == nullptr || static_cast<size_t>(w) >= body->displayModel()->connectors.size())
			{
				continue;
			}
			const Vector3i c = body->displayModel()->connectors[w];
			const glm::vec3 mountPos = base + glm::rotate(hullRot, glm::vec3(static_cast<float>(c.x), static_cast<float>(c.z), static_cast<float>(c.y)));
			const glm::quat turretRot = hullRot * glm::angleAxis(-UNDEG(psDroid->asWeaps[w].rot.direction), glm::vec3(0.f, 1.f, 0.f));
			// (each piece lists about its own origin, so the turret drifts off the hull a little as
			// it rolls: hidden by the smoke and the water)
			sinkWith(WEAPON_MOUNT_IMD(psDroid, w), mountPos, turretRot, false);
			sinkWith(WEAPON_IMD(psDroid, w), mountPos, turretRot, false);
		}
		particles::spawn("water_splash", Vector3f(base.x, -base.z, base.y + 4.f), Vector3f(0.f, 0.f, 1.f), size >= SIZE_HEAVY ? 2.f : 1.4f);
		// Plating thrown out, to splash down around it
		for (int i = 0; i < 3; ++i)
		{
			const float d = rand01() * 6.2832f;
			addPiece(getRandomDebrisImd(), team, base + glm::vec3(0.f, 12.f, 0.f),
			         glm::angleAxis(rand01() * 6.2832f, glm::normalize(glm::vec3(rand01() - 0.5f, rand01(), rand01() - 0.5f))),
			         glm::vec3(std::cos(d) * randRange(120.f, 280.f), randRange(220.f, 420.f), std::sin(d) * randRange(120.f, 280.f)),
			         glm::vec3(randRange(-9.f, 9.f), randRange(-9.f, 9.f), randRange(-9.f, 9.f)), false);
		}
		return;
	}

	// Hull: a short hop and a lazy tumble, then it settles as the wreck
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

void structureDestroyed(const STRUCTURE* psStruct)
{
	if (psStruct == nullptr || !psStruct->visibleForLocalDisplay() || psStruct->sDisplay.imd == nullptr)
	{
		return;
	}
	const STRUCTURE_TYPE type = psStruct->pStructureType->type;
	const bool wall = type == REF_WALL || type == REF_WALLCORNER || type == REF_GATE;
	const int team = getPlayerColour(psStruct->player);
	const glm::vec3 base(static_cast<float>(psStruct->pos.x), static_cast<float>(psStruct->pos.z), -static_cast<float>(psStruct->pos.y));
	const float size = static_cast<float>(std::max(psStruct->pStructureType->baseWidth, psStruct->pStructureType->baseBreadth));
	const float half = size * TILE_UNITS * 0.5f;
	const glm::quat rot = glm::angleAxis(-UNDEG(psStruct->rot.direction), glm::vec3(0.f, 1.f, 0.f));

	// The building itself sinks and leans as it collapses (it has already left the game)
	const iIMDShape* shape = getFactionDisplayIMD(getPlayerFaction(psStruct->player), psStruct->sDisplay.imd->displayModel());
	if (!wall && psStruct->status == SS_BUILT)
	{
		if (Piece* p = addShape(shape, team, base, rot, glm::vec3(0.f), glm::vec3(0.f), true))
		{
			p->collapse = true;
			p->resting = true;
			p->bottom = 0.f;
			p->life = kCollapseTime;
			p->shade = 200;
			p->collapseDepth = static_cast<float>(std::max(16, shape->max.y)) * 1.1f + 8.f;
			const float a = rand01() * 6.2832f;
			p->leanAxis = glm::vec3(std::cos(a), 0.f, std::sin(a));
			p->leanAngle = randRange(0.08f, 0.16f);
			p->footprint = half;
		}
	}

	// Chunks thrown out of the blast
	const int chunks = wall ? 2 : std::min(10, 3 + static_cast<int>(size) * 2);
	for (int i = 0; i < chunks; ++i)
	{
		const float a = rand01() * 6.2832f;
		const float out = randRange(120.f, 320.f) * (wall ? 0.6f : 1.f);
		addPiece(getRandomDebrisImd(), team, base + glm::vec3(randRange(-half, half) * 0.5f, 30.f, randRange(-half, half) * 0.5f),
		         glm::angleAxis(rand01() * 6.2832f, glm::normalize(glm::vec3(rand01() - 0.5f, rand01() + 0.1f, rand01() - 0.5f))),
		         glm::vec3(std::cos(a) * out, randRange(260.f, 520.f), std::sin(a) * out),
		         glm::vec3(randRange(-8.f, 8.f), randRange(-8.f, 8.f), randRange(-8.f, 8.f)), i == 0 && !wall, randRange(0.7f, 1.2f)); // tester: larger chunks looked oversized
	}

	// Rubble left on the footprint, revealed as the building sinks
	const int rubble = wall ? 2 : std::min(14, 4 + static_cast<int>(size * size) * 2);
	for (int i = 0; i < rubble; ++i)
	{
		glm::vec3 pos = base + glm::vec3(randRange(-half, half) * 0.8f, 0.f, randRange(-half, half) * 0.8f);
		const iIMDBaseShape* debris = getRandomDebrisImd();
		const float scale = randRange(0.8f, 1.5f) * (wall ? 0.8f : 1.f); // tester: 1.6-2.8 looked oversized
		if (Piece* p = addShape(debris ? debris->displayModel() : nullptr, team, pos,
		                        glm::angleAxis(rand01() * 6.2832f, glm::vec3(0.f, 1.f, 0.f)) * glm::angleAxis(randRange(-0.5f, 0.5f), glm::vec3(1.f, 0.f, 0.f)),
		                        glm::vec3(0.f), glm::vec3(0.f), false, scale))
		{
			p->pos.y = groundAt(p->pos) + p->bottom * 0.4f; // half buried
			p->resting = true;
			p->life = kRubbleLife + randRange(0.f, 20.f);
			p->shade = 95;
			p->smokes = (i % 3) == 0;
			p->nextSmoke = randRange(0.f, 2.f);
		}
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
		if (p.age >= p.life)
		{
			p = pieces.back();
			pieces.pop_back();
			continue;
		}
		if (p.sinking)
		{
			const float t = (p.age - p.sinkStart) / p.sinkTime;
			if (p.age >= p.nextBubble && t < 0.9f)
			{
				p.nextBubble = p.age + randRange(0.25f, 0.6f) * (p.smokes ? 1.f : 2.5f);
				particles::spawn("ship_bubbles", Vector3f(p.pos.x, -p.pos.z, groundAt(p.pos) + 2.f), Vector3f(0.f, 0.f, 1.f), p.smokes ? 1.2f : 0.5f);
			}
			if (p.smokes && t < 0.55f && p.age >= p.nextSmoke)
			{
				p.nextSmoke = p.age + randRange(0.3f, 0.6f);
				particles::spawn(t < 0.3f ? "damage_fire" : "damage_smoke", Vector3f(p.pos.x, -p.pos.z, p.pos.y + 14.f), Vector3f(0.f, 0.f, 1.f), 1.2f);
			}
			++i;
			continue;
		}
		if (p.collapse)
		{
			// Dust pouring out around the base while it goes down
			if (p.age >= p.nextDust)
			{
				p.nextDust = p.age + 0.18f;
				const float a = rand01() * 6.2832f;
				const glm::vec3 at = p.pos + glm::vec3(std::cos(a) * p.footprint, 0.f, std::sin(a) * p.footprint);
				particles::spawn("collapse_dust", Vector3f(at.x, -at.z, groundAt(at) + 8.f), Vector3f(0.f, 0.f, 1.f), std::max(0.8f, p.footprint / 120.f));
			}
			++i;
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
			if (p.pos.y < ground && waterAt(p.pos))
			{
				// Splash down and sink (Warzone 2126)
				particles::spawn("water_splash", Vector3f(p.pos.x, -p.pos.z, ground + 2.f), Vector3f(0.f, 0.f, 1.f), 0.7f);
				p.pos.y = ground - p.bottom * 0.5f;
				p.smokes = false;
				startSinking(p, 2.5f, 40.f * p.scale, randRange(0.2f, 0.6f));
				++i;
				continue;
			}
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
		if (p.smokes && p.age < (p.life > kLife ? 40.f : kSmokeTime) && p.age >= p.nextSmoke)
		{
			p.nextSmoke = p.age + randRange(0.35f, 0.7f);
			particles::spawn(p.age < 4.f ? "damage_fire" : "damage_smoke", Vector3f(p.pos.x, -p.pos.z, p.pos.y + 10.f), Vector3f(0.f, 0.f, 1.f), p.collapse ? 2.f : 1.f);
		}
		++i;
	}
}

void draw(const glm::mat4& viewMatrix)
{
	for (const Piece& p : pieces)
	{
		glm::vec3 pos = p.pos;
		glm::quat rot = p.rot;
		uint8_t shade = p.shade;
		if (p.collapse)
		{
			// Slow start then faster: the structure gives way and drops
			const float t = std::min(1.f, p.age / kCollapseTime);
			pos.y -= p.collapseDepth * t * t;
			rot = glm::angleAxis(p.leanAngle * std::sqrt(t), p.leanAxis) * rot;
			shade = static_cast<uint8_t>(200.f - 80.f * t);
		}
		else if (p.sinking)
		{
			// Settles slowly at first, then goes under; lists over as it fills
			const float t = std::min(1.f, (p.age - p.sinkStart) / p.sinkTime);
			pos.y -= p.sinkDepth * t * t;
			rot = glm::angleAxis(p.listAngle * std::min(1.f, t * 1.6f), p.listAxis) * rot;
			shade = static_cast<uint8_t>(static_cast<float>(p.shade) * (1.f - 0.45f * t));
		}
		else if (p.age > p.life - kSinkTime)
		{
			pos.y -= kSinkDepth * p.scale * (p.age - (p.life - kSinkTime)) / kSinkTime;
		}
		glm::mat4 model = glm::translate(pos) * glm::toMat4(rot);
		if (p.scale != 1.f)
		{
			model = model * glm::scale(glm::vec3(p.scale));
		}
		pie_Draw3DShape(p.shape, 0, p.team, pal_RGBA(shade, static_cast<uint8_t>(shade * 0.92f), static_cast<uint8_t>(shade * 0.86f), 255), pie_SHADOW, 0, model, viewMatrix);
	}
}

void clear()
{
	pieces.clear();
}

} // namespace wreck_physics

bool wreck_physics::enabled()
{
	return war_getPhysicalWrecks();
}
