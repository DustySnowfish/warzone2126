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
/// Warzone 2126: settings for the random map generator (tools/w2126/mapgen) and the shareable
/// map code that reproduces a generated map exactly.
///
/// The settings are part of the synchronised game options. They reach the map script as the
/// global object `mapOptions` (style, water, height, cliffs, oil, trees, scale), and the seed
/// replaces the game's random seed for generating the map, so the lobby preview, every player's
/// game and anyone who enters the same code all get the same map.
///
/// Map code: <size><tileset><players>-<style><six sliders>-<seed>, for example 512R15-2AAAAAA-1BX9QZ
///   size     map size in tiles; tileset A Arizona, U Urban, R Rockies; players the player count
///   style    0 random, 1 rolling hills, 2 plateaus, 3 lakes, 4 canyons, 5 highlands
///   sliders  water, height, cliffs, oil, trees, scale; one base-36 digit each, 0-K for 0-100 in steps of 5
///   seed     base 36. Letters are case-insensitive; spaces are ignored.

#ifndef __INCLUDED_SRC_RANDOMMAP2126_H__
#define __INCLUDED_SRC_RANDOMMAP2126_H__

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include <nonstd/optional.hpp>
using nonstd::optional;

struct RandomMapSettings2126
{
	enum Slider
	{
		WATER = 0,
		HEIGHT,
		CLIFFS,
		OIL,
		TREES,
		SCALE,
		SLIDER_COUNT
	};
	static constexpr int STYLE_COUNT = 6; // including 0, random
	static constexpr uint8_t SLIDER_DEFAULT = 50;
	static constexpr uint8_t SLIDER_STEP = 5;

	uint32_t seed = 0; ///< 0 until a random map is chosen in the lobby
	uint8_t style = 0;
	std::array<uint8_t, SLIDER_COUNT> sliders = {SLIDER_DEFAULT, SLIDER_DEFAULT, SLIDER_DEFAULT, SLIDER_DEFAULT, SLIDER_DEFAULT, SLIDER_DEFAULT};

	/// Bring every value into range (style 0-5, sliders 0-100 on a step of 5)
	void sanitise();
	bool operator==(const RandomMapSettings2126 &o) const { return seed == o.seed && style == o.style && sliders == o.sliders; }
	bool operator!=(const RandomMapSettings2126 &o) const { return !(*this == o); }

	/// The `mapOptions` handed to the map script
	std::vector<std::pair<std::string, int32_t>> scriptOptions() const;

	/// "seed,style,water,height,cliffs,oil,trees,scale" for savegames, replays and config
	std::string toStorageString() const;
	/// Parse toStorageString(); anything missing or invalid keeps the defaults
	static RandomMapSettings2126 fromStorageString(const std::string &s);
};

/// Which random map a code is for
struct RandomMapIdentity2126
{
	int size = 0;
	char tileset = 0; ///< 'A', 'U' or 'R'
	int players = 0;
	bool operator==(const RandomMapIdentity2126 &o) const { return size == o.size && tileset == o.tileset && players == o.players; }
};

/// The identity of one of the generated random maps from its level name ("Random-512-Rockies") and
/// player count, or nothing when it is not one of them
optional<RandomMapIdentity2126> randomMapIdentity2126(const std::string &levelName, int players);
/// The level name for an identity ("Random-512-Rockies")
std::string randomMapLevelName2126(const RandomMapIdentity2126 &identity);

std::string encodeRandomMapCode2126(const RandomMapIdentity2126 &identity, const RandomMapSettings2126 &settings);
/// Parse a map code; nothing when it is not valid
optional<std::pair<RandomMapIdentity2126, RandomMapSettings2126>> decodeRandomMapCode2126(const std::string &code);

/// A fresh non-zero seed (not from the synchronised generator: only the host picks seeds)
uint32_t newRandomMapSeed2126();

/// Display names (untranslated, for gettext) of the landscape styles, index = style
const char *randomMapStyleName2126(int style);
/// Display names (untranslated) of the sliders
const char *randomMapSliderName2126(int slider);
const char *randomMapSliderTip2126(int slider);

/// Whether the current map is generated from the lobby's seed and settings (one of the Random
/// maps, or another script-generated map, with a seed set; never in benchmarks)
bool randomMapUsesLobbySeed2126();
/// The current game's map code, if it is one of the generated random maps with a lobby seed
optional<std::string> currentRandomMapCode2126();
/// Called every frame in game: shortly after the start, prints the map code to the console once
void announceRandomMapCode2126();

/// A game was generated from the lobby seed: the next lobby visit should start with a new one
void markRandomMapSeedUsed2126();
/// For the lobby: a new seed is due (none yet, or the last one has been played)
bool randomMapSeedDue2126();

/// Hand the settings to the map script engine before a random map is generated (game or preview)
void applyRandomMapScriptOptions2126(const RandomMapSettings2126 &settings);

#endif // __INCLUDED_SRC_RANDOMMAP2126_H__
