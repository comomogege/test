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

class ActionsWidget : public BlockWidget {
	Q_OBJECT

public:
	ActionsWidget(QWidget *parent, PeerData *peer);

protected:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private slots:
	void onBotHelp();
	void onBotSettings();
	void onClearHistory();
	void onClearHistorySure();
	void onDeleteConversation();
	void onDeleteConversationSure();
	void onBlockUser();
	void onUpgradeToSupergroup();
	void onDeleteChannel();
	void onDeleteChannelSure();
	void onLeaveChannel();
	void onLeaveChannelSure();
	void onReport();

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void validateBlockStatus() const;

	int buttonsBottom() const;

	void refreshButtons();
	void refreshBlockUser();
	void refreshDeleteChannel();
	void refreshLeaveChannel();
	void refreshVisibility();

	Ui::LeftOutlineButton *addButton(const QString &text, const char *slot
		, const style::OutlineButton &st = st::defaultLeftOutlineButton, int skipHeight = 0);
	void resizeButton(Ui::LeftOutlineButton *button, int newWidth, int top);

	QString getBlockButtonText() const;
	bool hasBotCommand(const QString &command) const;
	void sendBotCommand(const QString &command);

	QList<Ui::LeftOutlineButton*> _buttons;
	//ChildWidget<Ui::LeftOutlineButton> _botHelp = { nullptr };
	//ChildWidget<Ui::LeftOutlineButton> _botSettings = { nullptr };
	//ChildWidget<Ui::LeftOutlineButton> _reportChannel = { nullptr };
	//ChildWidget<Ui::LeftOutlineButton> _leaveChannel = { nullptr };
	//ChildWidget<Ui::LeftOutlineButton> _deleteChannel = { nullptr };
	//ChildWidget<Ui::LeftOutlineButton> _upgradeToSupergroup = { nullptr };
	//ChildWidget<Ui::LeftOutlineButton> _clearHistory = { nullptr };
	//ChildWidget<Ui::LeftOutlineButton> _deleteConversation = { nullptr };
	//ChildWidget<Ui::LeftOutlineButton> _blockUser = { nullptr };

	// Hold some button pointers to update / toggle them.
	bool _hasBotHelp = false;
	bool _hasBotSettings = false;
	Ui::LeftOutlineButton *_blockUser = nullptr;
	Ui::LeftOutlineButton *_deleteChannel = nullptr;
	Ui::LeftOutlineButton *_leaveChannel = nullptr;

};

} // namespace Profile
