function droidLimit(player)	// inside hackNetOff()
{
	// Warzone 2126: the host picks 150, 300 or 500 units; trucks and commanders scale with it
	const units = (typeof unitLimit !== "undefined") ? unitLimit : 150;
	setDroidLimit(player, units, DROID_ANY);
	setDroidLimit(player, units >= 500 ? 20 : units >= 300 ? 15 : 10, DROID_COMMAND);
	setDroidLimit(player, units >= 500 ? 25 : units >= 300 ? 20 : 15, DROID_CONSTRUCT);
}
