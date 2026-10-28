
/*
 * This file describes building construction procedures.
 *
 */

(function(_global) {
////////////////////////////////////////////////////////////////////////////////////////////

function randomLocation()
{
	const x = baseLocation.x + random(baseScale) - baseScale / 2;
	const y = baseLocation.y + random(baseScale) - baseScale / 2;

	if (x < 3 || y < 3 || x > mapWidth - 4 || y > mapHeight - 4)
	{
		return baseLocation;
	}

	return {x: x, y: y};

}

// a function for checking the presence of enemy units at the construction site
function safeSpot(x, y)
{
	return dangerLevel({x: x, y: y}) <= 0;
}

function truckFree(truck)
{
	if (truck.droidType !== DROID_CONSTRUCT)
	{
		return false;
	}

	if (truck.order === DORDER_BUILD ||
		truck.order === DORDER_HELPBUILD ||
		truck.order === DORDER_LINEBUILD ||
		truck.order === DORDER_DEMOLISH ||
		(typeof DORDER_EARTHWORK !== "undefined" && truck.order === DORDER_EARTHWORK))
	{
		return false;
	}

	return true;
}

// returns one or two free trucks
function getTwoFreeTrucks()
{
	let trucks = enumTrucks().filter(truckFree);

	if (trucks.length > 2)
	{
		const ret = naiveFindClusters(trucks, baseScale / 2);

		if (ret.maxCount >= 2)
		{
			trucks = ret.clusters[ret.maxIdx];
		}
	}

	if (trucks.length > 2)
	{
		trucks.length = 2;
	}

	return trucks;
}

function getFreeTruckAround(x, y, withShips)
{
	const all = withShips && typeof enumSeaTrucks !== "undefined" ? enumTrucks().concat(enumSeaTrucks()) : enumTrucks();
	const list = all.filter(truckFree).filter((droid) => (
		droidCanReach(droid, x, y)
	)).sort((one, two) => (distance(one, x, y) - distance(two, x, y)));

	if (list.length > 0)
	{
		return list[0];
	}
}

function buildModule(struct)
{
	const trucks = getTwoFreeTrucks();

	if (trucks.length <= 0)
	{
		return BUILDRET.FAILURE;
	}

	const moduleInfo = modules.filter((item) => (isAvailable(item.module) && item.base === struct.stattype)).last();

	if (!defined(moduleInfo))
	{
		return BUILDRET.UNAVAILABLE;
	}

	if (struct.modules >= moduleInfo.count)
	{
		return BUILDRET.UNAVAILABLE;
	}

	let success = false;

	for (let i = 0; i < trucks.length; ++i)
	{
		success = orderDroidBuild(trucks[i], DORDER_BUILD, moduleInfo.module, struct.x, struct.y) || success;
	}

	return (success) ? BUILDRET.SUCCESS : BUILDRET.FAILURE;
}

function buildBasicStructure(statlist, importance)
{
	if (throttled(5000, statlist[0]))
	{
		return BUILDRET.FAILURE;
	}
	// by default, don't try building things in dangerous locations
	if (!defined(importance))
	{
		importance = IMPORTANCE.MANDATORY;
	}

	const trucks = getTwoFreeTrucks();

	if (trucks.length <= 0)
	{
		return BUILDRET.FAILURE;
	}

	// choose structure type (out of the statlist),
	// together with suitable location
	let idx = 0;
	let loc = false;
	let avail = false;

	for (let i = 0; i < statlist.length; ++i)
	{
		if (isAvailable(statlist[i]))
		{
			avail = true;
			idx = i;

			if (distanceToBase(trucks[0]) <= baseScale)
			{
				loc = pickStructLocation(trucks[0], statlist[i], trucks[0].x, trucks[0].y);
			}
			else
			{
				const rndLoc = randomLocation();

				loc = pickStructLocation(trucks[0], statlist[i], rndLoc.x, rndLoc.y);
			}

			break;
		}
	}

	if (!avail)
	{
		return BUILDRET.UNAVAILABLE;
	}

	if (!defined(loc))
	{
		return BUILDRET.FAILURE;
	}

	if (importance === IMPORTANCE.PEACETIME && !safeSpot(loc.x, loc.y))
	{
		return BUILDRET.FAILURE;
	}

	// now actually build
	let success = false;

	for (let i = 0; i < trucks.length; ++i)
	{
		success = orderDroidBuild(trucks[i], DORDER_BUILD, statlist[idx], loc.x, loc.y) || success;
	}

	return (success) ? BUILDRET.SUCCESS : BUILDRET.FAILURE;
}

function finishStructures()
{
	let success = false;
	const list = enumStruct(me).filterProperty("status", BEING_BUILT);

	for (let i = 0; i < list.length; ++i)
	{
		if (success ||
			(throttled(10000, list[i].id)) ||
			(list[i].stattype === RESOURCE_EXTRACTOR))
		{
			break;
		}

		const truck = getFreeTruckAround(list[i].x, list[i].y);

		if (!defined(truck))
		{
			break;
		}

		if (orderDroidObj(truck, DORDER_HELPBUILD, list[i]))
		{
			success = true;
		}
	}

	return success;
}

function buildStructureAround(statlist, loc, unique)
{
	if (!defined(statlist))
	{
		return BUILDRET.UNAVAILABLE;
	}

	const truck = getFreeTruckAround(loc.x, loc.y);

	if (!defined(truck))
	{
		return BUILDRET.FAILURE;
	}

	const stat = statlist.filter(isAvailable).filter((s) => {
		if (unique !== true)
		{
			return true;
		}

		const list = enumStruct(me, s);

		for (let i = 0; i < list.length; ++i)
		{
			if (distance(list[i], loc) < baseScale / 2)
			{
				return false;
			}
		}

		return true;
	}).last();

	if (!defined(stat))
	{
		return BUILDRET.UNAVAILABLE;
	}

	const loc2 = pickStructLocation(truck, stat, loc.x, loc.y);

	if (!defined(loc2))
	{
		return BUILDRET.FAILURE;
	}
	// if we're not into turtling, don't build too many towers
	if (personality.defensiveness < 100 && distance(loc2, loc) > baseScale / 5)
	{
		return BUILDRET.FAILURE;
	}

	if (orderDroidBuild(truck, DORDER_BUILD, stat, loc2.x, loc2.y))
	{
		return BUILDRET.SUCCESS;
	}

	return BUILDRET.FAILURE;
}

function captureOil(oil)
{
	if (!defined(oil))
	{
		return BUILDRET.FAILURE;
	}

	// Warzone 2126: oil in the water takes an offshore rig, built by a ship or hover truck
	const offshore = terrainType(oil.x, oil.y) === TER_WATER;
	const stat = offshore ? (isAvailable("A0OffshoreRig") ? "A0OffshoreRig" : undefined)
		: structures.derricks.filter((s) => (s !== "A0OffshoreRig" && isAvailable(s))).last();

	if (!defined(stat))
	{
		return offshore ? BUILDRET.FAILURE : BUILDRET.UNAVAILABLE;
	}

	const truck = getFreeTruckAround(oil.x, oil.y, offshore);

	if (!defined(truck))
	{
		return BUILDRET.FAILURE;
	}

	if (throttled(90000, oil.y * mapWidth + oil.x))
	{
		return BUILDRET.FAILURE;
	}

	if (orderDroidBuild(truck, DORDER_BUILD, stat, oil.x, oil.y))
	{
		return BUILDRET.SUCCESS;
	}

	return BUILDRET.FAILURE;
}

function chooseDefense(defrole)
{
	return weaponStatsToDefenses(chooseAvailableWeaponPathByRoleRatings(getProductionPaths(), chooseDefendWeaponRole(), 2, defrole), defrole);
}

function buildTowers()
{
	const oils = enumStructList(structures.derricks);

	if (oils.length === 0)
	{
		return false;
	}

	if (withChance(70))
	{
		return buildStructureAround(chooseDefense(DEFROLE.STANDALONE), oils.random()) !== BUILDRET.UNAVAILABLE;
	}

	return buildStructureAround(chooseDefense(DEFROLE.FORTRESS).concat(structures.sensors), oils.random(), true) !== BUILDRET.UNAVAILABLE;
}

function buildGateways()
{
	function uncached()
	{
		const oils = countStructList(structures.derricks);

		if (oils <= 0)
		{
			return BUILDRET.FAILURE;
		}

		// lets not cycle through all gateways on the map
		if (!areThereGW())
		{
			return BUILDRET.FAILURE;
		}

		const gates = gateways.filter((gate) => {
			let l = gate.x1 - gate.x2 + gate.y1 - gate.y2;

			if (l < 0)
			{
				l = -l;
			}

			let cnt = enumRange(gate.x1, gate.y1, l, ALLIES).filterProperty("stattype", DEFENSE).length;
			cnt    += enumRange(gate.x2, gate.y2, l, ALLIES).filterProperty("stattype", DEFENSE).length;
			cnt    -= enumRange(gate.x1, gate.y1, l, ENEMIES).filterProperty("stattype", DEFENSE).length;
			cnt    -= enumRange(gate.x2, gate.y2, l, ENEMIES).filterProperty("stattype", DEFENSE).length;

			return cnt >= 0 && (cnt < l || (personality.defensiveness === 100 && withChance(70))); // turtle AI needs to keep building towers
		}).sort((one, two) => (distanceToBase({x: one.x1, y: one.y1}) - distanceToBase({x: two.x1, y: two.y1})));

		if (gates.length === 0)
		{
			return BUILDRET.FAILURE;
		}

		if (withChance(50))
		{
			return buildStructureAround(chooseDefense(DEFROLE.GATEWAY), {x: gates[0].x1, y: gates[0].y1}) !== BUILDRET.UNAVAILABLE;
		}
		else
		{
			return buildStructureAround(chooseDefense(DEFROLE.GATEWAY), {x: gates[0].x2, y: gates[0].y2}) !== BUILDRET.UNAVAILABLE;
		}
	}

	return cached(uncached, 200);
}

function buildArty()
{
	return buildBasicStructure(chooseDefense(DEFROLE.ARTY), IMPORTANCE.PEACETIME);
}

_global.buildMinimum = function(statlist, count, importance) {
	if (countStructList(statlist) < count)
	{
		if (buildBasicStructure(statlist, importance) !== BUILDRET.UNAVAILABLE)
		{
			return true;
		}
	}

	return false;
};

_global.captureSomeOil = function() {
	if (throttled(500))
	{
		return true;
	}

	function getOilList()
	{
		let oils = [];
		oilResources.forEach((stat) => { oils = oils.concat(enumFeature(ALL_PLAYERS, stat)); });

		oils = oils.concat(enumStructList(structures.derricks).filterProperty("status", BEING_BUILT));
		oils = oils.sort((one, two) => (distanceToBase(one) - distanceToBase(two)));

		if (oils.length > 10)
		{
			oils.length = 10;
		}

		return oils;
	}

	const oils = cached(getOilList, 5000);

	if (countFinishedStructList(structures.derricks) >= 4 * structListLimit(structures.gens))
	{
		return false;
	}

	for (let i = 0; i < oils.length; ++i)
	{
		if (captureOil(oils[i]) === BUILDRET.SUCCESS)
		{
			return true;
		}
	}

	return false;
};

_global.buildMinimumDerricks = function(count) {
	if (countFinishedStructList(structures.derricks) < count)
	{
		if (captureSomeOil())
		{
			return true;
		}
	}

	return false;
};

function buildExpand()
{
	if (myPower() > personality.maxPower)
	{
		switch (chooseObjectType())
		{
			case 0:
				if (needFastestResearch() === PROPULSIONUSAGE.GROUND)
				{
					if (buildMinimum(structures.factories, Infinity, IMPORTANCE.PEACETIME))
					{
						return true;
					}
				}
				// fall-through
			case 1:
				if (needFastestResearch() === PROPULSIONUSAGE.GROUND)
				{
					if (buildMinimum(structures.templateFactories, Infinity, IMPORTANCE.PEACETIME))
					{
						return true;
					}
						return true;
				}
				// fall-through
			case 3:
				if (buildMinimum(structures.vtolFactories, Infinity, IMPORTANCE.PEACETIME))
				{
					return true;
				}
		}
	}

	return false;
}

function buildEnergy()
{
	const oils = countFinishedStructList(structures.derricks);
	const gens = countStructList(structures.gens);

	if (oils > 4 * gens)
	{
		if (buildBasicStructure(structures.gens, IMPORTANCE.PEACETIME) !== BUILDRET.UNAVAILABLE)
		{
			return true;
		}
	}

	if (withChance(50))
	{
		if (captureSomeOil())
		{
			return true;
		}
	}

	return false;
}

function buildModules()
{
	for (let i = 0; i < modules.length; ++i)
	{
		if (modules[i].base === FACTORY && needFastestResearch() !== PROPULSIONUSAGE.GROUND)
		{
			continue;
		}

		const str = enumStruct(me, modules[i].base);

		for (let j = 0; j < str.length; ++j)
		{
			if (buildModule(str[j]) !== BUILDRET.UNAVAILABLE)
			{
				return true;
			}
		}
	}

	return false;
}

_global.buildVtols = function() {
	return buildMinimum(structures.vtolPads, enumDroid(me, DROID_WEAPON).filter(isVTOL).length, IMPORTANCE.PEACETIME);
};

function buildExtras()
{
	if (throttled(180000))
	{
		return false;
	}

	return (buildBasicStructure(structures.extras, IMPORTANCE.PEACETIME) !== BUILDRET.UNAVAILABLE);
}


// Warzone 2126: raise defensive ridgelines between the base and the nearest enemy.
// Units behind a ridge are hull-down (less direct-fire damage) and hidden from sight.
let earthworksBuilt = 0;

function nearestEnemyStart()
{
	let best;
	let bestDist = Infinity;
	for (let i = 0; i < maxPlayers; ++i)
	{
		if (i === me || !isEnemy(i) || !defined(startPositions[i]))
		{
			continue;
		}
		const d = distanceToBase(startPositions[i]);
		if (d < bestDist)
		{
			bestDist = d;
			best = startPositions[i];
		}
	}
	return best;
}

function earthworkSiteClear(x1, y1, x2, y2)
{
	if (x1 < 2 || y1 < 2 || x2 >= mapWidth - 2 || y2 >= mapHeight - 2)
	{
		return false;
	}
	for (let x = x1; x <= x2; ++x)
	{
		for (let y = y1; y <= y2; ++y)
		{
			if (terrainType(x, y) === TER_WATER || terrainType(x, y) === TER_CLIFFFACE)
			{
				return false;
			}
		}
	}
	const cx = Math.round((x1 + x2) / 2);
	const cy = Math.round((y1 + y2) / 2);
	// Keep clear of buildings and oil (the ground next to structures cannot move anyway)
	return enumRange(cx, cy, 4, ALL_PLAYERS, false).filter((obj) => (
		obj.type === STRUCTURE || (obj.type === FEATURE && obj.stattype === OIL_RESOURCE)
	)).length === 0;
}

let earthworkAttempts = 0;

function buildEarthworks()
{
	if (typeof orderDroidEarthwork === "undefined" || earthworksBuilt >= 4 || earthworkAttempts >= 20 || throttled(120000))
	{
		return false;
	}
	if (gameTime < 240000 || enumTrucks().length < 4)
	{
		return false; // get the economy going first
	}
	const enemy = nearestEnemyStart();
	if (!defined(enemy))
	{
		return false;
	}
	const dx = enemy.x - baseLocation.x;
	const dy = enemy.y - baseLocation.y;
	const len = Math.sqrt(dx * dx + dy * dy);
	if (len < 20)
	{
		return false;
	}
	const ux = dx / len;
	const uy = dy / len;
	const across = Math.abs(dx) > Math.abs(dy); // enemy mostly east or west: the ridge runs north-south
	const side = [0, 7, -7, 14][earthworksBuilt];
	// A ridge across the approach, 10 to 18 tiles out (at most a third of the way); later ridges to either side
	for (let reach = Math.min(18, len / 3); reach >= Math.min(10, len / 4); reach -= 2)
	{
		++earthworkAttempts;
		const cx = Math.round(baseLocation.x + ux * reach - uy * side);
		const cy = Math.round(baseLocation.y + uy * reach + ux * side);
		const x1 = across ? cx - 1 : cx - 3;
		const x2 = across ? cx : cx + 3;
		const y1 = across ? cy - 3 : cy - 1;
		const y2 = across ? cy + 3 : cy;
		if (!earthworkSiteClear(x1, y1, x2, y2) || !safeSpot(cx, cy))
		{
			continue;
		}
		const trucks = getTwoFreeTrucks().filter((truck) => (droidCanReach(truck, cx, cy)));
		if (trucks.length === 0)
		{
			return false;
		}
		trucks.forEach((truck) => {
			orderDroidEarthwork(truck, x1, y1, x2, y2, false);
		});
		++earthworksBuilt;
		return true;
	}
	++earthworksBuilt; // no usable site on this side: move on to the next one
	return false;
}

_global.buildDefenses = function() {
	if (chooseObjectType() !== 2)
	{
		return false;
	}

	if (withChance(33) && areThereGW() === true)
	{
		if (buildGateways())
		{
			return true;
		}

	}
	else if (withChance(50))
	{
		if (buildTowers()) // includes sensor towers and forts
		{
			return true;
		}
	}
	else
	{
		if (buildArty())
		{
			return true;
		}
	}

	return false;
};

function listOutdatedDefenses()
{
	for (const path in weaponStats)
	{
		for (const role in DEFROLE)
		{
			const list = weaponStatsToDefenses(weaponStats[path], DEFROLE[role]);

			for (let i = 0; i < list.length - 2; ++i)
			{
				if (isAvailable(list[i + 2]))
				{
					if (countStruct(list[i]) > 0)
					{
						return enumStruct(me, list[i]);
					}
				}
			}
		}
	}

	return [];
}

function recycleDefenses()
{
	const trucks = enumTrucks().filter(truckFree);

	if (trucks.length <= 0)
	{
		return false;
	}

	const list = listOutdatedDefenses();

	for (let i = 0; i < list.length; ++i)
	{
		for (let j = 0; j < trucks.length; ++j)
		{
			if (droidCanReach(trucks[j], list[i].x, list[i].y))
			{
				orderDroidObj(trucks[j], DORDER_DEMOLISH, list[i]);
				return true;
			}
		}
	}

	return false;
}

_global.checkConstruction = function() {
	if (enumTrucks().filter(truckFree).length === 0)
	{
		return;
	}

	if (functionSeries("construction", [
		buildEarthworks,
		finishStructures,
		buildOrder,
		buildExpand,
		buildShipyard,
		buildEnergy,
		buildModules,
		buildVtols,
		buildExtras,
		recycleDefenses,
		buildEarthworks,
		buildDefenses,
	]))
	{
		queue("checkConstruction");
	}
};

////////////////////////////////////////////////////////////////////////////////////////////
})(this);
