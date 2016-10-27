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
#include "profile/profile_settings_widget.h"

#include "styles/style_profile.h"
#include "ui/buttons/left_outline_button.h"
#include "ui/flatcheckbox.h"
#include "boxes/confirmbox.h"
#include "boxes/contactsbox.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "lang.h"

#include "mainwindow.h" // tmp

namespace Profile {

using UpdateFlag = Notify::PeerUpdate::Flag;

SettingsWidget::SettingsWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_settings_section))
, _enableNotifications(this, lang(lng_profile_enable_notifications), true, st::defaultCheckbox) {
	connect(_enableNotifications, SIGNAL(changed()), this, SLOT(onNotificationsChange()));

	Notify::PeerUpdate::Flags observeEvents = UpdateFlag::NotificationsEnabled;
	if (auto chat = peer->asChat()) {
		if (chat->amCreator()) {
			observeEvents |= UpdateFlag::ChatCanEdit | UpdateFlag::InviteLinkChanged;
		}
	} else if (auto channel = peer->asChannel()) {
		if (channel->amCreator()) {
			observeEvents |= UpdateFlag::UsernameChanged | UpdateFlag::InviteLinkChanged;
		}
	}
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	refreshButtons();
	_enableNotifications->finishAnimations();

	show();
}

void SettingsWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		return;
	}

	if (update.flags & UpdateFlag::NotificationsEnabled) {
		refreshEnableNotifications();
	}
	if (update.flags & (UpdateFlag::ChatCanEdit | UpdateFlag::UsernameChanged | UpdateFlag::InviteLinkChanged)) {
		refreshInviteLinkButton();
	}
	if (update.flags & (UpdateFlag::ChatCanEdit)) {
		refreshManageAdminsButton();
	}

	contentSizeUpdated();
}

int SettingsWidget::resizeGetHeight(int newWidth) {
	int newHeight = contentTop() + st::profileEnableNotificationsTop;

	_enableNotifications->moveToLeft(st::profileBlockTitlePosition.x(), newHeight);
	newHeight += _enableNotifications->height() + st::profileSettingsBlockSkip;

	auto moveLink = [&newHeight, newWidth](Ui::LeftOutlineButton *button) {
		if (!button) return;

		int left = defaultOutlineButtonLeft();
		int availableWidth = newWidth - left - st::profileBlockMarginRight;
		accumulate_min(availableWidth, st::profileBlockOneLineWidthMax);
		button->resizeToWidth(availableWidth);
		button->moveToLeft(left, newHeight);
		newHeight += button->height();
	};
	moveLink(_manageAdmins);
	moveLink(_inviteLink);

	newHeight += st::profileBlockMarginBottom;
	return newHeight;
}

void SettingsWidget::refreshButtons() {
	refreshEnableNotifications();
	refreshManageAdminsButton();
	refreshInviteLinkButton();
}

void SettingsWidget::refreshEnableNotifications() {
	if (peer()->notify == UnknownNotifySettings) {
		App::api()->requestNotifySetting(peer());
	} else {
		auto &notifySettings = peer()->notify;
		bool enabled = (notifySettings == EmptyNotifySettings || notifySettings->mute < unixtime());
		_enableNotifications->setChecked(enabled, Checkbox::NotifyAboutChange::DontNotify);
	}
}

void SettingsWidget::refreshManageAdminsButton() {
	auto hasManageAdmins = [this]() {
		if (auto chat = peer()->asChat()) {
			return (chat->amCreator() && chat->canEdit());
		} else if (auto channel = peer()->asChannel()) {
			return (channel->amCreator() && channel->isMegagroup());
		}
		return false;
	};
	_manageAdmins.destroy();
	if (hasManageAdmins()) {
		_manageAdmins = new Ui::LeftOutlineButton(this, lang(lng_profile_manage_admins), st::defaultLeftOutlineButton);
		_manageAdmins->show();
		connect(_manageAdmins, SIGNAL(clicked()), this, SLOT(onManageAdmins()));
	}
}

void SettingsWidget::refreshInviteLinkButton() {
	auto getInviteLinkText = [this]() -> QString {
		if (auto chat = peer()->asChat()) {
			if (chat->amCreator() && chat->canEdit()) {
				return lang(chat->inviteLink().isEmpty() ? lng_group_invite_create : lng_group_invite_create_new);
			}
		} else if (auto channel = peer()->asChannel()) {
			if (channel->amCreator() && !channel->isPublic()) {
				return lang(channel->inviteLink().isEmpty() ? lng_group_invite_create : lng_group_invite_create_new);
			}
		}
		return QString();
	};
	auto inviteLinkText = getInviteLinkText();
	if (inviteLinkText.isEmpty()) {
		_inviteLink.destroy();
	} else {
		_inviteLink = new Ui::LeftOutlineButton(this, inviteLinkText, st::defaultLeftOutlineButton);
		_inviteLink->show();
		connect(_inviteLink, SIGNAL(clicked()), this, SLOT(onInviteLink()));
	}
}

void SettingsWidget::onNotificationsChange() {
	App::main()->updateNotifySetting(peer(), _enableNotifications->checked() ? NotifySettingSetNotify : NotifySettingSetMuted);
}

void SettingsWidget::onManageAdmins() {
	if (auto chat = peer()->asChat()) {
		Ui::showLayer(new ContactsBox(chat, MembersFilter::Admins));
	} else if (auto channel = peer()->asChannel()) {
		Ui::showLayer(new MembersBox(channel, MembersFilter::Admins));
	}
}

void SettingsWidget::onInviteLink() {
	auto getInviteLink = [this]() {
		if (auto chat = peer()->asChat()) {
			return chat->inviteLink();
		} else if (auto channel = peer()->asChannel()) {
			return channel->inviteLink();
		}
		return QString();
	};
	auto link = getInviteLink();

	ConfirmBox *box = new ConfirmBox(lang(link.isEmpty() ? lng_group_invite_about : lng_group_invite_about_new));
	connect(box, SIGNAL(confirmed()), this, SLOT(onInviteLinkSure()));
	Ui::showLayer(box);
}

void SettingsWidget::onInviteLinkSure() {
	Ui::hideLayer();
	App::api()->exportInviteLink(peer());
}

} // namespace Profile
