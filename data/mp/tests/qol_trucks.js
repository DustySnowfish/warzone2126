// Warzone 2126 quality-of-life test (extra script): one of player 0's trucks starts a research
// facility near the base while the others stand idle; with "Trucks Help Nearby Building" on they
// should join in after a few seconds. Logs the trucks' orders every 4 seconds ("QoL test").

var site = null;

function report()
{
	var trucks = enumDroid(0, DROID_CONSTRUCT);
	debug("QoL test t=" + Math.floor(gameTime / 1000) + "s trucks " + trucks.map(function(d) { return d.id + ":o" + d.order + ":a" + d.action; }).join(" ") +
		" sites " + enumStruct(0).filter(function(s) { return s.status === BEING_BUILT; }).map(function(s) { return s.name + "@" + s.x + "," + s.y; }).join(" "));
}

function start()
{
	var trucks = enumDroid(0, DROID_CONSTRUCT);
	if (trucks.length === 0) { debug("QoL test: no trucks"); return; }
	var s = startPositions[0];
	site = pickStructLocation(trucks[0], "A0ResearchFacility", s.x + 4, s.y + 4);
	var ok = site ? orderDroidBuild(trucks[0], DORDER_BUILD, "A0ResearchFacility", site.x, site.y) : false;
	debug("QoL test: " + trucks.length + " trucks, truck " + trucks[0].id + " builds at " + (site ? site.x + "," + site.y : "none") + " ok=" + ok);
	centreView(s.x + 2, s.y + 2);
	setTimer("report", 4000);
}

function eventStartLevel()
{
	queue("start", 3000);
}
