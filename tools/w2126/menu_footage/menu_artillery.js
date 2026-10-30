// Warzone 2126 menu footage: a base shelled by mortars and howitzers in a dust storm, with tanks
// rolling out to meet them. Arizona, 2 players, advanced bases; player 0's base is at the south-west
// (around tile 14, 110) on the visual bench's map (seed 3992670690).

function eventStartLevel()
{
	hackNetOff();
	var guns = [];
	for (var i = 0; i < 10; ++i)
	{
		var w = i % 3 === 0 ? "Howitzer105Mk1" : "Mortar1Mk1";
		guns.push(addDroid(1, 25 + (i % 4) * 2, 97 + Math.floor(i / 4) * 2, "Battery", "Body11ABT", "tracked01", "", "", w));
	}
	var tanks = [];
	for (var j = 0; j < 8; ++j)
	{
		tanks.push(addDroid(0, 18 + (j % 4), 104 - Math.floor(j / 4), "Tank", "Body5REC", "tracked01", "", "", j % 2 ? "Cannon2A-TMk1" : "Rocket-LtA-T"));
	}
	hackNetOn();
	// The batteries shell the base's buildings, nearest first
	var targets = enumStruct(0);
	guns.forEach(function(g, i) {
		if (targets.length) { orderDroidObj(g, DORDER_ATTACK, targets[(i * 3) % targets.length]); }
	});
	tanks.forEach(function(t, i) { orderDroidLoc(t, DORDER_SCOUT, 26 + (i % 4), 98 + Math.floor(i / 4)); });
}
