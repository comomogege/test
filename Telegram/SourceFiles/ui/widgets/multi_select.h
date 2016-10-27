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

class InputField;

namespace Ui {

class IconButton;

class MultiSelect : public TWidget {
public:
	MultiSelect(QWidget *parent, const style::MultiSelect &st, const QString &placeholder = QString());

	QString getQuery() const;
	void setInnerFocus();
	void clearQuery();

	void setQueryChangedCallback(base::lambda_unique<void(const QString &query)> callback);
	void setSubmittedCallback(base::lambda_unique<void(bool ctrlShiftEnter)> callback);
	void setResizedCallback(base::lambda_unique<void()> callback);

	enum class AddItemWay {
		Default,
		SkipAnimation,
	};
	using PaintRoundImage = base::lambda_unique<void(Painter &p, int x, int y, int outerWidth, int size)>;
	void addItem(uint64 itemId, const QString &text, const style::color &color, PaintRoundImage paintRoundImage, AddItemWay way = AddItemWay::Default);
	void setItemText(uint64 itemId, const QString &text);

	void setItemRemovedCallback(base::lambda_unique<void(uint64 itemId)> callback);
	void removeItem(uint64 itemId);

protected:
	int resizeGetHeight(int newWidth) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void scrollTo(int activeTop, int activeBottom);

	const style::MultiSelect &_st;

	ChildWidget<ScrollArea> _scroll;

	class Inner;
	ChildWidget<Inner> _inner;

	base::lambda_unique<void()> _resizedCallback;
	base::lambda_unique<void(const QString &query)> _queryChangedCallback;

};

// This class is hold in header because it requires Qt preprocessing.
class MultiSelect::Inner : public ScrolledWidget {
	Q_OBJECT

public:
	using ScrollCallback = base::lambda_unique<void(int activeTop, int activeBottom)>;
	Inner(QWidget *parent, const style::MultiSelect &st, const QString &placeholder, ScrollCallback callback);

	QString getQuery() const;
	bool setInnerFocus();
	void clearQuery();

	void setQueryChangedCallback(base::lambda_unique<void(const QString &query)> callback);
	void setSubmittedCallback(base::lambda_unique<void(bool ctrlShiftEnter)> callback);

	class Item;
	void addItem(std_::unique_ptr<Item> item, AddItemWay way);
	void setItemText(uint64 itemId, const QString &text);

	void setItemRemovedCallback(base::lambda_unique<void(uint64 itemId)> callback);
	void removeItem(uint64 itemId);

	void setResizedCallback(base::lambda_unique<void(int heightDelta)> callback);

	~Inner();

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private slots:
	void onQueryChanged();
	void onSubmitted(bool ctrlShiftEnter) {
		if (_submittedCallback) {
			_submittedCallback(ctrlShiftEnter);
		}
	}
	void onFieldFocused();

private:
	void computeItemsGeometry(int newWidth);
	void updateItemsGeometry();
	void updateFieldGeometry();
	void updateHasAnyItems(bool hasAnyItems);
	void updateSelection(QPoint mousePosition);
	void clearSelection() {
		updateSelection(QPoint(-1, -1));
	}
	void updateCursor();
	void updateHeightStep();
	void finishHeightAnimation();
	enum class ChangeActiveWay {
		Default,
		SkipSetFocus,
	};
	void setActiveItem(int active, ChangeActiveWay skipSetFocus = ChangeActiveWay::Default);
	void setActiveItemPrevious();
	void setActiveItemNext();

	QMargins itemPaintMargins() const;

	const style::MultiSelect &_st;
	FloatAnimation _iconOpacity;

	ScrollCallback _scrollCallback;

	using Items = QList<Item*>;
	Items _items;
	using RemovingItems = OrderedSet<Item*>;
	RemovingItems _removingItems;

	int _selected = -1;
	int _active = -1;
	bool _overDelete = false;

	int _fieldLeft = 0;
	int _fieldTop = 0;
	int _fieldWidth = 0;
	ChildWidget<InputField> _field;
	ChildWidget<Ui::IconButton> _cancel;

	int _newHeight = 0;
	IntAnimation _height;

	base::lambda_unique<void(const QString &query)> _queryChangedCallback;
	base::lambda_unique<void(bool ctrlShiftEnter)> _submittedCallback;
	base::lambda_unique<void(uint64 itemId)> _itemRemovedCallback;
	base::lambda_unique<void(int heightDelta)> _resizedCallback;

};

} // namespace Ui
