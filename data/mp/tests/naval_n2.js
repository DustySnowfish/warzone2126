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

var fleet = {};
var yard = null;
var farPoint = null;

function eventStartLevel()
{
	hackNetOff();
	setPower(50000, 0);
	completeResearch("R-Vehicle-Prop-Hover", 0, true);
	completeResearch("R-Vehicle-Prop-NavalScrew", 0, true);
	completeResearch("R-Vehicle-Body-NavalFrigate", 0, true);
	completeResearch("R-Vehicle-Prop-NavalJet", 0, true);
	var s = startPositions[0];
	var w = findWaterNear(s.x, s.y);
	fleet.a = addDroid(0, w.x, w.y, "Patrol Boat", "NavalPatrol", "NavalScrew", "", "", "MG2Mk1");
	fleet.b = addDroid(0, w.x + 1, w.y, "Patrol Jet", "NavalPatrol", "NavalJet", "", "", "MG2Mk1");
	fleet.f = addDroid(0, w.x, w.y + 1, "Frigate", "NavalFrigate", "NavalScrew", "", "", "Cannon1Mk1", "Cannon1Mk1");
	farPoint = findWaterNear(w.x + 14, w.y + 14);
	centreView(w.x, w.y);
	var trucks = enumDroid(0, DROID_CONSTRUCT);
	var loc = pickStructLocation(trucks[0], "A0Shipyard", w.x, w.y);
	orderDroidBuild(trucks[0], DORDER_BUILD, "A0Shipyard", loc.x, loc.y);
	debug("N2: shipyard ordered at " + loc.x + "," + loc.y + " far " + farPoint.x + "," + farPoint.y);
	hackNetOn();
	setTimer("report", 3000);
}

function report()
{
	var f = getObject(DROID, 0, fleet.f.id);
	var b = getObject(DROID, 0, fleet.b.id);
	var a = getObject(DROID, 0, fleet.a.id);
	var msg = "N2 t=" + gameTime;
	if (f) { msg += " frigate hp " + f.health + " order " + f.order + " at " + f.x + "," + f.y; }
	if (a) { msg += " | boat at " + a.x + "," + a.y; }
	if (b) { msg += " | jet at " + b.x + "," + b.y; } else { msg += " | jet gone"; }
	if (yard) { msg += " | yard " + yard.x + "," + yard.y; }
	debug(msg);
}

function eventStructureBuilt(structure, droid)
{
	if (structure.player === 0 && structure.name === "Shipyard")
	{
		yard = structure;
		setHealth(fleet.f, 25);
		orderDroid(fleet.f, DORDER_RTR);
		orderDroidLoc(fleet.a, DORDER_SCOUT, farPoint.x, farPoint.y);
		orderDroidLoc(fleet.b, DORDER_SCOUT, farPoint.x, farPoint.y);
		debug("N2: shipyard built at " + structure.x + "," + structure.y + "; frigate damaged and sent to repair; boats sent out");
		queue("turnBack", 12000);
	}
}

function turnBack()
{
	var s = startPositions[0];
	var w = findWaterNear(s.x, s.y);
	orderDroidLoc(fleet.a, DORDER_SCOUT, w.x, w.y);
	orderDroidLoc(fleet.b, DORDER_SCOUT, w.x + 2, w.y);
	debug("N2: boats turned back");
	queue("sinkJet", 12000);
}

function sinkJet()
{
	var b = getObject(DROID, 0, fleet.b.id);
	if (b)
	{
		centreView(b.x, b.y);
		removeObject(b, true);
		debug("N2: jet destroyed at " + b.x + "," + b.y);
	}
}
