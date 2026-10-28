// Warzone 2126 torpedo test (extra script): a torpedo launcher on the water by player 0's base, a
// machinegun tower beside it as a control, an enemy boat and an enemy tank five tiles away.
// Logs health every 5 seconds ("Torp test").

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


var boat = null, tank = null, launcher = null;

function report()
{
	var b = boat ? getObject(DROID, 1, boat.id) : null;
	var t = tank ? getObject(DROID, 1, tank.id) : null;
	debug("Torp test t=" + Math.floor(gameTime / 1000) + "s boat " + (b ? Math.round(b.health) + " at " + b.x + "," + b.y : "sunk") + " tank " + (t ? Math.round(t.health) : "dead") +
		" launcher " + (launcher ? launcher.x + "," + launcher.y : "none") + " boat seen " + (b ? enumRange(b.x, b.y, 1, 1, true).length : -1));
}

function eventStartLevel()
{
	hackNetOff();
	completeResearch("R-Defense-Torpedo", 0, true);
	var s = startPositions[0];
	var w = findWaterNear(s.x, s.y);
	// Open water about 5 tiles out from the launcher, in a straight line over water
	var w2 = null;
	for (var a = 0; a < 16 && !w2; ++a)
	{
		var tx = Math.round(w.x + 5 * Math.cos(a * Math.PI / 8)), ty = Math.round(w.y + 5 * Math.sin(a * Math.PI / 8));
		var clear = true;
		for (var k = 1; k <= 6; ++k)
		{
			var px = Math.round(w.x + (tx - w.x) * k / 6), py = Math.round(w.y + (ty - w.y) * k / 6);
			if (terrainType(px, py) !== TER_WATER) { clear = false; break; }
		}
		if (clear) { w2 = { x: tx, y: ty }; }
	}
	launcher = addStructure("Emplacement-Torpedo", 0, w.x * 128 + 64, w.y * 128 + 64);
	boat = addDroid(1, w2.x, w2.y, "Enemy Boat", "NavalPatrol", "NavalScrew", "", "", "MG1Mk1");
	tank = addDroid(1, s.x + 3, s.y + 6, "Enemy Tank", "Body1REC", "wheeled01", "", "", "MG1Mk1");
	hackNetOn();
	if (boat) { orderDroid(boat, DORDER_HOLD); }
	if (tank) { orderDroid(tank, DORDER_HOLD); }
	debug("Torp test: launcher " + (launcher ? launcher.id + " " + launcher.status : "failed") + " at " + w.x + "," + w.y + ", boat at " + w2.x + "," + w2.y);
	centreView(w.x, w.y);
	setTimer("report", 5000);
}
