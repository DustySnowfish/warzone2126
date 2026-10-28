// Warzone 2126 naval N7 test (extra script): sea construction on an Islands map. Player 0 gets a
// ship truck and a hover truck. The ship truck builds an offshore rig on the nearest offshore oil;
// the hover truck builds a sonar buoy and a torpedo launcher in the water near the base. Then an
// enemy patrol boat sails in (heard by the buoy from far away, sunk by the torpedoes) and an enemy
// tank waits on the shore in torpedo range (torpedoes must ignore it).
// Reports every 5 seconds (log lines starting "Sea test").

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


var shipTruck = null, hoverTruck = null, boat = null, tank = null;
var oil = null, buoySpot = null, torpSpot = null;
var phase = 0;
var spawnedAt = 0;

function nearestOffshoreOil(x, y)
{
	var best = null, bestD = 1e9;
	enumFeature(ALL_PLAYERS, "OilResource").forEach(function(f) {
		if (terrainType(f.x, f.y) !== TER_WATER) { return; }
		var d = distBetweenTwoPoints(x, y, f.x, f.y);
		if (d < bestD) { bestD = d; best = f; }
	});
	return best;
}

function count(name) { return enumStruct(0, name).filter(function(s) { return s.status === BUILT; }).length; }

function report()
{
	var b = boat ? getObject(DROID, 1, boat.id) : null;
	var t = tank ? getObject(DROID, 1, tank.id) : null;
	var heard = b ? enumRange(b.x, b.y, 1, 1, true).some(function(o) { return o.id === boat.id; }) : false;
	debug("Sea test t=" + Math.floor(gameTime / 1000) + "s phase " + phase + " rigs " + count("A0OffshoreRig") + " buoys " + count("A0SonarBuoy") +
		" launchers " + count("Emplacement-Torpedo") + " boat " + (b ? (b.x + "," + b.y + " hp " + Math.round(b.health) + " visible " + heard) : "gone") +
		" tank " + (t ? ("hp " + Math.round(t.health)) : "gone") + " power " + playerPower(0));
}

function step()
{
	var t = gameTime / 1000;
	if (phase === 0 && t > 2)
	{
		var okRig = oil && orderDroidBuild(shipTruck, DORDER_BUILD, "A0OffshoreRig", oil.x, oil.y, 0);
		var okBuoy = buoySpot && orderDroidBuild(hoverTruck, DORDER_BUILD, "A0SonarBuoy", buoySpot.x, buoySpot.y, 0);
		debug("Sea test: rig ordered " + okRig + " at " + (oil ? oil.x + "," + oil.y : "?") + ", buoy ordered " + okBuoy);
		phase = 1;
	}
	else if (phase === 1 && count("A0SonarBuoy") > 0 && torpSpot)
	{
		var okT = orderDroidBuild(hoverTruck, DORDER_BUILD, "Emplacement-Torpedo", torpSpot.x, torpSpot.y, 0);
		debug("Sea test: torpedo launcher ordered " + okT + " at " + torpSpot.x + "," + torpSpot.y);
		phase = 2;
	}
	else if (phase === 2 && count("Emplacement-Torpedo") > 0)
	{
		// An enemy boat far out (beyond sight, inside sonar range) and a tank on the shore near the launcher
		var far = findWaterNear(torpSpot.x + 16, torpSpot.y + 6);
		hackNetOff();
		boat = far ? addDroid(1, far.x, far.y, "Enemy Boat", "NavalPatrol", "NavalScrew", "", "", "MG1Mk1") : null;
		tank = addDroid(1, start.x + 2, start.y + 2, "Enemy Tank", "Body1REC", "wheeled01", "", "", "MG1Mk1");
		hackNetOn();
		if (tank) { orderDroidLoc(tank, DORDER_MOVE, torpSpot.x, torpSpot.y); }
		debug("Sea test: enemy boat at " + (far ? far.x + "," + far.y : "?") + ", tank near the launcher");
		phase = 3;
	}
	else if (phase === 3 && boat && getObject(DROID, 1, boat.id))
	{
		// After 20 seconds out at sea, the boat sails at the launcher
		if (spawnedAt === 0) { spawnedAt = gameTime; }
		if (gameTime - spawnedAt > 20000)
		{
			orderDroidLoc(getObject(DROID, 1, boat.id), DORDER_MOVE, torpSpot.x + 3, torpSpot.y + 2);
			debug("Sea test: enemy boat sails at the launcher");
			phase = 4;
		}
	}
}

var start;

function eventStartLevel()
{
	hackNetOff();
	setPower(20000, 0);
	["R-Vehicle-Prop-Hover", "R-Vehicle-Prop-NavalScrew", "R-Struc-OffshoreRig", "R-Struc-SonarBuoy", "R-Defense-Torpedo"].forEach(function(r) { completeResearch(r, 0, true); });
	start = startPositions[0];
	var w = findWaterNear(start.x, start.y);
	oil = nearestOffshoreOil(start.x, start.y);
	shipTruck = w ? addDroid(0, w.x, w.y, "Ship Truck", "NavalPatrol", "NavalScrew", "", "", "Spade1Mk1") : null;
	hoverTruck = addDroid(0, start.x, start.y + 2, "Hover Truck", "Body1REC", "hover01", "", "", "Spade1Mk1");
	buoySpot = w ? findWaterNear(w.x + 4, w.y + 4) : null;
	torpSpot = w ? findWaterNear(w.x + 1, w.y - 2) : null;
	hackNetOn();
	debug("Sea test: ship truck " + (shipTruck ? shipTruck.id : "failed") + ", hover truck " + (hoverTruck ? hoverTruck.id : "failed") + ", offshore oil " + (oil ? oil.x + "," + oil.y : "none"));
	if (w) { centreView(w.x, w.y); }
	setTimer("report", 5000);
	setTimer("step", 1000);
}
