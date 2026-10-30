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

#include "menufootage2126.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/gfx_api.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/sequence/video_decoder.h"
#include "lib/sequence/video_provider.h"

#include "warzoneconfig.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <string>
#include <vector>

namespace menu_footage
{

namespace
{

constexpr const char *kDir = "misc/menufootage";
constexpr float kFadeSeconds = 0.6f;

struct State
{
	bool listed = false;
	std::vector<std::string> clips;
	size_t next = 0;
	std::unique_ptr<WZVideoDecoder> decoder;
	double clipStart = 0.0;      // seconds (steady clock) when the current clip started
	double duration = 0.0;       // of the current clip, 0 when unknown
	double shownPts = -1.0;      // presentation time of the frame in the texture
	bool ending = false;         // the clip has no more frames: fade out, then the next one
	double endAt = 0.0;
	bool haveFrame = false;
	WZVideoFrameYUV pending;     // decoded, not yet due
	bool havePending = false;
	gfx_api::texture *texture = nullptr;
	unsigned texW = 0, texH = 0;
	iV_Image rgba;
};

State s;

double now()
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void listClips()
{
	s.listed = true;
	s.clips.clear();
	WZ_PHYSFS_enumerateFiles(kDir, [](const char *file) -> bool {
		const std::string name = file;
		if (name.size() > 5 && name.compare(name.size() - 5, 5, ".webm") == 0)
		{
			s.clips.push_back(std::string(kDir) + "/" + name);
		}
		return true;
	});
	std::sort(s.clips.begin(), s.clips.end());
	if (!s.clips.empty())
	{
		// Start at a different clip each time the game runs
		std::mt19937 gen(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
		s.next = gen() % s.clips.size();
	}
}

/// YUV 4:2:0 to RGBA (studio or full range, BT.601 or BT.709)
void convert(const WZVideoFrameYUV &f)
{
	struct K { int y, yOff, rv, gu, gv, bu; };
	static const K bt601s = {298, 16, 409, 100, 208, 516}, bt709s = {298, 16, 459, 55, 136, 541};
	static const K bt601f = {256, 0, 359, 88, 183, 454}, bt709f = {256, 0, 403, 48, 120, 475};
	const K &k = f.colorMatrix == WZVideoColorMatrix::BT709 ? (f.fullRange ? bt709f : bt709s) : (f.fullRange ? bt601f : bt601s);
	unsigned char *out = s.rgba.bmp_w();
	auto clip = [](int v) { return static_cast<unsigned char>(v < 0 ? 0 : v > 255 ? 255 : v); };
	for (unsigned y = 0; y < f.height; ++y)
	{
		const uint8_t *py = f.y + static_cast<size_t>(y) * f.yStride;
		const uint8_t *pu = f.u + static_cast<size_t>(y / 2) * f.uvStride;
		const uint8_t *pv = f.v + static_cast<size_t>(y / 2) * f.uvStride;
		unsigned char *o = out + static_cast<size_t>(y) * f.width * 4;
		for (unsigned x = 0; x < f.width; ++x)
		{
			const int Y = (py[x] - k.yOff) * k.y;
			const int U = pu[x / 2] - 128, V = pv[x / 2] - 128;
			o[0] = clip((Y + k.rv * V + 128) >> 8);
			o[1] = clip((Y - k.gu * U - k.gv * V + 128) >> 8);
			o[2] = clip((Y + k.bu * U + 128) >> 8);
			o[3] = 255;
			o += 4;
		}
	}
}

void upload(const WZVideoFrameYUV &f)
{
	if (f.width == 0 || f.height == 0)
	{
		return;
	}
	if (!s.texture || s.texW != f.width || s.texH != f.height)
	{
		delete s.texture;
		s.texture = gfx_api::context::get().create_texture(1, f.width, f.height, gfx_api::pixel_format::FORMAT_RGBA8_UNORM_PACK8, "mem::menuFootage2126");
		s.texW = f.width;
		s.texH = f.height;
		s.rgba.allocate(f.width, f.height, 4, false);
	}
	convert(f);
	if (s.texture)
	{
		s.texture->upload(0, s.rgba);
		s.haveFrame = true;
	}
}

bool openNext()
{
	s.decoder.reset();
	s.havePending = false;
	s.ending = false;
	for (size_t tries = 0; tries < s.clips.size(); ++tries)
	{
		const std::string path = s.clips[s.next];
		s.next = (s.next + 1) % s.clips.size();
		PHYSFS_file *f = PHYSFS_openRead(path.c_str());
		if (!f)
		{
			continue;
		}
		auto decoder = videoDecoderOpen(makeVideoProvider(f, WzString::fromUtf8(path)));
		if (!decoder || !decoder->hasVideo())
		{
			debug(LOG_WARNING, "Menu footage: cannot play %s", path.c_str());
			continue;
		}
		s.decoder = std::move(decoder);
		s.duration = s.decoder->videoMetadata().duration;
		s.clipStart = now();
		s.shownPts = -1.0;
		return true;
	}
	return false;
}

/// Bring the texture up to the frame due now; returns the fade level (0 black .. 1 clear)
float advance()
{
	if (!s.decoder && !openNext())
	{
		return 0.f;
	}
	const double t = now() - s.clipStart;
	if (s.ending)
	{
		if (now() >= s.endAt)
		{
			if (!openNext())
			{
				return 0.f;
			}
			return advance();
		}
		return static_cast<float>(std::max(0.0, (s.endAt - now()) / kFadeSeconds));
	}
	// Show every frame that has come due (the decoder's planes stay valid only until its next
	// frame, so each is converted as it is reached); at most a few per call
	for (int n = 0; n < 6; ++n)
	{
		if (!s.havePending)
		{
			if (!s.decoder->nextVideoFrame(s.pending))
			{
				s.ending = true;
				s.endAt = now() + kFadeSeconds;
				break;
			}
			s.havePending = true;
		}
		if (s.pending.pts > t)
		{
			break;
		}
		upload(s.pending);
		s.shownPts = s.pending.pts;
		s.havePending = false;
	}
	float fade = static_cast<float>(std::min(1.0, t / kFadeSeconds));
	if (s.duration > 0.0)
	{
		fade = std::min(fade, static_cast<float>(std::max(0.0, (s.duration - t) / kFadeSeconds)));
	}
	if (s.ending)
	{
		fade = std::min(fade, static_cast<float>(std::max(0.0, (s.endAt - now()) / kFadeSeconds)));
	}
	return fade;
}

} // anonymous namespace

bool draw()
{
	if (!war_getMenuFootage2126() || !videoDecoderWebmSupported() || screen_mapPreviewActive())
	{
		if (s.decoder)
		{
			stop();
		}
		return false;
	}
	if (!s.listed)
	{
		listClips();
	}
	if (s.clips.empty())
	{
		return false;
	}
	const float fade = advance();
	if (!s.haveFrame || !s.texture)
	{
		return false;
	}
	const float W = static_cast<float>(pie_GetVideoBufferWidth()), H = static_cast<float>(pie_GetVideoBufferHeight());
	// Cover the screen, keeping the clip's shape
	const float scale = std::max(W / s.texW, H / s.texH);
	const float w = s.texW * scale, h = s.texH * scale;
	pie_UniTransBoxFill(0, 0, W, H, pal_RGBA(0, 0, 0, 255));
	iV_DrawImageAnisotropic(*s.texture, Vector2i(static_cast<int>((W - w) / 2), static_cast<int>((H - h) / 2)), Vector2f(0.f, 0.f), Vector2f(w, h), 0.f, WZCOL_WHITE);
	// Fades between clips, and a soft shade behind the menu panel on the left
	if (fade < 1.f)
	{
		pie_UniTransBoxFill(0, 0, W, H, pal_RGBA(0, 0, 0, static_cast<uint8_t>((1.f - fade) * 255.f)));
	}
	const int shadeW = std::min(static_cast<int>(W * 0.45f), 620);
	for (int i = 0; i < 8; ++i)
	{
		const int x1 = shadeW * (8 - i) / 8;
		pie_UniTransBoxFill(0, 0, x1, H, pal_RGBA(0, 0, 0, 14));
	}
	return true;
}

void stop()
{
	s.decoder.reset();
	s.havePending = false;
	s.haveFrame = false;
	s.ending = false;
	delete s.texture;
	s.texture = nullptr;
	s.texW = s.texH = 0;
}

} // namespace menu_footage
