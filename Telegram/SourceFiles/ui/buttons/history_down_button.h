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

#include "ui/button.h"

namespace Ui {

class HistoryDownButton : public Button {
public:
	HistoryDownButton(QWidget *parent);

	void setUnreadCount(int unreadCount);
	int unreadCount() const {
		return _unreadCount;
	}

	bool hidden() const;

	void showAnimated();
	void hideAnimated();

	void finishAnimation();

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(int oldState, ButtonStateChangeSource source) override;

private:
	void toggleAnimated();
	void step_arrowOver(float64 ms, bool timer);

	QPixmap _cache;
	bool _shown = false;

	anim::fvalue a_arrowOpacity;
	Animation _a_arrowOver;

	FloatAnimation _a_show;

	int _unreadCount = 0;

};

} // namespace Ui
