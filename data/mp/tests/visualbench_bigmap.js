include("tests/movebench_common.js");

// Warzone 2126 large map test (512x512): two groups cross the whole map towards each other,
// through the gaps in the ridgelines, and meet near the central plateau.

function eventStartLevel()
{
	hackNetOff();
	benchEnable(0, "medium");
	benchEnable(1, "medium");
	const west = benchSpawnFixed(0, "medium", 40, 60, 5, +1, 20);
	const east = benchSpawnFixed(1, "medium", 460, 440, 5, -1, 20);
	hackNetOn();
	for (let i = 0; i < west.length; i++) orderDroidLoc(west[i], DORDER_SCOUT, 470, 450);
	for (let i = 0; i < east.length; i++) orderDroidLoc(east[i], DORDER_SCOUT, 40, 60);
	debug("bigmap: map " + mapWidth + "x" + mapHeight + ", " + west.length + " vs " + east.length);
	setTimer("report", 5000);
}

function report()
{
	const w = enumDroid(0);
	const e = enumDroid(1);
	const lead = w.length > 0 ? w[0] : null;
	debug("bigmap t=" + gameTime + " units " + w.length + "/" + e.length + (lead ? " west lead at " + lead.x + "," + lead.y : ""));
}
