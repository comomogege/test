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

#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtGui/QCursor>
#include <QtGui/QFont>

#include "ui/animation.h"
#include "ui/style/style_core_font.h"
#include "ui/style/style_core_color.h"
#include "ui/style/style_core_icon.h"

namespace style {
namespace internal {

void loadSprite();
int spriteWidth();
void destroySprite();

class Sprite {
public:
	Sprite() {
	}
	Sprite(int left, int top, int width, int height)
		: _rect(rtl() ? (spriteWidth() - left * cIntRetinaFactor() - width * cIntRetinaFactor()) : left * cIntRetinaFactor(),
				top * cIntRetinaFactor(),
				width * cIntRetinaFactor(),
				height * cIntRetinaFactor()) {
	}
	int pxWidth() const {
		return _rect.width() / cIntRetinaFactor();
	}
	int pxHeight() const {
		return _rect.height() / cIntRetinaFactor();
	}
	QSize pxSize() const {
		return _rect.size() / cIntRetinaFactor();
	}
	QRect rect() const {
		return _rect;
	}
	bool isEmpty() const {
		return _rect.isEmpty();
	}

private:
	QRect _rect;

};
inline bool operator==(const Sprite &a, const Sprite &b) {
	return a.rect() == b.rect();
}
inline bool operator!=(const Sprite &a, const Sprite &b) {
	return !(a == b);
}

} // namespace internal

using string = QString;
using rect = QRect;
using point = QPoint;
using size = QSize;
using transition = anim::transition;
using cursor = Qt::CursorShape;
using align = Qt::Alignment;
using margins = QMargins;
using font = internal::Font;
using color = internal::Color;
using sprite = internal::Sprite;
using icon = internal::Icon;

static constexpr cursor cur_default = Qt::ArrowCursor;
static constexpr cursor cur_pointer = Qt::PointingHandCursor;
static constexpr cursor cur_text = Qt::IBeamCursor;
static constexpr cursor cur_cross = Qt::CrossCursor;
static constexpr cursor cur_sizever = Qt::SizeVerCursor;
static constexpr cursor cur_sizehor = Qt::SizeHorCursor;
static constexpr cursor cur_sizebdiag = Qt::SizeBDiagCursor;
static constexpr cursor cur_sizefdiag = Qt::SizeFDiagCursor;
static constexpr cursor cur_sizeall = Qt::SizeAllCursor;

static const align al_topleft = (Qt::AlignTop | Qt::AlignLeft);
static const align al_top = (Qt::AlignTop | Qt::AlignHCenter);
static const align al_topright = (Qt::AlignTop | Qt::AlignRight);
static const align al_right = (Qt::AlignVCenter | Qt::AlignRight);
static const align al_bottomright = (Qt::AlignBottom | Qt::AlignRight);
static const align al_bottom = (Qt::AlignBottom | Qt::AlignHCenter);
static const align al_bottomleft = (Qt::AlignBottom | Qt::AlignLeft);
static const align al_left = (Qt::AlignVCenter | Qt::AlignLeft);
static const align al_center = (Qt::AlignVCenter | Qt::AlignHCenter);

const QPixmap &spritePixmap();

} // namespace style
