// Warzone 2126 crater routing test (extra script): bombs a band of deep craters across open ground
// near player 0's base, then sends two tanks from one side to the other. "Direct" is just ordered
// to the far side (the pathfinder should take it round the crater field); "Through" is sent via
// the middle of the field first. Logs "Crater route" lines with the tiles crossed and the times.

var site = null;
var tanks = {};
var bombsLeft = 0;

function clearGround(x1, y1, x2, y2)
{
	for (var x = x1; x <= x2; ++x)
	{
		for (var y = y1; y <= y2; ++y)
		{
			var t = terrainType(x, y);
			if (t === TER_WATER || t === TER_CLIFFFACE) { return false; }
		}
	}
	return enumArea(x1, y1, x2, y2, ALL_PLAYERS, false).filter(function(o) { return o.type === STRUCTURE; }).length === 0;
}

function eventStartLevel()
{
	setWeather(WEATHER_CLEAR);
	var s = startPositions[0];
	// Open ground 28 x 22 tiles: the route runs along x through the middle, the field is 6 deep and 10 wide
	for (var r = 8; r < 120 && !site; r += 2)
	{
		for (var a = 0; a < 32 && !site; ++a)
		{
			var x = Math.round(s.x + r * Math.cos(a * Math.PI / 16)) - 14, y = Math.round(s.y + r * Math.sin(a * Math.PI / 16)) - 11;
			if (x > 3 && y > 3 && x + 28 < mapWidth - 3 && y + 22 < mapHeight - 3 && clearGround(x, y, x + 28, y + 22))
			{
				site = { x: x, y: y };
			}
		}
	}
	if (!site) { debug("Crater route: no open ground"); return; }
	addSpotter(site.x + 14, site.y + 11, 0, 30 * 128, false, 0);
	centreView(site.x + 14, site.y + 11);
	debug("Crater route: site " + site.x + "," + site.y);
	bombsLeft = 12;
	setTimer("bomb", 1500);
}

function bomb()
{
	if (bombsLeft <= 0) { return; }
	--bombsLeft;
	// Field: x 7..20, y 8..14 (relative), a grid of heavy impacts every 2 tiles: long to cross, short to go round
	for (var dx = 7; dx <= 20; dx += 2)
	{
		for (var dy = 8; dy <= 14; dy += 2)
		{
			fireWeaponAtLoc("Bomb2-VTOL-HvHE", site.x + dx + (bombsLeft % 2), site.y + dy + (bombsLeft % 2), 0, true);
		}
	}
	if (bombsLeft === 0)
	{
		removeTimer("bomb");
		queue("send", 6000);
	}
}

function send()
{
	hackNetOff();
	var y = site.y + 11;
	tanks.direct = { droid: addDroid(0, site.x + 2, y - 1, "Direct", "Body5REC", "tracked01", "", "", "MG1Mk1"), name: "direct", start: gameTime, inField: 0, done: false };
	tanks.through = { droid: addDroid(0, site.x + 2, y + 1, "Through", "Body5REC", "tracked01", "", "", "MG1Mk1"), name: "through", start: gameTime, inField: 0, done: false, via: true };
	tanks.around = { droid: addDroid(0, site.x + 2, y + 3, "Around", "Body5REC", "tracked01", "", "", "MG1Mk1"), name: "around", start: gameTime, inField: 0, done: false, via: true, viaY: site.y + 18 };
	hackNetOn();
	orderDroidLoc(tanks.around.droid, DORDER_MOVE, site.x + 14, site.y + 18);
	orderDroidLoc(tanks.direct.droid, DORDER_MOVE, site.x + 26, y - 1);
	orderDroidLoc(tanks.through.droid, DORDER_MOVE, site.x + 14, y + 1);
	setTimer("track", 500);
}

function track()
{
	var y = site.y + 11, allDone = true;
	for (var k in tanks)
	{
		var t = tanks[k];
		if (t.done) { continue; }
		var d = getObject(DROID, 0, t.droid.id);
		if (!d) { t.done = true; continue; }
		var rx = d.x - site.x, ry = d.y - site.y;
		if (rx >= 7 && rx <= 21 && ry >= 8 && ry <= 15) { ++t.inField; }
		if (t.via && rx >= 13) { t.via = false; orderDroidLoc(d, DORDER_MOVE, site.x + 26, t.viaY ? y + 3 : y + 1); }
		if (rx >= 25)
		{
			t.done = true;
			debug("Crater route: " + t.name + " arrived in " + ((gameTime - t.start) / 1000).toFixed(1) + " s, " + (t.inField / 2).toFixed(1) + " s inside the crater field");
			continue;
		}
		allDone = false;
	}
	if (allDone) { removeTimer("track"); debug("Crater route: done"); }
}
