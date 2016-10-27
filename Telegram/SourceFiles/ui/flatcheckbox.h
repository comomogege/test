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

class FlatCheckbox : public Button {
	Q_OBJECT

public:
	FlatCheckbox(QWidget *parent, const QString &text, bool checked, const style::flatCheckbox &st);

	bool checked() const;
	void setChecked(bool checked);

	void step_appearance(float64 ms, bool timer);
	void paintEvent(QPaintEvent *e);

	void setOpacity(float64 o);

public slots:
	void onClicked();
	void onStateChange(int oldState, ButtonStateChangeSource source);

signals:
	void changed();

private:
	style::flatCheckbox _st;
	anim::fvalue a_over;
	Animation _a_appearance;

	QString _text;
	style::font _font;

	float64 _opacity;

	bool _checked;

};

class FlatRadiobutton : public FlatCheckbox {
	Q_OBJECT

public:

	FlatRadiobutton(QWidget *parent, const QString &group, int32 value, const QString &text, bool checked, const style::flatCheckbox &st);
	int32 val() const {
		return _value;
	}
	~FlatRadiobutton();

public slots:

	void onChanged();

private:

	void *_group;
	int32 _value;

};

class Checkbox : public Button {
	Q_OBJECT

public:
	Checkbox(QWidget *parent, const QString &text, bool checked = false, const style::Checkbox &st = st::defaultCheckbox);

	bool checked() const;
	enum class NotifyAboutChange {
		Notify,
		DontNotify,
	};
	void setChecked(bool checked, NotifyAboutChange notify = NotifyAboutChange::Notify);

	void finishAnimations();

	int naturalWidth() const override;

protected:
	void paintEvent(QPaintEvent *e) override;

public slots:
	void onClicked();
	void onStateChange(int oldState, ButtonStateChangeSource source);

signals:
	void changed();

private:
	void step_over(float64 ms, bool timer);
	void step_checked(float64 ms, bool timer);

	const style::Checkbox &_st;
	anim::fvalue a_over, a_checked;
	Animation _a_over, _a_checked;

	QString _text, _fullText;
	int32 _textWidth;
	QRect _checkRect;

	bool _checked;

};

class Radiobutton : public Button {
	Q_OBJECT

public:
	Radiobutton(QWidget *parent, const QString &group, int32 value, const QString &text, bool checked = false, const style::Radiobutton &st = st::defaultRadiobutton);

	bool checked() const;
	void setChecked(bool checked);

	int32 val() const {
		return _value;
	}

	void step_over(float64 ms, bool timer);
	void step_checked(float64 ms, bool timer);

	void paintEvent(QPaintEvent *e);

	~Radiobutton();

public slots:
	void onClicked();
	void onStateChange(int oldState, ButtonStateChangeSource source);

signals:
	void changed();

private:
	void onChanged();

	const style::Radiobutton &_st;
	anim::fvalue a_over, a_checked;
	Animation _a_over, _a_checked;

	QString _text, _fullText;
	int32 _textWidth;
	QRect _checkRect;

	bool _checked;

	void *_group;
	int32 _value;

};
