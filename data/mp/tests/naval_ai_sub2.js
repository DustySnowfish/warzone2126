// Warzone 2126 naval AI submarine classes test (extra script): as naval_ai_sub.js, plus the coastal
// and missile submarines, cruise missiles, VTOLs and anti-submarine VTOL bombs for everyone and two
// VTOLs for each AI, so NullBot takes turns between the submarine hulls and builds anti-submarine
// VTOLs. Reports every 20 seconds ("subs=" by hull, "asw=").

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
		["R-Struc-OffshoreRig", "R-Struc-SonarBuoy", "R-Defense-Torpedo", "R-Wpn-Torpedo-Twin", "R-Vehicle-Body-NavalFrigate", "R-Defense-CoastalGun", "R-Vehicle-Body-NavalSub", "R-Vehicle-Body-NavalSubCoastal", "R-Vehicle-Body-NavalSubMissile", "R-Wpn-Missile-Cruise", "R-Vehicle-Prop-VTOL", "R-Struc-VTOLFactory", "R-Struc-VTOLPad", "R-Wpn-Bomb01", "R-Wpn-DepthCharge", "R-Wpn-DepthCharge-VTOL"].forEach(function(r) { completeResearch(r, p, true); });
		if (p !== selectedPlayer)
		{
			// a few land units stranded on each AI's island, for the landing craft to carry
			for (var k = 0; k < 4; ++k)
			{
				addDroid(p, startPositions[p].x + k - 2, startPositions[p].y + 3, "Stranded Tank", "Body5REC", "wheeled01", "", "", "MG2Mk1");
			}
			for (var v = 0; v < 2; ++v)
			{
				addDroid(p, startPositions[p].x + v - 1, startPositions[p].y - 3, "Test VTOL", "Body5REC", "V-Tol", "", "", "Bomb1-VTOL-LtHE");
			}
		}
		if (p !== selectedPlayer)
		{
			var w = findWaterNear(startPositions[p].x, startPositions[p].y);
			if (w)
			{
				var bait = addDroid(0, w.x, w.y, "Bait Frigate", "NavalFrigate", "NavalScrew", "", "", "MG1Mk1");
				if (bait) { orderDroid(bait, DORDER_HOLD); }
				var sub = addDroid(0, w.x, w.y, "Bait Sub", "NavalSubAttack", "NavalScrew", "", "", "Torpedo-Twin", "Torpedo-Twin");
				if (sub) { orderDroid(sub, DORDER_HOLD); }
			}
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
		var seaTrucks = enumDroid(p, DROID_CONSTRUCT).filter(function(d) { return d.propulsion === "NavalScrew" || d.propulsion === "NavalJet"; }).length;
		msg += " | p" + p + " seaTrucks=" + seaTrucks + " rigs=" + enumStruct(p, "A0OffshoreRig").length + " buoys=" + enumStruct(p, "A0SonarBuoy").length + " torpedoes=" + enumStruct(p, "Emplacement-Torpedo").length + " batteries=" + enumStruct(p, "Emplacement-CoastalGun").length;
		msg += " | p" + p + " screw=" + (res && res.done ? 1 : 0) + " yards=" + yards.length + "(" + yards.filter(function(s) { return s.status === BUILT; }).length + ") ships=" + ships.length + " tubes=" + ships.filter(function(d) { return d.weapons.some(function(x) { return x.name.indexOf("Torpedo") === 0; }); }).length + " subs=" + ["NavalSubAttack", "NavalSubCoastal", "NavalSubMissile"].map(function(b) { return ships.filter(function(d) { return d.body === b; }).length; }).join("/") + " cruise=" + ships.filter(function(d) { return d.weapons.some(function(x) { return x.name === "Missile-Cruise"; }); }).length + " asw=" + enumDroid(p).filter(function(d) { return d.weapons.some(function(x) { return x.name === "Bomb-VTOL-DepthCharge"; }); }).length + " sonar=" + ships.filter(function(d) { return d.droidType === DROID_SENSOR; }).length + " charges=" + ships.filter(function(d) { return d.weapons.some(function(x) { return x.name === "DepthCharge-Rack"; }); }).length + " res(sonar,dc)=" + [getResearch("R-Sys-Sonar", p), getResearch("R-Wpn-DepthCharge", p)].map(function(r) { return r && r.done ? 1 : 0; }).join("") + " crafts=" + crafts.map(function(c) { return c.x + "," + c.y + " o" + c.order + " cargo" + (c.cargoCount - 1); }).join("/") + " tanksAbroad=" + landed;
		if (crafts.length > 0)
		{
			msg += " tanks{" + enumDroid(p).filter(function(d) { return d.droidType === DROID_WEAPON && d.propulsion !== "NavalScrew" && d.propulsion !== "NavalJet"; }).map(function(d) { return d.x + "," + d.y + " o" + d.order + " a" + d.action; }).join(" ") + "}";
		}
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
		debug("NAVAL ship lost: " + obj.body + " player " + obj.player + " at " + obj.x + "," + obj.y);
	}
}
