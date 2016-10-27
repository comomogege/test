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

#include "ui/twidget.h"
#include "window/slide_animation.h"

namespace Window {

class SectionMemento;

struct SectionSlideParams {
	QPixmap oldContentCache;
	bool withTopBarShadow = false;
};

class SectionWidget : public TWidget, protected base::Subscriber {
	Q_OBJECT

public:

	SectionWidget(QWidget *parent);

	virtual PeerData *peerForDialogs() const {
		return nullptr;
	}

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(const QRect &newGeometry, int topDelta);

	virtual bool hasTopBarShadow() const {
		return false;
	}
	void showAnimated(SlideDirection direction, const SectionSlideParams &params);

	// This can be used to grab with or without top bar shadow.
	// This will be protected when animation preparation will be done inside.
	virtual QPixmap grabForShowAnimation(const SectionSlideParams &params) {
		return myGrab(this);
	}

	// Attempt to show the required section inside the existing one.
	// For example if this section already shows exactly the required
	// memento it can simply return true - it is shown already.
	virtual bool showInternal(const SectionMemento *memento) = 0;

	// Create a memento of that section to store it in the history stack.
	virtual std_::unique_ptr<SectionMemento> createMemento() const = 0;

	virtual void setInnerFocus() {
		setFocus();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

	// Temp variable used in resizeEvent() implementation, that is passed
	// to setGeometryWithTopMoved() to adjust the scroll position with the resize.
	int topDelta() const {
		return _topDelta;
	}

	// Called after the hideChildren() call in showAnimated().
	virtual void showAnimatedHook() {
	}

	// Called after the showChildren() call in showFinished().
	virtual void showFinishedHook() {
	}

private:
	void showFinished();

	std_::unique_ptr<SlideAnimation> _showAnimation;

	// Saving here topDelta in resizeWithTopMoved() to get it passed to resizeEvent().
	int _topDelta = 0;

};

} // namespace Window
