// Warzone 2126 tileset survey (extra script): clear weather, then the camera visits player 0's
// base, a cliff, a shoreline, open ground and the map centre, 15 s each, for screenshots.

var views = [];
var viewIndex = 0;

function findTile(test)
{
	var best = null, bestScore = -1;
	for (var y = 4; y < mapHeight - 4; y += 2)
	{
		for (var x = 4; x < mapWidth - 4; x += 2)
		{
			var score = test(x, y);
			if (score > bestScore)
			{
				bestScore = score;
				best = { x: x, y: y };
			}
		}
	}
	return best;
}

function countAround(x, y, r, type)
{
	var n = 0;
	for (var dy = -r; dy <= r; ++dy)
	{
		for (var dx = -r; dx <= r; ++dx)
		{
			if (terrainType(x + dx, y + dy) === type)
			{
				++n;
			}
		}
	}
	return n;
}

function nextView()
{
	if (viewIndex >= views.length)
	{
		return;
	}
	var v = views[viewIndex++];
	centreView(v.x, v.y);
	debug("Tileset view " + viewIndex + ": " + v.name + " at " + v.x + "," + v.y);
}

function eventStartLevel()
{
	setWeather(WEATHER_CLEAR);
	var s = startPositions[0];
	// One spotter over the whole map (tile coordinates, range in world units)
	addSpotter(Math.floor(mapWidth / 2), Math.floor(mapHeight / 2), 0, Math.max(mapWidth, mapHeight) * 128, false, 0);
	var cx = Math.floor(mapWidth / 2), cy = Math.floor(mapHeight / 2);
	views.push({ name: "base", x: s.x, y: s.y });
	var cliff = findTile(function(x, y) { return countAround(x, y, 3, TER_CLIFFFACE); });
	if (cliff && countAround(cliff.x, cliff.y, 3, TER_CLIFFFACE) > 0) { cliff.name = "cliff"; views.push(cliff); }
	var shore = findTile(function(x, y) {
		var w = countAround(x, y, 3, TER_WATER);
		return (w > 10 && w < 35) ? 100 - Math.abs(w - 24) : 0;
	});
	if (shore && countAround(shore.x, shore.y, 3, TER_WATER) > 10) { shore.name = "shore"; views.push(shore); }
	var vx = cx - s.x, vy = cy - s.y, len = Math.sqrt(vx * vx + vy * vy) || 1;
	views.push({ name: "open", x: Math.round(s.x + vx / len * 14), y: Math.round(s.y + vy / len * 14) });
	views.push({ name: "centre", x: cx, y: cy });
	nextView();
	setTimer("nextView", 15000);
}
