// Warzone 2126 submarine test (extra script). Player 0 has an Attack Submarine (Twin Torpedo Tubes)
// 9 tiles from two enemy frigates (machine guns).
// - 40 s: setup; the submarine holds fire: it should stay hidden from player 1 (not seen, not shot).
// - 60 s: it attacks frigate A: it surfaces, becomes visible and the frigates fire back.
// - about 8 s after it stops firing it dives and vanishes again.
// - 115 s: player 1 gets a Sonar frigate and a Depth Charge frigate next to the frigates: the
//   submarine is heard and depth-charged.
// Logs every 3 seconds ("Sub test").

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

var sub = null, theirs = [], hunters = [], spot = null;

function hp(list, p)
{
	return list.map(function(d) { var o = d ? getObject(DROID, p, d.id) : null; return o ? Math.round(o.health) : "sunk"; }).join("/");
}

function report()
{
	var s = sub ? getObject(DROID, 0, sub.id) : null;
	var seen = s ? enumDroid(0, DROID_ANY, 1).some(function(d) { return d.id === s.id; }) : false;
	debug("Sub test t=" + Math.floor(gameTime / 1000) + "s sub " + (s ? Math.round(s.health) : "sunk") + (seen ? " SEEN by enemy" : " hidden") +
		" enemy " + hp(theirs, 1) + " hunters " + hp(hunters, 1));
}

function eventStartLevel()
{
	queue("setup", 40000);
}

function setup()
{
	hackNetOff();
	["R-Vehicle-Body-NavalSub", "R-Wpn-Torpedo-Twin", "R-Vehicle-Body-NavalFrigate"].forEach(function(r) { completeResearch(r, 0, true); });
	["R-Sys-Sonar", "R-Wpn-DepthCharge", "R-Vehicle-Body-NavalFrigate"].forEach(function(r) { completeResearch(r, 1, true); });
	var s = startPositions[0];
	for (var r = 6; r < 70 && !spot; ++r)
	{
		for (var a = 0; a < 32 && !spot; ++a)
		{
			var x0 = Math.round(s.x + r * Math.cos(a * Math.PI / 16)), y0 = Math.round(s.y + r * Math.sin(a * Math.PI / 16));
			for (var b = 0; b < 16 && !spot; ++b)
			{
				var x1 = Math.round(x0 + 9 * Math.cos(b * Math.PI / 8)), y1 = Math.round(y0 + 9 * Math.sin(b * Math.PI / 8));
				var ox = Math.round(-(y1 - y0) / 3), oy = Math.round((x1 - x0) / 3);
				if (waterLine(x0, y0, x1, y1) && waterLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy))
				{
					spot = { x0: x0, y0: y0, x1: x1, y1: y1, ox: ox, oy: oy };
				}
			}
		}
	}
	if (!spot) { debug("Sub test: no water"); hackNetOn(); return; }
	var f = spot;
	sub = addDroid(0, f.x0, f.y0, "Attack Sub", "NavalSubAttack", "NavalScrew", "", "", "Torpedo-Twin", "Torpedo-Twin");
	theirs.push(addDroid(1, f.x1, f.y1, "Enemy A", "NavalFrigate", "NavalScrew", "", "", "MG3Mk1", "MG3Mk1"));
	theirs.push(addDroid(1, f.x1 + f.ox, f.y1 + f.oy, "Enemy B", "NavalFrigate", "NavalScrew", "", "", "MG3Mk1", "MG3Mk1"));
	hackNetOn();
	[sub].concat(theirs).forEach(function(d) { if (d) { orderDroid(d, DORDER_HOLD); } });
	debug("Sub test: sub " + (sub ? sub.id : "fail") + " enemy " + theirs.map(function(d) { return d ? d.id : "fail"; }).join(",") + " at " + f.x0 + "," + f.y0 + " -> " + f.x1 + "," + f.y1);
	centreView(Math.round((f.x0 + f.x1) / 2), Math.round((f.y0 + f.y1) / 2));
	setTimer("report", 3000);
	queue("attack", 20000);
	queue("hunt", 75000);
}

function attack()
{
	var s = sub ? getObject(DROID, 0, sub.id) : null;
	var t = theirs[0] ? getObject(DROID, 1, theirs[0].id) : null;
	if (s && t)
	{
		orderDroidObj(s, DORDER_ATTACK, t);
		debug("Sub test: attack ordered");
		queue("holdFire", 8000);
	}
}

function holdFire()
{
	var s = sub ? getObject(DROID, 0, sub.id) : null;
	if (!s) { return; }
	// (no script call sets fire-at-will: sail out of range instead)
	var f = spot, dx = f.x0 - f.x1, dy = f.y0 - f.y1, len = Math.sqrt(dx * dx + dy * dy);
	for (var d = 10; d >= 3; --d)
	{
		for (var a = 0; a < 9; ++a)
		{
			var ang = Math.atan2(dy, dx) + (a % 2 ? 1 : -1) * Math.ceil(a / 2) * 0.35;
			var px = Math.round(f.x0 + d * Math.cos(ang)), py = Math.round(f.y0 + d * Math.sin(ang));
			if (waterLine(f.x0, f.y0, px, py))
			{
				orderDroidLoc(s, DORDER_MOVE, px, py);
				debug("Sub test: sub withdraws to " + px + "," + py + " (" + (d + Math.round(len)) + " tiles from the enemy; should dive 8 s after its last shot)");
				return;
			}
		}
	}
	debug("Sub test: no room to withdraw");
}

function hunt()
{
	var f = spot;
	hackNetOff();
	hunters.push(addDroid(1, f.x1 - f.ox, f.y1 - f.oy, "Enemy Sonar", "NavalFrigate", "NavalScrew", "", "", "Sensor-Sonar"));
	hunters.push(addDroid(1, Math.round((f.x0 + f.x1) / 2), Math.round((f.y0 + f.y1) / 2) + 1, "Enemy DC", "NavalFrigate", "NavalScrew", "", "", "DepthCharge-Rack", "DepthCharge-Rack"));
	hackNetOn();
	debug("Sub test: hunters " + hunters.map(function(d) { return d ? d.id : "fail"; }).join(","));
	queue("chase", 4000);
}

function chase()
{
	// The depth charge frigate and frigate B (machine guns: must not be able to hurt it while dived)
	// go after the submarine
	var s = sub ? getObject(DROID, 0, sub.id) : null;
	[hunters[1], theirs[1]].forEach(function(d)
	{
		var o = d ? getObject(DROID, 1, d.id) : null;
		if (o && s) { orderDroidObj(o, DORDER_ATTACK, s); }
	});
	debug("Sub test: hunters chase");
}
