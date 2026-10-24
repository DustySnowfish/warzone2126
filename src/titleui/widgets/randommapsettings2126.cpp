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
/// Warzone 2126: the lobby's Random map settings panel (see randommapsettings2126.h).

#include "randommapsettings2126.h"

#include "lib/framework/frame.h"
#include "lib/framework/wzapp.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/netplay/netplay.h"
#include "lib/widget/button.h"
#include "lib/widget/dropdown.h"
#include "lib/widget/editbox.h"
#include "lib/widget/label.h"
#include "lib/widget/slider.h"
#include "lib/widget/paragraph.h"

#include "src/main.h"
#include "src/multiplay.h"
#include "src/multirecv.h"
#include "src/multiint.h"
#include "src/randommap2126.h"
#include "src/hci/hud2126_style.h"

#include <array>

namespace
{

constexpr int kPad = 14;
constexpr int kRowH = 30;
constexpr int kLabelW = 150;
constexpr int kValueW = 40;
constexpr int kPanelW = 470;
constexpr int kControlH = 24;
constexpr uint32_t kSendDelayMs = 350; // slider drags settle before the options go out

bool canEdit()
{
	return ingame.side == InGameSide::HOST_OR_SINGLEPLAYER;
}

// MARK: - Small widgets in the 2126 style

class FlatButton : public W_BUTTON
{
public:
	explicit FlatButton(const WzString &label)
	{
		text.setText(label, font_regular);
	}
	int32_t idealWidth() override { return text->width() + 20; }
	void display(int xOffset, int yOffset) override
	{
		const int x0 = x() + xOffset, y0 = y() + yOffset, x1 = x0 + width(), y1 = y0 + height();
		const bool disabled = (getState() & WBUT_DISABLE) != 0;
		const bool hot = !disabled && (getState() & WBUT_HIGHLIGHT) != 0;
		const bool down = !disabled && (getState() & WBUT_DOWN) != 0;
		pie_UniTransBoxFill(x0, y0, x1, y1, down ? hud2126::tabActive() : hot ? hud2126::tabHover() : hud2126::tab());
		iV_Box(x0, y0, x1 - 1, y1 - 1, hot ? hud2126::accent() : hud2126::border());
		text->render(x0 + (width() - text->width()) / 2, y0 + (height() - text->lineSize()) / 2 - text->aboveBase(), disabled ? hud2126::disabled() : hud2126::value());
	}
private:
	WzCachedText text;
};

void displayFlatSlider(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	W_SLIDER *slider = static_cast<W_SLIDER *>(psWidget);
	const int x0 = xOffset + psWidget->x(), y0 = yOffset + psWidget->y();
	const int w = psWidget->width(), h = psWidget->height();
	const bool enabled = slider->isEnabled();
	const int trackY = y0 + h / 2 - 2;
	const int knobW = 10;
	const int travel = w - knobW;
	const int kx = x0 + travel * static_cast<int>(slider->pos) / std::max<int>(1, static_cast<int>(slider->numStops));
	pie_UniTransBoxFill(x0, trackY, x0 + w, trackY + 4, hud2126::tab());
	pie_UniTransBoxFill(x0, trackY, kx + knobW / 2, trackY + 4, enabled ? hud2126::accent() : hud2126::disabled());
	// The usual value (50) is marked so it is easy to get back to
	const int mid = x0 + travel / 2 + knobW / 2;
	pie_UniTransBoxFill(mid, trackY - 3, mid + 1, trackY + 7, hud2126::label());
	const PIELIGHT knob = !enabled ? hud2126::disabled() : slider->isHighlighted() ? hud2126::value() : hud2126::text();
	pie_UniTransBoxFill(kx, y0 + 2, kx + knobW, y0 + h - 2, knob);
}

void displayCodeBox(WIDGET *psWidget, UDWORD xOffset, UDWORD yOffset)
{
	const int x0 = xOffset + psWidget->x(), y0 = yOffset + psWidget->y();
	pie_UniTransBoxFill(x0, y0, x0 + psWidget->width(), y0 + psWidget->height(), pal_RGBA(8, 12, 16, 230));
	iV_Box(x0, y0, x0 + psWidget->width() - 1, y0 + psWidget->height() - 1, hud2126::border());
}

void fillDropdown(DropdownWidget &dropdown, const std::vector<std::string> &items)
{
	dropdown.clear();
	dropdown.setListHeight(kControlH * static_cast<uint32_t>(items.size()));
	for (const auto &text : items)
	{
		auto item = std::make_shared<W_LABEL>();
		item->setFont(font_regular, hud2126::value());
		item->setString(WzString::fromUtf8("  " + text));
		item->setGeometry(0, 0, 200, kControlH);
		dropdown.addItem(item);
	}
}

const char kTilesetLetters[] = { 'A', 'U', 'R' };
const std::array<int, 5> kPlayerChoices = { 2, 4, 8, 10, 15 };

std::shared_ptr<W_LABEL> makeLabel(const WzString &text, iV_fonts font, PIELIGHT colour)
{
	auto label = std::make_shared<W_LABEL>();
	label->setFont(font, colour);
	label->setString(text);
	label->setGeometry(0, 0, label->getMaxLineWidth(), iV_GetTextLineSize(font));
	return label;
}

// MARK: - The panel

class RandomMapPanel : public WIDGET
{
public:
	void initialize(const std::function<void()> &onChangedFunc)
	{
		onChanged = onChangedFunc;
		auto weakSelf = std::weak_ptr<RandomMapPanel>(std::static_pointer_cast<RandomMapPanel>(shared_from_this()));

		title = makeLabel(_("Random map settings"), font_regular_bold, hud2126::value());
		attach(title);
		subtitle = makeLabel("", font_small, hud2126::label());
		subtitle->setCanTruncate(true);
		attach(subtitle);

		// Landscape
		styleLabel = makeLabel(_("Landscape"), font_regular, hud2126::text());
		attach(styleLabel);
		styleDropdown = std::make_shared<DropdownWidget>();
		attach(styleDropdown);
		styleDropdown->setListBackgroundColor(pal_RGBA(18, 24, 30, 245));
		{
			std::vector<std::string> names;
			for (int i = 0; i < RandomMapSettings2126::STYLE_COUNT; ++i)
			{
				names.push_back(gettext(randomMapStyleName2126(i)));
			}
			fillDropdown(*styleDropdown, names);
		}
		styleDropdown->setSelectedIndex(game.randomMap.style);
		styleDropdown->setCanChange([](DropdownWidget &, size_t, std::shared_ptr<WIDGET>) { return canEdit(); });
		styleDropdown->setOnChange([weakSelf](DropdownWidget &dropdown) {
			auto self = weakSelf.lock();
			auto index = dropdown.getSelectedIndex();
			if (!self || !index.has_value() || !canEdit())
			{
				return;
			}
			game.randomMap.style = static_cast<uint8_t>(index.value());
			self->changed();
		});
		styleDropdown->setTip(_("The kind of landscape. Random picks one of the first five for each new map; Islands puts every player on their own island (hover and air units cross the sea)."));

		// Tileset: switches to the Random map with the same number of slots
		tilesetLabel = makeLabel(_("Tileset"), font_regular, hud2126::text());
		attach(tilesetLabel);
		tilesetDropdown = std::make_shared<DropdownWidget>();
		attach(tilesetDropdown);
		tilesetDropdown->setListBackgroundColor(pal_RGBA(18, 24, 30, 245));
		fillDropdown(*tilesetDropdown, { _("Arizona (desert)"), _("Urban (ruined city)"), _("Rockies (mountains, snow)") });
		tilesetDropdown->setCanChange([](DropdownWidget &, size_t, std::shared_ptr<WIDGET>) { return canEdit(); });
		tilesetDropdown->setOnChange([weakSelf](DropdownWidget &dropdown) {
			auto self = weakSelf.lock();
			auto index = dropdown.getSelectedIndex();
			if (!self || !index.has_value() || index.value() >= 3 || !canEdit())
			{
				return;
			}
			auto identity = randomMapIdentity2126(game.map, game.maxPlayers);
			if (!identity.has_value() || identity->tileset == kTilesetLetters[index.value()])
			{
				return;
			}
			RandomMapIdentity2126 target = identity.value();
			target.tileset = kTilesetLetters[index.value()];
			widgScheduleTask([weakSelf, target]() {
				auto strongSelf = weakSelf.lock();
				if (lobbySwitchRandomMap2126(randomMapLevelName2126(target)))
				{
					if (strongSelf)
					{
						strongSelf->syncFromGame(true);
						if (strongSelf->onChanged)
						{
							strongSelf->onChanged();
						}
					}
				}
				else if (strongSelf)
				{
					strongSelf->setMessage(_("The map cannot be changed now"), true);
					strongSelf->syncFromGame(true);
				}
			});
		});
		tilesetDropdown->setTip(_("The ground, plants and buildings. The size and number of players stay the same."));

		// Players: switches to the Random map with that many slots
		playersLabel = makeLabel(_("Players"), font_regular, hud2126::text());
		attach(playersLabel);
		playersDropdown = std::make_shared<DropdownWidget>();
		attach(playersDropdown);
		playersDropdown->setListBackgroundColor(pal_RGBA(18, 24, 30, 245));
		{
			std::vector<std::string> names;
			for (int n : kPlayerChoices)
			{
				names.push_back(astringf(_("%d players"), n));
			}
			fillDropdown(*playersDropdown, names);
		}
		playersDropdown->setCanChange([](DropdownWidget &, size_t, std::shared_ptr<WIDGET>) { return canEdit(); });
		playersDropdown->setOnChange([weakSelf](DropdownWidget &dropdown) {
			auto self = weakSelf.lock();
			auto index = dropdown.getSelectedIndex();
			if (!self || !index.has_value() || index.value() >= kPlayerChoices.size() || !canEdit())
			{
				return;
			}
			auto identity = randomMapIdentity2126(game.map, game.maxPlayers);
			RandomMapIdentity2126 target;
			target.tileset = identity.has_value() ? identity->tileset : 'A';
			target.players = kPlayerChoices[index.value()];
			if (identity.has_value() && identity->players == target.players)
			{
				return;
			}
			widgScheduleTask([weakSelf, target]() {
				auto strongSelf = weakSelf.lock();
				const bool ok = lobbySwitchRandomMap2126(randomMapLevelName2126(target));
				if (strongSelf)
				{
					if (!ok)
					{
						strongSelf->setMessage(_("The number of players cannot be changed now"), true);
					}
					strongSelf->syncFromGame(true);
					if (ok && strongSelf->onChanged)
					{
						strongSelf->onChanged();
					}
				}
			});
		});
		playersDropdown->setTip(_("How many players the map is made for. Online, it can only go up."));

		// Map size (choices depend on the number of players; filled in syncFromGame)
		sizeLabel = makeLabel(_("Map size"), font_regular, hud2126::text());
		attach(sizeLabel);
		sizeDropdown = std::make_shared<DropdownWidget>();
		attach(sizeDropdown);
		sizeDropdown->setListBackgroundColor(pal_RGBA(18, 24, 30, 245));
		sizeDropdown->setCanChange([](DropdownWidget &, size_t, std::shared_ptr<WIDGET>) { return canEdit(); });
		sizeDropdown->setOnChange([weakSelf](DropdownWidget &dropdown) {
			auto self = weakSelf.lock();
			auto index = dropdown.getSelectedIndex();
			if (!self || !index.has_value() || index.value() >= self->sizeValues.size() || !canEdit())
			{
				return;
			}
			game.randomMap.size = static_cast<uint16_t>(self->sizeValues[index.value()]);
			self->changed(true);
		});
		sizeDropdown->setTip(_("Map width and height in tiles. Bigger maps take longer to cross and to generate."));

		// Terrain: natural or mirrored
		terrainLabel = makeLabel(_("Terrain"), font_regular, hud2126::text());
		attach(terrainLabel);
		terrainDropdown = std::make_shared<DropdownWidget>();
		attach(terrainDropdown);
		terrainDropdown->setListBackgroundColor(pal_RGBA(18, 24, 30, 245));
		fillDropdown(*terrainDropdown, { _("Natural"), _("Mirrored (identical for every player)") });
		terrainDropdown->setCanChange([](DropdownWidget &, size_t, std::shared_ptr<WIDGET>) { return canEdit(); });
		terrainDropdown->setOnChange([weakSelf](DropdownWidget &dropdown) {
			auto self = weakSelf.lock();
			auto index = dropdown.getSelectedIndex();
			if (!self || !index.has_value() || !canEdit())
			{
				return;
			}
			game.randomMap.mirror = index.value() == 1 ? 1 : 0;
			self->changed(true);
		});
		terrainDropdown->setTip(_("Mirrored: every player's part of the map is a turned copy of the first, for strictly fair games. Natural: one landscape, so routes differ between players."));

		// Sliders
		for (int i = 0; i < RandomMapSettings2126::SLIDER_COUNT; ++i)
		{
			sliderLabels[i] = makeLabel(WzString::fromUtf8(gettext(randomMapSliderName2126(i))), font_regular, hud2126::text());
			sliderLabels[i]->setTip(gettext(randomMapSliderTip2126(i)));
			attach(sliderLabels[i]);

			W_SLDINIT init;
			init.numStops = 100 / RandomMapSettings2126::SLIDER_STEP;
			init.barSize = 10;
			init.pos = game.randomMap.sliders[i] / RandomMapSettings2126::SLIDER_STEP;
			init.width = 200;
			init.height = 18;
			init.pDisplay = displayFlatSlider;
			init.pTip = gettext(randomMapSliderTip2126(i));
			sliders[i] = std::make_shared<W_SLIDER>(&init);
			attach(sliders[i]);
			sliders[i]->addOnChange([weakSelf, i](W_SLIDER &slider) {
				auto self = weakSelf.lock();
				if (!self)
				{
					return;
				}
				if (!canEdit())
				{
					slider.pos = game.randomMap.sliders[i] / RandomMapSettings2126::SLIDER_STEP;
					return;
				}
				game.randomMap.sliders[i] = static_cast<uint8_t>(slider.pos * RandomMapSettings2126::SLIDER_STEP);
				self->changed();
			});
			if (!canEdit())
			{
				sliders[i]->disable();
			}
		}

		// Map code
		codeLabel = makeLabel(_("Map code"), font_regular, hud2126::text());
		codeLabel->setTip(_("Share this code: anyone who enters it gets exactly this map, on the same Random map"));
		attach(codeLabel);
		codeBox = std::make_shared<W_EDITBOX>();
		codeBox->pBoxDisplay = displayCodeBox;
		codeBox->setMaxStringSize(40);
		codeBox->setPlaceholder(_("Paste a map code"));
		codeBox->setPlaceholderTextColor(WZCOL_TEXT_DARK);
		codeBox->setOnReturnHandler([weakSelf](W_EDITBOX &box) {
			if (auto self = weakSelf.lock())
			{
				self->applyCode(box.getString().toUtf8());
			}
		});
		if (!canEdit())
		{
			codeBox->setState(WEDBS_DISABLE);
		}
		attach(codeBox);

		copyButton = std::make_shared<FlatButton>(_("Copy"));
		copyButton->setTip(_("Copy the map code to the clipboard"));
		copyButton->addOnClickHandler([weakSelf](W_BUTTON &) {
			auto self = weakSelf.lock();
			if (!self)
			{
				return;
			}
			const std::string code = self->currentCode();
			if (!code.empty() && wzSetClipboardText(code.c_str()))
			{
				self->setMessage(_("Copied to the clipboard"), false);
			}
		});
		attach(copyButton);

		pasteButton = std::make_shared<FlatButton>(_("Paste"));
		pasteButton->setTip(_("Use the map code on the clipboard"));
		pasteButton->addOnClickHandler([weakSelf](W_BUTTON &) {
			auto self = weakSelf.lock();
			if (self && wzHasClipboardText())
			{
				self->applyCode(wzGetClipboardText().toUtf8());
			}
		});
		attach(pasteButton);

		newButton = std::make_shared<FlatButton>(_("New"));
		newButton->setTip(_("A new seed: a different map with the same settings"));
		newButton->addOnClickHandler([weakSelf](W_BUTTON &) {
			auto self = weakSelf.lock();
			if (self && canEdit())
			{
				game.randomMap.seed = newRandomMapSeed2126();
				self->changed(true);
			}
		});
		attach(newButton);

		defaultsButton = std::make_shared<FlatButton>(_("Defaults"));
		defaultsButton->setTip(_("Random landscape and every slider at 50 (keeps the seed)"));
		defaultsButton->addOnClickHandler([weakSelf](W_BUTTON &) {
			auto self = weakSelf.lock();
			if (self && canEdit())
			{
				const uint32_t seed = game.randomMap.seed;
				game.randomMap = RandomMapSettings2126();
				game.randomMap.seed = seed;
				self->changed(true);
			}
		});
		attach(defaultsButton);

		if (!canEdit())
		{
			pasteButton->setState(WBUT_DISABLE);
			newButton->setState(WBUT_DISABLE);
			defaultsButton->setState(WBUT_DISABLE);
		}

		message = makeLabel("", font_small, hud2126::label());
		message->setCanTruncate(true);
		attach(message);

		syncFromGame(true);
	}

	int32_t idealWidth() override { return kPanelW; }
	int32_t idealHeight() override
	{
		return kPad + 22 + 18 + 10 + kRowH * (5 + RandomMapSettings2126::SLIDER_COUNT) + 10 + kRowH + 22 + kRowH + kPad;
	}

	void geometryChanged() override
	{
		const int w = width() - 2 * kPad;
		int y = kPad;
		title->setGeometry(kPad, y, w, 20);
		y += 22;
		subtitle->setGeometry(kPad, y, w, 16);
		y += 18 + 10;
		const int controlX = kPad + kLabelW;
		const int controlW = w - kLabelW - kValueW;
		styleLabel->setGeometry(kPad, y + (kControlH - styleLabel->height()) / 2, kLabelW, styleLabel->height());
		styleDropdown->setGeometry(controlX, y, controlW, kControlH);
		y += kRowH;
		const std::pair<std::shared_ptr<W_LABEL>, std::shared_ptr<DropdownWidget>> extraRows[] = {
			{ tilesetLabel, tilesetDropdown }, { playersLabel, playersDropdown }, { sizeLabel, sizeDropdown }, { terrainLabel, terrainDropdown } };
		for (const auto &row : extraRows)
		{
			row.first->setGeometry(kPad, y + (kControlH - row.first->height()) / 2, kLabelW, row.first->height());
			row.second->setGeometry(controlX, y, controlW, kControlH);
			y += kRowH;
		}
		for (int i = 0; i < RandomMapSettings2126::SLIDER_COUNT; ++i)
		{
			sliderLabels[i]->setGeometry(kPad, y + (kControlH - sliderLabels[i]->height()) / 2, kLabelW, sliderLabels[i]->height());
			sliders[i]->setGeometry(controlX, y + 3, controlW, 18);
			sliderRowY[i] = y;
			y += kRowH;
		}
		y += 10;
		codeLabel->setGeometry(kPad, y, w, codeLabel->height());
		y += 22;
		const int bw = 62, gap = 6;
		const int boxW = w - 3 * (bw + gap);
		codeBox->setGeometry(kPad, y, boxW, kControlH);
		copyButton->setGeometry(kPad + boxW + gap, y, bw, kControlH);
		pasteButton->setGeometry(kPad + boxW + gap + (bw + gap), y, bw, kControlH);
		newButton->setGeometry(kPad + boxW + gap + 2 * (bw + gap), y, bw, kControlH);
		y += kRowH;
		defaultsButton->setGeometry(kPad, y, 90, kControlH);
		message->setGeometry(kPad + 100, y + 4, w - 100, 16);
	}

	void run(W_CONTEXT *) override
	{
		if (pendingSend && realTime - lastChange >= kSendDelayMs)
		{
			pendingSend = false;
			sendNow();
		}
		syncFromGame(false);
	}

	void display(int xOffset, int yOffset) override
	{
		const int x0 = xOffset + x(), y0 = yOffset + y();
		pie_UniTransBoxFill(x0, y0, x0 + width(), y0 + height(), pal_RGBA(14, 19, 24, 245));
		iV_Box(x0, y0, x0 + width() - 1, y0 + height() - 1, hud2126::accent());
		// The dropdowns' boxes and arrows (the dropdown itself only draws its items)
		for (const auto &dropdown : { styleDropdown, tilesetDropdown, playersDropdown, sizeDropdown, terrainDropdown })
		{
			const int dx0 = x0 + dropdown->x(), dy0 = y0 + dropdown->y();
			const int dx1 = dx0 + dropdown->width(), dy1 = dy0 + dropdown->height();
			const bool hot = canEdit() && dropdown->isMouseOverWidget();
			pie_UniTransBoxFill(dx0, dy0, dx1, dy1, hot ? hud2126::tabHover() : hud2126::tab());
			iV_Box(dx0, dy0, dx1 - 1, dy1 - 1, hot ? hud2126::accent() : hud2126::border());
			const int ax = dx1 - 18, ay = dy0 + dropdown->height() / 2 - 2;
			for (int r = 0; r < 4; ++r)
			{
				pie_UniTransBoxFill(ax + r, ay + r, ax + 8 - r, ay + r + 1, canEdit() ? hud2126::text() : hud2126::disabled());
			}
		}
		// Slider values
		for (int i = 0; i < RandomMapSettings2126::SLIDER_COUNT; ++i)
		{
			const uint8_t v = game.randomMap.sliders[i];
			valueText[i].setText(WzString::number(v), font_regular);
			const int vx = x0 + width() - kPad - valueText[i]->width();
			const int vy = y0 + sliderRowY[i] + (kControlH - valueText[i]->lineSize()) / 2 - valueText[i]->aboveBase();
			valueText[i]->render(vx, vy, v == RandomMapSettings2126::SLIDER_DEFAULT ? hud2126::label() : hud2126::value());
		}
	}

private:
	std::string currentCode() const
	{
		auto identity = randomMapIdentity2126(game.map, game.maxPlayers);
		if (!identity.has_value() || game.randomMap.seed == 0)
		{
			return {};
		}
		return encodeRandomMapCode2126(identity.value(), game.randomMap);
	}

	void setMessage(const char *text, bool warning)
	{
		message->setFontColour(warning ? hud2126::warn() : hud2126::good());
		message->setString(WzString::fromUtf8(text));
		messageTime = realTime;
	}

	void applyCode(const std::string &code)
	{
		if (!canEdit())
		{
			return;
		}
		auto decoded = decodeRandomMapCode2126(code);
		if (!decoded.has_value())
		{
			setMessage(_("That is not a map code"), true);
			return;
		}
		auto current = randomMapIdentity2126(game.map, game.maxPlayers);
		if (!current.has_value() || current->players != decoded->first.players)
		{
			setMessage(astringf(_("That code is for a Random map with %d players: pick that map first"), decoded->first.players).c_str(), true);
			return;
		}
		if (current->tileset != decoded->first.tileset)
		{
			// Same slots, other tileset: switch to it
			if (!lobbySwitchRandomMap2126(randomMapLevelName2126(decoded->first)))
			{
				setMessage(_("The map cannot be changed now"), true);
				return;
			}
		}
		game.randomMap = decoded->second;
		changed(true);
		setMessage(_("Map code applied"), false);
	}

	/// The host changed something: show it now, send it (and refresh the preview) once it settles
	void changed(bool immediately = false)
	{
		game.randomMap.sanitise();
		lastChange = realTime;
		pendingSend = true;
		if (immediately)
		{
			lastChange = realTime - kSendDelayMs;
		}
		syncFromGame(true);
	}

	void sendNow()
	{
		resetReadyStatus(false);
		if (NetPlay.isHost && NetPlay.bComms)
		{
			sendOptions();
		}
		requestMapPreviewLoad(false);
		if (onChanged)
		{
			onChanged();
		}
	}

	void syncFromGame(bool force)
	{
		if (!force && shown == game.randomMap && shownMap == game.map)
		{
			if (messageTime != 0 && realTime - messageTime > 6000)
			{
				message->setString("");
				messageTime = 0;
			}
			return;
		}
		shown = game.randomMap;
		shownMap = game.map;
		if (styleDropdown->getSelectedIndex().value_or(99) != shown.style)
		{
			styleDropdown->setSelectedIndex(shown.style);
		}
		for (int i = 0; i < RandomMapSettings2126::SLIDER_COUNT; ++i)
		{
			sliders[i]->pos = shown.sliders[i] / RandomMapSettings2126::SLIDER_STEP;
		}
		auto currentIdentity = randomMapIdentity2126(game.map, game.maxPlayers);
		if (currentIdentity.has_value())
		{
			for (size_t t = 0; t < 3; ++t)
			{
				if (kTilesetLetters[t] == currentIdentity->tileset && tilesetDropdown->getSelectedIndex().value_or(99) != t)
				{
					tilesetDropdown->setSelectedIndex(t);
				}
			}
			for (size_t k = 0; k < kPlayerChoices.size(); ++k)
			{
				if (kPlayerChoices[k] == currentIdentity->players && playersDropdown->getSelectedIndex().value_or(99) != k)
				{
					playersDropdown->setSelectedIndex(k);
				}
			}
			// Size choices for this number of players
			if (sizeForPlayers != currentIdentity->players)
			{
				sizeForPlayers = currentIdentity->players;
				sizeValues.clear();
				std::vector<std::string> names;
				sizeValues.push_back(0);
				names.push_back(astringf(_("Auto (%d)"), randomMapDefaultSize2126(sizeForPlayers)));
				for (int choice : randomMapSizeChoices2126())
				{
					if (choice >= randomMapMinSize2126(sizeForPlayers))
					{
						sizeValues.push_back(choice);
						names.push_back(astringf("%d x %d", choice, choice));
					}
				}
				fillDropdown(*sizeDropdown, names);
			}
			for (size_t k = 0; k < sizeValues.size(); ++k)
			{
				const int effective = shown.size ? std::max<int>(shown.size, randomMapMinSize2126(sizeForPlayers)) : 0;
				if (sizeValues[k] == effective && sizeDropdown->getSelectedIndex().value_or(99) != k)
				{
					sizeDropdown->setSelectedIndex(k);
				}
			}
		}
		if (terrainDropdown->getSelectedIndex().value_or(99) != shown.mirror)
		{
			terrainDropdown->setSelectedIndex(shown.mirror);
		}
		const std::string code = currentCode();
		if (!codeBox->isEditing())
		{
			codeBox->setString(WzString::fromUtf8(code));
		}
		auto identity = randomMapIdentity2126(game.map, game.maxPlayers);
		if (identity.has_value())
		{
			subtitle->setString(WzString::fromUtf8(astringf(_("%s, %d players, %d x %d tiles. Every player gets this exact map."), game.map, static_cast<int>(game.maxPlayers), randomMapResolvedSize2126(game.randomMap, game.maxPlayers), randomMapResolvedSize2126(game.randomMap, game.maxPlayers))));
		}
		else
		{
			subtitle->setString(_("Pick one of the Random maps to use these settings"));
		}
		copyButton->setState(code.empty() ? WBUT_DISABLE : 0);
	}

	std::function<void()> onChanged;
	std::shared_ptr<W_LABEL> title, subtitle, styleLabel, codeLabel, message;
	std::shared_ptr<DropdownWidget> styleDropdown, tilesetDropdown, playersDropdown, sizeDropdown, terrainDropdown;
	std::shared_ptr<W_LABEL> tilesetLabel, playersLabel, sizeLabel, terrainLabel;
	std::vector<int> sizeValues;
	int sizeForPlayers = -1;
	std::array<std::shared_ptr<W_LABEL>, RandomMapSettings2126::SLIDER_COUNT> sliderLabels;
	std::array<std::shared_ptr<W_SLIDER>, RandomMapSettings2126::SLIDER_COUNT> sliders;
	std::array<int, RandomMapSettings2126::SLIDER_COUNT> sliderRowY = {};
	std::array<WzCachedText, RandomMapSettings2126::SLIDER_COUNT> valueText;
	std::shared_ptr<W_EDITBOX> codeBox;
	std::shared_ptr<FlatButton> copyButton, pasteButton, newButton, defaultsButton;
	RandomMapSettings2126 shown;
	std::string shownMap;
	bool pendingSend = false;
	uint32_t lastChange = 0;
	uint32_t messageTime = 0;
};

} // anonymous namespace

std::shared_ptr<WIDGET> makeRandomMapSettingsPanel2126(const std::function<void()> &onChanged)
{
	auto panel = std::make_shared<RandomMapPanel>();
	panel->initialize(onChanged);
	panel->setGeometry(0, 0, panel->idealWidth(), panel->idealHeight());
	return panel;
}

std::string randomMapSettingsSummary2126()
{
	std::string s = gettext(randomMapStyleName2126(game.randomMap.style));
	bool tuned = false;
	for (uint8_t v : game.randomMap.sliders)
	{
		tuned |= (v != RandomMapSettings2126::SLIDER_DEFAULT);
	}
	if (tuned)
	{
		s += ", " + std::string(_("tuned"));
	}
	return s;
}
