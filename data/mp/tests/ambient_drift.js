// Warzone 2126 drifting dust and leaves test (extra script): the view on open ground near player
// 0's base, nothing else happening, so the drifting leaves, litter or dust show.

function eventStartLevel()
{
	var s = startPositions[0];
	var cx = mapWidth / 2, cy = mapHeight / 2;
	var vx = cx - s.x, vy = cy - s.y, len = Math.sqrt(vx * vx + vy * vy) || 1;
	centreView(Math.round(s.x + vx / len * 10), Math.round(s.y + vy / len * 10));
	debug("Drift test: view set");
}
