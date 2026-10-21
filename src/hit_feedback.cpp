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
/** @file hit_feedback.cpp
 * Hit flash and health bar damage trail (see hit_feedback.h). Display only.
 */

#include "hit_feedback.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/piedef.h"

#include "baseobject.h"
#include "objectdef.h"
#include "warzoneconfig.h"

#include <algorithm>
#include <unordered_map>

namespace hit_feedback
{

namespace
{

constexpr float kMinDrop = 0.006f;        // health fraction a single change must remove to flash
constexpr uint32_t kFlashMs = 120;        // flash length (tester: 160 ms was too strong)
constexpr uint32_t kTrailHoldMs = 450;    // trail holds before draining
constexpr float kTrailDrainPerSec = 0.55f;
constexpr uint32_t kForgetMs = 8000;      // drop entries not seen for this long

struct State
{
	float fraction = 1.f;      // last seen health fraction
	float trail = 1.f;         // trail start value, drains towards fraction
	uint32_t trailSince = 0;   // when the trail last grew (graphics time)
	uint32_t flashStart = 0;
	float flashStrength = 0.f;
	uint32_t lastSeen = 0;
	uint32_t lastUpdate = 0;
};

std::unordered_map<uint64_t, State> states;
uint32_t lastPrune = 0;

uint64_t keyOf(const BASE_OBJECT* psObj)
{
	return (static_cast<uint64_t>(psObj->type) << 32) | psObj->id;
}

void prune()
{
	if (graphicsTime - lastPrune < 2000)
	{
		return;
	}
	lastPrune = graphicsTime;
	for (auto it = states.begin(); it != states.end();)
	{
		if (graphicsTime - it->second.lastSeen > kForgetMs)
		{
			it = states.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void drainTrail(State& s)
{
	if (s.lastUpdate == graphicsTime)
	{
		return;
	}
	const float dt = static_cast<float>(graphicsTime - s.lastUpdate) / 1000.f;
	s.lastUpdate = graphicsTime;
	if (s.trail > s.fraction && graphicsTime - s.trailSince > kTrailHoldMs)
	{
		s.trail = std::max(s.fraction, s.trail - kTrailDrainPerSec * std::min(dt, 0.25f));
	}
}

} // anonymous namespace

void observe(const BASE_OBJECT* psObj, uint32_t body, uint32_t maxBody)
{
	if (psObj == nullptr || maxBody == 0)
	{
		return;
	}
	prune();
	const float fraction = std::min(1.f, static_cast<float>(body) / static_cast<float>(maxBody));
	auto found = states.find(keyOf(psObj));
	if (found == states.end())
	{
		State s;
		s.fraction = fraction;
		s.trail = fraction;
		s.lastSeen = graphicsTime;
		s.lastUpdate = graphicsTime;
		states.emplace(keyOf(psObj), s);
		return;
	}
	State& s = found->second;
	s.lastSeen = graphicsTime;
	drainTrail(s);
	const float drop = s.fraction - fraction;
	if (drop >= kMinDrop)
	{
		// A hit: flash harder for bigger hits, and hold the trail where the health was
		const float strength = std::min(0.6f, 0.2f + drop * 4.f); // tester: full strength was too much
		const bool stillFlashing = graphicsTime - s.flashStart < kFlashMs;
		s.flashStrength = stillFlashing ? std::max(s.flashStrength, strength) : strength;
		s.flashStart = graphicsTime;
		s.trail = std::max(s.trail, s.fraction);
		s.trailSince = graphicsTime;
	}
	if (fraction > s.trail)
	{
		s.trail = fraction; // repaired
	}
	// Small changes (burning, repair) just follow along without flashing
	s.fraction = fraction;
}

float flash(const BASE_OBJECT* psObj)
{
	if (psObj == nullptr)
	{
		return 0.f;
	}
	auto found = states.find(keyOf(psObj));
	if (found == states.end())
	{
		return 0.f;
	}
	const State& s = found->second;
	const uint32_t age = graphicsTime - s.flashStart;
	if (s.flashStrength <= 0.f || age >= kFlashMs)
	{
		return 0.f;
	}
	const float t = 1.f - static_cast<float>(age) / static_cast<float>(kFlashMs);
	return s.flashStrength * t * t;
}

float trailFraction(const BASE_OBJECT* psObj, float currentFraction)
{
	if (psObj == nullptr)
	{
		return currentFraction;
	}
	auto found = states.find(keyOf(psObj));
	if (found == states.end())
	{
		return currentFraction;
	}
	drainTrail(found->second);
	return std::max(currentFraction, found->second.trail);
}

void clear()
{
	states.clear();
}

ScopedFlash::ScopedFlash(const BASE_OBJECT* psObj, uint32_t body, uint32_t maxBody)
{
	observe(psObj, body, maxBody);
	const float f = war_getHitFlash() ? flash(psObj) : 0.f;
	if (f > 0.f)
	{
		pie_SetHitFlash(f);
		active = true;
	}
}

ScopedFlash::~ScopedFlash()
{
	if (active)
	{
		pie_SetHitFlash(0.f);
	}
}

} // namespace hit_feedback
