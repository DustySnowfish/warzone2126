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

var craft = null;
var tanks = [];
var phase = 0;

function landNear(x0, y0)
{
	for (var r = 1; r < 40; ++r)
	{
		for (var dy = -r; dy <= r; ++dy)
		{
			for (var dx = -r; dx <= r; ++dx)
			{
				if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) { continue; }
				var x = x0 + dx, y = y0 + dy;
				if (x < 3 || y < 3 || x >= mapWidth - 3 || y >= mapHeight - 3) { continue; }
				if (terrainType(x, y) !== TER_WATER && terrainType(x, y) !== TER_CLIFFFACE) { return { x: x, y: y }; }
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
	completeResearch("R-Vehicle-Body05", 0, true);
	completeResearch("R-Vehicle-Body-NavalLanding", 0, true);
	var s = startPositions[0];
	var w = findWaterNear(s.x, s.y);
	craft = addDroid(0, w.x, w.y, "Landing Craft", "NavalLanding", "NavalScrew", "", "", "MG1Mk1"); // (no weapon slots: the turret is ignored)
	var l = landNear(w.x, w.y);
	var far = landNear(w.x - 12, w.y - 12) || l;
	tanks.push(addDroid(0, l.x, l.y, "Tank A", "Body5REC", "wheeled01", "", "", "MG2Mk1"));
	tanks.push(addDroid(0, l.x, l.y, "Tank B", "Body5REC", "wheeled01", "", "", "MG2Mk1"));
	tanks.push(addDroid(0, far.x, far.y, "Tank C", "Body1REC", "wheeled01", "", "", "MG1Mk1"));
	debug("N4: craft type " + craft.droidType + " cap " + craft.cargoCapacity + " at " + craft.x + "," + craft.y + "; tanks at " + l.x + "," + l.y);
	centreView(w.x, w.y);
	hackNetOn();
	tanks.forEach(function(t) { orderDroidObj(t, DORDER_EMBARK, craft); });
	setTimer("tick", 3000);
}

function tick()
{
	var c = getObject(DROID, 0, craft.id);
	if (!c) { debug("N4: craft gone"); removeTimer("tick"); return; }
	var msg = "N4 t=" + gameTime + " craft at " + c.x + "," + c.y + " order " + c.order + " cargo " + c.cargoCount + " left " + c.cargoLeft;
	tanks.forEach(function(t) { var d = getObject(DROID, 0, t.id); msg += " | " + t.name + (d ? " at " + d.x + "," + d.y + " o" + d.order : " aboard/gone"); });
	debug(msg);
	if (phase === 0 && c.cargoLeft <= 6 - 5)
	{
		phase = 1;
		// sail to the nearest other player's island and land there
		var target = startPositions[1];
		orderDroidLoc(c, DORDER_DISEMBARK, target.x, target.y);
		debug("N4: all aboard, landing near player 1 at " + target.x + "," + target.y);
		centreView(c.x, c.y);
	}
}

function eventTransporterLanded(transport)
{
	debug("N4: landed at " + transport.x + "," + transport.y);
}

function eventTransporterEmbarked(transport)
{
	debug("N4: a unit embarked");
}
