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

#include "infantry2126.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/ivis_opengl/imd.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/gamelib/gtime.h"

#include "component.h"
#include "droid.h"
#include "game_world.h"
#include "map.h"
#include "particles.h"
#include "stats.h"
#include "terrain.h"
#include "warzoneconfig.h"
#include "wreck_physics.h"

#include <nlohmann/json.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
	#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

#include <array>
#include <cmath>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace infantry2126
{

namespace
{

constexpr int kMaxFigures = 3;
constexpr int kChannels = 4; // rx, ry, rz (degrees), ty (units)

struct Channel
{
	std::vector<std::pair<float, float>> keys; // time, value

	bool empty() const { return keys.empty(); }

	float at(float t) const
	{
		if (keys.empty())
		{
			return 0.f;
		}
		if (t <= keys.front().first)
		{
			return keys.front().second;
		}
		for (size_t i = 1; i < keys.size(); ++i)
		{
			if (t <= keys[i].first)
			{
				const float span = keys[i].first - keys[i - 1].first;
				const float f = span > 0.f ? (t - keys[i - 1].first) / span : 1.f;
				return keys[i - 1].second + (keys[i].second - keys[i - 1].second) * f;
			}
		}
		return keys.back().second;
	}
};

struct Clip
{
	float length = 1.f;
	float stride = 0.f; // units travelled per cycle (walk and run)
	std::vector<std::array<Channel, kChannels>> tracks; // per joint
};

using Pose = std::vector<glm::vec4>; // per joint: rx, ry, rz, ty

struct Joint
{
	std::string name;
	int parent = -1;
	glm::vec3 pos { 0.f };
	const iIMDShape* shape = nullptr;
};

struct Rig
{
	int figures = 3;
	float spacing = 34.f;
	float scale = 1.f;
	std::vector<Joint> joints;
	int weaponJoint = -1;
	glm::vec3 weaponPos { 0.f };
	glm::mat4 weaponTransform { 1.f }; // rotation and scale of the weapon model at the mount
	// Joints the engine poses directly (-1 when the rig has no such joint)
	int pelvis = -1, chest = -1, head = -1, lUpperarm = -1, rUpperarm = -1;
	int thigh[2] = { -1, -1 }, shin[2] = { -1, -1 }, foot[2] = { -1, -1 }; // left, right
	Pose aim;                // the arms holding the weapon forward
	std::vector<bool> aimed; // joints the aim pose sets
	Clip idle, walk, run, fire;
};

bool loaded = false;
std::unordered_map<std::string, Rig> rigs;

int jointIndex(const Rig& rig, const std::string& name)
{
	for (size_t i = 0; i < rig.joints.size(); ++i)
	{
		if (rig.joints[i].name == name)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

bool readJson(const char* path, nlohmann::json& out)
{
	PHYSFS_file* f = PHYSFS_openRead(path);
	if (f == nullptr)
	{
		return false;
	}
	const PHYSFS_sint64 len = PHYSFS_fileLength(f);
	std::vector<char> data(static_cast<size_t>(std::max<PHYSFS_sint64>(len, 0)));
	const bool ok = len > 0 && WZ_PHYSFS_readBytes(f, data.data(), static_cast<PHYSFS_uint32>(len)) == len;
	PHYSFS_close(f);
	if (!ok)
	{
		return false;
	}
	out = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
	return !out.is_discarded() && out.is_object();
}

int channelIndex(const std::string& name)
{
	return name == "rx" ? 0 : name == "ry" ? 1 : name == "rz" ? 2 : name == "ty" ? 3 : -1;
}

Clip parseClip(const Rig& rig, const nlohmann::json& j)
{
	Clip clip;
	clip.tracks.resize(rig.joints.size());
	if (!j.is_object())
	{
		return clip;
	}
	clip.length = std::max(0.05f, j.value("length", 1.f));
	clip.stride = std::max(0.f, j.value("stride", 0.f));
	auto tracks = j.find("tracks");
	if (tracks == j.end() || !tracks->is_object())
	{
		return clip;
	}
	for (auto it = tracks->begin(); it != tracks->end(); ++it)
	{
		const int joint = jointIndex(rig, it.key());
		if (joint < 0 || !it->is_object())
		{
			continue;
		}
		for (auto ch = it->begin(); ch != it->end(); ++ch)
		{
			const int c = channelIndex(ch.key());
			if (c < 0 || !ch->is_array())
			{
				continue;
			}
			for (const auto& key : *ch)
			{
				if (key.is_array() && key.size() == 2 && key[0].is_number() && key[1].is_number())
				{
					clip.tracks[joint][c].keys.emplace_back(key[0].get<float>(), key[1].get<float>());
				}
			}
		}
	}
	return clip;
}

void load()
{
	loaded = true;
	rigs.clear();
	nlohmann::json doc;
	if (!readJson("infantry2126/rigs.json", doc))
	{
		debug(LOG_ERROR, "Infantry: cannot read infantry2126/rigs.json");
		return;
	}
	auto all = doc.find("rigs");
	if (all == doc.end() || !all->is_object())
	{
		return;
	}
	for (auto it = all->begin(); it != all->end(); ++it)
	{
		const nlohmann::json& r = *it;
		Rig rig;
		rig.figures = std::max(1, std::min(r.value("figures", 3), kMaxFigures));
		rig.spacing = r.value("spacing", 34.f);
		rig.scale = r.value("scale", 1.f);
		bool ok = true;
		for (const auto& j : r.value("joints", nlohmann::json::array()))
		{
			Joint joint;
			joint.name = j.value("name", "");
			joint.parent = jointIndex(rig, j.value("parent", ""));
			const auto pos = j.value("pos", std::vector<float> { 0.f, 0.f, 0.f });
			if (pos.size() == 3)
			{
				joint.pos = glm::vec3(pos[0], pos[1], pos[2]);
			}
			const std::string model = j.value("model", "");
			if (!model.empty())
			{
				const iIMDBaseShape* base = modelGet(WzString::fromUtf8(model));
				joint.shape = base ? base->displayModel() : nullptr;
				ok = ok && joint.shape != nullptr;
			}
			rig.joints.push_back(joint);
		}
		if (!ok || rig.joints.empty())
		{
			debug(LOG_ERROR, "Infantry: rig %s has missing parts", it.key().c_str());
			continue;
		}
		rig.weaponJoint = jointIndex(rig, r.value("weaponJoint", ""));
		const auto wpos = r.value("weaponPos", std::vector<float> { 0.f, 0.f, 0.f });
		if (wpos.size() == 3)
		{
			rig.weaponPos = glm::vec3(wpos[0], wpos[1], wpos[2]);
		}
		const auto wrot = r.value("weaponRot", std::vector<float> { 0.f, 0.f, 0.f });
		if (wrot.size() == 3)
		{
			rig.weaponTransform = glm::rotate(glm::radians(wrot[1]), glm::vec3(0.f, 1.f, 0.f))
				* glm::rotate(glm::radians(wrot[0]), glm::vec3(1.f, 0.f, 0.f))
				* glm::rotate(glm::radians(wrot[2]), glm::vec3(0.f, 0.f, 1.f));
		}
		rig.weaponTransform = rig.weaponTransform * glm::scale(glm::vec3(r.value("weaponScale", 1.f)));
		rig.aim.assign(rig.joints.size(), glm::vec4(0.f));
		rig.aimed.assign(rig.joints.size(), false);
		auto aim = r.find("aim");
		if (aim != r.end() && aim->is_object())
		{
			for (auto a = aim->begin(); a != aim->end(); ++a)
			{
				const int joint = jointIndex(rig, a.key());
				if (joint < 0 || !a->is_object())
				{
					continue;
				}
				rig.aimed[joint] = true;
				for (auto ch = a->begin(); ch != a->end(); ++ch)
				{
					const int c = channelIndex(ch.key());
					if (c >= 0 && ch->is_number())
					{
						rig.aim[joint][c] = ch->get<float>();
					}
				}
			}
		}
		rig.pelvis = jointIndex(rig, "pelvis");
		rig.chest = jointIndex(rig, "chest");
		rig.head = jointIndex(rig, "head");
		rig.lUpperarm = jointIndex(rig, "l_upperarm");
		rig.rUpperarm = jointIndex(rig, "r_upperarm");
		for (int side = 0; side < 2; ++side)
		{
			const std::string prefix = side == 0 ? "l_" : "r_";
			rig.thigh[side] = jointIndex(rig, prefix + "thigh");
			rig.shin[side] = jointIndex(rig, prefix + "shin");
			rig.foot[side] = jointIndex(rig, prefix + "foot");
		}
		auto clips = r.find("clips");
		if (clips != r.end() && clips->is_object())
		{
			rig.idle = parseClip(rig, clips->value("idle", nlohmann::json()));
			rig.walk = parseClip(rig, clips->value("walk", nlohmann::json()));
			rig.run = parseClip(rig, clips->value("run", nlohmann::json()));
			rig.fire = parseClip(rig, clips->value("fire", nlohmann::json()));
		}
		rigs[it.key()] = std::move(rig);
	}
	debug(LOG_INFO, "Infantry: %u rigs loaded", static_cast<unsigned>(rigs.size()));
}

const Rig* rigFor(const DROID* psDroid)
{
	if (!loaded)
	{
		load();
	}
	const BODY_STATS* body = psDroid->getBodyStats();
	if (body == nullptr)
	{
		return nullptr;
	}
	// Heavy cyborg bodies (and super cyborgs) use the heavy frame; everything else the light one
	const bool heavy = body->id.contains("Heavy") || body->id.contains("Super");
	auto it = rigs.find(heavy ? "heavy" : "light");
	return it == rigs.end() ? nullptr : &it->second;
}

void addClip(Pose& pose, const Clip& clip, float t, float weight, bool loop)
{
	if (weight <= 0.f || clip.tracks.size() != pose.size())
	{
		return;
	}
	if (loop)
	{
		t = std::fmod(t, clip.length);
		if (t < 0.f)
		{
			t += clip.length;
		}
	}
	for (size_t j = 0; j < pose.size(); ++j)
	{
		for (int c = 0; c < kChannels; ++c)
		{
			if (!clip.tracks[j][c].empty())
			{
				pose[j][c] += clip.tracks[j][c].at(t) * weight;
			}
		}
	}
}

struct Figure
{
	glm::vec3 pos { 0.f }; // render space (x, height, -map y)
	float yaw = 0.f;       // radians, render space, like the unit's heading
	float phase = 0.f;     // gait cycle, 0..1
	float speed = 0.f;     // smoothed, units per second
	bool alive = true;
	float jitter = 0.f;    // per-figure variation, 0..1
	uint32_t hitTime = 0;  // last flinch
	float hitStrength = 0.f;
	float hitTwist = 0.f;
	uint32_t lastCase = 0; // the shot whose spent case was thrown
	float drawHeight = 0.f; // height the figure stands at (the lower foot on the ground)
};

struct Team
{
	std::array<Figure, kMaxFigures> figs;
	int count = 0;
	uint32_t lastTime = 0;
	uint32_t lastHit = 0; // the unit's last hit already shown as a flinch
	bool init = false;
};

std::unordered_map<uint32_t, Team> teams;
uint32_t lastSweep = 0;
std::minstd_rand rng(2126u);

float rand01()
{
	return std::uniform_real_distribution<float>(0.f, 1.f)(rng);
}

/// Formation slot of figure i in the unit's model space (x across, z forward)
glm::vec3 slot(const Rig& rig, int i, int count, float moving)
{
	const float s = rig.spacing * (1.f - 0.25f * moving); // a little tighter on the move
	if (count == 3)
	{
		static const float kSlots[3][2] = { { 0.f, 0.55f }, { -0.85f, -0.45f }, { 0.85f, -0.45f } };
		return glm::vec3(kSlots[i][0] * s, 0.f, kSlots[i][1] * s);
	}
	if (count == 2)
	{
		return glm::vec3((i == 0 ? -0.55f : 0.55f) * s, 0.f, (i == 0 ? 0.15f : -0.15f) * s);
	}
	return glm::vec3(0.f);
}

float groundHeight(const glm::vec3& renderPos)
{
	const int x = std::max(0, std::min(static_cast<int>(renderPos.x), world_coord(gameWorld.map.width) - 1));
	const int y = std::max(0, std::min(static_cast<int>(-renderPos.z), world_coord(gameWorld.map.height) - 1));
	const int h = map_Height(gameWorld.map, x, y);
	return static_cast<float>(h) + getTerrainVisualObjectHeightDelta(gameWorld.map, x, y, h);
}

/// Joint matrices of one figure in render space
void jointMatrices(const Rig& rig, const Pose& pose, const glm::mat4& figureMatrix, std::vector<glm::mat4>& out)
{
	out.resize(rig.joints.size());
	for (size_t j = 0; j < rig.joints.size(); ++j)
	{
		const Joint& joint = rig.joints[j];
		const glm::vec4& p = pose[j];
		const glm::mat4& parent = joint.parent >= 0 ? out[joint.parent] : figureMatrix;
		out[j] = parent * glm::translate(joint.pos + glm::vec3(0.f, p.w, 0.f))
			* glm::rotate(glm::radians(p.y), glm::vec3(0.f, 1.f, 0.f))
			* glm::rotate(glm::radians(p.x), glm::vec3(1.f, 0.f, 0.f))
			* glm::rotate(glm::radians(p.z), glm::vec3(0.f, 0.f, 1.f));
	}
}

/// The figure's pose: idle, walk and run blended by speed, the arms holding the weapon (or tool),
/// the chest turned to the turret's aim, and the recoil when it fires.
Pose figurePose(const Rig& rig, const Figure& fig, const DROID* psDroid, int index, bool armed)
{
	Pose pose(rig.joints.size(), glm::vec4(0.f));
	const float moving = std::min(1.f, fig.speed / 40.f);
	const float running = std::max(0.f, std::min(1.f, (fig.speed - 130.f) / 100.f));
	const float t = static_cast<float>(graphicsTime) / 1000.f + fig.jitter * 7.f;
	addClip(pose, rig.idle, t, 1.f - moving, true);
	addClip(pose, rig.walk, fig.phase * rig.walk.length, moving * (1.f - running), true);
	addClip(pose, rig.run, fig.phase * rig.run.length, moving * running, true);
	// Arms: the aim pose replaces the gait swing (the weapon is held forward)
	for (size_t j = 0; j < pose.size(); ++j)
	{
		if (rig.aimed[j])
		{
			const float keep = armed ? 0.f : 0.6f; // tools swing a little with the gait
			pose[j] = pose[j] * keep + rig.aim[j] * (1.f - keep) + glm::vec4(0.f, 0.f, 0.f, pose[j].w);
		}
	}
	const int chest = jointIndex(rig, "chest");
	const int rUpper = jointIndex(rig, "r_upperarm");
	if (psDroid->numWeaps > 0)
	{
		// Turn the chest towards the turret's aim; pitch the weapon arm with it
		const Rotation rot = getInterpolatedWeaponRotation(psDroid, 0, graphicsTime);
		float yaw = static_cast<float>(static_cast<int16_t>(rot.direction)) * 360.f / 65536.f;
		yaw = std::max(-80.f, std::min(80.f, yaw));
		const float pitch = std::max(-30.f, std::min(45.f, static_cast<float>(static_cast<int16_t>(rot.pitch)) * 360.f / 65536.f));
		if (chest >= 0)
		{
			pose[chest].y -= yaw;
		}
		if (rUpper >= 0)
		{
			pose[rUpper].x -= pitch;
		}
		// Recoil: each figure fires a little after the one before, so a fireteam ripples
		const uint32_t fired = psDroid->asWeaps[0].lastFired + static_cast<uint32_t>(index) * 110u;
		if (fired != 0 && graphicsTime >= fired)
		{
			const float since = static_cast<float>(graphicsTime - fired) / 1000.f;
			if (since < rig.fire.length)
			{
				addClip(pose, rig.fire, since, 1.f, false);
			}
		}
	}
	// Flinch when hit: the upper body snaps back and twists, then recovers
	if (fig.hitTime != 0 && graphicsTime >= fig.hitTime)
	{
		const float since = static_cast<float>(graphicsTime - fig.hitTime) / 1000.f;
		if (since < 0.45f)
		{
			const float envelope = since < 0.06f ? since / 0.06f : 1.f - (since - 0.06f) / 0.39f;
			const float k = envelope * envelope * fig.hitStrength;
			if (rig.chest >= 0) { pose[rig.chest].x -= 18.f * k; pose[rig.chest].y += fig.hitTwist * k; }
			if (rig.head >= 0) { pose[rig.head].x -= 14.f * k; }
			if (rig.pelvis >= 0) { pose[rig.pelvis].w -= 2.f * k; }
			if (rig.lUpperarm >= 0) { pose[rig.lUpperarm].z -= 18.f * k; }
			if (rig.rUpperarm >= 0) { pose[rig.rUpperarm].x += 12.f * k; }
		}
	}
	return pose;
}

/// Feet on uneven ground: the figure stands with its lower foot on the ground, the other knee bends
/// to reach the higher ground (two-bone leg), and both feet tilt to the slope ahead.
void standOnGround(const Rig& rig, Figure& fig, Pose& pose)
{
	const glm::vec3 forward(std::sin(fig.yaw), 0.f, std::cos(fig.yaw));
	const glm::vec3 right(forward.z, 0.f, -forward.x);
	float footHeight[2];
	for (int side = 0; side < 2; ++side)
	{
		const float across = (rig.thigh[side] >= 0 ? rig.joints[rig.thigh[side]].pos.x : (side == 0 ? -5.f : 5.f)) * rig.scale;
		footHeight[side] = groundHeight(fig.pos + right * across + forward * (2.f * rig.scale));
	}
	const float base = std::min(fig.pos.y, std::min(footHeight[0], footHeight[1]));
	fig.drawHeight = base;
	const float ahead = groundHeight(fig.pos + forward * (10.f * rig.scale));
	const float behind = groundHeight(fig.pos - forward * (10.f * rig.scale));
	const float slope = glm::degrees(std::atan2(ahead - behind, 20.f * rig.scale));
	for (int side = 0; side < 2; ++side)
	{
		if (rig.thigh[side] < 0 || rig.shin[side] < 0 || rig.foot[side] < 0)
		{
			continue;
		}
		const float legLength = (std::abs(rig.joints[rig.shin[side]].pos.y) + std::abs(rig.joints[rig.foot[side]].pos.y)) * rig.scale;
		const float lift = std::min(footHeight[side] - base, legLength * 0.6f);
		const float bend = lift > 0.5f ? glm::degrees(std::acos(1.f - lift / legLength)) : 0.f;
		pose[rig.thigh[side]].x -= bend;
		pose[rig.shin[side]].x += 2.f * bend;
		pose[rig.foot[side]].x -= bend + std::max(-30.f, std::min(30.f, slope));
	}
}

bool softGround(unsigned char type)
{
	return type == TER_SAND || type == TER_SANDYBRUSH || type == TER_BAKEDEARTH || type == TER_GREENMUD
		|| type == TER_REDBRUSH || type == TER_SLUSH || type == TER_SHEETICE;
}

bool dustyGround(unsigned char type)
{
	return type == TER_SAND || type == TER_SANDYBRUSH || type == TER_BAKEDEARTH || type == TER_REDBRUSH || type == TER_PINKROCK;
}

/// A foot comes down: a footprint on soft ground, a puff of dust on dry ground when running
void footfall(const Figure& fig, const glm::vec3& footPos, float running)
{
	const int mx = static_cast<int>(footPos.x), my = static_cast<int>(-footPos.z);
	if (mx < 0 || my < 0 || mx >= world_coord(gameWorld.map.width) || my >= world_coord(gameWorld.map.height))
	{
		return;
	}
	const unsigned char type = terrainType(worldTile(gameWorld.map, mx, my));
	const Vector3f at(footPos.x, -footPos.z, groundHeight(footPos) + 8.f); // as high as track marks: the ground detail is drawn above the height map
	const Vector3f dir(std::sin(fig.yaw), -std::cos(fig.yaw), 0.f);
	if (softGround(type))
	{
		particles::spawn("marks_foot", at, dir, 1.f);
	}
	if (running > 0.3f && dustyGround(type))
	{
		particles::spawn("foot_dust", at, Vector3f(0.f, 0.f, 1.f), 0.6f + 0.4f * running);
	}
}

bool crossed(float before, float after, float mark)
{
	if (after >= before)
	{
		return before < mark && after >= mark;
	}
	return before < mark || after >= mark; // wrapped past 1
}

void breakApart(const Rig& rig, const Figure& fig, const Pose& pose, int team, const glm::vec3& push)
{
	std::vector<glm::mat4> mats;
	const glm::mat4 figureMatrix = glm::translate(fig.pos) * glm::rotate(fig.yaw, glm::vec3(0.f, 1.f, 0.f)) * glm::scale(glm::vec3(rig.scale));
	jointMatrices(rig, pose, figureMatrix, mats);
	for (size_t j = 0; j < rig.joints.size(); ++j)
	{
		if (rig.joints[j].shape == nullptr)
		{
			continue;
		}
		const glm::vec3 vel = push + glm::vec3(rand01() * 160.f - 80.f, 90.f + rand01() * 170.f, rand01() * 160.f - 80.f);
		const glm::vec3 spin(rand01() * 10.f - 5.f, rand01() * 10.f - 5.f, rand01() * 10.f - 5.f);
		wreck_physics::addLoosePart(rig.joints[j].shape, team, mats[j], vel, spin);
	}
	// The core ruptures: a burst of amber and smoke where the chest was
	const glm::vec3 c = fig.pos + glm::vec3(0.f, 40.f * rig.scale, 0.f);
	particles::spawn("damage_smoke", Vector3f(c.x, -c.z, c.y), Vector3f(0.f, 0.f, 1.f), 0.6f);
	particles::spawn("build_sparks", Vector3f(c.x, -c.z, c.y), Vector3f(0.f, 0.f, 1.f), 0.8f);
}

void sweep()
{
	if (graphicsTime - lastSweep < 5000)
	{
		return;
	}
	lastSweep = graphicsTime;
	for (auto it = teams.begin(); it != teams.end();)
	{
		it = (graphicsTime - it->second.lastTime > 10000) ? teams.erase(it) : std::next(it);
	}
}

} // namespace

bool handles(const DROID* psDroid)
{
	return war_getInfantryFireteams() && psDroid != nullptr && psDroid->isCyborg() && rigFor(psDroid) != nullptr;
}

bool draw(const DROID* psDroid, const glm::mat4& unitMatrix, const glm::mat4& viewMatrix, uint32_t colour, PIELIGHT brightness)
{
	const Rig* rigPtr = rigFor(psDroid);
	if (rigPtr == nullptr)
	{
		return false;
	}
	const Rig& rig = *rigPtr;
	sweep();
	Team& team = teams[psDroid->id];
	const float dt = team.init ? std::max(0.f, std::min(0.25f, static_cast<float>(graphicsTime - team.lastTime) / 1000.f)) : 0.f;
	team.lastTime = graphicsTime;

	const glm::vec3 unitPos(unitMatrix[3]);
	const float unitYaw = std::atan2(unitMatrix[2][0], unitMatrix[2][2]); // the model's +z in render space
	const float health = psDroid->originalBody > 0 ? static_cast<float>(psDroid->body) / static_cast<float>(psDroid->originalBody) : 1.f;
	const int alive = std::max(1, std::min(rig.figures, static_cast<int>(std::ceil(health * static_cast<float>(rig.figures) - 0.001f))));
	const float unitSpeed = static_cast<float>(std::abs(psDroid->sMove.speed));
	const float moving = std::min(1.f, unitSpeed / 60.f);
	const bool armed = psDroid->numWeaps > 0;

	if (!team.init || team.count != rig.figures)
	{
		team = Team();
		team.init = true;
		team.count = rig.figures;
		team.lastTime = graphicsTime;
		for (int i = 0; i < rig.figures; ++i)
		{
			Figure& f = team.figs[i];
			f.jitter = rand01();
			f.phase = rand01();
			f.yaw = unitYaw;
			f.pos = unitPos + glm::vec3(unitMatrix * glm::vec4(slot(rig, i, rig.figures, 0.f), 0.f));
		}
	}

	// A new hit on the unit: one of the figures flinches (heavy weapons knock it back)
	if (psDroid->timeLastHit != team.lastHit)
	{
		team.lastHit = psDroid->timeLastHit;
		if (graphicsTime - psDroid->timeLastHit < 500)
		{
			int choices[kMaxFigures];
			int n = 0;
			for (int i = 0; i < std::min(alive, rig.figures); ++i)
			{
				if (team.figs[i].alive) { choices[n++] = i; }
			}
			if (n > 0)
			{
				Figure& f = team.figs[choices[std::min(n - 1, static_cast<int>(rand01() * static_cast<float>(n)))]];
				const WEAPON_SUBCLASS w = psDroid->lastHitWeapon;
				const bool heavy = w == WSC_CANNON || w == WSC_MORTARS || w == WSC_HOWITZERS || w == WSC_MISSILE || w == WSC_ROCKET
					|| w == WSC_SLOWROCKET || w == WSC_SLOWMISSILE || w == WSC_GAUSS || w == WSC_BOMB || w == WSC_ENERGY;
				f.hitTime = graphicsTime;
				f.hitStrength = heavy ? 1.f : 0.55f;
				f.hitTwist = (rand01() - 0.5f) * 30.f;
				if (heavy)
				{
					f.pos -= glm::vec3(std::sin(f.yaw), 0.f, std::cos(f.yaw)) * (14.f * rig.scale);
				}
			}
		}
	}

	const WEAPON_STATS* weaponStats = armed ? psDroid->getWeaponStats(0) : nullptr;
	const bool throwsCases = weaponStats && (weaponStats->weaponSubClass == WSC_MGUN || weaponStats->weaponSubClass == WSC_CANNON);

	bool drew = false;
	std::vector<glm::mat4> mats;
	for (int i = 0; i < rig.figures; ++i)
	{
		Figure& f = team.figs[i];
		if (!f.alive)
		{
			continue;
		}
		// Follow the formation slot with a little lag, each figure at its own pace
		const glm::vec3 target = unitPos + glm::vec3(unitMatrix * glm::vec4(slot(rig, i, rig.figures, moving), 0.f));
		const glm::vec3 before = f.pos;
		const float follow = std::min(1.f, dt * (4.f + 2.f * f.jitter));
		f.pos.x += (target.x - f.pos.x) * follow;
		f.pos.z += (target.z - f.pos.z) * follow;
		f.pos.y = groundHeight(f.pos);
		const float moved = glm::length(glm::vec2(f.pos.x - before.x, f.pos.z - before.z));
		const float instSpeed = dt > 0.f ? moved / dt : 0.f;
		f.speed += (instSpeed - f.speed) * std::min(1.f, dt * 6.f);
		// Face the way it walks when moving, the unit's heading (and so the turret) when still
		float wantYaw = unitYaw;
		if (moved > 0.4f && dt > 0.f)
		{
			wantYaw = std::atan2(f.pos.x - before.x, f.pos.z - before.z);
		}
		float d = std::remainder(wantYaw - f.yaw, glm::two_pi<float>());
		f.yaw += d * std::min(1.f, dt * 5.f);
		// Gait: the cycle advances with the distance covered, so the feet do not slide
		const float running = std::max(0.f, std::min(1.f, (f.speed - 130.f) / 100.f));
		const float stride = (rig.walk.stride * (1.f - running) + rig.run.stride * running) * rig.scale;
		const float phaseBefore = f.phase;
		if (stride > 1.f)
		{
			f.phase = std::fmod(f.phase + moved / stride, 1.f);
		}

		Pose pose = figurePose(rig, f, psDroid, i, armed);
		if (i >= alive)
		{
			// This figure falls: it breaks into its parts
			f.alive = false;
			const glm::vec3 push(std::sin(f.yaw) * -60.f, 0.f, std::cos(f.yaw) * -60.f);
			breakApart(rig, f, pose, static_cast<int>(colour), push);
			continue;
		}

		standOnGround(rig, f, pose);
		const glm::vec3 standAt(f.pos.x, f.drawHeight, f.pos.z);
		const glm::mat4 figureMatrix = glm::translate(standAt) * glm::rotate(f.yaw, glm::vec3(0.f, 1.f, 0.f)) * glm::scale(glm::vec3(rig.scale));
		jointMatrices(rig, pose, figureMatrix, mats);
		// Footfalls: the left foot lands near a quarter of the cycle, the right near three quarters
		if (moved > 0.f)
		{
			for (int side = 0; side < 2; ++side)
			{
				if (rig.foot[side] >= 0 && crossed(phaseBefore, f.phase, side == 0 ? 0.25f : 0.75f))
				{
					footfall(f, glm::vec3(mats[rig.foot[side]][3]), running);
				}
			}
		}
		for (size_t j = 0; j < rig.joints.size(); ++j)
		{
			if (rig.joints[j].shape && pie_Draw3DShape(rig.joints[j].shape, 0, colour, brightness, pie_SHADOW, 0, mats[j], viewMatrix))
			{
				drew = true;
			}
		}
		// The weapon (or tool) in the hand
		if (rig.weaponJoint >= 0)
		{
			const glm::mat4 hand = mats[rig.weaponJoint] * glm::translate(rig.weaponPos) * rig.weaponTransform;
			const iIMDBaseShape* toolBase = nullptr;
			if (armed)
			{
				toolBase = WEAPON_IMD(psDroid, 0);
			}
			else if (psDroid->droidType == DROID_CYBORG_CONSTRUCT)
			{
				toolBase = CONSTRUCT_IMD(psDroid, psDroid->player);
			}
			else if (psDroid->droidType == DROID_CYBORG_REPAIR)
			{
				toolBase = REPAIR_IMD(psDroid, psDroid->player);
			}
			const iIMDShape* tool = toolBase ? toolBase->displayModel() : nullptr;
			if (tool)
			{
				if (pie_Draw3DShape(tool, 0, colour, brightness, pie_SHADOW, 0, hand, viewMatrix))
				{
					drew = true;
				}
				if (armed)
				{
					WEAPON w = psDroid->asWeaps[0];
					w.lastFired += static_cast<uint32_t>(i) * 110u;
					const iIMDBaseShape* flashBase = MUZZLE_FLASH_PIE(psDroid, 0);
					drawMuzzleFlash(w, tool, flashBase ? flashBase->displayModel() : nullptr, brightness, pie_SHADOW, 0, hand, viewMatrix, 0, static_cast<UBYTE>(colour));
					// A spent case thrown out to the right of the weapon
					if (throwsCases && w.lastFired != 0 && w.lastFired != f.lastCase && graphicsTime >= w.lastFired && graphicsTime - w.lastFired < 300)
					{
						f.lastCase = w.lastFired;
						const glm::vec3 at(hand[3]);
						const glm::vec3 side = glm::normalize(glm::vec3(std::cos(f.yaw), 0.f, -std::sin(f.yaw)));
						const glm::vec3 throwDir = glm::normalize(side + glm::vec3(0.f, 0.8f, 0.f));
						particles::spawn("shell_case", Vector3f(at.x, -at.z, at.y), Vector3f(throwDir.x, -throwDir.z, throwDir.y), 1.f);
					}
				}
			}
		}
	}
	return drew;
}

void droidDestroyed(const DROID* psDroid)
{
	auto it = teams.find(psDroid->id);
	const Rig* rig = rigFor(psDroid);
	if (it == teams.end() || rig == nullptr)
	{
		return;
	}
	for (int i = 0; i < it->second.count; ++i)
	{
		Figure& f = it->second.figs[i];
		if (f.alive)
		{
			f.alive = false;
			const Pose pose = figurePose(*rig, f, psDroid, i, psDroid->numWeaps > 0);
			breakApart(*rig, f, pose, static_cast<int>(getPlayerColour(psDroid->player)), glm::vec3(0.f));
		}
	}
	teams.erase(it);
}

void clear()
{
	teams.clear();
}

void reload()
{
	loaded = false;
	rigs.clear();
}

} // namespace infantry2126
