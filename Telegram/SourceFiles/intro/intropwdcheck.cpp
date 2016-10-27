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
#include "intro/intropwdcheck.h"

#include "ui/filedialog.h"
#include "boxes/confirmbox.h"
#include "lang.h"
#include "application.h"
#include "intro/introsignup.h"

IntroPwdCheck::IntroPwdCheck(IntroWidget *parent) : IntroStep(parent)
, a_errorAlpha(0)
, _a_error(animation(this, &IntroPwdCheck::step_error))
, _next(this, lang(lng_intro_submit), st::btnIntroNext)
, _salt(parent->getPwdSalt())
, _hasRecovery(parent->getHasRecovery())
, _hint(parent->getPwdHint())
, _pwdField(this, st::inpIntroPassword, lang(lng_signin_password))
, _codeField(this, st::inpIntroPassword, lang(lng_signin_code))
, _toRecover(this, lang(lng_signin_recover))
, _toPassword(this, lang(lng_signin_try_password))
, _reset(this, lang(lng_signin_reset_account), st::btnRedLink)
, sentRequest(0) {
	setVisible(false);
	setGeometry(parent->innerRect());

	connect(&_next, SIGNAL(clicked()), this, SLOT(onSubmitPwd()));
	connect(&checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));
	connect(&_toRecover, SIGNAL(clicked()), this, SLOT(onToRecover()));
	connect(&_toPassword, SIGNAL(clicked()), this, SLOT(onToPassword()));
	connect(&_pwdField, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(&_codeField, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(&_reset, SIGNAL(clicked()), this, SLOT(onReset()));

	_pwdField.setEchoMode(QLineEdit::Password);

	if (!_hint.isEmpty()) {
		_hintText.setText(st::introFont, lng_signin_hint(lt_password_hint, _hint));
	}
	_codeField.hide();
	_toPassword.hide();
	_toRecover.show();
	_reset.hide();

	setMouseTracking(true);
}

void IntroPwdCheck::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	if (trivial || e->rect().intersects(textRect)) {
		p.setFont(st::introHeaderFont->f);
		p.drawText(textRect, lang(lng_signin_title), style::al_top);
		p.setFont(st::introFont->f);
		p.drawText(textRect, lang(_pwdField.isHidden() ? lng_signin_recover_desc : lng_signin_desc), style::al_bottom);
	}
	if (_pwdField.isHidden()) {
		if (!_emailPattern.isEmpty()) {
			p.drawText(QRect(textRect.x(), _pwdField.y() + _pwdField.height() + st::introFinishSkip, textRect.width(), st::introFont->height), _emailPattern, style::al_top);
		}
	} else if (!_hint.isEmpty()) {
		_hintText.drawElided(p, _pwdField.x(), _pwdField.y() + _pwdField.height() + st::introFinishSkip, _pwdField.width(), 1, style::al_top);
	}
	if (_a_error.animating() || error.length()) {
		p.setOpacity(a_errorAlpha.current());

		QRect errRect((width() - st::introErrWidth) / 2, (_pwdField.y() + _pwdField.height() + st::introFinishSkip + st::introFont->height + _next.y() - st::introErrHeight) / 2, st::introErrWidth, st::introErrHeight);
		p.setFont(st::introErrFont->f);
		p.setPen(st::introErrColor->p);
		p.drawText(errRect, error, QTextOption(style::al_center));

		p.setOpacity(1);
	}
}

void IntroPwdCheck::resizeEvent(QResizeEvent *e) {
	if (e->oldSize().width() != width()) {
		_next.move((width() - _next.width()) / 2, st::introBtnTop);
		_pwdField.move((width() - _pwdField.width()) / 2, st::introTextTop + st::introTextSize.height() + st::introCountry.top);
		_codeField.move((width() - _codeField.width()) / 2, st::introTextTop + st::introTextSize.height() + st::introCountry.top);
		_toRecover.move(_next.x() + (_pwdField.width() - _toRecover.width()) / 2, _next.y() + _next.height() + st::introFinishSkip);
		_toPassword.move(_next.x() + (_pwdField.width() - _toPassword.width()) / 2, _next.y() + _next.height() + st::introFinishSkip);
		_reset.move((width() - _reset.width()) / 2, _toRecover.y() + _toRecover.height() + st::introFinishSkip);
	}
	textRect = QRect((width() - st::introTextSize.width()) / 2, st::introTextTop, st::introTextSize.width(), st::introTextSize.height());
}

void IntroPwdCheck::showError(const QString &err) {
	if (!_a_error.animating() && err == error) return;

	if (err.length()) {
		error = err;
		a_errorAlpha.start(1);
	} else {
		a_errorAlpha.start(0);
	}
	_a_error.start();
}

void IntroPwdCheck::step_error(float64 ms, bool timer) {
	float64 dt = ms / st::introErrDuration;

	if (dt >= 1) {
		_a_error.stop();
		a_errorAlpha.finish();
		if (!a_errorAlpha.current()) {
			error.clear();
		}
	} else {
		a_errorAlpha.update(dt, st::introErrFunc);
	}
	if (timer) update();
}

void IntroPwdCheck::activate() {
	IntroStep::activate();
	if (_pwdField.isHidden()) {
		_codeField.setFocus();
	} else {
		_pwdField.setFocus();
	}
}

void IntroPwdCheck::cancelled() {
	if (sentRequest) {
		MTP::cancel(sentRequest);
		sentRequest = 0;
	}
}

void IntroPwdCheck::stopCheck() {
	checkRequest.stop();
}

void IntroPwdCheck::onCheckRequest() {
	int32 status = MTP::state(sentRequest);
	if (status < 0) {
		int32 leftms = -status;
		if (leftms >= 1000) {
			MTP::cancel(sentRequest);
			sentRequest = 0;
			if (!_pwdField.isEnabled()) {
				_pwdField.setDisabled(false);
				_codeField.setDisabled(false);
				activate();
			}
		}
	}
	if (!sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void IntroPwdCheck::pwdSubmitDone(bool recover, const MTPauth_Authorization &result) {
	sentRequest = 0;
	stopCheck();
	if (recover) {
		cSetPasswordRecovered(true);
	}
	_pwdField.setDisabled(false);
	_codeField.setDisabled(false);
	const auto &d(result.c_auth_authorization());
	if (d.vuser.type() != mtpc_user || !d.vuser.c_user().is_self()) { // wtf?
		showError(lang(lng_server_error));
		return;
	}
	intro()->finish(d.vuser);
}

bool IntroPwdCheck::pwdSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		sentRequest = 0;
		stopCheck();
		_codeField.setDisabled(false);
		showError(lang(lng_flood_error));
		_pwdField.setDisabled(false);
		_pwdField.notaBene();
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	sentRequest = 0;
	stopCheck();
	_pwdField.setDisabled(false);
	_codeField.setDisabled(false);
	const QString &err = error.type();
	if (err == qstr("PASSWORD_HASH_INVALID")) {
		showError(lang(lng_signin_bad_password));
		_pwdField.selectAll();
		_pwdField.notaBene();
		return true;
	} else if (err == qstr("PASSWORD_EMPTY")) {
		intro()->onBack();
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	_pwdField.setFocus();
	return false;
}

bool IntroPwdCheck::codeSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		showError(lang(lng_flood_error));
		_codeField.notaBene();
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	sentRequest = 0;
	stopCheck();
	_pwdField.setDisabled(false);
	_codeField.setDisabled(false);
	const QString &err = error.type();
	if (err == qstr("PASSWORD_EMPTY")) {
		intro()->onBack();
		return true;
	} else if (err == qstr("PASSWORD_RECOVERY_NA")) {
		recoverStartFail(error);
		return true;
	} else if (err == qstr("PASSWORD_RECOVERY_EXPIRED")) {
		_emailPattern = QString();
		onToPassword();
		return true;
	} else if (err == qstr("CODE_INVALID")) {
		showError(lang(lng_signin_wrong_code));
		_codeField.selectAll();
		_codeField.notaBene();
		return true;
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	_codeField.setFocus();
	return false;
}

void IntroPwdCheck::recoverStarted(const MTPauth_PasswordRecovery &result) {
	_emailPattern = st::introFont->elided(lng_signin_recover_hint(lt_recover_email, qs(result.c_auth_passwordRecovery().vemail_pattern)), textRect.width());
	update();
}

bool IntroPwdCheck::recoverStartFail(const RPCError &error) {
	stopCheck();
	_pwdField.setDisabled(false);
	_codeField.setDisabled(false);
	_pwdField.show();
	_codeField.hide();
	_pwdField.setFocus();
	update();
	showError(QString());
	return true;
}

void IntroPwdCheck::onToRecover() {
	if (_hasRecovery) {
		if (sentRequest) {
			MTP::cancel(sentRequest);
			sentRequest = 0;
		}
		showError(QString());
		_toRecover.hide();
		_toPassword.show();
		_pwdField.hide();
		_pwdField.setText(QString());
		_codeField.show();
		_codeField.setFocus();
		if (_emailPattern.isEmpty()) {
			MTP::send(MTPauth_RequestPasswordRecovery(), rpcDone(&IntroPwdCheck::recoverStarted), rpcFail(&IntroPwdCheck::recoverStartFail));
		}
		update();
	} else {
		ConfirmBox *box = new InformBox(lang(lng_signin_no_email_forgot));
		Ui::showLayer(box);
		connect(box, SIGNAL(destroyed(QObject*)), this, SLOT(onToReset()));
	}
}

void IntroPwdCheck::onToPassword() {
	ConfirmBox *box = new InformBox(lang(lng_signin_cant_email_forgot));
	Ui::showLayer(box);
	connect(box, SIGNAL(destroyed(QObject*)), this, SLOT(onToReset()));
}

void IntroPwdCheck::onToReset() {
	if (sentRequest) {
		MTP::cancel(sentRequest);
		sentRequest = 0;
	}
	_toRecover.show();
	_toPassword.hide();
	_pwdField.show();
	_codeField.hide();
	_codeField.setText(QString());
	_pwdField.setFocus();
	_reset.show();
	update();
}

void IntroPwdCheck::onReset() {
	if (sentRequest) return;
	ConfirmBox *box = new ConfirmBox(lang(lng_signin_sure_reset), lang(lng_signin_reset), st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onResetSure()));
	Ui::showLayer(box);
}

void IntroPwdCheck::onResetSure() {
	if (sentRequest) return;
	sentRequest = MTP::send(MTPaccount_DeleteAccount(MTP_string("Forgot password")), rpcDone(&IntroPwdCheck::deleteDone), rpcFail(&IntroPwdCheck::deleteFail));
}

bool IntroPwdCheck::deleteFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	sentRequest = 0;

	auto type = error.type();
	if (type.startsWith(qstr("2FA_CONFIRM_WAIT_"))) {
		int seconds = type.mid(qstr("2FA_CONFIRM_WAIT_").size()).toInt();
		int days = (seconds + 59) / 86400;
		int hours = ((seconds + 59) % 86400) / 3600;
		int minutes = ((seconds + 59) % 3600) / 60;
		QString when;
		if (days > 0) {
			when = lng_signin_reset_in_days(lt_count_days, days, lt_count_hours, hours, lt_count_minutes, minutes);
		} else if (hours > 0) {
			when = lng_signin_reset_in_hours(lt_count_hours, hours, lt_count_minutes, minutes);
		} else {
			when = lng_signin_reset_in_minutes(lt_count_minutes, minutes);
		}
		Ui::showLayer(new InformBox(lng_signin_reset_wait(lt_phone_number, App::formatPhone(intro()->getPhone()), lt_when, when)));
	} else if (type == qstr("2FA_RECENT_CONFIRM")) {
		Ui::showLayer(new InformBox(lang(lng_signin_reset_cancelled)));
	} else {
		Ui::hideLayer();
		showError(lang(lng_server_error));
	}
	return true;
}

void IntroPwdCheck::deleteDone(const MTPBool &v) {
	Ui::hideLayer();
	intro()->replaceStep(new IntroSignup(intro()));
}

void IntroPwdCheck::onInputChange() {
	showError(QString());
}

void IntroPwdCheck::onSubmitPwd(bool force) {
	if (sentRequest) return;
	if (_pwdField.isHidden()) {
		if (!force && !_codeField.isEnabled()) return;
		QString code = _codeField.text().trimmed();
		if (code.isEmpty()) {
			_codeField.notaBene();
			return;
		}

		sentRequest = MTP::send(MTPauth_RecoverPassword(MTP_string(code)), rpcDone(&IntroPwdCheck::pwdSubmitDone, true), rpcFail(&IntroPwdCheck::codeSubmitFail));
	} else {
		if (!force && !_pwdField.isEnabled()) return;

		_pwdField.setDisabled(true);
		setFocus();

		showError(QString());

		QByteArray pwdData = _salt + _pwdField.text().toUtf8() + _salt, pwdHash(32, Qt::Uninitialized);
		hashSha256(pwdData.constData(), pwdData.size(), pwdHash.data());
		sentRequest = MTP::send(MTPauth_CheckPassword(MTP_bytes(pwdHash)), rpcDone(&IntroPwdCheck::pwdSubmitDone, false), rpcFail(&IntroPwdCheck::pwdSubmitFail));
	}
}

void IntroPwdCheck::onSubmit() {
	onSubmitPwd();
}
