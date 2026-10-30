/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
/// Warzone 2126: random map settings and map codes (see randommap2126.h).

#include "randommap2126.h"

#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"
#include "multiplay.h"
#include "console.h"
#include "visualbench.h"
#include "movebench.h"
#include <wzmaplib/map.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <random>

namespace
{

const char kBase36[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

int base36Digit(char c)
{
	if (c >= '0' && c <= '9')
	{
		return c - '0';
	}
	if (c >= 'A' && c <= 'Z')
	{
		return c - 'A' + 10;
	}
	if (c >= 'a' && c <= 'z')
	{
		return c - 'a' + 10;
	}
	return -1;
}

std::string toBase36(uint32_t v)
{
	if (v == 0)
	{
		return "0";
	}
	std::string s;
	while (v > 0)
	{
		s.insert(s.begin(), kBase36[v % 36]);
		v /= 36;
	}
	return s;
}

optional<uint32_t> fromBase36(const std::string &s)
{
	if (s.empty() || s.size() > 7)
	{
		return nullopt;
	}
	uint64_t v = 0;
	for (char c : s)
	{
		const int d = base36Digit(c);
		if (d < 0)
		{
			return nullopt;
		}
		v = v * 36 + static_cast<uint64_t>(d);
	}
	if (v > UINT32_MAX)
	{
		return nullopt;
	}
	return static_cast<uint32_t>(v);
}

struct TilesetName
{
	char letter;
	const char *name;
};
const TilesetName kTilesets[] = {
	{ 'A', "Arizona" },
	{ 'U', "Urban" },
	{ 'R', "Rockies" },
};

} // anonymous namespace

void RandomMapSettings2126::sanitise()
{
	if (style >= STYLE_COUNT)
	{
		style = 0;
	}
	if (size != 0)
	{
		int best = 0;
		for (int choice : randomMapSizeChoices2126())
		{
			if (best == 0 || std::abs(choice - static_cast<int>(size)) < std::abs(best - static_cast<int>(size)))
			{
				best = choice;
			}
		}
		size = static_cast<uint16_t>(best);
	}
	mirror = mirror ? 1 : 0;
	if (layout >= LAYOUT_COUNT)
	{
		layout = 0;
	}
	for (auto &v : sliders)
	{
		v = static_cast<uint8_t>(std::min<int>(100, (v + SLIDER_STEP / 2) / SLIDER_STEP * SLIDER_STEP));
	}
}

std::vector<std::pair<std::string, int32_t>> RandomMapSettings2126::scriptOptions() const
{
	return {
		{ "style", style },
		{ "water", sliders[WATER] },
		{ "height", sliders[HEIGHT] },
		{ "cliffs", sliders[CLIFFS] },
		{ "oil", sliders[OIL] },
		{ "trees", sliders[TREES] },
		{ "scale", sliders[SCALE] },
		{ "scavengers", sliders[SCAVENGERS] },
		{ "ruins", sliders[RUINS] },
		{ "size", size },
		{ "mirror", mirror },
		{ "layout", layout },
	};
}

std::string RandomMapSettings2126::toStorageString() const
{
	// The first six sliders, size and mirror in their original places; later sliders after them
	std::string s = std::to_string(seed) + "," + std::to_string(style);
	for (int i = 0; i < SLIDER_COUNT_V1; ++i)
	{
		s += "," + std::to_string(sliders[i]);
	}
	s += "," + std::to_string(size) + "," + std::to_string(mirror);
	for (int i = SLIDER_COUNT_V1; i < SLIDER_COUNT; ++i)
	{
		s += "," + std::to_string(sliders[i]);
	}
	s += "," + std::to_string(layout);
	return s;
}

RandomMapSettings2126 RandomMapSettings2126::fromStorageString(const std::string &s)
{
	RandomMapSettings2126 r;
	std::vector<uint64_t> values;
	uint64_t cur = 0;
	bool have = false;
	for (size_t i = 0; i <= s.size(); ++i)
	{
		if (i == s.size() || s[i] == ',')
		{
			values.push_back(have ? cur : 0);
			cur = 0;
			have = false;
		}
		else if (s[i] >= '0' && s[i] <= '9' && cur < UINT32_MAX)
		{
			cur = cur * 10 + static_cast<uint64_t>(s[i] - '0');
			have = true;
		}
		else
		{
			return RandomMapSettings2126(); // malformed: defaults
		}
	}
	// seed, style, six sliders [, size, mirror [, later sliders, layout]]
	const size_t v1 = 2 + SLIDER_COUNT_V1;
	const size_t full = v1 + 2 + (SLIDER_COUNT - SLIDER_COUNT_V1) + 1;
	if (values.size() != v1 && values.size() != v1 + 2 && values.size() != full)
	{
		return RandomMapSettings2126();
	}
	if (values.size() >= v1 + 2)
	{
		r.size = static_cast<uint16_t>(std::min<uint64_t>(values[v1], 512));
		r.mirror = static_cast<uint8_t>(std::min<uint64_t>(values[v1 + 1], 1));
	}
	r.seed = static_cast<uint32_t>(std::min<uint64_t>(values[0], UINT32_MAX));
	r.style = static_cast<uint8_t>(std::min<uint64_t>(values[1], 255));
	for (int i = 0; i < SLIDER_COUNT_V1; ++i)
	{
		r.sliders[i] = static_cast<uint8_t>(std::min<uint64_t>(values[2 + i], 255));
	}
	if (values.size() > v1 + 2)
	{
		for (int i = SLIDER_COUNT_V1; i < SLIDER_COUNT; ++i)
		{
			r.sliders[i] = static_cast<uint8_t>(std::min<uint64_t>(values[v1 + 2 + (i - SLIDER_COUNT_V1)], 255));
		}
		r.layout = static_cast<uint8_t>(std::min<uint64_t>(values[full - 1], 255));
	}
	r.sanitise();
	return r;
}

optional<RandomMapIdentity2126> randomMapIdentity2126(const std::string &levelNameIn, int players)
{
	std::string levelName = levelNameIn;
	// Multiplayer level names may carry a tech level suffix ("-T1")
	if (levelName.size() > 3 && levelName[levelName.size() - 3] == '-' && levelName[levelName.size() - 2] == 'T')
	{
		levelName.resize(levelName.size() - 3);
	}
	// "Random-<Tileset>-<players>"
	const std::string prefix = "Random-";
	if (levelName.compare(0, prefix.size(), prefix) != 0)
	{
		return nullopt;
	}
	const size_t dash = levelName.rfind('-');
	if (dash == std::string::npos || dash < prefix.size() || std::to_string(players) != levelName.substr(dash + 1))
	{
		return nullopt;
	}
	const std::string tileset = levelName.substr(prefix.size(), dash - prefix.size());
	for (const auto &t : kTilesets)
	{
		if (tileset == t.name)
		{
			RandomMapIdentity2126 id;
			id.tileset = t.letter;
			id.players = players;
			return id;
		}
	}
	return nullopt;
}

std::string randomMapLevelName2126(const RandomMapIdentity2126 &identity)
{
	for (const auto &t : kTilesets)
	{
		if (t.letter == identity.tileset)
		{
			return std::string("Random-") + t.name + "-" + std::to_string(identity.players);
		}
	}
	return {};
}

const char *randomMapTilesetName2126(char tileset)
{
	for (const auto &t : kTilesets)
	{
		if (t.letter == tileset)
		{
			return t.name;
		}
	}
	return "";
}

const std::vector<int> &randomMapSizeChoices2126()
{
	static const std::vector<int> sizes = { 128, 192, 256, 320, 384, 448, 512 };
	return sizes;
}

int randomMapDefaultSize2126(int players)
{
	return players <= 2 ? 128 : players <= 4 ? 192 : players <= 8 ? 256 : players <= 10 ? 384 : 512;
}

int randomMapMinSize2126(int players)
{
	return players <= 4 ? 128 : players <= 8 ? 192 : players <= 10 ? 256 : 320;
}

int randomMapResolvedSize2126(const RandomMapSettings2126 &settings, int players)
{
	return settings.size ? std::max<int>(settings.size, randomMapMinSize2126(players)) : randomMapDefaultSize2126(players);
}

std::string encodeRandomMapCode2126(const RandomMapIdentity2126 &identity, const RandomMapSettings2126 &settingsIn)
{
	RandomMapSettings2126 settings = settingsIn;
	settings.sanitise();
	std::string code = std::to_string(randomMapResolvedSize2126(settings, identity.players)) + identity.tileset + std::to_string(identity.players) + "-";
	code += kBase36[settings.style];
	// The later sliders only when one of them is not at its default, so codes stay as they were
	int count = RandomMapSettings2126::SLIDER_COUNT_V1;
	for (int i = RandomMapSettings2126::SLIDER_COUNT_V1; i < RandomMapSettings2126::SLIDER_COUNT; ++i)
	{
		if (settings.sliders[i] != RandomMapSettings2126::SLIDER_DEFAULT)
		{
			count = RandomMapSettings2126::SLIDER_COUNT;
		}
	}
	for (int i = 0; i < count; ++i)
	{
		code += kBase36[settings.sliders[i] / RandomMapSettings2126::SLIDER_STEP];
	}
	code += kBase36[settings.mirror + 2 * settings.layout];
	code += "-" + toBase36(settings.seed);
	return code;
}

optional<std::pair<RandomMapIdentity2126, RandomMapSettings2126>> decodeRandomMapCode2126(const std::string &codeIn)
{
	std::string code;
	for (char c : codeIn)
	{
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
		{
			continue;
		}
		code += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
	}
	const size_t d1 = code.find('-');
	const size_t d2 = (d1 == std::string::npos) ? std::string::npos : code.find('-', d1 + 1);
	if (d2 == std::string::npos || code.find('-', d2 + 1) != std::string::npos)
	{
		return nullopt;
	}
	const std::string mapPart = code.substr(0, d1);
	const std::string settingsPart = code.substr(d1 + 1, d2 - d1 - 1);
	const std::string seedPart = code.substr(d2 + 1);

	// Map part: digits, a tileset letter, digits
	size_t letterPos = std::string::npos;
	for (size_t i = 0; i < mapPart.size(); ++i)
	{
		if (mapPart[i] < '0' || mapPart[i] > '9')
		{
			if (letterPos != std::string::npos)
			{
				return nullopt;
			}
			letterPos = i;
		}
	}
	if (letterPos == std::string::npos || letterPos == 0 || letterPos + 1 >= mapPart.size() || letterPos > 4 || mapPart.size() - letterPos - 1 > 2)
	{
		return nullopt;
	}
	RandomMapIdentity2126 identity;
	const int codeSize = std::stoi(mapPart.substr(0, letterPos));
	identity.tileset = mapPart[letterPos];
	identity.players = std::stoi(mapPart.substr(letterPos + 1));
	if (identity.tileset != 'A' && identity.tileset != 'U' && identity.tileset != 'R')
	{
		return nullopt;
	}

	// Settings: style, six sliders, then (from the second version on) the terrain digit; or style,
	// all the sliders and the terrain digit
	const int v1 = RandomMapSettings2126::SLIDER_COUNT_V1;
	int sliderCount;
	bool hasMirror;
	if (settingsPart.size() == static_cast<size_t>(1 + v1))
	{
		sliderCount = v1;
		hasMirror = false;
	}
	else if (settingsPart.size() == static_cast<size_t>(2 + v1))
	{
		sliderCount = v1;
		hasMirror = true;
	}
	else if (settingsPart.size() == static_cast<size_t>(2 + RandomMapSettings2126::SLIDER_COUNT))
	{
		sliderCount = RandomMapSettings2126::SLIDER_COUNT;
		hasMirror = true;
	}
	else
	{
		return nullopt;
	}
	RandomMapSettings2126 settings;
	const int style = base36Digit(settingsPart[0]);
	if (style < 0 || style >= RandomMapSettings2126::STYLE_COUNT)
	{
		return nullopt;
	}
	settings.style = static_cast<uint8_t>(style);
	for (int i = 0; i < sliderCount; ++i)
	{
		const int step = base36Digit(settingsPart[1 + i]);
		if (step < 0 || step > 100 / RandomMapSettings2126::SLIDER_STEP)
		{
			return nullopt;
		}
		settings.sliders[i] = static_cast<uint8_t>(step * RandomMapSettings2126::SLIDER_STEP);
	}
	if (hasMirror)
	{
		const int terrain = base36Digit(settingsPart[1 + sliderCount]);
		if (terrain < 0 || terrain >= 2 * RandomMapSettings2126::LAYOUT_COUNT)
		{
			return nullopt;
		}
		settings.mirror = static_cast<uint8_t>(terrain % 2);
		settings.layout = static_cast<uint8_t>(terrain / 2);
	}
	// Size: one of the choices, big enough for the players
	const auto &choices = randomMapSizeChoices2126();
	if (std::find(choices.begin(), choices.end(), codeSize) == choices.end() || codeSize < randomMapMinSize2126(identity.players))
	{
		return nullopt;
	}
	settings.size = static_cast<uint16_t>(codeSize);
	auto seed = fromBase36(seedPart);
	if (!seed.has_value() || seed.value() == 0)
	{
		return nullopt;
	}
	settings.seed = seed.value();
	return std::make_pair(identity, settings);
}

uint32_t newRandomMapSeed2126()
{
	static std::mt19937 gen(static_cast<uint32_t>(std::random_device{}()) ^ static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
	uint32_t seed = 0;
	while (seed == 0)
	{
		seed = gen(); // any 32-bit value fits in 7 base-36 digits
	}
	return seed;
}

const char *randomMapStyleName2126(int style)
{
	static const char *names[] = { N_("Random"), N_("Rolling hills"), N_("Plateaus"), N_("Lakes"), N_("Canyons"), N_("Highlands"), N_("Islands"), N_("River valleys") };
	return (style >= 0 && style < RandomMapSettings2126::STYLE_COUNT) ? names[style] : names[0];
}

const char *randomMapLayoutName2126(int layout)
{
	static const char *names[] = { N_("Ring (free for all)"), N_("Two teams facing"), N_("Four corners (pairs)") };
	return (layout >= 0 && layout < RandomMapSettings2126::LAYOUT_COUNT) ? names[layout] : names[0];
}

bool randomMapLayoutFits2126(int layout, int players)
{
	// As in the map script (tools/w2126/mapgen/random_map.js, LAYOUT)
	switch (layout)
	{
	case 1: return players >= 4 && players % 2 == 0;
	case 2: return players >= 8 && players % 4 == 0;
	default: return true;
	}
}

const char *randomMapSliderName2126(int slider)
{
	static const char *names[] = { N_("Water"), N_("Height"), N_("Cliffs"), N_("Oil"), N_("Trees and rocks"), N_("Feature size"), N_("Scavengers"), N_("Ruins and wrecks") };
	return (slider >= 0 && slider < RandomMapSettings2126::SLIDER_COUNT) ? names[slider] : "";
}

const char *randomMapSliderTip2126(int slider)
{
	static const char *tips[] = {
		N_("Lakes: 0 is dry land, 50 is the landscape's usual amount, 100 is several times more"),
		N_("Hills and plateaus: 0 is nearly flat, 100 is as tall and steep as the engine allows"),
		N_("Cliff lines and terraces: 0 is open slopes, 100 is more levels with fewer ramps between them"),
		N_("Oil sites around each base: 0 is half the usual number, 100 is half as many again (base and centre oil stay the same)"),
		N_("Trees and rocks: 0 is bare ground, 100 is twice the usual"),
		N_("Size of hills, valleys and lakes: 0 is small and busy, 100 is big and sweeping"),
		N_("Scavenger camps (when scavengers are on): 0 is none (their oil is free), up to 30 small outposts, 50 the usual camps, 70 fortified camps, 85 and up a second ring of camps nearer the centre"),
		N_("Ruined buildings, wrecked vehicles and city blocks: 0 is none, 100 is twice the usual"),
	};
	return (slider >= 0 && slider < RandomMapSettings2126::SLIDER_COUNT) ? tips[slider] : "";
}

static bool seedUsed = false;

void markRandomMapSeedUsed2126()
{
	seedUsed = true;
}

bool randomMapSeedDue2126()
{
	const bool due = seedUsed || game.randomMap.seed == 0;
	seedUsed = false;
	return due;
}

bool randomMapUsesLobbySeed2126()
{
	if (game.randomMap.seed == 0 || visualBenchActive() || movementBenchActive())
	{
		return false;
	}
	// game.isRandom is not always set (the lobby clears it when hosting starts), so the Random maps
	// are also recognised by name
	return game.isRandom || randomMapIdentity2126(game.map, game.maxPlayers).has_value();
}

optional<std::string> currentRandomMapCode2126()
{
	if (!randomMapUsesLobbySeed2126())
	{
		return nullopt;
	}
	auto identity = randomMapIdentity2126(game.map, game.maxPlayers);
	if (!identity.has_value())
	{
		return nullopt;
	}
	return encodeRandomMapCode2126(identity.value(), game.randomMap);
}

void announceRandomMapCode2126()
{
	static bool announced = false;
	static uint32_t lastGameTime = 0;
	if (gameTime < lastGameTime)
	{
		announced = false; // a new game
	}
	lastGameTime = gameTime;
	if (announced || gameTime < 4000)
	{
		return;
	}
	announced = true;
	auto code = currentRandomMapCode2126();
	if (code.has_value())
	{
		addConsoleMessage(astringf(_("Random map code: %s (enter it in Random map settings to play this map again)"), code->c_str()).c_str(), DEFAULT_JUSTIFY, SYSTEM_MESSAGE);
	}
}

void applyRandomMapScriptOptions2126(const RandomMapSettings2126 &settings)
{
	WzMap::setMapScriptOptions(settings.scriptOptions());
}
