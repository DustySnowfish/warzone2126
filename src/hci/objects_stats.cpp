/*
	This file is part of Warzone 2100.
	Copyright (C) 2021-2023  Warzone 2100 Project

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
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "lib/widget/button.h"
#include "lib/widget/bar.h"
#include "objects_stats.h"
#include "hud2126_style.h"
#include "../research.h"
#include "../structure.h"
#include "../droid.h"
#include "../stats.h"
#include "../template.h"
#include "../display3d.h"
#include "../qtscript.h"
#include "../warcam.h"
#include "../geometry.h"
#include "groups.h"

void BaseObjectsController::clearSelection()
{
	::clearSelection();
}

void BaseObjectsController::clearStructureSelection()
{
	StructureList* intStrList = interfaceStructList();
	if (intStrList)
	{
		for (auto structure : *intStrList)
		{
			structure->selected = false;
		}
	}
}

void BaseObjectsController::selectObject(BASE_OBJECT *object, bool jumpToHighlightedStatsObject)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(, object);
	object->selected = true;
	triggerEventSelected();
	jsDebugSelected(object);
	setHighlightedObject(object, jumpToHighlightedStatsObject);
	refresh();
}

void BaseObjectsController::prepareToClose()
{
	clearData();
}

void BaseObjectsController::jumpToObject(BASE_OBJECT *object)
{
	ASSERT_NOT_NULLPTR_OR_RETURN(, object);
	setPlayerPos(gameWorld.map, object->pos.x, object->pos.y);
	setWarCamActive(false);
}

void BaseObjectsController::updateHighlighted()
{
	if (objectsSize() == 0)
	{
		setHighlightedObject(nullptr, false);
		return;
	}

	auto findAnySelectedObject = [&] (BASE_OBJECT *object) {
		if (object->died == 0 && object->selected)
		{
			setHighlightedObject(object, false);
			return true;
		}

		return false;
	};

	if (findObject(findAnySelectedObject))
	{
		return;
	}

	if (auto highlighted = getHighlightedObject())
	{
		auto findHighlightedObject = [&] (BASE_OBJECT *object) {
			if (object->died == 0 && object == highlighted)
			{
				setHighlightedObject(object, false);
				return true;
			}

			return false;
		};

		if (findObject(findHighlightedObject))
		{
			return;
		}
	}

	setHighlightedObject(getObjectAt(0), false);
}

void BaseStatsController::displayStatsForm()
{
	if (widgGetFromID(psWScreen, IDSTAT_FORM) == nullptr)
	{
		auto newStatsForm = makeStatsForm();
		psWScreen->psForm->attach(newStatsForm);
		intMode = INT_STAT;
		setSecondaryWindowUp(true);
	}
}

// To be called from within widget click & run handlers / functions
void BaseStatsController::scheduleDisplayStatsForm(const std::shared_ptr<BaseStatsController>& controller)
{
	std::weak_ptr<BaseStatsController> psWeakController = controller;
	widgScheduleTask([psWeakController]() {
		if (auto psStrongController = psWeakController.lock())
		{
			psStrongController->displayStatsForm();
		}
	});
}

DynamicIntFancyButton::DynamicIntFancyButton()
{
	style |= WFORM_SECONDARY;
}

void DynamicIntFancyButton::updateLayout()
{
	updateHighlight();
	initDisplay();
}

void DynamicIntFancyButton::released(W_CONTEXT *context, WIDGET_KEY mouseButton)
{
	bool clickAndReleaseOnThisButton = ((state & WBUT_DOWN) != 0); // relies on W_CLICKFORM handling to properly set WBUT_DOWN

	IntFancyButton::released(context, mouseButton);

	if (!clickAndReleaseOnThisButton)
	{
		return; // do nothing
	}

	if (mouseButton == WKEY_PRIMARY)
	{
		clickPrimary();
	}
	else if (mouseButton == WKEY_SECONDARY)
	{
		clickSecondary(false);
	}
}

bool DynamicIntFancyButton::clickHeld(W_CONTEXT *psContext, WIDGET_KEY key)
{
	if (key == WKEY_PRIMARY)
	{
		clickSecondary(true);
		return true;
	}
	return false;
}

void StatsButton::addProgressBar()
{
	auto init = W_BARINIT();
	init.x = STAT_PROGBARX;
	init.y = STAT_PROGBARY;
	init.width = STAT_PROGBARWIDTH;
	init.height = STAT_PROGBARHEIGHT;
	init.sCol = WZCOL_ACTION_PROGRESS_BAR_MAJOR;
	init.sMinorCol = WZCOL_ACTION_PROGRESS_BAR_MINOR;
	init.pTip = _("Progress Bar");
	init.style = WBAR_TROUGH | WIDG_HIDDEN;
	init.iRange = GAME_TICKS_PER_SEC;
	attach(progressBar = std::make_shared<W_BARGRAPH>(&init));
	progressBar->setBackgroundColour(WZCOL_ACTION_PRODUCTION_RUN_BACKGROUND);
}

void ObjectButton::jump()
{
	auto object = getController().getObjectAt(objectIndex);
	ASSERT_NOT_NULLPTR_OR_RETURN(, object);

	if ((jumpPosition.x == 0 && jumpPosition.y == 0) || !objectOnScreen(object, 0))
	{
		jumpPosition = getPlayerPos();
		getController().jumpToObject(object);
	}
	else
	{
		setPlayerPos(gameWorld.map, jumpPosition.x, jumpPosition.y);
		setWarCamActive(false);
		jumpPosition = {0, 0};
	}
}

void StatsFormButton::addCostBar()
{
	W_BARINIT sBarInit;
	sBarInit.x = STAT_TIMEBARX;
	sBarInit.y = STAT_TIMEBARY;
	sBarInit.width = STAT_PROGBARWIDTH;
	sBarInit.height = STAT_PROGBARHEIGHT;
	sBarInit.sCol = WZCOL_ACTION_PROGRESS_BAR_MAJOR;
	sBarInit.sMinorCol = WZCOL_ACTION_PROGRESS_BAR_MINOR;

	sBarInit.iRange = GAME_TICKS_PER_SEC;
	attach(costBar = std::make_shared<W_BARGRAPH>(&sBarInit));
	costBar->setBackgroundColour(WZCOL_BLACK);
	costBar->setTransparentToMouse(true);
}

void DynamicIntFancyButton::updateHighlight()
{
	if (isHighlighted()) {
		state |= WBUT_CLICKLOCK;
	} else {
		state &= ~WBUT_CLICKLOCK;
	}
}

void ObjectsForm::display(int xOffset, int yOffset)
{
	if (!isClosing())
	{
		updateButtons();
		getController().updateHighlighted();
		if (previousHighlighted != getController().getHighlightedObject())
		{
			goToHighlightedTab();
		}
		previousHighlighted = getController().getHighlightedObject();
	}
	BaseWidget::display(xOffset, yOffset);
}

void ObjectsForm::goToHighlightedTab()
{
	auto highlighted = getController().getHighlightedObject();
	for (auto i = 0; i < getController().objectsSize(); i++)
	{
		if (highlighted == getController().getObjectAt(i))
		{
			objectsList->goToChildPage(i);
			return;
		}
	}
}

void ObjectsForm::initialize()
{
	// creating an obj stat form
	id = IDOBJ_FORM;
	setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
		psWidget->setGeometry(OBJ_BACKX, OBJ_BACKY - (getGroupButtonEnabled() ? 80 : 0), OBJ_BACKWIDTH, OBJ_BACKHEIGHT);
	}));

	addCloseButton();
	addTabList();
}

void ObjectsForm::addCloseButton()
{
	W_BUTINIT init;
	init.calcLayout = LAMBDA_CALCLAYOUT_SIMPLE({
		psWidget->setGeometry(OBJ_BACKWIDTH - CLOSE_WIDTH, 0, CLOSE_WIDTH, CLOSE_HEIGHT);
	});
	init.pTip = _("Close");
	init.pDisplay = intDisplayImageHilight;
	init.UserData = PACKDWORD_TRI(0, IMAGE_CLOSEHILIGHT , IMAGE_CLOSE);
	auto button = std::make_shared<W_BUTTON>(&init);
	attach(button);
	button->addOnClickHandler([](W_BUTTON&) {
		widgScheduleTask([]() {
			intResetScreen(false);
			intMode = INT_NORMAL;
		});
	});
}

void ObjectsForm::addTabList()
{
	attach(objectsList = IntListTabWidget::make());
	objectsList->id = IDOBJ_TABFORM;
	objectsList->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
		IntListTabWidget *pObjectsList = static_cast<IntListTabWidget *>(psWidget);
		assert(pObjectsList != nullptr);
		pObjectsList->setChildSize(OBJ_BUTWIDTH, OBJ_BUTHEIGHT * 2);
		pObjectsList->setChildSpacing(OBJ_GAP, OBJ_GAP);
		int objListWidth = OBJ_BUTWIDTH * 5 + STAT_GAP * 4;
		pObjectsList->setGeometry((OBJ_BACKWIDTH - objListWidth) / 2, OBJ_TABY, objListWidth, OBJ_BACKHEIGHT - OBJ_TABY);
	}));
}

void ObjectsForm::updateButtons()
{
	auto objectsCount = getController().objectsSize();
	while (buttonsCount < objectsCount)
	{
		addNewButton();
	}

	while (buttonsCount > objectsCount)
	{
		removeLastButton();
	}
}

void ObjectsForm::addNewButton()
{
	auto buttonIndex = buttonsCount++;
	auto buttonHolder = std::make_shared<WIDGET>();
	objectsList->addWidgetToLayout(buttonHolder);

	auto statButton = makeStatsButton(buttonIndex);
	buttonHolder->attach(statButton);
	statButton->setGeometry(0, 0, OBJ_BUTWIDTH, OBJ_BUTHEIGHT);
	statButton->style |= WFORM_SECONDARY;

	auto objectButton = makeObjectButton(buttonIndex);
	buttonHolder->attach(objectButton);
	objectButton->setGeometry(0, OBJ_STARTY, OBJ_BUTWIDTH, OBJ_BUTHEIGHT);
}

void ObjectsForm::removeLastButton()
{
	buttonsCount--;
	if (auto listWidget = objectsList->listWidget())
	{
		auto lastButton = listWidget->children().back();
		listWidget->detach(lastButton);
	}
}

void StatsForm::initialize()
{
	id = IDSTAT_FORM;
	use2126Layout = intInterface2126Active();
	if (use2126Layout)
	{
		setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			const int top = kHud2126StatusHeight + 8;
			psWidget->setGeometry(STAT_X, top, StatsForm::width2126(), std::max(200, static_cast<int>(RET_Y) - 6 - top));
		}));
	}
	else
	{
		setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			psWidget->setGeometry(STAT_X, STAT_Y, STAT_WIDTH, STAT_HEIGHT);
		}));
	}

	addCloseButton();
	addTabList();
	if (use2126Layout)
	{
		addHeader2126();
	}
}

void StatsForm::addCloseButton()
{
	W_BUTINIT init;
	init.x = (use2126Layout ? width2126() : STAT_WIDTH) - CLOSE_WIDTH;
	init.y = 0;
	init.width = CLOSE_WIDTH;
	init.height = CLOSE_HEIGHT;
	init.pTip = _("Close");
	init.pDisplay = intDisplayImageHilight;
	init.UserData = PACKDWORD_TRI(0, IMAGE_CLOSEHILIGHT , IMAGE_CLOSE);
	auto button = std::make_shared<W_BUTTON>(&init);
	attach(button);
	button->addOnClickHandler([](W_BUTTON&) {
		widgScheduleTask([]() {
			intRemoveStats();
			intMode = INT_OBJECT;
		});
	});
}

void StatsForm::addTabList()
{
	attach(optionList = IntListTabWidget::make());
	optionList->id = IDSTAT_TABFORM;
	optionList->setChildSize(STAT_BUTWIDTH, STAT_BUTHEIGHT);
	optionList->setChildSpacing(STAT_GAP, STAT_GAP);
	if (use2126Layout)
	{
		optionList->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
			auto parent = psWidget->parent();
			const int listWidth = STAT_BUTWIDTH * 3 + STAT_GAP * 2;
			const int parentH = parent ? parent->height() : STAT_HEIGHT;
			psWidget->setGeometry(6, StatsForm::k2126ListY, listWidth, std::max(STAT_BUTHEIGHT, parentH - StatsForm::k2126ListY - StatsForm::k2126FooterH - 4));
		}));
		return;
	}
	int statListWidth = STAT_BUTWIDTH * 2 + STAT_GAP;
	optionList->setGeometry((STAT_WIDTH - statListWidth) / 2, STAT_TABFORMY, statListWidth, STAT_HEIGHT - STAT_TABFORMY);
}

void StatsForm::displayFooter2126(int xOffset, int yOffset)
{
	// The hovered button (or the last one hovered) names the item, its cost and its time
	std::shared_ptr<StatsFormButton> hovered;
	std::function<void(const std::shared_ptr<WIDGET> &)> findHovered = [&](const std::shared_ptr<WIDGET> &w) {
		for (const auto &child : w->children())
		{
			if (hovered || !child->visible())
			{
				continue;
			}
			auto button = std::dynamic_pointer_cast<StatsFormButton>(child);
			if (button && button->isHovered2126())
			{
				hovered = button;
				return;
			}
			findHovered(child);
		}
	};
	findHovered(optionList);
	{
		{
			auto button = hovered;
			if (button)
			{
				BASE_STATS *stats = button->statsForInfo2126();
				const uint32_t cost = button->costForInfo2126();
				const int seconds = stats ? estimateSeconds2126(stats) : -1;
				if (stats != footerStats2126 || cost != footerCost2126 || seconds != footerSeconds2126)
				{
					footerStats2126 = stats;
					footerCost2126 = cost;
					footerSeconds2126 = seconds;
					footerName2126.setText(stats ? WzString::fromUtf8(getLocalizedStatsName(stats)) : WzString(), font_regular_bold);
					std::string line = astringf(_("Cost %u"), cost);
					if (seconds >= 0)
					{
						line += astringf(_("    Time %d:%02d"), seconds / 60, seconds % 60);
					}
					footerLine2126.setText(WzString::fromUtf8(line), font_regular);
				}
			}
		}
	}
	const int x0 = xOffset + x(), y1 = yOffset + y() + height();
	const int fy = y1 - k2126FooterH;
	pie_UniTransBoxFill(x0 + 6, fy, x0 + width() - 6, fy + 1, hud2126::border());
	if (footerStats2126 == nullptr)
	{
		if (footerName2126->width() == 0)
		{
			footerName2126.setText(_("Hover an item for details"), font_small);
		}
		footerName2126->render(x0 + 8, fy + 10 - footerName2126->aboveBase(), hud2126::label());
		return;
	}
	footerName2126->render(x0 + 8, fy + 8 - footerName2126->aboveBase(), hud2126::value());
	footerLine2126->render(x0 + 8, fy + 8 + footerName2126->lineSize() + 4 - footerLine2126->aboveBase(), hud2126::text());
}

void StatsForm::display(int xOffset, int yOffset)
{
	updateLayout();
	BaseWidget::display(xOffset, yOffset);
	if (use2126Layout && !isClosing())
	{
		displayFooter2126(xOffset, yOffset);
	}
}

int ObjectStatsForm::estimateSeconds2126(BASE_STATS *stats)
{
	BASE_OBJECT *object = getController().getHighlightedObject();
	if (stats == nullptr || object == nullptr)
	{
		return -1;
	}
	if (stats->hasType(STAT_RESEARCH) && object->type == OBJ_STRUCTURE)
	{
		const RESEARCH *research = static_cast<const RESEARCH *>(stats);
		const int rate = getBuildingResearchPoints(static_cast<const STRUCTURE *>(object));
		const int done = (research->index < asPlayerResList[selectedPlayer].size()) ? static_cast<int>(asPlayerResList[selectedPlayer][research->index].currentPoints) : 0;
		return rate > 0 ? std::max(0, (static_cast<int>(research->researchPoints) - done) / rate) : -1;
	}
	if (stats->hasType(STAT_STRUCTURE) && object->type == OBJ_DROID)
	{
		const DROID *droid = static_cast<const DROID *>(object);
		if (droid->asBits[COMP_CONSTRUCT] == 0 || droid->asBits[COMP_CONSTRUCT] >= asConstructStats.size())
		{
			return -1;
		}
		const int rate = constructorPoints(asConstructStats[droid->asBits[COMP_CONSTRUCT]], droid->player);
		return rate > 0 ? static_cast<int>(static_cast<const STRUCTURE_STATS *>(stats)->buildPoints) / rate : -1;
	}
	if (stats->hasType(STAT_TEMPLATE) && object->type == OBJ_STRUCTURE)
	{
		const int rate = getBuildingProductionPoints(static_cast<const STRUCTURE *>(object));
		return rate > 0 ? static_cast<int>(calcTemplateBuild(static_cast<const DROID_TEMPLATE *>(stats))) / rate : -1;
	}
	return -1;
}

void StatsForm::updateLayout()
{
	updateButtons();
}

void BaseObjectsStatsController::updateHighlightedObjectStats()
{
	auto highlightedObject = getHighlightedObject();
	auto size = objectsSize();

	for (auto i = 0; i < size; i++)
	{
		if (getObjectAt(i) == highlightedObject)
		{
			highlightedObjectStats = getObjectStatsAt(i);
			return;
		}
	}

	highlightedObjectStats = nullptr;
}

void ObjectStatsForm::updateLayout()
{
	BaseWidget::updateLayout();
	auto& controller = getController();
	controller.updateHighlightedObjectStats();
	auto highlighted = getController().getHighlightedObjectStats();
	if (highlighted != nullptr && controller.getQueuedJumpToHighlightedStatsObject())
	{
		goToHighlightedTab();
		controller.clearQueuedJumpToHighlightedStatsObject();
	}
	previousHighlighted = highlighted;
}

void ObjectStatsForm::goToHighlightedTab()
{
	auto highlighted = getController().getHighlightedObjectStats();
	for (auto i = 0; i < getController().statsSize(); i++)
	{
		if (highlighted == getController().getStatsAt(i))
		{
			optionList->goToChildPage(i);
			return;
		}
	}
}

void StatsForm::updateButtons()
{
	auto statsCount = getController().statsSize();
	while (buttonsCount < statsCount)
	{
		addNewButton();
	}

	while (buttonsCount > statsCount)
	{
		removeLastButton();
	}
}

void StatsForm::addNewButton()
{
	auto buttonIndex = buttonsCount++;
	auto button = makeOptionButton(buttonIndex);
	optionList->addWidgetToLayout(button);
	button->style |= WFORM_SECONDARY;
}

void StatsForm::removeLastButton()
{
	buttonsCount--;
	if (auto listWidget = optionList->listWidget())
	{
		auto lastButton = listWidget->children().back();
		listWidget->detach(lastButton);
	}
}

