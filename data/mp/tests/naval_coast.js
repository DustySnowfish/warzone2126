// Warzone 2126 coastal battery test (extra script): a Coastal Battery on the coast by player 0's base
// with a sonar buoy offshore, an enemy Destroyer about 16 tiles out and an enemy Frigate about 21
// tiles out (both beyond the battery's own sight, revealed by the buoy). Also asks the engine for a
// truck build spot for the battery to check the coast placement rule. Logs every 4 seconds ("Coast test").

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



function isLand(x, y)
{
	if (x < 3 || y < 3 || x >= mapWidth - 3 || y >= mapHeight - 3) { return false; }
	var t = terrainType(x, y);
	return t !== TER_WATER && t !== TER_CLIFFFACE;
}

var battery = null, ships = [];

function report()
{
	debug("Coast test t=" + Math.floor(gameTime / 1000) + "s battery " + (battery ? (getObject(STRUCTURE, 0, battery.id) ? Math.round(getObject(STRUCTURE, 0, battery.id).health) : "destroyed") : "none") +
		" ships " + ships.map(function(d) { var o = d ? getObject(DROID, 1, d.id) : null; return o ? Math.round(o.health) + "@" + o.x + "," + o.y : "sunk"; }).join(" "));
}

function setup()
{
	hackNetOff();
	completeResearch("R-Defense-CoastalGun", 0, true);
	completeResearch("R-Struc-SonarBuoy", 0, true);
	var s = startPositions[0];
	var w = findWaterNear(s.x, s.y);
	// Step back from the water onto land for a 2 x 2 spot with water within 2 tiles
	var spot = null;
	for (var r = 1; r < 6 && !spot; ++r)
	{
		for (var dy = -r; dy <= r && !spot; ++dy)
		{
			for (var dx = -r; dx <= r && !spot; ++dx)
			{
				var x = w.x + dx, y = w.y + dy;
				if (isLand(x, y) && isLand(x + 1, y) && isLand(x, y + 1) && isLand(x + 1, y + 1)) { spot = { x: x, y: y }; }
			}
		}
	}
	// Out to sea: the direction from the spot through the water tile, continued
	var vx = w.x - spot.x, vy = w.y - spot.y, len = Math.sqrt(vx * vx + vy * vy) || 1;
	vx /= len; vy /= len;
	function seaAt(d)
	{
		for (var a = 0; a < 24; ++a)
		{
			var ang = Math.atan2(vy, vx) + (a % 2 ? 1 : -1) * Math.floor((a + 1) / 2) * 0.12;
			var x = Math.round(spot.x + d * Math.cos(ang)), y = Math.round(spot.y + d * Math.sin(ang));
			if (x > 3 && y > 3 && x < mapWidth - 3 && y < mapHeight - 3 && terrainType(x, y) === TER_WATER && terrainType(x + 1, y) === TER_WATER && terrainType(x, y + 1) === TER_WATER) { return { x: x, y: y }; }
		}
		return null;
	}
	var buoyAt = seaAt(7), d1 = seaAt(16), d2 = seaAt(21);
	var truck = enumDroid(0, DROID_CONSTRUCT)[0];
	var picked = truck ? pickStructLocation(truck, "Emplacement-CoastalGun", spot.x, spot.y, 0) : null;
	if (truck)
	{
		["Emplacement-HvyATrocket", "Emplacement-Torpedo", "Emplacement-CoastalGun"].forEach(function(st) {
			var q = pickStructLocation(truck, st, spot.x, spot.y, 0);
			var near = -1;
			if (q)
			{
				for (var r = 0; r < 8 && near < 0; ++r)
				{
					for (var dy = -r; dy <= r && near < 0; ++dy)
					{
						for (var dx = -r; dx <= r; ++dx)
						{
							if (terrainType(q.x + dx, q.y + dy) === TER_WATER) { near = r; break; }
						}
					}
				}
			}
			debug("Coast test pick " + st + " from " + spot.x + "," + spot.y + " -> " + (q ? q.x + "," + q.y + " water within " + near : "none") + " truck at " + truck.x + "," + truck.y + " reach " + propulsionCanReach("wheeled01", truck.x, truck.y, spot.x, spot.y));
		});
	}
	battery = addStructure("Emplacement-CoastalGun", 0, spot.x * 128 + 128, spot.y * 128 + 128);
	if (buoyAt) { addStructure("A0SonarBuoy", 0, buoyAt.x * 128 + 64, buoyAt.y * 128 + 64); }
	if (d1) { ships.push(addDroid(1, d1.x, d1.y, "Enemy Destroyer", "NavalDestroyer", "NavalScrew", "", "", "Cannon2A-TMk1", "Cannon2A-TMk1", "Cannon2A-TMk1")); }
	if (d2) { ships.push(addDroid(1, d2.x, d2.y, "Enemy Frigate", "NavalFrigate", "NavalScrew", "", "", "MG3Mk1", "MG3Mk1")); }
	hackNetOn();
	ships.forEach(function(d) { if (d) { orderDroid(d, DORDER_HOLD); } });
	debug("Coast test: battery " + (battery ? battery.id : "failed") + " at " + spot.x + "," + spot.y + " picked " + (picked ? picked.x + "," + picked.y : "none") +
		" buoy " + (buoyAt ? buoyAt.x + "," + buoyAt.y : "none") + " ships at " + (d1 ? d1.x + "," + d1.y : "-") + " " + (d2 ? d2.x + "," + d2.y : "-"));
	centreView(Math.round((spot.x + (d1 ? d1.x : spot.x)) / 2), Math.round((spot.y + (d1 ? d1.y : spot.y)) / 2));
	setTimer("report", 4000);
}

function eventStartLevel()
{
	// Spawn after a delay so slow software renderers have drawn the scene by then
	queue("setup", 40000);
}
