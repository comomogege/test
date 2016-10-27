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

namespace Dialogs {

class Row;
class FakeRow;

namespace Layout {

const style::icon *ChatTypeIcon(PeerData *peer, bool active);

class RowPainter {
public:
	static void paint(Painter &p, const Row *row, int w, bool active, bool selected, bool onlyBackground);
	static void paint(Painter &p, const FakeRow *row, int w, bool active, bool selected, bool onlyBackground);
};

void paintImportantSwitch(Painter &p, Mode current, int w, bool selected, bool onlyBackground);

enum UnreadBadgeSize {
	UnreadBadgeInDialogs = 0,
	UnreadBadgeInHistoryToDown,
	UnreadBadgeInStickersPanel,
	UnreadBadgeInStickersBox,

	UnreadBadgeSizesCount
};
struct UnreadBadgeStyle {
	UnreadBadgeStyle();

	style::align align;
	bool active;
	bool muted;
	int size;
	UnreadBadgeSize sizeId;
	style::font font;
};
void paintUnreadCount(Painter &p, const QString &text, int x, int y, const UnreadBadgeStyle &st, int *outUnreadWidth = nullptr);

} // namespace Layout
} // namespace Dialogs
