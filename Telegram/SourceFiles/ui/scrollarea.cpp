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
#include "ui/scrollarea.h"

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

ScrollShadow::ScrollShadow(ScrollArea *parent, const style::flatScroll *st) : QWidget(parent), _st(st) {
	setVisible(false);
}

void ScrollShadow::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.fillRect(rect(), _st->shColor->b);
}

void ScrollShadow::changeVisibility(bool shown) {
	setVisible(shown);
}

ScrollBar::ScrollBar(ScrollArea *parent, bool vert, const style::flatScroll *st) : QWidget(parent)
, _st(st)
, _vertical(vert)
, _over(false)
, _overbar(false)
, _moving(false)
, _topSh(false)
, _bottomSh(false)
, _connected(vert ? parent->verticalScrollBar() : parent->horizontalScrollBar())
, _scrollMax(_connected->maximum())
, _hideIn(-1)
, a_bg((_st->hiding ? st::transparent : _st->bgColor)->c)
, a_bar((_st->hiding ? st::transparent : _st->barColor)->c)
, _a_appearance(animation(this, &ScrollBar::step_appearance)) {
	recountSize();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideTimer()));

	connect(_connected, SIGNAL(valueChanged(int)), this, SLOT(onValueChanged()));
	connect(_connected, SIGNAL(rangeChanged(int, int)), this, SLOT(updateBar()));

	updateBar();
}

void ScrollBar::recountSize() {
	setGeometry(_vertical ? QRect(rtl() ? 0 : (area()->width() - _st->width), _st->deltat, _st->width, area()->height() - _st->deltat - _st->deltab) : QRect(_st->deltat, area()->height() - _st->width, area()->width() - _st->deltat - _st->deltab, _st->width));
}

void ScrollBar::onValueChanged() {
	area()->onScrolled();
	updateBar();
}

void ScrollBar::updateBar(bool force) {
	QRect newBar;
	if (_connected->maximum() != _scrollMax) {
		int32 oldMax = _scrollMax, newMax = _connected->maximum();
		_scrollMax = newMax;
		area()->rangeChanged(oldMax, newMax, _vertical);
	}
	if (_vertical) {
		int sh = area()->scrollHeight(), rh = height(), h = sh ? int32((rh * int64(area()->height())) / sh) : 0;
		if (h >= rh || !area()->scrollTopMax() || rh < _st->minHeight) {
			if (!isHidden()) hide();
			bool newTopSh = (_st->topsh < 0), newBottomSh = (_st->bottomsh < 0);
			if (newTopSh != _topSh || force) emit topShadowVisibility(_topSh = newTopSh);
			if (newBottomSh != _bottomSh || force) emit bottomShadowVisibility(_bottomSh = newBottomSh);
			return;
		}

		if (h <= _st->minHeight) h = _st->minHeight;
		int stm = area()->scrollTopMax(), y = stm ? int32(((rh - h) * int64(area()->scrollTop())) / stm) : 0;
		if (y > rh - h) y = rh - h;

		newBar = QRect(_st->deltax, y, width() - 2 * _st->deltax, h);
	} else {
		int sw = area()->scrollWidth(), rw = width(), w = sw ? int32((rw * int64(area()->width())) / sw) : 0;
		if (w >= rw || !area()->scrollLeftMax() || rw < _st->minHeight) {
			if (!isHidden()) hide();
			return;
		}

		if (w <= _st->minHeight) w = _st->minHeight;
		int slm = area()->scrollLeftMax(), x = slm ? int32(((rw - w) * int64(area()->scrollLeft())) / slm) : 0;
		if (x > rw - w) x = rw - w;

		newBar = QRect(x, _st->deltax, w, height() - 2 * _st->deltax);
	}
	if (newBar != _bar) {
		_bar = newBar;
		update();
	}
	if (_vertical) {
		bool newTopSh = (_st->topsh < 0) || (area()->scrollTop() > _st->topsh), newBottomSh = (_st->bottomsh < 0) || (area()->scrollTop() < area()->scrollTopMax() - _st->bottomsh);
		if (newTopSh != _topSh || force) emit topShadowVisibility(_topSh = newTopSh);
		if (newBottomSh != _bottomSh || force) emit bottomShadowVisibility(_bottomSh = newBottomSh);
	}
	if (isHidden()) show();
}

void ScrollBar::onHideTimer() {
	_hideIn = -1;
	a_bg.start(QColor(a_bg.current().red(), a_bg.current().green(), a_bg.current().blue(), 0));
	a_bar.start(QColor(a_bar.current().red(), a_bar.current().green(), a_bar.current().blue(), 0));
	_a_appearance.start();
}

ScrollArea *ScrollBar::area() {
	return static_cast<ScrollArea*>(parentWidget());
}

void ScrollBar::paintEvent(QPaintEvent *e) {
	if (!_bar.width() && !_bar.height()) {
		hide();
		return;
	}
	if (!a_bg.current().alpha() && !a_bar.current().alpha()) return;
	QPainter p(this);

	int32 deltal = _vertical ? _st->deltax : 0, deltar = _vertical ? _st->deltax : 0;
	int32 deltat = _vertical ? 0 : _st->deltax, deltab = _vertical ? 0 : _st->deltax;
	p.setPen(Qt::NoPen);
	if (_st->round) {
		p.setRenderHint(QPainter::HighQualityAntialiasing, true);
		p.setBrush(a_bg.current());
		p.drawRoundedRect(QRect(deltal, deltat, width() - deltal - deltar, height() - deltat - deltab), _st->round, _st->round);
		p.setBrush(a_bar.current());
		p.drawRoundedRect(_bar, _st->round, _st->round);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);
	} else {
		p.fillRect(QRect(deltal, deltat, width() - deltal - deltar, height() - deltat - deltab), a_bg.current());
		p.fillRect(_bar, a_bar.current());
	}
}

void ScrollBar::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / _st->duration;
	if (dt >= 1) {
		_a_appearance.stop();
		a_bg.finish();
		a_bar.finish();
	} else {
		a_bg.update(dt, anim::linear);
		a_bar.update(dt, anim::linear);
	}
	if (timer) update();
}

void ScrollBar::hideTimeout(int64 dt) {
	if (_hideIn < 0) {
		a_bg.start((_over ? _st->bgOverColor : _st->bgColor)->c);
		a_bar.start((_overbar ? _st->barOverColor : _st->barColor)->c);
		_a_appearance.start();
	}
	_hideIn = dt;
	if (!_moving && _hideIn >= 0) {
		_hideTimer.start(_hideIn);
	}
}

void ScrollBar::enterEvent(QEvent *e) {
	_hideTimer.stop();
	setMouseTracking(true);
	_over = true;
	a_bg.start(_st->bgOverColor->c);
	a_bar.start(_st->barColor->c);
	_a_appearance.start();
}

void ScrollBar::leaveEvent(QEvent *e) {
	if (!_moving) {
		setMouseTracking(false);
		a_bg.start(_st->bgColor->c);
		a_bar.start(_st->barColor->c);
		_a_appearance.start();
		if (_hideIn >= 0) {
			_hideTimer.start(_hideIn);
		} else if (_st->hiding) {
			hideTimeout(_st->hiding);
		}
	}
	_over = _overbar = false;
}

void ScrollBar::mouseMoveEvent(QMouseEvent *e) {
	bool newOverBar = _bar.contains(e->pos());
	if (_overbar != newOverBar) {
		_overbar = newOverBar;
		if (!_moving) {
			a_bar.start((newOverBar ? _st->barOverColor : _st->barColor)->c);
			a_bg.start(_st->bgOverColor->c);
			_a_appearance.start();
		}
	}
	if (_moving) {
		int delta = 0, barDelta = _vertical ? (area()->height() - _bar.height()) : (area()->width() - _bar.width());
		if (barDelta > 0) {
			QPoint d = (e->globalPos() - _dragStart);
			delta = int32((_vertical ? (d.y() * int64(area()->scrollTopMax())) : (d.x() * int64(area()->scrollLeftMax()))) / barDelta);
		}
		_connected->setValue(_startFrom + delta);
	}
}

void ScrollBar::mousePressEvent(QMouseEvent *e) {
	if (!width() || !height()) return;

	_dragStart = e->globalPos();
	_moving = true;
	if (_overbar) {
		_startFrom = _connected->value();
	} else {
		int32 val = _vertical ? e->pos().y() : e->pos().x(), div = _vertical ? height() : width();
		val = (val <= _st->deltat) ? 0 : (val - _st->deltat);
		div = (div <= _st->deltat + _st->deltab) ? 1 : (div - _st->deltat - _st->deltab);
		_startFrom = _vertical ? int32((val * int64(area()->scrollTopMax())) / div) : ((val * int64(area()->scrollLeftMax())) / div);
		_connected->setValue(_startFrom);
		if (!_overbar) {
			_overbar = true;
			a_bar.start(_st->barOverColor->c);
			a_bg.start(_st->bgOverColor->c);
			_a_appearance.start();
		}
	}

	area()->setMovingByScrollBar(true);
	emit area()->scrollStarted();
}

void ScrollBar::mouseReleaseEvent(QMouseEvent *e) {
	if (_moving) {
		_moving = false;
		bool a = false;
		if (!_overbar) {
			if (!_over || _hideIn) {
				a_bar.start(_st->barColor->c);
				a = true;
			}
		}
		if (!_over) {
			if (_hideIn) {
				a_bg.start(_st->bgColor->c);
				a = true;
			}
			if (_hideIn >= 0) {
				_hideTimer.start(_hideIn);
			}
		}
		if (a) _a_appearance.start();

		area()->setMovingByScrollBar(false);
		emit area()->scrollFinished();
	}
	if (!_over) {
		setMouseTracking(false);
	}
}

void ScrollBar::resizeEvent(QResizeEvent *e) {
	updateBar();
}

void SplittedWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (rtl()) {
		p.translate(-otherWidth(), 0);
		paintRegion(p, e->region().translated(otherWidth(), 0), false);
	} else {
		paintRegion(p, e->region(), false);
	}
}

void SplittedWidget::update(const QRect &r) {
	if (rtl()) {
		TWidget::update(r.translated(-otherWidth(), 0).intersected(rect()));
		emit updateOther(r);
	} else {
		TWidget::update(r.intersected(rect()));
		emit updateOther(r.translated(-width(), 0));
	}
}

void SplittedWidget::update(const QRegion &r) {
	if (rtl()) {
		TWidget::update(r.translated(-otherWidth(), 0).intersected(rect()));
		emit updateOther(r);
	} else {
		TWidget::update(r.intersected(rect()));
		emit updateOther(r.translated(-width(), 0));
	}
}

void SplittedWidgetOther::paintEvent(QPaintEvent *e) {
	Painter p(this);
	SplittedWidget *s = static_cast<SplittedWidget*>(static_cast<ScrollArea*>(parentWidget())->widget());
	if (rtl()) {
		s->paintRegion(p, e->region(), true);
	} else {
		p.translate(-s->width(), 0);
		s->paintRegion(p, e->region().translated(s->width(), 0), true);
	}
}

ScrollArea::ScrollArea(QWidget *parent, const style::flatScroll &st, bool handleTouch) : QScrollArea(parent)
, _st(st)
, _horizontalBar(this, false, &_st)
, _verticalBar(this, true, &_st)
, _topShadow(this, &_st)
, _bottomShadow(this, &_st)
, _touchEnabled(handleTouch) {
	setLayoutDirection(cLangDir());

	connect(_verticalBar, SIGNAL(topShadowVisibility(bool)), _topShadow, SLOT(changeVisibility(bool)));
	connect(_verticalBar, SIGNAL(bottomShadowVisibility(bool)), _bottomShadow, SLOT(changeVisibility(bool)));
	_verticalBar->updateBar(true);

	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	setFrameStyle(QFrame::NoFrame | QFrame::Plain);
	viewport()->setAutoFillBackground(false);

	_horizontalValue = horizontalScrollBar()->value();
	_verticalValue = verticalScrollBar()->value();

	if (_touchEnabled) {
		viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
		_touchTimer.setSingleShot(true);
		connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));
		connect(&_touchScrollTimer, SIGNAL(timeout()), this, SLOT(onTouchScrollTimer()));
	}
}

void ScrollArea::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0) ? x : (x > 0) ? qMax(0, x - elapsed) : qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0) ? y : (y > 0) ? qMax(0, y - elapsed) : qMin(0, y + elapsed));
}

void ScrollArea::onScrolled() {
	myEnsureResized(widget());

	bool em = false;
	int horizontalValue = horizontalScrollBar()->value();
	int verticalValue = verticalScrollBar()->value();
	if (_horizontalValue != horizontalValue) {
		if (_disabled) {
			horizontalScrollBar()->setValue(_horizontalValue);
		} else {
			_horizontalValue = horizontalValue;
			if (_st.hiding) {
				_horizontalBar->hideTimeout(_st.hiding);
			}
			em = true;
		}
	}
	if (_verticalValue != verticalValue) {
		if (_disabled) {
			verticalScrollBar()->setValue(_verticalValue);
		} else {
			_verticalValue = verticalValue;
			if (_st.hiding) {
				_verticalBar->hideTimeout(_st.hiding);
			}
			em = true;
		}
	}
	if (em) {
		emit scrolled();
		if (!_movingByScrollBar) {
			sendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton);
		}
	}
}

int ScrollArea::scrollWidth() const {
	QWidget *w(widget());
	return w ? qMax(w->width(), width()) : width();
}

int ScrollArea::scrollHeight() const {
	QWidget *w(widget());
	return w ? qMax(w->height(), height()) : height();
}

int ScrollArea::scrollLeftMax() const {
	return scrollWidth() - width();
}

int ScrollArea::scrollTopMax() const {
	return scrollHeight() - height();
}

int ScrollArea::scrollLeft() const {
	return _horizontalValue;
}

int ScrollArea::scrollTop() const {
	return _verticalValue;
}

void ScrollArea::onTouchTimer() {
	_touchRightButton = true;
}

void ScrollArea::onTouchScrollTimer() {
	uint64 nowTime = getms();
	if (_touchScrollState == TouchScrollAcceleration && _touchWaitingAcceleration && (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = TouchScrollManual;
		touchResetSpeed();
	} else if (_touchScrollState == TouchScrollAuto || _touchScrollState == TouchScrollAcceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		bool hasScrolled = touchScroll(delta);

		if (_touchSpeed.isNull() || !hasScrolled) {
			_touchScrollState = TouchScrollManual;
			_touchScroll = false;
			_touchScrollTimer.stop();
		} else {
			_touchTime = nowTime;
		}
		touchDeaccelerate(elapsed);
	}
}

void ScrollArea::touchUpdateSpeed() {
	const uint64 nowTime = getms();
	if (_touchPrevPosValid) {
		const int elapsed = nowTime - _touchSpeedTime;
		if (elapsed) {
			const QPoint newPixelDiff = (_touchPos - _touchPrevPos);
			const QPoint pixelsPerSecond = newPixelDiff * (1000 / elapsed);

			// fingers are inacurates, we ignore small changes to avoid stopping the autoscroll because
			// of a small horizontal offset when scrolling vertically
			const int newSpeedY = (qAbs(pixelsPerSecond.y()) > FingerAccuracyThreshold) ? pixelsPerSecond.y() : 0;
			const int newSpeedX = (qAbs(pixelsPerSecond.x()) > FingerAccuracyThreshold) ? pixelsPerSecond.x() : 0;
			if (_touchScrollState == TouchScrollAuto) {
				const int oldSpeedY = _touchSpeed.y();
				const int oldSpeedX = _touchSpeed.x();
				if ((oldSpeedY <= 0 && newSpeedY <= 0) || ((oldSpeedY >= 0 && newSpeedY >= 0)
					&& (oldSpeedX <= 0 && newSpeedX <= 0)) || (oldSpeedX >= 0 && newSpeedX >= 0)) {
					_touchSpeed.setY(snap((oldSpeedY + (newSpeedY / 4)), -MaxScrollAccelerated, +MaxScrollAccelerated));
					_touchSpeed.setX(snap((oldSpeedX + (newSpeedX / 4)), -MaxScrollAccelerated, +MaxScrollAccelerated));
				} else {
					_touchSpeed = QPoint();
				}
			} else {
				// we average the speed to avoid strange effects with the last delta
				if (!_touchSpeed.isNull()) {
					_touchSpeed.setX(snap((_touchSpeed.x() / 4) + (newSpeedX * 3 / 4), -MaxScrollFlick, +MaxScrollFlick));
					_touchSpeed.setY(snap((_touchSpeed.y() / 4) + (newSpeedY * 3 / 4), -MaxScrollFlick, +MaxScrollFlick));
				} else {
					_touchSpeed = QPoint(newSpeedX, newSpeedY);
				}
			}
		}
	} else {
		_touchPrevPosValid = true;
	}
	_touchSpeedTime = nowTime;
	_touchPrevPos = _touchPos;
}

void ScrollArea::touchResetSpeed() {
	_touchSpeed = QPoint();
	_touchPrevPosValid = false;
}

bool ScrollArea::eventFilter(QObject *obj, QEvent *e) {
	bool res = QScrollArea::eventFilter(obj, e);
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (_touchEnabled && ev->device()->type() == QTouchDevice::TouchScreen) {
			if (obj == widget()) {
				touchEvent(ev);
				return true;
			}
		}
	}
	return res;
}

bool ScrollArea::viewportEvent(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (_touchEnabled && ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}
	return QScrollArea::viewportEvent(e);
}

void ScrollArea::touchEvent(QTouchEvent *e) {
	if (!e->touchPoints().isEmpty()) {
		_touchPrevPos = _touchPos;
		_touchPos = e->touchPoints().cbegin()->screenPos().toPoint();
	}

	switch (e->type()) {
	case QEvent::TouchBegin:
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchPress = true;
		if (_touchScrollState == TouchScrollAuto) {
			_touchScrollState = TouchScrollAcceleration;
			_touchWaitingAcceleration = true;
			_touchAccelerationTime = getms();
			touchUpdateSpeed();
			_touchStart = _touchPos;
		} else {
			_touchScroll = false;
			_touchTimer.start(QApplication::startDragTime());
		}
		_touchStart = _touchPrevPos = _touchPos;
		_touchRightButton = false;
		break;

	case QEvent::TouchUpdate:
		if (!_touchPress) return;
		if (!_touchScroll && (_touchPos - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchTimer.stop();
			_touchScroll = true;
			touchUpdateSpeed();
		}
		if (_touchScroll) {
			if (_touchScrollState == TouchScrollManual) {
				touchScrollUpdated(_touchPos);
			} else if (_touchScrollState == TouchScrollAcceleration) {
				touchUpdateSpeed();
				_touchAccelerationTime = getms();
				if (_touchSpeed.isNull()) {
					_touchScrollState = TouchScrollManual;
				}
			}
		}
		break;

	case QEvent::TouchEnd:
		if (!_touchPress) return;
		_touchPress = false;
		if (_touchScroll) {
			if (_touchScrollState == TouchScrollManual) {
				_touchScrollState = TouchScrollAuto;
				_touchPrevPosValid = false;
				_touchScrollTimer.start(15);
				_touchTime = getms();
			} else if (_touchScrollState == TouchScrollAuto) {
				_touchScrollState = TouchScrollManual;
				_touchScroll = false;
				touchResetSpeed();
			} else if (_touchScrollState == TouchScrollAcceleration) {
				_touchScrollState = TouchScrollAuto;
				_touchWaitingAcceleration = false;
				_touchPrevPosValid = false;
			}
		} else if (window()) { // one short tap -- like left mouse click, one long tap -- like right mouse click
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);

			sendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton, _touchStart);
			sendSynteticMouseEvent(this, QEvent::MouseButtonPress, btn, _touchStart);
			sendSynteticMouseEvent(this, QEvent::MouseButtonRelease, btn, _touchStart);

			if (_touchRightButton) {
				auto windowHandle = window()->windowHandle();
				auto localPoint = windowHandle->mapFromGlobal(_touchStart);
				QContextMenuEvent ev(QContextMenuEvent::Mouse, localPoint, _touchStart, QGuiApplication::keyboardModifiers());
				ev.setTimestamp(getms());
				QGuiApplication::sendEvent(windowHandle, &ev);
			}
		}
		_touchTimer.stop();
		_touchRightButton = false;
		break;

	case QEvent::TouchCancel:
		_touchPress = false;
		_touchScroll = false;
		_touchScrollState = TouchScrollManual;
		_touchTimer.stop();
		break;
	}
}

void ScrollArea::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	touchScroll(_touchPos - _touchPrevPos);
	touchUpdateSpeed();
}

void ScrollArea::disableScroll(bool dis) {
	_disabled = dis;
	if (_disabled && _st.hiding) {
		_horizontalBar->hideTimeout(0);
		_verticalBar->hideTimeout(0);
	}
}

void ScrollArea::scrollContentsBy(int dx, int dy) {
	if (_disabled) {
		return;
	}
	QScrollArea::scrollContentsBy(dx, dy);
}

bool ScrollArea::touchScroll(const QPoint &delta) {
	int32 scTop = scrollTop(), scMax = scrollTopMax(), scNew = snap(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) return false;

	scrollToY(scNew);
	return true;
}

void ScrollArea::resizeEvent(QResizeEvent *e) {
	QScrollArea::resizeEvent(e);
	_horizontalBar->recountSize();
	_verticalBar->recountSize();
	_topShadow->setGeometry(QRect(0, 0, width(), qAbs(_st.topsh)));
	_bottomShadow->setGeometry(QRect(0, height() - qAbs(_st.bottomsh), width(), qAbs(_st.bottomsh)));
	if (SplittedWidget *w = qobject_cast<SplittedWidget*>(widget())) {
		w->resize(width() - w->otherWidth(), w->height());
		if (!rtl()) {
			_other->move(w->width(), w->y());
		}
	}
	emit geometryChanged();
}

void ScrollArea::moveEvent(QMoveEvent *e) {
	QScrollArea::moveEvent(e);
	emit geometryChanged();
}

void ScrollArea::keyPressEvent(QKeyEvent *e) {
	if ((e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) && e->modifiers().testFlag(Qt::AltModifier)) {
		e->ignore();
	} else if(e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		((QObject*)widget())->event(e);
	} else {
		QScrollArea::keyPressEvent(e);
	}
}

void ScrollArea::enterEventHook(QEvent *e) {
	if (_disabled) return;
	if (_st.hiding) {
		_horizontalBar->hideTimeout(_st.hiding);
		_verticalBar->hideTimeout(_st.hiding);
	}
	return QScrollArea::enterEvent(e);
}

void ScrollArea::leaveEventHook(QEvent *e) {
	if (_st.hiding) {
		_horizontalBar->hideTimeout(0);
		_verticalBar->hideTimeout(0);
	}
	return QScrollArea::leaveEvent(e);
}

void ScrollArea::scrollToY(int toTop, int toBottom) {
	myEnsureResized(widget());
	myEnsureResized(this);

	int toMin = 0, toMax = scrollTopMax();
	if (toTop < toMin) {
		toTop = toMin;
	} else if (toTop > toMax) {
		toTop = toMax;
	}
	bool exact = (toBottom < 0);

	int curTop = scrollTop(), curHeight = height(), curBottom = curTop + curHeight, scToTop = toTop;
	if (!exact && toTop >= curTop) {
		if (toBottom < toTop) toBottom = toTop;
		if (toBottom <= curBottom) return;

		scToTop = toBottom - curHeight;
		if (scToTop > toTop) scToTop = toTop;
		if (scToTop == curTop) return;
	} else {
		scToTop = toTop;
	}
	verticalScrollBar()->setValue(scToTop);
}

void ScrollArea::setWidget(QWidget *w) {
	SplittedWidget *splitted = qobject_cast<SplittedWidget*>(w);
	if (widget() && _touchEnabled) {
		widget()->removeEventFilter(this);
		if (!_widgetAcceptsTouch) widget()->setAttribute(Qt::WA_AcceptTouchEvents, false);
	}
	if (_other && !splitted) {
		delete _other;
		_other = 0;
		disconnect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(onVerticalScroll()));
	} else if (!_other && splitted) {
		_other = new SplittedWidgetOther(this);
		_other->setAttribute(Qt::WA_OpaquePaintEvent);
		_other->resize(_verticalBar->width(), _other->height());
		connect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(onVerticalScroll()));
		_horizontalBar->raise();
		_verticalBar->raise();
	}
	if (_ownsWidget) {
		_ownsWidget = false;
		delete takeWidget();
	}
	QScrollArea::setWidget(w);
	if (w) {
		w->setAutoFillBackground(false);
		if (_touchEnabled) {
			w->installEventFilter(this);
			_widgetAcceptsTouch = w->testAttribute(Qt::WA_AcceptTouchEvents);
			w->setAttribute(Qt::WA_AcceptTouchEvents);
		}
		if (splitted) {
			splitted->setOtherWidth(_verticalBar->width());
			w->setGeometry(rtl() ? splitted->otherWidth() : 0, 0, width() - splitted->otherWidth(), w->height());
			connect(splitted, SIGNAL(resizeOther()), this, SLOT(onResizeOther()));
			connect(splitted, SIGNAL(updateOther(const QRect&)), this, SLOT(onUpdateOther(const QRect&)));
			connect(splitted, SIGNAL(updateOther(const QRegion&)), this, SLOT(onUpdateOther(const QRegion&)));
			onResizeOther();
			splitted->update();
		}
	}
}

void ScrollArea::setOwnedWidget(QWidget *widget) {
	setWidget(widget);
	_ownsWidget = true;
}

QWidget *ScrollArea::takeWidget() {
	if (_other) {
		delete _other;
		_other = 0;
		disconnect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(onVerticalScroll()));
	}
	return QScrollArea::takeWidget();
}

void ScrollArea::onResizeOther() {
	_other->resize(_other->width(), widget()->height());
}

void ScrollArea::onUpdateOther(const QRect &r) {
	_other->update(r.intersected(_other->rect()));
}

void ScrollArea::onUpdateOther(const QRegion &r) {
	_other->update(r.intersected(_other->rect()));
}

void ScrollArea::onVerticalScroll() {
	_other->move(_other->x(), widget()->y());
}

void ScrollArea::rangeChanged(int oldMax, int newMax, bool vertical) {
}

void ScrollArea::updateColors(const style::color &bar, const style::color &bg, const style::color &barOver, const style::color &bgOver) {
	_st.barColor = bar;
	_st.bgColor = bg;
	_st.barOverColor = barOver;
	_st.bgOverColor = bgOver;
	_horizontalBar->update();
	_verticalBar->update();
}

bool ScrollArea::focusNextPrevChild(bool next) {
	if (QWidget::focusNextPrevChild(next)) {
//		if (QWidget *fw = focusWidget())
//			ensureWidgetVisible(fw);
		return true;
	}
	return false;
}

void ScrollArea::setMovingByScrollBar(bool movingByScrollBar) {
	_movingByScrollBar = movingByScrollBar;
}

ScrollArea::~ScrollArea() {
	if (!_ownsWidget) {
		takeWidget();
	}
}
