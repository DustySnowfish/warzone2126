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
/** @file time_of_day.cpp
 * Time of day (see time_of_day.h). Display only.
 */

#include "time_of_day.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/piedef.h"

#include "droiddef.h"
#include "environment_light.h"
#include "lighting.h"
#include "warzoneconfig.h"
#include "weather.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace time_of_day
{

namespace
{

constexpr float kPi = 3.14159265f;
constexpr float kCycleMs = 24.f * 60.f * 1000.f; // a full day in 24 minutes of play
constexpr float kCycleStart = 0.30f;             // the cycle starts in the morning
constexpr uint32_t kSunStepMs = 1500;            // the sun moves in small steps (each relights the tiles)
constexpr float kSunStepCos = 0.99985f;          // about one degree

struct State
{
	bool haveBase = false;
	glm::vec3 baseToSun { 0.f, 1.f, 0.f }; // the map's sun, as a direction towards it (render space, y up)
	bool sunMoved = false;                 // whether we changed the sun from the map's
	glm::vec3 appliedToSun { 0.f, 1.f, 0.f };
	uint32_t lastSunChange = 0;
	float phase = 0.42f;
};

State s;

glm::vec3 toSunFromEngine(const Vector3f& sun)
{
	// The engine keeps the sun as the light's travel direction; the sky looks towards the sun
	const glm::vec3 d(-sun.x, -sun.y, -sun.z);
	const float len = glm::length(d);
	return len > 1e-4f ? d / len : glm::vec3(0.f, 1.f, 0.f);
}

float smooth(float a, float b, float x)
{
	const float t = std::max(0.f, std::min(1.f, (x - a) / (b - a)));
	return t * t * (3.f - 2.f * t);
}

float phaseForMode(Mode mode)
{
	switch (mode)
	{
	case Mode::Dawn: return 0.285f;
	case Mode::Day: return 0.42f;
	case Mode::Dusk: return 0.715f;
	case Mode::Night: return 0.97f;
	case Mode::Cycle:
	{
		const float t = kCycleStart + static_cast<float>(graphicsTime) / kCycleMs;
		return t - std::floor(t);
	}
	default: return 0.42f;
	}
}

void setSun(const glm::vec3& toSun, bool force)
{
	if (!force && glm::dot(toSun, s.appliedToSun) > kSunStepCos)
	{
		return;
	}
	if (!force && graphicsTime - s.lastSunChange < kSunStepMs)
	{
		return;
	}
	s.appliedToSun = toSun;
	s.lastSunChange = graphicsTime;
	setTheSun(Vector3f(-toSun.x, -toSun.y, -toSun.z) * 1000.f);
}

void applyTints(const glm::vec3& diffuse, const glm::vec3& ambient, const glm::vec3& specular)
{
	pie_SetLightingTint(LIGHT_DIFFUSE, diffuse * weather::lightTint(LIGHT_DIFFUSE));
	pie_SetLightingTint(LIGHT_AMBIENT, ambient * weather::lightTint(LIGHT_AMBIENT));
	pie_SetLightingTint(LIGHT_SPECULAR, specular * weather::lightTint(LIGHT_SPECULAR));
}

} // anonymous namespace

void update()
{
	if (!s.haveBase)
	{
		s.haveBase = true;
		s.baseToSun = toSunFromEngine(getTheSun());
		s.appliedToSun = s.baseToSun;
	}
	const Mode mode = static_cast<Mode>(war_getTimeOfDay());
	if (mode == Mode::MapDefault)
	{
		if (s.sunMoved)
		{
			s.sunMoved = false;
			setSun(s.baseToSun, true);
		}
		else
		{
			// Scripts may move the sun; follow them
			s.baseToSun = toSunFromEngine(getTheSun());
		}
		applyTints(glm::vec3(1.f), glm::vec3(1.f), glm::vec3(1.f));
		environment_light::setTimeOfDay(glm::vec3(1.f), glm::vec3(1.f), glm::vec3(1.f, 0.95f, 0.85f));
		s.phase = 0.42f;
		return;
	}

	s.phase = phaseForMode(mode);
	// Sun path: rises at 0.25, highest at 0.5, sets at 0.75; it swings across the sky from the map's
	// own sun direction at noon, and never climbs higher than the map's sun
	const float baseElevation = std::asin(std::max(-1.f, std::min(1.f, s.baseToSun.y)));
	const float maxElevation = std::max(0.5f, std::min(1.15f, baseElevation));
	const float baseAzimuth = std::atan2(s.baseToSun.z, s.baseToSun.x);
	const float dayArc = std::sin((s.phase - 0.25f) * 2.f * kPi); // 1 at noon, 0 at sunrise/sunset, < 0 at night
	const float elevation = dayArc * maxElevation;
	const float azimuth = baseAzimuth + (s.phase - 0.5f) * kPi * 0.9f;
	const float sunUp = std::sin(elevation);
	const float daylight = smooth(-0.06f, 0.22f, sunUp); // 0 at night, 1 in full day
	const float golden = daylight * (1.f - smooth(0.12f, 0.45f, sunUp)); // low warm sun

	// Light direction: the sun by day, a moon high on the opposite side by night
	glm::vec3 toSun;
	if (sunUp > -0.02f)
	{
		const float e = std::max(elevation, 0.06f); // keep shadows from stretching to infinity
		toSun = glm::vec3(std::cos(e) * std::cos(azimuth), std::sin(e), std::cos(e) * std::sin(azimuth));
	}
	else
	{
		const float moonAzimuth = baseAzimuth + kPi * 0.8f;
		const float moonElevation = 0.85f;
		toSun = glm::normalize(glm::vec3(std::cos(moonElevation) * std::cos(moonAzimuth), std::sin(moonElevation), std::cos(moonElevation) * std::sin(moonAzimuth)));
	}
	s.sunMoved = true;
	setSun(toSun, false);

	// Light colours
	const glm::vec3 noonSun(1.f, 0.97f, 0.92f);
	const glm::vec3 lowSun(1.f, 0.62f, 0.36f);
	const glm::vec3 moon(0.5f, 0.6f, 0.85f);
	const glm::vec3 sunColour = glm::mix(noonSun, lowSun, golden / std::max(daylight, 0.001f) * 0.9f);
	const float moonStrength = 0.55f;
	const glm::vec3 diffuse = glm::mix(moon * moonStrength, sunColour * (0.35f + 0.65f * daylight), daylight);
	const glm::vec3 ambientDay = glm::mix(glm::vec3(1.f), glm::vec3(1.f, 0.82f, 0.68f), golden);
	const glm::vec3 ambient = glm::mix(glm::vec3(0.46f, 0.52f, 0.74f), ambientDay, daylight);
	const glm::vec3 specular = glm::mix(glm::vec3(0.35f, 0.4f, 0.55f), sunColour, daylight);
	applyTints(diffuse, ambient, specular);

	// Sky: deep blue at night, warm horizon at dawn and dusk
	const glm::vec3 zenith = glm::mix(glm::vec3(0.07f, 0.09f, 0.18f), glm::mix(glm::vec3(1.f), glm::vec3(0.8f, 0.72f, 0.75f), golden), daylight);
	const glm::vec3 horizon = glm::mix(glm::vec3(0.08f, 0.09f, 0.16f), glm::mix(glm::vec3(1.f), glm::vec3(1.25f, 0.78f, 0.55f), golden), daylight);
	environment_light::setTimeOfDay(zenith, horizon, sunUp > -0.02f ? sunColour : moon);
}

void reset()
{
	s = State();
}

Vector3f sunForSaving()
{
	if (!s.sunMoved)
	{
		return getTheSun();
	}
	return Vector3f(-s.baseToSun.x, -s.baseToSun.y, -s.baseToSun.z) * 1000.f;
}

float phase()
{
	return s.phase;
}

} // namespace time_of_day
