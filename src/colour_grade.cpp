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
/** @file colour_grade.cpp
 * Per-map colour grading: grade files baked into a LUT for the tonemap pass.
 *
 * Grade file (all keys optional):
 *   {
 *     "version": 1,
 *     "name": "Arizona",
 *     "strength": 1.0,          blend of the whole grade, 0..1
 *     "exposure": 0.0,          stops
 *     "temperature": 0.0,       -1 cool (blue) .. +1 warm (orange)
 *     "tint": 0.0,              -1 green .. +1 magenta
 *     "contrast": 1.0,          S-curve around mid grey; 1 = unchanged
 *     "saturation": 1.0,        0 = grey, 1 = unchanged
 *     "lift":  [0, 0, 0],       added to the shadows (fades out towards white)
 *     "gamma": [1, 1, 1],       midtones; above 1 brightens
 *     "gain":  [1, 1, 1],       multiplies the highlights
 *     "lut": "colourgrade/luts/name.png",  optional LUT image applied last
 *     "lutStrength": 1.0
 *   }
 * Steps run in the order listed. A LUT image is a strip of N squares of N x N pixels
 * (N x N wide, N high), red across each square, green down, blue picking the square;
 * tools/assets/make_neutral_lut.py writes a neutral one to grade in an image editor.
 */

#include "colour_grade.h"

#include "levels.h"
#include "map.h"
#include "warzoneconfig.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/pietypes.h"
#include "lib/ivis_opengl/png_util.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

namespace colour_grade
{

namespace
{

using Rgb = std::array<float, 3>;

struct GradeParams
{
	std::string name = "none";
	float strength = 1.f;
	float exposure = 0.f;
	float temperature = 0.f;
	float tint = 0.f;
	float contrast = 1.f;
	float saturation = 1.f;
	Rgb lift = { 0.f, 0.f, 0.f };
	Rgb gamma = { 1.f, 1.f, 1.f };
	Rgb gain = { 1.f, 1.f, 1.f };
	std::string lutFile;
	float lutStrength = 1.f;
};

struct LutImage
{
	int size = 0;
	std::vector<float> rgb; // size^3 * 3, index ((b * size + g) * size + r) * 3
};

struct State
{
	gfx_api::texture* lut = nullptr;
	std::string key;            // what the current LUT was built for
	std::string gradeFile;      // file in use ("" if none)
	PHYSFS_sint64 gradeFileTime = 0;
	PHYSFS_sint64 lutFileTime = 0;
	std::string lutFile;
	float strength = 0.f;
	std::string name = "none";
	std::chrono::steady_clock::time_point nextFileCheck;
};

State state;

const char* tilesetGradeName(MAP_TILESET tileset)
{
	switch (tileset)
	{
	case MAP_TILESET::URBAN: return "urban";
	case MAP_TILESET::ROCKIES: return "rockies";
	case MAP_TILESET::ARIZONA: break;
	}
	return "arizona";
}

/// Map name without the "-T1" style tech-level suffix that skirmish levels carry.
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

/// The grade file for the current map: per-map first, then per-tileset. "" if none.
std::string findGradeFile()
{
	const char* level = getLevelName();
	if (level != nullptr && level[0] != '\0')
	{
		for (const std::string& mapName : { std::string(level), baseMapName(level) })
		{
			const std::string path = "colourgrade/maps/" + mapName + ".json";
			if (PHYSFS_exists(path.c_str()))
			{
				return path;
			}
		}
	}
	const std::string path = std::string("colourgrade/") + tilesetGradeName(currentMapTileset) + ".json";
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

Rgb jsonRgb(const nlohmann::json& doc, const char* key, Rgb fallback, float lo, float hi)
{
	auto it = doc.find(key);
	if (it == doc.end())
	{
		return fallback;
	}
	if (it->is_number())
	{
		const float v = std::max(lo, std::min(it->get<float>(), hi));
		return { v, v, v };
	}
	if (it->is_array() && it->size() == 3)
	{
		Rgb out = fallback;
		for (size_t i = 0; i < 3; ++i)
		{
			if ((*it)[i].is_number())
			{
				out[i] = std::max(lo, std::min((*it)[i].get<float>(), hi));
			}
		}
		return out;
	}
	return fallback;
}

bool parseGrade(const std::string& path, GradeParams& out)
{
	std::vector<char> data;
	if (!readFile(path, data))
	{
		debug(LOG_ERROR, "Colour grade: cannot read %s", path.c_str());
		return false;
	}
	nlohmann::json doc = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
	if (doc.is_discarded() || !doc.is_object())
	{
		debug(LOG_ERROR, "Colour grade: %s is not valid JSON", path.c_str());
		return false;
	}
	out = GradeParams();
	auto nameIt = doc.find("name");
	out.name = (nameIt != doc.end() && nameIt->is_string()) ? nameIt->get<std::string>() : path;
	out.strength = jsonFloat(doc, "strength", 1.f, 0.f, 1.f);
	out.exposure = jsonFloat(doc, "exposure", 0.f, -4.f, 4.f);
	out.temperature = jsonFloat(doc, "temperature", 0.f, -1.f, 1.f);
	out.tint = jsonFloat(doc, "tint", 0.f, -1.f, 1.f);
	out.contrast = jsonFloat(doc, "contrast", 1.f, 0.25f, 4.f);
	out.saturation = jsonFloat(doc, "saturation", 1.f, 0.f, 4.f);
	out.lift = jsonRgb(doc, "lift", out.lift, -0.5f, 0.5f);
	out.gamma = jsonRgb(doc, "gamma", out.gamma, 0.1f, 4.f);
	out.gain = jsonRgb(doc, "gain", out.gain, 0.f, 4.f);
	auto lutIt = doc.find("lut");
	if (lutIt != doc.end() && lutIt->is_string())
	{
		out.lutFile = lutIt->get<std::string>();
	}
	out.lutStrength = jsonFloat(doc, "lutStrength", 1.f, 0.f, 1.f);
	return true;
}

bool loadLutImage(const std::string& path, LutImage& out)
{
	iV_Image image;
	if (!iV_loadImage_PNG2(path.c_str(), image, true))
	{
		debug(LOG_ERROR, "Colour grade: cannot load LUT image %s", path.c_str());
		return false;
	}
	const unsigned int n = image.height();
	if (n < 2 || n > 64 || image.width() != n * n || image.channels() != 4)
	{
		debug(LOG_ERROR, "Colour grade: %s must be N*N x N pixels (for example 1024 x 32)", path.c_str());
		return false;
	}
	out.size = static_cast<int>(n);
	out.rgb.resize(static_cast<size_t>(n) * n * n * 3);
	const unsigned char* px = image.bmp();
	for (unsigned int b = 0; b < n; ++b)
	{
		for (unsigned int g = 0; g < n; ++g)
		{
			for (unsigned int r = 0; r < n; ++r)
			{
				const unsigned char* p = px + (static_cast<size_t>(g) * image.width() + b * n + r) * 4;
				float* o = &out.rgb[((static_cast<size_t>(b) * n + g) * n + r) * 3];
				for (int c = 0; c < 3; ++c)
				{
					o[c] = static_cast<float>(p[c]) / 255.f;
				}
			}
		}
	}
	return true;
}

Rgb sampleLutImage(const LutImage& lut, const Rgb& c)
{
	const int n = lut.size;
	const float maxIndex = static_cast<float>(n - 1);
	int i0[3], i1[3];
	float f[3];
	for (int k = 0; k < 3; ++k)
	{
		const float x = std::max(0.f, std::min(c[k], 1.f)) * maxIndex;
		i0[k] = std::min(static_cast<int>(x), n - 1);
		i1[k] = std::min(i0[k] + 1, n - 1);
		f[k] = x - static_cast<float>(i0[k]);
	}
	auto at = [&](int r, int g, int b, int ch) {
		return lut.rgb[((static_cast<size_t>(b) * n + static_cast<size_t>(g)) * n + static_cast<size_t>(r)) * 3 + static_cast<size_t>(ch)];
	};
	Rgb out {};
	for (int ch = 0; ch < 3; ++ch)
	{
		float acc = 0.f;
		for (int corner = 0; corner < 8; ++corner)
		{
			const int r = (corner & 1) ? i1[0] : i0[0];
			const int g = (corner & 2) ? i1[1] : i0[1];
			const int b = (corner & 4) ? i1[2] : i0[2];
			const float w = ((corner & 1) ? f[0] : 1.f - f[0]) * ((corner & 2) ? f[1] : 1.f - f[1]) * ((corner & 4) ? f[2] : 1.f - f[2]);
			acc += w * at(r, g, b, ch);
		}
		out[ch] = acc;
	}
	return out;
}

float clamp01(float v)
{
	return std::max(0.f, std::min(v, 1.f));
}

/// S-curve contrast in display space, pivoting on mid grey; keeps 0 and 1 fixed.
float applyContrast(float v, float contrast)
{
	constexpr float kPivot = 0.46f; // 18% grey, display-encoded
	v = clamp01(v);
	if (v < kPivot)
	{
		return kPivot * std::pow(v / kPivot, contrast);
	}
	return 1.f - (1.f - kPivot) * std::pow((1.f - v) / (1.f - kPivot), contrast);
}

/// Grade one display-encoded colour (0..1).
Rgb gradeColour(const GradeParams& p, const LutImage* lutImage, Rgb c)
{
	constexpr float kGamma = 2.2f;
	// Exposure and white balance in linear light
	Rgb wb = { 1.f + 0.2f * p.temperature, 1.f - 0.15f * p.tint, 1.f - 0.2f * p.temperature };
	const float wbLuma = 0.2126f * wb[0] + 0.7152f * wb[1] + 0.0722f * wb[2];
	const float exposure = std::exp2(p.exposure);
	for (int k = 0; k < 3; ++k)
	{
		float lin = std::pow(clamp01(c[k]), kGamma);
		lin *= exposure * wb[static_cast<size_t>(k)] / wbLuma;
		c[k] = std::pow(std::max(lin, 0.f), 1.f / kGamma);
	}
	for (size_t k = 0; k < 3; ++k)
	{
		float v = applyContrast(c[k], p.contrast);
		v = p.gain[k] * (v + p.lift[k] * (1.f - v));
		v = std::pow(std::max(v, 0.f), 1.f / p.gamma[k]);
		c[k] = v;
	}
	const float luma = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
	for (size_t k = 0; k < 3; ++k)
	{
		c[k] = clamp01(luma + (c[k] - luma) * p.saturation);
	}
	if (lutImage != nullptr && lutImage->size > 0 && p.lutStrength > 0.f)
	{
		const Rgb l = sampleLutImage(*lutImage, c);
		for (size_t k = 0; k < 3; ++k)
		{
			c[k] = c[k] + (l[k] - c[k]) * p.lutStrength;
		}
	}
	return c;
}

void buildLut(const GradeParams* params, const LutImage* lutImage, iV_Image& image)
{
	constexpr int n = kLutSize;
	image.allocate(n * n, n, 4, false);
	unsigned char* px = image.bmp_w();
	for (int b = 0; b < n; ++b)
	{
		for (int g = 0; g < n; ++g)
		{
			for (int r = 0; r < n; ++r)
			{
				Rgb c = { static_cast<float>(r) / (n - 1), static_cast<float>(g) / (n - 1), static_cast<float>(b) / (n - 1) };
				if (params != nullptr)
				{
					c = gradeColour(*params, lutImage, c);
				}
				unsigned char* o = px + (static_cast<size_t>(g) * (n * n) + static_cast<size_t>(b) * n + static_cast<size_t>(r)) * 4;
				for (size_t k = 0; k < 3; ++k)
				{
					o[k] = static_cast<unsigned char>(std::lround(clamp01(c[k]) * 255.f));
				}
				o[3] = 255;
			}
		}
	}
}

void rebuild(const std::string& key, const std::string& gradeFile)
{
	GradeParams params;
	bool haveParams = !gradeFile.empty() && parseGrade(gradeFile, params);
	LutImage lutImage;
	bool haveLutImage = haveParams && !params.lutFile.empty() && loadLutImage(params.lutFile, lutImage);

	iV_Image image;
	buildLut(haveParams ? &params : nullptr, haveLutImage ? &lutImage : nullptr, image);
	if (state.lut == nullptr)
	{
		state.lut = gfx_api::context::get().create_texture(1, kLutSize * kLutSize, kLutSize, gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8, "colourGradeLut");
	}
	if (state.lut != nullptr && !state.lut->upload(0, image))
	{
		debug(LOG_ERROR, "Colour grade: LUT upload failed");
	}

	state.key = key;
	state.gradeFile = gradeFile;
	state.gradeFileTime = fileTime(gradeFile);
	state.lutFile = haveParams ? params.lutFile : std::string();
	state.lutFileTime = fileTime(state.lutFile);
	state.name = haveParams ? params.name : std::string("none");
	state.strength = haveParams ? params.strength : 0.f;
	debug(LOG_INFO, "Colour grade: %s (%s)", state.name.c_str(), gradeFile.empty() ? "no grade file" : gradeFile.c_str());
}

void refresh()
{
	const char* level = getLevelName();
	const std::string key = std::string(level ? level : "") + "|" + tilesetGradeName(currentMapTileset);
	const auto now = std::chrono::steady_clock::now();
	bool changed = state.lut == nullptr || key != state.key;
	if (!changed && now >= state.nextFileCheck)
	{
		// Pick up edits to the grade file (or a newly added per-map file) while running
		state.nextFileCheck = now + std::chrono::seconds(1);
		const std::string gradeFile = findGradeFile();
		changed = gradeFile != state.gradeFile
			|| fileTime(gradeFile) != state.gradeFileTime
			|| fileTime(state.lutFile) != state.lutFileTime;
	}
	if (changed)
	{
		rebuild(key, findGradeFile());
		state.nextFileCheck = now + std::chrono::seconds(1);
	}
}

} // anonymous namespace

gfx_api::texture* currentLut()
{
	refresh();
	return state.lut;
}

float currentStrength()
{
	return state.strength * static_cast<float>(war_getColourGrading()) / 100.f;
}

const std::string& currentName()
{
	return state.name;
}

void shutdown()
{
	delete state.lut;
	state = State();
}

} // namespace colour_grade
