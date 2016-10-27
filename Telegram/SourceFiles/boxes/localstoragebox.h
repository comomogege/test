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

class BoxButton;
class LinkButton;

class LocalStorageBox : public AbstractBox {
	Q_OBJECT

public:
	LocalStorageBox();

private slots:
	void onClear();
	void onTempDirCleared(int task);
	void onTempDirClearFailed(int task);

protected:
	void paintEvent(QPaintEvent *e) override;

	void showAll() override;

private:
	void updateControls();
	void checkLocalStoredCounts();

	enum class State {
		Normal,
		Clearing,
		Cleared,
		ClearFailed,
	};
	State _state = State::Normal;

	ChildWidget<LinkButton> _clear;
	ChildWidget<BoxButton> _close;

	int _imagesCount = -1;
	int _audiosCount = -1;

};
