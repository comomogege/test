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

#include "animation.h"

class FlatInput : public QLineEdit {
	Q_OBJECT
	T_WIDGET

public:
	FlatInput(QWidget *parent, const style::flatInput &st, const QString &ph = QString(), const QString &val = QString());

	void notaBene();

	void setPlaceholder(const QString &ph);
	void setPlaceholderFast(bool fast);
	void updatePlaceholder();
	const QString &placeholder() const;
	QRect placeholderRect() const;

	QRect getTextRect() const;

	void step_appearance(float64 ms, bool timer);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	void customUpDown(bool isCustom);
	const QString &getLastText() const {
		return _oldtext;
	}

	void setTextMargins(const QMargins &mrg);

public slots:
	void onTextChange(const QString &text);
	void onTextEdited();

	void onTouchTimer();

signals:
	void changed();
	void cancelled();
	void submitted(bool ctrlShiftEnter);
	void focused();
	void blurred();

protected:
	void enterEventHook(QEvent *e) {
		return QLineEdit::enterEvent(e);
	}
	void leaveEventHook(QEvent *e) {
		return QLineEdit::leaveEvent(e);
	}

	bool event(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	virtual void correctValue(const QString &was, QString &now);

	style::font phFont() {
		return _st.font;
	}

	void phPrepare(Painter &p);

private:
	void updatePlaceholderText();

	QString _oldtext, _ph, _fullph;
	bool _fastph;

	bool _customUpDown;

	bool _phVisible;
	anim::ivalue a_phLeft;
	anim::fvalue a_phAlpha;
	anim::cvalue a_phColor, a_borderColor, a_bgColor;
	Animation _a_appearance;

	int _notingBene;
	style::flatInput _st;

	style::font _font;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;
};

class CountryCodeInput : public FlatInput {
	Q_OBJECT

public:
	CountryCodeInput(QWidget *parent, const style::flatInput &st);

public slots:
	void startErasing(QKeyEvent *e);
	void codeSelected(const QString &code);

signals:
	void codeChanged(const QString &code);
	void addedToNumber(const QString &added);

protected:
	void correctValue(const QString &was, QString &now) override;

private:
	bool _nosignal;

};

class PhonePartInput : public FlatInput {
	Q_OBJECT

public:
	PhonePartInput(QWidget *parent, const style::flatInput &st);

public slots:
	void addedToNumber(const QString &added);
	void onChooseCode(const QString &code);

signals:
	void voidBackspace(QKeyEvent *e);

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	void correctValue(const QString &was, QString &now) override;

private:
	QVector<int> _pattern;

};

enum CtrlEnterSubmit {
	CtrlEnterSubmitEnter,
	CtrlEnterSubmitCtrlEnter,
	CtrlEnterSubmitBoth,
};

class InputArea : public TWidget {
	Q_OBJECT

public:
	InputArea(QWidget *parent, const style::InputArea &st, const QString &ph = QString(), const QString &val = QString());

	void showError();

	void setMaxLength(int32 maxLength) {
		_maxLength = maxLength;
	}

	const QString &getLastText() const {
		return _oldtext;
	}
	void updatePlaceholder();

	void step_placeholderFg(float64 ms, bool timer);
	void step_placeholderShift(float64 ms, bool timer);
	void step_border(float64 ms, bool timer);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	QString getText(int32 start = 0, int32 end = -1) const;
	bool hasText() const;

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	void customUpDown(bool isCustom);
	void setCtrlEnterSubmit(CtrlEnterSubmit ctrlEnterSubmit);

	void setTextCursor(const QTextCursor &cursor) {
		return _inner.setTextCursor(cursor);
	}
	QTextCursor textCursor() const {
		return _inner.textCursor();
	}
	void setText(const QString &text) {
		_inner.setText(text);
		updatePlaceholder();
	}
	void clear() {
		_inner.clear();
		updatePlaceholder();
	}
	bool hasFocus() const {
		return _inner.hasFocus();
	}
	void setFocus() {
		_inner.setFocus();
	}
	void clearFocus() {
		_inner.clearFocus();
	}

public slots:
	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

signals:
	void changed();
	void submitted(bool ctrlShiftEnter);
	void cancelled();
	void tabbed();

	void focused();
	void blurred();
	void resized();

protected:
	void insertEmoji(EmojiPtr emoji, QTextCursor c);
	TWidget *tparent() {
		return qobject_cast<TWidget*>(parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(parentWidget());
	}

	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	int32 _maxLength;
	bool heightAutoupdated();
	void checkContentHeight();

	class Inner : public QTextEdit {
	public:
		Inner(InputArea *parent);

		QVariant loadResource(int type, const QUrl &name) override;

	protected:
		bool viewportEvent(QEvent *e) override;
		void focusInEvent(QFocusEvent *e) override;
		void focusOutEvent(QFocusEvent *e) override;
		void keyPressEvent(QKeyEvent *e) override;
		void paintEvent(QPaintEvent *e) override;
		void contextMenuEvent(QContextMenuEvent *e) override;

		QMimeData *createMimeDataFromSelection() const override;

	private:
		InputArea *f() const {
			return static_cast<InputArea*>(parentWidget());
		}
		friend class InputArea;

	};
	friend class Inner;

	void focusInInner();
	void focusOutInner();

	void processDocumentContentsChange(int position, int charsAdded);

	void startBorderAnimation();

	Inner _inner;

	QString _oldtext;

	CtrlEnterSubmit _ctrlEnterSubmit;
	bool _undoAvailable, _redoAvailable, _inHeightCheck;

	bool _customUpDown;

	QString _placeholder, _placeholderFull;
	bool _placeholderVisible;
	anim::ivalue a_placeholderLeft;
	anim::fvalue a_placeholderOpacity;
	anim::cvalue a_placeholderFg;
	Animation _a_placeholderFg, _a_placeholderShift;

	anim::fvalue a_borderOpacityActive;
	anim::cvalue a_borderFg;
	Animation _a_border;

	bool _focused, _error;

	const style::InputArea &_st;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;

	bool _correcting;

};

class InputField : public TWidget {
	Q_OBJECT

public:
	InputField(QWidget *parent, const style::InputField &st, const QString &ph = QString(), const QString &val = QString());

	void setMaxLength(int32 maxLength) {
		_maxLength = maxLength;
	}

	void showError();

	const QString &getLastText() const {
		return _oldtext;
	}
	void updatePlaceholder();
	void setPlaceholderHidden(bool forcePlaceholderHidden);
	void finishPlaceholderAnimation();

	void step_placeholderFg(float64 ms, bool timer);
	void step_placeholderShift(float64 ms, bool timer);
	void step_border(float64 ms, bool timer);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	QString getText(int32 start = 0, int32 end = -1) const;
	bool hasText() const;

	bool isUndoAvailable() const;
	bool isRedoAvailable() const;

	void customUpDown(bool isCustom);

	void setTextCursor(const QTextCursor &cursor) {
		return _inner.setTextCursor(cursor);
	}
	QTextCursor textCursor() const {
		return _inner.textCursor();
	}
	void setText(const QString &text) {
		_inner.setText(text);
		updatePlaceholder();
	}
	void clear() {
		_inner.clear();
		updatePlaceholder();
	}
	bool hasFocus() const {
		return _inner.hasFocus();
	}
	void setFocus() {
		_inner.setFocus();
		QTextCursor c(_inner.textCursor());
		c.movePosition(QTextCursor::End);
		_inner.setTextCursor(c);
	}
	void clearFocus() {
		_inner.clearFocus();
	}
	void setCursorPosition(int pos) {
		QTextCursor c(_inner.textCursor());
		c.setPosition(pos);
		_inner.setTextCursor(c);
	}

public slots:
	void onTouchTimer();

	void onDocumentContentsChange(int position, int charsRemoved, int charsAdded);
	void onDocumentContentsChanged();

	void onUndoAvailable(bool avail);
	void onRedoAvailable(bool avail);

	void selectAll();

signals:
	void changed();
	void submitted(bool ctrlShiftEnter);
	void cancelled();
	void tabbed();

	void focused();
	void blurred();

protected:
	void insertEmoji(EmojiPtr emoji, QTextCursor c);
	TWidget *tparent() {
		return qobject_cast<TWidget*>(parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(parentWidget());
	}

	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	int32 _maxLength;
	bool _forcePlaceholderHidden = false;

	class Inner : public QTextEdit {
	public:
		Inner(InputField *parent);

		QVariant loadResource(int type, const QUrl &name) override;

	protected:
		bool viewportEvent(QEvent *e) override;
		void focusInEvent(QFocusEvent *e) override;
		void focusOutEvent(QFocusEvent *e) override;
		void keyPressEvent(QKeyEvent *e) override;
		void paintEvent(QPaintEvent *e) override;
		void contextMenuEvent(QContextMenuEvent *e) override;

		QMimeData *createMimeDataFromSelection() const override;

	private:
		InputField *f() const {
			return static_cast<InputField*>(parentWidget());
		}
		friend class InputField;

	};
	friend class Inner;

	void focusInInner();
	void focusOutInner();

	void processDocumentContentsChange(int position, int charsAdded);

	void startBorderAnimation();

	Inner _inner;

	QString _oldtext;

	bool _undoAvailable, _redoAvailable;

	bool _customUpDown;

	QString _placeholder, _placeholderFull;
	bool _placeholderVisible;
	anim::ivalue a_placeholderLeft;
	anim::fvalue a_placeholderOpacity;
	anim::cvalue a_placeholderFg;
	Animation _a_placeholderFg, _a_placeholderShift;

	anim::fvalue a_borderOpacityActive;
	anim::cvalue a_borderFg;
	Animation _a_border;

	bool _focused, _error;

	const style::InputField &_st;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;

	bool _correcting;
};

class MaskedInputField : public QLineEdit {
	Q_OBJECT
	T_WIDGET

public:
	MaskedInputField(QWidget *parent, const style::InputField &st, const QString &placeholder = QString(), const QString &val = QString());

	bool event(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	void showError();

	bool setPlaceholder(const QString &ph);
	void setPlaceholderFast(bool fast);
	void updatePlaceholder();

	QRect getTextRect() const;

	void step_placeholderFg(float64 ms, bool timer);
	void step_placeholderShift(float64 ms, bool timer);
	void step_border(float64 ms, bool timer);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

	void customUpDown(bool isCustom);
	const QString &getLastText() const {
		return _oldtext;
	}
	void setText(const QString &text) {
		QLineEdit::setText(text);
		updatePlaceholder();
	}
	void clear() {
		QLineEdit::clear();
		updatePlaceholder();
	}

public slots:
	void onTextChange(const QString &text);
	void onCursorPositionChanged(int oldPosition, int position);

	void onTextEdited();

	void onTouchTimer();

signals:
	void changed();
	void cancelled();
	void submitted(bool ctrlShiftEnter);
	void focused();
	void blurred();

protected:
	void enterEventHook(QEvent *e) {
		return QLineEdit::enterEvent(e);
	}
	void leaveEventHook(QEvent *e) {
		return QLineEdit::leaveEvent(e);
	}

	virtual void correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor);
	virtual void paintPlaceholder(Painter &p);

	style::font phFont() {
		return _st.font;
	}

	void placeholderPreparePaint(Painter &p);
	const QString &placeholder() const;
	QRect placeholderRect() const;

	void setTextMargins(const QMargins &mrg);
	const style::InputField &_st;

private:
	void startBorderAnimation();
	void updatePlaceholderText();

	int32 _maxLength;

	QString _oldtext;
	int32 _oldcursor;

	bool _undoAvailable, _redoAvailable;

	bool _customUpDown;

	QString _placeholder, _placeholderFull;
	bool _placeholderVisible, _placeholderFast;
	anim::ivalue a_placeholderLeft;
	anim::fvalue a_placeholderOpacity;
	anim::cvalue a_placeholderFg;
	Animation _a_placeholderFg, _a_placeholderShift;

	anim::fvalue a_borderOpacityActive;
	anim::cvalue a_borderFg;
	Animation _a_border;

	bool _focused, _error;

	style::margins _textMargins;

	QTimer _touchTimer;
	bool _touchPress, _touchRightButton, _touchMove;
	QPoint _touchStart;
};

class PasswordField : public MaskedInputField {
public:
	PasswordField(QWidget *parent, const style::InputField &st, const QString &ph = QString(), const QString &val = QString());

};

class PortInput : public MaskedInputField {
public:
	PortInput(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val);

protected:
	void correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor) override;

};

class UsernameInput : public MaskedInputField {
public:
	UsernameInput(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val, bool isLink);

protected:
	void correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor) override;
	void paintPlaceholder(Painter &p) override;

private:
	QString _linkPlaceholder;

};

class PhoneInput : public MaskedInputField {
public:
	PhoneInput(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val);

	void clearText();

protected:
	void focusInEvent(QFocusEvent *e) override;

	void correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor) override;
	void paintPlaceholder(Painter &p) override;

private:
	QString _defaultPlaceholder;
	QVector<int> _pattern;

};
