// Warzone 2126 submarine classes test (extra script).
// - Coastal Submarine (quiet: heard at 60% of sonar range) on hold; an enemy sonar frigate starts
//   15 tiles away (outside 60% of 22 tiles) and sails to 8 tiles: the sub should be heard only then.
// - Missile Submarine with 2 torpedo tubes and 2 cruise missiles; an enemy structure on land 18 to
//   38 tiles away, seen through a player 0 sensor tower next to it; after 45 s the submarine is
//   ordered to strike it: the cruise missiles should fly in (it surfaces for 10 s per launch).
// Logs every 3 seconds ("Sub2 test").

function waterAt(x, y)
{
	for (var m = -1; m <= 1; ++m)
	{
		for (var q = -1; q <= 1; ++q)
		{
			if (x + m < 2 || y + q < 2 || x + m >= mapWidth - 2 || y + q >= mapHeight - 2 || terrainType(x + m, y + q) !== TER_WATER) { return false; }
		}
	}
	return true;
}

function waterLine(x0, y0, x1, y1)
{
	var n = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0)) + 1;
	for (var k = 0; k <= n; ++k)
	{
		if (!waterAt(Math.round(x0 + (x1 - x0) * k / n), Math.round(y0 + (y1 - y0) * k / n))) { return false; }
	}
	return true;
}

var coastal = null, missile = null, sonar = null, gen = null, line = null;

function alive(d, p) { return d ? getObject(DROID, p, d.id) : null; }

function seenBy(p, d)
{
	return d ? enumDroid(0, DROID_ANY, p).some(function(o) { return o.id === d.id; }) : false;
}

function report()
{
	var c = alive(coastal, 0), m = alive(missile, 0), s = alive(sonar, 1);
	var g = gen ? getObject(STRUCTURE, 1, gen.id) : null;
	var dist = (c && s) ? Math.round(distBetweenTwoPoints(c.x, c.y, s.x, s.y)) : -1;
	debug("Sub2 test t=" + Math.floor(gameTime / 1000) + "s coastal " + (c ? Math.round(c.health) + (seenBy(1, c) ? " HEARD" : " hidden") : "sunk") +
		" sonar dist " + dist + " | missile sub " + (m ? Math.round(m.health) + (seenBy(1, m) ? " SEEN" : " hidden") : "sunk") +
		" target " + (g ? Math.round(g.health) + (enumStruct(1, g.stattype, 0).some(function(o) { return o.id === g.id; }) ? " visible" : " unseen") : "destroyed") +
		(m ? " sub order " + m.order + " action " + m.action + " weapons " + m.weapons.map(function(w) { return w.name; }).join(",") : ""));
}

function eventStartLevel()
{
	queue("setup", 40000);
}

function setup()
{
	hackNetOff();
	["R-Vehicle-Body-NavalSubCoastal", "R-Vehicle-Body-NavalSubMissile", "R-Wpn-Missile-Cruise", "R-Wpn-Torpedo-Twin"].forEach(function(r) { completeResearch(r, 0, true); });
	["R-Sys-Sonar", "R-Vehicle-Body-NavalFrigate"].forEach(function(r) { completeResearch(r, 1, true); });
	var s = startPositions[0];
	// A straight 16-tile run of open water
	for (var r = 6; r < 80 && !line; ++r)
	{
		for (var a = 0; a < 32 && !line; ++a)
		{
			var x0 = Math.round(s.x + r * Math.cos(a * Math.PI / 16)), y0 = Math.round(s.y + r * Math.sin(a * Math.PI / 16));
			for (var b = 0; b < 16 && !line; ++b)
			{
				var x1 = Math.round(x0 + 16 * Math.cos(b * Math.PI / 8)), y1 = Math.round(y0 + 16 * Math.sin(b * Math.PI / 8));
				if (waterLine(x0, y0, x1, y1)) { line = { x0: x0, y0: y0, x1: x1, y1: y1 }; }
			}
		}
	}
	if (!line) { debug("Sub2 test: no water"); hackNetOn(); return; }
	var L = line;
	coastal = addDroid(0, L.x0, L.y0, "Coastal Sub", "NavalSubCoastal", "NavalScrew", "", "", "Torpedo-Twin");
	sonar = addDroid(1, L.x1, L.y1, "Enemy Sonar", "NavalFrigate", "NavalScrew", "", "", "Sensor-Sonar");
	// The missile submarine 3 tiles to the side of the coastal one, where there is water
	var spots = [[3, 0], [-3, 0], [0, 3], [0, -3], [2, 2], [-2, -2], [2, -2], [-2, 2]];
	var mx = L.x0, my = L.y0;
	for (var i = 0; i < spots.length; ++i)
	{
		if (waterAt(L.x0 + spots[i][0], L.y0 + spots[i][1])) { mx = L.x0 + spots[i][0]; my = L.y0 + spots[i][1]; break; }
	}
	missile = addDroid(0, mx, my, "Missile Sub", "NavalSubMissile", "NavalScrew", "", "", "Torpedo-Twin", "Torpedo-Twin", "Missile-Cruise", "Missile-Cruise");
	// Enemy generator on flat land 20 to 36 tiles away, with a player 0 sensor tower beside it
	for (var d = 20; d <= 36 && !gen; d += 2)
	{
		for (var k = 0; k < 24 && !gen; ++k)
		{
			var gx = Math.round(mx + d * Math.cos(k * Math.PI / 12)), gy = Math.round(my + d * Math.sin(k * Math.PI / 12));
			if (gx < 4 || gy < 4 || gx >= mapWidth - 4 || gy >= mapHeight - 4) { continue; }
			var land = true;
			for (var m = 0; m <= 1 && land; ++m) { for (var q = 0; q <= 1 && land; ++q) { if (terrainType(gx + m, gy + q) === TER_WATER) { land = false; } } }
			var pick = { x: gx, y: gy };
			if (land)
			{
				["A0PowerGenerator", "A0ResearchFacility", "WallTower01"].forEach(function(n) { if (!gen) { gen = addStructure(n, 1, pick.x * 128 + 64, pick.y * 128 + 64); } });
				if (!gen) { debug("Sub2 test: addStructure failed at " + pick.x + "," + pick.y); }
				if (gen)
				{
					var tower = null;
					[[3, 0], [-3, 0], [0, 3], [0, -3], [2, 2], [-2, 2]].forEach(function(o) { if (!tower && terrainType(pick.x + o[0], pick.y + o[1]) !== TER_WATER) { tower = addStructure("Sys-SensoTower02", 0, (pick.x + o[0]) * 128 + 64, (pick.y + o[1]) * 128 + 64); } });
					debug("Sub2 test: sensor tower " + (tower ? "placed" : "FAILED"));
				}
			}
		}
	}
	hackNetOn();
	[coastal, missile, sonar].forEach(function(o) { if (o) { orderDroid(o, DORDER_HOLD); } });
	debug("Sub2 test: coastal " + (coastal ? coastal.id : "fail") + " missile " + (missile ? missile.id : "fail") + " sonar " + (sonar ? sonar.id : "fail") +
		" target " + (gen ? gen.x + "," + gen.y + " dist " + Math.round(distBetweenTwoPoints(gen.x, gen.y, mx, my)) : "fail"));
	centreView(L.x0, L.y0);
	setTimer("report", 3000);
	queue("closeIn", 25000);
	queue("strike", 45000);
}

function strike()
{
	var m = alive(missile, 0);
	var g = gen ? getObject(STRUCTURE, 1, gen.id) : null;
	if (m && g)
	{
		orderDroidObj(m, DORDER_ATTACK, g);
		debug("Sub2 test: missile submarine ordered to strike the target");
	}
}

function closeIn()
{
	var s = alive(sonar, 1);
	if (s && line)
	{
		var L = line;
		orderDroidLoc(s, DORDER_MOVE, Math.round(L.x0 + (L.x1 - L.x0) / 2), Math.round(L.y0 + (L.y1 - L.y0) / 2));
		debug("Sub2 test: sonar frigate closes to 8 tiles");
	}
}
