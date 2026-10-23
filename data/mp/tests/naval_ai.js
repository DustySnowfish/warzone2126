// Warzone 2126 naval AI test (extra script): Islands with three NullBots. Ground research up to
// hover is given to everyone at the start so the AIs reach Shipbuilding quickly; the script then
// reports Shipyards and ships every 20 seconds.

function eventStartLevel()
{
	hackNetOff();
	for (var p = 0; p < maxPlayers; ++p)
	{
		completeResearch("R-Vehicle-Prop-Hover", p, true);
		completeResearch("R-Vehicle-Body05", p, true);
		completeResearch("R-Vehicle-Prop-NavalScrew", p, true);
		completeResearch("R-Vehicle-Body-NavalLanding", p, true);
		if (p !== selectedPlayer)
		{
			// a few land units stranded on each AI's island, for the landing craft to carry
			for (var k = 0; k < 4; ++k)
			{
				addDroid(p, startPositions[p].x + k - 2, startPositions[p].y + 3, "Stranded Tank", "Body5REC", "wheeled01", "", "", "MG2Mk1");
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
		msg += " | p" + p + " screw=" + (res && res.done ? 1 : 0) + " yards=" + yards.length + "(" + yards.filter(function(s) { return s.status === BUILT; }).length + ") ships=" + ships.length + " crafts=" + crafts.map(function(c) { return c.x + "," + c.y + " o" + c.order + " cargo" + (c.cargoCount - 1); }).join("/") + " tanksAbroad=" + landed;
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
		debug("NAVAL ship lost: player " + obj.player + " at " + obj.x + "," + obj.y);
	}
}
