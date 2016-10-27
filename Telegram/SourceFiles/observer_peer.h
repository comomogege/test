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

#include "core/observer.h"

namespace Notify {

// Generic notifications about updates of some PeerData.
// You can subscribe to them by Notify::registerPeerObserver().
// 0x0000FFFFU for general peer updates (valid for any peer).
// 0xFFFF0000U for specific peer updates (valid for user / chat / channel).
struct PeerUpdate {
	PeerUpdate(PeerData *updated = nullptr) : peer(updated) {
	}
	PeerData *peer;

	enum class Flag {
		// Common flags
		NameChanged           = 0x00000001U,
		UsernameChanged       = 0x00000002U,
		PhotoChanged          = 0x00000004U,
		AboutChanged          = 0x00000008U,
		NotificationsEnabled  = 0x00000010U,
		SharedMediaChanged    = 0x00000020U,
		MigrationChanged      = 0x00000040U,

		// For chats and channels
		InviteLinkChanged     = 0x00000020U,
		MembersChanged        = 0x00000040U,
		AdminsChanged         = 0x00000080U,

		// For users
		UserCanShareContact   = 0x00010000U,
		UserIsContact         = 0x00020000U,
		UserPhoneChanged      = 0x00040000U,
		UserIsBlocked         = 0x00080000U,
		BotCommandsChanged    = 0x00100000U,
		UserOnlineChanged     = 0x00200000U,
		BotCanAddToGroups     = 0x00400000U,

		// For chats
		ChatCanEdit           = 0x00010000U,

		// For channels
		ChannelAmIn           = 0x00010000U,
		ChannelAmEditor       = 0x00020000U,
		ChannelCanEditPhoto   = 0x00040000U,
		ChannelCanAddMembers  = 0x00080000U,
		ChannelCanViewAdmins  = 0x00100000U,
		ChannelCanViewMembers = 0x00200000U,
	};
	Q_DECLARE_FLAGS(Flags, Flag);
	Flags flags = 0;

	// NameChanged data
	PeerData::Names oldNames;
	PeerData::NameFirstChars oldNameFirstChars;

	// SharedMediaChanged data
	int32 mediaTypesMask = 0;

};
Q_DECLARE_OPERATORS_FOR_FLAGS(PeerUpdate::Flags);

void peerUpdatedDelayed(const PeerUpdate &update);
inline void peerUpdatedDelayed(PeerData *peer, PeerUpdate::Flags events) {
	PeerUpdate update(peer);
	update.flags = events;
	peerUpdatedDelayed(update);
}
void peerUpdatedSendDelayed();

class PeerUpdatedHandler {
public:
	template <typename Lambda>
	PeerUpdatedHandler(PeerUpdate::Flags events, Lambda &&handler) : _events(events), _handler(std_::move(handler)) {
	}
	void operator()(const PeerUpdate &update) const {
		if (update.flags & _events) {
			_handler(update);
		}
	}

private:
	PeerUpdate::Flags _events;
	base::lambda_unique<void(const PeerUpdate&)> _handler;

};
base::Observable<PeerUpdate, PeerUpdatedHandler> &PeerUpdated();

} // namespace Notify
