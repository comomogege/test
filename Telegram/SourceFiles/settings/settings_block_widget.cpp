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
#include "settings/settings_block_widget.h"

#include "styles/style_settings.h"
#include "ui/flatcheckbox.h"

namespace Settings {

BlockWidget::BlockWidget(QWidget *parent, UserData *self, const QString &title) : ScrolledWidget(parent)
, _self(self)
, _title(title) {
}

void BlockWidget::setContentLeft(int contentLeft) {
	_contentLeft = contentLeft;
}

int BlockWidget::contentTop() const {
	return emptyTitle() ? 0 : (st::settingsBlockMarginTop + st::settingsBlockTitleHeight);
}

int BlockWidget::resizeGetHeight(int newWidth) {
	int x = contentLeft(), result = contentTop();
	int availw = newWidth - x;
	for_const (auto &row, _rows) {
		row.child->moveToLeft(x + row.margin.left(), result + row.margin.top(), newWidth);
		auto availRowWidth = availw - row.margin.left() - row.margin.right() - x;
		auto natural = row.child->naturalWidth();
		auto rowWidth = (natural < 0) ? availRowWidth : qMin(natural, availRowWidth);
		if (row.child->width() != rowWidth) {
			row.child->resizeToWidth(rowWidth);
		}
		result += row.child->height() + row.margin.top() + row.margin.bottom();
	}
	result += st::settingsBlockMarginBottom;
	return result;
}

void BlockWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	paintTitle(p);
	paintContents(p);
}

void BlockWidget::paintTitle(Painter &p) {
	if (emptyTitle()) return;

	p.setFont(st::settingsBlockTitleFont);
	p.setPen(st::settingsBlockTitleFg);
	int titleTop = st::settingsBlockMarginTop + st::settingsBlockTitleTop;
	p.drawTextLeft(contentLeft(), titleTop, width(), _title);
}

void BlockWidget::addCreatedRow(TWidget *child, const style::margins &margin) {
	_rows.push_back({ child, margin });
}

void BlockWidget::rowHeightUpdated() {
	auto newHeight = resizeGetHeight(width());
	if (newHeight != height()) {
		resize(width(), newHeight);
		emit heightUpdated();
	}
}

void BlockWidget::createChildRow(ChildWidget<Checkbox> &child, style::margins &margin, const QString &text, const char *slot, bool checked) {
	child = new Checkbox(this, text, checked, st::defaultBoxCheckbox);
	connect(child, SIGNAL(changed()), this, slot);
}

void BlockWidget::createChildRow(ChildWidget<Radiobutton> &child, style::margins &margin, const QString &group, int value, const QString &text, const char *slot, bool checked) {
	child = new Radiobutton(this, group, value, text, checked, st::defaultRadiobutton);
	connect(child, SIGNAL(changed()), this, slot);
}

void BlockWidget::createChildRow(ChildWidget<LinkButton> &child, style::margins &margin, const QString &text, const char *slot, const style::linkButton &st) {
	child = new LinkButton(this, text, st);
	connect(child, SIGNAL(clicked()), this, slot);
}

} // namespace Settings
