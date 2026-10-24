// Warzone 2126 naval test (extra script, normal rules): Islands, 4 players, ship research done and
// a small fleet in the water next to player 0's base. Build a Shipyard on the coast (Build menu,
// Base tab) to make more; ships are designed with the Patrol or Frigate hull and a ship propulsion.

function findWaterNear(x0, y0)
{
	for (var r = 1; r < 80; ++r)
	{
		for (var dy = -r; dy <= r; ++dy)
		{
			for (var dx = -r; dx <= r; ++dx)
			{
				if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) { continue; }
				var x = x0 + dx, y = y0 + dy;
				if (x < 4 || y < 4 || x >= mapWidth - 4 || y >= mapHeight - 4) { continue; }
				// Open water: this tile and its neighbours
				var open = true;
				for (var k = -1; k <= 1 && open; ++k)
				{
					for (var m = -1; m <= 1; ++m)
					{
						if (terrainType(x + k, y + m) !== TER_WATER) { open = false; break; }
					}
				}
				if (open) { return { x: x, y: y }; }
			}
		}
	}
	return null;
}

function eventStartLevel()
{
	hackNetOff();
	setPower(50000, 0);
	completeResearch("R-Vehicle-Prop-Hover", 0, true);
	completeResearch("R-Vehicle-Prop-NavalScrew", 0, true);
	completeResearch("R-Vehicle-Body-NavalFrigate", 0, true);
	completeResearch("R-Vehicle-Prop-NavalJet", 0, true);
	completeResearch("R-Vehicle-Body-NavalLanding", 0, true);
	completeResearch("R-Vehicle-Body-NavalSuperBattleship", 0, true); // (and the hulls before it)
	completeResearch("R-Vehicle-Body-NavalCarrier", 0, true); // (with VTOL propulsion and the rearming pad)
	completeResearch("R-Wpn-AAGun03", 0, true);
	var s = startPositions[0];
	var w = findWaterNear(s.x, s.y);
	if (w)
	{
		addDroid(0, w.x, w.y, "Patrol Boat", "NavalPatrol", "NavalScrew", "", "", "MG2Mk1");
		addDroid(0, w.x + 1, w.y, "Patrol Boat", "NavalPatrol", "NavalJet", "", "", "MG2Mk1");
		addDroid(0, w.x, w.y + 1, "Frigate", "NavalFrigate", "NavalScrew", "", "", "Cannon1Mk1", "Cannon1Mk1");
		addDroid(0, w.x + 3, w.y + 3, "Super Battleship", "NavalSuperBattleship", "NavalScrew", "", "", "Cannon2A-TMk1", "Cannon2A-TMk1", "Cannon2A-TMk1", "Cannon2A-TMk1", "Cannon2A-TMk1", "Cannon2A-TMk1", "Cannon2A-TMk1", "Cannon2A-TMk1");
		addDroid(0, w.x - 3, w.y + 3, "Cruiser", "NavalCruiser", "NavalJet", "", "", "Rocket-MRL", "Rocket-MRL", "Rocket-MRL", "Rocket-MRL", "Rocket-MRL");
		addDroid(0, w.x, w.y + 6, "Carrier", "NavalCarrier", "NavalScrew", "", "", "QuadMg1AAGun", "QuadMg1AAGun");
		centreView(w.x, w.y);
		// A truck builds a Shipyard on the nearest valid coast (tests the half-land, half-water rule)
		var trucks = enumDroid(0, DROID_CONSTRUCT);
		if (trucks.length > 0)
		{
			var loc = pickStructLocation(trucks[0], "A0Shipyard", w.x, w.y);
			if (loc)
			{
				orderDroidBuild(trucks[0], DORDER_BUILD, "A0Shipyard", loc.x, loc.y);
				console("Naval test: Shipyard ordered at " + loc.x + ", " + loc.y);
				debug("Naval test: Shipyard ordered at " + loc.x + ", " + loc.y);
			}
			else
			{
				console("Naval test: no valid Shipyard location found");
				debug("Naval test: no valid Shipyard location found");
			}
		}
	}
	else
	{
		console("Naval test: no open water found near the base");
	}
	hackNetOn();
}

function eventStructureBuilt(structure, droid)
{
	if (structure.player === 0 && structure.name === "Shipyard")
	{
		var ok = buildDroid(structure, "Patrol Boat", "NavalPatrol", "NavalScrew", "", "", "MG1Mk1");
		debug("Naval test: Shipyard built, production started: " + ok);
		console("Naval test: Shipyard built, production started: " + ok);
	}
}

function eventDroidBuilt(droid, structure)
{
	if (droid.player === 0 && structure && structure.name === "Shipyard")
	{
		debug("Naval test: ship launched at " + droid.x + ", " + droid.y);
		console("Naval test: ship launched at " + droid.x + ", " + droid.y);
	}
}
