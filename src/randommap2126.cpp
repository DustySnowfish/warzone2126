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

#include <chrono>
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
	};
}

std::string RandomMapSettings2126::toStorageString() const
{
	std::string s = std::to_string(seed) + "," + std::to_string(style);
	for (uint8_t v : sliders)
	{
		s += "," + std::to_string(v);
	}
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
	if (values.size() != 2 + SLIDER_COUNT)
	{
		return RandomMapSettings2126();
	}
	r.seed = static_cast<uint32_t>(std::min<uint64_t>(values[0], UINT32_MAX));
	r.style = static_cast<uint8_t>(std::min<uint64_t>(values[1], 255));
	for (int i = 0; i < SLIDER_COUNT; ++i)
	{
		r.sliders[i] = static_cast<uint8_t>(std::min<uint64_t>(values[2 + i], 255));
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
	// "Random-<size>-<Tileset>"
	const std::string prefix = "Random-";
	if (levelName.compare(0, prefix.size(), prefix) != 0)
	{
		return nullopt;
	}
	const size_t dash = levelName.find('-', prefix.size());
	if (dash == std::string::npos)
	{
		return nullopt;
	}
	int size = 0;
	for (size_t i = prefix.size(); i < dash; ++i)
	{
		if (levelName[i] < '0' || levelName[i] > '9')
		{
			return nullopt;
		}
		size = size * 10 + (levelName[i] - '0');
	}
	const std::string tileset = levelName.substr(dash + 1);
	for (const auto &t : kTilesets)
	{
		if (tileset == t.name)
		{
			RandomMapIdentity2126 id;
			id.size = size;
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
			return "Random-" + std::to_string(identity.size) + "-" + t.name;
		}
	}
	return {};
}

std::string encodeRandomMapCode2126(const RandomMapIdentity2126 &identity, const RandomMapSettings2126 &settingsIn)
{
	RandomMapSettings2126 settings = settingsIn;
	settings.sanitise();
	std::string code = std::to_string(identity.size) + identity.tileset + std::to_string(identity.players) + "-";
	code += kBase36[settings.style];
	for (uint8_t v : settings.sliders)
	{
		code += kBase36[v / RandomMapSettings2126::SLIDER_STEP];
	}
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
	identity.size = std::stoi(mapPart.substr(0, letterPos));
	identity.tileset = mapPart[letterPos];
	identity.players = std::stoi(mapPart.substr(letterPos + 1));
	if (identity.tileset != 'A' && identity.tileset != 'U' && identity.tileset != 'R')
	{
		return nullopt;
	}

	if (settingsPart.size() != 1 + RandomMapSettings2126::SLIDER_COUNT)
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
	for (int i = 0; i < RandomMapSettings2126::SLIDER_COUNT; ++i)
	{
		const int step = base36Digit(settingsPart[1 + i]);
		if (step < 0 || step > 100 / RandomMapSettings2126::SLIDER_STEP)
		{
			return nullopt;
		}
		settings.sliders[i] = static_cast<uint8_t>(step * RandomMapSettings2126::SLIDER_STEP);
	}
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
	static const char *names[] = { N_("Random"), N_("Rolling hills"), N_("Plateaus"), N_("Lakes"), N_("Canyons"), N_("Highlands") };
	return (style >= 0 && style < RandomMapSettings2126::STYLE_COUNT) ? names[style] : names[0];
}

const char *randomMapSliderName2126(int slider)
{
	static const char *names[] = { N_("Water"), N_("Height"), N_("Cliffs"), N_("Oil"), N_("Trees and rocks"), N_("Feature size") };
	return (slider >= 0 && slider < RandomMapSettings2126::SLIDER_COUNT) ? names[slider] : "";
}

const char *randomMapSliderTip2126(int slider)
{
	static const char *tips[] = {
		N_("Lakes: 0 is dry land, 50 is the landscape's usual amount, 100 is several times more"),
		N_("Hills and plateaus: 0 is nearly flat, 100 is as tall and steep as the engine allows"),
		N_("Cliff lines and terraces: 0 is open slopes, 100 is more levels with fewer ramps between them"),
		N_("Oil sites around each base: 0 is half the usual number, 100 is half as many again (base and centre oil stay the same)"),
		N_("Trees, rocks and wrecks: 0 is bare ground, 100 is twice the usual"),
		N_("Size of hills, valleys and lakes: 0 is small and busy, 100 is big and sweeping"),
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
