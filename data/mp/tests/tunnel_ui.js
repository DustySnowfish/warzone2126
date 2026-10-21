include("tests/movebench_common.js");

// Warzone 2126 tunnel interface test: two entrances and eight tanks, no orders. Select the tanks
// and click the far entrance to send them through the tunnel network.

function eventStartLevel()
{
	hackNetOff();
	benchEnable(0, "medium");
	addStructure("A0TunnelEntrance", 0, 12 * 128 + 64, 8 * 128 + 64);
	addStructure("A0TunnelEntrance", 0, 15 * 128 + 64, 19 * 128 + 64);
	benchSpawnFixed(0, "medium", 13, 16, 4, +1, 8);
	hackNetOn();
	centreView(14, 14);
}
