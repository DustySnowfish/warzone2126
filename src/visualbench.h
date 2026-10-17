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
 *  Visual benchmark: a repeatable, windowed skirmish run that drives the camera
 *  through a fixed list of shots, records frame timings for each shot, takes a
 *  screenshot at the end of each one, writes a results.json and quits.
 *
 *  The run is defined by data/mp/tests/visualbench_<name>.json. That file is an
 *  ordinary skirmish test config (challenge / player_N groups) plus a
 *  "visualbench" group describing the warm-up and the camera shots:
 *
 *    "visualbench": {
 *      "localAI": "nb_generic.js",  // AI that plays the local player
 *      "warmupGameSeconds": 420,    // game time simulated before the first shot
 *      "warmupSpeed": 16,           // game speed multiplier during warm-up
 *      "settleSeconds": 2,          // real seconds at normal speed before each shot is measured
 *      "vsync": false,              // default false: measure uncapped frame rates
 *      "revealMap": true,           // default true: no fog of war, so every shot shows the scene
 *      "shots": [
 *        { "name": "p0_base_wide",
 *          "anchor": "start:0",     // start:N, centre, or tile
 *          "offset": [0, 4],        // tiles from the anchor
 *          "distance": 3000,        // camera zoom distance
 *          "pitch": -40,            // degrees, -1 (flat) to -88 (top-down)
 *          "yaw": 0,                // degrees
 *          "orbit": 0,              // degrees per second while measuring
 *          "pan": [0, 0],           // tiles per second while measuring
 *          "hold": 8 }              // real seconds measured
 *      ]
 *    }
 *
 *  The match always uses the same RNG seed, so runs are comparable.
 *  Results go to <config dir>/benchmarks/<name>_<timestamp>/.
 */

#pragma once

#include <cstdint>
#include <string>

/// Selects the benchmark to run. Called from command-line parsing.
void visualBenchSelect(const std::string &name);

/// True if a visual benchmark was selected for this run.
bool visualBenchActive();

/// The tests/ config file name for the selected benchmark.
std::string visualBenchTestConfig();

/// Match RNG seed for the benchmark (only meaningful when active).
uint32_t visualBenchSeed();

/// Called once the level has loaded: starts the AI for the local player and the warm-up.
void visualBenchLevelLoaded();

/// Called once per rendered frame from the game loop.
void visualBenchFrame();
