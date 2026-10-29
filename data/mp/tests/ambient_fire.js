// Warzone 2126 grass fire test (extra script): flamer tanks and mortars fight on open ground near
// player 0's base on an Arizona map, so shots set the grass alight; the view follows the fight.
// Logs "Fire test" when the fight starts.

function eventStartLevel()
{
	var s = startPositions[0];
	// Open ground towards the map centre, 12 tiles out from the base
	var cx = mapWidth / 2, cy = mapHeight / 2;
	var vx = cx - s.x, vy = cy - s.y, len = Math.sqrt(vx * vx + vy * vy) || 1;
	var x = Math.round(s.x + vx / len * 12), y = Math.round(s.y + vy / len * 12);
	hackNetOff();
	completeResearch("R-Wpn-Flamer01Mk1", 0, true);
	for (var i = 0; i < 3; ++i)
	{
		addDroid(0, x - 2 + i * 2, y - 3, "Flamer", "Body5REC", "HalfTrack", "", "", "Flame1Mk1");
		addDroid(1, x - 2 + i * 2, y + 3, "Target", "Body11ABT", "tracked01", "", "", "MG1Mk1");
	}
	addDroid(0, x, y - 7, "Mortar", "Body5REC", "HalfTrack", "", "", "Mortar1Mk1");
	hackNetOn();
	centreView(x, y);
	debug("Fire test: fight at " + x + "," + y);
}
