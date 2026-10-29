// Warzone 2126 torpedo tube test (extra script): two player 0 frigates, one with Twin Torpedo Tubes
// (guided) and one with a Heavy Torpedo (straight), facing two enemy frigates about 7 tiles away
// in open water, plus an enemy tank on the shore that the torpedoes must ignore.
// Logs health every 3 seconds ("Tubes test").

function waterLine(x0, y0, x1, y1)
{
	var n = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0)) + 1;
	for (var k = 0; k <= n; ++k)
	{
		var px = Math.round(x0 + (x1 - x0) * k / n), py = Math.round(y0 + (y1 - y0) * k / n);
		for (var m = -1; m <= 1; ++m)
		{
			for (var q = -1; q <= 1; ++q)
			{
				if (px + m < 2 || py + q < 2 || px + m >= mapWidth - 2 || py + q >= mapHeight - 2) { return false; }
				if (terrainType(px + m, py + q) !== TER_WATER) { return false; }
			}
		}
	}
	return true;
}

var mine = [], theirs = [], tank = null;

function hp(list, p)
{
	return list.map(function(d) { var o = d ? getObject(DROID, p, d.id) : null; return o ? Math.round(o.health) : "sunk"; }).join("/");
}

function report()
{
	var t = tank ? getObject(DROID, 1, tank.id) : null;
	debug("Tubes test t=" + Math.floor(gameTime / 1000) + "s twin/heavy " + hp(mine, 0) + " enemy " + hp(theirs, 1) + " tank " + (t ? Math.round(t.health) : "dead"));
}

function eventStartLevel()
{
	// Spawn after a delay so slow software renderers have drawn the scene by then
	queue("setup", 40000);
}

function setup()
{
	hackNetOff();
	completeResearch("R-Wpn-Torpedo-Twin", 0, true);
	completeResearch("R-Wpn-Torpedo-Heavy", 0, true);
	var s = startPositions[0];
	var found = null;
	for (var r = 6; r < 70 && !found; ++r)
	{
		for (var a = 0; a < 32 && !found; ++a)
		{
			var x0 = Math.round(s.x + r * Math.cos(a * Math.PI / 16)), y0 = Math.round(s.y + r * Math.sin(a * Math.PI / 16));
			for (var b = 0; b < 16 && !found; ++b)
			{
				var x1 = Math.round(x0 + 7 * Math.cos(b * Math.PI / 8)), y1 = Math.round(y0 + 7 * Math.sin(b * Math.PI / 8));
				var ox = Math.round(-(y1 - y0) / 3), oy = Math.round((x1 - x0) / 3);
				if (waterLine(x0, y0, x1, y1) && waterLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy))
				{
					found = { x0: x0, y0: y0, x1: x1, y1: y1, ox: ox, oy: oy };
				}
			}
		}
	}
	if (!found) { debug("Tubes test: no water"); hackNetOn(); return; }
	var f = found;
	mine.push(addDroid(0, f.x0, f.y0, "Twin Tubes", "NavalFrigate", "NavalScrew", "", "", "Torpedo-Twin"));
	mine.push(addDroid(0, f.x0 + f.ox, f.y0 + f.oy, "Heavy Tube", "NavalFrigate", "NavalScrew", "", "", "Torpedo-Heavy"));
	theirs.push(addDroid(1, f.x1, f.y1, "Enemy A", "NavalFrigate", "NavalScrew", "", "", "MG1Mk1"));
	theirs.push(addDroid(1, f.x1 + f.ox, f.y1 + f.oy, "Enemy B", "NavalFrigate", "NavalScrew", "", "", "MG1Mk1"));
	tank = addDroid(1, s.x + 3, s.y + 6, "Enemy Tank", "Body1REC", "wheeled01", "", "", "MG1Mk1");
	hackNetOn();
	mine.concat(theirs).concat([tank]).forEach(function(d) { if (d) { orderDroid(d, DORDER_HOLD); } });
	debug("Tubes test: mine " + mine.map(function(d) { return d ? d.id : "fail"; }).join(",") + " enemy " + theirs.map(function(d) { return d ? d.id : "fail"; }).join(",") + " at " + f.x0 + "," + f.y0 + " -> " + f.x1 + "," + f.y1);
	centreView(Math.round((f.x0 + f.x1) / 2), Math.round((f.y0 + f.y1) / 2));
	setTimer("report", 3000);
}
