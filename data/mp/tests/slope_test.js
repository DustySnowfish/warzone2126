// Warzone 2126 earthworks slope test (extra script): on a Plateaus map, player 0's trucks cut a ramp
// across a cliff near their base. Reports every 10 seconds whether wheels can reach the high side and
// how many cliff tiles remain in the ramp.

var ramp = null;

function isCliff(x, y) { return terrainType(x, y) === TER_CLIFFFACE; }

// A cliff run along x: open ground, 1 to 3 cliff tiles, open ground at another height
function findCliff(x0, y0)
{
	for (var r = 4; r < 40; ++r)
	{
		for (var dy = -r; dy <= r; ++dy)
		{
			for (var dx = -r; dx <= r; ++dx)
			{
				if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) { continue; }
				var x = x0 + dx, y = y0 + dy;
				if (x < 8 || y < 8 || x >= mapWidth - 12 || y >= mapHeight - 8) { continue; }
				if (isCliff(x - 1, y) || !isCliff(x, y)) { continue; }
				var n = 0;
				while (n < 4 && isCliff(x + n, y)) { ++n; }
				if (n > 3 || isCliff(x + n, y)) { continue; }
				// Open ground for 2 tiles on both sides, across 3 rows
				var ok = true;
				for (var k = -1; k <= 1 && ok; ++k)
				{
					for (var m = 1; m <= 2; ++m)
					{
						if (isCliff(x - m, y + k) || isCliff(x + n - 1 + m, y + k) || terrainType(x - m, y + k) === TER_WATER || terrainType(x + n - 1 + m, y + k) === TER_WATER) { ok = false; break; }
					}
				}
				if (!ok) { continue; }
				var low = MapTiles[y][x - 2].height, high = MapTiles[y][x + n + 1].height;
				if (Math.abs(high - low) < 60) { continue; }
				return { x1: x - 2, y: y, x2: x + n + 1, low: low, high: high };
			}
		}
	}
	return null;
}

function countCliffs()
{
	var c = 0;
	for (var x = Math.min(ramp.x1, ramp.x2); x <= Math.max(ramp.x1, ramp.x2); ++x)
	{
		for (var k = -1; k <= 1; ++k) { if (isCliff(x, ramp.y + k)) { ++c; } }
	}
	return c;
}

var tank = null, tankStart = 0, done = false;

function report()
{
	if (!ramp) { return; }
	if (tank && !done)
	{
		var t = getObject(DROID, 0, tank.id);
		if (t && Math.abs(t.x - ramp.x2) <= 1 && Math.abs(t.y - ramp.y) <= 1)
		{
			done = true;
			debug("Slope test: tank reached the high side in " + Math.floor((gameTime - tankStart) / 1000) + "s");
		}
	}
	if (tank) { var tt = getObject(DROID, 0, tank.id); if (tt) { debug("Slope test tank at " + tt.x + "," + tt.y); } }
	var busy = enumDroid(0, DROID_CONSTRUCT).filter(function(d) { return d.order !== 0; }).length;
	if (!tank && busy === 0 && gameTime > 15000)
	{
		hackNetOff();
		tank = addDroid(0, ramp.x1, ramp.y, "Ramp tester", "Body1REC", "wheeled01", "", "", "MG1Mk1");
		hackNetOn();
		if (!tank) { debug("Slope test: could not add the tank"); tank = { id: -1 }; done = true; }
		if (!done)
		{
			tankStart = gameTime;
			orderDroidLoc(tank, DORDER_MOVE, ramp.x2, ramp.y);
			debug("Slope test: earthwork done at t=" + Math.floor(gameTime / 1000) + "s, tank sent up the ramp");
		}
	}
	var ok = propulsionCanReach("wheeled01", ramp.x1, ramp.y, ramp.x2, ramp.y);
	console("Slope test: cliffs left " + countCliffs() + ", wheels reach high side: " + ok);
	debug("Slope test t=" + Math.floor(gameTime / 1000) + "s cliffs " + countCliffs() + " reach " + ok);
}

function eventStartLevel()
{
	hackNetOff();
	var start = startPositions[0];
	ramp = findCliff(start.x, start.y);
	if (!ramp)
	{
		console("Slope test: no suitable cliff found");
		debug("Slope test: no cliff");
		return;
	}
	debug("Slope test: ramp (" + ramp.x1 + "," + ramp.y + ") -> (" + ramp.x2 + "," + ramp.y + ") heights " + ramp.low + " -> " + ramp.high);
	var trucks = enumDroid(0, DROID_CONSTRUCT);
	for (var i = 0; i < trucks.length; ++i)
	{
		orderDroidEarthwork(trucks[i], ramp.x1, ramp.y, ramp.x2, ramp.y, 3);
	}
	centreView(Math.floor((ramp.x1 + ramp.x2) / 2), ramp.y);
	report();
	setTimer("report", 10000);
}
