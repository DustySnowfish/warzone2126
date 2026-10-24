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

#include "lib/framework/frame.h"
#include "lib/framework/input.h"
#include "lib/widget/button.h"
#include "lib/widget/label.h"
#include "lib/widget/bar.h"
#include "build.h"
#include "lib/widget/paragraph.h"
#include "hud2126_style.h"
#include "../display.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "../objmem.h"
#include "../order.h"
#include "../qtscript.h"
#include "../power.h"
#include "../map.h"
#include "../game_world.h"

DROID *BuildController::highlightedBuilder = nullptr;
bool BuildController::showFavorites = false;

void BuildController::updateData()
{
	updateBuildersList();
	updateHighlighted();
	updateBuildOptionsList();
}

void BuildController::updateBuildersList()
{
	builders.clear();

	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "selectedPlayer = %" PRIu32 "", selectedPlayer);

	for (DROID *droid : gameWorld.objects.droids[selectedPlayer])
	{
		if (droid->isConstructionDroid() && droid->died == 0)
		{
			builders.push_back(droid);
		}
	}

	std::reverse(builders.begin(), builders.end());
}

int BuildController::category2126 = 0;

/// Warzone 2126 build categories: 1 base, 2 defence, 3 support
static int buildCategoryOf2126(const STRUCTURE_STATS *stats)
{
	switch (stats->type)
	{
	case REF_HQ: case REF_FACTORY: case REF_FACTORY_MODULE: case REF_POWER_GEN: case REF_POWER_MODULE:
	case REF_RESOURCE_EXTRACTOR: case REF_RESEARCH: case REF_RESEARCH_MODULE: case REF_COMMAND_CONTROL:
	case REF_CYBORG_FACTORY: case REF_VTOL_FACTORY: case REF_LAB:
		return 1;
	case REF_DEFENSE: case REF_WALL: case REF_WALLCORNER: case REF_GATE: case REF_FORTRESS:
		return 2;
	default:
		return 3;
	}
}

void BuildController::updateBuildOptionsList()
{
	auto newBuildOptions = fillStructureList(gameWorld.objects, selectedPlayer, MAXSTRUCTURES - 1, shouldShowFavorites());

	stats.clear();
	for (STRUCTURE_STATS *option : newBuildOptions)
	{
		if (category2126 == 0 || !intInterface2126Active() || buildCategoryOf2126(option) == category2126)
		{
			stats.push_back(option);
		}
	}
}

STRUCTURE_STATS *BuildController::getObjectStatsAt(size_t objectIndex) const
{
	auto builder = getObjectAt(objectIndex);
	if (!builder)
	{
		return nullptr;
	}

	if (!(droidType(builder) == DROID_CONSTRUCT || droidType(builder) == DROID_CYBORG_CONSTRUCT))
	{
		return nullptr;
	}

	STRUCTURE_STATS *builderStats;
	if (orderStateStatsLoc(builder, DORDER_BUILD, &builderStats)) // Moving to build location?
	{
		return builderStats;
	}

	auto structure = DroidGetBuildStructure(builder);
	if (structure != nullptr) // Is building
	{
		return structure->pStructureType;
	}

	if (orderState(builder, DORDER_DEMOLISH))
	{
		return structGetDemolishStat();
	}

	return nullptr;
}


void BuildController::startBuildPosition(STRUCTURE_STATS *buildOption)
{
	auto builder = getHighlightedObject();
	ASSERT_NOT_NULLPTR_OR_RETURN(, builder);

	triggerEvent(TRIGGER_MENU_BUILD_SELECTED);

	if (buildOption == structGetDemolishStat())
	{
		objMode = IOBJ_DEMOLISHSEL;
	}
	else
	{
		objMode = IOBJ_BUILDSEL;
		intStartConstructionPosition(builder, buildOption);
	}

	intRemoveStats();
	intMode = INT_OBJECT;
}

void BuildController::toggleFavorites(STRUCTURE_STATS *buildOption)
{
	bool &isFavorite = asStructureStats[buildOption->index].isFavorite;
	if (shouldShowFavorites())
	{
		isFavorite = false;
	}
	else
	{
		isFavorite = !isFavorite;
	}
	updateBuildOptionsList();
}

void BuildController::refresh()
{
	updateData();

	if (objectsSize() == 0)
	{
		closeInterface();
	}
}

void BuildController::clearData()
{
	builders.clear();
	setHighlightedObject(nullptr, false);
	stats.clear();
}

void BuildController::toggleBuilderSelection(DROID *droid)
{
	if (droid->selected)
	{
		droid->selected = false;
	}
	else
	{
		if (auto highlightedObject = getHighlightedObject())
		{
			highlightedObject->selected = true;
		}
		selectObject(droid, false);
	}
	triggerEventSelected();
}

void BuildController::setHighlightedObject(BASE_OBJECT *object, bool jumpToHighlightedStatsObject)
{
	if (object == nullptr)
	{
		highlightedBuilder = nullptr;
		return;
	}

	auto builder = castDroid(object);
	ASSERT_NOT_NULLPTR_OR_RETURN(, builder);
	ASSERT_OR_RETURN(, builder->isConstructionDroid(), "Droid is not a construction droid");
	queuedJumpToHighlightedStatsObject = queuedJumpToHighlightedStatsObject || jumpToHighlightedStatsObject;
	highlightedBuilder = builder;
}

class BuildObjectButton : public ObjectButton
{
private:
	typedef ObjectButton BaseWidget;

public:
	BuildObjectButton(const std::shared_ptr<BuildController> &controller, size_t newObjectIndex)
		: controller(controller)
	{
		objectIndex = newObjectIndex;
	}

	void clickPrimary() override
	{
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);

		if (keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL) || keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT))
		{
			controller->toggleBuilderSelection(droid);
			return;
		}

		controller->clearSelection();
		controller->selectObject(controller->getObjectAt(objectIndex), false);
		jump();

		BaseStatsController::scheduleDisplayStatsForm(controller);
	}

protected:
	void display(int xOffset, int yOffset) override
	{
		updateLayout();
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);
		if (isDead(droid))
		{
			ASSERT_FAILURE(!isDead(droid), "!isDead(droid)", AT_MACRO, __FUNCTION__, "Droid is dead");
			// ensure the backing information is refreshed before the next draw
			intRefreshScreen();
			return;
		}
		displayIMD(AtlasImage(), ImdObject::Droid(droid), xOffset, yOffset);
		displayIfHighlight(xOffset, yOffset);
	}

	BuildController &getController() const override
	{
		return *controller.get();
	}

	std::string getTip() override
	{
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN("", droid);
		return droidGetName(droid);
	}

private:
	std::shared_ptr<BuildController> controller;
};

class BuildStatsButton: public StatsButton
{
private:
	typedef	StatsButton BaseWidget;

protected:
	BuildStatsButton() {}

public:
	static std::shared_ptr<BuildStatsButton> make(const std::shared_ptr<BuildController> &controller, size_t objectIndex)
	{
		class make_shared_enabler: public BuildStatsButton {};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->objectIndex = objectIndex;
		widget->initialize();
		return widget;
	}

protected:
	void display(int xOffset, int yOffset) override
	{
		updateLayout();
		auto stat = getStats();
		displayIMD(AtlasImage(), stat ? ImdObject::StructureStat(stat): ImdObject::Component(nullptr), xOffset, yOffset);
		displayIfHighlight(xOffset, yOffset);
	}

	void updateLayout() override
	{
		BaseWidget::updateLayout();
		auto droid = controller->getObjectAt(objectIndex);
		updateProgressBar(droid);
		updateProductionRunSizeLabel(droid);
	}

private:
	STRUCTURE_STATS *getStats() override
	{
		return controller->getObjectStatsAt(objectIndex);
	}

	void initialize()
	{
		addProgressBar();
		addProductionRunSizeLabel();
	}

	void addProductionRunSizeLabel()
	{
		W_LABINIT init;
		init.style = WIDG_HIDDEN;
		init.x = OBJ_TEXTX;
		init.y = OBJ_T1TEXTY;
		init.width = 16;
		init.height = 16;
		init.pText = WzString::fromUtf8("BUG! (a)");

		attach(productionRunSizeLabel = std::make_shared<W_LABEL>(&init));
		productionRunSizeLabel->setFontColour(WZCOL_ACTION_PRODUCTION_RUN_TEXT);
	}

	void updateProgressBar(DROID *droid)
	{
		progressBar->hide();

		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);
		ASSERT(droid->asBits[COMP_CONSTRUCT], "Invalid droid type");

		if (auto structure = DroidGetBuildStructure(droid))
		{
			//show progress of build
			if (structure->currentBuildPts != 0)
			{
				formatTime(progressBar.get(), structure->currentBuildPts, structureBuildPointsToCompletion(*structure), structure->lastBuildRate, _("Build Progress"));
			}
			else
			{
				formatPower(progressBar.get(), checkPowerRequest(structure), structure->pStructureType->powerToBuild);
			}
		}
	}

	void updateProductionRunSizeLabel(DROID *droid)
	{
		int remaining = -1;

		STRUCTURE_STATS const *stats = nullptr;
		int count = 0;
		auto processOrder = [&](DroidOrder const &order) {
			STRUCTURE_STATS *newStats = nullptr;
			int deltaCount = 0;
			switch (order.type)
			{
				case DORDER_BUILD:
				case DORDER_LINEBUILD:
					newStats = order.psStats;
					deltaCount = order.type == DORDER_LINEBUILD? 1 + (abs(order.pos.x - order.pos2.x) + abs(order.pos.y - order.pos2.y)) / TILE_UNITS : 1;
					break;
				case DORDER_HELPBUILD:
					if (STRUCTURE *target = castStructure(order.psObj))
					{
						newStats = target->pStructureType;
						deltaCount = 1;
					}
					break;
				default:
					return false;
			}
			if (newStats != nullptr && (stats == nullptr || stats == newStats))
			{
				stats = newStats;
				count += deltaCount;
				return true;
			}
			return false;
		};

		if (droid && processOrder(droid->order))
		{
			for (auto const &order: droid->asOrderList)
			{
				if (!processOrder(order))
				{
					break;
				}
			}
		}
		if (count > 1)
		{
			remaining = count;
		}

		if (remaining != -1)
		{
			productionRunSizeLabel->setString(WzString::format("%d", remaining));
			productionRunSizeLabel->show();
		}
		else
		{
			productionRunSizeLabel->hide();
		}
	}

	bool isHighlighted() const override
	{
		auto droid = controller->getObjectAt(objectIndex);
		return droid && (droid->selected || droid == controller->getHighlightedObject());
	}

	void clickPrimary() override
	{
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);

		if (keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL) || keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT))
		{
			controller->toggleBuilderSelection(droid);
		}
		else
		{
			controller->clearSelection();
			controller->selectObject(droid, true);
		}

		BaseStatsController::scheduleDisplayStatsForm(controller);
	}

	void clickSecondary(bool synthesizedFromHold) override
	{
		auto droid = controller->getObjectAt(objectIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, droid);
		auto highlighted = controller->getHighlightedObject();

		// prevent highlighting a builder when another builder is already selected
		if (droid == highlighted || !highlighted->selected)
		{
			controller->setHighlightedObject(droid, true);
			BaseStatsController::scheduleDisplayStatsForm(controller);
		}
	}

	std::shared_ptr<W_LABEL> productionRunSizeLabel;
	std::shared_ptr<BuildController> controller;
	size_t objectIndex;
};

class BuildOptionButton: public StatsFormButton
{
private:
	typedef StatsFormButton BaseWidget;
	using BaseWidget::BaseWidget;

public:
	static std::shared_ptr<BuildOptionButton> make(const std::shared_ptr<BuildController> &controller, size_t buildOptionIndex)
	{
		class make_shared_enabler: public BuildOptionButton {};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->buildOptionIndex = buildOptionIndex;
		widget->initialize();
		return widget;
	}

protected:
	void display(int xOffset, int yOffset) override
	{
		updateLayout();
		auto stat = getStats();
		ASSERT_NOT_NULLPTR_OR_RETURN(, stat);

		displayIMD(AtlasImage(), ImdObject::StructureStat(stat), xOffset, yOffset);
		displayIfHighlight(xOffset, yOffset);
		if (asStructureStats[stat->index].isFavorite && !controller->shouldShowFavorites())
		{
			iV_DrawImage(IntImages, IMAGE_ALLY_RESEARCH_TC, xOffset + x() + 44, yOffset + y() + 3);
		}
	}

private:
	STRUCTURE_STATS *getStats() override
	{
		return controller->getStatsAt(buildOptionIndex);
	}

	void initialize()
	{
		addCostBar();
	}

	bool isHighlighted() const override
	{
		return controller->isHighlightedObjectStats(buildOptionIndex);
	}

	void updateLayout() override
	{
		BaseWidget::updateLayout();

		if (isMouseOverWidget())
		{
			intSetShadowPower(getCost());
		}

		costBar->majorSize = std::min(100, (int32_t)(getCost() / POWERPOINTS_DROIDDIV));
	}

	uint32_t getCost() override
	{
		STRUCTURE_STATS * psStats = getStats();
		return psStats ? psStats->powerToBuild : 0;
	}

	void clickPrimary() override
	{
		auto clickedStats = controller->getStatsAt(buildOptionIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, clickedStats);

		auto controllerRef = controller;
		widgScheduleTask([clickedStats, controllerRef]() {
			controllerRef->startBuildPosition(clickedStats);
		});
	}

	void clickSecondary(bool synthesizedFromHold) override
	{
		auto clickedStats = controller->getStatsAt(buildOptionIndex);
		ASSERT_NOT_NULLPTR_OR_RETURN(, clickedStats);

		auto controllerRef = controller;
		widgScheduleTask([clickedStats, controllerRef]() {
			controllerRef->toggleFavorites(clickedStats);
		});
	}

	std::shared_ptr<BuildController> controller;
	size_t buildOptionIndex;
};

class BuildObjectsForm: public ObjectsForm
{
private:
	typedef ObjectsForm BaseWidget;
	using BaseWidget::BaseWidget;

public:
	static std::shared_ptr<BuildObjectsForm> make(const std::shared_ptr<BuildController> &controller)
	{
		class make_shared_enabler: public BuildObjectsForm {};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->initialize();
		return widget;
	}

	std::shared_ptr<StatsButton> makeStatsButton(size_t buttonIndex) const override
	{
		return BuildStatsButton::make(controller, buttonIndex);
	}

	std::shared_ptr<ObjectButton> makeObjectButton(size_t buttonIndex) const override
	{
		return std::make_shared<BuildObjectButton>(controller, buttonIndex);
	}

protected:
	BuildController &getController() const override
	{
		return *controller.get();
	}

private:
	std::shared_ptr<BuildController> controller;
};

/// Warzone 2126: build menu button that starts earthwork placement (raise or lower ground)
class EarthworkButton: public W_BUTTON
{
public:
	explicit EarthworkButton(int mode): mode(mode) {}

	void display(int xOffset, int yOffset) override
	{
		const int x0 = xOffset + x();
		const int y0 = yOffset + y();
		const int w = width();
		const int h = height();
		const bool highlight = (getState() & WBUT_HIGHLIGHT) != 0;
		pie_BoxFill(x0, y0, x0 + w, y0 + h, highlight ? pal_RGBA(70, 70, 110, 220) : pal_RGBA(30, 30, 60, 200));
		iV_Box(x0, y0, x0 + w - 1, y0 + h - 1, highlight ? WZCOL_WHITE : pal_RGBA(120, 120, 170, 255));
		const PIELIGHT ground = pal_RGBA(200, 150, 90, 255);
		if (mode == 3)
		{
			// Slope: low ground, a ramp, high ground, with an arrow up the ramp
			const int lowY = y0 + h * 3 / 4, highY = y0 + h / 3;
			const int x1 = x0 + w / 4, x2 = x0 + w * 3 / 4;
			iV_Lines({ glm::ivec4(x0 + 3, lowY, x1, lowY), glm::ivec4(x1, lowY, x2, highY), glm::ivec4(x2, highY, x0 + w - 4, highY) }, ground);
			const PIELIGHT violet = pal_RGBA(220, 130, 255, 255);
			const int ax = (x1 + x2) / 2 - 2, ay = (lowY + highY) / 2 - 4;
			iV_Lines({ glm::ivec4(ax - 4, ay + 4, ax + 3, ay - 3), glm::ivec4(ax + 3, ay - 3, ax - 1, ay - 3), glm::ivec4(ax + 3, ay - 3, ax + 3, ay + 1) }, violet);
			return;
		}
		if (mode == 2)
		{
			// Level: a flat line with arrows pressing down and up onto it
			const PIELIGHT green = pal_RGBA(120, 230, 120, 255);
			const int lineY = y0 + h / 2 + 1;
			iV_Lines({ glm::ivec4(x0 + 3, lineY, x0 + w - 4, lineY) }, ground);
			const int ax = x0 + w / 2;
			iV_Lines({ glm::ivec4(ax, y0 + 3, ax, lineY - 3), glm::ivec4(ax, lineY - 3, ax - 3, lineY - 6), glm::ivec4(ax, lineY - 3, ax + 3, lineY - 6),
			           glm::ivec4(ax, y0 + h - 3, ax, lineY + 3), glm::ivec4(ax, lineY + 3, ax - 3, lineY + 6), glm::ivec4(ax, lineY + 3, ax + 3, lineY + 6) }, green);
			return;
		}
		const bool lower = mode == 1;
		const PIELIGHT arrow = lower ? pal_RGBA(110, 180, 255, 255) : pal_RGBA(255, 190, 70, 255);
		const int baseY = y0 + h * 3 / 4;
		const int left = x0 + 3;
		const int right = x0 + w - 4;
		const int midX = x0 + w / 2;
		const int bump = lower ? h / 5 : -h / 5;
		// Ground line with a mound (raise) or a dip (lower) in the middle
		iV_Lines({ glm::ivec4(left, baseY, midX - w / 4, baseY), glm::ivec4(midX - w / 4, baseY, midX, baseY + bump),
		           glm::ivec4(midX, baseY + bump, midX + w / 4, baseY), glm::ivec4(midX + w / 4, baseY, right, baseY) }, ground);
		// Arrow
		const int tipY = lower ? y0 + h / 2 : y0 + 3;
		const int tailY = lower ? y0 + 3 : y0 + h / 2;
		const int head = lower ? -3 : 3;
		iV_Lines({ glm::ivec4(midX, tailY, midX, tipY), glm::ivec4(midX, tipY, midX - 3, tipY + head), glm::ivec4(midX, tipY, midX + 3, tipY + head) }, arrow);
	}

private:
	int mode; // terrain_deform::EarthworkMode
};

class BuildStatsForm: public ObjectStatsForm
{
private:
	typedef ObjectStatsForm BaseWidget;
	using BaseWidget::BaseWidget;

public:
	static std::shared_ptr<BuildStatsForm> make(const std::shared_ptr<BuildController> &controller)
	{
		class make_shared_enabler: public BuildStatsForm {};
		auto widget = std::make_shared<make_shared_enabler>();
		widget->controller = controller;
		widget->initialize();
		return widget;
	}

	std::shared_ptr<StatsFormButton> makeOptionButton(size_t buttonIndex) const override
	{
		return BuildOptionButton::make(controller, buttonIndex);
	}

protected:
	BuildController &getController() const override
	{
		return *controller.get();
	}

private:
	void initialize() override
	{
		BaseWidget::initialize();
		addObsoleteButton();
		addFavoriteButton();
		addEarthworkButtons();
	}

	void addEarthworkButtons()
	{
		const int w = AtlasImage(IntImages, IMAGE_FDP_UP).width();
		const int h = AtlasImage(IntImages, IMAGE_FDP_UP).height();
		for (int i = 0; i < 4; ++i)
		{
			auto button = std::make_shared<EarthworkButton>(i);
			attach(button);
			// After the obsolete and favourite toggles (each one image wide), narrower so all four fit the panel
			button->setGeometry(4 * 3 + w * 3 + 6 + i * (w - 7), STAT_SLDY, w - 9, h);
			button->setTip(i == 3 ? _("Slope Ground: drag from low ground to high ground (or back) for the selected trucks to cut a ramp, at least 3 tiles wide ( ' )")
			               : i == 1 ? _("Lower Ground: click a tile or drag an area for the selected trucks to dig ( ] )")
			               : i == 2 ? _("Level Ground: click a tile or drag an area for the selected trucks to flatten ( \\ )")
			               : _("Raise Ground: click a tile or drag an area for the selected trucks to build up ( [ )"));
			button->addOnClickHandler([i](W_BUTTON &) {
				startEarthworkPlacement(i);
			});
		}
	}

	void addObsoleteButton()
	{
		attach(obsoleteButton = std::make_shared<MultipleChoiceButton>());
		obsoleteButton->style |= WBUT_SECONDARY;
		obsoleteButton->setChoice(controller->shouldShowRedundantDesign());
		obsoleteButton->setImages(false, MultipleChoiceButton::Images(AtlasImage(IntImages, IMAGE_OBSOLETE_HIDE_UP), AtlasImage(IntImages, IMAGE_OBSOLETE_HIDE_UP), AtlasImage(IntImages, IMAGE_OBSOLETE_HIDE_HI)));
		obsoleteButton->setTip(false, _("Hiding Obsolete Tech"));
		obsoleteButton->setImages(true,  MultipleChoiceButton::Images(AtlasImage(IntImages, IMAGE_OBSOLETE_SHOW_UP), AtlasImage(IntImages, IMAGE_OBSOLETE_SHOW_UP), AtlasImage(IntImages, IMAGE_OBSOLETE_SHOW_HI)));
		obsoleteButton->setTip(true, _("Showing Obsolete Tech"));
		obsoleteButton->move(4 + AtlasImage(IntImages, IMAGE_FDP_UP).width() + 4, STAT_SLDY);

		auto weakController = std::weak_ptr<BuildController>(controller);
		obsoleteButton->addOnClickHandler([weakController](W_BUTTON &button) {
			if (auto buildController = weakController.lock())
			{
				auto &_obsoleteButton = static_cast<MultipleChoiceButton &>(button);
				auto newValue = !_obsoleteButton.getChoice();
				buildController->setShouldShowRedundantDesign(newValue);
				_obsoleteButton.setChoice(newValue);
			}
		});
	}

	void addFavoriteButton()
	{
		attach(favoriteButton = std::make_shared<MultipleChoiceButton>());
		favoriteButton->style |= WBUT_SECONDARY;
		favoriteButton->setChoice(controller->shouldShowFavorites());
		favoriteButton->setImages(false, MultipleChoiceButton::Images(AtlasImage(IntImages, IMAGE_ALLY_RESEARCH), AtlasImage(IntImages, IMAGE_ALLY_RESEARCH), AtlasImage(IntImages, IMAGE_ALLY_RESEARCH)));
		favoriteButton->setTip(false, _("Showing All Tech\nRight-click to add to Favorites"));
		favoriteButton->setImages(true,  MultipleChoiceButton::Images(AtlasImage(IntImages, IMAGE_ALLY_RESEARCH_TC), AtlasImage(IntImages, IMAGE_ALLY_RESEARCH_TC), AtlasImage(IntImages, IMAGE_ALLY_RESEARCH_TC)));
		favoriteButton->setTip(true, _("Showing Only Favorite Tech\nRight-click to remove from Favorites"));
		favoriteButton->move(4 * 2 + AtlasImage(IntImages, IMAGE_FDP_UP).width() * 2 + 4 * 2, STAT_SLDY);

		auto weakController = std::weak_ptr<BuildController>(controller);
		favoriteButton->addOnClickHandler([weakController](W_BUTTON &button) {
			if (auto buildController = weakController.lock())
			{
				auto &_favoriteButton = static_cast<MultipleChoiceButton &>(button);
				auto newValue = !_favoriteButton.getChoice();
				buildController->setShouldShowFavorite(newValue);
				_favoriteButton.setChoice(newValue);
			}
		});
	}

	// Warzone 2126 panel: category tabs under the header
	class CategoryTab2126 : public W_BUTTON
	{
	public:
		CategoryTab2126(int category, const char *label) : category(category)
		{
			text.setText(WzString::fromUtf8(label), font_small);
		}
		void display(int xOffset, int yOffset) override
		{
			const int x0 = xOffset + x(), y0 = yOffset + y(), x1 = x0 + width(), y1 = y0 + height();
			const bool active = BuildController::category2126 == category;
			const bool hot = (getState() & WBUT_HIGHLIGHT) != 0;
			pie_UniTransBoxFill(x0, y0, x1, y1, active ? hud2126::tabActive() : hot ? hud2126::tabHover() : hud2126::tab());
			iV_Box(x0, y0, x1 - 1, y1 - 1, active ? hud2126::accent() : hud2126::border());
			text->render(x0 + (width() - text->width()) / 2, y0 + (height() - text->lineSize()) / 2 - text->aboveBase(), active || hot ? hud2126::value() : hud2126::text());
		}
		const int category;
	private:
		WzCachedText text;
	};

	void addHeader2126() override
	{
		const char *labels[] = { N_("All"), N_("Base"), N_("Defence"), N_("Support") };
		const char *tips[] = {
			N_("Everything the selected trucks can build"),
			N_("HQ, factories, research, power, derricks and modules"),
			N_("Defences, hardpoints, walls and gates"),
			N_("Repair, rearm pads and everything else"),
		};
		const int n = 4;
		const int gap = 2;
		const int tabW = (width2126() - 12 - gap * (n - 1)) / n;
		auto weakController = std::weak_ptr<BuildController>(controller);
		for (int i = 0; i < n; ++i)
		{
			auto tab = std::make_shared<CategoryTab2126>(i, gettext(labels[i]));
			attach(tab);
			tab->setGeometry(6 + i * (tabW + gap), k2126HeaderRowY, tabW, k2126HeaderRowH);
			tab->setTip(gettext(tips[i]));
			tab->addOnClickHandler([weakController, i](W_BUTTON &) {
				if (auto buildController = weakController.lock())
				{
					buildController->setCategory2126(i);
				}
			});
		}
	}

	std::shared_ptr<BuildController> controller;
	std::shared_ptr<MultipleChoiceButton> obsoleteButton;
	std::shared_ptr<MultipleChoiceButton> favoriteButton;
};

bool BuildController::showInterface()
{
	updateData();
	if (builders.empty())
	{
		return false;
	}

	auto objectsForm = BuildObjectsForm::make(shared_from_this());
	psWScreen->psForm->attach(objectsForm);
	displayStatsForm();
	triggerEvent(TRIGGER_MENU_BUILD_UP);
	return true;
}

std::shared_ptr<StatsForm> BuildController::makeStatsForm()
{
	return BuildStatsForm::make(shared_from_this());
}
