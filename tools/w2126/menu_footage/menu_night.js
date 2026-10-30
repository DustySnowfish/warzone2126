// Warzone 2126 menu footage: VTOLs strike a base at night in a thunderstorm while its AA sites
// fire back. Rockies highlands, 2 players, advanced bases; player 1's base is at the north-east
// (around tile 109, 15) on the visual bench's map (seed 3992670690).

var vtols = [];

function eventStartLevel()
{
	hackNetOff();
	addStructure("AASite-QuadMg1", 1, 104 * 128, 22 * 128);
	addStructure("AASite-QuadMg1", 1, 114 * 128, 23 * 128);
	addStructure("AASite-QuadRotMg", 1, 101 * 128, 14 * 128);
	var kit = ["Bomb1-VTOL-LtHE", "Rocket-VTOL-LtA-T", "Bomb3-VTOL-LtINC", "Cannon1-VTOL"];
	for (var i = 0; i < 10; ++i)
	{
		vtols.push(addDroid(0, 80 + (i % 5), 40 + Math.floor(i / 5) * 2, "Strike", "Body5REC", "V-Tol", "", "", kit[i % kit.length]));
	}
	hackNetOn();
	var targets = enumStruct(1);
	vtols.forEach(function(v, i) {
		if (targets.length) { orderDroidObj(v, DORDER_ATTACK, targets[(i * 2) % targets.length]); }
	});
}
