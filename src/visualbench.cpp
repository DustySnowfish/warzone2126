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
/** \file
 *  Visual benchmark runner. See visualbench.h for the config format.
 */

#include "visualbench.h"

#include <map>
#include "colour_grade.h"
#include "environment_light.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/framework/wzapp.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/wztime.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/netplay/netplay.h"

#include "display.h"
#include "display3d.h"
#include "game_world.h"
#include "keybind.h"
#include "map.h"
#include "qtscript.h"
#include "version.h"
#include "warcam.h"
#include "wzjsonhelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{

using Clock = std::chrono::steady_clock;

struct Shot
{
	std::string name;
	std::string anchor = "centre";
	float offsetX = 0.f, offsetY = 0.f;   // tiles
	float distance = 2500.f;
	float pitch = -40.f;                   // degrees
	float yaw = 0.f;                       // degrees
	float orbit = 0.f;                     // degrees per second
	float panX = 0.f, panY = 0.f;          // tiles per second
	float hold = 8.f;                      // seconds
};

struct ShotResult
{
	std::string name;
	std::vector<double> frameMs;
	std::string screenshot;
	std::map<std::string, double> cpuTotalMs; // per system, summed over the measured frames
	size_t units = 0;                         // droids alive at the end of the shot
};

enum class Phase
{
	Idle,        // not active, or level not loaded yet
	Warmup,      // simulating at high speed until warmupGameSeconds
	Settle,      // camera placed, normal speed, not measuring yet
	Measure,     // camera moving per shot, recording frame times
	Capture,     // camera frozen at the shot's final position while the screenshot is taken
	Finishing,   // last screenshot requested, waiting for the save thread
	Done
};

bool active = false;
std::string benchName;
uint32_t seed = 12345;
std::string localAI = "nb_generic.js";
uint32_t warmupGameMs = 0;
uint32_t warmupSpeed = 16;
float settleSeconds = 2.f;
bool disableVsync = true;
bool revealMap = true;
std::vector<Shot> shots;
std::vector<ShotResult> results;

Phase phase = Phase::Idle;
size_t shotIndex = 0;
Clock::time_point phaseStart;
Clock::time_point lastFrame;
bool haveLastFrame = false;
int captureFrames = 0;
std::string outDir;      // PhysFS write-dir relative, ends with '/'
std::string runStamp;

float jsonFloat(const nlohmann::json &j, const char *key, float def)
{
	auto it = j.find(key);
	return (it != j.end() && it->is_number()) ? it->get<float>() : def;
}

void jsonPair(const nlohmann::json &j, const char *key, float &a, float &b)
{
	auto it = j.find(key);
	if (it != j.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number())
	{
		a = (*it)[0].get<float>();
		b = (*it)[1].get<float>();
	}
}

bool loadConfig()
{
	const std::string path = "tests/" + visualBenchTestConfig();
	auto doc = wzLoadJsonObjectFromFile(path);
	if (!doc.has_value() || !doc->contains("visualbench"))
	{
		debug(LOG_ERROR, "Visual bench: %s is missing or has no \"visualbench\" group", path.c_str());
		return false;
	}
	const nlohmann::json &vb = (*doc)["visualbench"];
	if (vb.contains("localAI") && vb["localAI"].is_string())
	{
		localAI = vb["localAI"].get<std::string>();
	}
	warmupGameMs = static_cast<uint32_t>(std::max(0.f, jsonFloat(vb, "warmupGameSeconds", 0.f)) * 1000.f);
	warmupSpeed = static_cast<uint32_t>(std::clamp(jsonFloat(vb, "warmupSpeed", 16.f), 1.f, 64.f));
	settleSeconds = std::max(0.f, jsonFloat(vb, "settleSeconds", 2.f));
	if (vb.contains("vsync") && vb["vsync"].is_boolean())
	{
		disableVsync = !vb["vsync"].get<bool>();
	}
	if (vb.contains("revealMap") && vb["revealMap"].is_boolean())
	{
		revealMap = vb["revealMap"].get<bool>();
	}

	shots.clear();
	if (vb.contains("shots") && vb["shots"].is_array())
	{
		for (const auto &s : vb["shots"])
		{
			Shot shot;
			shot.name = s.value("name", "shot" + std::to_string(shots.size()));
			shot.anchor = s.value("anchor", std::string("centre"));
			jsonPair(s, "offset", shot.offsetX, shot.offsetY);
			jsonPair(s, "pan", shot.panX, shot.panY);
			shot.distance = jsonFloat(s, "distance", shot.distance);
			shot.pitch = std::clamp(jsonFloat(s, "pitch", shot.pitch), static_cast<float>(MIN_PLAYER_X_ANGLE), static_cast<float>(MAX_PLAYER_X_ANGLE));
			shot.yaw = jsonFloat(s, "yaw", shot.yaw);
			shot.orbit = jsonFloat(s, "orbit", shot.orbit);
			shot.hold = std::max(1.f, jsonFloat(s, "hold", shot.hold));
			shots.push_back(shot);
		}
	}
	if (shots.empty())
	{
		debug(LOG_ERROR, "Visual bench: no shots defined in %s", path.c_str());
		return false;
	}
	return true;
}

/// World coordinates of a shot's anchor point.
Vector2f anchorWorld(const Shot &shot)
{
	const float mapW = static_cast<float>(world_coord(gameWorld.map.width));
	const float mapH = static_cast<float>(world_coord(gameWorld.map.height));
	Vector2f p(mapW / 2.f, mapH / 2.f);
	if (shot.anchor.rfind("start:", 0) == 0)
	{
		const int player = atoi(shot.anchor.c_str() + 6);
		if (player >= 0 && player < MAX_PLAYERS)
		{
			const Vector2i start = getPlayerStartPosition(NetPlay.players[player].position);
			p = Vector2f(static_cast<float>(start.x), static_cast<float>(start.y));
		}
	}
	else if (shot.anchor == "tile")
	{
		p = Vector2f(0.f, 0.f);
	}
	p.x += shot.offsetX * TILE_UNITS;
	p.y += shot.offsetY * TILE_UNITS;
	return p;
}

/// Places the camera for the current shot, t seconds into its measured period.
void applyCamera(const Shot &shot, float t)
{
	Vector2f p = anchorWorld(shot);
	p.x += shot.panX * TILE_UNITS * t;
	p.y += shot.panY * TILE_UNITS * t;

	const float mapW = static_cast<float>(world_coord(gameWorld.map.width));
	const float mapH = static_cast<float>(world_coord(gameWorld.map.height));
	p.x = std::clamp(p.x, static_cast<float>(TILE_UNITS), mapW - TILE_UNITS);
	p.y = std::clamp(p.y, static_cast<float>(TILE_UNITS), mapH - TILE_UNITS);

	if (getWarCamStatus())
	{
		setWarCamActive(false);
	}
	setPlayerPos(gameWorld.map, static_cast<SDWORD>(p.x), static_cast<SDWORD>(p.y));
	setViewDistance(shot.distance);

	float yaw = std::fmod(shot.yaw + shot.orbit * t, 360.f);
	if (yaw < 0.f)
	{
		yaw += 360.f;
	}
	playerPos.r.x = DEG(360 + static_cast<int>(std::lround(shot.pitch)));
	playerPos.r.y = static_cast<int>(std::lround(yaw * 8192.f / 45.f)); // DEG() for fractional degrees
	playerPos.r.z = 0;
}

std::string timestamp()
{
	time_t now = time(nullptr);
	struct tm t = getLocalTime(now);
	char buf[32];
	snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
	return buf;
}

double percentile(std::vector<double> v, double pct)
{
	if (v.empty())
	{
		return 0.0;
	}
	std::sort(v.begin(), v.end());
	const size_t idx = std::min(v.size() - 1, static_cast<size_t>(std::ceil(pct / 100.0 * v.size())) - 1);
	return v[idx];
}

nlohmann::json statsFor(const std::vector<double> &frameMs)
{
	nlohmann::json s = nlohmann::json::object();
	double total = 0.0;
	for (double ms : frameMs)
	{
		total += ms;
	}
	const double avgMs = frameMs.empty() ? 0.0 : total / frameMs.size();
	const double p99 = percentile(frameMs, 99.0);
	s["frames"] = frameMs.size();
	s["avgFrameMs"] = avgMs;
	s["avgFps"] = avgMs > 0.0 ? 1000.0 / avgMs : 0.0;
	s["p99FrameMs"] = p99;
	s["onePercentLowFps"] = p99 > 0.0 ? 1000.0 / p99 : 0.0;
	s["maxFrameMs"] = frameMs.empty() ? 0.0 : *std::max_element(frameMs.begin(), frameMs.end());
	return s;
}

void writeResults()
{
	nlohmann::json out = nlohmann::json::object();
	out["benchmark"] = benchName;
	out["timestamp"] = runStamp;
	out["version"] = version_getFormattedVersionString(false);
	out["renderer"] = gfx_api::context::get().getFormattedRendererInfoString();
	out["resolution"] = std::to_string(screenWidth) + "x" + std::to_string(screenHeight);
	out["seed"] = seed;
	out["warmupGameSeconds"] = warmupGameMs / 1000;
	out["vsync"] = !disableVsync;
	out["hdr"] = gfx_api::context::get().hdrActive();
	out["bloomIntensity"] = gfx_api::context::get().bloomIntensity();
	out["colourGrade"] = colour_grade::currentName();
	out["colourGradeStrength"] = colour_grade::currentStrength();
	out["environment"] = environment_light::currentName();
	out["taa"] = gfx_api::context::get().taaActive();
	out["revealMap"] = revealMap;

	std::vector<double> all;
	nlohmann::json shotList = nlohmann::json::array();
	for (const auto &r : results)
	{
		nlohmann::json s = statsFor(r.frameMs);
		s["name"] = r.name;
		s["screenshot"] = r.screenshot;
		s["units"] = r.units;
		nlohmann::json cpu = nlohmann::json::object();
		for (const auto& c : r.cpuTotalMs)
		{
			cpu[c.first] = r.frameMs.empty() ? 0.0 : c.second / static_cast<double>(r.frameMs.size()); // average ms per frame
		}
		s["cpuMsPerFrame"] = cpu;
		shotList.push_back(s);
		all.insert(all.end(), r.frameMs.begin(), r.frameMs.end());
	}
	out["shots"] = shotList;
	out["overall"] = statsFor(all);

	const std::string path = outDir + "results.json";
	const std::string dumped = out.dump(2);
	PHYSFS_file *fileHandle = PHYSFS_openWrite(path.c_str());
	if (!fileHandle)
	{
		debug(LOG_ERROR, "Visual bench: failed to open %s for writing: %s", path.c_str(), WZ_PHYSFS_getLastError());
		return;
	}
	WZ_PHYSFS_writeBytes(fileHandle, dumped.c_str(), static_cast<PHYSFS_uint32>(dumped.size()));
	PHYSFS_close(fileHandle);

	const char *writeDir = PHYSFS_getWriteDir();
	debug(LOG_INFO, "Visual bench complete: %s%s", writeDir ? writeDir : "", path.c_str());
	fprintf(stdout, "WZBENCH: results %s%s\n", writeDir ? writeDir : "", path.c_str());
	fprintf(stdout, "WZBENCH: overall avg %.1f fps, 1%% low %.1f fps\n",
	        out["overall"]["avgFps"].get<double>(), out["overall"]["onePercentLowFps"].get<double>());
	fflush(stdout);
}

void enterPhase(Phase p)
{
	phase = p;
	phaseStart = Clock::now();
}

float secondsInPhase()
{
	return std::chrono::duration<float>(Clock::now() - phaseStart).count();
}

void startShot(size_t index)
{
	shotIndex = index;
	gameTimeSetMod(Rational(1));
	applyCamera(shots[shotIndex], 0.f);
	enterPhase(Phase::Settle);
}

} // anonymous namespace

void visualBenchAddCpuMs(const char* system, double ms)
{
	if (active && phase == Phase::Measure && !results.empty())
	{
		results.back().cpuTotalMs[system] += ms;
	}
}

void visualBenchSelect(const std::string &name)
{
	active = true;
	benchName = name;
}

bool visualBenchActive()
{
	return active;
}

std::string visualBenchTestConfig()
{
	return "visualbench_" + benchName + ".json";
}

uint32_t visualBenchSeed()
{
	return seed;
}

void visualBenchLevelLoaded()
{
	if (!active || phase != Phase::Idle)
	{
		return;
	}
	if (!loadConfig())
	{
		wzQuit(1);
		return;
	}

	// "localAI": "none" leaves the local player's units to the test's rules script (scripted battles)
	if (localAI != "none" && selectedPlayer < MAX_PLAYERS && !NetPlay.players[selectedPlayer].isSpectator)
	{
		jsAutogameSpecific(WzString::fromUtf8("multiplay/skirmish/" + localAI), selectedPlayer, AIDifficulty::DEFAULT);
	}

	runStamp = timestamp();
	outDir = "benchmarks/" + benchName + "_" + runStamp + "/";
	PHYSFS_mkdir("benchmarks");
	PHYSFS_mkdir(outDir.c_str());

	if (disableVsync)
	{
		// Measure real GPU/CPU headroom rather than the display refresh rate.
		// Not saved to the player's config.
		if (!gfx_api::context::get().setSwapInterval(gfx_api::context::swap_interval_mode::immediate, []() {}))
		{
			debug(LOG_WARNING, "Visual bench: could not disable vsync; frame rates will be capped by the display");
		}
	}
	if (revealMap)
	{
		// Whole map visible, so every shot shows terrain, structures and units
		// rather than unexplored fog. The local player is an AI in this mode.
		enableGodMode();
	}

	results.clear();
	haveLastFrame = false;
	gameTimeSetMod(Rational(static_cast<int>(warmupSpeed)));
	enterPhase(Phase::Warmup);
	debug(LOG_INFO, "Visual bench '%s': warm-up to %u s game time at x%u, then %zu shots",
	      benchName.c_str(), warmupGameMs / 1000, warmupSpeed, shots.size());
}

void visualBenchFrame()
{
	if (!active || phase == Phase::Idle || phase == Phase::Done)
	{
		return;
	}

	const Clock::time_point now = Clock::now();
	const double frameMs = haveLastFrame ? std::chrono::duration<double, std::milli>(now - lastFrame).count() : 0.0;
	lastFrame = now;
	haveLastFrame = true;

	switch (phase)
	{
		case Phase::Warmup:
			// Keep the camera on the first shot so the warm-up is also visible.
			applyCamera(shots[0], 0.f);
			if (gameTime >= warmupGameMs)
			{
				startShot(0);
			}
			break;

		case Phase::Settle:
			applyCamera(shots[shotIndex], 0.f);
			if (secondsInPhase() >= settleSeconds)
			{
				ShotResult r;
				r.name = shots[shotIndex].name;
				results.push_back(r);
				enterPhase(Phase::Measure);
			}
			break;

		case Phase::Measure:
		{
			const Shot &shot = shots[shotIndex];
			const float t = secondsInPhase();
			applyCamera(shot, t);
			results.back().frameMs.push_back(frameMs);
			if (t >= shot.hold)
			{
				size_t units = 0;
				for (unsigned player = 0; player < MAX_PLAYERS; ++player)
				{
					units += gameWorld.objects.droids[player].size();
				}
				results.back().units = units;
				char name[64];
				snprintf(name, sizeof(name), "%02zu_%s.png", shotIndex + 1, shot.name.c_str());
				results.back().screenshot = name;
				screenDumpToDiskNamed((outDir + name).c_str(), true);
				captureFrames = 0;
				enterPhase(Phase::Capture);
			}
			break;
		}

		case Phase::Capture:
			// The screenshot is grabbed at the end of the next rendered frame, so hold the
			// camera still for a few frames before moving to the next shot.
			applyCamera(shots[shotIndex], shots[shotIndex].hold);
			if (++captureFrames >= 3)
			{
				if (shotIndex + 1 < shots.size())
				{
					startShot(shotIndex + 1);
				}
				else
				{
					writeResults();
					enterPhase(Phase::Finishing);
				}
			}
			break;

		case Phase::Finishing:
			// Give the background thread time to encode and save the last screenshot.
			if (secondsInPhase() >= 3.f)
			{
				phase = Phase::Done;
				wzQuit(0);
			}
			break;

		default:
			break;
	}
}
