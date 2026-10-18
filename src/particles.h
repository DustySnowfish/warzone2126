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
/** @file particles.h
 * Data-driven particle effects (muzzle flashes, sparks, smoke, fire, explosions, dust).
 *
 * Effects are defined in data/base/effects/particles.json as named effects made of layers;
 * data/base/effects/weapon_vfx.json says which effect each weapon class plays when it
 * fires, flies and hits. Everything here is display-only: it never touches game state and
 * uses its own random numbers, so it cannot affect multiplayer sync.
 *
 * Positions are in game world coordinates: x east, y south (map), z up (height).
 */

#pragma once

#include "lib/framework/vector.h"

#include <glm/fwd.hpp>
#include <string>

struct WEAPON_STATS;
struct LightingData;

namespace particles
{

/// Load (or reload) the effect definitions. Safe to call again after the data changes.
bool initialise();
void shutdown();
/// Remove all live particles (on level change).
void clear();

/// Advance the simulation and add this frame's effect lights. Call once per frame.
void update(LightingData& lightData);
/// Draw the particles into the current scene pass.
void draw(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);

/// Play a named effect. `direction` (world, may be zero) orients cone emitters.
void spawn(const std::string& effectName, const Vector3f& position, const Vector3f& direction = Vector3f(0.f, 0.f, 0.f), float scale = 1.f);

/// Weapon events (look up the weapon's effects in weapon_vfx.json).
void weaponFired(const WEAPON_STATS* weapon, const Vector3f& muzzlePosition, const Vector3f& muzzleDirection);
/// A projectile in flight; call every frame it is drawn. `id` identifies the projectile.
void weaponTrail(const WEAPON_STATS* weapon, uint32_t id, const Vector3f& position, const Vector3f& velocityDirection);
enum class ImpactKind { Hit, Miss, Water };
void weaponImpact(const WEAPON_STATS* weapon, const Vector3f& position, ImpactKind kind);

/// True when a weapon has effects that replace its classic sprite effect for this event.
bool replacesClassicMuzzle(const WEAPON_STATS* weapon);
bool replacesClassicImpact(const WEAPON_STATS* weapon);

/// Number of live particles (for benchmark results).
size_t liveCount();

} // namespace particles
