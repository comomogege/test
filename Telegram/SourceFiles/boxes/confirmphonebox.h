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

#include "boxes/abstractbox.h"

class FlatLabel;

class ConfirmPhoneBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	static void start(const QString &phone, const QString &hash);

	~ConfirmPhoneBox();

private slots:
	void onCallStatusTimer();
	void onSendCode();
	void onCodeChanged();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void showAll() override {
		showChildren();
	}
	void doSetInnerFocus() override {
		_code->setFocus();
	}

private:
	ConfirmPhoneBox(QWidget *parent, const QString &phone, const QString &hash);
	void sendCodeDone(const MTPauth_SentCode &result);
	bool sendCodeFail(const RPCError &error);

	void callDone(const MTPauth_SentCode &result);

	void confirmDone(const MTPBool &result);
	bool confirmFail(const RPCError &error);

	QString getPhone() const {
		return _phone;
	}
	void launch();

	enum CallState {
		Waiting,
		Calling,
		Called,
		Disabled,
	};
	struct CallStatus {
		CallState state;
		int timeout;
	};
	void setCallStatus(const CallStatus &status);
	QString getCallText() const;

	void showError(const QString &error);

	mtpRequestId _sendCodeRequestId = 0;

	// _hash from the link for account.sendConfirmPhoneCode call.
	// _phoneHash from auth.sentCode for account.confirmPhone call.
	QString _phone, _hash;
	QString _phoneHash;

	// If we receive the code length, we autosubmit _code field when enough symbols is typed.
	int _sentCodeLength = 0;

	mtpRequestId _checkCodeRequestId = 0;

	ChildWidget<FlatLabel> _about = { nullptr };
	ChildWidget<BoxButton> _send = { nullptr };
	ChildWidget<BoxButton> _cancel = { nullptr };
	ChildWidget<InputField> _code = { nullptr };

	// Flag for not calling onTextChanged() recursively.
	bool _fixing = false;
	QString _error;

	CallStatus _callStatus;
	QTimer _callTimer;

};
