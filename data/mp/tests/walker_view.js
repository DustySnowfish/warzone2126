// Warzone 2126 walker test (extra script): player 0's walkers (biped, quad, hex on light, medium
// and heavy bodies) on open ground near its base, close camera. They walk, a biped jumps a wall line,
// then an enemy tank arrives and they fight.
var site = null;
var squad = [];
var jumper = null;

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
	for (var r = 6; r < 100 && !site; r += 2)
	{
		for (var a = 0; a < 32 && !site; ++a)
		{
			var x = Math.round(s.x + r * Math.cos(a * Math.PI / 16)) - 10, y = Math.round(s.y + r * Math.sin(a * Math.PI / 16)) - 8;
			if (x > 3 && y > 3 && x + 22 < mapWidth - 3 && y + 16 < mapHeight - 3 && clearGround(x, y, x + 22, y + 16)) { site = { x: x, y: y }; }
		}
	}
	if (!site) { debug("Walker test: no open ground"); return; }
	addSpotter(site.x + 10, site.y + 8, 0, 26 * 128, false, 0);
	hackNetOff();
	var kinds = [
		["Body1REC", "WalkerBiped", "MG2Mk1"], ["Body5REC", "WalkerBiped", "Cannon1Mk1"], ["Body11ABT", "WalkerQuad", "Cannon2A-TMk1"],
		["Body5REC", "WalkerQuad", "MG3Mk1"], ["Body11ABT", "WalkerHex", "Cannon375mmMk1"], ["Body9REC", "WalkerHex", "Rocket-HvyA-T"]
	];
	for (var i = 0; i < kinds.length; ++i)
	{
		var d = addDroid(0, site.x + 3 + (i % 3) * 3, site.y + 6 + Math.floor(i / 3) * 3, "Walker " + i, kinds[i][0], kinds[i][1], "", "", kinds[i][2]);
		if (d) { squad.push(d); } else { debug("Walker test: could not add " + kinds[i].join(" ")); }
	}
	// a wall line across the way for the jump
	for (var wy = site.y + 2; wy <= site.y + 14; ++wy)
	{
		addStructure("A0HardcreteMk1Wall", 1, (site.x + 14) * 128 + 64, wy * 128 + 64);
	}
	hackNetOn();
	debug("Walker test: " + squad.length + " walkers at " + site.x + "," + site.y);
	queue("look", 2500);
	queue("walk", 20000);
}

function look()
{
	cameraZoom(650, 6000);
	setTimer("follow", 400);
}

function follow()
{
	var d = squad.length ? getObject(DROID, 0, squad[1].id) : null;
	if (d) { centreView(d.x, d.y + 6); }
}

function walk()
{
	squad.forEach(function(d, i) { orderDroidLoc(d, DORDER_MOVE, site.x + 9 + (i % 3) * 1, site.y + 4 + Math.floor(i / 3) * 6); });
	queue("jump", 14000);
}

function jump()
{
	var live = squad.map(function(d) { return getObject(DROID, 0, d.id); }).filter(function(d) { return d && d.jumpReady; });
	debug("Walker test: " + live.length + " walkers ready to jump");
	live.forEach(function(d) { orderDroidLoc(d, DORDER_JUMP, d.x + 7, d.y); });
	queue("report", 2500);
	queue("enemy", 12000);
}

function report()
{
	squad.forEach(function(d) {
		var o = getObject(DROID, 0, d.id);
		if (o) { debug("Walker test: " + o.name + " at " + o.x + "," + o.y + " jumpReady " + o.jumpReady); }
	});
}

function enemy()
{
	hackNetOff();
	addDroid(1, site.x + 21, site.y + 8, "Enemy tank", "Body5REC", "tracked01", "", "", "MG2Mk1");
	hackNetOn();
	debug("Walker test: enemy arrives");
}
