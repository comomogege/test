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
#pragma once

#include "abstractbox.h"

class PasscodeBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	PasscodeBox(bool turningOff = false);
	PasscodeBox(const QByteArray &newSalt, const QByteArray &curSalt, bool hasRecovery, const QString &hint, bool turningOff = false);

private slots:
	void onSave(bool force = false);
	void onBadOldPasscode();
	void onOldChanged();
	void onNewChanged();
	void onEmailChanged();
	void onForceNoMail();
	void onBoxDestroyed(QObject *obj);
	void onRecoverByEmail();
	void onRecoverExpired();
	void onSubmit();

signals:
	void reloadPassword();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void showAll() override;
	void doSetInnerFocus() override;

private:
	void init();

	void setPasswordDone(const MTPBool &result);
	bool setPasswordFail(const RPCError &error);

	void recoverStarted(const MTPauth_PasswordRecovery &result);
	bool recoverStartFail(const RPCError &error);

	void recover();
	QString _pattern;

	AbstractBox *_replacedBy;
	bool _turningOff, _cloudPwd;
	mtpRequestId _setRequest;

	QByteArray _newSalt, _curSalt;
	bool _hasRecovery, _skipEmailWarning = false;

	int32 _aboutHeight;

	QString _boxTitle;
	Text _about, _hintText;

	BoxButton _saveButton, _cancelButton;
	PasswordField _oldPasscode, _newPasscode, _reenterPasscode;
	InputField _passwordHint, _recoverEmail;
	LinkButton _recover;

	QString _oldError, _newError, _emailError;

};

class RecoverBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	RecoverBox(const QString &pattern);

public slots:
	void onSubmit();
	void onCodeChanged();

signals:
	void reloadPassword();
	void recoveryExpired();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void showAll() override;
	void doSetInnerFocus() override;

private:
	void codeSubmitDone(bool recover, const MTPauth_Authorization &result);
	bool codeSubmitFail(const RPCError &error);

	mtpRequestId _submitRequest;

	QString _pattern;

	BoxButton _saveButton, _cancelButton;
	InputField _recoverCode;

	QString _error;

};
