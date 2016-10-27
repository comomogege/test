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
#include "settings/settings_info_widget.h"

#include "styles/style_settings.h"
#include "lang.h"
#include "ui/flatlabel.h"
#include "ui/effects/widget_slide_wrap.h"
#include "boxes/usernamebox.h"
#include "observer_peer.h"

namespace Settings {

using UpdateFlag = Notify::PeerUpdate::Flag;

InfoWidget::InfoWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_info)) {
	auto observeEvents = UpdateFlag::UsernameChanged | UpdateFlag::UserPhoneChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	createControls();
}

void InfoWidget::createControls() {
	style::margins margin(0, -st::settingsBlockOneLineTextPart.margin.top(), 0, st::settingsSmallSkip - st::settingsBlockOneLineTextPart.margin.bottom());
	style::margins slidedPadding(0, st::settingsSmallSkip / 2, 0, st::settingsSmallSkip - (st::settingsSmallSkip / 2));
	addChildRow(_mobileNumber, margin, slidedPadding);
	addChildRow(_username, margin, slidedPadding);
	addChildRow(_link, margin, slidedPadding);
	if (self()->username.isEmpty()) {
		_link->hideFast();
	}
	refreshControls();
}

void InfoWidget::refreshControls() {
	refreshMobileNumber();
	refreshUsername();
	refreshLink();
}

void InfoWidget::refreshMobileNumber() {
	TextWithEntities phoneText;
	if (auto user = self()->asUser()) {
		if (!user->phone().isEmpty()) {
			phoneText.text = App::formatPhone(user->phone());
		} else {
			phoneText.text = App::phoneFromSharedContact(peerToUser(user->id));
		}
	}
	setLabeledText(_mobileNumber, lang(lng_profile_mobile_number), phoneText, TextWithEntities(), lang(lng_profile_copy_phone));
}

void InfoWidget::refreshUsername() {
	TextWithEntities usernameText;
	QString copyText;
	if (self()->username.isEmpty()) {
		usernameText.text = lang(lng_settings_choose_username);
	} else {
		usernameText.text = '@' + self()->username;
		copyText = lang(lng_context_copy_mention);
	}
	usernameText.entities.push_back(EntityInText(EntityInTextCustomUrl, 0, usernameText.text.size(), qsl("https://telegram.me/") + self()->username));
	setLabeledText(_username, lang(lng_profile_username), usernameText, TextWithEntities(), copyText);
	if (auto text = _username->entity()->textLabel()) {
		text->setClickHandlerHook([](const ClickHandlerPtr &handler, Qt::MouseButton button) {
			Ui::showLayer(new UsernameBox());
			return false;
		});
	}
}

void InfoWidget::refreshLink() {
	TextWithEntities linkText;
	TextWithEntities linkTextShort;
	if (!self()->username.isEmpty()) {
		linkText.text = qsl("https://telegram.me/") + self()->username;
		linkText.entities.push_back(EntityInText(EntityInTextUrl, 0, linkText.text.size()));
		linkTextShort.text = qsl("telegram.me/") + self()->username;
		linkTextShort.entities.push_back(EntityInText(EntityInTextCustomUrl, 0, linkTextShort.text.size(), qsl("https://telegram.me/") + self()->username));
	}
	setLabeledText(_link, lang(lng_profile_link), linkText, linkTextShort, QString());
	if (auto text = _link->entity()->textLabel()) {
		text->setClickHandlerHook([](const ClickHandlerPtr &handler, Qt::MouseButton button) {
			Ui::showLayer(new UsernameBox());
			return false;
		});
	}
	if (auto shortText = _link->entity()->shortTextLabel()) {
		shortText->setExpandLinksMode(ExpandLinksUrlOnly);
		shortText->setClickHandlerHook([](const ClickHandlerPtr &handler, Qt::MouseButton button) {
			Ui::showLayer(new UsernameBox());
			return false;
		});
	}
}

void InfoWidget::setLabeledText(ChildWidget<LabeledWrap> &row, const QString &label, const TextWithEntities &textWithEntities, const TextWithEntities &shortTextWithEntities, const QString &copyText) {
	if (textWithEntities.text.isEmpty()) {
		row->slideUp();
	} else {
		row->entity()->setLabeledText(label, textWithEntities, shortTextWithEntities, copyText);
		row->slideDown();
	}
}

InfoWidget::LabeledWidget::LabeledWidget(QWidget *parent) : TWidget(parent) {
}

void InfoWidget::LabeledWidget::setLabeledText(const QString &label, const TextWithEntities &textWithEntities, const TextWithEntities &shortTextWithEntities, const QString &copyText) {
	_label.destroy();
	_text.destroy();
	_shortText.destroy();
	if (textWithEntities.text.isEmpty()) return;

	_label = new FlatLabel(this, label, FlatLabel::InitType::Simple, st::settingsBlockLabel);
	_label->show();
	setLabelText(_text, textWithEntities, copyText);
	setLabelText(_shortText, shortTextWithEntities, copyText);
	resizeToWidth(width());
}

void InfoWidget::LabeledWidget::setLabelText(ChildWidget<FlatLabel> &text, const TextWithEntities &textWithEntities, const QString &copyText) {
	text.destroy();
	if (textWithEntities.text.isEmpty()) return;

	text = new FlatLabel(this, QString(), FlatLabel::InitType::Simple, st::settingsBlockOneLineTextPart);
	text->show();
	text->setMarkedText(textWithEntities);
	text->setContextCopyText(copyText);
	text->setSelectable(true);
	text->setDoubleClickSelectsParagraph(true);
}

void InfoWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != self()) {
		return;
	}

	if (update.flags & UpdateFlag::UsernameChanged) {
		refreshUsername();
		refreshLink();
	}
	if (update.flags & (UpdateFlag::UserPhoneChanged)) {
		refreshMobileNumber();
	}

	contentSizeUpdated();
}

int InfoWidget::LabeledWidget::naturalWidth() const {
	if (!_text) return -1;
	return _label->naturalWidth() + st::normalFont->spacew + _text->naturalWidth();
}

int InfoWidget::LabeledWidget::resizeGetHeight(int newWidth) {
	int marginLeft = st::settingsBlockOneLineTextPart.margin.left();
	int marginRight = st::settingsBlockOneLineTextPart.margin.right();

	if (!_label) return 0;

	_label->moveToLeft(0, st::settingsBlockOneLineTextPart.margin.top(), newWidth);
	auto labelNatural = _label->naturalWidth();
	t_assert(labelNatural >= 0);

	_label->resize(qMin(newWidth, labelNatural), _label->height());

	int textLeft = _label->width() + st::normalFont->spacew;
	int textWidth = _text->naturalWidth();
	int availableWidth = newWidth - textLeft;
	bool doesNotFit = (textWidth > availableWidth);
	accumulate_min(textWidth, availableWidth);
	accumulate_min(textWidth, st::msgMaxWidth);
	if (textWidth + marginLeft + marginRight < 0) {
		textWidth = -(marginLeft + marginRight);
	}
	_text->resizeToWidth(textWidth + marginLeft + marginRight);
	_text->moveToLeft(textLeft - marginLeft, 0, newWidth);
	if (_shortText) {
		_shortText->resizeToWidth(textWidth + marginLeft + marginRight);
		_shortText->moveToLeft(textLeft - marginLeft, 0, newWidth);
		if (doesNotFit) {
			_shortText->show();
			_text->hide();
		} else {
			_shortText->hide();
			_text->show();
		}
	}
	return st::settingsBlockOneLineTextPart.margin.top() + _label->height() + st::settingsBlockOneLineTextPart.margin.bottom();
}

} // namespace Settings
