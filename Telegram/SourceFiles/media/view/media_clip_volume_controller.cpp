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
#include "media/view/media_clip_volume_controller.h"

#include "styles/style_mediaview.h"

namespace Media {
namespace Clip {

VolumeController::VolumeController(QWidget *parent) : TWidget(parent) {
	resize(st::mediaviewVolumeSize);
	setCursor(style::cur_pointer);
	setMouseTracking(true);
}

void VolumeController::setVolume(float64 volume) {
	_volume = volume;
	update();
}

void VolumeController::paintEvent(QPaintEvent *e) {
	Painter p(this);

	int32 top = st::mediaviewVolumeIconTop;
	int32 left = (width() - st::mediaviewVolumeIcon.width()) / 2;
	int32 mid = left + qRound(st::mediaviewVolumeIcon.width() * _volume);
	int32 right = left + st::mediaviewVolumeIcon.width();

	if (mid > left) {
		auto over = _a_over.current(getms(), _over ? 1. : 0.);
		p.setOpacity(over * st::mediaviewActiveOpacity + (1. - over) * st::mediaviewInactiveOpacity);
		p.setClipRect(rtlrect(left, top, mid - left, st::mediaviewVolumeIcon.height(), width()));
		st::mediaviewVolumeOnIcon.paint(p, QPoint(left, top), width());
	}
	if (right > mid) {
		p.setClipRect(rtlrect(mid, top, right - mid, st::mediaviewVolumeIcon.height(), width()));
		st::mediaviewVolumeIcon.paint(p, QPoint(left, top), width());
	}
}

void VolumeController::mouseMoveEvent(QMouseEvent *e) {
	if (_downCoord < 0) {
		return;
	}
	int delta = e->pos().x() - _downCoord;
	int left = (width() - st::mediaviewVolumeIcon.width()) / 2;
	float64 startFrom = snap((_downCoord - left) / float64(st::mediaviewVolumeIcon.width()), 0., 1.);
	float64 add = delta / float64(4 * st::mediaviewVolumeIcon.width());
	auto newVolume = snap(startFrom + add, 0., 1.);
	changeVolume(newVolume);
}

void VolumeController::mousePressEvent(QMouseEvent *e) {
	_downCoord = snap(e->pos().x(), 0, width());
	int left = (width() - st::mediaviewVolumeIcon.width()) / 2;
	auto newVolume = snap((_downCoord - left) / float64(st::mediaviewVolumeIcon.width()), 0., 1.);
	changeVolume(newVolume);
}

void VolumeController::changeVolume(float64 newVolume) {
	if (newVolume != _volume) {
		setVolume(newVolume);
		emit volumeChanged(_volume);
	}
}

void VolumeController::mouseReleaseEvent(QMouseEvent *e) {
	_downCoord = -1;
}

void VolumeController::enterEvent(QEvent *e) {
	setOver(true);
}

void VolumeController::leaveEvent(QEvent *e) {
	setOver(false);
}

void VolumeController::setOver(bool over) {
	if (_over == over) return;

	_over = over;
	auto from = _over ? 0. : 1., to = _over ? 1. : 0.;
	_a_over.start([this] { update(); }, from, to, st::mediaviewOverDuration);
}

} // namespace Clip
} // namespace Media
