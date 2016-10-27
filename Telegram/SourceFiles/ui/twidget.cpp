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

#include "application.h"
#include "mainwindow.h"

namespace Fonts {

	bool Started = false;
	void start() {
		if (!Started) {
			Started = true;

			QFontDatabase::addApplicationFont(qsl(":/gui/art/fonts/OpenSans-Regular.ttf"));
			QFontDatabase::addApplicationFont(qsl(":/gui/art/fonts/OpenSans-Bold.ttf"));
			QFontDatabase::addApplicationFont(qsl(":/gui/art/fonts/OpenSans-Semibold.ttf"));
		}
	}

}

namespace {
	void _sendResizeEvents(QWidget *target) {
		QResizeEvent e(target->size(), QSize());
		QApplication::sendEvent(target, &e);

		const QObjectList children = target->children();
		for (int i = 0; i < children.size(); ++i) {
			QWidget *child = static_cast<QWidget*>(children.at(i));
			if (child->isWidgetType() && !child->isWindow() && child->testAttribute(Qt::WA_PendingResizeEvent)) {
				_sendResizeEvents(child);
			}
		}
	}
}

bool TWidget::inFocusChain() const {
	return !isHidden() && App::wnd() && (App::wnd()->focusWidget() == this || isAncestorOf(App::wnd()->focusWidget()));
}

void myEnsureResized(QWidget *target) {
	if (target && (target->testAttribute(Qt::WA_PendingResizeEvent) || !target->testAttribute(Qt::WA_WState_Created))) {
		_sendResizeEvents(target);
	}
}

QPixmap myGrab(TWidget *target, QRect rect) {
	myEnsureResized(target);
	if (rect.isNull()) rect = target->rect();

    QPixmap result(rect.size() * cRetinaFactor());
    result.setDevicePixelRatio(cRetinaFactor());
    result.fill(Qt::transparent);

	target->grabStart();
    target->render(&result, QPoint(), QRegion(rect), QWidget::DrawChildren | QWidget::IgnoreMask);
	target->grabFinish();

	return result;
}

void sendSynteticMouseEvent(QWidget *widget, QEvent::Type type, Qt::MouseButton button, const QPoint &globalPoint) {
	if (auto windowHandle = widget->window()->windowHandle()) {
		auto localPoint = windowHandle->mapFromGlobal(globalPoint);
		QMouseEvent ev(type
			, localPoint
			, localPoint
			, globalPoint
			, button
			, QGuiApplication::mouseButtons() | button
			, QGuiApplication::keyboardModifiers()
#ifndef OS_MAC_OLD
			, Qt::MouseEventSynthesizedByApplication
#endif // OS_MAC_OLD
		);
		ev.setTimestamp(getms());
		QGuiApplication::sendEvent(windowHandle, &ev);
	}
}
