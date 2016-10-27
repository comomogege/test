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

#include "styles/style_widgets.h"

namespace Ui {

template <typename Widget>
class WidgetSlideWrap;

template <>
class WidgetSlideWrap<TWidget> : public TWidget {
public:
	WidgetSlideWrap(QWidget *parent, TWidget *entity
		, style::margins entityPadding
		, base::lambda_unique<void()> updateCallback
		, int duration = st::widgetSlideDuration);

	void slideUp();
	void slideDown();
	void showFast();
	void hideFast();

	TWidget *entity() {
		return _entity;
	}

	const TWidget *entity() const {
		return _entity;
	}

	int naturalWidth() const override;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
	int resizeGetHeight(int newWidth) override;

private:
	void step_height(float64 ms, bool timer);

	TWidget *_entity;
	bool _inResizeToWidth = false;
	style::margins _padding;
	int _duration;
	base::lambda_unique<void()> _updateCallback;

	style::size _realSize;
	int _forceHeight = -1;
	anim::ivalue a_height;
	Animation _a_height;
	bool _hiding = false;

};

template <typename Widget>
class WidgetSlideWrap : public WidgetSlideWrap<TWidget> {
public:
	WidgetSlideWrap(QWidget *parent, Widget *entity
		, style::margins entityPadding
		, base::lambda_unique<void()> updateCallback
		, int duration = st::widgetSlideDuration) : WidgetSlideWrap<TWidget>(parent, entity, entityPadding, std_::move(updateCallback), duration) {
	}
	Widget *entity() {
		return static_cast<Widget*>(WidgetSlideWrap<TWidget>::entity());
	}
	const Widget *entity() const {
		return static_cast<const Widget*>(WidgetSlideWrap<TWidget>::entity());
	}

};

} // namespace Ui
