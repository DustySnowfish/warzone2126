// Warzone 2126 naval AI carrier test (extra script): as naval_ai.js, plus VTOL, rearming pad and
// Aircraft Carrier research, four VTOLs and a ready carrier for each AI, to see NullBot build carriers and put its
// VTOLs aboard. Reports every 20 seconds.

function findWaterNear(x0, y0)
{
	for (var r = 1; r < 80; ++r)
	{
		for (var dy = -r; dy <= r; ++dy)
		{
			for (var dx = -r; dx <= r; ++dx)
			{
				if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) { continue; }
				var x = x0 + dx, y = y0 + dy;
				if (x < 4 || y < 4 || x >= mapWidth - 4 || y >= mapHeight - 4) { continue; }
				// Open water: this tile and its neighbours
				var open = true;
				for (var k = -1; k <= 1 && open; ++k)
				{
					for (var m = -1; m <= 1; ++m)
					{
						if (terrainType(x + k, y + m) !== TER_WATER) { open = false; break; }
					}
				}
				if (open) { return { x: x, y: y }; }
			}
		}
	}
	return null;
}


function eventStartLevel()
{
	hackNetOff();
	for (var p = 0; p < maxPlayers; ++p)
	{
		completeResearch("R-Vehicle-Prop-Hover", p, true);
		completeResearch("R-Vehicle-Body05", p, true);
		completeResearch("R-Vehicle-Prop-NavalScrew", p, true);
		completeResearch("R-Vehicle-Body-NavalLanding", p, true);
		["R-Vehicle-Prop-VTOL", "R-Struc-VTOLFactory", "R-Struc-VTOLPad", "R-Wpn-Cannon1Mk1", "R-Wpn-AAGun03", "R-Vehicle-Body-NavalCarrier"].forEach(function(r) { completeResearch(r, p, true); });
		if (p !== selectedPlayer)
		{
			// a few land units stranded on each AI's island, for the landing craft to carry
			for (var k = 0; k < 4; ++k)
			{
				addDroid(p, startPositions[p].x + k - 2, startPositions[p].y + 3, "Stranded Tank", "Body5REC", "wheeled01", "", "", "MG2Mk1");
				addDroid(p, startPositions[p].x + k - 2, startPositions[p].y - 3, "Test VTOL", "Body5REC", "V-Tol", "", "", "Cannon1-VTOL");
			}
			// a ready carrier in the home waters, so the assignment logic runs at once
			var w = findWaterNear(startPositions[p].x, startPositions[p].y);
			if (w) { addDroid(p, w.x, w.y, "Carrier", "NavalCarrier", "NavalScrew", "", "", "QuadMg1AAGun", "QuadMg1AAGun"); }
		}
		setPower(4000, p);
	}
	hackNetOn();
	setTimer("navalReport", 20000);
}

function navalReport()
{
	var msg = "NAVAL t=" + Math.floor(gameTime / 1000);
	for (var p = 0; p < maxPlayers; ++p)
	{
		var yards = enumStruct(p, "A0Shipyard");
		var ships = enumDroid(p).filter(function(d) { return (d.propulsion === "NavalScrew" || d.propulsion === "NavalJet") && d.droidType !== DROID_SUPERTRANSPORTER; });
		var crafts = enumDroid(p).filter(function(d) { return d.droidType === DROID_SUPERTRANSPORTER; });
		var landed = enumDroid(p).filter(function(d) { return d.name === "Stranded Tank" && distBetweenTwoPoints(d.x, d.y, startPositions[p].x, startPositions[p].y) > 40; }).length;
		var res = getResearch("R-Vehicle-Prop-NavalScrew", p);
		msg += " | p" + p + " screw=" + (res && res.done ? 1 : 0) + " yards=" + yards.length + "(" + yards.filter(function(s) { return s.status === BUILT; }).length + ") ships=" + ships.length + " crafts=" + crafts.map(function(c) { return c.x + "," + c.y + " o" + c.order + " cargo" + (c.cargoCount - 1); }).join("/") + " tanksAbroad=" + landed;
		if (crafts.length > 0)
		{
			msg += " tanks{" + enumDroid(p).filter(function(d) { return d.droidType === DROID_WEAPON && d.propulsion !== "NavalScrew" && d.propulsion !== "NavalJet"; }).map(function(d) { return d.x + "," + d.y + " o" + d.order + " a" + d.action; }).join(" ") + "}";
		}
		var carriers = ships.filter(function(d) { return d.body === "NavalCarrier"; });
		var vtols = enumDroid(p).filter(function(d) { return isVTOL(d); });
		msg += " carriers=" + carriers.map(function(c) { return c.x + "," + c.y; }).join("/") + " vtols=" + vtols.length + " onCarrier=" + vtols.filter(function(v) { return v.order === DORDER_CARRIER; }).length;
		if (ships.length > 0)
		{
			msg += " [" + ships.map(function(d) { return d.body.replace("Naval", "") + "@" + d.x + "," + d.y + " o" + d.order + " h" + Math.round(d.health); }).join(" ") + "]";
		}
	}
	debug(msg);
}

function eventDestroyed(obj)
{
	if (obj.type === DROID && (obj.propulsion === "NavalScrew" || obj.propulsion === "NavalJet"))
	{
		debug("NAVAL ship lost: player " + obj.player + " at " + obj.x + "," + obj.y);
	}
}
