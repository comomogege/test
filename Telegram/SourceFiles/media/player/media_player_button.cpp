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
#include "media/player/media_player_button.h"

namespace Media {
namespace Player {
namespace {

template <int N>
QPainterPath interpolatePaths(QPointF (&from)[N], QPointF (&to)[N], float64 k) {
	static_assert(N > 1, "Wrong points count in path!");

	auto from_coef = 1. - k, to_coef = k;
	QPainterPath result;
	auto x = from[0].x() * from_coef + to[0].x() * to_coef;
	auto y = from[0].y() * from_coef + to[0].y() * to_coef;
	result.moveTo(x, y);
	for (int i = 1; i != N; ++i) {
		result.lineTo(from[i].x() * from_coef + to[i].x() * to_coef, from[i].y() * from_coef + to[i].y() * to_coef);
	}
	result.lineTo(x, y);
	return result;
}

} // namespace

PlayButtonLayout::PlayButtonLayout(const style::MediaPlayerButton &st, UpdateCallback &&callback)
: _st(st)
, _callback(std_::move(callback)) {
}

void PlayButtonLayout::setState(State state) {
	if (_nextState == state) return;

	_nextState = state;
	if (!_transformProgress.animating(getms())) {
		_oldState = _state;
		_state = _nextState;
		_transformBackward = false;
		if (_state != _oldState) {
			startTransform(0., 1.);
			if (_callback) _callback();
		}
	} else if (_oldState == _nextState) {
		qSwap(_oldState, _state);
		startTransform(_transformBackward ? 0. : 1., _transformBackward ? 1. : 0.);
		_transformBackward = !_transformBackward;
	}
}

void PlayButtonLayout::finishTransform() {
	_transformProgress.finish();
	_transformBackward = false;
	if (_callback) _callback();
}

void PlayButtonLayout::paint(Painter &p, const QBrush &brush) {
	if (_transformProgress.animating(getms())) {
		auto from = _oldState, to = _state;
		auto backward = _transformBackward;
		auto progress = _transformProgress.current(1.);
		if (from == State::Cancel || (from == State::Pause && to == State::Play)) {
			qSwap(from, to);
			backward = !backward;
		}
		if (backward) progress = 1. - progress;

		t_assert(from != to);
		if (from == State::Play) {
			if (to == State::Pause) {
				paintPlayToPause(p, brush, progress);
			} else {
				t_assert(to == State::Cancel);
				paintPlayToCancel(p, brush, progress);
			}
		} else {
			t_assert(from == State::Pause && to == State::Cancel);
			paintPauseToCancel(p, brush, progress);
		}
	} else {
		switch (_state) {
		case State::Play: paintPlay(p, brush); break;
		case State::Pause: paintPlayToPause(p, brush, 1.); break;
		case State::Cancel: paintPlayToCancel(p, brush, 1.); break;
		}
	}
}

void PlayButtonLayout::paintPlay(Painter &p, const QBrush &brush) {
	auto playLeft = 0. + _st.playPosition.x();
	auto playTop = 0. + _st.playPosition.y();
	auto playWidth = _st.playOuter.width() - 2 * playLeft;
	auto playHeight = _st.playOuter.height() - 2 * playTop;

	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::HighQualityAntialiasing, true);

	QPainterPath pathPlay;
	pathPlay.moveTo(playLeft, playTop);
	pathPlay.lineTo(playLeft + playWidth, playTop + (playHeight / 2.));
	pathPlay.lineTo(playLeft, playTop + playHeight);
	pathPlay.lineTo(playLeft, playTop);
	p.fillPath(pathPlay, brush);

	p.setRenderHint(QPainter::HighQualityAntialiasing, false);
}

void PlayButtonLayout::paintPlayToPause(Painter &p, const QBrush &brush, float64 progress) {
	auto playLeft = 0. + _st.playPosition.x();
	auto playTop = 0. + _st.playPosition.y();
	auto playWidth = _st.playOuter.width() - 2 * playLeft;
	auto playHeight = _st.playOuter.height() - 2 * playTop;

	auto pauseLeft = 0. + _st.pausePosition.x();
	auto pauseTop = 0. + _st.pausePosition.y();
	auto pauseWidth = _st.pauseOuter.width() - 2 * pauseLeft;
	auto pauseHeight = _st.pauseOuter.height() - 2 * pauseTop;
	auto pauseStroke = 0. + _st.pauseStroke;

	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::HighQualityAntialiasing, true);

	QPointF pathLeftPause[] = {
		{ pauseLeft, pauseTop },
		{ pauseLeft + pauseStroke, pauseTop },
		{ pauseLeft + pauseStroke, pauseTop + pauseHeight },
		{ pauseLeft, pauseTop + pauseHeight },
	};
	QPointF pathLeftPlay[] = {
		{ playLeft, playTop },
		{ playLeft + (playWidth / 2.), playTop + (playHeight / 4.) },
		{ playLeft + (playWidth / 2.), playTop + (3 * playHeight / 4.) },
		{ playLeft, playTop + playHeight },
	};
	p.fillPath(interpolatePaths(pathLeftPlay, pathLeftPause, progress), brush);

	QPointF pathRightPause[] = {
		{ pauseLeft + pauseWidth - pauseStroke, pauseTop },
		{ pauseLeft + pauseWidth, pauseTop },
		{ pauseLeft + pauseWidth, pauseTop + pauseHeight },
		{ pauseLeft + pauseWidth - pauseStroke, pauseTop + pauseHeight },
	};
	QPointF pathRightPlay[] = {
		{ playLeft + (playWidth / 2.), playTop + (playHeight / 4.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + (playWidth / 2.), playTop + (3 * playHeight / 4.) },
	};
	p.fillPath(interpolatePaths(pathRightPlay, pathRightPause, progress), brush);

	p.setRenderHint(QPainter::HighQualityAntialiasing, false);
}

void PlayButtonLayout::paintPlayToCancel(Painter &p, const QBrush &brush, float64 progress) {
	static const auto sqrt2 = sqrt(2.);

	auto playLeft = 0. + _st.playPosition.x();
	auto playTop = 0. + _st.playPosition.y();
	auto playWidth = _st.playOuter.width() - 2 * playLeft;
	auto playHeight = _st.playOuter.height() - 2 * playTop;

	auto cancelLeft = 0. + _st.cancelPosition.x();
	auto cancelTop = 0. + _st.cancelPosition.y();
	auto cancelWidth = _st.cancelOuter.width() - 2 * cancelLeft;
	auto cancelHeight = _st.cancelOuter.height() - 2 * cancelTop;
	auto cancelStroke = (0. + _st.cancelStroke) / sqrt2;

	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::HighQualityAntialiasing, true);

	QPointF pathPlay[] = {
		{ playLeft, playTop },
		{ playLeft, playTop },
		{ playLeft + (playWidth / 2.), playTop + (playHeight / 4.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + (playWidth / 2.), playTop + (3 * playHeight / 4.) },
		{ playLeft, playTop + playHeight },
		{ playLeft, playTop + playHeight },
		{ playLeft, playTop + (playHeight / 2.) },
	};
	QPointF pathCancel[] = {
		{ cancelLeft, cancelTop + cancelStroke },
		{ cancelLeft + cancelStroke, cancelTop },
		{ cancelLeft + (cancelWidth / 2.), cancelTop + (cancelHeight / 2.) - cancelStroke },
		{ cancelLeft + cancelWidth - cancelStroke, cancelTop },
		{ cancelLeft + cancelWidth, cancelTop + cancelStroke },
		{ cancelLeft + (cancelWidth / 2.) + cancelStroke, cancelTop + (cancelHeight / 2.) },
		{ cancelLeft + cancelWidth, cancelTop + cancelHeight - cancelStroke },
		{ cancelLeft + cancelWidth - cancelStroke, cancelTop + cancelHeight },
		{ cancelLeft + (cancelWidth / 2.), cancelTop + (cancelHeight / 2.) + cancelStroke },
		{ cancelLeft + cancelStroke, cancelTop + cancelHeight },
		{ cancelLeft, cancelTop + cancelHeight - cancelStroke },
		{ cancelLeft + (cancelWidth / 2.) - cancelStroke, cancelTop + (cancelHeight / 2.) },
	};
	p.fillPath(interpolatePaths(pathPlay, pathCancel, progress), brush);

	p.setRenderHint(QPainter::HighQualityAntialiasing, false);
}

void PlayButtonLayout::paintPauseToCancel(Painter &p, const QBrush &brush, float64 progress) {
	static const auto sqrt2 = sqrt(2.);

	auto pauseLeft = 0. + _st.pausePosition.x();
	auto pauseTop = 0. + _st.pausePosition.y();
	auto pauseWidth = _st.pauseOuter.width() - 2 * pauseLeft;
	auto pauseHeight = _st.pauseOuter.height() - 2 * pauseTop;
	auto pauseStroke = 0. + _st.pauseStroke;

	auto cancelLeft = 0. + _st.cancelPosition.x();
	auto cancelTop = 0. + _st.cancelPosition.y();
	auto cancelWidth = _st.cancelOuter.width() - 2 * cancelLeft;
	auto cancelHeight = _st.cancelOuter.height() - 2 * cancelTop;
	auto cancelStroke = (0. + _st.cancelStroke) / sqrt2;

	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::HighQualityAntialiasing, true);

	QPointF pathLeftPause[] = {
		{ pauseLeft, pauseTop },
		{ pauseLeft + pauseStroke, pauseTop },
		{ pauseLeft + pauseStroke, pauseTop + pauseHeight },
		{ pauseLeft, pauseTop + pauseHeight },
	};
	QPointF pathLeftCancel[] = {
		{ cancelLeft, cancelTop + cancelStroke },
		{ cancelLeft + cancelStroke, cancelTop },
		{ cancelLeft + cancelWidth, cancelTop + cancelHeight - cancelStroke },
		{ cancelLeft + cancelWidth - cancelStroke, cancelTop + cancelHeight },
	};
	p.fillPath(interpolatePaths(pathLeftPause, pathLeftCancel, progress), brush);

	QPointF pathRightPause[] = {
		{ pauseLeft + pauseWidth - pauseStroke, pauseTop },
		{ pauseLeft + pauseWidth, pauseTop },
		{ pauseLeft + pauseWidth, pauseTop + pauseHeight },
		{ pauseLeft + pauseWidth - pauseStroke, pauseTop + pauseHeight },
	};
	QPointF pathRightCancel[] = {
		{ cancelLeft + cancelWidth - cancelStroke, cancelTop },
		{ cancelLeft + cancelWidth, cancelTop + cancelStroke },
		{ cancelLeft + cancelStroke, cancelTop + cancelHeight },
		{ cancelLeft, cancelTop + cancelHeight - cancelStroke },
	};
	p.fillPath(interpolatePaths(pathRightPause, pathRightCancel, progress), brush);

	p.setRenderHint(QPainter::HighQualityAntialiasing, false);
}

void PlayButtonLayout::animationCallback() {
	if (!_transformProgress.animating()) {
		auto finalState = _nextState;
		_nextState = _state;
		setState(finalState);
	}
	_callback();
}

void PlayButtonLayout::startTransform(float64 from, float64 to) {
	_transformProgress.start([this] { animationCallback(); }, from, to, st::mediaPlayerButtonTransformDuration);
}

} // namespace Player
} // namespace Media
