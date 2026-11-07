// Warzone 2126 infantry test (extra script): cyborg fireteams of player 0 on open ground near its
// base, close camera. They walk, then meet an enemy tank and two enemy cyborgs and fight.
var site = null;
var squad = [];

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
			if (x > 3 && y > 3 && x + 20 < mapWidth - 3 && y + 16 < mapHeight - 3 && clearGround(x, y, x + 20, y + 16)) { site = { x: x, y: y }; }
		}
	}
	if (!site) { debug("Infantry test: no open ground"); return; }
	addSpotter(site.x + 10, site.y + 8, 0, 24 * 128, false, 0);
	hackNetOff();
	var cx = site.x + 4, cy = site.y + 8;
	var kinds = [
		["CyborgLightBody", "CyborgChaingun"], ["CyborgLightBody", "CyborgCannon"], ["CyborgLightBody", "CyborgFlamer01"],
		["CyborgHeavyBody", "Cyb-Hvywpn-Mcannon"], ["CyborgLightBody", "CyborgSpade"], ["CyborgHeavyBody", "Cyb-Hvywpn-Mcannon"]
	];
	for (var i = 0; i < kinds.length; ++i)
	{
		var d = addDroid(0, cx + (i % 3) * 2, cy + Math.floor(i / 3) * 2, "Cyb " + i, kinds[i][0], "CyborgLegs", "", "", kinds[i][1]);
		if (d) { squad.push(d); } else { debug("Infantry test: could not add " + kinds[i][1]); }
	}
	hackNetOn();
	queue("look", 2500);
	debug("Infantry test: " + squad.length + " cyborg units at " + cx + "," + cy);
	queue("walk", 24000);
}

function look()
{
	centreView(site.x + 6, site.y + 15);
	cameraZoom(500, 6000);
}

function walk()
{
	squad.forEach(function(d, i) { orderDroidLoc(d, DORDER_MOVE, site.x + 12 + (i % 3) * 2, site.y + 8 + Math.floor(i / 3) * 2); });
	setTimer("follow", 1000);
	queue("enemy", 22000);
}

function follow()
{
	var live = squad.map(function(d) { return getObject(DROID, 0, d.id); }).filter(function(d) { return d; });
	if (live.length === 0) { removeTimer("follow"); return; }
	var x = 0, y = 0;
	live.forEach(function(d) { x += d.x; y += d.y; });
	centreView(Math.round(x / live.length), Math.round(y / live.length) + 7);
}

function enemy()
{
	hackNetOff();
	addDroid(1, site.x + 19, site.y + 8, "Enemy tank", "Body5REC", "tracked01", "", "", "MG2Mk1");
	addDroid(1, site.x + 19, site.y + 10, "Enemy cyborg", "CyborgLightBody", "CyborgLegs", "", "", "CyborgChaingun");
	addDroid(1, site.x + 19, site.y + 6, "Enemy cyborg", "CyborgLightBody", "CyborgLegs", "", "", "CyborgChaingun");
	hackNetOn();
	debug("Infantry test: enemies arrive");
}
