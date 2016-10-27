/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "settings/settings_chat_settings_widget.h"

#include "styles/style_settings.h"
#include "lang.h"
#include "ui/effects/widget_slide_wrap.h"
#include "ui/flatlabel.h"
#include "localstorage.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "boxes/emojibox.h"
#include "boxes/stickers_box.h"
#include "boxes/downloadpathbox.h"
#include "boxes/connectionbox.h"
#include "boxes/confirmbox.h"

namespace Settings {

LabeledLink::LabeledLink(QWidget *parent, const QString &label, const QString &text, Type type, const char *slot) : TWidget(parent)
, _label(this, label, FlatLabel::InitType::Simple, (type == Type::Primary) ? st::settingsPrimaryLabel : st::labelDefFlat)
, _link(this, text, (type == Type::Primary) ? st::defaultBoxLinkButton : st::btnDefLink) {
	connect(_link, SIGNAL(clicked()), parent, slot);
}

void LabeledLink::setLink(const QString &text) {
	_link.destroy();
	_link = new LinkButton(this, text);
}

int LabeledLink::naturalWidth() const {
	return _label->naturalWidth() + st::normalFont->spacew + _link->naturalWidth();
}

int LabeledLink::resizeGetHeight(int newWidth) {
	_label->moveToLeft(0, 0, newWidth);
	_link->resizeToWidth(newWidth - st::normalFont->spacew - _label->width());
	_link->moveToLeft(_label->width() + st::normalFont->spacew, 0, newWidth);
	return _label->height();
}

DownloadPathState::DownloadPathState(QWidget *parent) : TWidget(parent)
, _path(this, lang(lng_download_path_label), downloadPathText(), LabeledLink::Type::Secondary, SLOT(onDownloadPath()))
, _clear(this, lang(lng_download_path_clear)) {
	connect(_clear, SIGNAL(clicked()), this, SLOT(onClear()));
	connect(App::wnd(), SIGNAL(tempDirCleared(int)), this, SLOT(onTempDirCleared(int)));
	connect(App::wnd(), SIGNAL(tempDirClearFailed(int)), this, SLOT(onTempDirClearFailed(int)));
	subscribe(Global::RefDownloadPathChanged(), [this]() {
		_path->link()->setText(downloadPathText());
		resizeToWidth(width());
	});
	switch (App::wnd()->tempDirState()) {
	case MainWindow::TempDirEmpty: _state = State::Empty; break;
	case MainWindow::TempDirExists: _state = State::Exists; break;
	case MainWindow::TempDirRemoving: _state = State::Clearing; break;
	}
	updateControls();
}

int DownloadPathState::resizeGetHeight(int newWidth) {
	_path->resizeToWidth(qMin(newWidth, _path->naturalWidth()));
	_path->moveToLeft(0, 0, newWidth);
	_clear->moveToRight(0, 0, newWidth);
	return _path->height();
}

void DownloadPathState::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto text = ([this]() -> QString {
		switch (_state) {
		case State::Clearing: return lang(lng_download_path_clearing);
		case State::Cleared: return lang(lng_download_path_cleared);
		case State::ClearFailed: return lang(lng_download_path_clear_failed);
		}
		return QString();
	})();
	if (!text.isEmpty()) {
		p.setFont(st::linkFont);
		p.setPen(st::windowTextFg);
		p.drawTextRight(0, 0, width(), text);
	}
}

void DownloadPathState::updateControls() {
	_clear->setVisible(_state == State::Exists);
	update();
}

QString DownloadPathState::downloadPathText() const {
	if (Global::DownloadPath().isEmpty()) {
		return lang(lng_download_path_default);
	} else if (Global::DownloadPath() == qsl("tmp")) {
		return lang(lng_download_path_temp);
	}
	return QDir::toNativeSeparators(Global::DownloadPath());
};

void DownloadPathState::onDownloadPath() {
	Ui::showLayer(new DownloadPathBox());
}

void DownloadPathState::onClear() {
	ConfirmBox *box = new ConfirmBox(lang(lng_sure_clear_downloads));
	connect(box, SIGNAL(confirmed()), this, SLOT(onClearSure()));
	Ui::showLayer(box);
}

void DownloadPathState::onClearSure() {
	Ui::hideLayer();
	App::wnd()->tempDirDelete(Local::ClearManagerDownloads);
	_state = State::Clearing;
	updateControls();
}

void DownloadPathState::onTempDirCleared(int task) {
	if (task & Local::ClearManagerDownloads) {
		_state = State::Cleared;
	}
	updateControls();
}

void DownloadPathState::onTempDirClearFailed(int task) {
	if (task & Local::ClearManagerDownloads) {
		_state = State::ClearFailed;
	}
	updateControls();
}

ChatSettingsWidget::ChatSettingsWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_chat_settings)) {
	createControls();
}

void ChatSettingsWidget::createControls() {
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins marginSkip(0, 0, 0, st::settingsSkip);
	style::margins marginSub(0, 0, 0, st::settingsSubSkip);
	style::margins slidedPadding(0, marginSub.bottom() / 2, 0, marginSub.bottom() - (marginSub.bottom() / 2));

	addChildRow(_replaceEmoji, marginSub, lang(lng_settings_replace_emojis), SLOT(onReplaceEmoji()), cReplaceEmojis());
	style::margins marginList(st::defaultBoxCheckbox.textPosition.x(), 0, 0, st::settingsSkip);
	addChildRow(_viewList, marginList, slidedPadding, lang(lng_settings_view_emojis), SLOT(onViewList()), st::btnDefLink);
	if (!cReplaceEmojis()) {
		_viewList->hideFast();
	}

	addChildRow(_dontAskDownloadPath, marginSub, lang(lng_download_path_dont_ask), SLOT(onDontAskDownloadPath()), !Global::AskDownloadPath());
	style::margins marginPath(st::defaultBoxCheckbox.textPosition.x(), 0, 0, st::settingsSkip);
	addChildRow(_downloadPath, marginPath, slidedPadding);
	if (Global::AskDownloadPath()) {
		_downloadPath->hideFast();
	}

	addChildRow(_sendByEnter, marginSmall, qsl("send_key"), 0, lang(lng_settings_send_enter), SLOT(onSendByEnter()), !cCtrlEnter());
	addChildRow(_sendByCtrlEnter, marginSkip, qsl("send_key"), 1, lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_settings_send_cmdenter : lng_settings_send_ctrlenter), SLOT(onSendByCtrlEnter()), cCtrlEnter());
	addChildRow(_automaticMediaDownloadSettings, marginSmall, lang(lng_media_auto_settings), SLOT(onAutomaticMediaDownloadSettings()));
	addChildRow(_manageStickerSets, marginSmall, lang(lng_stickers_you_have), SLOT(onManageStickerSets()));
}

void ChatSettingsWidget::onReplaceEmoji() {
	cSetReplaceEmojis(_replaceEmoji->checked());
	Local::writeUserSettings();

	if (_replaceEmoji->checked()) {
		_viewList->slideDown();
	} else {
		_viewList->slideUp();
	}
}

void ChatSettingsWidget::onViewList() {
	Ui::showLayer(new EmojiBox());
}

void ChatSettingsWidget::onDontAskDownloadPath() {
	Global::SetAskDownloadPath(!_dontAskDownloadPath->checked());
	Local::writeUserSettings();
	if (_dontAskDownloadPath->checked()) {
		_downloadPath->slideDown();
	} else {
		_downloadPath->slideUp();
	}
}

void ChatSettingsWidget::onSendByEnter() {
	if (_sendByEnter->checked()) {
		cSetCtrlEnter(false);
		if (App::main()) App::main()->ctrlEnterSubmitUpdated();
		Local::writeUserSettings();
	}
}

void ChatSettingsWidget::onSendByCtrlEnter() {
	if (_sendByCtrlEnter->checked()) {
		cSetCtrlEnter(true);
		if (App::main()) App::main()->ctrlEnterSubmitUpdated();
		Local::writeUserSettings();
	}
}

void ChatSettingsWidget::onAutomaticMediaDownloadSettings() {
	Ui::showLayer(new AutoDownloadBox());
}

void ChatSettingsWidget::onManageStickerSets() {
	Ui::showLayer(new StickersBox());
}

} // namespace Settings
