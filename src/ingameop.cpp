/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

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
/**
 * @file ingameop.c
 * Ingame options screen.
 * Pumpkin Studios. 98
 */

#include "lib/framework/frame.h"
#include "lib/framework/wzapp.h"
#include "lib/gamelib/gtime.h"
#include "lib/widget/widget.h"
#include "lib/widget/label.h"
#include "lib/netplay/netplay.h"
#include "lib/sound/audio.h"					// for sound.
#include "lib/sound/mixer.h"

#include "display3d.h"
#include "intdisplay.h"
#include "hci.h"			// for wFont def.& intmode.
#include "loop.h"
#include "lib/framework/cursors.h"
#include "frontend.h"		// for textdisplay function
#include "loadsave.h"		// for textdisplay function
#include "console.h"		// to add console message
#include "keybind.h"
#include "multiplay.h"
#include "musicmanager.h"
#include "ingameop.h"
#include "mission.h"
#include "notifications.h"
#include "wrappers.h"
#include "warzoneconfig.h"
#include "qtscript.h"		// for bInTutorial
#include "radar.h"
#include "seqdisp.h"
#include "campaigninfo.h"
#include "hci/groups.h"
#include "screens/netpregamescreen.h"
#include "screens/guidescreen.h"
#include "screens/ingameopscreen.h"
#include "screens/spectatorgameoverscreen.h"
#include "titleui/options/optionsforms.h"
#include "hci/hud2126_style.h"
#include "display.h"
#include "input/manager.h"
#include "input/debugmappings.h"
#include "activity.h"
#include "levels.h"
#include "difficulty.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/screen.h"

bool hostQuitConfirmation = true;

bool	InGameOpUp		= false;
static bool inGameHostQuitHandled = false;
static bool		isInGameConfirmQuitUp = false;

// ////////////////////////////////////////////////////////////////////////////
// functions

// ////////////////////////////////////////////////////////////////////////////
// Warzone 2126 look: with the 2126 interface the menu is a wider flat panel with a "Paused" header
// (game mode, map, difficulty, cheats), full-width flat buttons, and the scene dimmed around it.

static bool igo2126()
{
	return intInterface2126Active();
}

static int igLineH() { return igo2126() ? 40 : INTINGAMEOPLINE_H; }
static int igButtonH() { return igo2126() ? 34 : INTINGAMEOP_OP_H; }
static int igMarginH() { return igo2126() ? 14 : INTINGAMEOPMARGIN_H; }
static int igHeaderH() { return igo2126() ? 78 : 0; }
static int igFormW() { return igo2126() ? 320 : INTINGAMEOP_W; }
static int igButtonX() { return igo2126() ? 16 : INTINGAMEOP_1_X; }
static int igButtonW() { return igo2126() ? igFormW() - 32 : INTINGAMEOP_OP_W; }
static int igLineY(int line) { return igHeaderH() + (line - 1) * igLineH() + igMarginH(); }
static int igFormH(int lines) { return igHeaderH() + lines * igLineH() + igMarginH() * 2 - (igo2126() ? igLineH() - igButtonH() : 0); }
static int igFormX(int w) { return (pie_GetVideoBufferWidth() - w) / 2; }
static int igFormY(int h) { return igo2126() ? std::max(20, (pie_GetVideoBufferHeight() - h) / 2 - 20) : INTINGAMEOPAUTO_Y(0) - h / 2 + INTINGAMEOPAUTO_H(0) / 2; }

struct IGButtonCache2126
{
	WzText text;
};

static void displayIGButton2126(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	W_BUTTON *psBut = static_cast<W_BUTTON *>(psWidget);
	auto &cache = *static_cast<IGButtonCache2126 *>(psWidget->pUserData);
	const int x0 = xOffset + psWidget->x(), y0 = yOffset + psWidget->y();
	const int x1 = x0 + psWidget->width(), y1 = y0 + psWidget->height();
	const bool hot = (psBut->getState() & WBUT_HIGHLIGHT) != 0;
	const bool down = (psBut->getState() & WBUT_DOWN) != 0;
	const bool quit = psWidget->id == INTINGAMEOP_QUIT || psWidget->id == INTINGAMEOP_CONFIRM_QUIT || psWidget->id == INTINGAMEOP_HOSTQUIT;
	pie_UniTransBoxFill(x0, y0, x1, y1, down ? hud2126::tabActive() : hot ? hud2126::tabHover() : hud2126::tab());
	iV_Box(x0, y0, x1 - 1, y1 - 1, hot ? (quit ? hud2126::bad() : hud2126::accent()) : hud2126::border());
	if (hot)
	{
		pie_UniTransBoxFill(x0, y0, x0 + 3, y1, quit ? hud2126::bad() : hud2126::accent());
	}
	cache.text.setText(psBut->pText, font_regular_bold);
	const PIELIGHT colour = hot ? (quit ? hud2126::bad() : hud2126::value()) : hud2126::text();
	cache.text.render(x0 + (psWidget->width() - cache.text.width()) / 2, y0 + (psWidget->height() - cache.text.lineSize()) / 2 - cache.text.aboveBase(), colour);
}

/// Dims the scene around a panel; drawn by a child widget at the panel's top left, so the offsets
/// passed in are the panel's position (the panel itself is drawn already)
static void dimAroundPanel2126(WIDGET *child, int xOffset, int yOffset)
{
	WIDGET *panel = child->parent().get();
	if (panel == nullptr)
	{
		return;
	}
	const int fx0 = xOffset, fy0 = yOffset, fx1 = xOffset + panel->width(), fy1 = yOffset + panel->height();
	const int W = static_cast<int>(pie_GetVideoBufferWidth()), H = static_cast<int>(pie_GetVideoBufferHeight());
	const PIELIGHT dim = pal_RGBA(0, 0, 0, 110);
	pie_UniTransBoxFill(0, 0, W, fy0, dim);
	pie_UniTransBoxFill(0, fy1, W, H, dim);
	pie_UniTransBoxFill(0, fy0, fx0, fy1, dim);
	pie_UniTransBoxFill(fx1, fy0, W, fy1, dim);
}

class IGDim2126 : public WIDGET
{
public:
	void display(int xOffset, int yOffset) override { dimAroundPanel2126(this, xOffset, yOffset); }
};

/// The header of the 2126 menu; it also dims the scene around the panel
class IGHeader2126 : public WIDGET
{
public:
	IGHeader2126()
	{
		title.setText(_("Paused"), font_large);
		std::string sub;
		switch (ActivityManager::instance().getCurrentGameMode())
		{
		case ActivitySink::GameMode::CAMPAIGN: sub = _("Campaign"); break;
		case ActivitySink::GameMode::CHALLENGE: sub = _("Challenge"); break;
		case ActivitySink::GameMode::SKIRMISH: sub = _("Skirmish"); break;
		case ActivitySink::GameMode::MULTIPLAYER: sub = _("Multiplayer"); break;
		default: break;
		}
		const char *level = getLevelName();
		if (level != nullptr && level[0] != '\0')
		{
			sub += sub.empty() ? std::string(_(level)) : std::string("  ·  ") + _(level);
		}
		if (ActivityManager::instance().getCurrentGameMode() == ActivitySink::GameMode::CAMPAIGN)
		{
			const char *names[] = {N_("Super Easy"), N_("Easy"), N_("Normal"), N_("Hard"), N_("Insane")};
			const int lev = static_cast<int>(getDifficultyLevel());
			if (lev >= 0 && lev < 5)
			{
				sub += std::string("  ·  ") + gettext(names[lev]);
			}
		}
		subtitle.setText(WzString::fromUtf8(sub), font_regular);
		cheats = gInputManager.debugManager().debugMappingsAllowed();
		if (cheats)
		{
			cheatsText.setText(_("Cheats enabled"), font_small);
		}
	}

	void display(int xOffset, int yOffset) override
	{
		dimAroundPanel2126(this, xOffset, yOffset);
		const int x0 = xOffset + x(), y0 = yOffset + y();
		title.render(x0 + 18, y0 + 16 - title.aboveBase(), hud2126::value());
		subtitle.render(x0 + 18, y0 + 16 + title.lineSize() + 2 - subtitle.aboveBase(), hud2126::label());
		if (cheats)
		{
			cheatsText.render(x0 + width() - 18 - cheatsText.width(), y0 + 22 - cheatsText.aboveBase(), hud2126::warn());
		}
		pie_UniTransBoxFill(x0 + 16, y0 + height() - 1, x0 + width() - 16, y0 + height(), hud2126::border());
	}

private:
	WzText title, subtitle, cheatsText;
	bool cheats = false;
};

static void addIGHeader2126(const std::shared_ptr<W_FORM> &form)
{
	if (!igo2126())
	{
		return;
	}
	auto header = std::make_shared<IGHeader2126>();
	form->attach(header);
	header->setGeometry(0, 0, igFormW(), igHeaderH());
}


// ////////////////////////////////////////////////////////////////////////////

static bool addIGTextButton(UDWORD id, UWORD x, UWORD y, UWORD width, const char *string, UDWORD Style)
{
	W_BUTINIT sButInit;

	//resume
	sButInit.formID		= INTINGAMEOP;
	sButInit.id			= id;
	sButInit.style		= Style;


	sButInit.x			= x;
	sButInit.y			= y;
	sButInit.width		= width;
	sButInit.height		= igButtonH();

	sButInit.pText		= string;
	if (igo2126())
	{
		sButInit.pDisplay	= displayIGButton2126;
		sButInit.pUserData = new IGButtonCache2126();
		sButInit.onDelete = [](WIDGET *psWidget) {
			delete static_cast<IGButtonCache2126 *>(psWidget->pUserData);
			psWidget->pUserData = nullptr;
		};
	}
	else
	{
		sButInit.pDisplay	= displayTextOption;
		sButInit.pUserData = new DisplayTextOptionCache();
		sButInit.onDelete = [](WIDGET *psWidget) {
			assert(psWidget->pUserData != nullptr);
			delete static_cast<DisplayTextOptionCache *>(psWidget->pUserData);
			psWidget->pUserData = nullptr;
		};
	}
	widgAddButton(psWScreen, &sButInit);

	return true;
}

static bool addHostQuitOptions()
{
	// get rid of the old stuff.
	widgDelete(psWScreen, INTINGAMEPOPUP);
	widgDelete(psWScreen, INTINGAMEOP);

	auto const &parent = psWScreen->psForm;

	// add form
	auto inGameOp = std::make_shared<IntFormAnimated>();
	parent->attach(inGameOp);
	inGameOp->id = INTINGAMEOP;
	inGameOp->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
		psWidget->setGeometry(INTINGAMEOP3_X, INTINGAMEOP3_Y, INTINGAMEOP3_W, INTINGAMEOP3_H);
	}));

	addIGTextButton(INTINGAMEOP_RESUME, INTINGAMEOP_1_X, INTINGAMEOP_1_Y, INTINGAMEOP_OP_W, _("Resume Game"), OPALIGN);

	auto inGamePopup = std::make_shared<IntFormAnimated>();
	parent->attach(inGamePopup);
	inGamePopup->id = INTINGAMEPOPUP;
	inGamePopup->setCalcLayout(LAMBDA_CALCLAYOUT_SIMPLE({
		assert(psWScreen != nullptr);
		WIDGET * inGameOp = widgGetFromID(psWScreen, INTINGAMEOP);
		assert(inGameOp != nullptr);
		psWidget->setGeometry((pie_GetVideoBufferWidth() - 600)/2, inGameOp->y() - 26 - 20, 600, 26);
	}));

	auto label = std::make_shared<W_LABEL>();
	inGamePopup->attach(label);
	label->setGeometry(0, 0, inGamePopup->width(), inGamePopup->height());
	label->setString(_("WARNING: You're the host. If you quit, the game ends for everyone!"));
	label->setTextAlignment(WLAB_ALIGNCENTRE);
	label->setFont(font_medium, WZCOL_YELLOW);

	addIGTextButton(INTINGAMEOP_QUIT, INTINGAMEOP_1_X, INTINGAMEOP_2_Y, INTINGAMEOP_OP_W, _("Host Quit"), OPALIGN);

	return true;
}

// ////////////////////////////////////////////////////////////////////////////

static bool _intAddInGameOptions()
{
	audio_StopAll();

	//clear out any mission widgets - timers etc that may be on the screen
	clearMissionWidgets();

	setWidgetsStatus(true);

	//if already open, then close!
	if (widgGetFromID(psWScreen, INTINGAMEOP))
	{
		intCloseInGameOptions(false, true);
		return true;
	}

	setReticulesEnabled(false);

	// hide the spectator game over screen (if it's up) while the menu is open, so it doesn't block the menu
	setSpectatorGameOverScreenVisible(false);

	// Pause the game.
	if (!gamePaused())
	{
		kf_TogglePauseMode();
	}

	auto const &parent = psWScreen->psForm;

	// add form
	auto ingameOp = std::make_shared<IntFormAnimated>();
	parent->attach(ingameOp);
	ingameOp->id = INTINGAMEOP;
	addIGHeader2126(ingameOp);

	int row = 1;
	// add 'resume'
	addIGTextButton(INTINGAMEOP_RESUME, igButtonX(), igLineY(row), igButtonW(), _("Resume Game"), OPALIGN);
	row++;

	if (hasGameGuideTopics())
	{
		// add "View Guide"
		addIGTextButton(INTINGAMEOP_OPENGAMEGUIDE, igButtonX(), igLineY(row), igButtonW(), _("View Guide"), OPALIGN);
		row++;
	}

	// add 'options'
	addIGTextButton(INTINGAMEOP_OPTIONS, igButtonX(), igLineY(row), igButtonW(), _("Options"), OPALIGN);
	row++;

	if (!((bMultiPlayer && NetPlay.bComms) || bInTutorial || NETisReplay()))
	{
		if (bMultiPlayer)
		{
			// add 'load'
			addIGTextButton(INTINGAMEOP_LOAD_SKIRMISH, igButtonX(), igLineY(row), igButtonW(), _("Load Game"), OPALIGN);
			row++;
			// add 'save'
			addIGTextButton(INTINGAMEOP_SAVE_SKIRMISH, igButtonX(), igLineY(row), igButtonW(), _("Save Game"), OPALIGN);
			row++;
		}
		else
		{
			// add 'load'
			addIGTextButton(INTINGAMEOP_LOAD_MISSION, igButtonX(), igLineY(row), igButtonW(), _("Load Game"), OPALIGN);
			row++;
			if (!getCamTweakOption_AutosavesOnly())
			{
				// add 'save'
				addIGTextButton(INTINGAMEOP_SAVE_MISSION, igButtonX(), igLineY(row), igButtonW(), _("Save Game"), OPALIGN);
				row++;
			}
		}
	}

	// add 'quit' text
	if (NetPlay.isHost && bMultiPlayer && NetPlay.bComms)
	{
		addIGTextButton(hostQuitConfirmation ? INTINGAMEOP_HOSTQUIT : INTINGAMEOP_QUIT, igButtonX(), igLineY(row), igButtonW(), _("Host Quit"), OPALIGN);
	}
	else
	{
		addIGTextButton(INTINGAMEOP_CONFIRM_QUIT, igButtonX(), igLineY(row), igButtonW(), _("Quit"), OPALIGN);
	}

	// calculate layout
	int lines = row;
	ingameOp->setCalcLayout([lines](WIDGET *psWidget){
		if (igo2126())
		{
			psWidget->setGeometry(igFormX(igFormW()), igFormY(igFormH(lines)), igFormW(), igFormH(lines));
			return;
		}
		psWidget->setGeometry(INTINGAMEOP_X, INTINGAMEOPAUTO_Y(lines), INTINGAMEOP_W, INTINGAMEOPAUTO_H(lines));
	});

	intMode		= INT_INGAMEOP;			// change interface mode.
	InGameOpUp	= true;					// inform interface.

	wzSetCursor(CURSOR_DEFAULT);

	return true;
}

bool intAddInGameOptions()
{
	return _intAddInGameOptions();
}

static bool startInGameConfirmQuit()
{
	widgDelete(psWScreen, INTINGAMEOP); // get rid of the old stuff.
	auto const& parent = psWScreen->psForm;

	// add form
	auto ingameOp = std::make_shared<IntFormAnimated>();
	parent->attach(ingameOp);
	ingameOp->id = INTINGAMEOP;

	int row = 1;

	auto label = std::make_shared<W_LABEL>();
	ingameOp->attach(label);
	const bool flat = igo2126();
	const int confirmW = flat ? 380 : INTINGAMEOP4_W;
	const int confirmX = flat ? 16 : INTINGAMEOP_2_X;
	const int confirmBW = flat ? confirmW - 32 : INTINGAMEOP4_OP_W;
	auto lineY = [flat](int line) { return flat ? 16 + (line - 1) * 40 : INTINGAMEOPAUTO_Y_LINE(line); };
	label->setGeometry(confirmX, lineY(row), confirmBW, flat ? 30 : INTINGAMEOP_OP_H);
	if (bMultiPlayer && (NetPlay.bComms || NETisReplay()))
	{
		label->setString(_("Warning: Are you sure?")); //Do not mention saving in real multiplayer matches
	}
	else
	{
		label->setString(_("Warning: Are you sure? Any unsaved progress will be lost."));
	}
	label->setTextAlignment(WLAB_ALIGNCENTRE);
	label->setFont(flat ? font_regular_bold : font_medium, flat ? hud2126::warn() : WZCOL_YELLOW);
	if (flat)
	{
		auto dimmer = std::make_shared<IGDim2126>();
		ingameOp->attach(dimmer);
		dimmer->setGeometry(0, 0, 1, 1);
	}

	row++;

	// add quit confirmation text
	addIGTextButton(INTINGAMEOP_QUIT, confirmX, lineY(row), confirmBW, _("Confirm"), OPALIGN);
	row++;
	addIGTextButton(INTINGAMEOP_GO_BACK, confirmX, lineY(row), confirmBW, _("Back"), OPALIGN);

	ingameOp->setCalcLayout([row, flat, confirmW](WIDGET* psWidget) {
		if (flat)
		{
			const int h = 16 + row * 40 + 10;
			psWidget->setGeometry(igFormX(confirmW), igFormY(h), confirmW, h);
			return;
		}
		psWidget->setGeometry(INTINGAMEOP4_X, INTINGAMEOPAUTO_Y(row), INTINGAMEOP4_W, INTINGAMEOPAUTO_H(row));
	});

	return true;
}

static bool runInGameConfirmQuit(UDWORD id)
{
	switch (id)
	{
		case INTINGAMEOP_QUIT:
			return true;

		case INTINGAMEOP_GO_BACK:
			intReopenMenuWithoutUnPausing();
			break;

		default:
			break;
	}

	return false;
}

//
// Quick hack to throw up a ingame 'popup' for when the host drops connection.
//
void resetInGameHostQuit()
{
	inGameHostQuitHandled = false;
}

void handleInGameHostQuit()
{
	if (inGameHostQuitHandled)
	{
		return;
	}
	inGameHostQuitHandled = true;

	WZ_Notification notification;
	notification.duration = GAME_TICKS_PER_SEC * 10;
	notification.contentTitle = _("Host has quit the game!");
	notification.contentText = _("The game can't continue without the host.");
	notification.tag = "hostQuitInGame";
	addNotification(notification, WZ_Notification_Trigger::Immediate());

	addConsoleMessage(_("Host has quit the game!"), DEFAULT_JUSTIFY, SYSTEM_MESSAGE, false, MAX_CONSOLE_MESSAGE_DURATION);

	if (!ingame.endTime.has_value())
	{
		ingame.endTime = std::chrono::steady_clock::now();
	}

	shutdownGameStartScreen();

	if (MissionResUp || intMode == INT_MISSIONRES)
	{
		// after-game results are already being displayed
		intMissionResultsUpdateButtons();
		return;
	}

	if (NetPlay.players[selectedPlayer].isSpectator)
	{
		// Special message for spectators to inform them that the game is fully over
		addConsoleMessage(_("GAME OVER"), CENTRE_JUSTIFY, SYSTEM_MESSAGE, false, MAX_CONSOLE_MESSAGE_DURATION);
		addConsoleMessage(_("The battle is over - you can leave the room."), CENTRE_JUSTIFY, SYSTEM_MESSAGE, false, MAX_CONSOLE_MESSAGE_DURATION);
		return;
	}

	audio_StopAll();

	//clear out any mission widgets - timers etc that may be on the screen
	clearMissionWidgets();

	// Display the after-game results, without recording a win/loss for an aborted game
	intAddMissionResult(testPlayerHasWon(), false, false, _("Host has quit the game!"));
}

// ////////////////////////////////////////////////////////////////////////////

static void ProcessOptionFinished()
{
	intMode		= INT_NORMAL;

	if (gamePaused())
	{
		kf_TogglePauseMode();
	}

	setReticulesEnabled(true);

	// re-show the spectator game over screen (if it's up)
	setSpectatorGameOverScreenVisible(true);
}

void intCloseInGameOptionsNoAnim()
{
	if (NetPlay.isHost)
	{
		widgDelete(psWScreen, INTINGAMEPOPUP);
	}
	widgDelete(psWScreen, INTINGAMEOP);
	InGameOpUp = false;

	ProcessOptionFinished();
	resetMissionWidgets();
}

// ////////////////////////////////////////////////////////////////////////////
bool intCloseInGameOptions(bool bPutUpLoadSave, bool bResetMissionWidgets)
{
	isInGameConfirmQuitUp = false;

	if (NetPlay.isHost)
	{
		widgDelete(psWScreen, INTINGAMEPOPUP);
	}

	if (bPutUpLoadSave)
	{
		WIDGET *widg = widgGetFromID(psWScreen, INTINGAMEOP);
		if (widg)
		{
			widgDelete(psWScreen, INTINGAMEOP);
		}

		InGameOpUp = false;
	}
	if (!bPutUpLoadSave)
	{
		// close the form.
		// Start the window close animation.
		IntFormAnimated *form = dynamic_cast<IntFormAnimated *>(widgGetFromID(psWScreen, INTINGAMEOP));
		if (form)
		{
			form->closeAnimateDelete();
			InGameOpUp = false;
		}
		form = dynamic_cast<IntFormAnimated *>(widgGetFromID(psWScreen, INTINGAMEOP_QUIT));
		if (form)
		{
			form->closeAnimateDelete();
			InGameOpUp = false;
		}
	}

	ProcessOptionFinished();

	//don't add the widgets if the load/save screen is put up or exiting to front end
	if (bResetMissionWidgets)
	{
		//put any widgets back on for the missions
		resetMissionWidgets();
	}

	// the setting for group menu display may have been modified
	intShowGroupSelectionMenu();
	return true;
}

void intReopenMenuWithoutUnPausing()
{
	isInGameConfirmQuitUp = false;

	if (NetPlay.isHost)
	{
		widgDelete(psWScreen, INTINGAMEPOPUP);
	}
	widgDelete(psWScreen, INTINGAMEOP);
	intAddInGameOptions();
}

static bool startIGOptionsMenu()
{
	showInGameOptionsScreen(psWScreen, createOptionsBrowser(true), []() {
		// the setting for group menu display may have been modified
		intShowGroupSelectionMenu();

		// put back the mission widgets
		resetMissionWidgets();
	});
	return true;
}

// ////////////////////////////////////////////////////////////////////////////
// process clicks made by user.
void intProcessInGameOptions(UDWORD id)
{
	if (isInGameConfirmQuitUp)
	{
		if (runInGameConfirmQuit(id))
		{
			intCloseInGameOptions(true, true);
		}
		return;
	}

	switch (id)
	{
	// NORMAL KEYS
	case INTINGAMEOP_HOSTQUIT:				//quit was pressed
		addHostQuitOptions();
		break;

	case INTINGAMEOP_CONFIRM_QUIT:
		startInGameConfirmQuit();
		isInGameConfirmQuitUp = true;
		break;

	case INTINGAMEOP_QUIT:		//quit was confirmed.
		intCloseInGameOptions(false, false);
		break;

	case INTINGAMEOP_OPENGAMEGUIDE:
		intCloseInGameOptions(false, false);
		showGuideScreen([]() {
			if (psWScreen != nullptr)
			{
				resetMissionWidgets();
			}
		}, true);
		break;

	case INTINGAMEOP_OPTIONS:			//game options  was pressed
		intCloseInGameOptions(false, false);
		startIGOptionsMenu();
		break;
	case INTINGAMEOP_RESUME:			//resume was pressed.
		intCloseInGameOptions(false, true);
		break;
	case INTINGAMEOP_GO_BACK:
		intReopenMenuWithoutUnPausing();
		break;


//	case INTINGAMEOP_REPLAY:
//		intCloseInGameOptions(true, false);
//		if(0!=strcmp(getLevelName(),"CAM_1A"))
//		{
//			loopMissionState = LMS_LOADGAME;
//			strcpy(saveGameName, "replay/replay.gam");
//			addConsoleMessage(_("GAME SAVED!"), LEFT_JUSTIFY, SYSTEM_MESSAGE);
//		}
//		break;
	case INTINGAMEOP_LOAD_MISSION:
		intCloseInGameOptions(true, false);
		addLoadSave(LOAD_INGAME_MISSION, _("Load Campaign Saved Game"));	// change mode when loadsave returns
		break;
	case INTINGAMEOP_LOAD_SKIRMISH:
		intCloseInGameOptions(true, false);
		addLoadSave(LOAD_INGAME_SKIRMISH, _("Load Skirmish Saved Game"));	// change mode when loadsave returns
		break;
	case INTINGAMEOP_SAVE_MISSION:
		intCloseInGameOptions(true, false);
		addLoadSave(SAVE_INGAME_MISSION, _("Save Campaign Game"));
		break;
	case INTINGAMEOP_SAVE_SKIRMISH:
		intCloseInGameOptions(true, false);
		addLoadSave(SAVE_INGAME_SKIRMISH, _("Save Skirmish Game"));
		break;

	default:
		break;
	}
}
