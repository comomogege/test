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

#include "profile/profile_block_widget.h"

namespace Ui {
class LeftOutlineButton;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {

class SharedMediaWidget : public BlockWidget {
	Q_OBJECT

public:
	SharedMediaWidget(QWidget *parent, PeerData *peer);

protected:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private slots:
	void onMediaChosen();

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void refreshButtons();
	void refreshButton(MediaOverviewType type);
	void refreshVisibility();

	void resizeButtons(int newWidth, int *top);

	Ui::LeftOutlineButton *_mediaButtons[OverviewCount] = { nullptr };
	History *_history;
	History *_migrated;

};

} // namespace Profile
