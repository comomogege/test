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
#include "lang.h"

#include "localstorage.h"

#include "abstractbox.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "styles/style_boxes.h"

void BlueTitleShadow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect());
	st::boxBlueTitleShadow.fill(p, QRect(r.left(), 0, r.width(), height()));
}

BlueTitleClose::BlueTitleClose(QWidget *parent) : Button(parent)
, a_iconFg(st::boxBlueCloseFg->c)
, _a_over(animation(this, &BlueTitleClose::step_over)) {
	resize(st::boxTitleHeight, st::boxTitleHeight);
	setCursor(style::cur_pointer);
	connect(this, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(onStateChange(int, ButtonStateChangeSource)));
}

void BlueTitleClose::onStateChange(int oldState, ButtonStateChangeSource source) {
	if ((oldState & StateOver) != (_state & StateOver)) {
		a_iconFg.start(((_state & StateOver) ? st::boxBlueCloseOverFg : st::boxBlueCloseFg)->c);
		_a_over.start();
	}
}

void BlueTitleClose::step_over(float64 ms, bool timer) {
	float64 dt = ms / st::boxBlueCloseDuration;
	if (dt >= 1) {
		_a_over.stop();
		a_iconFg.finish();
	} else {
		a_iconFg.update(dt, anim::linear);
	}
	if (timer) update((st::boxTitleHeight - st::boxBlueCloseIcon.width()) / 2, (st::boxTitleHeight - st::boxBlueCloseIcon.height()) / 2, st::boxBlueCloseIcon.width(), st::boxBlueCloseIcon.height());
}

void BlueTitleClose::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect()), s((st::boxTitleHeight - st::boxBlueCloseIcon.width()) / 2, (st::boxTitleHeight - st::boxBlueCloseIcon.height()) / 2, st::boxBlueCloseIcon.width(), st::boxBlueCloseIcon.height());
	if (!s.contains(r)) {
		p.fillRect(r, st::boxBlueTitleBg);
	}
	if (s.intersects(r)) {
		p.fillRect(s.intersected(r), a_iconFg.current());
		st::boxBlueCloseIcon.paint(p, s.topLeft(), width());
	}
}

AbstractBox::AbstractBox(int w) : LayerWidget() {
	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(w, 0);
}

void AbstractBox::prepare() {
	showAll();
}

void AbstractBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onClose();
	} else {
		LayerWidget::keyPressEvent(e);
	}
}

void AbstractBox::resizeEvent(QResizeEvent *e) {
	if (_blueClose) {
		_blueClose->moveToRight(0, 0);
	}
	if (_blueShadow) {
		_blueShadow->moveToLeft(0, st::boxTitleHeight);
		_blueShadow->resize(width(), st::boxBlueTitleShadow.height());
	}
	LayerWidget::resizeEvent(e);
}

void AbstractBox::parentResized() {
	int32 newHeight = countHeight();
	setGeometry((App::wnd()->width() - width()) / 2, (App::wnd()->height() - newHeight) / 2, width(), newHeight);
	update();
}

bool AbstractBox::paint(QPainter &p) {
	p.fillRect(rect(), st::boxBg);
	return false;
}

void AbstractBox::paintTitle(Painter &p, const QString &title, const QString &additional) {
	p.setFont(st::boxTitleFont);
	if (_blueTitle) {
		p.fillRect(0, 0, width(), st::boxTitleHeight, st::boxBlueTitleBg->b);
		p.setPen(st::white);

		int32 titleWidth = st::boxTitleFont->width(title);
		p.drawTextLeft(st::boxBlueTitlePosition.x(), st::boxBlueTitlePosition.y(), width(), title, titleWidth);

		if (!additional.isEmpty()) {
			p.setFont(st::boxTextFont);
			p.setPen(st::boxBlueTitleAdditionalFg);
			p.drawTextLeft(st::boxBlueTitlePosition.x() + titleWidth + st::boxBlueTitleAdditionalSkip, st::boxBlueTitlePosition.y(), width(), additional);
		}
	} else {
		p.setPen(st::boxTitleFg);
		p.drawTextLeft(st::boxTitlePosition.x(), st::boxTitlePosition.y(), width(), title);
	}
}

void AbstractBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (paint(p)) return;
}

void AbstractBox::setMaxHeight(int32 maxHeight) {
	resizeMaxHeight(width(), maxHeight);
}

void AbstractBox::resizeMaxHeight(int32 newWidth, int32 maxHeight) {
	if (width() != newWidth || _maxHeight != maxHeight) {
		QRect g(geometry());
		_maxHeight = maxHeight;
		resize(newWidth, countHeight());
		if (parentWidget()) {
			QRect r = geometry();
			int32 parenth = parentWidget()->height();
			if (r.top() + r.height() + st::boxVerticalMargin > parenth) {
				int32 newTop = qMax(parenth - int(st::boxVerticalMargin) - r.height(), (parenth - r.height()) / 2);
				if (newTop != r.top()) {
					move(r.left(), newTop);
				}
			}
			parentWidget()->update(geometry().united(g).marginsAdded(QMargins(st::boxShadow.width(), st::boxShadow.height(), st::boxShadow.width(), st::boxShadow.height())));
		}
	}
}

int AbstractBox::countHeight() const {
	return qMin(_maxHeight, App::wnd()->height() - 2 * st::boxVerticalMargin);
}

void AbstractBox::onClose() {
	if (!_closed) {
		_closed = true;
		closePressed();
	}
	emit closed(this);
}

void AbstractBox::setBlueTitle(bool blue) {
	_blueTitle = blue;
	delete _blueShadow;
	_blueShadow = new BlueTitleShadow(this);
	delete _blueClose;
	_blueClose = new BlueTitleClose(this);
	_blueClose->setAttribute(Qt::WA_OpaquePaintEvent);
	connect(_blueClose, SIGNAL(clicked()), this, SLOT(onClose()));
}

void AbstractBox::raiseShadow() {
	if (_blueShadow) {
		_blueShadow->raise();
	}
}

ScrollableBox::ScrollableBox(const style::flatScroll &scroll, int32 w) : AbstractBox(w)
, _scroll(this, scroll)
, _topSkip(st::boxTitleHeight)
, _bottomSkip(st::boxScrollSkip) {
	setBlueTitle(true);
}

void ScrollableBox::resizeEvent(QResizeEvent *e) {
	updateScrollGeometry();
	AbstractBox::resizeEvent(e);
}

void ScrollableBox::init(ScrolledWidget *inner, int bottomSkip, int topSkip) {
	_bottomSkip = bottomSkip;
	_topSkip = topSkip;
	_scroll->setOwnedWidget(inner);
	_scroll->setFocusPolicy(Qt::NoFocus);
	updateScrollGeometry();
}

void ScrollableBox::setScrollSkips(int bottomSkip, int topSkip) {
	if (_topSkip != topSkip || _bottomSkip != bottomSkip) {
		_topSkip = topSkip;
		_bottomSkip = bottomSkip;
		updateScrollGeometry();
	}
}

void ScrollableBox::updateScrollGeometry() {
	_scroll->setGeometry(0, _topSkip, width(), height() - _topSkip - _bottomSkip);
}

ItemListBox::ItemListBox(const style::flatScroll &scroll, int32 w) : ScrollableBox(scroll, w) {
	setMaxHeight(st::boxMaxListHeight);
}
