// Warzone 2126 menu footage: two fleets meet on open sea in light rain; a carrier's VTOLs join in.
// Islands, 2 players, the visual bench's map seed 3992670690: open water around tile 29, 30.

function ship(p, x, y, body, weapons)
{
	var args = [p, x, y, "Ship", body, "NavalScrew", "", ""].concat(weapons);
	return addDroid.apply(null, args);
}

var west = [], east = [], vtols = [];

function eventStartLevel()
{
	hackNetOff();
	west.push(ship(0, 16, 40, "NavalDestroyer", ["Cannon2A-TMk1", "Cannon2A-TMk1", "Cannon2A-TMk1"]));
	west.push(ship(0, 19, 43, "NavalFrigate", ["MG3Mk1", "Rocket-LtA-T"]));
	west.push(ship(0, 13, 44, "NavalCruiser", ["Cannon375mmMk1", "Cannon2A-TMk1", "Cannon2A-TMk1", "MG3Mk1", "MG3Mk1"]));
	west.push(ship(0, 21, 38, "NavalFrigate", ["MG3Mk1", "MG3Mk1"]));
	var carrier = ship(0, 10, 36, "NavalCarrier", ["QuadMg1AAGun", "QuadMg1AAGun"]);
	for (var v = 0; v < 4; ++v)
	{
		vtols.push(addDroid(0, 9 + (v % 2), 35 + Math.floor(v / 2), "VTOL", "Body5REC", "V-Tol", "", "", "Cannon1-VTOL"));
	}
	east.push(ship(1, 42, 18, "NavalDestroyer", ["Cannon2A-TMk1", "Cannon2A-TMk1", "Cannon2A-TMk1"]));
	east.push(ship(1, 39, 15, "NavalBattleship", ["Cannon375mmMk1", "Cannon375mmMk1", "Cannon2A-TMk1", "Cannon2A-TMk1", "MG3Mk1", "MG3Mk1"]));
	east.push(ship(1, 45, 21, "NavalFrigate", ["MG3Mk1", "Rocket-LtA-T"]));
	east.push(ship(1, 37, 20, "NavalFrigate", ["MG3Mk1", "MG3Mk1"]));
	hackNetOn();
	west.forEach(function(d, i) { orderDroidLoc(d, DORDER_SCOUT, 32 + (i % 2) * 3, 24 + i); });
	east.forEach(function(d, i) { orderDroidLoc(d, DORDER_SCOUT, 24 + (i % 2) * 3, 34 - i); });
	orderDroidLoc(carrier, DORDER_MOVE, 14, 33);
	queue("launchVtols", 2500);
}

function launchVtols()
{
	vtols.forEach(function(v, i) {
		var t = east[i % east.length];
		if (t) { orderDroidObj(v, DORDER_ATTACK, t); }
	});
}
