/*
	This file is part of Warzone 2100.
	Copyright (C) 2026  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
/// Warzone 2126: details panel for a single selected unit or building (see infopanel2126.h).

#include "infopanel2126.h"
#include "hud2126_style.h"

#include "lib/framework/frame.h"
#include "lib/framework/wzapp.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/netplay/netplay.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/paragraph.h"

#include "../hci.h"
#include "../action.h"
#include "../carrier2126.h"
#include "../combat.h"
#include "../droid.h"
#include "../order.h"
#include "../game_world.h"
#include "../group.h"
#include "../multiplay.h"
#include "../projectile.h"
#include "../map.h"
#include "../radar.h"
#include "../research.h"
#include "../stats.h"
#include "../structure.h"
#include "../transporter.h"

#include <string>
#include <vector>

namespace
{

enum class RowStyle { Title, Sub, Section, Line, Bar };

struct Row
{
	RowStyle style;
	std::string label;
	std::string value;
	float fraction = 0.f; // Bar rows
	PIELIGHT colour = hud2126::text();
};

/// Ranges in tiles, the unit players see on the map (one decimal)
std::string tiles(int worldUnits)
{
	const int tenths = (worldUnits * 10 + TILE_UNITS / 2) / TILE_UNITS;
	return astringf(_("%d.%d tiles"), tenths / 10, tenths % 10);
}

std::string percent(int64_t part, int64_t whole)
{
	if (whole <= 0)
	{
		return "0%";
	}
	return astringf("%d%%", static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(100, part * 100 / whole))));
}

const char *orderText(const DROID *psDroid)
{
	switch (psDroid->order.type)
	{
	case DORDER_NONE: return psDroid->action == DACTION_NONE ? _("Idle") : getDroidActionName(psDroid->action);
	case DORDER_STOP: return _("Stopping");
	case DORDER_MOVE: return _("Moving");
	case DORDER_ATTACK: return _("Attacking");
	case DORDER_BUILD: return _("Building");
	case DORDER_HELPBUILD: return _("Helping to build");
	case DORDER_LINEBUILD: return _("Building a line");
	case DORDER_DEMOLISH: return _("Demolishing");
	case DORDER_REPAIR: return _("Repairing");
	case DORDER_OBSERVE: return _("Observing");
	case DORDER_FIRESUPPORT: return _("Fire support");
	case DORDER_RTB: return _("Returning to base");
	case DORDER_RTR:
	case DORDER_RTR_SPECIFIED: return _("Returning for repair");
	case DORDER_EMBARK: return _("Boarding");
	case DORDER_DISEMBARK: return _("Landing troops");
	case DORDER_ATTACKTARGET: return _("Attacking");
	case DORDER_COMMANDERSUPPORT: return _("Supporting commander");
	case DORDER_BUILDMODULE: return _("Building a module");
	case DORDER_RECYCLE: return _("Going to be recycled");
	case DORDER_GUARD: return _("Guarding");
	case DORDER_CIRCLE: return _("Circling");
	case DORDER_HOLD: return _("Holding position");
	case DORDER_SCOUT: return _("Scouting");
	case DORDER_PATROL: return _("Patrolling");
	case DORDER_REARM: return _("Rearming");
	case DORDER_RECOVER: return _("Picking up an artifact");
	case DORDER_RESTORE: return _("Restoring");
	case DORDER_EARTHWORK: return _("Earthworks");
	case DORDER_CARRIER: return carrier2126::isDocked(psDroid) ? _("On carrier deck") : _("Flying to carrier");
	default: return getDroidOrderName(psDroid->order.type);
	}
}

void addWeapons(std::vector<Row> &rows, const BASE_OBJECT *psObj)
{
	if (psObj->numWeaps == 0)
	{
		return;
	}
	rows.push_back({RowStyle::Section, psObj->numWeaps > 1 ? astringf(_("Weapons (%u)"), psObj->numWeaps) : std::string(_("Weapon")), ""});
	for (unsigned i = 0; i < psObj->numWeaps && i < MAX_WEAPONS; ++i)
	{
		const WEAPON &w = psObj->asWeaps[i];
		if (w.nStat >= asWeaponStats.size())
		{
			continue;
		}
		const WEAPON_STATS &stats = asWeaponStats[w.nStat];
		rows.push_back({RowStyle::Line, getLocalizedStatsName(&stats), ""});
		std::string detail = astringf(_("Damage %d   Range %s   %d/min"), weaponDamage(stats, psObj->player),
		                              tiles(proj_GetLongRange(stats, psObj->player)).c_str(), weaponROF(stats, psObj->player));
		if (stats.vtolAttackRuns > 0 && psObj->type == OBJ_DROID)
		{
			const unsigned runs = stats.vtolAttackRuns;
			const unsigned left = w.usedAmmo >= runs ? 0 : runs - w.usedAmmo;
			detail += astringf(_("   Ammo %u/%u"), left, runs);
		}
		rows.push_back({RowStyle::Sub, "", detail});
	}
}

void addHealthAndArmour(std::vector<Row> &rows, const BASE_OBJECT *psObj, unsigned body, unsigned maxBody)
{
	const float f = maxBody > 0 ? static_cast<float>(body) / static_cast<float>(maxBody) : 1.f;
	Row health{RowStyle::Bar, _("Health"), astringf("%u / %u", body, maxBody)};
	health.fraction = f;
	health.colour = hud2126::health(f);
	rows.push_back(health);
	rows.push_back({RowStyle::Line, _("Armour"), astringf(_("Kinetic %d   Thermal %d"), objArmour(psObj, WC_KINETIC), objArmour(psObj, WC_HEAT))});
}

std::vector<Row> droidRows(const DROID *psDroid)
{
	std::vector<Row> rows;
	rows.push_back({RowStyle::Title, droidGetName(psDroid), ""});
	std::string parts;
	if (psDroid->asBits[COMP_BODY] < asBodyStats.size())
	{
		parts = getLocalizedStatsName(&asBodyStats[psDroid->asBits[COMP_BODY]]);
	}
	if (psDroid->asBits[COMP_PROPULSION] < asPropulsionStats.size())
	{
		parts += std::string("  |  ") + getLocalizedStatsName(&asPropulsionStats[psDroid->asBits[COMP_PROPULSION]]);
	}
	rows.push_back({RowStyle::Sub, "", parts});
	addHealthAndArmour(rows, psDroid, psDroid->body, psDroid->originalBody);
	// baseSpeed is world units per second
	{
		const int tenths = static_cast<int>((psDroid->baseSpeed * 10 + TILE_UNITS / 2) / TILE_UNITS);
		rows.push_back({RowStyle::Line, _("Top speed"), astringf(_("%d.%d tiles/s"), tenths / 10, tenths % 10)});
	}
	rows.push_back({RowStyle::Line, _("Rank"), astringf(_("%s   Kills %u"), getDroidLevelName(psDroid), psDroid->kills)});
	rows.push_back({RowStyle::Line, _("Order"), orderText(psDroid)});

	addWeapons(rows, psDroid);

	// System turrets
	const int player = psDroid->player;
	if (psDroid->droidType == DROID_CONSTRUCT || psDroid->droidType == DROID_CYBORG_CONSTRUCT)
	{
		if (psDroid->asBits[COMP_CONSTRUCT] < asConstructStats.size())
		{
			const CONSTRUCT_STATS &s = asConstructStats[psDroid->asBits[COMP_CONSTRUCT]];
			rows.push_back({RowStyle::Section, getLocalizedStatsName(&s), ""});
			rows.push_back({RowStyle::Line, _("Build rate"), astringf(_("%d points/s"), constructorPoints(s, player))});
		}
	}
	if (psDroid->droidType == DROID_REPAIR || psDroid->droidType == DROID_CYBORG_REPAIR)
	{
		if (psDroid->asBits[COMP_REPAIRUNIT] < asRepairStats.size())
		{
			const REPAIR_STATS &s = asRepairStats[psDroid->asBits[COMP_REPAIRUNIT]];
			rows.push_back({RowStyle::Section, getLocalizedStatsName(&s), ""});
			rows.push_back({RowStyle::Line, _("Repair rate"), astringf(_("%d points/s"), repairPoints(s, player))});
		}
	}
	if (psDroid->asBits[COMP_SENSOR] < asSensorStats.size())
	{
		const SENSOR_STATS &s = asSensorStats[psDroid->asBits[COMP_SENSOR]];
		const bool sensorTurret = psDroid->droidType == DROID_SENSOR;
		rows.push_back({RowStyle::Line, _("Sight"), sensorTurret ? astringf(_("%s (%s)"), tiles(sensorRange(s, player)).c_str(), getLocalizedStatsName(&s)) : tiles(sensorRange(s, player))});
	}

	// Warzone 2126: aircraft carriers and the VTOLs assigned to them
	if (carrier2126::isCarrier(psDroid))
	{
		rows.push_back({RowStyle::Line, _("Aircraft"), astringf(_("%d of %d on deck"), carrier2126::dockedCount(psDroid), carrier2126::capacity(psDroid))});
	}
	if (psDroid->isVtol() && psDroid->carrierId2126 != 0)
	{
		rows.push_back({RowStyle::Line, _("Carrier"), carrier2126::isDocked(psDroid) ? _("On deck") : _("Assigned")});
	}

	// Cargo
	if (psDroid->isTransporter() && psDroid->psGroup)
	{
		int aboard = 0;
		for (const DROID *psCurr : psDroid->psGroup->psList)
		{
			aboard += (psCurr != psDroid) ? 1 : 0;
		}
		rows.push_back({RowStyle::Line, _("Aboard"), astringf(_("%d units, %d of %d places free"), aboard, calcRemainingCapacity(psDroid), transporterCapacity(psDroid))});
	}
	return rows;
}

std::vector<Row> structureRows(const STRUCTURE *psStruct)
{
	std::vector<Row> rows;
	const STRUCTURE_STATS *stats = psStruct->pStructureType;
	rows.push_back({RowStyle::Title, getLocalizedStatsName(stats), ""});
	if (psStruct->status == SS_BEING_BUILT)
	{
		Row built{RowStyle::Bar, _("Construction"), percent(psStruct->currentBuildPts, std::max<int64_t>(1, structureBuildPointsToCompletion(*psStruct) + psStruct->currentBuildPts))};
		built.fraction = static_cast<float>(psStruct->currentBuildPts) / static_cast<float>(std::max<uint32_t>(1, structureBuildPointsToCompletion(*psStruct) + psStruct->currentBuildPts));
		built.colour = hud2126::accent();
		rows.push_back(built);
	}
	addHealthAndArmour(rows, psStruct, psStruct->body, psStruct->structureBody());
	if (psStruct->capacity > 0)
	{
		rows.push_back({RowStyle::Line, _("Modules"), astringf("%u", static_cast<unsigned>(psStruct->capacity))});
	}

	if (psStruct->status == SS_BUILT && psStruct->pFunctionality)
	{
		const FUNCTIONALITY *f = psStruct->pFunctionality;
		if (psStruct->isFactory())
		{
			const FACTORY &fac = f->factory;
			if (fac.psSubject)
			{
				const int total = static_cast<int>(calcTemplateBuild(fac.psSubject));
				Row prod{RowStyle::Bar, _("Producing"), astringf("%s  %s", fac.psSubject->name.toUtf8().c_str(), percent(total - fac.buildPointsRemaining, total).c_str())};
				prod.fraction = total > 0 ? static_cast<float>(total - fac.buildPointsRemaining) / static_cast<float>(total) : 0.f;
				prod.colour = hud2126::accent();
				rows.push_back(prod);
				if (fac.timeStartHold != 0)
				{
					rows.push_back({RowStyle::Sub, "", _("On hold")});
				}
			}
			else
			{
				rows.push_back({RowStyle::Line, _("Producing"), _("Idle")});
			}
		}
		else if (stats->type == REF_RESEARCH)
		{
			const RESEARCH_FACILITY &res = f->researchFacility;
			if (res.psSubject && psStruct->player < MAX_PLAYERS && static_cast<size_t>(res.psSubject->index) < asPlayerResList[psStruct->player].size())
			{
				const int done = static_cast<int>(asPlayerResList[psStruct->player][res.psSubject->index].currentPoints);
				const int total = static_cast<int>(res.psSubject->researchPoints);
				Row r{RowStyle::Bar, _("Researching"), astringf("%s  %s", getLocalizedStatsName(res.psSubject), percent(done, total).c_str())};
				r.fraction = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.f;
				r.colour = hud2126::accent();
				rows.push_back(r);
				rows.push_back({RowStyle::Sub, "", astringf(_("%d research points/s"), getBuildingResearchPoints(psStruct))});
			}
			else
			{
				rows.push_back({RowStyle::Line, _("Researching"), _("Idle")});
			}
		}
		else if (stats->type == REF_POWER_GEN)
		{
			int linked = 0;
			for (const STRUCTURE *psDerrick : f->powerGenerator.apResExtractors)
			{
				linked += psDerrick ? 1 : 0;
			}
			rows.push_back({RowStyle::Line, _("Derricks"), astringf(_("%d of %d linked"), linked, NUM_POWER_MODULES)});
		}
		else if (stats->type == REF_REPAIR_FACILITY)
		{
			const BASE_OBJECT *psObj = f->repairFacility.psObj;
			rows.push_back({RowStyle::Line, _("Repairing"), psObj && psObj->type == OBJ_DROID ? droidGetName((const DROID *)psObj) : _("Idle")});
		}
		else if (stats->type == REF_REARM_PAD)
		{
			const BASE_OBJECT *psObj = f->rearmPad.psObj;
			rows.push_back({RowStyle::Line, _("Rearming"), psObj && psObj->type == OBJ_DROID ? droidGetName((const DROID *)psObj) : _("Idle")});
		}
	}
	addWeapons(rows, psStruct);
	if (psStruct->status == SS_BUILT && objSensorRange(psStruct) > 0 && psStruct->numWeaps == 0 && stats->type != REF_RESOURCE_EXTRACTOR)
	{
		rows.push_back({RowStyle::Line, _("Sight"), tiles(objSensorRange(psStruct))});
	}
	return rows;
}

constexpr int kW = 300;
constexpr int kPad = 8;

class InfoPanel : public WIDGET
{
public:
	void setRows(std::vector<Row> &&newRows)
	{
		rows = std::move(newRows);
		if (cacheA.size() < rows.size())
		{
			cacheA.resize(rows.size());
			cacheB.resize(rows.size());
		}
		int h = kPad;
		for (const Row &r : rows)
		{
			h += rowHeight(r);
		}
		wantedH = h + kPad;
	}

	int32_t idealWidth() override { return kW; }
	int32_t idealHeight() override { return wantedH; }

	void display(int xOffset, int yOffset) override
	{
		if (rows.empty())
		{
			return;
		}
		const int x0 = xOffset + x(), y0 = yOffset + y();
		pie_UniTransBoxFill(x0, y0, x0 + width(), y0 + height(), hud2126::bg());
		iV_Box(x0, y0, x0 + width() - 1, y0 + height() - 1, hud2126::border());
		int ty = y0 + kPad;
		const int lx = x0 + kPad, vx = x0 + kPad + 92, right = x0 + width() - kPad;
		for (size_t i = 0; i < rows.size(); ++i)
		{
			const Row &r = rows[i];
			switch (r.style)
			{
			case RowStyle::Title:
				cacheA[i].setText(WzString::fromUtf8(r.label), font_regular_bold);
				cacheA[i]->render(lx, ty - cacheA[i]->aboveBase(), hud2126::value());
				break;
			case RowStyle::Sub:
				cacheA[i].setText(WzString::fromUtf8(r.value), font_small);
				cacheA[i]->render(r.label.empty() && i > 0 && rows[i - 1].style == RowStyle::Line ? lx + 10 : lx, ty - cacheA[i]->aboveBase(), hud2126::label());
				break;
			case RowStyle::Section:
				pie_UniTransBoxFill(lx, ty + 1, right, ty + 2, hud2126::border());
				cacheA[i].setText(WzString::fromUtf8(r.label), font_small);
				cacheA[i]->render(lx, ty + 5 - cacheA[i]->aboveBase(), hud2126::accent());
				break;
			case RowStyle::Line:
				cacheA[i].setText(WzString::fromUtf8(r.label), font_small);
				if (r.value.empty())
				{
					cacheA[i]->render(lx, ty - cacheA[i]->aboveBase(), hud2126::text());
				}
				else
				{
					cacheA[i]->render(lx, ty - cacheA[i]->aboveBase(), hud2126::label());
					cacheB[i].setText(WzString::fromUtf8(r.value), font_small);
					cacheB[i]->render(vx, ty - cacheB[i]->aboveBase(), hud2126::value());
				}
				break;
			case RowStyle::Bar:
			{
				cacheA[i].setText(WzString::fromUtf8(r.label), font_small);
				cacheA[i]->render(lx, ty - cacheA[i]->aboveBase(), hud2126::label());
				cacheB[i].setText(WzString::fromUtf8(r.value), font_small);
				cacheB[i]->render(vx, ty - cacheB[i]->aboveBase(), hud2126::value());
				const int barY = ty + cacheB[i]->lineSize() + 1;
				pie_UniTransBoxFill(vx, barY, right, barY + 3, pal_RGBA(0, 0, 0, 160));
				pie_UniTransBoxFill(vx, barY, vx + static_cast<int>((right - vx) * std::max(0.f, std::min(1.f, r.fraction))), barY + 3, r.colour);
				break;
			}
			}
			ty += rowHeight(r);
		}
	}

private:
	static int rowHeight(const Row &r)
	{
		switch (r.style)
		{
		case RowStyle::Title: return 22;
		case RowStyle::Section: return 20;
		case RowStyle::Bar: return 21;
		default: return 15;
		}
	}

	std::vector<Row> rows;
	std::vector<WzCachedText> cacheA, cacheB;
	int wantedH = 60;
};

std::shared_ptr<InfoPanel> thePanel;

bool spectating()
{
	return selectedPlayer >= MAX_PLAYERS || (bMultiPlayer && selectedPlayer < NetPlay.players.size() && NetPlay.players[selectedPlayer].isSpectator);
}

/// The one selected object, or nullptr when none or more than one is selected
const BASE_OBJECT *singleSelection()
{
	const BASE_OBJECT *found = nullptr;
	for (const DROID *psDroid : gameWorld.objects.droids[selectedPlayer])
	{
		if (psDroid->selected && !psDroid->died)
		{
			if (found)
			{
				return nullptr;
			}
			found = psDroid;
		}
	}
	for (const STRUCTURE *psStruct : gameWorld.objects.structures[selectedPlayer])
	{
		if (psStruct->selected && !psStruct->died)
		{
			if (found)
			{
				return nullptr;
			}
			found = psStruct;
		}
	}
	return found;
}

} // anonymous namespace

void infoPanel2126Remove()
{
	if (thePanel)
	{
		if (psWScreen)
		{
			widgDelete(psWScreen, IDHUD2126_INFO);
		}
		thePanel.reset();
	}
}

void infoPanel2126Update()
{
	if (psWScreen == nullptr || spectating())
	{
		infoPanel2126Remove();
		return;
	}
	const BASE_OBJECT *psObj = singleSelection();
	const bool panelFree = (intMode == INT_NORMAL || intMode == INT_ORDER || intMode == INT_STAT || intMode == INT_OBJECT);
	if (!psObj || !panelFree || !getWidgetsStatus())
	{
		if (thePanel)
		{
			thePanel->hide();
		}
		return;
	}
	if (!thePanel || widgGetFromID(psWScreen, IDHUD2126_INFO) == nullptr)
	{
		thePanel = std::make_shared<InfoPanel>();
		thePanel->id = IDHUD2126_INFO;
		psWScreen->psForm->attach(thePanel);
		thePanel->setCalcLayout([](WIDGET *psWidget) {
			// Bottom right, just above the minimap
			const int h = psWidget->idealHeight();
			const int top = std::max(40, radarTopY2126() - h - 10);
			psWidget->setGeometry(static_cast<int>(screenWidth) - kW - BASE_GAP * 4, top, kW, h);
		});
	}
	const int oldH = thePanel->idealHeight();
	thePanel->setRows(psObj->type == OBJ_DROID ? droidRows((const DROID *)psObj) : structureRows((const STRUCTURE *)psObj));
	if (!thePanel->visible() || oldH != thePanel->idealHeight())
	{
		thePanel->show();
		thePanel->callCalcLayout();
	}
}
