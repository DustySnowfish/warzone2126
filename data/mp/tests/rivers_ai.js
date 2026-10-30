// Warzone 2126 River valleys test (extra script): a 2v2 on River valleys with the teams on facing
// sides. Every 30 seconds it reports, per player, how many units are on the other team's side of
// the river (so have crossed at a bridge or ford) and how many have been lost. Run headless:
//   warzone2100 --skirmish=rivers_ai.json --headless --autogame   (player 0 stays idle there)

var lost = [0, 0, 0, 0];

function eventStartLevel()
{
	setTimer("riverReport", 30000);
}

function teamCentre(team)
{
	var x = 0, y = 0, n = 0;
	for (var p = 0; p < maxPlayers; ++p)
	{
		if ((p < maxPlayers / 2 ? 0 : 1) === team)
		{
			x += startPositions[p].x; y += startPositions[p].y; ++n;
		}
	}
	return { x: x / n, y: y / n };
}

function riverReport()
{
	var a = teamCentre(0), b = teamCentre(1);
	var msg = "RIVER t=" + Math.floor(gameTime / 1000);
	for (var p = 0; p < maxPlayers; ++p)
	{
		var own = p < maxPlayers / 2 ? a : b, other = p < maxPlayers / 2 ? b : a;
		var units = enumDroid(p);
		var across = units.filter(function(d) { return distBetweenTwoPoints(d.x, d.y, other.x, other.y) < distBetweenTwoPoints(d.x, d.y, own.x, own.y); }).length;
		msg += " | p" + p + " units=" + units.length + " across=" + across + " lost=" + lost[p] + " structs=" + enumStruct(p).length;
	}
	debug(msg);
}

function eventDestroyed(obj)
{
	if (obj.type === DROID && obj.player >= 0 && obj.player < 4)
	{
		lost[obj.player]++;
	}
}
