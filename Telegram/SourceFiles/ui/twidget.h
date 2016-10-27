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

namespace App {
	const QPixmap &sprite();
}

namespace Fonts {
	void start();
}

class Painter : public QPainter {
public:
	explicit Painter(QPaintDevice *device) : QPainter(device) {
	}

	void drawTextLeft(int x, int y, int outerw, const QString &text, int textWidth = -1) {
		QFontMetrics m(fontMetrics());
		if (rtl() && textWidth < 0) textWidth = m.width(text);
		drawText(rtl() ? (outerw - x - textWidth) : x, y + m.ascent(), text);
	}
	void drawTextRight(int x, int y, int outerw, const QString &text, int textWidth = -1) {
		QFontMetrics m(fontMetrics());
		if (!rtl() && textWidth < 0) textWidth = m.width(text);
		drawText(rtl() ? x : (outerw - x - textWidth), y + m.ascent(), text);
	}
	void drawPixmapLeft(int x, int y, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QPoint(rtl() ? (outerw - x - (from.width() / pix.devicePixelRatio())) : x, y), pix, from);
	}
	void drawPixmapLeft(const QPoint &p, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapLeft(p.x(), p.y(), outerw, pix, from);
	}
	void drawPixmapLeft(int x, int y, int w, int h, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QRect(rtl() ? (outerw - x - w) : x, y, w, h), pix, from);
	}
	void drawPixmapLeft(const QRect &r, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapLeft(r.x(), r.y(), r.width(), r.height(), outerw, pix, from);
	}
	void drawPixmapLeft(int x, int y, int outerw, const QPixmap &pix) {
		drawPixmap(QPoint(rtl() ? (outerw - x - (pix.width() / pix.devicePixelRatio())) : x, y), pix);
	}
	void drawPixmapLeft(const QPoint &p, int outerw, const QPixmap &pix) {
		return drawPixmapLeft(p.x(), p.y(), outerw, pix);
	}
	void drawPixmapRight(int x, int y, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QPoint(rtl() ? x : (outerw - x - (from.width() / pix.devicePixelRatio())), y), pix, from);
	}
	void drawPixmapRight(const QPoint &p, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapRight(p.x(), p.y(), outerw, pix, from);
	}
	void drawPixmapRight(int x, int y, int w, int h, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QRect(rtl() ? x : (outerw - x - w), y, w, h), pix, from);
	}
	void drawPixmapRight(const QRect &r, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapRight(r.x(), r.y(), r.width(), r.height(), outerw, pix, from);
	}
	void drawPixmapRight(int x, int y, int outerw, const QPixmap &pix) {
		drawPixmap(QPoint(rtl() ? x : (outerw - x - (pix.width() / pix.devicePixelRatio())), y), pix);
	}
	void drawPixmapRight(const QPoint &p, int outerw, const QPixmap &pix) {
		return drawPixmapRight(p.x(), p.y(), outerw, pix);
	}
	void drawSprite(int x, int y, const style::sprite &sprite) {
		return drawPixmap(QPoint(x, y), App::sprite(), sprite.rect());
	}
	void drawSprite(const QPoint &p, const style::sprite &sprite) {
		return drawPixmap(p, App::sprite(), sprite.rect());
	}
	void drawSpriteLeft(int x, int y, int outerw, const style::sprite &sprite) {
		return drawPixmapLeft(x, y, outerw, App::sprite(), sprite.rect());
	}
	void drawSpriteLeft(const QPoint &p, int outerw, const style::sprite &sprite) {
		return drawPixmapLeft(p, outerw, App::sprite(), sprite.rect());
	}
	void drawSpriteLeft(int x, int y, int w, int h, int outerw, const style::sprite &sprite) {
		return drawPixmapLeft(x, y, w, h, outerw, App::sprite(), sprite.rect());
	}
	void drawSpriteLeft(const QRect &r, int outerw, const style::sprite &sprite) {
		return drawPixmapLeft(r, outerw, App::sprite(), sprite.rect());
	}
	void drawSpriteRight(int x, int y, int outerw, const style::sprite &sprite) {
		return drawPixmapRight(x, y, outerw, App::sprite(), sprite.rect());
	}
	void drawSpriteRight(const QPoint &p, int outerw, const style::sprite &sprite) {
		return drawPixmapRight(p, outerw, App::sprite(), sprite.rect());
	}
	void drawSpriteRight(int x, int y, int w, int h, int outerw, const style::sprite &sprite) {
		return drawPixmapRight(x, y, w, h, outerw, App::sprite(), sprite.rect());
	}
	void drawSpriteRight(const QRect &r, int outerw, const style::sprite &sprite) {
		return drawPixmapRight(r, outerw, App::sprite(), sprite.rect());
	}
	void drawSpriteCenter(const QRect &in, const style::sprite &sprite) {
		return drawPixmap(QPoint(in.x() + (in.width() - sprite.pxWidth()) / 2, in.y() + (in.height() - sprite.pxHeight()) / 2), App::sprite(), sprite.rect());
	}
	void drawSpriteCenterLeft(const QRect &in, int outerw, const style::sprite &sprite) {
		return drawPixmapLeft(QPoint(in.x() + (in.width() - sprite.pxWidth()) / 2, in.y() + (in.height() - sprite.pxHeight()) / 2), outerw, App::sprite(), sprite.rect());
	}
	void drawSpriteCenterRight(const QRect &in, int outerw, const style::sprite &sprite) {
		return drawPixmapRight(QPoint(in.x() + (in.width() - sprite.pxWidth()) / 2, in.y() + (in.height() - sprite.pxHeight()) / 2), outerw, App::sprite(), sprite.rect());
	}
};

#define T_WIDGET \
public: \
	TWidget *tparent() { \
		return qobject_cast<TWidget*>(parentWidget()); \
	} \
	const TWidget *tparent() const { \
		return qobject_cast<const TWidget*>(parentWidget()); \
	} \
	virtual void leaveToChildEvent(QEvent *e, QWidget *child) { /* e -- from enterEvent() of child TWidget */ \
	} \
	virtual void enterFromChildEvent(QEvent *e, QWidget *child) { /* e -- from leaveEvent() of child TWidget */ \
	} \
	void moveToLeft(int x, int y, int outerw = 0) { \
		move(rtl() ? ((outerw > 0 ? outerw : parentWidget()->width()) - x - width()) : x, y); \
	} \
	void moveToRight(int x, int y, int outerw = 0) { \
		move(rtl() ? x : ((outerw > 0 ? outerw : parentWidget()->width()) - x - width()), y); \
	} \
	void setGeometryToLeft(int x, int y, int w, int h, int outerw = 0) { \
		setGeometry(rtl() ? ((outerw > 0 ? outerw : parentWidget()->width()) - x - w) : x, y, w, h); \
	} \
	void setGeometryToRight(int x, int y, int w, int h, int outerw = 0) { \
		setGeometry(rtl() ? x : ((outerw > 0 ? outerw : parentWidget()->width()) - x - w), y, w, h); \
	} \
	QPoint myrtlpoint(int x, int y) const { \
		return rtlpoint(x, y, width()); \
	} \
	QPoint myrtlpoint(const QPoint p) const { \
		return rtlpoint(p, width()); \
	} \
	QRect myrtlrect(int x, int y, int w, int h) const { \
		return rtlrect(x, y, w, h, width()); \
	} \
	QRect myrtlrect(const QRect &r) const { \
		return rtlrect(r, width()); \
	} \
	void rtlupdate(const QRect &r) { \
		update(myrtlrect(r)); \
	} \
	void rtlupdate(int x, int y, int w, int h) { \
		update(myrtlrect(x, y, w, h)); \
	} \
protected: \
	void enterEvent(QEvent *e) override { \
		TWidget *p(tparent()); \
		if (p) p->leaveToChildEvent(e, this); \
		return enterEventHook(e); \
	} \
	void leaveEvent(QEvent *e) override { \
		TWidget *p(tparent()); \
		if (p) p->enterFromChildEvent(e, this); \
		return leaveEventHook(e); \
	}

class TWidget : public QWidget {
	Q_OBJECT
	T_WIDGET

public:
	TWidget(QWidget *parent = nullptr) : QWidget(parent) {
	}
	bool event(QEvent *e) override {
		return QWidget::event(e);
	}
	virtual void grabStart() {
	}
	virtual void grabFinish() {
	}

	bool inFocusChain() const;

	void hideChildren() {
		for (auto child : children()) {
			if (auto widget = qobject_cast<QWidget*>(child)) {
				widget->hide();
			}
		}
	}
	void showChildren() {
		for (auto child : children()) {
			if (auto widget = qobject_cast<QWidget*>(child)) {
				widget->show();
			}
		}
	}

	QPointer<TWidget> weakThis() {
		return QPointer<TWidget>(this);
	}
	QPointer<const TWidget> weakThis() const {
		return QPointer<const TWidget>(this);
	}

	// Get the size of the widget as it should be.
	// Negative return value means no default width.
	virtual int naturalWidth() const {
		return -1;
	}

	// Count new height for width=newWidth and resize to it.
	void resizeToWidth(int newWidth) {
		auto newSize = QSize(newWidth, resizeGetHeight(newWidth));
		if (newSize != size()) {
			resize(newSize);
			update();
		}
	}

protected:
	void enterEventHook(QEvent *e) {
		return QWidget::enterEvent(e);
	}
	void leaveEventHook(QEvent *e) {
		return QWidget::leaveEvent(e);
	}

	// Resizes content and counts natural widget height for the desired width.
	virtual int resizeGetHeight(int newWidth) {
		return height();
	}

};

void myEnsureResized(QWidget *target);
QPixmap myGrab(TWidget *target, QRect rect = QRect());

class SingleDelayedCall : public QObject {
	Q_OBJECT

public:
	SingleDelayedCall(QObject *parent, const char *member) : QObject(parent), _member(member) {
	}
	void call() {
		if (_pending.testAndSetOrdered(0, 1)) {
			QMetaObject::invokeMethod(this, "makeDelayedCall", Qt::QueuedConnection);
		}
	}

private slots:
	void makeDelayedCall() {
		if (_pending.testAndSetOrdered(1, 0)) {
			QMetaObject::invokeMethod(parent(), _member);
		}
	}

private:
	QAtomicInt _pending = { 0 };
	const char *_member;

};

// A simple wrap around T* to explicitly state ownership
template <typename T>
class ChildWidget {
public:
	ChildWidget(std_::nullptr_t) : _widget(nullptr) {
	}

	// No default constructor, but constructors with at least
	// one argument are simply make functions.
	template <typename Parent, typename... Args>
	ChildWidget(Parent &&parent, Args&&... args) : _widget(new T(std_::forward<Parent>(parent), std_::forward<Args>(args)...)) {
	}

	ChildWidget(const ChildWidget<T> &other) = delete;
	ChildWidget<T> &operator=(const ChildWidget<T> &other) = delete;

	ChildWidget<T> &operator=(std_::nullptr_t) {
		_widget = nullptr;
		return *this;
	}
	ChildWidget<T> &operator=(T *widget) {
		_widget = widget;
		return *this;
	}

	T *operator->() const {
		return _widget;
	}
	T &operator*() const {
		return *_widget;
	}

	// So we can pass this pointer to methods like connect().
	T *ptr() const {
		return _widget;
	}
	operator T*() const {
		return ptr();
	}

	// Use that instead "= new T(parent, ...)"
	template <typename Parent, typename... Args>
	void create(Parent &&parent, Args&&... args) {
		delete _widget;
		_widget = new T(std_::forward<Parent>(parent), std_::forward<Args>(args)...);
	}
	void destroy() {
		if (_widget) {
			delete _widget;
			_widget = nullptr;
		}
	}
	void destroyDelayed() {
		if (_widget) {
			_widget->hide();
			_widget->deleteLater();
			_widget = nullptr;
		}
	}

private:
	T *_widget;

};

void sendSynteticMouseEvent(QWidget *widget, QEvent::Type type, Qt::MouseButton button, const QPoint &globalPoint);

inline void sendSynteticMouseEvent(QWidget *widget, QEvent::Type type, Qt::MouseButton button) {
	return sendSynteticMouseEvent(widget, type, button, QCursor::pos());
}
