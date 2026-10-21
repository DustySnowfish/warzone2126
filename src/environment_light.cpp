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
/** @file environment_light.cpp
 * Image-based lighting environments, built on the CPU from small description files.
 *
 * Environment file (all keys optional; colours are 0..255 sRGB):
 *   {
 *     "version": 1,
 *     "name": "Arizona",
 *     "zenith":  [120, 150, 190],   sky straight up
 *     "horizon": [215, 170, 120],   sky at the horizon
 *     "ground":  [140, 95, 65],     light bounced from the ground
 *     "horizonSharpness": 3.0,      higher keeps the horizon colour in a thinner band
 *     "sunGlow": 0.5,               glow around the sun (the sun itself is direct light)
 *     "image": "environment/images/name.png",  optional panorama (2:1, lat-long)
 *     "imageMix": 1.0,              blend of the panorama over the colours above
 *     "sky": {                      the visible sky dome (keys default to the lighting values)
 *       "zenith": [..], "horizon": [..], "horizonSharpness": 3.0,
 *       "sunGlow": 0.5, "sunDisc": 1.0, "brightness": 1.0,
 *       "bandFade": 0.3             how much of the top of the cloud band fades into the dome
 *     },
 *     "haze": {                     distant terrain fades into the sky behind it (needs HDR)
 *       "start": 0.55, "end": 0.92, fractions of the drawn area (from the camera pivot)
 *       "strength": 1.0
 *     }
 *   }
 *
 * Output texture (matches pbr.glsl): 64 x 32 lat-long levels stacked vertically, RGBM
 * encoded (range 8): 6 reflection levels for roughness 0, 0.2 .. 1, then one diffuse
 * irradiance level. The environment is scaled so the light from straight up has unit
 * brightness; the shader applies the engine's ambient level on top.
 */

#include "environment_light.h"

#include "levels.h"
#include "objectdef.h"
#include "lighting.h"
#include "map.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/piedef.h"
#include "lib/ivis_opengl/pietypes.h"
#include "lib/ivis_opengl/png_util.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/pielight_convert.h"

#include "display3d_render_internal.h"
#include "display3d_render_graph.h"
#include "display3d.h"


#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <cmath>
#include <vector>

#include "lib/gamelib/gtime.h"

#include <nlohmann/json.hpp>

namespace environment_light
{

namespace
{

constexpr int kOutWidth = 64;
constexpr int kOutHeight = 32;
constexpr int kSpecularLevels = 6;
constexpr int kLevels = kSpecularLevels + 1;
constexpr float kRgbmRange = 8.f;
constexpr float kPi = 3.14159265f;

struct Rgb
{
	float r = 0.f, g = 0.f, b = 0.f;
	Rgb() = default;
	Rgb(float r_, float g_, float b_) : r(r_), g(g_), b(b_) {}
	Rgb operator+(const Rgb& o) const { return { r + o.r, g + o.g, b + o.b }; }
	Rgb operator*(float s) const { return { r * s, g * s, b * s }; }
	float luma() const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
};

struct Dir
{
	float x = 0.f, y = 0.f, z = 0.f;
};

Rgb mix(const Rgb& a, const Rgb& b, float t)
{
	return a * (1.f - t) + b * t;
}

float dot(const Dir& a, const Dir& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Dir normalise(Dir d)
{
	const float l = std::sqrt(dot(d, d));
	if (l > 1e-6f)
	{
		d.x /= l; d.y /= l; d.z /= l;
	}
	return d;
}

float srgbToLinear(float v)
{
	return std::pow(std::max(0.f, std::min(v, 1.f)), 2.2f);
}

/// Lat-long texel centre to direction; matches wzEnvUV in pbr.glsl (v = 0 is straight up).
Dir texelDirection(int x, int y, int width, int height)
{
	const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
	const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
	const float phi = (u - 0.5f) * 2.f * kPi;   // atan(z, x)
	const float theta = v * kPi;                 // acos(y)
	return { std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi) };
}

struct EnvParams
{
	std::string name = "none";
	Rgb zenith { srgbToLinear(120.f / 255.f), srgbToLinear(150.f / 255.f), srgbToLinear(190.f / 255.f) };
	Rgb horizon { srgbToLinear(200.f / 255.f), srgbToLinear(190.f / 255.f), srgbToLinear(175.f / 255.f) };
	Rgb ground { srgbToLinear(110.f / 255.f), srgbToLinear(100.f / 255.f), srgbToLinear(90.f / 255.f) };
	float horizonSharpness = 3.f;
	float sunGlow = 0.5f;
	std::string imageFile;
	float imageMix = 1.f;
	// Visible sky dome
	Rgb skyZenith, skyHorizon;
	float skyHorizonSharpness = 3.f;
	float skySunGlow = 0.5f;
	float skySunDisc = 1.f;
	float skyBrightness = 1.f;
	float skyBandFade = 0.3f;
	// Distance haze
	float hazeStart = 0.55f;
	float hazeEnd = 0.92f;
	float hazeStrength = 1.f;
	// Cloud shadows (off unless the file has a "clouds" block)
	float cloudCoverage = 0.45f;
	float cloudStrength = 0.f;
	float cloudSize = 2600.f;     // world units across one cloud cell
	float cloudHeight = 1800.f;   // world units above the camera
	float cloudSoftness = 0.12f;
	float cloudWindX = 40.f;      // world units per second
	float cloudWindY = 15.f;
	// Weather (Warzone 2126): off unless the file has a "weather" block
	WeatherConfig weather;
};

struct State
{
	gfx_api::texture* texture = nullptr;
	std::string key;
	std::string envFile;
	PHYSFS_sint64 envFileTime = 0;
	std::string imageFile;
	PHYSFS_sint64 imageFileTime = 0;
	std::string name = "none";
	std::chrono::steady_clock::time_point nextFileCheck;
	bool haveSky = false;
	EnvParams sky;
	Dir sunDir;
	Rgb sunColour;
	// Warzone 2126: a rebuild for a moved sun runs on a worker thread; the old texture stays until it is done
	std::future<std::shared_ptr<iV_Image>> pending;
};

State state;
// Warzone 2126 time of day: per-frame multipliers on the sky colours, and the sun colour for the environment
Rgb skyTintZenith { 1.f, 1.f, 1.f };
Rgb skyTintHorizon { 1.f, 1.f, 1.f };
Rgb timeOfDaySunColour { 1.f, 0.95f, 0.85f };
float weatherCloudBoost = 0.f; // Warzone 2126 weather: 0..1, more and darker cloud
float weatherHazeBoost = 0.f;  // Warzone 2126 weather: 0..1, haze closes in

const char* tilesetName(MAP_TILESET tileset)
{
	switch (tileset)
	{
	case MAP_TILESET::URBAN: return "urban";
	case MAP_TILESET::ROCKIES: return "rockies";
	case MAP_TILESET::ARIZONA: break;
	}
	return "arizona";
}

std::string baseMapName(std::string level)
{
	const size_t dash = level.rfind("-T");
	if (dash != std::string::npos && dash + 2 < level.size()
		&& std::all_of(level.begin() + static_cast<std::ptrdiff_t>(dash + 2), level.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
	{
		level.resize(dash);
	}
	return level;
}

PHYSFS_sint64 fileTime(const std::string& path)
{
	if (path.empty() || !PHYSFS_exists(path.c_str()))
	{
		return 0;
	}
	return WZ_PHYSFS_getLastModTime(path.c_str());
}

std::string findEnvFile()
{
	const char* level = getLevelName();
	if (level != nullptr && level[0] != '\0')
	{
		for (const std::string& mapName : { std::string(level), baseMapName(level) })
		{
			const std::string path = "environment/maps/" + mapName + ".json";
			if (PHYSFS_exists(path.c_str()))
			{
				return path;
			}
		}
	}
	const std::string path = std::string("environment/") + tilesetName(currentMapTileset) + ".json";
	return PHYSFS_exists(path.c_str()) ? path : std::string();
}

bool readFile(const std::string& path, std::vector<char>& out)
{
	PHYSFS_file* f = PHYSFS_openRead(path.c_str());
	if (f == nullptr)
	{
		return false;
	}
	const PHYSFS_sint64 len = PHYSFS_fileLength(f);
	if (len < 0 || len > (1 << 20))
	{
		PHYSFS_close(f);
		return false;
	}
	out.resize(static_cast<size_t>(len));
	const bool ok = len == 0 || WZ_PHYSFS_readBytes(f, out.data(), static_cast<PHYSFS_uint32>(len)) == len;
	PHYSFS_close(f);
	return ok;
}

float jsonFloat(const nlohmann::json& doc, const char* key, float fallback, float lo, float hi)
{
	auto it = doc.find(key);
	if (it == doc.end() || !it->is_number())
	{
		return fallback;
	}
	return std::max(lo, std::min(it->get<float>(), hi));
}

/// sRGB 0..255 triple to linear colour
Rgb jsonColour(const nlohmann::json& doc, const char* key, const Rgb& fallback)
{
	auto it = doc.find(key);
	if (it == doc.end() || !it->is_array() || it->size() != 3)
	{
		return fallback;
	}
	float c[3];
	for (size_t i = 0; i < 3; ++i)
	{
		if (!(*it)[i].is_number())
		{
			return fallback;
		}
		c[i] = srgbToLinear((*it)[i].get<float>() / 255.f);
	}
	return { c[0], c[1], c[2] };
}

bool parseEnv(const std::string& path, EnvParams& out)
{
	std::vector<char> data;
	if (!readFile(path, data))
	{
		debug(LOG_ERROR, "Environment: cannot read %s", path.c_str());
		return false;
	}
	nlohmann::json doc = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
	if (doc.is_discarded() || !doc.is_object())
	{
		debug(LOG_ERROR, "Environment: %s is not valid JSON", path.c_str());
		return false;
	}
	out = EnvParams();
	auto nameIt = doc.find("name");
	out.name = (nameIt != doc.end() && nameIt->is_string()) ? nameIt->get<std::string>() : path;
	out.zenith = jsonColour(doc, "zenith", out.zenith);
	out.horizon = jsonColour(doc, "horizon", out.horizon);
	out.ground = jsonColour(doc, "ground", out.ground);
	out.horizonSharpness = jsonFloat(doc, "horizonSharpness", 3.f, 0.25f, 16.f);
	out.sunGlow = jsonFloat(doc, "sunGlow", 0.5f, 0.f, 4.f);
	auto imageIt = doc.find("image");
	if (imageIt != doc.end() && imageIt->is_string())
	{
		out.imageFile = imageIt->get<std::string>();
	}
	out.imageMix = jsonFloat(doc, "imageMix", 1.f, 0.f, 1.f);
	out.skyZenith = out.zenith;
	out.skyHorizon = out.horizon;
	out.skyHorizonSharpness = out.horizonSharpness;
	out.skySunGlow = out.sunGlow;
	auto skyIt = doc.find("sky");
	if (skyIt != doc.end() && skyIt->is_object())
	{
		const nlohmann::json& sky = *skyIt;
		out.skyZenith = jsonColour(sky, "zenith", out.skyZenith);
		out.skyHorizon = jsonColour(sky, "horizon", out.skyHorizon);
		out.skyHorizonSharpness = jsonFloat(sky, "horizonSharpness", out.skyHorizonSharpness, 0.25f, 16.f);
		out.skySunGlow = jsonFloat(sky, "sunGlow", out.skySunGlow, 0.f, 4.f);
		out.skySunDisc = jsonFloat(sky, "sunDisc", 1.f, 0.f, 4.f);
		out.skyBrightness = jsonFloat(sky, "brightness", 1.f, 0.f, 4.f);
		out.skyBandFade = jsonFloat(sky, "bandFade", 0.3f, 0.f, 0.95f);
	}
	auto cloudsIt = doc.find("clouds");
	if (cloudsIt != doc.end() && cloudsIt->is_object())
	{
		const nlohmann::json& clouds = *cloudsIt;
		out.cloudCoverage = jsonFloat(clouds, "coverage", out.cloudCoverage, 0.f, 1.f);
		out.cloudStrength = jsonFloat(clouds, "strength", 0.35f, 0.f, 1.f);
		out.cloudSize = jsonFloat(clouds, "size", out.cloudSize, 200.f, 50000.f);
		out.cloudHeight = jsonFloat(clouds, "height", out.cloudHeight, 0.f, 20000.f);
		out.cloudSoftness = jsonFloat(clouds, "softness", out.cloudSoftness, 0.01f, 0.5f);
		auto windIt = clouds.find("wind");
		if (windIt != clouds.end() && windIt->is_array() && windIt->size() == 2 && (*windIt)[0].is_number() && (*windIt)[1].is_number())
		{
			out.cloudWindX = std::max(-2000.f, std::min(2000.f, (*windIt)[0].get<float>()));
			out.cloudWindY = std::max(-2000.f, std::min(2000.f, (*windIt)[1].get<float>()));
		}
	}
	auto weatherIt = doc.find("weather");
	if (weatherIt != doc.end() && weatherIt->is_object())
	{
		const nlohmann::json& w = *weatherIt;
		const auto typeIt = w.find("type");
		const std::string type = (typeIt != w.end() && typeIt->is_string()) ? typeIt->get<std::string>() : "";
		out.weather.type = type == "rain" ? WeatherConfig::Type::Rain : type == "snow" ? WeatherConfig::Type::Snow : type == "dust" ? WeatherConfig::Type::Dust : WeatherConfig::Type::None;
		auto range = [&](const char* key, float& lo, float& hi, float minV, float maxV) {
			auto it = w.find(key);
			if (it != w.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number())
			{
				lo = std::max(minV, std::min(maxV, (*it)[0].get<float>()));
				hi = std::max(lo, std::min(maxV, (*it)[1].get<float>()));
			}
		};
		range("clear", out.weather.clearMin, out.weather.clearMax, 10.f, 7200.f);
		range("storm", out.weather.stormMin, out.weather.stormMax, 10.f, 7200.f);
		out.weather.strength = jsonFloat(w, "strength", out.weather.strength, 0.f, 1.f);
		out.weather.startChance = jsonFloat(w, "startChance", out.weather.startChance, 0.f, 1.f);
	}
	auto hazeIt = doc.find("haze");
	if (hazeIt != doc.end() && hazeIt->is_object())
	{
		const nlohmann::json& haze = *hazeIt;
		out.hazeStart = jsonFloat(haze, "start", out.hazeStart, 0.f, 1.f);
		out.hazeEnd = jsonFloat(haze, "end", out.hazeEnd, 0.f, 1.f);
		out.hazeStrength = jsonFloat(haze, "strength", out.hazeStrength, 0.f, 1.f);
		out.hazeEnd = std::max(out.hazeEnd, out.hazeStart + 0.01f);
	}
	return true;
}

/// Radiance image in linear light
struct Radiance
{
	int width = 0, height = 0;
	std::vector<Rgb> texels;
	const Rgb& at(int x, int y) const { return texels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]; }
};

Rgb sampleImage(const iV_Image& image, const Dir& d)
{
	// Same lat-long layout as the output; nearest texel is enough at this size
	const float u = std::atan2(d.z, d.x) / (2.f * kPi) + 0.5f;
	const float v = std::acos(std::max(-1.f, std::min(d.y, 1.f))) / kPi;
	const int x = std::min(static_cast<int>(u * static_cast<float>(image.width())), static_cast<int>(image.width()) - 1);
	const int y = std::min(static_cast<int>(v * static_cast<float>(image.height())), static_cast<int>(image.height()) - 1);
	const unsigned char* p = image.bmp() + (static_cast<size_t>(y) * image.width() + static_cast<size_t>(x)) * image.channels();
	return { srgbToLinear(p[0] / 255.f), srgbToLinear(p[1] / 255.f), srgbToLinear(p[2] / 255.f) };
}

Radiance buildSource(const EnvParams& p, const Dir& sunDir, const Rgb& sunColour, const iV_Image* image)
{
	Radiance src;
	src.width = kOutWidth;
	src.height = kOutHeight;
	src.texels.resize(static_cast<size_t>(src.width * src.height));
	for (int y = 0; y < src.height; ++y)
	{
		for (int x = 0; x < src.width; ++x)
		{
			const Dir d = texelDirection(x, y, src.width, src.height);
			Rgb c;
			if (d.y >= 0.f)
			{
				c = mix(p.zenith, p.horizon, std::pow(1.f - d.y, p.horizonSharpness));
			}
			else
			{
				c = mix(p.horizon, p.ground, std::min(1.f, -d.y * 4.f));
			}
			const float s = std::max(0.f, dot(d, sunDir));
			c = c + sunColour * (p.sunGlow * std::pow(s, 8.f));
			if (image != nullptr && p.imageMix > 0.f)
			{
				c = mix(c, sampleImage(*image, d), p.imageMix);
			}
			src.texels[static_cast<size_t>(y * src.width + x)] = c;
		}
	}
	return src;
}

float ggx(float NdotH, float roughness)
{
	const float a = roughness * roughness;
	const float a2 = a * a;
	const float d = NdotH * NdotH * (a2 - 1.f) + 1.f;
	return a2 / std::max(kPi * d * d, 1e-6f);
}

void encodeRgbm(const Rgb& c, unsigned char* out)
{
	const float peak = std::max(c.r, std::max(c.g, c.b)) / kRgbmRange;
	float m = std::max(1.f / 255.f, std::min(peak, 1.f));
	m = std::ceil(m * 255.f) / 255.f;
	const float scale = 1.f / (m * kRgbmRange);
	out[0] = static_cast<unsigned char>(std::lround(std::min(c.r * scale, 1.f) * 255.f));
	out[1] = static_cast<unsigned char>(std::lround(std::min(c.g * scale, 1.f) * 255.f));
	out[2] = static_cast<unsigned char>(std::lround(std::min(c.b * scale, 1.f) * 255.f));
	out[3] = static_cast<unsigned char>(std::lround(m * 255.f));
}

void buildTexture(const Radiance& src, iV_Image& image)
{
	const int n = src.width * src.height;
	std::vector<Dir> dirs(static_cast<size_t>(n));
	std::vector<float> solidAngle(static_cast<size_t>(n));
	for (int y = 0; y < src.height; ++y)
	{
		const float theta = (static_cast<float>(y) + 0.5f) / static_cast<float>(src.height) * kPi;
		const float sa = (2.f * kPi / static_cast<float>(src.width)) * (kPi / static_cast<float>(src.height)) * std::sin(theta);
		for (int x = 0; x < src.width; ++x)
		{
			dirs[static_cast<size_t>(y * src.width + x)] = texelDirection(x, y, src.width, src.height);
			solidAngle[static_cast<size_t>(y * src.width + x)] = sa;
		}
	}

	// Scale so the diffuse light on an upward-facing surface has unit brightness
	Rgb upIrradiance;
	float upWeight = 0.f;
	for (int i = 0; i < n; ++i)
	{
		const float w = std::max(0.f, dirs[static_cast<size_t>(i)].y) * solidAngle[static_cast<size_t>(i)];
		upIrradiance = upIrradiance + src.texels[static_cast<size_t>(i)] * w;
		upWeight += w;
	}
	const float upLuma = upWeight > 0.f ? (upIrradiance * (1.f / upWeight)).luma() : 0.f;
	const float unitScale = upLuma > 1e-5f ? 1.f / upLuma : 1.f;

	image.allocate(kOutWidth, kOutHeight * kLevels, 4, false);
	unsigned char* px = image.bmp_w();
	for (int level = 0; level < kLevels; ++level)
	{
		const bool irradiance = level == kSpecularLevels;
		const float roughness = irradiance ? 1.f : static_cast<float>(level) / static_cast<float>(kSpecularLevels - 1);
		for (int y = 0; y < kOutHeight; ++y)
		{
			for (int x = 0; x < kOutWidth; ++x)
			{
				const Dir R = texelDirection(x, y, kOutWidth, kOutHeight);
				Rgb sum;
				float weightSum = 0.f;
				if (!irradiance && level == 0)
				{
					sum = src.at(x * src.width / kOutWidth, y * src.height / kOutHeight);
					weightSum = 1.f;
				}
				else
				{
					for (int i = 0; i < n; ++i)
					{
						const Dir& L = dirs[static_cast<size_t>(i)];
						const float NdotL = dot(R, L);
						if (NdotL <= 0.f)
						{
							continue;
						}
						float w = NdotL * solidAngle[static_cast<size_t>(i)];
						if (!irradiance)
						{
							// GGX lobe around the reflection direction (assumes N = V = R)
							const Dir H = normalise({ R.x + L.x, R.y + L.y, R.z + L.z });
							w *= ggx(std::max(0.f, dot(R, H)), roughness);
						}
						sum = sum + src.texels[static_cast<size_t>(i)] * w;
						weightSum += w;
					}
				}
				const Rgb c = weightSum > 0.f ? sum * (unitScale / weightSum) : Rgb();
				encodeRgbm(c, px + (static_cast<size_t>(level * kOutHeight + y) * kOutWidth + static_cast<size_t>(x)) * 4);
			}
		}
	}
}

void rebuild(const std::string& key, const std::string& envFile, const Dir& sunDir, const Rgb& sunColour, bool async)
{
	EnvParams params;
	const bool haveParams = !envFile.empty() && parseEnv(envFile, params);
	iV_Image panorama;
	bool havePanorama = false;
	if (haveParams && !params.imageFile.empty())
	{
		havePanorama = iV_loadImage_PNG2(params.imageFile.c_str(), panorama, true);
		if (!havePanorama)
		{
			debug(LOG_ERROR, "Environment: cannot load image %s", params.imageFile.c_str());
		}
	}

	state.key = key;
	state.envFile = envFile;
	state.envFileTime = fileTime(envFile);
	state.imageFile = haveParams ? params.imageFile : std::string();
	state.imageFileTime = fileTime(state.imageFile);
	state.name = haveParams ? params.name : std::string("none");
	state.haveSky = haveParams;
	state.sky = params;
	state.sunDir = sunDir;
	state.sunColour = sunColour;

	if (!haveParams)
	{
		// No environment for this map: PBR models fall back to flat ambient light
		delete state.texture;
		state.texture = nullptr;
		pie_SetEnvironmentMap(nullptr);
		return;
	}

	if (async)
	{
		// Only the sun moved: build off the main thread so a moving sun never stalls a frame
		auto pano = std::make_shared<iV_Image>(std::move(panorama));
		state.pending = std::async(std::launch::async, [params, sunDir, sunColour, pano, havePanorama]() {
			const Radiance src = buildSource(params, sunDir, sunColour, havePanorama ? pano.get() : nullptr);
			auto image = std::make_shared<iV_Image>();
			buildTexture(src, *image);
			return image;
		});
		return;
	}
	const auto start = std::chrono::steady_clock::now();
	const Radiance src = buildSource(params, sunDir, sunColour, havePanorama ? &panorama : nullptr);
	iV_Image image;
	buildTexture(src, image);
	if (state.texture == nullptr)
	{
		state.texture = gfx_api::context::get().create_texture(1, kOutWidth, kOutHeight * kLevels, gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8, "environmentLight");
	}
	if (state.texture != nullptr && !state.texture->upload(0, image))
	{
		debug(LOG_ERROR, "Environment: texture upload failed");
	}
	pie_SetEnvironmentMap(state.texture);
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	debug(LOG_INFO, "Environment: %s (%s), built in %d ms", state.name.c_str(), envFile.c_str(), static_cast<int>(ms));
}

} // anonymous namespace

void update()
{
	const Vector3f sun = getTheSun();
	// The shaders light from the direction opposite the sun position vector
	const Dir sunDir = normalise(Dir { -sun.x, -sun.y, -sun.z });
	const char* level = getLevelName();
	char sunKey[64];
	snprintf(sunKey, sizeof(sunKey), "%.2f,%.2f,%.2f|%.1f,%.1f", static_cast<double>(sunDir.x), static_cast<double>(sunDir.y), static_cast<double>(sunDir.z),
	         static_cast<double>(timeOfDaySunColour.g), static_cast<double>(timeOfDaySunColour.b));
	const std::string key = std::string(level ? level : "") + "|" + tilesetName(currentMapTileset) + "|" + sunKey;

	const auto now = std::chrono::steady_clock::now();
	bool changed = key != state.key;
	if (!changed && now >= state.nextFileCheck)
	{
		state.nextFileCheck = now + std::chrono::seconds(1);
		const std::string envFile = findEnvFile();
		changed = envFile != state.envFile
			|| fileTime(envFile) != state.envFileTime
			|| fileTime(state.imageFile) != state.imageFileTime;
	}
	// A finished background rebuild: upload it
	if (state.pending.valid() && state.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		std::shared_ptr<iV_Image> image = state.pending.get();
		if (image && state.haveSky)
		{
			if (state.texture == nullptr)
			{
				state.texture = gfx_api::context::get().create_texture(1, kOutWidth, kOutHeight * kLevels, gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8, "environmentLight");
			}
			if (state.texture != nullptr && state.texture->upload(0, *image))
			{
				pie_SetEnvironmentMap(state.texture);
			}
		}
	}
	if (changed && !state.pending.valid())
	{
		// The sun moving alone (time of day) is rebuilt in the background; a new map or file at once
		const std::string oldPrefix = state.key.substr(0, state.key.rfind('|'));
		const std::string newPrefix = key.substr(0, key.rfind('|'));
		const bool sunOnly = !state.key.empty() && oldPrefix == newPrefix && state.texture != nullptr;
		rebuild(key, findEnvFile(), sunDir, timeOfDaySunColour, sunOnly);
		state.nextFileCheck = now + std::chrono::seconds(1);
	}
}

bool drawSky(const glm::mat4& projectionMatrix, const glm::mat4& viewMatrix)
{
	if (!state.haveSky)
	{
		return false;
	}
	const EnvParams& p = state.sky;
	const glm::mat4 rotationOnly = glm::mat4(glm::mat3(viewMatrix));
	gfx_api::constant_buffer_type<SHADER_SKY_DOME> constants {};
	constants.invViewProj = glm::inverse(projectionMatrix * rotationOnly);
	constants.zenith = glm::vec4(p.skyZenith.r * skyTintZenith.r, p.skyZenith.g * skyTintZenith.g, p.skyZenith.b * skyTintZenith.b, 0.f);
	constants.horizon = glm::vec4(p.skyHorizon.r * skyTintHorizon.r, p.skyHorizon.g * skyTintHorizon.g, p.skyHorizon.b * skyTintHorizon.b, 0.f);
	constants.ground = glm::vec4(p.ground.r * skyTintHorizon.r, p.ground.g * skyTintHorizon.g, p.ground.b * skyTintHorizon.b, 0.f);
	constants.sunDir = glm::vec4(state.sunDir.x, state.sunDir.y, state.sunDir.z, 0.f);
	constants.sunColour = glm::vec4(state.sunColour.r, state.sunColour.g, state.sunColour.b, 0.f);
	constants.params = glm::vec4(p.skyHorizonSharpness, p.skySunGlow, p.skySunDisc, p.skyBrightness);
	const RENDER_STATE renderState = getCurrentRenderState();
	const glm::vec4 fog = pielightToRGBAVec4(renderState.fogColour);
	constants.fogColour = glm::vec4(fog.r, fog.g, fog.b, renderState.fogEnabled ? 1.f : 0.f);
	display3d_drawFullscreenTriangle<gfx_api::SkyDomePSO>(constants);
	return true;
}

bool hazeActive()
{
	return state.haveSky && (state.sky.hazeStrength > 0.f || state.sky.cloudStrength > 0.f || weatherCloudBoost > 0.f || weatherHazeBoost > 0.f) && gfx_api::context::get().hdrActive();
}

void fillHazeConstants(gfx_api::constant_buffer_type<SHADER_TONEMAP>& constants)
{
	constants.haze = glm::vec4(0.f);
	constants.cloud = glm::vec4(0.f);
	if (!hazeActive() || !pie_IsInGame3DFrameContextReady())
	{
		return;
	}
	const InGame3DFrameContext& fc = pie_GetInGame3DFrameContext();
	const EnvParams& p = state.sky;
	constants.invViewProj = glm::inverse(fc.perspectiveMatrix * glm::mat4(glm::mat3(fc.viewMatrix)));
	constants.hazeZenith = glm::vec4(p.skyZenith.r * skyTintZenith.r, p.skyZenith.g * skyTintZenith.g, p.skyZenith.b * skyTintZenith.b, 0.f);
	constants.hazeHorizon = glm::vec4(p.skyHorizon.r * skyTintHorizon.r, p.skyHorizon.g * skyTintHorizon.g, p.skyHorizon.b * skyTintHorizon.b, 0.f);
	constants.hazeGround = glm::vec4(p.ground.r * skyTintHorizon.r, p.ground.g * skyTintHorizon.g, p.ground.b * skyTintHorizon.b, 0.f);
	constants.hazeSunDir = glm::vec4(state.sunDir.x, state.sunDir.y, state.sunDir.z, 0.f);
	constants.hazeSunColour = glm::vec4(state.sunColour.r, state.sunColour.g, state.sunColour.b, p.skySunGlow);
	// The camera pivot in render space is (x, -z) of the game position
	const glm::vec2 pivot(static_cast<float>(playerPos.p.x), -static_cast<float>(playerPos.p.z));
	constants.hazeShape = glm::vec4(p.skyHorizonSharpness, p.skyBrightness, pivot.x - fc.cameraPos.x, pivot.y - fc.cameraPos.z);
	// The classic renderer draws a square of terrain around the pivot; haze completes before its edge
	const float halfDrawn = static_cast<float>(world_coord(std::min(visibleTiles.x, visibleTiles.y) / 2 - 2));
	// Weather pulls the haze in closer and makes it stronger
	const float hazeNear = p.hazeStart * (1.f - 0.75f * weatherHazeBoost);
	const float hazeFar = std::max(hazeNear + 0.01f, p.hazeEnd * (1.f - 0.45f * weatherHazeBoost));
	constants.haze = glm::vec4(halfDrawn * hazeNear, halfDrawn * hazeFar, std::max(p.hazeStrength, weatherHazeBoost), 0.f);
	const float cloudStrength = std::min(0.85f, p.cloudStrength + 0.4f * weatherCloudBoost);
	const float cloudCoverage = std::min(0.95f, p.cloudCoverage + 0.45f * weatherCloudBoost);
	if (cloudStrength > 0.f && state.sunDir.y > 0.f)
	{
		// Drift with display time; wrap far beyond any map so float precision holds
		const double seconds = static_cast<double>(graphicsTime) / 1000.0;
		const double wrap = static_cast<double>(p.cloudSize) * 512.0;
		const float driftX = static_cast<float>(std::fmod(seconds * p.cloudWindX, wrap));
		const float driftY = static_cast<float>(std::fmod(seconds * p.cloudWindY, wrap));
		constants.cloud = glm::vec4(cloudCoverage, cloudStrength, 1.f / p.cloudSize, p.cloudHeight);
		constants.cloudOffset = glm::vec4(fc.cameraPos.x - driftX, fc.cameraPos.z - driftY, p.cloudSoftness, 0.f);
	}
}

void setTimeOfDay(const glm::vec3& zenithTint, const glm::vec3& horizonTint, const glm::vec3& sunColour)
{
	skyTintZenith = Rgb { zenithTint.r, zenithTint.g, zenithTint.b };
	skyTintHorizon = Rgb { horizonTint.r, horizonTint.g, horizonTint.b };
	// Rounded so tiny colour changes do not trigger environment rebuilds
	auto q = [](float v) { return std::round(v * 20.f) / 20.f; };
	timeOfDaySunColour = Rgb { q(sunColour.r), q(sunColour.g), q(sunColour.b) };
}

const WeatherConfig& weatherConfig()
{
	static const WeatherConfig none;
	return state.haveSky ? state.sky.weather : none;
}

void setWeatherShading(float cloudBoost, float hazeBoost)
{
	weatherCloudBoost = std::max(0.f, std::min(1.f, cloudBoost));
	weatherHazeBoost = std::max(0.f, std::min(1.f, hazeBoost));
}

float skyBandFade()
{
	return state.haveSky ? state.sky.skyBandFade : 0.f;
}

const std::string& currentName()
{
	return state.name;
}

void shutdown()
{
	if (state.pending.valid())
	{
		state.pending.wait();
	}
	pie_SetEnvironmentMap(nullptr);
	delete state.texture;
	state = State();
}

} // namespace environment_light
