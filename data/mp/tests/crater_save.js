// Warzone 2126 crater savegame test (extra script): two trucks lower a 4 x 4 patch near player 0's
// base by the full earthwork depth, then the game is autosaved. Loading that save should keep the
// patch's original heights (terraindeform2126.json in the save folder), so it keeps its churned look
// and cannot be dug deeper than the limit. Logs "Crater save test".

var area = null;

function eventStartLevel()
{
	queue("dig", 30000);
}

function dig()
{
	var s = startPositions[0], tried = 0;
	for (var r = 6; r < 40 && !area; ++r)
	{
		for (var a = 0; a < 32 && !area; ++a)
		{
			var x = Math.round(s.x + r * Math.cos(a * Math.PI / 16)), y = Math.round(s.y + r * Math.sin(a * Math.PI / 16));
			var ok = true;
			for (var m = 0; m <= 3 && ok; ++m) { for (var q = 0; q <= 3 && ok; ++q) { var t = terrainType(x + m, y + q); if (t === TER_WATER || t === TER_CLIFFFACE) { ok = false; } } }
			if (ok) { ++tried; }
			if (ok && enumRange(x + 2, y + 2, 4, ALL_PLAYERS, false).filter(function(o) { return o.type === STRUCTURE; }).length === 0) { area = { x: x, y: y }; }
		}
	}
	if (!area) { debug("Crater save test: no ground (start " + s.x + "," + s.y + ", " + tried + " dry spots, all near structures)"); return; }
	hackNetOff();
	var trucks = [];
	for (var k = 0; k < 2; ++k)
	{
		trucks.push(addDroid(0, area.x - 1, area.y + k, "Digger", "Body5REC", "wheeled01", "", "", "Spade1Mk1"));
	}
	hackNetOn();
	trucks.forEach(function(t) { if (t) { orderDroidEarthwork(t, area.x, area.y, area.x + 3, area.y + 3, 1); } });
	debug("Crater save test: digging at " + area.x + "," + area.y);
	centreView(area.x + 2, area.y + 2);
	queue("save", 80000);
}

function save()
{
	debug("Crater save test: autosave " + autoSave());
}
