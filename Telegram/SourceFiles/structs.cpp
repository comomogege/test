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
#include "structs.h"

#include "lang.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "mainwindow.h"
#include "ui/filedialog.h"
#include "apiwrap.h"
#include "boxes/confirmbox.h"
#include "media/media_audio.h"
#include "localstorage.h"
#include "history/history_media_types.h"
#include "styles/style_history.h"

namespace {

int peerColorIndex(const PeerId &peer) {
	auto myId = MTP::authedId();
	auto peerId = peerToBareInt(peer);
	auto both = (QByteArray::number(peerId) + QByteArray::number(myId)).mid(0, 15);
	uchar md5[16];
	hashMd5(both.constData(), both.size(), md5);
	return (md5[peerId & 0x0F] & (peerIsUser(peer) ? 0x07 : 0x03));
}

struct ColorReferenceWrap {
	ColorReferenceWrap(const style::color &data) : data(data) {
	}
	const style::color &data;
};

ImagePtr generateUserpicImage(const style::icon &icon) {
	auto data = QImage(icon.width() * cIntRetinaFactor(), icon.height() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	data.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&data);
		icon.paint(p, 0, 0, icon.width());
	}
	return ImagePtr(App::pixmapFromImageInPlace(std_::move(data)), "PNG");
}

} // namespace

const style::color &peerColor(int index) {
	static const ColorReferenceWrap peerColors[kUserColorsCount] = {
		st::historyPeer1NameFg,
		st::historyPeer2NameFg,
		st::historyPeer3NameFg,
		st::historyPeer4NameFg,
		st::historyPeer5NameFg,
		st::historyPeer6NameFg,
		st::historyPeer7NameFg,
		st::historyPeer8NameFg,
	};
	return peerColors[index].data;
}

ImagePtr userDefPhoto(int index) {
	static const ImagePtr userDefPhotos[kUserColorsCount] = {
		generateUserpicImage(st::historyPeer1UserpicPerson),
		generateUserpicImage(st::historyPeer2UserpicPerson),
		generateUserpicImage(st::historyPeer3UserpicPerson),
		generateUserpicImage(st::historyPeer4UserpicPerson),
		generateUserpicImage(st::historyPeer5UserpicPerson),
		generateUserpicImage(st::historyPeer6UserpicPerson),
		generateUserpicImage(st::historyPeer7UserpicPerson),
		generateUserpicImage(st::historyPeer8UserpicPerson),
	};
	return userDefPhotos[index];
}

ImagePtr chatDefPhoto(int index) {
	static const ImagePtr chatDefPhotos[kChatColorsCount] = {
		generateUserpicImage(st::historyPeer1UserpicChat),
		generateUserpicImage(st::historyPeer2UserpicChat),
		generateUserpicImage(st::historyPeer3UserpicChat),
		generateUserpicImage(st::historyPeer4UserpicChat),
	};
	return chatDefPhotos[index];
}

ImagePtr channelDefPhoto(int index) {
	static const ImagePtr channelDefPhotos[kChannelColorsCount] = {
		generateUserpicImage(st::historyPeer1UserpicChannel),
		generateUserpicImage(st::historyPeer2UserpicChannel),
		generateUserpicImage(st::historyPeer3UserpicChannel),
		generateUserpicImage(st::historyPeer4UserpicChannel),
	};
	return channelDefPhotos[index];
}

using UpdateFlag = Notify::PeerUpdate::Flag;

NotifySettings globalNotifyAll, globalNotifyUsers, globalNotifyChats;
NotifySettingsPtr globalNotifyAllPtr = UnknownNotifySettings, globalNotifyUsersPtr = UnknownNotifySettings, globalNotifyChatsPtr = UnknownNotifySettings;

PeerData::PeerData(const PeerId &id) : id(id)
, colorIndex(peerColorIndex(id))
, color(peerColor(colorIndex))
, _userpic(isUser() ? userDefPhoto(colorIndex) : ((isChat() || isMegagroup()) ? chatDefPhoto(colorIndex) : channelDefPhoto(colorIndex))) {
	nameText.setText(st::msgNameFont, QString(), _textNameOptions);
}

void PeerData::updateNameDelayed(const QString &newName, const QString &newNameOrPhone, const QString &newUsername) {
	if (name == newName) {
		if (isUser()) {
			if (asUser()->nameOrPhone == newNameOrPhone && asUser()->username == newUsername) {
				return;
			}
		} else if (isChannel()) {
			if (asChannel()->username == newUsername) {
				return;
			}
		} else if (isChat()) {
			return;
		}
	}

	++nameVersion;
	name = newName;
	nameText.setText(st::msgNameFont, name, _textNameOptions);

	Notify::PeerUpdate update(this);
	update.flags |= UpdateFlag::NameChanged;
	update.oldNames = names;
	update.oldNameFirstChars = chars;

	if (isUser()) {
		if (asUser()->username != newUsername) {
			asUser()->username = newUsername;
			update.flags |= UpdateFlag::UsernameChanged;
		}
		asUser()->setNameOrPhone(newNameOrPhone);
	} else if (isChannel()) {
		if (asChannel()->username != newUsername) {
			asChannel()->username = newUsername;
			if (newUsername.isEmpty()) {
				asChannel()->flags &= ~MTPDchannel::Flag::f_username;
			} else {
				asChannel()->flags |= MTPDchannel::Flag::f_username;
			}
			update.flags |= UpdateFlag::UsernameChanged;
		}
	}
	fillNames();
	if (App::main()) {
		emit App::main()->peerNameChanged(this, update.oldNames, update.oldNameFirstChars);
	}
	Notify::peerUpdatedDelayed(update);
}

void PeerData::setUserpic(ImagePtr userpic) {
	_userpic = userpic;
}

ImagePtr PeerData::currentUserpic() const {
	if (_userpic->loaded()) {
		return _userpic;
	}
	_userpic->load();

	if (isUser()) {
		return userDefPhoto(colorIndex);
	} else if (isMegagroup() || isChat()) {
		return chatDefPhoto(colorIndex);
	}
	return channelDefPhoto(colorIndex);
}

void PeerData::paintUserpic(Painter &p, int size, int x, int y) const {
	p.drawPixmap(x, y, currentUserpic()->pixCircled(size, size));
}

StorageKey PeerData::userpicUniqueKey() const {
	if (photoLoc.isNull() || !_userpic->loaded()) {
		return StorageKey(0, (isUser() ? 0x1000 : ((isChat() || isMegagroup()) ? 0x2000 : 0x3000)) | colorIndex);
	}
	return storageKey(photoLoc);
}

void PeerData::saveUserpic(const QString &path, int size) const {
	currentUserpic()->pixRounded(ImageRoundRadius::Small, size, size).save(path, "PNG");
}

QPixmap PeerData::genUserpic(int size) const {
	return currentUserpic()->pixRounded(ImageRoundRadius::Small, size, size);
}

const Text &BotCommand::descriptionText() const {
	if (_descriptionText.isEmpty() && !_description.isEmpty()) {
		_descriptionText.setText(st::mentionFont, _description, _textNameOptions);
	}
	return _descriptionText;
}

bool UserData::canShareThisContact() const {
	return canShareThisContactFast() || !App::phoneFromSharedContact(peerToUser(id)).isEmpty();
}

void UserData::setPhoto(const MTPUserProfilePhoto &p) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = _userpic;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_userProfilePhoto: {
		const auto &d(p.c_userProfilePhoto());
		newPhotoId = d.vphoto_id.v;
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? userDefPhoto(colorIndex) : ImagePtr(newPhotoLoc);
		//App::feedPhoto(App::photoFromUserPhoto(peerToUser(id), MTP_int(unixtime()), p));
	} break;
	default: {
		newPhotoId = 0;
		if (id == ServiceUserId) {
			if (_userpic.v() == userDefPhoto(colorIndex).v()) {
				newPhoto = ImagePtr(App::pixmapFromImageInPlace(App::wnd()->iconLarge().scaledToWidth(160, Qt::SmoothTransformation)), "PNG");
			}
		} else {
			newPhoto = userDefPhoto(colorIndex);
		}
		newPhotoLoc = StorageImageLocation();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != _userpic.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		setUserpic(newPhoto);
		photoLoc = newPhotoLoc;
		if (App::main()) {
			emit App::main()->peerPhotoChanged(this);
		}
		Notify::peerUpdatedDelayed(this, UpdateFlag::PhotoChanged);
	}
}

void PeerData::fillNames() {
	names.clear();
	chars.clear();
	QString toIndex = textAccentFold(name);
	if (cRussianLetters().match(toIndex).hasMatch()) {
		toIndex += ' ' + translitRusEng(toIndex);
	}
	if (isUser()) {
		if (!asUser()->nameOrPhone.isEmpty() && asUser()->nameOrPhone != name) toIndex += ' ' + textAccentFold(asUser()->nameOrPhone);
		if (!asUser()->username.isEmpty()) toIndex += ' ' + textAccentFold(asUser()->username);
	} else if (isChannel()) {
		if (!asChannel()->username.isEmpty()) toIndex += ' ' + textAccentFold(asChannel()->username);
	}
	toIndex += ' ' + rusKeyboardLayoutSwitch(toIndex);

	QStringList namesList = toIndex.toLower().split(cWordSplit(), QString::SkipEmptyParts);
	for (QStringList::const_iterator i = namesList.cbegin(), e = namesList.cend(); i != e; ++i) {
		names.insert(*i);
		chars.insert(i->at(0));
	}
}

bool UserData::setAbout(const QString &newAbout) {
	if (_about == newAbout) {
		return false;
	}
	_about = newAbout;
	Notify::peerUpdatedDelayed(this, UpdateFlag::AboutChanged);
	return true;
}

void UserData::setName(const QString &newFirstName, const QString &newLastName, const QString &newPhoneName, const QString &newUsername) {
	bool changeName = !newFirstName.isEmpty() || !newLastName.isEmpty();

	QString newFullName;
	if (changeName && newFirstName.trimmed().isEmpty()) {
		firstName = newLastName;
		lastName = QString();
		newFullName = firstName;
	} else {
		if (changeName) {
			firstName = newFirstName;
			lastName = newLastName;
		}
		newFullName = lastName.isEmpty() ? firstName : lng_full_name(lt_first_name, firstName, lt_last_name, lastName);
	}
	updateNameDelayed(newFullName, newPhoneName, newUsername);
}

void UserData::setPhone(const QString &newPhone) {
	_phone = newPhone;
}

void UserData::setBotInfoVersion(int version) {
	if (version < 0) {
		if (botInfo) {
			if (!botInfo->commands.isEmpty()) {
				botInfo->commands.clear();
				Notify::botCommandsChanged(this);
			}
			botInfo = nullptr;
			Notify::userIsBotChanged(this);
		}
	} else if (!botInfo) {
		botInfo = std_::make_unique<BotInfo>();
		botInfo->version = version;
		Notify::userIsBotChanged(this);
	} else if (botInfo->version < version) {
		if (!botInfo->commands.isEmpty()) {
			botInfo->commands.clear();
			Notify::botCommandsChanged(this);
		}
		botInfo->description.clear();
		botInfo->version = version;
		botInfo->inited = false;
	}
}

void UserData::setBotInfo(const MTPBotInfo &info) {
	switch (info.type()) {
	case mtpc_botInfo: {
		const auto &d(info.c_botInfo());
		if (peerFromUser(d.vuser_id.v) != id || !botInfo) return;

		QString desc = qs(d.vdescription);
		if (botInfo->description != desc) {
			botInfo->description = desc;
			botInfo->text = Text(st::msgMinWidth);
		}

		const auto &v(d.vcommands.c_vector().v);
		botInfo->commands.reserve(v.size());
		bool changedCommands = false;
		int32 j = 0;
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			if (v.at(i).type() != mtpc_botCommand) continue;

			QString cmd = qs(v.at(i).c_botCommand().vcommand), desc = qs(v.at(i).c_botCommand().vdescription);
			if (botInfo->commands.size() <= j) {
				botInfo->commands.push_back(BotCommand(cmd, desc));
				changedCommands = true;
			} else {
				if (botInfo->commands[j].command != cmd) {
					botInfo->commands[j].command = cmd;
					changedCommands = true;
				}
				if (botInfo->commands[j].setDescription(desc)) {
					changedCommands = true;
				}
			}
			++j;
		}
		while (j < botInfo->commands.size()) {
			botInfo->commands.pop_back();
			changedCommands = true;
		}

		botInfo->inited = true;

		if (changedCommands) {
			Notify::botCommandsChanged(this);
		}
	} break;
	}
}

void UserData::setNameOrPhone(const QString &newNameOrPhone) {
	if (nameOrPhone != newNameOrPhone) {
		nameOrPhone = newNameOrPhone;
		phoneText.setText(st::msgNameFont, nameOrPhone, _textNameOptions);
	}
}

void UserData::madeAction(TimeId when) {
	if (botInfo || isServiceUser(id) || when <= 0) return;

	if (onlineTill <= 0 && -onlineTill < when) {
		onlineTill = -when - SetOnlineAfterActivity;
		App::markPeerUpdated(this);
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::UserOnlineChanged);
	} else if (onlineTill > 0 && onlineTill < when + 1) {
		onlineTill = when + SetOnlineAfterActivity;
		App::markPeerUpdated(this);
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::UserOnlineChanged);
	}
}

void UserData::setBlockStatus(BlockStatus blockStatus) {
	if (blockStatus != _blockStatus) {
		_blockStatus = blockStatus;
		Notify::peerUpdatedDelayed(this, UpdateFlag::UserIsBlocked);
	}
}

void ChatData::setPhoto(const MTPChatPhoto &p, const PhotoId &phId) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = _userpic;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_chatPhoto: {
		const auto &d(p.c_chatPhoto());
		if (phId != UnknownPeerPhotoId) {
			newPhotoId = phId;
		}
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? chatDefPhoto(colorIndex) : ImagePtr(newPhotoLoc);
//		photoFull = ImagePtr(640, 640, d.vphoto_big, chatDefPhoto(colorIndex));
	} break;
	default: {
		newPhotoId = 0;
		newPhotoLoc = StorageImageLocation();
		newPhoto = chatDefPhoto(colorIndex);
//		photoFull = ImagePtr();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != _userpic.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		setUserpic(newPhoto);
		photoLoc = newPhotoLoc;
		if (App::main()) {
			emit App::main()->peerPhotoChanged(this);
		}
		Notify::peerUpdatedDelayed(this, UpdateFlag::PhotoChanged);
	}
}

void ChatData::setName(const QString &newName) {
	updateNameDelayed(newName.isEmpty() ? name : newName, QString(), QString());
}

void ChatData::invalidateParticipants() {
	auto wasCanEdit = canEdit();
	participants = ChatData::Participants();
	admins = ChatData::Admins();
	flags &= ~MTPDchat::Flag::f_admin;
	invitedByMe = ChatData::InvitedByMe();
	botStatus = 0;
	if (wasCanEdit != canEdit()) {
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::ChatCanEdit);
	}
	Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::MembersChanged | Notify::PeerUpdate::Flag::AdminsChanged);
}

void ChatData::setInviteLink(const QString &newInviteLink) {
	if (newInviteLink != _inviteLink) {
		_inviteLink = newInviteLink;
		Notify::peerUpdatedDelayed(this, UpdateFlag::InviteLinkChanged);
	}
}

void ChannelData::setPhoto(const MTPChatPhoto &p, const PhotoId &phId) { // see Local::readPeer as well
	PhotoId newPhotoId = photoId;
	ImagePtr newPhoto = _userpic;
	StorageImageLocation newPhotoLoc = photoLoc;
	switch (p.type()) {
	case mtpc_chatPhoto: {
		const auto &d(p.c_chatPhoto());
		if (phId != UnknownPeerPhotoId) {
			newPhotoId = phId;
		}
		newPhotoLoc = App::imageLocation(160, 160, d.vphoto_small);
		newPhoto = newPhotoLoc.isNull() ? (isMegagroup() ? chatDefPhoto(colorIndex) : channelDefPhoto(colorIndex)) : ImagePtr(newPhotoLoc);
//		photoFull = ImagePtr(640, 640, d.vphoto_big, (isMegagroup() ? chatDefPhoto(colorIndex) : channelDefPhoto(colorIndex)));
	} break;
	default: {
		newPhotoId = 0;
		newPhotoLoc = StorageImageLocation();
		newPhoto = (isMegagroup() ? chatDefPhoto(colorIndex) : channelDefPhoto(colorIndex));
//		photoFull = ImagePtr();
	} break;
	}
	if (newPhotoId != photoId || newPhoto.v() != _userpic.v() || newPhotoLoc != photoLoc) {
		photoId = newPhotoId;
		setUserpic(newPhoto);
		photoLoc = newPhotoLoc;
		if (App::main()) {
			emit App::main()->peerPhotoChanged(this);
		}
		Notify::peerUpdatedDelayed(this, UpdateFlag::PhotoChanged);
	}
}

void ChannelData::setName(const QString &newName, const QString &newUsername) {
	updateNameDelayed(newName.isEmpty() ? name : newName, QString(), newUsername);
}

void ChannelData::updateFull(bool force) {
	if (!_lastFullUpdate || force || getms(true) > _lastFullUpdate + UpdateFullChannelTimeout) {
		if (App::api()) {
			App::api()->requestFullPeer(this);
			if (!amCreator() && !inviter) App::api()->requestSelfParticipant(this);
		}
	}
}

void ChannelData::fullUpdated() {
	_lastFullUpdate = getms(true);
}

bool ChannelData::setAbout(const QString &newAbout) {
	if (_about == newAbout) {
		return false;
	}
	_about = newAbout;
	Notify::peerUpdatedDelayed(this, UpdateFlag::AboutChanged);
	return true;
}

void ChannelData::setInviteLink(const QString &newInviteLink) {
	if (newInviteLink != _inviteLink) {
		_inviteLink = newInviteLink;
		Notify::peerUpdatedDelayed(this, UpdateFlag::InviteLinkChanged);
	}
}

void ChannelData::setMembersCount(int newMembersCount) {
	if (_membersCount != newMembersCount) {
		if (isMegagroup() && !mgInfo->lastParticipants.isEmpty()) {
			mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
			mgInfo->lastParticipantsCount = membersCount();
		}
		_membersCount = newMembersCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::MembersChanged);
	}
}

void ChannelData::setAdminsCount(int newAdminsCount) {
	if (_adminsCount != newAdminsCount) {
		_adminsCount = newAdminsCount;
		Notify::peerUpdatedDelayed(this, Notify::PeerUpdate::Flag::AdminsChanged);
	}
}

void ChannelData::flagsUpdated() {
	if (isMegagroup()) {
		if (!mgInfo) {
			mgInfo = new MegagroupInfo();
		}
	} else if (mgInfo) {
		delete mgInfo;
		mgInfo = nullptr;
	}
}

void ChannelData::selfAdminUpdated() {
	if (isMegagroup()) {
		if (amEditor()) {
			mgInfo->lastAdmins.insert(App::self());
		} else {
			mgInfo->lastAdmins.remove(App::self());
		}
	}
}

ChannelData::~ChannelData() {
	delete mgInfo;
}

uint64 PtsWaiter::ptsKey(PtsSkippedQueue queue) {
	return _queue.insert(uint64(uint32(_last)) << 32 | uint64(uint32(_count)), queue).key();
}

void PtsWaiter::setWaitingForSkipped(ChannelData *channel, int32 ms) {
	if (ms >= 0) {
		if (App::main()) {
			App::main()->ptsWaiterStartTimerFor(channel, ms);
		}
		_waitingForSkipped = true;
	} else {
		_waitingForSkipped = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::setWaitingForShortPoll(ChannelData *channel, int32 ms) {
	if (ms >= 0) {
		if (App::main()) {
			App::main()->ptsWaiterStartTimerFor(channel, ms);
		}
		_waitingForShortPoll = true;
	} else {
		_waitingForShortPoll = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::checkForWaiting(ChannelData *channel) {
	if (!_waitingForSkipped && !_waitingForShortPoll && App::main()) {
		App::main()->ptsWaiterStartTimerFor(channel, -1);
	}
}

void PtsWaiter::applySkippedUpdates(ChannelData *channel) {
	if (!_waitingForSkipped) return;

	setWaitingForSkipped(channel, -1);

	if (!App::main() || _queue.isEmpty()) return;

	++_applySkippedLevel;
	for (QMap<uint64, PtsSkippedQueue>::const_iterator i = _queue.cbegin(), e = _queue.cend(); i != e; ++i) {
		switch (i.value()) {
		case SkippedUpdate: App::main()->feedUpdate(_updateQueue.value(i.key())); break;
		case SkippedUpdates: App::main()->feedUpdates(_updatesQueue.value(i.key())); break;
		}
	}
	--_applySkippedLevel;
	clearSkippedUpdates();
}

void PtsWaiter::clearSkippedUpdates() {
	_queue.clear();
	_updateQueue.clear();
	_updatesQueue.clear();
	_applySkippedLevel = 0;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	}
	return check(channel, pts, count);
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdates &updates) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updatesQueue.insert(ptsKey(SkippedUpdates), updates);
	return false;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdate &update) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updateQueue.insert(ptsKey(SkippedUpdate), update);
	return false;
}

bool PtsWaiter::check(ChannelData *channel, int32 pts, int32 count) { // return false if need to save that update and apply later
	if (!inited()) {
		init(pts);
		return true;
	}

	_last = qMax(_last, pts);
	_count += count;
	if (_last == _count) {
		_good = _last;
		return true;
	} else if (_last < _count) {
		setWaitingForSkipped(channel, 1);
	} else {
		setWaitingForSkipped(channel, WaitForSkippedTimeout);
	}
	return !count;
}

PhotoData::PhotoData(const PhotoId &id, const uint64 &access, int32 date, const ImagePtr &thumb, const ImagePtr &medium, const ImagePtr &full)
: id(id)
, access(access)
, date(date)
, thumb(thumb)
, medium(medium)
, full(full)
, peer(0)
, uploadingData(0) {
}

void PhotoData::automaticLoad(const HistoryItem *item) {
	full->automaticLoad(item);
}

void PhotoData::automaticLoadSettingsChanged() {
	full->automaticLoadSettingsChanged();
}

void PhotoData::download() {
	full->loadEvenCancelled();
	notifyLayoutChanged();
}

bool PhotoData::loaded() const {
	bool wasLoading = loading();
	if (full->loaded()) {
		if (wasLoading) {
			notifyLayoutChanged();
		}
		return true;
	}
	return false;
}

bool PhotoData::loading() const {
	return full->loading();
}

bool PhotoData::displayLoading() const {
	return full->loading() ? full->displayLoading() : uploading();
}

void PhotoData::cancel() {
	full->cancel();
	notifyLayoutChanged();
}

void PhotoData::notifyLayoutChanged() const {
	auto &items = App::photoItems();
	auto i = items.constFind(const_cast<PhotoData*>(this));
	if (i != items.cend()) {
		for_const (auto item, i.value()) {
			Notify::historyItemLayoutChanged(item);
		}
	}
}

float64 PhotoData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			return float64(uploadingData->offset) / uploadingData->size;
		}
		return 0;
	}
	return full->progress();
}

int32 PhotoData::loadOffset() const {
	return full->loadOffset();
}

bool PhotoData::uploading() const {
	return uploadingData;
}

void PhotoData::forget() {
	thumb->forget();
	replyPreview->forget();
	medium->forget();
	full->forget();
}

ImagePtr PhotoData::makeReplyPreview() {
	if (replyPreview->isNull() && !thumb->isNull()) {
		if (thumb->loaded()) {
			int w = thumb->width(), h = thumb->height();
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			replyPreview = ImagePtr(w > h ? thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			thumb->load();
		}
	}
	return replyPreview;
}

PhotoData::~PhotoData() {
	delete base::take(uploadingData);
}

void PhotoOpenClickHandler::onClickImpl() const {
	App::wnd()->showPhoto(this, App::hoveredLinkItem() ? App::hoveredLinkItem() : App::contextItem());
}

void PhotoSaveClickHandler::onClickImpl() const {
	auto data = photo();
	if (!data->date) return;

	data->download();
}

void PhotoCancelClickHandler::onClickImpl() const {
	auto data = photo();
	if (!data->date) return;

	if (data->uploading()) {
		if (auto item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : nullptr)) {
			if (auto media = item->getMedia()) {
				if (media->type() == MediaTypePhoto && static_cast<HistoryPhoto*>(media)->photo() == data) {
					App::contextItem(item);
					App::main()->deleteLayer(-2);
				}
			}
		}
	} else {
		data->cancel();
	}
}

QString joinList(const QStringList &list, const QString &sep) {
	QString result;
	if (list.isEmpty()) return result;

	int32 l = list.size(), s = sep.size() * (l - 1);
	for (int32 i = 0; i < l; ++i) {
		s += list.at(i).size();
	}
	result.reserve(s);
	result.append(list.at(0));
	for (int32 i = 1; i < l; ++i) {
		result.append(sep).append(list.at(i));
	}
	return result;
}

QString saveFileName(const QString &title, const QString &filter, const QString &prefix, QString name, bool savingAs, const QDir &dir) {
#ifdef Q_OS_WIN
	name = name.replace(QRegularExpression(qsl("[\\\\\\/\\:\\*\\?\\\"\\<\\>\\|]")), qsl("_"));
#elif defined Q_OS_MAC
	name = name.replace(QRegularExpression(qsl("[\\:]")), qsl("_"));
#elif defined Q_OS_LINUX
	name = name.replace(QRegularExpression(qsl("[\\/]")), qsl("_"));
#endif
	if (Global::AskDownloadPath() || savingAs) {
		if (!name.isEmpty() && name.at(0) == QChar::fromLatin1('.')) {
			name = filedialogDefaultName(prefix, name);
		} else if (dir.path() != qsl(".")) {
			QString path = dir.absolutePath();
			if (path != cDialogLastPath()) {
				cSetDialogLastPath(path);
				Local::writeUserSettings();
			}
		}

		// check if extension of filename is present in filter
		// it should be in first filter section on the first place
		// place it there, if it is not
		QString ext = QFileInfo(name).suffix(), fil = filter, sep = qsl(";;");
		if (!ext.isEmpty()) {
			if (QRegularExpression(qsl("^[a-zA-Z_0-9]+$")).match(ext).hasMatch()) {
				QStringList filters = filter.split(sep);
				if (filters.size() > 1) {
					QString first = filters.at(0);
					int32 start = first.indexOf(qsl("(*."));
					if (start >= 0) {
						if (!QRegularExpression(qsl("\\(\\*\\.") + ext + qsl("[\\)\\s]"), QRegularExpression::CaseInsensitiveOption).match(first).hasMatch()) {
							QRegularExpressionMatch m = QRegularExpression(qsl(" \\*\\.") + ext + qsl("[\\)\\s]"), QRegularExpression::CaseInsensitiveOption).match(first);
							if (m.hasMatch() && m.capturedStart() > start + 3) {
								int32 oldpos = m.capturedStart(), oldend = m.capturedEnd();
								fil = first.mid(0, start + 3) + ext + qsl(" *.") + first.mid(start + 3, oldpos - start - 3) + first.mid(oldend - 1) + sep + joinList(filters.mid(1), sep);
							} else {
								fil = first.mid(0, start + 3) + ext + qsl(" *.") + first.mid(start + 3) + sep + joinList(filters.mid(1), sep);
							}
						}
					} else {
						fil = QString();
					}
				} else {
					fil = QString();
				}
			} else {
				fil = QString();
			}
		}
		return filedialogGetSaveFile(name, title, fil, name) ? name : QString();
	}

	QString path;
	if (Global::DownloadPath().isEmpty()) {
		path = psDownloadPath();
	} else if (Global::DownloadPath() == qsl("tmp")) {
		path = cTempDir();
	} else {
		path = Global::DownloadPath();
	}
	if (name.isEmpty()) name = qsl(".unknown");
	if (name.at(0) == QChar::fromLatin1('.')) {
		if (!QDir().exists(path)) QDir().mkpath(path);
		return filedialogDefaultName(prefix, name, path);
	}
	if (dir.path() != qsl(".")) {
		path = dir.absolutePath() + '/';
	}

	QString nameStart, extension;
	int32 extPos = name.lastIndexOf('.');
	if (extPos >= 0) {
		nameStart = name.mid(0, extPos);
		extension = name.mid(extPos);
	} else {
		nameStart = name;
	}
	QString nameBase = path + nameStart;
	name = nameBase + extension;
	for (int i = 0; QFileInfo(name).exists(); ++i) {
		name = nameBase + QString(" (%1)").arg(i + 2) + extension;
	}

	if (!QDir().exists(path)) QDir().mkpath(path);
	return name;
}

bool StickerData::setInstalled() const {
	switch (set.type()) {
	case mtpc_inputStickerSetID: {
		auto it = Global::StickerSets().constFind(set.c_inputStickerSetID().vid.v);
		return (it != Global::StickerSets().cend()) && !(it->flags & MTPDstickerSet::Flag::f_archived) && (it->flags & MTPDstickerSet::Flag::f_installed);
	} break;
	case mtpc_inputStickerSetShortName: {
		QString name = qs(set.c_inputStickerSetShortName().vshort_name).toLower();
		for (auto it = Global::StickerSets().cbegin(), e = Global::StickerSets().cend(); it != e; ++it) {
			if (it->shortName.toLower() == name) {
				return !(it->flags & MTPDstickerSet::Flag::f_archived) && (it->flags & MTPDstickerSet::Flag::f_installed);
			}
		}
	} break;
	}
	return false;
}

QString documentSaveFilename(const DocumentData *data, bool forceSavingAs = false, const QString already = QString(), const QDir &dir = QDir()) {
	auto alreadySavingFilename = data->loadingFilePath();
	if (!alreadySavingFilename.isEmpty()) {
		return alreadySavingFilename;
	}

	QString name, filter, caption, prefix;
	MimeType mimeType = mimeTypeForName(data->mime);
	QStringList p = mimeType.globPatterns();
	QString pattern = p.isEmpty() ? QString() : p.front();
	if (data->voice()) {
		bool mp3 = (data->mime == qstr("audio/mp3"));
		name = already.isEmpty() ? (mp3 ? qsl(".mp3") : qsl(".ogg")) : already;
		filter = mp3 ? qsl("MP3 Audio (*.mp3);;") : qsl("OGG Opus Audio (*.ogg);;");
		filter += filedialogAllFilesFilter();
		caption = lang(lng_save_audio);
		prefix = qsl("audio");
	} else if (data->isVideo()) {
		name = already.isEmpty() ? qsl(".mov") : already;
		filter = qsl("MOV Video (*.mov);;") + filedialogAllFilesFilter();
		caption = lang(lng_save_video);
		prefix = qsl("video");
	} else {
		name = already.isEmpty() ? data->name : already;
		if (name.isEmpty()) {
			name = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
		}
		if (pattern.isEmpty()) {
			filter = QString();
		} else {
			filter = mimeType.filterString() + qsl(";;") + filedialogAllFilesFilter();
		}
		caption = lang(data->song() ? lng_save_audio_file : lng_save_file);
		prefix = qsl("doc");
	}

	return saveFileName(caption, filter, prefix, name, forceSavingAs, dir);
}

void DocumentOpenClickHandler::doOpen(DocumentData *data, HistoryItem *context, ActionOnLoad action) {
	if (!data->date) return;

	auto msgId = context ? context->fullId() : FullMsgId();
	bool playVoice = data->voice() && audioPlayer();
	bool playMusic = data->song() && audioPlayer();
	bool playVideo = data->isVideo() && audioPlayer();
	bool playAnimation = data->isAnimation();
	auto &location = data->location(true);
	if (!location.isEmpty() || (!data->data().isEmpty() && (playVoice || playMusic || playVideo || playAnimation))) {
		if (playVoice) {
			AudioMsgId playing;
			auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Voice);
			if (playing == AudioMsgId(data, msgId) && !(playbackState.state & AudioPlayerStoppedMask) && playbackState.state != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(AudioMsgId::Type::Voice);
			} else {
				AudioMsgId audio(data, msgId);
				audioPlayer()->play(audio);
				audioPlayer()->notify(audio);
				if (App::main()) {
					App::main()->mediaMarkRead(data);
				}
			}
		} else if (playMusic) {
			AudioMsgId playing;
			auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Song);
			if (playing == AudioMsgId(data, msgId) && !(playbackState.state & AudioPlayerStoppedMask) && playbackState.state != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(AudioMsgId::Type::Song);
			} else {
				AudioMsgId song(data, msgId);
				audioPlayer()->play(song);
				audioPlayer()->notify(song);
			}
		} else if (playVideo) {
			if (!data->data().isEmpty()) {
				App::wnd()->showDocument(data, context);
			} else if (location.accessEnable()) {
				App::wnd()->showDocument(data, context);
				location.accessDisable();
			} else {
				auto filepath = location.name();
				if (documentIsValidMediaFile(filepath)) {
					psOpenFile(filepath);
				} else {
					psShowInFolder(filepath);
				}
			}
			if (App::main()) App::main()->mediaMarkRead(data);
		} else if (data->voice() || data->song() || data->isVideo()) {
			auto filepath = location.name();
			if (documentIsValidMediaFile(filepath)) {
				psOpenFile(filepath);
			}
			if (App::main()) App::main()->mediaMarkRead(data);
		} else if (data->size < MediaViewImageSizeLimit) {
			if (!data->data().isEmpty() && playAnimation) {
				if (action == ActionOnLoadPlayInline && context && context->getMedia()) {
					context->getMedia()->playInline(context);
				} else {
					App::wnd()->showDocument(data, context);
				}
			} else if (location.accessEnable()) {
				if (data->isAnimation() || QImageReader(location.name()).canRead()) {
					if (action == ActionOnLoadPlayInline && context && context->getMedia()) {
						context->getMedia()->playInline(context);
					} else {
						App::wnd()->showDocument(data, context);
					}
				} else {
					psOpenFile(location.name());
				}
				location.accessDisable();
			} else {
				psOpenFile(location.name());
			}
		} else {
			psOpenFile(location.name());
		}
		return;
	}

	if (data->status != FileReady) return;

	QString filename;
	if (!data->saveToCache()) {
		filename = documentSaveFilename(data);
		if (filename.isEmpty()) return;
	}

	data->save(filename, action, msgId);
}

void DocumentOpenClickHandler::onClickImpl() const {
	auto item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : nullptr);
	doOpen(document(), item, document()->voice() ? ActionOnLoadNone : ActionOnLoadOpen);
}

void GifOpenClickHandler::onClickImpl() const {
	auto item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : nullptr);
	doOpen(document(), item, ActionOnLoadPlayInline);
}

void DocumentSaveClickHandler::doSave(DocumentData *data, bool forceSavingAs) {
	if (!data->date) return;

	QString filepath = data->filepath(DocumentData::FilePathResolveSaveFromDataSilent, forceSavingAs);
	if (!filepath.isEmpty() && !forceSavingAs) {
		QPoint pos(QCursor::pos());
		if (!psShowOpenWithMenu(pos.x(), pos.y(), filepath)) {
			psOpenFile(filepath, true);
		}
	} else {
		QFileInfo fileinfo(filepath);
		QDir filedir(filepath.isEmpty() ? QDir() : fileinfo.dir());
		QString filename(filepath.isEmpty() ? QString() : fileinfo.fileName());
		QString newfname = documentSaveFilename(data, forceSavingAs, filename, filedir);
		if (!newfname.isEmpty()) {
			ActionOnLoad action = filename.isEmpty() ? ActionOnLoadNone : ActionOnLoadOpenWith;
			FullMsgId actionMsgId = App::hoveredLinkItem() ? App::hoveredLinkItem()->fullId() : (App::contextItem() ? App::contextItem()->fullId() : FullMsgId());
			data->save(newfname, action, actionMsgId);
		}
	}
}

void DocumentSaveClickHandler::onClickImpl() const {
	doSave(document());
}

void DocumentCancelClickHandler::onClickImpl() const {
	auto data = document();
	if (!data->date) return;

	if (data->uploading()) {
		if (auto item = App::hoveredLinkItem() ? App::hoveredLinkItem() : (App::contextItem() ? App::contextItem() : nullptr)) {
			if (auto media = item->getMedia()) {
				if (media->getDocument() == data) {
					App::contextItem(item);
					App::main()->deleteLayer(-2);
				}
			}
		}
	} else {
		data->cancel();
	}
}

VoiceData::~VoiceData() {
	if (!waveform.isEmpty() && waveform.at(0) == -1 && waveform.size() > int32(sizeof(TaskId))) {
		TaskId taskId = 0;
		memcpy(&taskId, waveform.constData() + 1, sizeof(taskId));
		Local::cancelTask(taskId);
	}
}

DocumentAdditionalData::~DocumentAdditionalData() {
}

DocumentData::DocumentData(DocumentId id, int32 dc, uint64 accessHash, int32 version, const QString &url, const QVector<MTPDocumentAttribute> &attributes)
: id(id)
, _dc(dc)
, _access(accessHash)
, _version(version)
, _url(url) {
	setattributes(attributes);
	if (_dc && _access) {
		_location = Local::readFileLocation(mediaKey());
	}
}

DocumentData *DocumentData::create(DocumentId id) {
	return new DocumentData(id, 0, 0, 0, QString(), QVector<MTPDocumentAttribute>());
}

DocumentData *DocumentData::create(DocumentId id, int32 dc, uint64 accessHash, int32 version, const QVector<MTPDocumentAttribute> &attributes) {
	return new DocumentData(id, dc, accessHash, version, QString(), attributes);
}

DocumentData *DocumentData::create(DocumentId id, const QString &url, const QVector<MTPDocumentAttribute> &attributes) {
	return new DocumentData(id, 0, 0, 0, url, attributes);
}

void DocumentData::setattributes(const QVector<MTPDocumentAttribute> &attributes) {
	for (int32 i = 0, l = attributes.size(); i < l; ++i) {
		switch (attributes[i].type()) {
		case mtpc_documentAttributeImageSize: {
			auto &d = attributes[i].c_documentAttributeImageSize();
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAnimated: if (type == FileDocument || type == StickerDocument || type == VideoDocument) {
			type = AnimatedDocument;
			_additional = nullptr;
		} break;
		case mtpc_documentAttributeSticker: {
			auto &d = attributes[i].c_documentAttributeSticker();
			if (type == FileDocument) {
				type = StickerDocument;
				_additional = std_::make_unique<StickerData>();
			}
			if (sticker()) {
				sticker()->alt = qs(d.valt);
				if (sticker()->set.type() != mtpc_inputStickerSetID || d.vstickerset.type() == mtpc_inputStickerSetID) {
					sticker()->set = d.vstickerset;
				}
			}
		} break;
		case mtpc_documentAttributeVideo: {
			auto &d = attributes[i].c_documentAttributeVideo();
			if (type == FileDocument) {
				type = VideoDocument;
			}
			_duration = d.vduration.v;
			dimensions = QSize(d.vw.v, d.vh.v);
		} break;
		case mtpc_documentAttributeAudio: {
			auto &d = attributes[i].c_documentAttributeAudio();
			if (type == FileDocument) {
				if (d.is_voice()) {
					type = VoiceDocument;
					_additional = std_::make_unique<VoiceData>();
				} else {
					type = SongDocument;
					_additional = std_::make_unique<SongData>();
				}
			}
			if (voice()) {
				voice()->duration = d.vduration.v;
				VoiceWaveform waveform = documentWaveformDecode(qba(d.vwaveform));
				uchar wavemax = 0;
				for (int32 i = 0, l = waveform.size(); i < l; ++i) {
					uchar waveat = waveform.at(i);
					if (wavemax < waveat) wavemax = waveat;
				}
				voice()->waveform = waveform;
				voice()->wavemax = wavemax;
			} else if (song()) {
				song()->duration = d.vduration.v;
				song()->title = qs(d.vtitle);
				song()->performer = qs(d.vperformer);
			}
		} break;
		case mtpc_documentAttributeFilename: name = qs(attributes[i].c_documentAttributeFilename().vfile_name); break;
		}
	}
	if (type == StickerDocument) {
		if (dimensions.width() <= 0 || dimensions.height() <= 0 || dimensions.width() > StickerMaxSize || dimensions.height() > StickerMaxSize || size > StickerInMemory) {
			type = FileDocument;
			_additional = nullptr;
		}
	}
}

bool DocumentData::saveToCache() const {
	return (type == StickerDocument) || (isAnimation() && size < AnimationInMemory) || (voice() && size < AudioVoiceMsgInMemory);
}

void DocumentData::forget() {
	thumb->forget();
	if (sticker()) sticker()->img->forget();
	replyPreview->forget();
	_data.clear();
}

void DocumentData::automaticLoad(const HistoryItem *item) {
	if (loaded() || status != FileReady) return;

	if (saveToCache() && _loader != CancelledMtpFileLoader) {
		if (type == StickerDocument) {
			save(QString(), _actionOnLoad, _actionOnLoadMsgId);
		} else if (isAnimation()) {
			bool loadFromCloud = false;
			if (item) {
				if (item->history()->peer->isUser()) {
					loadFromCloud = !(cAutoDownloadGif() & dbiadNoPrivate);
				} else {
					loadFromCloud = !(cAutoDownloadGif() & dbiadNoGroups);
				}
			} else { // if load at least anywhere
				loadFromCloud = !(cAutoDownloadGif() & dbiadNoPrivate) || !(cAutoDownloadGif() & dbiadNoGroups);
			}
			save(QString(), _actionOnLoad, _actionOnLoadMsgId, loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
		} else if (voice()) {
			if (item) {
				bool loadFromCloud = false;
				if (item->history()->peer->isUser()) {
					loadFromCloud = !(cAutoDownloadAudio() & dbiadNoPrivate);
				} else {
					loadFromCloud = !(cAutoDownloadAudio() & dbiadNoGroups);
				}
				save(QString(), _actionOnLoad, _actionOnLoadMsgId, loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
			}
		}
	}
}

void DocumentData::automaticLoadSettingsChanged() {
	if (loaded() || status != FileReady || (!isAnimation() && !voice()) || !saveToCache() || _loader != CancelledMtpFileLoader) return;
	_loader = 0;
}

void DocumentData::performActionOnLoad() {
	if (_actionOnLoad == ActionOnLoadNone) return;

	const FileLocation &loc(location(true));
	QString already = loc.name();
	HistoryItem *item = _actionOnLoadMsgId.msg ? App::histItemById(_actionOnLoadMsgId) : nullptr;
	bool showImage = !isVideo() && (size < MediaViewImageSizeLimit);
	bool playVoice = voice() && audioPlayer() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen);
	bool playMusic = song() && audioPlayer() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen);
	bool playAnimation = isAnimation() && (_actionOnLoad == ActionOnLoadPlayInline || _actionOnLoad == ActionOnLoadOpen) && showImage && item && item->getMedia();
	if (playVoice) {
		if (loaded()) {
			AudioMsgId playing;
			auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Voice);
			if (playing == AudioMsgId(this, _actionOnLoadMsgId) && !(playbackState.state & AudioPlayerStoppedMask) && playbackState.state != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(AudioMsgId::Type::Voice);
			} else if (playbackState.state & AudioPlayerStoppedMask) {
				audioPlayer()->play(AudioMsgId(this, _actionOnLoadMsgId));
				if (App::main()) App::main()->mediaMarkRead(this);
			}
		}
	} else if (playMusic) {
		if (loaded()) {
			AudioMsgId playing;
			auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Song);
			if (playing == AudioMsgId(this, _actionOnLoadMsgId) && !(playbackState.state & AudioPlayerStoppedMask) && playbackState.state != AudioPlayerFinishing) {
				audioPlayer()->pauseresume(AudioMsgId::Type::Song);
			} else if (playbackState.state & AudioPlayerStoppedMask) {
				AudioMsgId song(this, _actionOnLoadMsgId);
				audioPlayer()->play(song);
				audioPlayer()->notify(song);
			}
		}
	} else if (playAnimation) {
		if (loaded()) {
			if (_actionOnLoad == ActionOnLoadPlayInline && item->getMedia()) {
				item->getMedia()->playInline(item);
			} else {
				App::wnd()->showDocument(this, item);
			}
		}
	} else {
		if (already.isEmpty()) return;

		if (_actionOnLoad == ActionOnLoadOpenWith) {
			QPoint pos(QCursor::pos());
			if (!psShowOpenWithMenu(pos.x(), pos.y(), already)) {
				psOpenFile(already, true);
			}
		} else if (_actionOnLoad == ActionOnLoadOpen || _actionOnLoad == ActionOnLoadPlayInline) {
			if (voice() || song() || isVideo()) {
				if (documentIsValidMediaFile(already)) {
					psOpenFile(already);
				}
				if (App::main()) App::main()->mediaMarkRead(this);
			} else if (loc.accessEnable()) {
				if (showImage && QImageReader(loc.name()).canRead()) {
					if (_actionOnLoad == ActionOnLoadPlayInline && item && item->getMedia()) {
						item->getMedia()->playInline(item);
					} else {
						App::wnd()->showDocument(this, item);
					}
				} else {
					psOpenFile(already);
				}
				loc.accessDisable();
			} else {
				psOpenFile(already);
			}
		}
	}
	_actionOnLoad = ActionOnLoadNone;
}

bool DocumentData::loaded(FilePathResolveType type) const {
	if (loading() && _loader->done()) {
		if (_loader->fileType() == mtpc_storage_fileUnknown) {
			_loader->deleteLater();
			_loader->stop();
			_loader = CancelledMtpFileLoader;
		} else {
			DocumentData *that = const_cast<DocumentData*>(this);
			that->_location = FileLocation(mtpToStorageType(_loader->fileType()), _loader->fileName());
			that->_data = _loader->bytes();
			if (that->sticker() && !_loader->imagePixmap().isNull()) {
				that->sticker()->img = ImagePtr(_data, _loader->imageFormat(), _loader->imagePixmap());
			}

			_loader->deleteLater();
			_loader->stop();
			_loader = nullptr;
		}
		notifyLayoutChanged();
	}
	return !data().isEmpty() || !filepath(type).isEmpty();
}

bool DocumentData::loading() const {
	return _loader && _loader != CancelledMtpFileLoader;
}

QString DocumentData::loadingFilePath() const {
	return loading() ? _loader->fileName() : QString();
}

bool DocumentData::displayLoading() const {
	return loading() ? (!_loader->loadingLocal() || !_loader->autoLoading()) : uploading();
}

float64 DocumentData::progress() const {
	if (uploading()) {
		return snap((size > 0) ? float64(uploadOffset) / size : 0., 0., 1.);
	}
	return loading() ? _loader->currentProgress() : (loaded() ? 1. : 0.);
}

int32 DocumentData::loadOffset() const {
	return loading() ? _loader->currentOffset() : 0;
}

bool DocumentData::uploading() const {
	return status == FileUploading;
}

void DocumentData::save(const QString &toFile, ActionOnLoad action, const FullMsgId &actionMsgId, LoadFromCloudSetting fromCloud, bool autoLoading) {
	if (loaded(FilePathResolveChecked)) {
		auto &l = location(true);
		if (!toFile.isEmpty()) {
			if (!_data.isEmpty()) {
				QFile f(toFile);
				f.open(QIODevice::WriteOnly);
				f.write(_data);
				f.close();

				setLocation(FileLocation(StorageFilePartial, toFile));
				Local::writeFileLocation(mediaKey(), FileLocation(mtpToStorageType(mtpc_storage_filePartial), toFile));
			} else if (l.accessEnable()) {
				auto alreadyName = l.name();
				if (alreadyName != toFile) {
					QFile(alreadyName).copy(toFile);
				}
				l.accessDisable();
			}
		}
		_actionOnLoad = action;
		_actionOnLoadMsgId = actionMsgId;
		performActionOnLoad();
		return;
	}

	if (_loader == CancelledMtpFileLoader) _loader = nullptr;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancel(); // changes _actionOnLoad
			_loader = nullptr;
		}
	}

	_actionOnLoad = action;
	_actionOnLoadMsgId = actionMsgId;
	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		status = FileReady;
		if (!_access && !_url.isEmpty()) {
			_loader = new webFileLoader(_url, toFile, fromCloud, autoLoading);
		} else {
			_loader = new mtpFileLoader(_dc, id, _access, _version, locationType(), toFile, size, (saveToCache() ? LoadToCacheAsWell : LoadToFileOnly), fromCloud, autoLoading);
		}
		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(documentLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*,bool)), App::main(), SLOT(documentLoadFailed(FileLoader*,bool)));
		_loader->start();
	}
	notifyLayoutChanged();
}

void DocumentData::cancel() {
	if (!loading()) return;

	auto loader = base::take(_loader);
	_loader = CancelledMtpFileLoader;
	loader->cancel();
	loader->deleteLater();
	loader->stop();

	notifyLayoutChanged();
	if (auto main = App::main()) {
		main->documentLoadProgress(this);
	}

	_actionOnLoad = ActionOnLoadNone;
}

void DocumentData::notifyLayoutChanged() const {
	auto &items = App::documentItems();
	for (auto item : items.value(const_cast<DocumentData*>(this))) {
		Notify::historyItemLayoutChanged(item);
	}

	if (auto items = InlineBots::Layout::documentItems()) {
		for (auto item : items->value(const_cast<DocumentData*>(this))) {
			Notify::inlineItemLayoutChanged(item);
		}
	}
}

VoiceWaveform documentWaveformDecode(const QByteArray &encoded5bit) {
	VoiceWaveform result((encoded5bit.size() * 8) / 5, 0);
	for (int32 i = 0, l = result.size(); i < l; ++i) { // read each 5 bit of encoded5bit as 0-31 unsigned char
		int32 byte = (i * 5) / 8, shift = (i * 5) % 8;
		result[i] = (((*(uint16*)(encoded5bit.constData() + byte)) >> shift) & 0x1F);
	}
	return result;
}

QByteArray documentWaveformEncode5bit(const VoiceWaveform &waveform) {
	QByteArray result((waveform.size() * 5 + 7) / 8, 0);
	for (int32 i = 0, l = waveform.size(); i < l; ++i) { // write each 0-31 unsigned char as 5 bit to result
		int32 byte = (i * 5) / 8, shift = (i * 5) % 8;
		(*(uint16*)(result.data() + byte)) |= (uint16(waveform.at(i) & 0x1F) << shift);
	}
	return result;
}

QByteArray DocumentData::data() const {
	return _data;
}

const FileLocation &DocumentData::location(bool check) const {
	if (check && !_location.check()) {
		const_cast<DocumentData*>(this)->_location = Local::readFileLocation(mediaKey());
	}
	return _location;
}

void DocumentData::setLocation(const FileLocation &loc) {
	if (loc.check()) {
		_location = loc;
	}
}

QString DocumentData::filepath(FilePathResolveType type, bool forceSavingAs) const {
	bool check = (type != FilePathResolveCached);
	QString result = (check && _location.name().isEmpty()) ? QString() : location(check).name();
	bool saveFromData = result.isEmpty() && !data().isEmpty();
	if (saveFromData) {
		if (type != FilePathResolveSaveFromData && type != FilePathResolveSaveFromDataSilent) {
			saveFromData = false;
		} else if (type == FilePathResolveSaveFromDataSilent && (Global::AskDownloadPath() || forceSavingAs)) {
			saveFromData = false;
		}
	}
	if (saveFromData) {
		QString filename = documentSaveFilename(this, forceSavingAs);
		if (!filename.isEmpty()) {
			QFile f(filename);
			if (f.open(QIODevice::WriteOnly)) {
				if (f.write(data()) == data().size()) {
					f.close();
					const_cast<DocumentData*>(this)->_location = FileLocation(StorageFilePartial, filename);
					Local::writeFileLocation(mediaKey(), _location);
					result = filename;
				}
			}
		}
	}
	return result;
}

ImagePtr DocumentData::makeReplyPreview() {
	if (replyPreview->isNull() && !thumb->isNull()) {
		if (thumb->loaded()) {
			int w = thumb->width(), h = thumb->height();
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			replyPreview = ImagePtr(w > h ? thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			thumb->load();
		}
	}
	return replyPreview;
}

bool fileIsImage(const QString &name, const QString &mime) {
	QString lowermime = mime.toLower(), namelower = name.toLower();
	if (lowermime.startsWith(qstr("image/"))) {
		return true;
	} else if (namelower.endsWith(qstr(".bmp"))
		|| namelower.endsWith(qstr(".jpg"))
		|| namelower.endsWith(qstr(".jpeg"))
		|| namelower.endsWith(qstr(".gif"))
		|| namelower.endsWith(qstr(".webp"))
		|| namelower.endsWith(qstr(".tga"))
		|| namelower.endsWith(qstr(".tiff"))
		|| namelower.endsWith(qstr(".tif"))
		|| namelower.endsWith(qstr(".psd"))
		|| namelower.endsWith(qstr(".png"))) {
		return true;
	}
	return false;
}

void DocumentData::recountIsImage() {
	if (isAnimation() || isVideo()) return;
	_duration = fileIsImage(name, mime) ? 1 : -1; // hack
}

bool DocumentData::setRemoteVersion(int32 version) {
	if (_version == version) {
		return false;
	}
	_version = version;
	_location = FileLocation();
	_data = QByteArray();
	status = FileReady;
	if (loading()) {
		_loader->deleteLater();
		_loader->stop();
		_loader = nullptr;
	}
	return true;
}

void DocumentData::setRemoteLocation(int32 dc, uint64 access) {
	_dc = dc;
	_access = access;
	if (isValid()) {
		if (_location.check()) {
			Local::writeFileLocation(mediaKey(), _location);
		} else {
			_location = Local::readFileLocation(mediaKey());
		}
	}
}

void DocumentData::setContentUrl(const QString &url) {
	_url = url;
}

void DocumentData::collectLocalData(DocumentData *local) {
	if (local == this) return;

	if (!local->_data.isEmpty()) {
		_data = local->_data;
		if (voice()) {
			if (!Local::copyAudio(local->mediaKey(), mediaKey())) {
				Local::writeAudio(mediaKey(), _data);
			}
		} else {
			if (!Local::copyStickerImage(local->mediaKey(), mediaKey())) {
				Local::writeStickerImage(mediaKey(), _data);
			}
		}
	}
	if (!local->_location.isEmpty()) {
		_location = local->_location;
		Local::writeFileLocation(mediaKey(), _location);
	}
}

DocumentData::~DocumentData() {
	if (loading()) {
		_loader->deleteLater();
		_loader->stop();
		_loader = nullptr;
	}
}

WebPageData::WebPageData(const WebPageId &id, WebPageType type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const QString &description, DocumentData *document, PhotoData *photo, int32 duration, const QString &author, int32 pendingTill) : id(id)
, type(type)
, url(url)
, displayUrl(displayUrl)
, siteName(siteName)
, title(title)
, description(description)
, duration(duration)
, author(author)
, photo(photo)
, document(document)
, pendingTill(pendingTill) {
}

GameData::GameData(const GameId &id, const uint64 &accessHash, const QString &shortName, const QString &title, const QString &description, PhotoData *photo, DocumentData *document) : id(id)
, accessHash(accessHash)
, shortName(shortName)
, title(title)
, description(description)
, photo(photo)
, document(document) {
}

void PeerOpenClickHandler::onClickImpl() const {
	if (App::main()) {
		if (peer() && peer()->isChannel() && App::main()->historyPeer() != peer()) {
			if (!peer()->asChannel()->isPublic() && !peer()->asChannel()->amIn()) {
				Ui::showLayer(new InformBox(lang((peer()->isMegagroup()) ? lng_group_not_accessible : lng_channel_not_accessible)));
			} else {
				Ui::showPeerHistory(peer(), ShowAtUnreadMsgId, Ui::ShowWay::Forward);
			}
		} else {
			Ui::showPeerProfile(peer());
		}
	}
}

MsgId clientMsgId() {
	static MsgId currentClientMsgId = StartClientMsgId;
	Q_ASSERT(currentClientMsgId < EndClientMsgId);
	return currentClientMsgId++;
}
