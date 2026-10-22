include("tests/movebench_common.js");

// Warzone 2126 interface test: a mixed force of five unit types for the selected-units bar.
// Select them (drag, or Ctrl+A for all combat units) and try the cards and command buttons.

function eventStartLevel()
{
	hackNetOff();
	var kinds = ["medium", "cannon", "heavy", "lightcannonht", "cyborg"];
	var counts = [8, 6, 3, 5, 6];
	for (var k = 0; k < kinds.length; ++k)
	{
		benchEnable(0, kinds[k]);
		benchSpawnFixed(0, kinds[k], 11 + k * 2, 16, 2, +1, counts[k]);
	}
	hackNetOn();
	centreView(15, 15);
	showInterface();
}
