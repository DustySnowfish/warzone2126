// Warzone 2126 menu footage: a column of tanks forcing a river bridge at dusk, under fire from
// towers and tanks on the far bank. River valleys, 2 players; the visual bench's synchronised seed
// gives map seed 3992670690. The bridge is at tile 81, 86, the river running north-west to
// south-east; the column comes from the south-west.

function tank(p, x, y, body, prop, weapon)
{
	return addDroid(p, x, y, "Tank", body, prop, "", "", weapon);
}

function eventStartLevel()
{
	hackNetOff();
	var attackers = [];
	// The column on the south-east bank, heading over the bridge to the north-west
	var kit = [["Body11ABT", "tracked01", "Cannon375mmMk1"], ["Body5REC", "tracked01", "Cannon2A-TMk1"],
		["Body5REC", "HalfTrack", "Rocket-LtA-T"], ["Body11ABT", "tracked01", "Cannon375mmMk1"],
		["Body5REC", "tracked01", "MG3Mk1"], ["Body5REC", "HalfTrack", "Flame1Mk1"]];
	for (var i = 0; i < 12; ++i)
	{
		var k = kit[i % kit.length];
		attackers.push(tank(0, 70 + (i % 3) * 2 + Math.floor(i / 3), 97 - Math.floor(i / 3) * 2 + (i % 3), k[0], k[1], k[2]));
	}
	// Defenders on the north-west bank
	addStructure("WallTower01", 1, 86 * 128, 80 * 128);
	addStructure("WallTower01", 1, 89 * 128, 84 * 128);
	addStructure("PillBox1", 1, 84 * 128, 78 * 128);
	addStructure("Emplacement-MortarPit01", 1, 92 * 128, 78 * 128);
	var defenders = [];
	for (var j = 0; j < 6; ++j)
	{
		defenders.push(tank(1, 89 + Math.floor(j / 3) * 2, 75 + (j % 3) * 2, "Body5REC", "tracked01", j % 2 ? "Cannon2A-TMk1" : "Rocket-LtA-T"));
	}
	hackNetOn();
	for (var a = 0; a < attackers.length; ++a)
	{
		orderDroidLoc(attackers[a], DORDER_SCOUT, 88 + (a % 3), 78 - (a % 4));
	}
	for (var d = 0; d < defenders.length; ++d)
	{
		orderDroidLoc(defenders[d], DORDER_SCOUT, 80, 87);
	}
}
