/*
 * Warzone 2126: naval play for NullBot.
 *
 * On maps where the base has a coast and an enemy can be reached over water, the AI researches
 * ship propulsion, builds a Shipyard, keeps a small fleet and uses it: the fleet gathers at home,
 * defends the home waters, then sails to the enemy shore nearest a target and fights from there.
 * Damaged ships go back to a Shipyard (or repair facility) for repairs.
 *
 * Ships are kept out of NullBot's land attack groups, which cannot use them.
 */

(function(_global) {
////////////////////////////////////////////////////////////////////////////////////////////

const NAVAL_SHIPYARD = "A0Shipyard";
const NAVAL_CHECK_PROP = "NavalScrew";
const NAVAL_GATHER = 3; // ships waiting at home before the fleet sails
const NAVAL_RETREAT = 40; // health percentage at which a ship goes for repairs
const NAVAL_LANDING_BODY = "NavalLanding";
const NAVAL_LANDING_RESEARCH = "R-Vehicle-Body-NavalLanding";
const NAVAL_LANDING_CRAFT = 2; // landing craft kept
const NAVAL_DEBUG = true; // log decisions (for tuning against real games)

let navalTarget; // { id, type, player, wx, wy }: the object attacked and the water spot to attack it from

_global.isNavalPropulsion = function(prop) {
	return propulsionStats.some((val) => (val.usage === PROPULSIONUSAGE.NAVAL && val.stat === prop));
};

_global.isNavalDroid = function(droid) {
	return defined(droid) && defined(droid.propulsion) && isNavalPropulsion(droid.propulsion);
};

function navalDebug(msg)
{
	if (NAVAL_DEBUG)
	{
		debug("NullBot " + me + " naval: " + msg);
	}
}

function isWater(x, y)
{
	return x > 1 && y > 1 && x < mapWidth - 2 && y < mapHeight - 2 && terrainType(x, y) === TER_WATER;
}

function isOpenWater(x, y)
{
	return isWater(x, y) && isWater(x + 1, y) && isWater(x - 1, y) && isWater(x, y + 1) && isWater(x, y - 1);
}

// Open water (the tile and its neighbours) within the radius, nearest first, sampled every
// `step` tiles
function waterSpots(x0, y0, radius, step)
{
	x0 = Math.round(x0);
	y0 = Math.round(y0);
	const ret = [];
	for (let dy = -radius; dy <= radius; dy += step)
	{
		for (let dx = -radius; dx <= radius; dx += step)
		{
			if (dx * dx + dy * dy <= radius * radius && isOpenWater(x0 + dx, y0 + dy))
			{
				ret.push({ x: x0 + dx, y: y0 + dy, d: dx * dx + dy * dy });
			}
		}
	}
	return ret.sort((a, b) => (a.d - b.d));
}

// Home waters: the nearest open water to the base from which an enemy base can be reached
// (an inland lake is no use), and whether there is any
function homeWaterInfo()
{
	function uncached()
	{
		const mine = waterSpots(baseLocation.x, baseLocation.y, baseScale * 2, 2);

		if (mine.length === 0)
		{
			navalDebug("ships not wanted: no open water near the base");
			return { wanted: false };
		}

		const theirs = [];

		for (let i = 0; i < maxPlayers; ++i)
		{
			if (i !== me && isEnemy(i) && defined(startPositions[i]))
			{
				waterSpots(startPositions[i].x, startPositions[i].y, baseScale * 2, 3).slice(0, 30).forEach((w) => {
					w.player = i;
					theirs.push(w);
				});
			}
		}

		// Every home spot on the same sea as the first one that reaches an enemy (Shipyard sites are
		// tried from each, nearest first)
		for (let j = 0; j < mine.length && j < 80; ++j)
		{
			const w = theirs.filter((t) => (propulsionCanReach(NAVAL_CHECK_PROP, mine[j].x, mine[j].y, t.x, t.y)))[0];

			if (defined(w))
			{
				navalDebug("ships wanted: home waters " + mine[j].x + "," + mine[j].y + ", player " + w.player + " reachable at " + w.x + "," + w.y);
				const sea = mine.filter((m) => (propulsionCanReach(NAVAL_CHECK_PROP, mine[j].x, mine[j].y, m.x, m.y)));
				return { wanted: true, home: { x: mine[j].x, y: mine[j].y }, spots: sea.slice(0, 8) };
			}
		}

		navalDebug("ships not wanted: no enemy reachable by water");
		return { wanted: false, home: { x: mine[0].x, y: mine[0].y } };
	}

	return cached(uncached, 180000, "navalHomeWater");
}

function homeWater()
{
	return homeWaterInfo().home;
}

// Whether ships would be any use here: home waters, and an enemy base reachable over water
_global.navalWanted = function() {
	return homeWaterInfo().wanted;
};

function iHaveShips()
{
	return propulsionStats.some((val) => (val.usage === PROPULSIONUSAGE.NAVAL && componentAvailable(val.stat)));
}

// Research: called for each idle lab before the usual choices
_global.navalResearch = function(lab) {
	if (iHaveShips() && navalWanted() && landingWanted() && !componentAvailable(NAVAL_LANDING_BODY) && withChance(30))
	{
		if (pursueResearch(lab, NAVAL_LANDING_RESEARCH))
		{
			navalDebug("researching Landing Craft");
			return true;
		}
	}

	// Bigger hulls once ships are in use (the research tree leads from Frigate to Super Battleship)
	if (iHaveShips() && navalWanted() && withChance(25))
	{
		const hulls = bodyStats.filter((b) => (b.usage === BODYUSAGE.NAVAL)).map((b) => (b.res));

		if (pursueResearch(lab, hulls.reverse()))
		{
			navalDebug("researching bigger hulls");
			return true;
		}
	}

	if (iHaveShips() || gameTime < 4 * 60000 || !navalWanted() || !withChance(35))
	{
		return false;
	}

	const ok = pursueResearch(lab, propulsionStatsToResList(PROPULSIONUSAGE.NAVAL));

	if (ok)
	{
		navalDebug("researching Shipbuilding");
	}

	return ok;
};

function isLandingCraft(droid)
{
	return isNavalDroid(droid) && droid.droidType === DROID_SUPERTRANSPORTER;
}

// Warships (not landing craft)
function myShips()
{
	return enumDroid(me).filter((d) => (isNavalDroid(d) && !isLandingCraft(d)));
}

function shipCap()
{
	const all = enumDroid(me, DROID_WEAPON).length;

	return Math.min(14, 3 + Math.floor(all / 4));
}

// Construction: a Shipyard on the home coast (a second one when rich)
_global.buildShipyard = function() {
	if (!iHaveShips() || !navalWanted() || throttled(15000, "navalShipyard"))
	{
		return false;
	}

	if (!isAvailable(NAVAL_SHIPYARD))
	{
		navalDebug("Shipyard wanted, not available");
		return false;
	}

	const have = countStructList([NAVAL_SHIPYARD]);

	if (have >= 2 || (have >= 1 && (myPower() < personality.maxPower || countFinishedStructList([NAVAL_SHIPYARD]) < 1)))
	{
		return false;
	}

	const trucks = enumTrucks().filter((t) => (
		t.order !== DORDER_BUILD && t.order !== DORDER_HELPBUILD && t.order !== DORDER_LINEBUILD && t.order !== DORDER_DEMOLISH &&
		(typeof DORDER_EARTHWORK === "undefined" || t.order !== DORDER_EARTHWORK) && distanceToBase(t) < baseScale * 2.5));

	if (trucks.length === 0)
	{
		return false;
	}

	const spots = homeWaterInfo().spots;
	let loc;

	for (let i = 0; i < spots.length && !defined(loc); ++i)
	{
		const l = pickStructLocation(trucks[0], NAVAL_SHIPYARD, spots[i].x, spots[i].y);

		if (defined(l) && distanceToBase(l) <= baseScale * 2.5)
		{
			loc = l;
		}
	}

	if (!defined(loc))
	{
		navalDebug("Shipyard wanted, no site near the home waters");
		return false;
	}

	let success = false;

	trucks.slice(0, 2).forEach((t) => {
		success = orderDroidBuild(t, DORDER_BUILD, NAVAL_SHIPYARD, loc.x, loc.y) || success;
	});
	navalDebug("Shipyard ordered at " + loc.x + "," + loc.y + ": " + success);

	return success;
};

// Production: warships from idle Shipyards
_global.checkShipProduction = function() {
	if (!iCanDesign() || !iHaveShips())
	{
		return false;
	}

	const yards = enumIdleStructList([NAVAL_SHIPYARD]);

	if (yards.length === 0 || myShips().length >= shipCap())
	{
		return false;
	}

	if (landingWanted() && componentAvailable(NAVAL_LANDING_BODY) && enumDroid(me).filter(isLandingCraft).length < NAVAL_LANDING_CRAFT)
	{
		// (a landing craft has no weapon slots; buildDroid needs a turret name all the same)
		if (buildDroid(yards[0], "Landing Craft", NAVAL_LANDING_BODY, getPropulsionStatsComponents(PROPULSIONUSAGE.NAVAL), "", "", "MG1Mk1"))
		{
			navalDebug("building a landing craft");
			return true;
		}
	}

	let bodies = filterBodyStatsByUsage(BODYUSAGE.NAVAL, chooseBodyClass());

	if (bodies.length === 0)
	{
		bodies = filterBodyStatsByUsage(BODYUSAGE.NAVAL);
	}

	const propulsions = getPropulsionStatsComponents(PROPULSIONUSAGE.NAVAL);
	let success = false;

	yards.forEach((yard) => {
		const bwPair = chooseShipBodyWeaponPair(bodies, chooseShipWeapon());

		if (defined(bwPair))
		{
			// (every mount gets the chosen weapon; a hull takes as many as it has mounts, up to 8)
			success = buildDroid(yard, "Warship", bwPair.b, propulsions, "", "", bwPair.w, bwPair.w, bwPair.w, bwPair.w, bwPair.w, bwPair.w, bwPair.w, bwPair.w) || success;
		}
	});

	return success;
};

// The water spot a ship would attack this object from, if there is one it can reach
function attackSpot(obj, home)
{
	return waterSpots(obj.x, obj.y, 5, 1).filter((w) => (propulsionCanReach(NAVAL_CHECK_PROP, home.x, home.y, w.x, w.y)))[0];
}

function targetAlive()
{
	if (!defined(navalTarget))
	{
		return false;
	}

	const obj = getObject(navalTarget.type, navalTarget.player, navalTarget.id);

	return obj !== null && defined(obj);
}

function chooseNavalTarget(from, home)
{
	let candidates = [];

	enumLivingPlayers().filter(isEnemy).forEach((p) => {
		candidates = candidates.concat(enumStruct(p), enumDroid(p));
	});

	candidates = candidates
		.filter((obj) => (!isVTOL(obj)))
		.sort((a, b) => (distance(a, from) - distance(b, from)))
		.slice(0, 40);

	for (let i = 0; i < candidates.length; ++i)
	{
		const spot = attackSpot(candidates[i], home);

		if (defined(spot))
		{
			return { id: candidates[i].id, type: candidates[i].type, player: candidates[i].player, wx: spot.x, wy: spot.y };
		}
	}

	return undefined;
}

// Landing craft: wanted when ships can reach an enemy that land units cannot
function landingWanted()
{
	function uncached()
	{
		if (!navalWanted())
		{
			return false;
		}

		const ground = getPropulsionStatsComponents(PROPULSIONUSAGE.GROUND).last();

		for (let i = 0; i < maxPlayers; ++i)
		{
			if (i !== me && isEnemy(i) && defined(startPositions[i]) && defined(ground)
			    && !propulsionCanReach(ground, baseLocation.x, baseLocation.y, startPositions[i].x, startPositions[i].y))
			{
				return true;
			}
		}

		return false;
	}

	return cached(uncached, 60000, "navalLandingWanted");
}

// The nearest enemy base that land units cannot reach from ours: where landings go
function invasionTarget()
{
	function uncached()
	{
		const ground = getPropulsionStatsComponents(PROPULSIONUSAGE.GROUND).last();
		let best;
		let bestDist = Infinity;

		for (let i = 0; i < maxPlayers; ++i)
		{
			if (i === me || !isEnemy(i) || !defined(startPositions[i]) || !defined(ground)
			    || propulsionCanReach(ground, baseLocation.x, baseLocation.y, startPositions[i].x, startPositions[i].y))
			{
				continue;
			}

			const d = distance(startPositions[i], baseLocation);

			if (d < bestDist)
			{
				bestDist = d;
				best = { x: startPositions[i].x, y: startPositions[i].y, player: i };
			}
		}

		return best;
	}

	return cached(uncached, 60000, "navalInvasionTarget");
}

// Land units at home that cannot reach the invasion target by land: passengers
function strandedUnits()
{
	const target = invasionTarget();

	if (!defined(target))
	{
		return [];
	}

	return enumDroid(me, DROID_WEAPON).concat(enumDroid(me, DROID_CYBORG)).filter((d) => (
		!isNavalDroid(d) && !isVTOL(d) && d.order !== DORDER_EMBARK && distanceToBase(d) < baseScale * 2 &&
		!droidCanReach(d, target.x, target.y)
	));
}

// Whether land units from the base can walk to the shore next to this water tile
function beachReachable(x, y)
{
	const ground = getPropulsionStatsComponents(PROPULSIONUSAGE.GROUND).last();

	if (!defined(ground))
	{
		return false;
	}

	for (let dy = -1; dy <= 1; ++dy)
	{
		for (let dx = -1; dx <= 1; ++dx)
		{
			const tx = x + dx, ty = y + dy;

			if (tx > 2 && ty > 2 && tx < mapWidth - 3 && ty < mapHeight - 3 && terrainType(tx, ty) !== TER_WATER && terrainType(tx, ty) !== TER_CLIFFFACE
			    && propulsionCanReach(ground, baseLocation.x, baseLocation.y, tx, ty))
			{
				return true;
			}
		}
	}

	return false;
}

// Where landing craft wait to load: the water nearest the base that ships can reach from the
// home waters (so right off a beach)
function loadingBeach()
{
	function uncached()
	{
		const home = homeWater();

		if (!defined(home))
		{
			return undefined;
		}

		for (let r = 1; r <= baseScale * 2; ++r)
		{
			for (let dy = -r; dy <= r; ++dy)
			{
				for (let dx = -r; dx <= r; ++dx)
				{
					if (Math.max(Math.abs(dx), Math.abs(dy)) !== r)
					{
						continue;
					}

					const x = baseLocation.x + dx, y = baseLocation.y + dy;

					if (isWater(x, y) && propulsionCanReach(NAVAL_CHECK_PROP, home.x, home.y, x, y) && beachReachable(x, y))
					{
						return { x: x, y: y };
					}
				}
			}
		}

		return home;
	}

	return cached(uncached, Infinity, "navalLoadingBeach");
}

let landingState = {}; // per craft id: { loading: game time loading started }
let landingGroup; // passengers, kept away from NullBot's other groups while they board

_global.isLandingPassenger = function(droid) {
	return defined(landingGroup) && droid.group === landingGroup && droid.order === DORDER_EMBARK;
};

// Units put ashore (or that gave up boarding) go back to NullBot's attack groups
function releasePassengers()
{
	if (defined(landingGroup))
	{
		enumGroup(landingGroup).filter((d) => (d.order !== DORDER_EMBARK)).forEach(groupDroid);
	}
}

function landingTick()
{
	const crafts = enumDroid(me).filter(isLandingCraft);

	if (crafts.length === 0)
	{
		return;
	}

	const home = homeWater();

	crafts.forEach((craft) => {
		const st = landingState[craft.id] || (landingState[craft.id] = { loading: 0, sailing: false });
		const cargo = craft.cargoCount > 0 ? craft.cargoCount - 1 : 0; // (the group counts the craft itself)

		if (craft.order === DORDER_DISEMBARK)
		{
			st.sailing = true;
			return; // under way
		}

		if (st.sailing)
		{
			st.sailing = false;
			releasePassengers();
			navalDebug("landing craft " + (cargo === 0 ? "landed its units" : "could not land " + cargo + " units") + " at " + craft.x + "," + craft.y);
		}

		if (cargo === 0 && distanceToBase(craft) > baseScale * 2.5)
		{
			// Empty and away: come home
			if (craft.order !== DORDER_MOVE && defined(home))
			{
				orderDroidLoc(craft, DORDER_MOVE, home.x, home.y);
			}
			st.loading = 0;
			return;
		}

		const full = craft.cargoLeft < 2;
		const waited = st.loading > 0 && gameTime - st.loading > 60000;

		if (cargo > 0 && (full || waited))
		{
			// Sail and land the units at the enemy base across the water
			const target = invasionTarget();

			if (defined(target))
			{
				orderDroidLoc(craft, DORDER_DISEMBARK, target.x, target.y);
				st.sailing = true;
				navalDebug("landing craft sails with " + cargo + " units for player " + target.player + " at " + target.x + "," + target.y);
			}
			st.loading = 0;
			return;
		}

		// Loading at home: wait off the beach and call stranded units aboard
		const beach = loadingBeach();

		if (defined(beach) && distance(craft, beach) > 2)
		{
			if (craft.order !== DORDER_MOVE)
			{
				orderDroidLoc(craft, DORDER_MOVE, beach.x, beach.y);
			}
			return;
		}

		const passengers = strandedUnits().filter((d) => (d.order !== DORDER_EMBARK));

		if (passengers.length > 0)
		{
			if (st.loading === 0)
			{
				st.loading = gameTime;
			}

			if (!defined(landingGroup))
			{
				landingGroup = MAX_GROUPS + 8; // (NullBot numbers its groups itself; newGroup() would clash with them)
			}

			passengers.slice(0, 3).forEach((d) => {
				groupAdd(landingGroup, d);
				orderDroidObj(d, DORDER_EMBARK, craft);
			});
			navalDebug("calling " + Math.min(3, passengers.length) + " of " + passengers.length + " stranded units aboard the landing craft at " + craft.x + "," + craft.y);
		}
	});
}

// Tactics, every few seconds
_global.navalTick = function() {
	buildShipyard();
	landingTick();

	const home = homeWater();

	if (!defined(home))
	{
		return;
	}

	const ships = myShips();

	if (ships.length === 0)
	{
		return;
	}

	// Damaged ships go for repairs; repaired ones rejoin
	const ready = ships.filter((ship) => {
		if (ship.order === DORDER_RTR)
		{
			return false;
		}

		if (ship.health < NAVAL_RETREAT)
		{
			orderDroid(ship, DORDER_RTR);
			return false;
		}

		return true;
	});

	if (ready.length === 0)
	{
		return;
	}

	// Enemy ships or units in the home waters: everyone defends
	const intruders = enumRange(home.x, home.y, baseScale * 0.7, ENEMIES, false).filter((obj) => (obj.type === DROID && !isVTOL(obj)));

	if (intruders.length > 0)
	{
		ready.forEach((ship) => {
			orderDroidLoc(ship, DORDER_SCOUT, intruders[0].x, intruders[0].y);
		});
		return;
	}

	const sailing = ready.filter((ship) => (distance(ship, home) > baseScale * 0.6));

	if (ready.length < NAVAL_GATHER && sailing.length === 0)
	{
		// Wait at home for the rest of the fleet
		ready.filter((ship) => (ship.order !== DORDER_SCOUT && distance(ship, home) > 4)).forEach((ship) => {
			orderDroidLoc(ship, DORDER_SCOUT, home.x, home.y);
		});
		return;
	}

	if (!targetAlive())
	{
		const from = sailing.length > 0 ? sailing[0] : home;

		navalTarget = chooseNavalTarget(from, home);

		if (defined(navalTarget))
		{
			navalDebug("fleet of " + ready.length + " attacking player " + navalTarget.player + " object " + navalTarget.id + " from the water at " + navalTarget.wx + "," + navalTarget.wy);
		}
	}

	if (!defined(navalTarget))
	{
		return;
	}

	ready.forEach((ship) => {
		if (ship.order !== DORDER_SCOUT || distance(ship, { x: navalTarget.wx, y: navalTarget.wy }) > 3)
		{
			orderDroidLoc(ship, DORDER_SCOUT, navalTarget.wx, navalTarget.wy);
		}
	});
};

////////////////////////////////////////////////////////////////////////////////////////////
})(this);
