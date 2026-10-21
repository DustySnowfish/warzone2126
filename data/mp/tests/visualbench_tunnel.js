include("tests/movebench_common.js");

// Warzone 2126 tunnel network prototype: two entrances far apart, eight tanks ordered to the far
// one. They drive to the near entrance, go underground (blips travel along the tunnel line),
// and come up at the far entrance.

const ENTRY = { x: 12, y: 8 };
const EXIT = { x: 52, y: 26 };

function eventStartLevel()
{
	hackNetOff();
	benchEnable(0, "medium");
	const entry = addStructure("A0TunnelEntrance", 0, ENTRY.x * 128 + 64, ENTRY.y * 128 + 64);
	const exit = addStructure("A0TunnelEntrance", 0, EXIT.x * 128 + 64, EXIT.y * 128 + 64);
	const tanks = benchSpawnFixed(0, "medium", ENTRY.x - 2, ENTRY.y + 4, 4, +1, 8);
	hackNetOn();
	for (let i = 0; i < tanks.length; i++)
	{
		orderDroidObj(tanks[i], DORDER_TUNNEL, exit);
	}
	debug("tunnel test: " + tanks.length + " tanks, entry " + (entry ? entry.id : "none") + ", exit " + (exit ? exit.id : "none"));
	setTimer("report", 2000);
}

function report()
{
	const onMap = enumDroid(0).length;
	debug("tunnel test t=" + gameTime + " on map " + onMap);
}
