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

/// Warzone 2126: save the lobby's generated Random map as an ordinary map package (see
/// randommapexport2126.h).

#include "randommapexport2126.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"

#include "game.h"
#include "init.h"
#include "levels.h"
#include "multiplay.h"
#include "randommap2126.h"

#include <wzmaplib/map.h>
#include <wzmaplib/map_package.h>
#include "ZipIOProvider.h"

#include <ctime>

namespace
{

class ExportLogger : public WzMap::LoggingProtocol
{
public:
	void printLog(WzMap::LoggingProtocol::LogLevel level, const char *function, int line, const char *str) override
	{
		if (level == WzMap::LoggingProtocol::LogLevel::Error)
		{
			debug(LOG_ERROR, "Map export: %s (%s:%d)", str, function, line);
		}
		else
		{
			debug(LOG_WZ, "Map export: %s", str);
		}
	}
};

MAP_TILESET tilesetOf(char letter)
{
	switch (letter)
	{
	case 'U': return MAP_TILESET::URBAN;
	case 'R': return MAP_TILESET::ROCKIES;
	default: return MAP_TILESET::ARIZONA;
	}
}

std::string today()
{
	const std::time_t now = std::time(nullptr);
	std::tm tm = {};
#if defined(_WIN32)
	localtime_s(&tm, &now);
#else
	localtime_r(&now, &tm);
#endif
	char buf[16];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
	return buf;
}

} // anonymous namespace

std::string randomMapExportName2126(const std::string &code)
{
	// "Gen-" and the map code, which names every generated map uniquely
	return "Gen-" + code;
}

RandomMapExportResult2126 exportRandomMap2126()
{
	RandomMapExportResult2126 result;
	const auto code = [&]() -> std::string {
		auto identity = randomMapIdentity2126(game.map, game.maxPlayers);
		if (!identity.has_value() || game.randomMap.seed == 0)
		{
			return {};
		}
		return encodeRandomMapCode2126(identity.value(), game.randomMap);
	}();
	auto identity = randomMapIdentity2126(game.map, game.maxPlayers);
	if (code.empty() || !identity.has_value())
	{
		result.error = _("Pick one of the Random maps first");
		return result;
	}

	LEVEL_DATASET *psLevel = levFindDataSet(game.map, &game.hash);
	if (psLevel == nullptr || psLevel->game < 0 || psLevel->game >= LEVEL_MAXFILES || psLevel->apDataFiles[psLevel->game].empty())
	{
		result.error = _("The map could not be found");
		return result;
	}
	rebuildSearchPath(psLevel->dataDir, false);
	auto loadDetails = psLevel->realFileName ? GameLoadDetails::makeMapPackageLoad(psLevel->realFileName) : GameLoadDetails::makeLevelFileLoad(psLevel->apDataFiles[psLevel->game]);
	applyRandomMapScriptOptions2126(game.randomMap);
	auto map = loadDetails.getMap(game.randomMap.seed);
	if (!map || !map->mapData() || !map->mapTerrainTypes() || !map->mapStructures() || !map->mapDroids() || !map->mapFeatures())
	{
		result.error = _("The map could not be generated");
		return result;
	}

	const std::string name = randomMapExportName2126(code);
	const std::string fileName = std::to_string(static_cast<int>(game.maxPlayers)) + "c-" + name + ".wz";
	const char *writeDir = PHYSFS_getWriteDir();
	if (!writeDir)
	{
		result.error = _("There is nowhere to save maps");
		return result;
	}
	PHYSFS_mkdir("maps");
	const std::string relPath = std::string("maps/") + fileName;
	if (PHYSFS_exists(relPath.c_str()))
	{
		result.name = name;
		result.path = std::string(writeDir) + "maps" + PHYSFS_getDirSeparator() + fileName;
		result.alreadySaved = true;
		return result;
	}
	const std::string fullPath = std::string(writeDir) + "maps" + PHYSFS_getDirSeparator() + fileName;

	WzMap::LevelDetails details;
	details.name = name;
	details.type = WzMap::MapType::SKIRMISH;
	details.players = static_cast<uint8_t>(game.maxPlayers);
	details.tileset = tilesetOf(identity->tileset);
	details.createdDate = today();
	details.author = "Warzone 2126 map generator";
	details.license = "CC0-1.0";
	details.generator = std::string("Warzone 2126 random map generator, map code ") + code;

	auto logger = std::make_shared<ExportLogger>();
	bool ok = false;
	{
		auto zipIO = WzMapZipIO::createZipArchiveFS(fullPath.c_str());
		if (zipIO)
		{
			// A plain copy of the generated data, so the package holds the map itself rather than
			// the script that made it
			auto baked = std::make_shared<WzMap::Map>();
			*baked->mapData() = *map->mapData();
			*baked->mapTerrainTypes() = *map->mapTerrainTypes();
			*baked->mapStructures() = *map->mapStructures();
			*baked->mapDroids() = *map->mapDroids();
			*baked->mapFeatures() = *map->mapFeatures();
			WzMap::MapPackage package(details, WzMap::MapType::SKIRMISH, baked);
			ok = package.exportMapPackageFiles("", WzMap::LevelFormat::JSON, WzMap::LatestOutputFormat, nullopt, false, logger, zipIO);
		}
		// The archive is written when the provider closes
	}
	if (!ok)
	{
		PHYSFS_delete(relPath.c_str());
		result.error = _("The map could not be saved");
		return result;
	}

	// Make it show up in the map list
	buildMapList();
	result.ok = true;
	result.name = name;
	result.path = fullPath;
	return result;
}
