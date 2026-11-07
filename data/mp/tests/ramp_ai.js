// Warzone 2126 NullBot ramp test (extra script): NullBots on a cliff-heavy map; the whole map is
// revealed, and the camera follows any truck doing an earthwork (ramps and ridges) for screenshots.
var watched = null;

function eventStartLevel()
{
	addSpotter(Math.floor(mapWidth / 2), Math.floor(mapHeight / 2), 0, Math.max(mapWidth, mapHeight) * 128, false, 0);
	setTimer("watch", 5000);
}

function watch()
{
	for (var p = 1; p < maxPlayers; ++p)
	{
		var trucks = enumDroid(p, DROID_CONSTRUCT).filter(function(d) { return d.order === DORDER_EARTHWORK; });
		if (trucks.length > 0)
		{
			var d = trucks[0];
			if (watched !== d.id) { debug("Ramp test: player " + p + " truck earthworking near " + d.x + "," + d.y); }
			watched = d.id;
			centreView(d.x, d.y);
			return;
		}
	}
}
