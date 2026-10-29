// Warzone 2126 ambient life test (extra script): centres the view on the coast by player 0's base
// (birds, gulls, surf), then at 110 s starts a firefight in the middle of the view so the birds
// scatter. Logs "Ambient test" when each phase starts.

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



var spot = null;

function fight()
{
	hackNetOff();
	for (var i = 0; i < 3; ++i)
	{
		addDroid(0, spot.x - 2 + i, spot.y - 2, "Gunner", "Body5REC", "HalfTrack", "", "", "MG3Mk1");
		addDroid(1, spot.x - 2 + i, spot.y + 3, "Target", "Body5REC", "HalfTrack", "", "", "MG3Mk1");
	}
	hackNetOn();
	debug("Ambient test: fight at " + spot.x + "," + spot.y + " t=" + Math.floor(gameTime / 1000));
}

function setup()
{
	var s = startPositions[0];
	var w = findWaterNear(s.x, s.y);
	// A few tiles back inland from the water, so the view holds both land and sea
	var vx = s.x - w.x, vy = s.y - w.y, len = Math.sqrt(vx * vx + vy * vy) || 1;
	spot = { x: Math.round(w.x + vx / len * 5), y: Math.round(w.y + vy / len * 5) };
	centreView(spot.x, spot.y);
	debug("Ambient test: view at " + spot.x + "," + spot.y + " water at " + w.x + "," + w.y + " t=" + Math.floor(gameTime / 1000));
	queue("fight", 110000);
}

function eventStartLevel()
{
	setup();
}
