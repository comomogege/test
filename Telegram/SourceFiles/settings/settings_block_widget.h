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

#include "core/observer.h"

class Checkbox;
class Radiobutton;

namespace Ui {
template <typename Widget>
class WidgetSlideWrap;
} // namespace Ui

namespace Settings {

class BlockWidget : public ScrolledWidget, protected base::Subscriber {
	Q_OBJECT

public:
	BlockWidget(QWidget *parent, UserData *self, const QString &title);

	void setContentLeft(int contentLeft);

protected:
	void paintEvent(QPaintEvent *e) override;
	virtual void paintContents(Painter &p) {
	}

	// Where does the block content start (after the title).
	int contentLeft() const {
		return _contentLeft;
	}
	int contentTop() const;

	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

	void contentSizeUpdated() {
		resizeToWidth(width());
		emit heightUpdated();
	}

	UserData *self() const {
		return _self;
	}

	bool emptyTitle() const {
		return _title.isEmpty();
	}

	template <typename Widget, typename ...Args>
	Widget *addChildRow(ChildWidget<Widget> &child, style::margins margin, Args&&... args) {
		createChildRow(child, margin, std_::forward<Args>(args)...);
		addCreatedRow(child, margin);
		return child;
	}

private:
	template <typename Widget, typename ...Args>
	void createChildRow(ChildWidget<Ui::WidgetSlideWrap<Widget>> &child, style::margins &margin, const style::margins &padding, Args&&... args) {
		ChildWidget<Widget> plainChild = { nullptr };
		createChildRow(plainChild, margin, std_::forward<Args>(args)...);
		child = new Ui::WidgetSlideWrap<Widget>(this, plainChild, padding, [this]() {
			rowHeightUpdated();
		});
		margin.setLeft(margin.left() - padding.left());
		margin.setTop(margin.top() - padding.top());
		margin.setRight(margin.right() - padding.right());
		margin.setBottom(margin.bottom() - padding.bottom());
	}
	void createChildRow(ChildWidget<Checkbox> &child, style::margins &margin, const QString &text, const char *slot, bool checked);
	void createChildRow(ChildWidget<Radiobutton> &child, style::margins &margin, const QString &group, int value, const QString &text, const char *slot, bool checked);
	void createChildRow(ChildWidget<LinkButton> &child, style::margins &margin, const QString &text, const char *slot, const style::linkButton &st = st::defaultBoxLinkButton);

	void addCreatedRow(TWidget *child, const style::margins &margin);
	void rowHeightUpdated();

	template <typename Widget>
	struct IsWidgetSlideWrap {
		static constexpr bool value = false;
	};
	template <typename Widget>
	struct IsWidgetSlideWrap<Ui::WidgetSlideWrap<Widget>> {
		static constexpr bool value = true;
	};

	template <typename Widget>
	using NotImplementedYet = std_::enable_if_t<
		!IsWidgetSlideWrap<Widget>::value &&
		!std_::is_same<Widget, Checkbox>::value &&
		!std_::is_same<Widget, Radiobutton>::value &&
		!std_::is_same<Widget, LinkButton>::value>;

	template <typename Widget, typename... Args, typename = NotImplementedYet<Widget>>
	void createChildRow(ChildWidget<Widget> &child, style::margins &margin, Args&&... args) {
		child = new Widget(this, std_::forward<Args>(args)...);
	}

	void paintTitle(Painter &p);

	struct ChildRow {
		TWidget *child;
		style::margins margin;
	};
	QVector<ChildRow> _rows;

	int _contentLeft = 0;
	UserData *_self;
	QString _title;

};

} // namespace Settings
