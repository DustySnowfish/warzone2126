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
		styleDropdown->setListHeight(kControlH * RandomMapSettings2126::STYLE_COUNT);
		styleDropdown->setListBackgroundColor(pal_RGBA(18, 24, 30, 245));
		for (int i = 0; i < RandomMapSettings2126::STYLE_COUNT; ++i)
		{
			auto item = std::make_shared<W_LABEL>();
			item->setFont(font_regular, hud2126::value());
			item->setString(WzString::fromUtf8(std::string("  ") + gettext(randomMapStyleName2126(i))));
			item->setGeometry(0, 0, 200, kControlH);
			styleDropdown->addItem(item);
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
		styleDropdown->setTip(_("The kind of landscape. Random picks one of the five for each new map."));

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
		return kPad + 22 + 18 + 10 + kRowH * (1 + RandomMapSettings2126::SLIDER_COUNT) + 10 + kRowH + 22 + kRowH + kPad;
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
		// The landscape dropdown's box and arrow (the dropdown itself only draws its items)
		{
			const int dx0 = x0 + styleDropdown->x(), dy0 = y0 + styleDropdown->y();
			const int dx1 = dx0 + styleDropdown->width(), dy1 = dy0 + styleDropdown->height();
			const bool hot = canEdit() && styleDropdown->isMouseOverWidget();
			pie_UniTransBoxFill(dx0, dy0, dx1, dy1, hot ? hud2126::tabHover() : hud2126::tab());
			iV_Box(dx0, dy0, dx1 - 1, dy1 - 1, hot ? hud2126::accent() : hud2126::border());
			const int ax = dx1 - 18, ay = dy0 + styleDropdown->height() / 2 - 2;
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
		if (!current.has_value() || !(current.value() == decoded->first))
		{
			const std::string want = randomMapLevelName2126(decoded->first);
			setMessage(astringf(_("That code is for %s (%d players): pick that map first"), want.c_str(), decoded->first.players).c_str(), true);
			return;
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
		const std::string code = currentCode();
		if (!codeBox->isEditing())
		{
			codeBox->setString(WzString::fromUtf8(code));
		}
		auto identity = randomMapIdentity2126(game.map, game.maxPlayers);
		if (identity.has_value())
		{
			subtitle->setString(WzString::fromUtf8(astringf(_("%s, %d players. Every player gets this exact map."), game.map, static_cast<int>(game.maxPlayers))));
		}
		else
		{
			subtitle->setString(_("Pick one of the Random maps to use these settings"));
		}
		copyButton->setState(code.empty() ? WBUT_DISABLE : 0);
	}

	std::function<void()> onChanged;
	std::shared_ptr<W_LABEL> title, subtitle, styleLabel, codeLabel, message;
	std::shared_ptr<DropdownWidget> styleDropdown;
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
