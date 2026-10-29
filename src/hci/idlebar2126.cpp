/*
	This file is part of Warzone 2126, based on Warzone 2100.
	Copyright (C) 2026  Warzone 2126 contributors

	Warzone 2126 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2126 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2126; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
/// Warzone 2126: idle indicators beside the minimap (see idlebar2126.h).

#include "idlebar2126.h"
#include "hud2126_style.h"

#include "lib/framework/frame.h"
#include "lib/framework/wzapp.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/netplay/netplay.h"
#include "lib/widget/widget.h"
#include "lib/widget/form.h"
#include "lib/widget/paragraph.h"

#include "../display.h"
#include "../display3d.h"
#include "../droid.h"
#include "../game_world.h"
#include "../hci.h"
#include "../group.h"
#include "../loop.h"
#include "../map.h"
#include "../multiplay.h"
#include "../order.h"
#include "../qtscript.h"
#include "../radar.h"
#include "../selection.h"
#include "../structure.h"
#include "../warzoneconfig.h"

#include <array>
#include <string>
#include <vector>

namespace
{

constexpr int kButtonW = 112;
constexpr int kButtonH = 28;
constexpr int kGap = 4;

bool spectating()
{
	return selectedPlayer >= MAX_PLAYERS || (bMultiPlayer && selectedPlayer < NetPlay.players.size() && NetPlay.players[selectedPlayer].isSpectator);
}

bool isTruck(const DROID* d)
{
	return d->droidType == DROID_CONSTRUCT || d->droidType == DROID_CYBORG_CONSTRUCT;
}

/// Riding in a transporter or landing craft (not something to wake up)
bool transporterIsCarrying2126(const DROID* d)
{
	return d->psGroup != nullptr && d->psGroup->type == GT_TRANSPORTER;
}

bool idleTruck(const DROID* d)
{
	return !d->died && isTruck(d) && d->order.type == DORDER_NONE && d->action == DACTION_NONE && !d->isTransporter()
		&& !transporterIsCarrying2126(d);
}

bool idleFactory(const STRUCTURE* st)
{
	if (st->died || st->status != SS_BUILT || !st->isFactory() || st->pFunctionality == nullptr)
	{
		return false;
	}
	const FACTORY& f = st->pFunctionality->factory;
	return f.psSubject == nullptr && f.psSubjectPending == nullptr;
}

bool idleLab(const STRUCTURE* st)
{
	if (st->died || st->status != SS_BUILT || st->pStructureType->type != REF_RESEARCH || st->pFunctionality == nullptr)
	{
		return false;
	}
	const RESEARCH_FACILITY& r = st->pFunctionality->researchFacility;
	return r.psSubject == nullptr && r.psSubjectPending == nullptr;
}

std::vector<BASE_OBJECT*> idleObjects(IdleKind2126 kind)
{
	std::vector<BASE_OBJECT*> out;
	if (selectedPlayer >= MAX_PLAYERS)
	{
		return out;
	}
	if (kind == IdleKind2126::Trucks)
	{
		for (DROID* d : gameWorld.objects.droids[selectedPlayer])
		{
			if (idleTruck(d))
			{
				out.push_back(d);
			}
		}
		return out;
	}
	for (STRUCTURE* st : gameWorld.objects.structures[selectedPlayer])
	{
		if (kind == IdleKind2126::Factories ? idleFactory(st) : idleLab(st))
		{
			out.push_back(st);
		}
	}
	return out;
}

/// The id of the last object each kind jumped to, so repeated clicks go round them all
std::array<uint32_t, 3> lastPicked = { 0, 0, 0 };

class IdleBar : public WIDGET
{
public:
	std::array<int, 3> counts = { 0, 0, 0 };

	int32_t idealWidth() override { return kButtonW; }
	int32_t idealHeight() override { return 3 * kButtonH + 2 * kGap; }

	void display(int xOffset, int yOffset) override
	{
		static const char* kLabels[3] = { N_("Idle trucks"), N_("Idle factories"), N_("Idle labs") };
		const int x0 = xOffset + x();
		int y0 = yOffset + y();
		for (int i = 0; i < 3; ++i, y0 += kButtonH + kGap)
		{
			const bool any = counts[i] > 0;
			const bool hovered = hoverIndex == i;
			pie_UniTransBoxFill(x0, y0, x0 + kButtonW, y0 + kButtonH, hovered && any ? hud2126::tabHover() : hud2126::bg());
			iV_Box(x0, y0, x0 + kButtonW - 1, y0 + kButtonH - 1, any ? (i == 0 ? hud2126::warn() : hud2126::accent()) : hud2126::border());
			labelText[i].setText(WzString::fromUtf8(_(kLabels[i])), font_small);
			labelText[i]->render(x0 + 7, y0 + kButtonH / 2 + 4, any ? hud2126::text() : hud2126::disabled());
			countText[i].setText(WzString::number(counts[i]), font_regular_bold);
			countText[i]->render(x0 + kButtonW - 7 - countText[i]->width(), y0 + kButtonH / 2 + 5, any ? hud2126::value() : hud2126::disabled());
		}
	}

	void run(W_CONTEXT* psContext) override
	{
		const int mx = psContext->mx - x();
		const int my = psContext->my - y();
		hoverIndex = -1;
		if (mx >= 0 && mx < kButtonW && my >= 0)
		{
			const int i = my / (kButtonH + kGap);
			if (i < 3 && my - i * (kButtonH + kGap) < kButtonH)
			{
				hoverIndex = i;
			}
		}
	}

	void released(W_CONTEXT* psContext, WIDGET_KEY key) override
	{
		const int my = psContext->my - y();
		const int i = my / (kButtonH + kGap);
		if (i < 0 || i > 2)
		{
			return;
		}
		if (key == WKEY_SECONDARY && i == 0)
		{
			idleBar2126SelectAllTrucks();
		}
		else
		{
			idleBar2126SelectNext(static_cast<IdleKind2126>(i));
		}
	}

	void highlightLost() override
	{
		hoverIndex = -1;
	}

private:
	int hoverIndex = -1;
	std::array<WzCachedText, 3> labelText;
	std::array<WzCachedText, 3> countText;
};

std::shared_ptr<IdleBar> theBar;

} // anonymous namespace

void idleBar2126SelectNext(IdleKind2126 kind)
{
	if (spectating())
	{
		return;
	}
	std::vector<BASE_OBJECT*> idle = idleObjects(kind);
	if (idle.empty())
	{
		return;
	}
	uint32_t& last = lastPicked[static_cast<size_t>(kind)];
	size_t pick = 0;
	for (size_t i = 0; i < idle.size(); ++i)
	{
		if (idle[i]->id == last)
		{
			pick = (i + 1) % idle.size();
			break;
		}
	}
	BASE_OBJECT* obj = idle[pick];
	last = obj->id;
	if (kind == IdleKind2126::Trucks)
	{
		clearSelection();
		SelectDroid(static_cast<DROID*>(obj));
	}
	else
	{
		clearSelection();
		obj->selected = true;
		triggerEventSelected();
		intObjectSelected(obj); // open its production or research menu
	}
	setViewPos(map_coord(obj->pos.x), map_coord(obj->pos.y), true);
}

void idleBar2126SelectAllTrucks()
{
	if (spectating())
	{
		return;
	}
	std::vector<BASE_OBJECT*> idle = idleObjects(IdleKind2126::Trucks);
	if (idle.empty())
	{
		return;
	}
	clearSelection();
	for (BASE_OBJECT* obj : idle)
	{
		SelectDroid(static_cast<DROID*>(obj));
	}
}

void idleBar2126Remove()
{
	if (theBar)
	{
		if (psWScreen)
		{
			widgDelete(psWScreen, IDHUD2126_IDLE);
		}
		theBar.reset();
	}
}

void idleBar2126Update()
{
	if (psWScreen == nullptr || spectating() || !getWidgetsStatus() || !radarOnScreen || !radarPermitted || !war_getIdleIndicators())
	{
		if (theBar)
		{
			theBar->hide();
		}
		return;
	}
	if (!theBar || widgGetFromID(psWScreen, IDHUD2126_IDLE) == nullptr)
	{
		theBar = std::make_shared<IdleBar>();
		theBar->id = IDHUD2126_IDLE;
		psWScreen->psForm->attach(theBar);
		theBar->setCalcLayout([](WIDGET* psWidget) {
			// Left of the minimap, level with its bottom edge
			const int h = psWidget->idealHeight();
			psWidget->setGeometry(radarLeftX2126() - kButtonW - 8, radarBottomY2126() - h, kButtonW, h);
		});
	}
	// Counts refresh a few times a second (cheap, but no need every frame)
	static uint32_t nextRefresh = 0;
	if (realTime >= nextRefresh)
	{
		nextRefresh = realTime + 250;
		for (int i = 0; i < 3; ++i)
		{
			theBar->counts[i] = static_cast<int>(idleObjects(static_cast<IdleKind2126>(i)).size());
		}
	}
	if (!theBar->visible())
	{
		theBar->show();
		theBar->callCalcLayout();
	}
}
