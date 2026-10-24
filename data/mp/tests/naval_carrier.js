// Warzone 2126 naval N6 test (extra script): an aircraft carrier and seven VTOLs (one more than the
// deck holds) next to player 0's base on an Islands map. The VTOLs are sent to the carrier, the
// carrier sails, the VTOLs strike an enemy base and come back to rearm, then the carrier is sunk.
// Reports every 5 seconds (console and log lines starting "Carrier test").

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


var carrier = null;
var vtols = [];
var phase = 0;
var target = null;

function docked()
{
	var c = carrier ? getObject(DROID, 0, carrier.id) : null;
	if (!c) { return -1; }
	var n = 0;
	for (var i = 0; i < vtols.length; ++i)
	{
		var v = getObject(DROID, 0, vtols[i].id);
		if (v && v.order === DORDER_CARRIER && distBetweenTwoPoints(v.x, v.y, c.x, c.y) < 2.5) { ++n; }
	}
	return n;
}

function aliveVtols()
{
	return vtols.filter(function(v) { return getObject(DROID, 0, v.id) !== null; });
}

function report()
{
	var c = carrier ? getObject(DROID, 0, carrier.id) : null;
	var orders = aliveVtols().map(function(v) { var d = getObject(DROID, 0, v.id); return d.order + "@" + d.x + "," + d.y; }).join(" ");
	var msg = "Carrier test t=" + Math.floor(gameTime / 1000) + "s phase " + phase + " carrier " + (c ? (c.x + "," + c.y + " hp " + c.health) : "gone") +
		" docked " + docked() + " vtols alive " + aliveVtols().length + " [" + orders + "]";
	debug(msg);
	console("Carrier: docked " + docked() + ", VTOLs " + aliveVtols().length);
}

function step()
{
	var t = gameTime / 1000;
	var c = carrier ? getObject(DROID, 0, carrier.id) : null;
	if (phase === 0 && t > 3)
	{
		vtols.forEach(function(v) { orderDroidObj(v, DORDER_CARRIER, carrier); });
		phase = 1;
		debug("Carrier test: VTOLs sent to the carrier");
	}
	else if (phase === 1 && t > 50 && c)
	{
		var w = findWaterNear(c.x + 12, c.y + 6);
		if (w) { orderDroidLoc(c, DORDER_MOVE, w.x, w.y); }
		phase = 2;
		debug("Carrier test: carrier sails to " + (w ? w.x + "," + w.y : "?"));
	}
	else if (phase === 2 && t > 90)
	{
		var enemies = enumStruct(1);
		if (enemies.length > 0)
		{
			target = enemies[0];
			aliveVtols().forEach(function(v) { orderDroidObj(getObject(DROID, 0, v.id), DORDER_ATTACK, target); });
			debug("Carrier test: VTOLs attack " + target.name + " at " + target.x + "," + target.y);
		}
		phase = 3;
	}
	else if (phase === 3 && t > 200 && c)
	{
		var w2 = findWaterNear(c.x - 10, c.y + 4);
		if (w2) { orderDroidLoc(c, DORDER_MOVE, w2.x, w2.y); }
		debug("Carrier test: carrier sails with " + docked() + " VTOLs aboard to " + (w2 ? w2.x + "," + w2.y : "?"));
		phase = 4;
	}
	else if (phase === 4 && t > 280 && c)
	{
		debug("Carrier test: sinking the carrier with " + docked() + " VTOLs aboard");
		removeObject(c, true);
		phase = 5;
	}
}

function eventStartLevel()
{
	hackNetOff();
	setPower(50000, 0);
	["R-Vehicle-Prop-NavalScrew", "R-Vehicle-Body05", "R-Vehicle-Prop-VTOL", "R-Struc-VTOLFactory", "R-Struc-VTOLPad",
	 "R-Wpn-Cannon1Mk1", "R-Wpn-AAGun03", "R-Vehicle-Body-NavalCarrier"].forEach(function(r) { completeResearch(r, 0, true); });
	var start = startPositions[0];
	var w = findWaterNear(start.x, start.y);
	var l = landNear(start.x, start.y);
	if (!w || !l)
	{
		debug("Carrier test: no water or land found");
		return;
	}
	carrier = addDroid(0, w.x, w.y, "Carrier", "NavalCarrier", "NavalScrew", "", "", "QuadMg1AAGun", "QuadMg1AAGun");
	for (var i = 0; i < 7; ++i)
	{
		var v = addDroid(0, l.x + (i % 3), l.y + Math.floor(i / 3), "VTOL " + i, "Body5REC", "V-Tol", "", "", "Cannon1-VTOL");
		if (v) { vtols.push(v); }
	}
	hackNetOn();
	debug("Carrier test: carrier " + (carrier ? carrier.id : "failed") + " at " + w.x + "," + w.y + ", " + vtols.length + " VTOLs at " + l.x + "," + l.y);
	centreView(w.x, w.y);
	setTimer("report", 5000);
	setTimer("step", 1000);
}
