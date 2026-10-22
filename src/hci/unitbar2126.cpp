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
/// Warzone 2126 interface, phase 2: the selected-units bar (see unitbar2126.h).

#include "unitbar2126.h"
#include "hud2126_style.h"

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/framework/wzapp.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piematrix.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/netplay/netplay.h"
#include "lib/widget/widget.h"
#include "lib/widget/paragraph.h"
#include "lib/widget/form.h"

#include "../hci.h"
#include "../intorder.h"
#include "../intimage.h"
#include "../droid.h"
#include "../order.h"
#include "../keybind.h"
#include "../component.h"
#include "../display3d.h"
#include "../game_world.h"
#include "../multiplay.h"
#include "../qtscript.h"
#include "../stats.h"
#include "../objmem.h"
#include "../map.h"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace
{

bool showClassicOrders = false;

// MARK: - Commands

/// Which selected units share a state: none, some, or all of them
enum class Shared { None, Some, All };

struct CommandDef
{
	const char *label;  // N_() strings, translated at display
	const char *key;
	const char *tip;
	std::function<void()> action;
	std::function<bool(const DROID *)> isSet; // nullptr: an order with no lasting state to show
	bool separatorBefore;
};

bool secondaryIs(const DROID *psDroid, SECONDARY_ORDER sec, SECONDARY_STATE state)
{
	return secondaryGetState(psDroid, sec, ModeQueue) == state;
}

const std::vector<CommandDef> &commandDefs()
{
	static const std::vector<CommandDef> defs = {
		{ N_("Stop"), "S", N_("Stop: cancel orders"), kf_OrderDroid(DORDER_STOP), nullptr, false },
		{ N_("Hold"), "H", N_("Hold Position: stay here and fire at anything in range"), kf_OrderDroid(DORDER_HOLD), [](const DROID *d) { return d->order.type == DORDER_HOLD; }, false },
		{ N_("Guard"), "G", N_("Guard Position: chase attackers a short way, then return"), kf_SetDroid(DSO_HALTTYPE, DSS_HALT_GUARD), [](const DROID *d) { return d->order.type != DORDER_HOLD && secondaryIs(d, DSO_HALTTYPE, DSS_HALT_GUARD); }, false },
		{ N_("Pursue"), "P", N_("Pursue: chase targets until they are destroyed"), kf_SetDroid(DSO_HALTTYPE, DSS_HALT_PURSUE), [](const DROID *d) { return d->order.type != DORDER_HOLD && secondaryIs(d, DSO_HALTTYPE, DSS_HALT_PURSUE); }, false },
		{ N_("Patrol"), "Q", N_("Patrol: then right-click the far point"), kf_SetDroid(DSO_PATROL, DSS_PATROL_SET), [](const DROID *d) { return secondaryIs(d, DSO_PATROL, DSS_PATROL_SET) || d->order.type == DORDER_PATROL; }, false },
		{ N_("Base"), "Sh+H", N_("Return to HQ"), kf_SetDroid(DSO_RETURN_TO_LOC, DSS_RTL_BASE), [](const DROID *d) { return d->order.type == DORDER_RTB; }, false },
		{ N_("Repair"), "R", N_("Return for repair"), kf_SetDroid(DSO_RETURN_TO_LOC, DSS_RTL_REPAIR), [](const DROID *d) { return d->order.type == DORDER_RTR || d->order.type == DORDER_RTR_SPECIFIED; }, false },
		{ N_("Fire"), "F", N_("Fire at Will"), kf_SetDroid(DSO_ATTACK_LEVEL, DSS_ALEV_ALWAYS), [](const DROID *d) { return secondaryIs(d, DSO_ATTACK_LEVEL, DSS_ALEV_ALWAYS); }, true },
		{ N_("Return"), "E", N_("Return Fire: only shoot back"), kf_SetDroid(DSO_ATTACK_LEVEL, DSS_ALEV_ATTACKED), [](const DROID *d) { return secondaryIs(d, DSO_ATTACK_LEVEL, DSS_ALEV_ATTACKED); }, false },
		{ N_("Cease"), "C", N_("Hold Fire"), kf_SetDroid(DSO_ATTACK_LEVEL, DSS_ALEV_NEVER), [](const DROID *d) { return secondaryIs(d, DSO_ATTACK_LEVEL, DSS_ALEV_NEVER); }, false },
	};
	return defs;
}

// MARK: - Selection summary

struct UnitGroup
{
	std::string name;
	std::vector<DROID *> droids;
	float healthSum = 0.f;
	unsigned bestLevel = 0;
};

struct Summary
{
	std::vector<UnitGroup> groups; // in order of first appearance, largest first
	size_t total = 0;
	std::vector<Shared> commandShared;
};

Summary buildSummary()
{
	Summary s;
	if (selectedPlayer >= MAX_PLAYERS)
	{
		return s;
	}
	std::map<std::string, size_t> index;
	const auto &defs = commandDefs();
	std::vector<size_t> setCount(defs.size(), 0);
	for (DROID *psDroid : gameWorld.objects.droids[selectedPlayer])
	{
		if (!psDroid->selected || psDroid->died)
		{
			continue;
		}
		++s.total;
		std::string name = droidGetName(psDroid);
		auto it = index.find(name);
		if (it == index.end())
		{
			it = index.emplace(name, s.groups.size()).first;
			s.groups.push_back(UnitGroup{name, {}, 0.f, 0});
		}
		UnitGroup &g = s.groups[it->second];
		g.droids.push_back(psDroid);
		g.healthSum += psDroid->originalBody > 0 ? static_cast<float>(psDroid->body) / static_cast<float>(psDroid->originalBody) : 1.f;
		g.bestLevel = std::max(g.bestLevel, getDroidLevel(psDroid));
		for (size_t i = 0; i < defs.size(); ++i)
		{
			if (defs[i].isSet && defs[i].isSet(psDroid))
			{
				++setCount[i];
			}
		}
	}
	std::stable_sort(s.groups.begin(), s.groups.end(), [](const UnitGroup &a, const UnitGroup &b) { return a.droids.size() > b.droids.size(); });
	s.commandShared.resize(defs.size());
	for (size_t i = 0; i < defs.size(); ++i)
	{
		s.commandShared[i] = (setCount[i] == 0) ? Shared::None : (setCount[i] == s.total ? Shared::All : Shared::Some);
	}
	return s;
}

/// Replace the selection with `keep` (a subset of the selected units), refreshing the interface once
void applySelection(const std::function<bool(const DROID *)> &keep)
{
	bool changed = false;
	for (DROID *psDroid : gameWorld.objects.droids[selectedPlayer])
	{
		if (psDroid->selected && !keep(psDroid))
		{
			psDroid->selected = false;
			changed = true;
		}
	}
	if (changed)
	{
		intRefreshScreen();
		triggerEventSelected();
	}
}

// MARK: - The bar

constexpr int kPad = 4;
constexpr int kGap = 3;
constexpr int kCmdW = 48;
constexpr int kCmdH = 32;
constexpr int kSepW = 9;
constexpr int kCardW = 64;
constexpr int kCardH = 60;
constexpr int kInfoW = 230; // single-unit details beside its card
constexpr int kBarH = kPad + kCmdH + kGap + 2 + kCardH + kPad;

class UnitBar : public WIDGET
{
public:
	UnitBar()
	{
		const auto &defs = commandDefs();
		cmdLabel.resize(defs.size() + 1);
		cmdKey.resize(defs.size() + 1);
		for (size_t i = 0; i < defs.size(); ++i)
		{
			cmdLabel[i].setText(WzString::fromUtf8(gettext(defs[i].label)), font_small);
			cmdKey[i].setText(WzString::fromUtf8(defs[i].key), font_small);
		}
		cmdLabel[defs.size()].setText(_("More"), font_small);
		cmdKey[defs.size()].setText("...", font_small);
	}

	void refresh()
	{
		summary = buildSummary();
		layout();
	}

	int32_t idealWidth() override { return wantedW; }
	int32_t idealHeight() override { return kBarH; }

	void display(int xOffset, int yOffset) override
	{
		// Rebuilt every frame (cheap), so no pointer to a unit that has since died is ever kept
		const int oldW = wantedW;
		refresh();
		if (wantedW != oldW)
		{
			widgScheduleTask([]() {
				if (auto w = widgGetFromID(psWScreen, IDHUD2126_UNITS))
				{
					w->callCalcLayout();
				}
			});
		}
		if (summary.total == 0)
		{
			return;
		}
		const int x0 = xOffset + x(), y0 = yOffset + y();
		pie_UniTransBoxFill(x0, y0, x0 + width(), y0 + height(), hud2126::bg());
		iV_Box(x0, y0, x0 + width() - 1, y0 + height() - 1, hud2126::border());
		const int hover = hitAt(mouseX() - screenPosX(), mouseY() - screenPosY());

		// Command row
		const auto &defs = commandDefs();
		for (size_t i = 0; i < cmdRects.size(); ++i)
		{
			const WzRect &r = cmdRects[i];
			const bool isMore = (i == defs.size());
			const Shared shared = isMore ? (showClassicOrders ? Shared::All : Shared::None) : summary.commandShared[i];
			const bool hot = (hover == static_cast<int>(i));
			const PIELIGHT fill = shared == Shared::All ? hud2126::tabActive() : (hot ? hud2126::tabHover() : hud2126::tab());
			const int bx = x0 + r.x(), by = y0 + r.y();
			pie_UniTransBoxFill(bx, by, bx + r.width(), by + r.height(), fill);
			iV_Box(bx, by, bx + r.width() - 1, by + r.height() - 1, shared == Shared::All ? hud2126::accent() : hud2126::border());
			if (shared == Shared::Some)
			{
				// Part of the selection: a thin accent line along the bottom
				pie_UniTransBoxFill(bx + 1, by + r.height() - 3, bx + r.width() - 1, by + r.height() - 1, hud2126::accent());
			}
			const PIELIGHT textCol = (hot || shared == Shared::All) ? hud2126::value() : hud2126::text();
			const int blockH = cmdLabel[i]->lineSize() + cmdKey[i]->lineSize();
			const int top = by + (r.height() - blockH) / 2;
			cmdLabel[i]->render(bx + (r.width() - cmdLabel[i]->width()) / 2, top - cmdLabel[i]->aboveBase(), textCol);
			cmdKey[i]->render(bx + (r.width() - cmdKey[i]->width()) / 2, top + cmdLabel[i]->lineSize() - cmdKey[i]->aboveBase(), hud2126::label());
			if (defs.size() > i && defs[i].separatorBefore)
			{
				const int sx = bx - (kSepW + 1) / 2 - 1;
				pie_UniTransBoxFill(sx, by + 4, sx + 1, by + r.height() - 4, hud2126::border());
			}
		}
		// Divider between the rows
		pie_UniTransBoxFill(x0 + kPad, y0 + kPad + kCmdH + kGap, x0 + width() - kPad, y0 + kPad + kCmdH + kGap + 1, hud2126::border());

		// Cards
		for (size_t i = 0; i < cardRects.size(); ++i)
		{
			const WzRect &r = cardRects[i];
			const bool hot = (hover == static_cast<int>(cardHitBase + i));
			const int bx = x0 + r.x(), by = y0 + r.y();
			pie_UniTransBoxFill(bx, by, bx + r.width(), by + r.height(), hot ? hud2126::tabHover() : hud2126::tab());
			iV_Box(bx, by, bx + r.width() - 1, by + r.height() - 1, hot ? hud2126::accent() : hud2126::border());
			if (i == overflowCard)
			{
				overflowText.setText(WzString::fromUtf8(astringf("+%zu", summary.groups.size() - i)), font_regular_bold);
				overflowText->render(bx + (r.width() - overflowText->width()) / 2, by + r.height() / 2 - overflowText->lineSize() / 2 - overflowText->aboveBase(), hud2126::text());
				continue;
			}
			const UnitGroup &g = summary.groups[i];
			drawModel(g.droids.front(), bx, by, r.width(), r.height() - 8);
			// Rank (best in the group), top left
			UDWORD rankImg = getDroidRankGraphicFromLevel(g.bestLevel);
			if (rankImg != UDWORD_MAX)
			{
				iV_DrawImage(IntImages, static_cast<UWORD>(rankImg), bx + 3, by + 3);
			}
			// Count, top right
			if (g.droids.size() > 1)
			{
				countText[i].setText(WzString::fromUtf8(astringf("%zu", g.droids.size())), font_regular_bold);
				countText[i]->render(bx + r.width() - countText[i]->width() - 4, by + 2 - countText[i]->aboveBase(), hud2126::value());
			}
			// Average health, bottom
			const float health = g.healthSum / static_cast<float>(g.droids.size());
			const int barX0 = bx + 4, barX1 = bx + r.width() - 4, barY = by + r.height() - 7;
			pie_UniTransBoxFill(barX0, barY, barX1, barY + 3, pal_RGBA(0, 0, 0, 160));
			pie_UniTransBoxFill(barX0, barY, barX0 + static_cast<int>((barX1 - barX0) * std::max(0.f, std::min(1.f, health))), barY + 3, hud2126::health(health));
		}

		// Details for a single unit
		if (summary.total == 1 && !summary.groups.empty() && infoRect.width() > 0)
		{
			const DROID *psDroid = summary.groups.front().droids.front();
			const int ix = x0 + infoRect.x(), iy = y0 + infoRect.y();
			infoName.setText(WzString::fromUtf8(droidGetName(psDroid)), font_regular_bold);
			std::string weapon;
			if (psDroid->numWeaps > 0 && psDroid->asWeaps[0].nStat < asWeaponStats.size())
			{
				weapon = getLocalizedStatsName(&asWeaponStats[psDroid->asWeaps[0].nStat]);
			}
			infoLine1.setText(WzString::fromUtf8(weapon.empty() ? std::string(_("No weapon")) : weapon), font_small);
			const int healthPct = psDroid->originalBody > 0 ? static_cast<int>(100.0 * psDroid->body / psDroid->originalBody) : 100;
			infoLine2.setText(WzString::fromUtf8(astringf(_("%s    Kills %u    Health %d%%"), getDroidLevelName(psDroid), psDroid->kills, healthPct)), font_small);
			int ty = iy + 6;
			infoName->render(ix, ty - infoName->aboveBase(), hud2126::value());
			ty += infoName->lineSize() + 4;
			infoLine1->render(ix, ty - infoLine1->aboveBase(), hud2126::text());
			ty += infoLine1->lineSize() + 3;
			infoLine2->render(ix, ty - infoLine2->aboveBase(), hud2126::label());
		}
	}

	void clicked(W_CONTEXT *, WIDGET_KEY key) override
	{
		refresh();
		const int hit = hitAt(mouseX() - screenPosX(), mouseY() - screenPosY());
		if (hit < 0 || selectedPlayer >= MAX_PLAYERS)
		{
			return;
		}
		const auto &defs = commandDefs();
		if (hit < static_cast<int>(defs.size()))
		{
			if (key == WKEY_PRIMARY)
			{
				defs[hit].action();
			}
		}
		else if (hit == static_cast<int>(defs.size()))
		{
			showClassicOrders = !showClassicOrders;
			if (showClassicOrders && widgGetFromID(psWScreen, IDORDER_FORM) == nullptr && !summary.groups.empty())
			{
				// The classic panel normally opens on a right-click; open it for the selection
				intObjectSelected(summary.groups.front().droids.front());
			}
		}
		else
		{
			const size_t card = static_cast<size_t>(hit - cardHitBase);
			if (card >= summary.groups.size() || card == overflowCard)
			{
				return;
			}
			const std::string name = summary.groups[card].name;
			if (key == WKEY_SECONDARY)
			{
				const DROID *psDroid = summary.groups[card].droids.front();
				setViewPos(map_coord(psDroid->pos.x), map_coord(psDroid->pos.y), true);
				return;
			}
			const bool shift = keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
			if (shift)
			{
				applySelection([&name](const DROID *d) { return name != droidGetName(d); });
			}
			else
			{
				applySelection([&name](const DROID *d) { return name == droidGetName(d); });
			}
		}
		refresh();
	}

	std::string getTip() override
	{
		refresh();
		const int hit = hitAt(mouseX() - screenPosX(), mouseY() - screenPosY());
		const auto &defs = commandDefs();
		if (hit < 0)
		{
			return {};
		}
		if (hit < static_cast<int>(defs.size()))
		{
			return astringf("%s (%s)", gettext(defs[hit].tip), defs[hit].key);
		}
		if (hit == static_cast<int>(defs.size()))
		{
			return _("More orders: ranges, retreat, factories and the rest of the classic order panel");
		}
		const size_t card = static_cast<size_t>(hit - cardHitBase);
		if (card == overflowCard)
		{
			return _("More unit types are selected");
		}
		if (card < summary.groups.size())
		{
			return astringf(_("%s x%zu: click to keep only these, Shift-click to drop them, right-click to look at them"), summary.groups[card].name.c_str(), summary.groups[card].droids.size());
		}
		return {};
	}

private:
	void layout()
	{
		const auto &defs = commandDefs();
		cmdRects.clear();
		int cx = kPad;
		for (size_t i = 0; i <= defs.size(); ++i)
		{
			if (i < defs.size() && defs[i].separatorBefore)
			{
				cx += kSepW;
			}
			if (i == defs.size())
			{
				cx += kSepW;
			}
			cmdRects.emplace_back(cx, kPad, kCmdW, kCmdH);
			cx += kCmdW + kGap;
		}
		const int cmdRowW = cx - kGap + kPad;

		// Cards: as many as fit in the width the command row needs (or the single-unit layout)
		cardRects.clear();
		infoRect = WzRect();
		overflowCard = SIZE_MAX;
		const int cardY = kPad + kCmdH + kGap + 2 + 1;
		const int maxCards = std::max(1, (cmdRowW - 2 * kPad + kGap) / (kCardW + kGap));
		if (summary.total == 1)
		{
			cardRects.emplace_back(kPad, cardY, kCardW, kCardH - 1);
			infoRect = WzRect(kPad + kCardW + 8, cardY, kInfoW, kCardH - 1);
		}
		else
		{
			const size_t n = summary.groups.size();
			const size_t shown = std::min(n, static_cast<size_t>(maxCards));
			for (size_t i = 0; i < shown; ++i)
			{
				cardRects.emplace_back(kPad + static_cast<int>(i) * (kCardW + kGap), cardY, kCardW, kCardH - 1);
			}
			if (n > shown)
			{
				overflowCard = shown - 1; // the last slot says "+N"
			}
		}
		cardHitBase = static_cast<int>(defs.size()) + 1;
		if (countText.size() < cardRects.size())
		{
			countText.resize(cardRects.size());
		}
		wantedW = cmdRowW;
	}

	int hitAt(int mx, int my) const
	{
		for (size_t i = 0; i < cmdRects.size(); ++i)
		{
			if (cmdRects[i].contains(mx, my))
			{
				return static_cast<int>(i);
			}
		}
		for (size_t i = 0; i < cardRects.size(); ++i)
		{
			if (cardRects[i].contains(mx, my))
			{
				return cardHitBase + static_cast<int>(i);
			}
		}
		return -1;
	}

	void drawModel(const DROID *psDroid, int bx, int by, int w, int h)
	{
		Vector3i rotation(-30, 45, 0);
		Vector3i position(0, 0, BUTTON_DEPTH);
		const UDWORD radius = std::max<UDWORD>(1, getComponentDroidRadius(psDroid));
		// Fill the card whatever the size: small cyborgs grow, big bodies and transporters shrink
		const int scale = std::max(DROID_BUT_SCALE / 2, std::min(DROID_BUT_SCALE * 3, DROID_BUT_SCALE * 2 * 48 / static_cast<int>(std::max<UDWORD>(radius, 16))));
		pie_SetGeometricOffset(bx + w / 2, by + h / 2 + 10);
		displayComponentButtonObject(psDroid, &rotation, &position, scale);
	}

	Summary summary;
	std::vector<WzRect> cmdRects;
	std::vector<WzRect> cardRects;
	WzRect infoRect;
	size_t overflowCard = SIZE_MAX;
	int cardHitBase = 0;
	int wantedW = 600;
	std::vector<WzCachedText> cmdLabel, cmdKey, countText;
	WzCachedText overflowText, infoName, infoLine1, infoLine2;
};

std::shared_ptr<UnitBar> theBar;

bool spectating()
{
	return selectedPlayer >= MAX_PLAYERS || (bMultiPlayer && selectedPlayer < NetPlay.players.size() && NetPlay.players[selectedPlayer].isSpectator);
}

} // anonymous namespace

bool unitBar2126ShowClassicOrders()
{
	return !intInterface2126Active() || showClassicOrders;
}

void unitBar2126Remove()
{
	if (theBar)
	{
		if (psWScreen)
		{
			widgDelete(psWScreen, IDHUD2126_UNITS);
		}
		theBar.reset();
	}
}

void unitBar2126Update()
{
	if (!intInterface2126Active() || psWScreen == nullptr || spectating())
	{
		unitBar2126Remove();
		return;
	}

	// The classic order panel: hidden unless asked for with "More"
	if (auto orderForm = widgGetFromID(psWScreen, IDORDER_FORM))
	{
		orderForm->show(showClassicOrders);
	}

	size_t selected = 0;
	for (const DROID *psDroid : gameWorld.objects.droids[selectedPlayer])
	{
		selected += psDroid->selected ? 1 : 0;
	}
	const bool panelFree = (intMode == INT_NORMAL || intMode == INT_ORDER);
	const bool wanted = selected > 0 && panelFree && getWidgetsStatus();

	if (!wanted)
	{
		if (theBar)
		{
			theBar->hide();
		}
		if (selected == 0)
		{
			showClassicOrders = false;
		}
		return;
	}

	if (!theBar || widgGetFromID(psWScreen, IDHUD2126_UNITS) == nullptr)
	{
		theBar = std::make_shared<UnitBar>();
		theBar->id = IDHUD2126_UNITS;
		psWScreen->psForm->attach(theBar);
		theBar->setCalcLayout([](WIDGET *psWidget) {
			// Just above the unit groups bar, starting where the object panels start
			const int w = std::min(psWidget->idealWidth(), std::max(200, static_cast<int>(screenWidth) - OBJ_BACKX - 220));
			psWidget->setGeometry(OBJ_BACKX, GROUP_BACKY - kBarH - 6, w, kBarH);
		});
	}
	if (!theBar->visible())
	{
		theBar->show();
		theBar->refresh();
		theBar->callCalcLayout();
	}
}
