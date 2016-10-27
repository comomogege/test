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

#include "ui/text/text.h"

class History;
class HistoryItem;

namespace Dialogs {
namespace Layout {
class RowPainter;
} // namespace Layout

class List;
class Row {
public:
	Row(History *history, Row *prev, Row *next, int pos)
		: _history(history)
		, _prev(prev)
		, _next(next)
		, _pos(pos) {
	}
	void *attached = nullptr; // for any attached data, for example View in contacts list

	History *history() const {
		return _history;
	}
	int pos() const {
		return _pos;
	}

private:
	friend class List;

	History *_history;
	Row *_prev, *_next;
	int _pos;

};

class FakeRow {
public:
	FakeRow(HistoryItem *item);

	HistoryItem *item() const {
		return _item;
	}

private:
	friend class Layout::RowPainter;

	HistoryItem *_item;
	mutable const HistoryItem *_cacheFor = nullptr;
	mutable Text _cache;

};

} // namespace Dialogs
