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
#include "settings/settings_privacy_widget.h"

#include "ui/effects/widget_slide_wrap.h"
#include "styles/style_settings.h"
#include "lang.h"
#include "application.h"
#include "boxes/sessionsbox.h"
#include "boxes/passcodebox.h"
#include "boxes/autolockbox.h"
#include "pspecific.h"

namespace Settings {

LocalPasscodeState::LocalPasscodeState(QWidget *parent) : TWidget(parent)
, _edit(this, lang(Global::LocalPasscode() ? lng_passcode_change : lng_passcode_turn_on), st::defaultBoxLinkButton)
, _turnOff(this, lang(lng_passcode_turn_off), st::defaultBoxLinkButton) {
	updateControls();
	connect(_edit, SIGNAL(clicked()), this, SLOT(onEdit()));
	connect(_turnOff, SIGNAL(clicked()), this, SLOT(onTurnOff()));
	subscribe(Global::RefLocalPasscodeChanged(), [this]() { updateControls(); });
}

int LocalPasscodeState::resizeGetHeight(int newWidth) {
	_edit->moveToLeft(0, 0, newWidth);
	_turnOff->moveToRight(0, 0, newWidth);
	return _edit->height();
}

void LocalPasscodeState::onEdit() {
	Ui::showLayer(new PasscodeBox());
}

void LocalPasscodeState::onTurnOff() {
	Ui::showLayer(new PasscodeBox(true));
}

void LocalPasscodeState::updateControls() {
	_edit->setText(lang(Global::LocalPasscode() ? lng_passcode_change : lng_passcode_turn_on));
	_edit->moveToLeft(0, 0);
	_turnOff->setVisible(Global::LocalPasscode());
}

CloudPasswordState::CloudPasswordState(QWidget *parent) : TWidget(parent)
, _edit(this, lang(lng_cloud_password_set), st::defaultBoxLinkButton)
, _turnOff(this, lang(lng_passcode_turn_off), st::defaultBoxLinkButton) {
	_turnOff->hide();
	connect(_edit, SIGNAL(clicked()), this, SLOT(onEdit()));
	connect(_turnOff, SIGNAL(clicked()), this, SLOT(onTurnOff()));
	Sandbox::connect(SIGNAL(applicationStateChanged(Qt::ApplicationState)), this, SLOT(onReloadPassword(Qt::ApplicationState)));
	onReloadPassword();
}

int CloudPasswordState::resizeGetHeight(int newWidth) {
	_edit->moveToLeft(0, 0, newWidth);
	_turnOff->moveToRight(0, 0, newWidth);
	return _edit->height();
}

void CloudPasswordState::onEdit() {
	PasscodeBox *box = new PasscodeBox(_newPasswordSalt, _curPasswordSalt, _hasPasswordRecovery, _curPasswordHint);
	connect(box, SIGNAL(reloadPassword()), this, SLOT(onReloadPassword()));
	Ui::showLayer(box);
}

void CloudPasswordState::onTurnOff() {
	if (_curPasswordSalt.isEmpty()) {
		_turnOff->hide();

		MTPDaccount_passwordInputSettings::Flags flags = MTPDaccount_passwordInputSettings::Flag::f_email;
		MTPaccount_PasswordInputSettings settings(MTP_account_passwordInputSettings(MTP_flags(flags), MTP_bytes(QByteArray()), MTP_bytes(QByteArray()), MTP_string(QString()), MTP_string(QString())));
		MTP::send(MTPaccount_UpdatePasswordSettings(MTP_bytes(QByteArray()), settings), rpcDone(&CloudPasswordState::offPasswordDone), rpcFail(&CloudPasswordState::offPasswordFail));
	} else {
		PasscodeBox *box = new PasscodeBox(_newPasswordSalt, _curPasswordSalt, _hasPasswordRecovery, _curPasswordHint, true);
		connect(box, SIGNAL(reloadPassword()), this, SLOT(onReloadPassword()));
		Ui::showLayer(box);
	}
}

void CloudPasswordState::onReloadPassword(Qt::ApplicationState state) {
	if (state == Qt::ApplicationActive) {
		MTP::send(MTPaccount_GetPassword(), rpcDone(&CloudPasswordState::getPasswordDone));
	}
}

void CloudPasswordState::getPasswordDone(const MTPaccount_Password &result) {
	_waitingConfirm = QString();

	switch (result.type()) {
	case mtpc_account_noPassword: {
		auto &d = result.c_account_noPassword();
		_curPasswordSalt = QByteArray();
		_hasPasswordRecovery = false;
		_curPasswordHint = QString();
		_newPasswordSalt = qba(d.vnew_salt);
		auto pattern = qs(d.vemail_unconfirmed_pattern);
		if (!pattern.isEmpty()) {
			_waitingConfirm = lng_cloud_password_waiting(lt_email, pattern);
		}
	} break;

	case mtpc_account_password: {
		auto &d = result.c_account_password();
		_curPasswordSalt = qba(d.vcurrent_salt);
		_hasPasswordRecovery = mtpIsTrue(d.vhas_recovery);
		_curPasswordHint = qs(d.vhint);
		_newPasswordSalt = qba(d.vnew_salt);
		auto pattern = qs(d.vemail_unconfirmed_pattern);
		if (!pattern.isEmpty()) {
			_waitingConfirm = lng_cloud_password_waiting(lt_email, pattern);
		}
	} break;
	}
	_edit->setText(lang(_curPasswordSalt.isEmpty() ? lng_cloud_password_set : lng_cloud_password_edit));
	_edit->setVisible(_waitingConfirm.isEmpty());
	_turnOff->setVisible(!_waitingConfirm.isEmpty() || !_curPasswordSalt.isEmpty());
	update();

	_newPasswordSalt.resize(_newPasswordSalt.size() + 8);
	memset_rand(_newPasswordSalt.data() + _newPasswordSalt.size() - 8, 8);
}

void CloudPasswordState::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto text = st::linkFont->elided(_waitingConfirm, width() - _turnOff->width());
	if (!text.isEmpty()) {
		p.setPen(st::windowTextFg);
		p.setFont(st::boxTextFont);
		p.drawTextLeft(0, 0, width(), text);
	}
}

void CloudPasswordState::offPasswordDone(const MTPBool &result) {
	onReloadPassword();
}

bool CloudPasswordState::offPasswordFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	onReloadPassword();
	return true;
}

PrivacyWidget::PrivacyWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_privacy)) {
	createControls();
	subscribe(Global::RefLocalPasscodeChanged(), [this]() { autoLockUpdated(); });
}

void PrivacyWidget::createControls() {
	style::margins marginSmall(0, 0, 0, st::settingsSmallSkip);
	style::margins marginSkip(0, 0, 0, st::settingsSkip);
	style::margins slidedPadding(0, marginSmall.bottom() / 2, 0, marginSmall.bottom() - (marginSmall.bottom() / 2));

	addChildRow(_localPasscodeState, marginSmall);
	auto label = lang(psIdleSupported() ? lng_passcode_autolock_away : lng_passcode_autolock_inactive);
	auto value = (Global::AutoLock() % 3600) ? lng_passcode_autolock_minutes(lt_count, Global::AutoLock() / 60) : lng_passcode_autolock_hours(lt_count, Global::AutoLock() / 3600);
	addChildRow(_autoLock, marginSmall, slidedPadding, label, value, LabeledLink::Type::Primary, SLOT(onAutoLock()));
	if (!Global::LocalPasscode()) {
		_autoLock->hideFast();
	}
	addChildRow(_cloudPasswordState, marginSmall);
	addChildRow(_showAllSessions, marginSmall, lang(lng_settings_show_sessions), SLOT(onShowSessions()));
}

void PrivacyWidget::autoLockUpdated() {
	if (Global::LocalPasscode()) {
		auto value = (Global::AutoLock() % 3600) ? lng_passcode_autolock_minutes(lt_count, Global::AutoLock() / 60) : lng_passcode_autolock_hours(lt_count, Global::AutoLock() / 3600);
		_autoLock->entity()->link()->setText(value);
		resizeToWidth(width());
		_autoLock->slideDown();
	} else {
		_autoLock->slideUp();
	}
}

void PrivacyWidget::onAutoLock() {
	Ui::showLayer(new AutoLockBox());
}

void PrivacyWidget::onShowSessions() {
	Ui::showLayer(new SessionsBox());
}

} // namespace Settings
