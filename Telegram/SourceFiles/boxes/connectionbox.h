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

class ConnectionBox : public AbstractBox {
	Q_OBJECT

public:
	ConnectionBox();

public slots:
	void onChange();
	void onSubmit();
	void onSave();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void showAll() override;
	void doSetInnerFocus() override;

private:
	InputField _hostInput;
	PortInput _portInput;
	InputField _userInput;
	PasswordField _passwordInput;
	Radiobutton _autoRadio, _httpProxyRadio, _tcpProxyRadio;
	Checkbox _tryIPv6;

	BoxButton _save, _cancel;

};

class AutoDownloadBox : public AbstractBox {
	Q_OBJECT

public:
	AutoDownloadBox();

public slots:
	void onSave();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void showAll() override;

private:
	Checkbox _photoPrivate, _photoGroups;
	Checkbox _audioPrivate, _audioGroups;
	Checkbox _gifPrivate, _gifGroups, _gifPlay;

	int32 _sectionHeight;

	BoxButton _save, _cancel;

};
