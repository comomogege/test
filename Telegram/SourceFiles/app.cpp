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
#include "app.h"

#ifdef OS_MAC_OLD
#include <libexif/exif-data.h>
#endif // OS_MAC_OLD

#include "styles/style_overview.h"
#include "styles/style_mediaview.h"
#include "lang.h"
#include "data/data_abstract_structure.h"
#include "history/history_service_layout.h"
#include "history/history_location_manager.h"
#include "history/history_media_types.h"
#include "media/media_audio.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "application.h"
#include "fileuploader.h"
#include "mainwidget.h"
#include "localstorage.h"
#include "apiwrap.h"
#include "numbers.h"
#include "observer_peer.h"
#include "window/chat_background.h"
#include "window/notifications_manager.h"
#include "platform/platform_notifications_manager.h"

namespace {
	App::LaunchState _launchState = App::Launched;

	UserData *self = nullptr;

	using PeersData = QHash<PeerId, PeerData*>;
	PeersData peersData;

	using MutedPeers = QMap<PeerData*, bool>;
	MutedPeers mutedPeers;

	using UpdatedPeers = QMap<PeerData*, bool>;
	UpdatedPeers updatedPeers;

	PhotosData photosData;
	DocumentsData documentsData;

	using LocationsData = QHash<LocationCoords, LocationData*>;
	LocationsData locationsData;

	using WebPagesData = QHash<WebPageId, WebPageData*>;
	WebPagesData webPagesData;

	using GamesData = QHash<GameId, GameData*>;
	GamesData gamesData;

	PhotoItems photoItems;
	DocumentItems documentItems;
	WebPageItems webPageItems;
	GameItems gameItems;
	SharedContactItems sharedContactItems;
	GifItems gifItems;

	using DependentItemsSet = OrderedSet<HistoryItem*>;
	using DependentItems = QMap<HistoryItem*, DependentItemsSet>;
	DependentItems dependentItems;

	Histories histories;

	using MsgsData = QHash<MsgId, HistoryItem*>;
	MsgsData msgsData;
	using ChannelMsgsData = QMap<ChannelId, MsgsData>;
	ChannelMsgsData channelMsgsData;

	using RandomData = QMap<uint64, FullMsgId>;
	RandomData randomData;

	using SentData = QMap<uint64, QPair<PeerId, QString>>;
	SentData sentData;

	HistoryItem *hoveredItem = nullptr,
		*pressedItem = nullptr,
		*hoveredLinkItem = nullptr,
		*pressedLinkItem = nullptr,
		*contextItem = nullptr,
		*mousedItem = nullptr;

	QPixmap *emoji = nullptr, *emojiLarge = nullptr;
	style::font monofont;

	struct CornersPixmaps {
		CornersPixmaps() {
			memset(p, 0, sizeof(p));
		}
		QPixmap *p[4];
	};
	CornersPixmaps corners[RoundCornersCount];
	using CornersMap = QMap<uint32, CornersPixmaps>;
	CornersMap cornersMap;
	QImage *cornersMaskLarge[4] = { nullptr }, *cornersMaskSmall[4] = { nullptr };

	using EmojiMap = QMap<uint64, QPixmap>;
	EmojiMap mainEmojiMap;
	QMap<int32, EmojiMap> otherEmojiMap;

	int32 serviceImageCacheSize = 0;

	using LastPhotosList = QLinkedList<PhotoData*>;
	LastPhotosList lastPhotos;
	using LastPhotosMap = QHash<PhotoData*, LastPhotosList::iterator>;
	LastPhotosMap lastPhotosMap;

	style::color _msgServiceBg;
	style::color _msgServiceSelectBg;
	style::color _historyScrollBarColor;
	style::color _historyScrollBgColor;
	style::color _historyScrollBarOverColor;
	style::color _historyScrollBgOverColor;
	style::color _introPointHoverColor;
}

namespace App {

	QString formatPhone(QString phone) {
		if (phone.isEmpty()) return QString();
		if (phone.at(0) == '0') return phone;

		QString number = phone;
		for (const QChar *ch = phone.constData(), *e = ch + phone.size(); ch != e; ++ch) {
			if (ch->unicode() < '0' || ch->unicode() > '9') {
				number = phone.replace(QRegularExpression(qsl("[^\\d]")), QString());
			}
		}
		QVector<int> groups = phoneNumberParse(number);
		if (groups.isEmpty()) return '+' + number;

		QString result;
		result.reserve(number.size() + groups.size() + 1);
		result.append('+');
		int32 sum = 0;
		for (int32 i = 0, l = groups.size(); i < l; ++i) {
			result.append(number.midRef(sum, groups.at(i)));
			sum += groups.at(i);
			if (sum < number.size()) result.append(' ');
		}
		if (sum < number.size()) result.append(number.midRef(sum));
		return result;
	}

	AppClass *app() {
		return AppClass::app();
	}

	MainWindow *wnd() {
		return AppClass::wnd();
	}

	MainWidget *main() {
		if (auto w = wnd()) {
			return w->mainWidget();
		}
		return nullptr;
	}

	bool passcoded() {
		if (auto w = wnd()) {
			return w->passcodeWidget();
		}
		return false;
	}

	FileUploader *uploader() {
		return app() ? app()->uploader() : 0;
	}

	ApiWrap *api() {
		return main() ? main()->api() : 0;
	}

namespace {
	bool loggedOut() {
		if (Global::LocalPasscode()) {
			Global::SetLocalPasscode(false);
			Global::RefLocalPasscodeChanged().notify();
		}
		if (audioPlayer()) {
			audioPlayer()->stopAndClear();
		}
		if (auto w = wnd()) {
			w->tempDirDelete(Local::ClearManagerAll);
			w->notifyClearFast();
			w->setupIntro(true);
		}
		MTP::setAuthedId(0);
		Local::reset();

		cSetOtherOnline(0);
		histories().clear();
		globalNotifyAllPtr = UnknownNotifySettings;
		globalNotifyUsersPtr = UnknownNotifySettings;
		globalNotifyChatsPtr = UnknownNotifySettings;
		if (App::uploader()) App::uploader()->clear();
		clearStorageImages();
		if (auto w = wnd()) {
			w->updateConnectingStatus();
			w->getTitle()->updateControlsVisibility();
		}
		return true;
	}
} // namespace

	void logOut() {
		if (MTP::started()) {
			MTP::logoutKeys(rpcDone(&loggedOut), rpcFail(&loggedOut));
		} else {
			loggedOut();
			MTP::start();
		}
	}

	TimeId onlineForSort(UserData *user, TimeId now) {
		if (isServiceUser(user->id) || user->botInfo) {
			return -1;
		}
		TimeId online = user->onlineTill;
		if (online <= 0) {
			switch (online) {
			case 0:
			case -1: return online;

			case -2: {
				QDate yesterday(date(now).date());
				return int32(QDateTime(yesterday.addDays(-3)).toTime_t()) + (unixtime() - myunixtime());
			} break;

			case -3: {
				QDate weekago(date(now).date());
				return int32(QDateTime(weekago.addDays(-7)).toTime_t()) + (unixtime() - myunixtime());
			} break;

			case -4: {
				QDate monthago(date(now).date());
				return int32(QDateTime(monthago.addDays(-30)).toTime_t()) + (unixtime() - myunixtime());
			} break;
			}
			return -online;
		}
		return online;
	}

	int32 onlineWillChangeIn(UserData *user, TimeId now) {
		if (isServiceUser(user->id) || user->botInfo) {
			return 86400;
		}
		return onlineWillChangeIn(user->onlineTill, now);
	}

	int32 onlineWillChangeIn(TimeId online, TimeId now) {
		if (online <= 0) {
            if (-online > now) return -online - now;
            return 86400;
        }
		if (online > now) {
			return online - now;
		}
		int32 minutes = (now - online) / 60;
		if (minutes < 60) {
			return (minutes + 1) * 60 - (now - online);
		}
		int32 hours = (now - online) / 3600;
		if (hours < 12) {
			return (hours + 1) * 3600 - (now - online);
		}
		QDateTime dNow(date(now)), dTomorrow(dNow.date().addDays(1));
		return dNow.secsTo(dTomorrow);
	}

	QString onlineText(UserData *user, TimeId now, bool precise) {
		if (isNotificationsUser(user->id)) {
			return lang(lng_status_service_notifications);
		} else if (user->botInfo) {
			return lang(lng_status_bot);
		} else if (isServiceUser(user->id)) {
			return lang(lng_status_support);
		}
		return onlineText(user->onlineTill, now, precise);
	}

	QString onlineText(TimeId online, TimeId now, bool precise) {
		if (online <= 0) {
			switch (online) {
			case 0: return lang(lng_status_offline);
            case -1: return lang(lng_status_invisible);
			case -2: return lang(lng_status_recently);
			case -3: return lang(lng_status_last_week);
			case -4: return lang(lng_status_last_month);
			}
            return (-online > now) ? lang(lng_status_online) : lang(lng_status_recently);
		}
		if (online > now) {
			return lang(lng_status_online);
		}
		QString when;
		if (precise) {
			QDateTime dOnline(date(online)), dNow(date(now));
			if (dOnline.date() == dNow.date()) {
				return lng_status_lastseen_today(lt_time, dOnline.time().toString(cTimeFormat()));
			} else if (dOnline.date().addDays(1) == dNow.date()) {
				return lng_status_lastseen_yesterday(lt_time, dOnline.time().toString(cTimeFormat()));
			}
			return lng_status_lastseen_date_time(lt_date, dOnline.date().toString(qsl("dd.MM.yy")), lt_time, dOnline.time().toString(cTimeFormat()));
		}
		int32 minutes = (now - online) / 60;
		if (!minutes) {
			return lang(lng_status_lastseen_now);
		} else if (minutes < 60) {
			return lng_status_lastseen_minutes(lt_count, minutes);
		}
		int32 hours = (now - online) / 3600;
		if (hours < 12) {
			return lng_status_lastseen_hours(lt_count, hours);
		}
		QDateTime dOnline(date(online)), dNow(date(now));
		if (dOnline.date() == dNow.date()) {
			return lng_status_lastseen_today(lt_time, dOnline.time().toString(cTimeFormat()));
		} else if (dOnline.date().addDays(1) == dNow.date()) {
			return lng_status_lastseen_yesterday(lt_time, dOnline.time().toString(cTimeFormat()));
		}
		return lng_status_lastseen_date(lt_date, dOnline.date().toString(qsl("dd.MM.yy")));
	}

	namespace {
		// we should get a full restriction in "{fulltype}: {reason}" format and we
		// need to find a "-all" tag in {fulltype}, otherwise ignore this restriction
		QString extractRestrictionReason(const QString &fullRestriction) {
			int fullTypeEnd = fullRestriction.indexOf(':');
			if (fullTypeEnd <= 0) {
				return QString();
			}

			// {fulltype} is in "{type}-{tag}-{tag}-{tag}" format
			// if we find "all" tag we return the restriction string
			auto typeTags = fullRestriction.mid(0, fullTypeEnd).split('-').mid(1);
#ifndef OS_MAC_STORE
			auto restrictionApplies = typeTags.contains(qsl("all"));
#else // OS_MAC_STORE
			auto restrictionApplies = typeTags.contains(qsl("all")) || typeTags.contains(qsl("ios"));
#endif // OS_MAC_STORE
			if (restrictionApplies) {
				return fullRestriction.midRef(fullTypeEnd + 1).trimmed().toString();
			}
			return QString();
		}
	}

	bool onlineColorUse(UserData *user, TimeId now) {
		if (isServiceUser(user->id) || user->botInfo) {
			return false;
		}
		return onlineColorUse(user->onlineTill, now);
	}

	bool onlineColorUse(TimeId online, TimeId now) {
		if (online <= 0) {
			switch (online) {
			case 0:
			case -1:
			case -2:
			case -3:
			case -4: return false;
			}
			return (-online > now);
		}
		return (online > now);
	}

	UserData *feedUser(const MTPUser &user) {
		UserData *data = nullptr;
		bool wasContact = false, minimal = false;
		const MTPUserStatus *status = 0, emptyStatus = MTP_userStatusEmpty();

		Notify::PeerUpdate update;
		using UpdateFlag = Notify::PeerUpdate::Flag;

		switch (user.type()) {
		case mtpc_userEmpty: {
			auto &d = user.c_userEmpty();

			auto peer = peerFromUser(d.vid.v);
			data = App::user(peer);
			auto canShareThisContact = data->canShareThisContactFast();
			wasContact = data->isContact();

			data->input = MTP_inputPeerUser(d.vid, MTP_long(0));
			data->inputUser = MTP_inputUser(d.vid, MTP_long(0));
			data->setName(lang(lng_deleted), QString(), QString(), QString());
			data->setPhoto(MTP_userProfilePhotoEmpty());
			data->access = UserNoAccess;
			data->flags = 0;
			data->setBotInfoVersion(-1);
			status = &emptyStatus;
			data->contact = -1;

			if (canShareThisContact != data->canShareThisContactFast()) update.flags |= UpdateFlag::UserCanShareContact;
			if (wasContact != data->isContact()) update.flags |= UpdateFlag::UserIsContact;
		} break;
		case mtpc_user: {
			auto &d = user.c_user();
			minimal = d.is_min();

			auto peer = peerFromUser(d.vid.v);
			data = App::user(peer);
			auto canShareThisContact = data->canShareThisContactFast();
			wasContact = data->isContact();
			if (!minimal) {
				data->flags = d.vflags.v;
				if (d.is_self()) {
					data->input = MTP_inputPeerSelf();
					data->inputUser = MTP_inputUserSelf();
				} else if (!d.has_access_hash()) {
					data->input = MTP_inputPeerUser(d.vid, MTP_long((data->access == UserNoAccess) ? 0 : data->access));
					data->inputUser = MTP_inputUser(d.vid, MTP_long((data->access == UserNoAccess) ? 0 : data->access));
				} else {
					data->input = MTP_inputPeerUser(d.vid, d.vaccess_hash);
					data->inputUser = MTP_inputUser(d.vid, d.vaccess_hash);
				}
				if (d.is_restricted()) {
					data->setRestrictionReason(extractRestrictionReason(qs(d.vrestriction_reason)));
				} else {
					data->setRestrictionReason(QString());
				}
			}
			if (d.is_deleted()) {
				if (!data->phone().isEmpty()) {
					data->setPhone(QString());
					update.flags |= UpdateFlag::UserPhoneChanged;
				}
				data->setName(lang(lng_deleted), QString(), QString(), QString());
				data->setPhoto(MTP_userProfilePhotoEmpty());
				data->access = UserNoAccess;
				status = &emptyStatus;
			} else {
				// apply first_name and last_name from minimal user only if we don't have
				// local values for first name and last name already, otherwise skip
				bool noLocalName = data->firstName.isEmpty() && data->lastName.isEmpty();
				QString fname = (!minimal || noLocalName) ? (d.has_first_name() ? textOneLine(qs(d.vfirst_name)) : QString()) : data->firstName;
				QString lname = (!minimal || noLocalName) ? (d.has_last_name() ? textOneLine(qs(d.vlast_name)) : QString()) : data->lastName;

				QString phone = minimal ? data->phone() : (d.has_phone() ? qs(d.vphone) : QString());
				QString uname = minimal ? data->username : (d.has_username() ? textOneLine(qs(d.vusername)) : QString());

				bool phoneChanged = (data->phone() != phone);
				if (phoneChanged) {
					data->setPhone(phone);
					update.flags |= UpdateFlag::UserPhoneChanged;
				}
				bool nameChanged = (data->firstName != fname) || (data->lastName != lname);

				bool showPhone = !isServiceUser(data->id) && !d.is_self() && !d.is_contact() && !d.is_mutual_contact();
				bool showPhoneChanged = !isServiceUser(data->id) && !d.is_self() && ((showPhone && data->contact) || (!showPhone && !data->contact));
				if (minimal) {
					showPhoneChanged = false;
					showPhone = !isServiceUser(data->id) && (data->id != peerFromUser(MTP::authedId())) && !data->contact;
				}

				// see also Local::readPeer

				QString pname = (showPhoneChanged || phoneChanged || nameChanged) ? ((showPhone && !phone.isEmpty()) ? formatPhone(phone) : QString()) : data->nameOrPhone;

				if (!minimal && d.is_self() && uname != data->username) {
					SignalHandlers::setCrashAnnotation("Username", uname);
				}
				data->setName(fname, lname, pname, uname);
				if (d.has_photo()) {
					data->setPhoto(d.vphoto);
				} else {
					data->setPhoto(MTP_userProfilePhotoEmpty());
				}
				if (d.has_access_hash()) data->access = d.vaccess_hash.v;
				status = d.has_status() ? &d.vstatus : &emptyStatus;
			}
			if (!minimal) {
				if (d.has_bot_info_version()) {
					data->setBotInfoVersion(d.vbot_info_version.v);
					data->botInfo->readsAllHistory = d.is_bot_chat_history();
					if (data->botInfo->cantJoinGroups != d.is_bot_nochats()) {
						data->botInfo->cantJoinGroups = d.is_bot_nochats();
						update.flags |= UpdateFlag::BotCanAddToGroups;
					}
					data->botInfo->inlinePlaceholder = d.has_bot_inline_placeholder() ? '_' + qs(d.vbot_inline_placeholder) : QString();
				} else {
					data->setBotInfoVersion(-1);
				}
				data->contact = (d.is_contact() || d.is_mutual_contact()) ? 1 : (data->phone().isEmpty() ? -1 : 0);
				if (data->contact == 1 && cReportSpamStatuses().value(data->id, dbiprsHidden) != dbiprsHidden) {
					cRefReportSpamStatuses().insert(data->id, dbiprsHidden);
					Local::writeReportSpamStatuses();
				}
				if (d.is_self() && ::self != data) {
					::self = data;
					Global::RefSelfChanged().notify();
				}
			}

			if (canShareThisContact != data->canShareThisContactFast()) update.flags |= UpdateFlag::UserCanShareContact;
			if (wasContact != data->isContact()) update.flags |= UpdateFlag::UserIsContact;
		} break;
		}

		if (!data) {
			return nullptr;
		}

		if (minimal) {
			if (data->loadedStatus == PeerData::NotLoaded) {
				data->loadedStatus = PeerData::MinimalLoaded;
			}
		} else if (data->loadedStatus != PeerData::FullLoaded) {
			data->loadedStatus = PeerData::FullLoaded;
		}

		auto oldOnlineTill = data->onlineTill;
		if (status && !minimal) switch (status->type()) {
		case mtpc_userStatusEmpty: data->onlineTill = 0; break;
		case mtpc_userStatusRecently:
		if (data->onlineTill > -10) { // don't modify pseudo-online
			data->onlineTill = -2;
		}
		break;
		case mtpc_userStatusLastWeek: data->onlineTill = -3; break;
		case mtpc_userStatusLastMonth: data->onlineTill = -4; break;
		case mtpc_userStatusOffline: data->onlineTill = status->c_userStatusOffline().vwas_online.v; break;
		case mtpc_userStatusOnline: data->onlineTill = status->c_userStatusOnline().vexpires.v; break;
		}
		if (oldOnlineTill != data->onlineTill) {
			update.flags |= UpdateFlag::UserOnlineChanged;
		}

		if (data->contact < 0 && !data->phone().isEmpty() && peerToUser(data->id) != MTP::authedId()) {
			data->contact = 0;
		}
		if (App::main()) {
			if ((data->contact > 0 && !wasContact) || (wasContact && data->contact < 1)) {
				Notify::userIsContactChanged(data);
			}

			markPeerUpdated(data);
			if (update.flags) {
				update.peer = data;
				Notify::peerUpdatedDelayed(update);
			}
		}
		return data;
	}

	UserData *feedUsers(const MTPVector<MTPUser> &users) {
        UserData *result = nullptr;
		for_const (auto &user, users.c_vector().v) {
			if (auto feededUser = feedUser(user)) {
				result = feededUser;
			}
		}

		return result;
	}

	PeerData *feedChat(const MTPChat &chat) {
		PeerData *data = nullptr;
		bool minimal = false;

		Notify::PeerUpdate update;
		using UpdateFlag = Notify::PeerUpdate::Flag;

		switch (chat.type()) {
		case mtpc_chat: {
			auto &d(chat.c_chat());

			data = App::chat(peerFromChat(d.vid.v));
			auto cdata = data->asChat();
			auto canEdit = cdata->canEdit();

			if (cdata->version < d.vversion.v) {
				cdata->version = d.vversion.v;
				cdata->invalidateParticipants();
			}

			data->input = MTP_inputPeerChat(d.vid);
			cdata->setName(qs(d.vtitle));
			cdata->setPhoto(d.vphoto);
			cdata->date = d.vdate.v;

			if (d.has_migrated_to() && d.vmigrated_to.type() == mtpc_inputChannel) {
				const auto &c(d.vmigrated_to.c_inputChannel());
				ChannelData *channel = App::channel(peerFromChannel(c.vchannel_id));
				if (!channel->mgInfo) {
					channel->flags |= MTPDchannel::Flag::f_megagroup;
					channel->flagsUpdated();
				}
				if (!channel->access) {
					channel->input = MTP_inputPeerChannel(c.vchannel_id, c.vaccess_hash);
					channel->inputChannel = d.vmigrated_to;
					channel->access = d.vmigrated_to.c_inputChannel().vaccess_hash.v;
				}
				bool updatedTo = (cdata->migrateToPtr != channel), updatedFrom = (channel->mgInfo->migrateFromPtr != cdata);
				if (updatedTo) {
					cdata->migrateToPtr = channel;
				}
				if (updatedFrom) {
					channel->mgInfo->migrateFromPtr = cdata;
					if (History *h = App::historyLoaded(cdata->id)) {
						if (History *hto = App::historyLoaded(channel->id)) {
							if (!h->isEmpty()) {
								h->clear(true);
							}
							if (hto->inChatList(Dialogs::Mode::All) && h->inChatList(Dialogs::Mode::All)) {
								App::removeDialog(h);
							}
						}
					}
					Notify::migrateUpdated(channel);
					update.flags |= UpdateFlag::MigrationChanged;
				}
				if (updatedTo) {
					Notify::migrateUpdated(cdata);
					update.flags |= UpdateFlag::MigrationChanged;
				}
			}

			if (!(cdata->flags & MTPDchat::Flag::f_admins_enabled) && (d.vflags.v & MTPDchat::Flag::f_admins_enabled)) {
				cdata->invalidateParticipants();
			}
			cdata->flags = d.vflags.v;

			cdata->count = d.vparticipants_count.v;
			cdata->isForbidden = false;
			if (canEdit != cdata->canEdit()) {
				update.flags |= UpdateFlag::ChatCanEdit;
			}
		} break;
		case mtpc_chatForbidden: {
			auto &d(chat.c_chatForbidden());

			data = App::chat(peerFromChat(d.vid.v));
			auto cdata = data->asChat();
			auto canEdit = cdata->canEdit();

			data->input = MTP_inputPeerChat(d.vid);
			cdata->setName(qs(d.vtitle));
			cdata->setPhoto(MTP_chatPhotoEmpty());
			cdata->date = 0;
			cdata->count = -1;
			cdata->invalidateParticipants();
			cdata->flags = 0;
			cdata->isForbidden = true;
			if (canEdit != cdata->canEdit()) {
				update.flags |= UpdateFlag::ChatCanEdit;
			}
		} break;
		case mtpc_channel: {
			auto &d(chat.c_channel());

			auto peerId = peerFromChannel(d.vid.v);
			minimal = d.is_min();
			if (minimal) {
				data = App::channelLoaded(peerId);
				if (!data) {
					return nullptr; // minimal is not loaded, need to make getDifference
				}
			} else {
				data = App::channel(peerId);
				data->input = MTP_inputPeerChannel(d.vid, d.has_access_hash() ? d.vaccess_hash : MTP_long(0));
			}

			auto cdata = data->asChannel();
			auto wasInChannel = cdata->amIn();
			auto canEditPhoto = cdata->canEditPhoto();
			auto canViewAdmins = cdata->canViewAdmins();
			auto canViewMembers = cdata->canViewMembers();
			auto canAddMembers = cdata->canAddMembers();
			auto wasEditor = cdata->amEditor();

			if (minimal) {
				auto mask = MTPDchannel::Flag::f_broadcast | MTPDchannel::Flag::f_verified | MTPDchannel::Flag::f_megagroup | MTPDchannel::Flag::f_democracy;
				cdata->flags = (cdata->flags & ~mask) | (d.vflags.v & mask);
			} else {
				cdata->inputChannel = MTP_inputChannel(d.vid, d.vaccess_hash);
				cdata->access = d.vaccess_hash.v;
				cdata->date = d.vdate.v;
				if (cdata->version < d.vversion.v) {
					cdata->version = d.vversion.v;
				}
				if (d.is_restricted()) {
					cdata->setRestrictionReason(extractRestrictionReason(qs(d.vrestriction_reason)));
				} else {
					cdata->setRestrictionReason(QString());
				}
				cdata->flags = d.vflags.v;
			}
			cdata->flagsUpdated();

			QString uname = d.has_username() ? textOneLine(qs(d.vusername)) : QString();
			cdata->setName(qs(d.vtitle), uname);

			cdata->isForbidden = false;
			cdata->setPhoto(d.vphoto);

			if (wasInChannel != cdata->amIn()) update.flags |= UpdateFlag::ChannelAmIn;
			if (canEditPhoto != cdata->canEditPhoto()) update.flags |= UpdateFlag::ChannelCanEditPhoto;
			if (canViewAdmins != cdata->canViewAdmins()) update.flags |= UpdateFlag::ChannelCanViewAdmins;
			if (canViewMembers != cdata->canViewMembers()) update.flags |= UpdateFlag::ChannelCanViewMembers;
			if (canAddMembers != cdata->canAddMembers()) update.flags |= UpdateFlag::ChannelCanAddMembers;
			if (wasEditor != cdata->amEditor()) {
				cdata->selfAdminUpdated();
				update.flags |= (UpdateFlag::ChannelAmEditor | UpdateFlag::AdminsChanged);
			}
		} break;
		case mtpc_channelForbidden: {
			auto &d(chat.c_channelForbidden());

			auto peerId = peerFromChannel(d.vid.v);
			data = App::channel(peerId);
			data->input = MTP_inputPeerChannel(d.vid, d.vaccess_hash);

			auto cdata = data->asChannel();
			auto wasInChannel = cdata->amIn();
			auto canEditPhoto = cdata->canEditPhoto();
			auto canViewAdmins = cdata->canViewAdmins();
			auto canViewMembers = cdata->canViewMembers();
			auto canAddMembers = cdata->canAddMembers();
			auto wasEditor = cdata->amEditor();

			cdata->inputChannel = MTP_inputChannel(d.vid, d.vaccess_hash);

			auto mask = mtpCastFlags(MTPDchannelForbidden::Flag::f_broadcast | MTPDchannelForbidden::Flag::f_megagroup);
			cdata->flags = (cdata->flags & ~mask) | (mtpCastFlags(d.vflags) & mask);
			cdata->flagsUpdated();

			cdata->setName(qs(d.vtitle), QString());

			cdata->access = d.vaccess_hash.v;
			cdata->setPhoto(MTP_chatPhotoEmpty());
			cdata->date = 0;
			cdata->setMembersCount(0);
			cdata->isForbidden = true;

			if (wasInChannel != cdata->amIn()) update.flags |= UpdateFlag::ChannelAmIn;
			if (canEditPhoto != cdata->canEditPhoto()) update.flags |= UpdateFlag::ChannelCanEditPhoto;
			if (canViewAdmins != cdata->canViewAdmins()) update.flags |= UpdateFlag::ChannelCanViewAdmins;
			if (canViewMembers != cdata->canViewMembers()) update.flags |= UpdateFlag::ChannelCanViewMembers;
			if (canAddMembers != cdata->canAddMembers()) update.flags |= UpdateFlag::ChannelCanAddMembers;
			if (wasEditor != cdata->amEditor()) {
				cdata->selfAdminUpdated();
				update.flags |= (UpdateFlag::ChannelAmEditor | UpdateFlag::AdminsChanged);
			}
		} break;
		}
		if (!data) {
			return nullptr;
		}

		if (minimal) {
			if (data->loadedStatus == PeerData::NotLoaded) {
				data->loadedStatus = PeerData::MinimalLoaded;
			}
		} else if (data->loadedStatus != PeerData::FullLoaded) {
			data->loadedStatus = PeerData::FullLoaded;
		}
		if (App::main()) {
			markPeerUpdated(data);
			if (update.flags) {
				update.peer = data;
				Notify::peerUpdatedDelayed(update);
			}
		}
		return data;
	}

	PeerData *feedChats(const MTPVector<MTPChat> &chats) {
		PeerData *result = nullptr;
		for_const (auto &chat, chats.c_vector().v) {
			if (auto feededChat = feedChat(chat)) {
				result = feededChat;
			}
		}
		return result;
	}

	void feedParticipants(const MTPChatParticipants &p, bool requestBotInfos, bool emitPeerUpdated) {
		ChatData *chat = 0;
		switch (p.type()) {
		case mtpc_chatParticipantsForbidden: {
			const auto &d(p.c_chatParticipantsForbidden());
			chat = App::chat(d.vchat_id.v);
			chat->count = -1;
			chat->invalidateParticipants();
		} break;

		case mtpc_chatParticipants: {
			const auto &d(p.c_chatParticipants());
			chat = App::chat(d.vchat_id.v);
			auto canEdit = chat->canEdit();
			if (!requestBotInfos || chat->version <= d.vversion.v) { // !requestBotInfos is true on getFullChat result
				chat->version = d.vversion.v;
				const auto &v(d.vparticipants.c_vector().v);
				chat->count = v.size();
				int32 pversion = chat->participants.isEmpty() ? 1 : (chat->participants.begin().value() + 1);
				chat->invitedByMe = ChatData::InvitedByMe();
				chat->admins = ChatData::Admins();
				chat->flags &= ~MTPDchat::Flag::f_admin;
				for (QVector<MTPChatParticipant>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
					int32 uid = 0, inviter = 0;
					switch (i->type()) {
					case mtpc_chatParticipantCreator: {
						const auto &p(i->c_chatParticipantCreator());
						uid = p.vuser_id.v;
						chat->creator = uid;
					} break;
					case mtpc_chatParticipantAdmin: {
						const auto &p(i->c_chatParticipantAdmin());
						uid = p.vuser_id.v;
						inviter = p.vinviter_id.v;
					} break;
					case mtpc_chatParticipant: {
						const auto &p(i->c_chatParticipant());
						uid = p.vuser_id.v;
						inviter = p.vinviter_id.v;
					} break;
					}
					if (!uid) continue;

					UserData *user = App::userLoaded(uid);
					if (user) {
						chat->participants[user] = pversion;
						if (inviter == MTP::authedId()) {
							chat->invitedByMe.insert(user);
						}
						if (i->type() == mtpc_chatParticipantAdmin) {
							chat->admins.insert(user);
							if (user->isSelf()) {
								chat->flags |= MTPDchat::Flag::f_admin;
							}
						}
					} else {
						chat->invalidateParticipants();
						break;
					}
				}
				if (!chat->participants.isEmpty()) {
					History *h = App::historyLoaded(chat->id);
					bool found = !h || !h->lastKeyboardFrom;
					int32 botStatus = -1;
					for (ChatData::Participants::iterator i = chat->participants.begin(), e = chat->participants.end(); i != e;) {
						if (i.value() < pversion) {
							i = chat->participants.erase(i);
						} else {
							if (i.key()->botInfo) {
								botStatus = 2;// (botStatus > 0/* || !i.key()->botInfo->readsAllHistory*/) ? 2 : 1;
								if (requestBotInfos && !i.key()->botInfo->inited && App::api()) App::api()->requestFullPeer(i.key());
							}
							if (!found && i.key()->id == h->lastKeyboardFrom) {
								found = true;
							}
							++i;
						}
					}
					chat->botStatus = botStatus;
					if (!found) {
						h->clearLastKeyboard();
					}
				}
			}
			if (canEdit != chat->canEdit()) {
				Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::ChatCanEdit);
			}
		} break;
		}
		Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::MembersChanged | Notify::PeerUpdate::Flag::AdminsChanged);
		if (chat && App::main()) {
			if (emitPeerUpdated) {
				App::main()->peerUpdated(chat);
			} else {
				markPeerUpdated(chat);
			}
		}
	}

	void feedParticipantAdd(const MTPDupdateChatParticipantAdd &d, bool emitPeerUpdated) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version + 1 < d.vversion.v) {
			chat->version = d.vversion.v;
			chat->invalidateParticipants();
			App::api()->requestPeer(chat);
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(chat);
				} else {
					markPeerUpdated(chat);
				}
			}
		} else if (chat->version <= d.vversion.v && chat->count >= 0) {
			chat->version = d.vversion.v;
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				if (chat->participants.isEmpty() && chat->count) {
					chat->count++;
					chat->botStatus = 0;
				} else if (chat->participants.find(user) == chat->participants.end()) {
					chat->participants[user] = (chat->participants.isEmpty() ? 1 : chat->participants.begin().value());
					if (d.vinviter_id.v == MTP::authedId()) {
						chat->invitedByMe.insert(user);
					} else {
						chat->invitedByMe.remove(user);
					}
					chat->count++;
					if (user->botInfo) {
						chat->botStatus = 2;// (chat->botStatus > 0/* || !user->botInfo->readsAllHistory*/) ? 2 : 1;
						if (!user->botInfo->inited && App::api()) App::api()->requestFullPeer(user);
					}
				}
			} else {
				chat->invalidateParticipants();
				chat->count++;
			}
			Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::MembersChanged);
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(chat);
				} else {
					markPeerUpdated(chat);
				}
			}
		}
	}

	void feedParticipantDelete(const MTPDupdateChatParticipantDelete &d, bool emitPeerUpdated) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version + 1 < d.vversion.v) {
			chat->version = d.vversion.v;
			chat->invalidateParticipants();
			App::api()->requestPeer(chat);
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(chat);
				} else {
					markPeerUpdated(chat);
				}
			}
		} else if (chat->version <= d.vversion.v && chat->count > 0) {
			chat->version = d.vversion.v;
			auto canEdit = chat->canEdit();
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				if (chat->participants.isEmpty()) {
					if (chat->count > 0) {
						chat->count--;
					}
				} else {
					ChatData::Participants::iterator i = chat->participants.find(user);
					if (i != chat->participants.end()) {
						chat->participants.erase(i);
						chat->count--;
						chat->invitedByMe.remove(user);
						chat->admins.remove(user);
						if (user->isSelf()) {
							chat->flags &= ~MTPDchat::Flag::f_admin;
						}

						History *h = App::historyLoaded(chat->id);
						if (h && h->lastKeyboardFrom == user->id) {
							h->clearLastKeyboard();
						}
					}
					if (chat->botStatus > 0 && user->botInfo) {
						int32 botStatus = -1;
						for (ChatData::Participants::const_iterator j = chat->participants.cbegin(), e = chat->participants.cend(); j != e; ++j) {
							if (j.key()->botInfo) {
								if (true || botStatus > 0/* || !j.key()->botInfo->readsAllHistory*/) {
									botStatus = 2;
									break;
								}
								botStatus = 1;
							}
						}
						chat->botStatus = botStatus;
					}
				}
			} else {
				chat->invalidateParticipants();
				chat->count--;
			}
			if (canEdit != chat->canEdit()) {
				Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::ChatCanEdit);
			}
			Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::MembersChanged);
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(chat);
				} else {
					markPeerUpdated(chat);
				}
			}
		}
	}

	void feedChatAdmins(const MTPDupdateChatAdmins &d, bool emitPeerUpdated) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version <= d.vversion.v) {
			bool badVersion = (chat->version + 1 < d.vversion.v);
			if (badVersion) {
				chat->invalidateParticipants();
				App::api()->requestPeer(chat);
			}
			chat->version = d.vversion.v;
			if (mtpIsTrue(d.venabled)) {
				if (!badVersion) {
					chat->invalidateParticipants();
				}
				chat->flags |= MTPDchat::Flag::f_admins_enabled;
			} else {
				chat->flags &= ~MTPDchat::Flag::f_admins_enabled;
			}
			Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::AdminsChanged);
			if (emitPeerUpdated) {
				App::main()->peerUpdated(chat);
			} else {
				markPeerUpdated(chat);
			}
		}
	}

	void feedParticipantAdmin(const MTPDupdateChatParticipantAdmin &d, bool emitPeerUpdated) {
		ChatData *chat = App::chat(d.vchat_id.v);
		if (chat->version + 1 < d.vversion.v) {
			chat->version = d.vversion.v;
			chat->invalidateParticipants();
			App::api()->requestPeer(chat);
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(chat);
				} else {
					markPeerUpdated(chat);
				}
			}
		} else if (chat->version <= d.vversion.v && chat->count > 0) {
			chat->version = d.vversion.v;
			auto canEdit = chat->canEdit();
			UserData *user = App::userLoaded(d.vuser_id.v);
			if (user) {
				if (mtpIsTrue(d.vis_admin)) {
					if (user->isSelf()) {
						chat->flags |= MTPDchat::Flag::f_admin;
					}
					if (chat->noParticipantInfo()) {
						App::api()->requestFullPeer(chat);
					} else {
						chat->admins.insert(user);
					}
				} else {
					if (user->isSelf()) {
						chat->flags &= ~MTPDchat::Flag::f_admin;
					}
					chat->admins.remove(user);
				}
			} else {
				chat->invalidateParticipants();
			}
			if (canEdit != chat->canEdit()) {
				Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::ChatCanEdit);
			}
			Notify::peerUpdatedDelayed(chat, Notify::PeerUpdate::Flag::AdminsChanged);
			if (App::main()) {
				if (emitPeerUpdated) {
					App::main()->peerUpdated(chat);
				} else {
					markPeerUpdated(chat);
				}
			}
		}
	}

	bool checkEntitiesAndViewsUpdate(const MTPDmessage &m) {
		auto peerId = peerFromMTP(m.vto_id);
		if (m.has_from_id() && peerToUser(peerId) == MTP::authedId()) {
			peerId = peerFromUser(m.vfrom_id);
		}
		if (auto existing = App::histItemById(peerToChannel(peerId), m.vid.v)) {
			auto text = qs(m.vmessage);
			auto entities = m.has_entities() ? entitiesFromMTP(m.ventities.c_vector().v) : EntitiesInText();
			existing->setText({ text, entities });
			existing->updateMedia(m.has_media() ? (&m.vmedia) : nullptr);
			existing->updateReplyMarkup(m.has_reply_markup() ? (&m.vreply_markup) : nullptr);
			existing->setViewsCount(m.has_views() ? m.vviews.v : -1);
			existing->addToOverview(AddToOverviewNew);

			if (!existing->detached()) {
				App::checkSavedGif(existing);
				return true;
			}

			return false;
		}
		return false;
	}

	template <typename TMTPDclass>
	void updateEditedMessage(const TMTPDclass &m) {
		auto peerId = peerFromMTP(m.vto_id);
		if (m.has_from_id() && peerToUser(peerId) == MTP::authedId()) {
			peerId = peerFromUser(m.vfrom_id);
		}
		if (auto existing = App::histItemById(peerToChannel(peerId), m.vid.v)) {
			existing->applyEdition(m);
		}
	}

	void updateEditedMessage(const MTPMessage &m) {
		if (m.type() == mtpc_message) { // apply message edit
			App::updateEditedMessage(m.c_message());
		} else if (m.type() == mtpc_messageService) {
			App::updateEditedMessage(m.c_messageService());
		}
	}

	void addSavedGif(DocumentData *doc) {
		SavedGifs &saved(cRefSavedGifs());
		int32 index = saved.indexOf(doc);
		if (index) {
			if (index > 0) saved.remove(index);
			saved.push_front(doc);
			if (saved.size() > Global::SavedGifsLimit()) saved.pop_back();
			Local::writeSavedGifs();

			if (App::main()) emit App::main()->savedGifsUpdated();
			cSetLastSavedGifsUpdate(0);
			App::main()->updateStickers();
		}
	}

	void checkSavedGif(HistoryItem *item) {
		if (!item->Has<HistoryMessageForwarded>() && (item->out() || item->history()->peer == App::self())) {
			if (HistoryMedia *media = item->getMedia()) {
				if (DocumentData *doc = media->getDocument()) {
					if (doc->isGifv()) {
						addSavedGif(doc);
					}
				}
			}
		}
	}

	void feedMsgs(const QVector<MTPMessage> &msgs, NewMessageType type) {
		QMap<uint64, int32> msgsIds;
		for (int32 i = 0, l = msgs.size(); i < l; ++i) {
			const auto &msg(msgs.at(i));
			switch (msg.type()) {
			case mtpc_message: {
				const auto &d(msg.c_message());
				bool needToAdd = true;
				if (type == NewMessageUnread) { // new message, index my forwarded messages to links overview
					if (checkEntitiesAndViewsUpdate(d)) { // already in blocks
						LOG(("Skipping message, because it is already in blocks!"));
						needToAdd = false;
					}
				}
				if (needToAdd) {
					msgsIds.insert((uint64(uint32(d.vid.v)) << 32) | uint64(i), i);
				}
			} break;
			case mtpc_messageEmpty: msgsIds.insert((uint64(uint32(msg.c_messageEmpty().vid.v)) << 32) | uint64(i), i); break;
			case mtpc_messageService: msgsIds.insert((uint64(uint32(msg.c_messageService().vid.v)) << 32) | uint64(i), i); break;
			}
		}
		for (QMap<uint64, int32>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			histories().addNewMessage(msgs.at(i.value()), type);
		}
	}

	void feedMsgs(const MTPVector<MTPMessage> &msgs, NewMessageType type) {
		return feedMsgs(msgs.c_vector().v, type);
	}

	ImagePtr image(const MTPPhotoSize &size) {
		switch (size.type()) {
		case mtpc_photoSize: {
			auto &d = size.c_photoSize();
			if (d.vlocation.type() == mtpc_fileLocation) {
				auto &l = d.vlocation.c_fileLocation();
				return ImagePtr(StorageImageLocation(d.vw.v, d.vh.v, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v), d.vsize.v);
			}
		} break;
		case mtpc_photoCachedSize: {
			auto &d = size.c_photoCachedSize();
			if (d.vlocation.type() == mtpc_fileLocation) {
				auto &l = d.vlocation.c_fileLocation();
				auto bytes = qba(d.vbytes);
				return ImagePtr(StorageImageLocation(d.vw.v, d.vh.v, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v), bytes);
			} else if (d.vlocation.type() == mtpc_fileLocationUnavailable) {
				auto bytes = qba(d.vbytes);
				return ImagePtr(StorageImageLocation(d.vw.v, d.vh.v, 0, 0, 0, 0), bytes);
			}
		} break;
		}
		return ImagePtr();
	}

	StorageImageLocation imageLocation(int32 w, int32 h, const MTPFileLocation &loc) {
		if (loc.type() == mtpc_fileLocation) {
			const auto &l(loc.c_fileLocation());
			return StorageImageLocation(w, h, l.vdc_id.v, l.vvolume_id.v, l.vlocal_id.v, l.vsecret.v);
		}
		return StorageImageLocation(w, h, 0, 0, 0, 0);
	}

	StorageImageLocation imageLocation(const MTPPhotoSize &size) {
		switch (size.type()) {
		case mtpc_photoSize: {
			const auto &d(size.c_photoSize());
			return imageLocation(d.vw.v, d.vh.v, d.vlocation);
		} break;
		case mtpc_photoCachedSize: {
			const auto &d(size.c_photoCachedSize());
			return imageLocation(d.vw.v, d.vh.v, d.vlocation);
		} break;
		}
		return StorageImageLocation();
	}

	void feedInboxRead(const PeerId &peer, MsgId upTo) {
		if (auto history = App::historyLoaded(peer)) {
			history->inboxRead(upTo);
		}
	}

	void feedOutboxRead(const PeerId &peer, MsgId upTo, TimeId when) {
		if (auto history = App::historyLoaded(peer)) {
			history->outboxRead(upTo);
			if (history->lastMsg && history->lastMsg->out() && history->lastMsg->id <= upTo) {
				if (App::main()) App::main()->dlgUpdated(history, history->lastMsg->id);
			}
			history->updateChatListEntry();

			if (history->peer->isUser()) {
				history->peer->asUser()->madeAction(when);
			}
		}
	}

	inline MsgsData *fetchMsgsData(ChannelId channelId, bool insert = true) {
		if (channelId == NoChannel) return &msgsData;
		ChannelMsgsData::iterator i = channelMsgsData.find(channelId);
		if (i == channelMsgsData.cend()) {
			if (insert) {
				i = channelMsgsData.insert(channelId, MsgsData());
			} else {
				return 0;
			}
		}
		return &(*i);
	}

	void feedWereDeleted(ChannelId channelId, const QVector<MTPint> &msgsIds) {
		MsgsData *data = fetchMsgsData(channelId, false);
		if (!data) return;

		ChannelHistory *channelHistory = (channelId == NoChannel) ? 0 : App::historyLoaded(peerFromChannel(channelId))->asChannelHistory();

		QMap<History*, bool> historiesToCheck;
		for (QVector<MTPint>::const_iterator i = msgsIds.cbegin(), e = msgsIds.cend(); i != e; ++i) {
			MsgsData::const_iterator j = data->constFind(i->v);
			if (j != data->cend()) {
				History *h = (*j)->history();
				(*j)->destroy();
				if (!h->lastMsg) historiesToCheck.insert(h, true);
			} else {
				if (channelHistory) {
					if (channelHistory->unreadCount() > 0 && i->v >= channelHistory->inboxReadBefore) {
						channelHistory->setUnreadCount(channelHistory->unreadCount() - 1);
					}
				}
			}
		}
		if (main()) {
			for (QMap<History*, bool>::const_iterator i = historiesToCheck.cbegin(), e = historiesToCheck.cend(); i != e; ++i) {
				main()->checkPeerHistory(i.key()->peer);
			}
		}
	}

	void feedUserLink(MTPint userId, const MTPContactLink &myLink, const MTPContactLink &foreignLink) {
		UserData *user = userLoaded(userId.v);
		if (user) {
			auto wasContact = user->isContact();
			bool wasShowPhone = !user->contact;
			switch (myLink.type()) {
			case mtpc_contactLinkContact:
				user->contact = 1;
				if (user->contact == 1 && cReportSpamStatuses().value(user->id, dbiprsHidden) != dbiprsHidden) {
					cRefReportSpamStatuses().insert(user->id, dbiprsHidden);
					Local::writeReportSpamStatuses();
				}
			break;
			case mtpc_contactLinkHasPhone:
				user->contact = 0;
			break;
			case mtpc_contactLinkNone:
			case mtpc_contactLinkUnknown:
				user->contact = -1;
			break;
			}
			if (user->contact < 1) {
				if (user->contact < 0 && !user->phone().isEmpty() && peerToUser(user->id) != MTP::authedId()) {
					user->contact = 0;
				}
			}

			if (wasContact != user->isContact()) {
				Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserIsContact);
			}
			if ((user->contact > 0 && !wasContact) || (wasContact && user->contact < 1)) {
				Notify::userIsContactChanged(user);
			}

			bool showPhone = !isServiceUser(user->id) && !user->isSelf() && !user->contact;
			bool showPhoneChanged = !isServiceUser(user->id) && !user->isSelf() && ((showPhone && !wasShowPhone) || (!showPhone && wasShowPhone));
			if (showPhoneChanged) {
				user->setName(textOneLine(user->firstName), textOneLine(user->lastName), showPhone ? App::formatPhone(user->phone()) : QString(), textOneLine(user->username));
			}
			markPeerUpdated(user);
		}
	}

	void markPeerUpdated(PeerData *data) {
		updatedPeers.insert(data, true);
	}

	void clearPeerUpdated(PeerData *data) {
		updatedPeers.remove(data);
	}

	void emitPeerUpdated() {
		if (!updatedPeers.isEmpty() && App::main()) {
			UpdatedPeers upd = updatedPeers;
			updatedPeers.clear();

			for (UpdatedPeers::const_iterator i = upd.cbegin(), e = upd.cend(); i != e; ++i) {
				App::main()->peerUpdated(i.key());
			}
		}
	}

	PhotoData *feedPhoto(const MTPPhoto &photo, PhotoData *convert) {
		switch (photo.type()) {
		case mtpc_photo: {
			return feedPhoto(photo.c_photo(), convert);
		} break;
		case mtpc_photoEmpty: {
			return App::photoSet(photo.c_photoEmpty().vid.v, convert, 0, 0, ImagePtr(), ImagePtr(), ImagePtr());
		} break;
		}
		return App::photo(0);
	}

	PhotoData *feedPhoto(const MTPPhoto &photo, const PreparedPhotoThumbs &thumbs) {
		const QPixmap *thumb = 0, *medium = 0, *full = 0;
		int32 thumbLevel = -1, mediumLevel = -1, fullLevel = -1;
		for (PreparedPhotoThumbs::const_iterator i = thumbs.cbegin(), e = thumbs.cend(); i != e; ++i) {
			int32 newThumbLevel = -1, newMediumLevel = -1, newFullLevel = -1;
			switch (i.key()) {
			case 's': newThumbLevel = 0; newMediumLevel = 5; newFullLevel = 4; break; // box 100x100
			case 'm': newThumbLevel = 2; newMediumLevel = 0; newFullLevel = 3; break; // box 320x320
			case 'x': newThumbLevel = 5; newMediumLevel = 3; newFullLevel = 1; break; // box 800x800
			case 'y': newThumbLevel = 6; newMediumLevel = 6; newFullLevel = 0; break; // box 1280x1280
			case 'w': newThumbLevel = 8; newMediumLevel = 8; newFullLevel = 2; break; // box 2560x2560 // if loading this fix HistoryPhoto::updateFrom
			case 'a': newThumbLevel = 1; newMediumLevel = 4; newFullLevel = 8; break; // crop 160x160
			case 'b': newThumbLevel = 3; newMediumLevel = 1; newFullLevel = 7; break; // crop 320x320
			case 'c': newThumbLevel = 4; newMediumLevel = 2; newFullLevel = 6; break; // crop 640x640
			case 'd': newThumbLevel = 7; newMediumLevel = 7; newFullLevel = 5; break; // crop 1280x1280
			}
			if (newThumbLevel < 0 || newMediumLevel < 0 || newFullLevel < 0) {
				continue;
			}
			if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
				thumbLevel = newThumbLevel;
				thumb = &i.value();
			}
			if (mediumLevel < 0 || newMediumLevel < mediumLevel) {
				mediumLevel = newMediumLevel;
				medium = &i.value();
			}
			if (fullLevel < 0 || newFullLevel < fullLevel) {
				fullLevel = newFullLevel;
				full = &i.value();
			}
		}
		if (!thumb || !medium || !full) {
			return App::photo(0);
		}
		switch (photo.type()) {
		case mtpc_photo: {
			const auto &ph(photo.c_photo());
			return App::photoSet(ph.vid.v, 0, ph.vaccess_hash.v, ph.vdate.v, ImagePtr(*thumb, "JPG"), ImagePtr(*medium, "JPG"), ImagePtr(*full, "JPG"));
		} break;
		case mtpc_photoEmpty: return App::photo(photo.c_photoEmpty().vid.v);
		}
		return App::photo(0);
	}

	PhotoData *feedPhoto(const MTPDphoto &photo, PhotoData *convert) {
		const auto &sizes(photo.vsizes.c_vector().v);
		const MTPPhotoSize *thumb = 0, *medium = 0, *full = 0;
		int32 thumbLevel = -1, mediumLevel = -1, fullLevel = -1;
		for (QVector<MTPPhotoSize>::const_iterator i = sizes.cbegin(), e = sizes.cend(); i != e; ++i) {
			char size = 0;
			switch (i->type()) {
			case mtpc_photoSize: {
				auto &s = i->c_photoSize().vtype.c_string().v;
				if (s.size()) size = s[0];
			} break;

			case mtpc_photoCachedSize: {
				auto &s = i->c_photoCachedSize().vtype.c_string().v;
				if (s.size()) size = s[0];
			} break;
			}
			if (!size) continue;

			int32 newThumbLevel = -1, newMediumLevel = -1, newFullLevel = -1;
			switch (size) {
			case 's': newThumbLevel = 0; newMediumLevel = 5; newFullLevel = 4; break; // box 100x100
			case 'm': newThumbLevel = 2; newMediumLevel = 0; newFullLevel = 3; break; // box 320x320
			case 'x': newThumbLevel = 5; newMediumLevel = 3; newFullLevel = 1; break; // box 800x800
			case 'y': newThumbLevel = 6; newMediumLevel = 6; newFullLevel = 0; break; // box 1280x1280
			case 'w': newThumbLevel = 8; newMediumLevel = 8; newFullLevel = 2; break; // box 2560x2560
			case 'a': newThumbLevel = 1; newMediumLevel = 4; newFullLevel = 8; break; // crop 160x160
			case 'b': newThumbLevel = 3; newMediumLevel = 1; newFullLevel = 7; break; // crop 320x320
			case 'c': newThumbLevel = 4; newMediumLevel = 2; newFullLevel = 6; break; // crop 640x640
			case 'd': newThumbLevel = 7; newMediumLevel = 7; newFullLevel = 5; break; // crop 1280x1280
			}
			if (newThumbLevel < 0 || newMediumLevel < 0 || newFullLevel < 0) {
				continue;
			}
			if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
				thumbLevel = newThumbLevel;
				thumb = &(*i);
			}
			if (mediumLevel < 0 || newMediumLevel < mediumLevel) {
				mediumLevel = newMediumLevel;
				medium = &(*i);
			}
			if (fullLevel < 0 || newFullLevel < fullLevel) {
				fullLevel = newFullLevel;
				full = &(*i);
			}
		}
		if (thumb && medium && full) {
			return App::photoSet(photo.vid.v, convert, photo.vaccess_hash.v, photo.vdate.v, App::image(*thumb), App::image(*medium), App::image(*full));
		}
		return App::photoSet(photo.vid.v, convert, 0, 0, ImagePtr(), ImagePtr(), ImagePtr());
	}

	DocumentData *feedDocument(const MTPdocument &document, const QPixmap &thumb) {
		switch (document.type()) {
		case mtpc_document: {
			auto &d = document.c_document();
			return App::documentSet(d.vid.v, 0, d.vaccess_hash.v, d.vversion.v, d.vdate.v, d.vattributes.c_vector().v, qs(d.vmime_type), ImagePtr(thumb, "JPG"), d.vdc_id.v, d.vsize.v, StorageImageLocation());
		} break;
		case mtpc_documentEmpty: return App::document(document.c_documentEmpty().vid.v);
		}
		return App::document(0);
	}

	DocumentData *feedDocument(const MTPdocument &document, DocumentData *convert) {
		switch (document.type()) {
		case mtpc_document: {
			return feedDocument(document.c_document(), convert);
		} break;
		case mtpc_documentEmpty: {
			return App::documentSet(document.c_documentEmpty().vid.v, convert, 0, 0, 0, QVector<MTPDocumentAttribute>(), QString(), ImagePtr(), 0, 0, StorageImageLocation());
		} break;
		}
		return App::document(0);
	}

	DocumentData *feedDocument(const MTPDdocument &document, DocumentData *convert) {
		return App::documentSet(document.vid.v, convert, document.vaccess_hash.v, document.vversion.v, document.vdate.v, document.vattributes.c_vector().v, qs(document.vmime_type), App::image(document.vthumb), document.vdc_id.v, document.vsize.v, App::imageLocation(document.vthumb));
	}

	WebPageData *feedWebPage(const MTPDwebPage &webpage, WebPageData *convert) {
		return App::webPageSet(webpage.vid.v, convert, webpage.has_type() ? qs(webpage.vtype) : qsl("article"), qs(webpage.vurl), qs(webpage.vdisplay_url), webpage.has_site_name() ? qs(webpage.vsite_name) : QString(), webpage.has_title() ? qs(webpage.vtitle) : QString(), webpage.has_description() ? qs(webpage.vdescription) : QString(), webpage.has_photo() ? App::feedPhoto(webpage.vphoto) : 0, webpage.has_document() ? App::feedDocument(webpage.vdocument) : 0, webpage.has_duration() ? webpage.vduration.v : 0, webpage.has_author() ? qs(webpage.vauthor) : QString(), 0);
	}

	WebPageData *feedWebPage(const MTPDwebPagePending &webpage, WebPageData *convert) {
		return App::webPageSet(webpage.vid.v, convert, QString(), QString(), QString(), QString(), QString(), QString(), 0, 0, 0, QString(), webpage.vdate.v);
	}

	WebPageData *feedWebPage(const MTPWebPage &webpage) {
		switch (webpage.type()) {
		case mtpc_webPage: return App::feedWebPage(webpage.c_webPage());
		case mtpc_webPageEmpty: {
			WebPageData *page = App::webPage(webpage.c_webPageEmpty().vid.v);
			if (page->pendingTill > 0) page->pendingTill = -1; // failed
			return page;
		} break;
		case mtpc_webPagePending: return App::feedWebPage(webpage.c_webPagePending());
		}
		return nullptr;
	}

	GameData *feedGame(const MTPDgame &game, GameData *convert) {
		return App::gameSet(game.vid.v, convert, game.vaccess_hash.v, qs(game.vshort_name), qs(game.vtitle), qs(game.vdescription), App::feedPhoto(game.vphoto), game.has_document() ? App::feedDocument(game.vdocument) : nullptr);
	}

	UserData *curUser() {
		return user(MTP::authedId());
	}

	PeerData *peer(const PeerId &id, PeerData::LoadedStatus restriction) {
		if (!id) return nullptr;

		auto i = peersData.constFind(id);
		if (i == peersData.cend()) {
			PeerData *newData = nullptr;
			if (peerIsUser(id)) {
				newData = new UserData(id);
			} else if (peerIsChat(id)) {
				newData = new ChatData(id);
			} else if (peerIsChannel(id)) {
				newData = new ChannelData(id);
			}
			t_assert(newData != nullptr);

			newData->input = MTPinputPeer(MTP_inputPeerEmpty());
			i = peersData.insert(id, newData);
		}
		switch (restriction) {
		case PeerData::MinimalLoaded: {
			if (i.value()->loadedStatus == PeerData::NotLoaded) {
				return nullptr;
			}
		} break;
		case PeerData::FullLoaded: {
			if (i.value()->loadedStatus != PeerData::FullLoaded) {
				return nullptr;
			}
		} break;
		}
		return i.value();
	}

	UserData *self() {
		return ::self;
	}

	PeerData *peerByName(const QString &username) {
		QString uname(username.trimmed());
		for_const (PeerData *peer, peersData) {
			if (!peer->userName().compare(uname, Qt::CaseInsensitive)) {
				return peer;
			}
		}
		return nullptr;
	}

	void updateImage(ImagePtr &old, ImagePtr now) {
		if (now->isNull()) return;
		if (old->isNull()) {
			old = now;
		} else if (DelayedStorageImage *img = old->toDelayedStorageImage()) {
			StorageImageLocation loc = now->location();
			if (!loc.isNull()) {
				img->setStorageLocation(loc);
			}
		}
	}

	PhotoData *photo(const PhotoId &photo) {
		PhotosData::const_iterator i = ::photosData.constFind(photo);
		if (i == ::photosData.cend()) {
			i = ::photosData.insert(photo, new PhotoData(photo));
		}
		return i.value();
	}

	PhotoData *photoSet(const PhotoId &photo, PhotoData *convert, const uint64 &access, int32 date, const ImagePtr &thumb, const ImagePtr &medium, const ImagePtr &full) {
		if (convert) {
			if (convert->id != photo) {
				PhotosData::iterator i = ::photosData.find(convert->id);
				if (i != ::photosData.cend() && i.value() == convert) {
					::photosData.erase(i);
				}
				convert->id = photo;
				delete convert->uploadingData;
				convert->uploadingData = 0;
			}
			if (date) {
				convert->access = access;
				convert->date = date;
				updateImage(convert->thumb, thumb);
				updateImage(convert->medium, medium);
				updateImage(convert->full, full);
			}
		}
		PhotosData::const_iterator i = ::photosData.constFind(photo);
		PhotoData *result;
		LastPhotosMap::iterator inLastIter = lastPhotosMap.end();
		if (i == ::photosData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new PhotoData(photo, access, date, thumb, medium, full);
			}
			::photosData.insert(photo, result);
		} else {
			result = i.value();
			if (result != convert && date) {
				result->access = access;
				result->date = date;
				updateImage(result->thumb, thumb);
				updateImage(result->medium, medium);
				updateImage(result->full, full);
			}
			inLastIter = lastPhotosMap.find(result);
		}
		if (inLastIter == lastPhotosMap.end()) { // insert new one
			if (lastPhotos.size() == MaxPhotosInMemory) {
				lastPhotos.front()->forget();
				lastPhotosMap.remove(lastPhotos.front());
				lastPhotos.pop_front();
			}
			lastPhotosMap.insert(result, lastPhotos.insert(lastPhotos.end(), result));
		} else {
			lastPhotos.erase(inLastIter.value()); // move to back
			(*inLastIter) = lastPhotos.insert(lastPhotos.end(), result);
		}
		return result;
	}

	DocumentData *document(const DocumentId &document) {
		DocumentsData::const_iterator i = ::documentsData.constFind(document);
		if (i == ::documentsData.cend()) {
			i = ::documentsData.insert(document, DocumentData::create(document));
		}
		return i.value();
	}

	DocumentData *documentSet(const DocumentId &document, DocumentData *convert, const uint64 &access, int32 version, int32 date, const QVector<MTPDocumentAttribute> &attributes, const QString &mime, const ImagePtr &thumb, int32 dc, int32 size, const StorageImageLocation &thumbLocation) {
		bool versionChanged = false;
		bool sentSticker = false;
		if (convert) {
			MediaKey oldKey = convert->mediaKey();
			bool idChanged = (convert->id != document);
			if (idChanged) {
				DocumentsData::iterator i = ::documentsData.find(convert->id);
				if (i != ::documentsData.cend() && i.value() == convert) {
					::documentsData.erase(i);
				}

				convert->id = document;
				convert->status = FileReady;
				sentSticker = (convert->sticker() != 0);
			}
			if (date) {
				convert->setattributes(attributes);
				versionChanged = convert->setRemoteVersion(version);
				convert->setRemoteLocation(dc, access);
				convert->date = date;
				convert->mime = mime;
				if (!thumb->isNull() && (convert->thumb->isNull() || convert->thumb->width() < thumb->width() || convert->thumb->height() < thumb->height() || versionChanged)) {
					updateImage(convert->thumb, thumb);
				}
				convert->size = size;
				convert->recountIsImage();
				if (convert->sticker() && convert->sticker()->loc.isNull() && !thumbLocation.isNull()) {
					convert->sticker()->loc = thumbLocation;
				}

				MediaKey newKey = convert->mediaKey();
				if (idChanged) {
					if (convert->voice()) {
						Local::copyAudio(oldKey, newKey);
					} else if (convert->sticker() || convert->isAnimation()) {
						Local::copyStickerImage(oldKey, newKey);
					}
				}
			}

			if (cSavedGifs().indexOf(convert) >= 0) { // id changed
				Local::writeSavedGifs();
			}
		}
		DocumentsData::const_iterator i = ::documentsData.constFind(document);
		DocumentData *result;
		if (i == ::documentsData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = DocumentData::create(document, dc, access, version, attributes);
				result->date = date;
				result->mime = mime;
				result->thumb = thumb;
				result->size = size;
				result->recountIsImage();
				if (result->sticker()) {
					result->sticker()->loc = thumbLocation;
				}
			}
			::documentsData.insert(document, result);
		} else {
			result = i.value();
			if (result != convert && date) {
				result->setattributes(attributes);
				versionChanged = result->setRemoteVersion(version);
				if (!result->isValid()) {
					result->setRemoteLocation(dc, access);
				}
				result->date = date;
				result->mime = mime;
				if (!thumb->isNull() && (result->thumb->isNull() || result->thumb->width() < thumb->width() || result->thumb->height() < thumb->height() || versionChanged)) {
					result->thumb = thumb;
				}
				result->size = size;
				result->recountIsImage();
				if (result->sticker() && result->sticker()->loc.isNull() && !thumbLocation.isNull()) {
					result->sticker()->loc = thumbLocation;
				}
			}
		}
		if (sentSticker && App::main()) {
			App::main()->incrementSticker(result);
		}
		if (versionChanged) {
			if (result->sticker() && result->sticker()->set.type() == mtpc_inputStickerSetID) {
				auto it = Global::StickerSets().constFind(result->sticker()->set.c_inputStickerSetID().vid.v);
				if (it != Global::StickerSets().cend()) {
					if (it->id == Stickers::CloudRecentSetId) {
						Local::writeRecentStickers();
					} else if (it->flags & MTPDstickerSet::Flag::f_archived) {
						Local::writeArchivedStickers();
					} else if (it->flags & MTPDstickerSet::Flag::f_installed) {
						Local::writeInstalledStickers();
					}
					if (it->flags & MTPDstickerSet_ClientFlag::f_featured) {
						Local::writeFeaturedStickers();
					}
				}
			}
			auto &items = App::documentItems();
			auto i = items.constFind(result);
			if (i != items.cend()) {
				for_const (auto item, i.value()) {
					item->setPendingInitDimensions();
				}
			}
		}
		return result;
	}

	WebPageData *webPage(const WebPageId &webPage) {
		auto i = webPagesData.constFind(webPage);
		if (i == webPagesData.cend()) {
			i = webPagesData.insert(webPage, new WebPageData(webPage));
		}
		return i.value();
	}

	WebPageData *webPageSet(const WebPageId &webPage, WebPageData *convert, const QString &type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const QString &description, PhotoData *photo, DocumentData *document, int32 duration, const QString &author, int32 pendingTill) {
		if (convert) {
			if (convert->id != webPage) {
				auto i = webPagesData.find(convert->id);
				if (i != webPagesData.cend() && i.value() == convert) {
					webPagesData.erase(i);
				}
				convert->id = webPage;
			}
			if ((convert->url.isEmpty() && !url.isEmpty()) || (convert->pendingTill && convert->pendingTill != pendingTill && pendingTill >= -1)) {
				convert->type = toWebPageType(type);
				convert->url = textClean(url);
				convert->displayUrl = textClean(displayUrl);
				convert->siteName = textClean(siteName);
				convert->title = textOneLine(textClean(title));
				convert->description = textClean(description);
				convert->photo = photo;
				convert->document = document;
				convert->duration = duration;
				convert->author = textClean(author);
				if (convert->pendingTill > 0 && pendingTill <= 0 && api()) api()->clearWebPageRequest(convert);
				convert->pendingTill = pendingTill;
				if (App::main()) App::main()->webPageUpdated(convert);
			}
		}
		auto i = webPagesData.constFind(webPage);
		WebPageData *result;
		if (i == webPagesData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new WebPageData(webPage, toWebPageType(type), url, displayUrl, siteName, title, description, document, photo, duration, author, (pendingTill >= -1) ? pendingTill : -1);
				if (pendingTill > 0 && api()) {
					api()->requestWebPageDelayed(result);
				}
			}
			webPagesData.insert(webPage, result);
		} else {
			result = i.value();
			if (result != convert) {
				if ((result->url.isEmpty() && !url.isEmpty()) || (result->pendingTill && result->pendingTill != pendingTill && pendingTill >= -1)) {
					result->type = toWebPageType(type);
					result->url = textClean(url);
					result->displayUrl = textClean(displayUrl);
					result->siteName = textClean(siteName);
					result->title = textOneLine(textClean(title));
					result->description = textClean(description);
					result->photo = photo;
					result->document = document;
					result->duration = duration;
					result->author = textClean(author);
					if (result->pendingTill > 0 && pendingTill <= 0 && api()) api()->clearWebPageRequest(result);
					result->pendingTill = pendingTill;
					if (App::main()) App::main()->webPageUpdated(result);
				}
			}
		}
		return result;
	}

	GameData *game(const GameId &game) {
		auto i = gamesData.constFind(game);
		if (i == gamesData.cend()) {
			i = gamesData.insert(game, new GameData(game));
		}
		return i.value();
	}

	GameData *gameSet(const GameId &game, GameData *convert, const uint64 &accessHash, const QString &shortName, const QString &title, const QString &description, PhotoData *photo, DocumentData *document) {
		if (convert) {
			if (convert->id != game) {
				auto i = gamesData.find(convert->id);
				if (i != gamesData.cend() && i.value() == convert) {
					gamesData.erase(i);
				}
				convert->id = game;
				convert->accessHash = 0;
			}
			if (!convert->accessHash && accessHash) {
				convert->accessHash = accessHash;
				convert->shortName = textClean(shortName);
				convert->title = textOneLine(textClean(title));
				convert->description = textClean(description);
				convert->photo = photo;
				convert->document = document;
				if (App::main()) App::main()->gameUpdated(convert);
			}
		}
		auto i = gamesData.constFind(game);
		GameData *result;
		if (i == gamesData.cend()) {
			if (convert) {
				result = convert;
			} else {
				result = new GameData(game, accessHash, shortName, title, description, photo, document);
			}
			gamesData.insert(game, result);
		} else {
			result = i.value();
			if (result != convert) {
				if (!result->accessHash && accessHash) {
					result->accessHash = accessHash;
					result->shortName = textClean(shortName);
					result->title = textOneLine(textClean(title));
					result->description = textClean(description);
					result->photo = photo;
					result->document = document;
					if (App::main()) App::main()->gameUpdated(result);
				}
			}
		}
		return result;
	}

	LocationData *location(const LocationCoords &coords) {
		auto i = locationsData.constFind(coords);
		if (i == locationsData.cend()) {
			i = locationsData.insert(coords, new LocationData(coords));
		}
		return i.value();
	}

	void forgetMedia() {
		lastPhotos.clear();
		lastPhotosMap.clear();
		for_const (auto photo, ::photosData) {
			photo->forget();
		}
		for_const (auto document, ::documentsData) {
			document->forget();
		}
		for_const (auto location, ::locationsData) {
			location->thumb->forget();
		}
	}

	MTPPhoto photoFromUserPhoto(MTPint userId, MTPint date, const MTPUserProfilePhoto &photo) {
		if (photo.type() == mtpc_userProfilePhoto) {
			const auto &uphoto(photo.c_userProfilePhoto());

			QVector<MTPPhotoSize> photoSizes;
			photoSizes.push_back(MTP_photoSize(MTP_string("a"), uphoto.vphoto_small, MTP_int(160), MTP_int(160), MTP_int(0)));
			photoSizes.push_back(MTP_photoSize(MTP_string("c"), uphoto.vphoto_big, MTP_int(640), MTP_int(640), MTP_int(0)));

			MTPDphoto::Flags photoFlags = 0;
			return MTP_photo(MTP_flags(photoFlags), uphoto.vphoto_id, MTP_long(0), date, MTP_vector<MTPPhotoSize>(photoSizes));
		}
		return MTP_photoEmpty(MTP_long(0));
	}

	QString peerName(const PeerData *peer, bool forDialogs) {
		return peer ? ((forDialogs && peer->isUser() && !peer->asUser()->nameOrPhone.isEmpty()) ? peer->asUser()->nameOrPhone : peer->name) : lang(lng_deleted);
	}

	Histories &histories() {
		return ::histories;
	}

	History *history(const PeerId &peer) {
		return ::histories.findOrInsert(peer);
	}

	History *historyFromDialog(const PeerId &peer, int32 unreadCnt, int32 maxInboxRead, int32 maxOutboxRead) {
		return ::histories.findOrInsert(peer, unreadCnt, maxInboxRead, maxOutboxRead);
	}

	History *historyLoaded(const PeerId &peer) {
		return ::histories.find(peer);
	}

	HistoryItem *histItemById(ChannelId channelId, MsgId itemId) {
		if (!itemId) return nullptr;

		auto data = fetchMsgsData(channelId, false);
		if (!data) return nullptr;

		auto i = data->constFind(itemId);
		if (i != data->cend()) {
			return i.value();
		}
		return nullptr;
	}

	void historyRegItem(HistoryItem *item) {
		MsgsData *data = fetchMsgsData(item->channelId());
		MsgsData::const_iterator i = data->constFind(item->id);
		if (i == data->cend()) {
			data->insert(item->id, item);
		} else if (i.value() != item) {
			LOG(("App Error: trying to historyRegItem() an already registered item"));
			i.value()->destroy();
			data->insert(item->id, item);
		}
	}

	void historyItemDetached(HistoryItem *item) {
		if (::hoveredItem == item) {
			hoveredItem(nullptr);
		}
		if (::pressedItem == item) {
			pressedItem(nullptr);
		}
		if (::hoveredLinkItem == item) {
			hoveredLinkItem(nullptr);
		}
		if (::pressedLinkItem == item) {
			pressedLinkItem(nullptr);
		}
		if (::contextItem == item) {
			contextItem(nullptr);
		}
		if (::mousedItem == item) {
			mousedItem(nullptr);
		}
	}

	void historyUnregItem(HistoryItem *item) {
		auto data = fetchMsgsData(item->channelId(), false);
		if (!data) return;

		auto i = data->find(item->id);
		if (i != data->cend()) {
			if (i.value() == item) {
				data->erase(i);
			}
		}
		historyItemDetached(item);
		auto j = ::dependentItems.find(item);
		if (j != ::dependentItems.cend()) {
			DependentItemsSet items;
			std::swap(items, j.value());
			::dependentItems.erase(j);

			for_const (auto dependent, items) {
				dependent->dependencyItemRemoved(item);
			}
		}
		if (auto manager = Window::Notifications::manager()) {
			manager->clearFromItem(item);
		}
		if (Global::started() && !App::quitting()) {
			Global::RefItemRemoved().notify(item, true);
		}
	}

	void historyUpdateDependent(HistoryItem *item) {
		DependentItems::iterator j = ::dependentItems.find(item);
		if (j != ::dependentItems.cend()) {
			for_const (HistoryItem *dependent, j.value()) {
				dependent->updateDependencyItem();
			}
		}
		if (App::main()) {
			App::main()->itemEdited(item);
		}
	}

	void historyClearMsgs() {
		::dependentItems.clear();

		QVector<HistoryItem*> toDelete;
		for_const (HistoryItem *item, msgsData) {
			if (item->detached()) {
				toDelete.push_back(item);
			}
		}
		for_const (const MsgsData &chMsgsData, channelMsgsData) {
			for_const (HistoryItem *item, chMsgsData) {
				if (item->detached()) {
					toDelete.push_back(item);
				}
			}
		}
		msgsData.clear();
		channelMsgsData.clear();
		for (int32 i = 0, l = toDelete.size(); i < l; ++i) {
			delete toDelete[i];
		}

		clearMousedItems();
	}

	void historyClearItems() {
		randomData.clear();
		sentData.clear();
		mutedPeers.clear();
		updatedPeers.clear();
		cSetSavedPeers(SavedPeers());
		cSetSavedPeersByTime(SavedPeersByTime());
		cSetRecentInlineBots(RecentInlineBots());

		for_const (auto peer, ::peersData) {
			delete peer;
		}
		::peersData.clear();
		for_const (auto game, ::gamesData) {
			delete game;
		}
		::gamesData.clear();
		for_const (auto webpage, ::webPagesData) {
			delete webpage;
		}
		::webPagesData.clear();
		for_const (auto photo, ::photosData) {
			delete photo;
		}
		::photosData.clear();
		for_const (auto document, ::documentsData) {
			delete document;
		}
		::documentsData.clear();

		if (api()) api()->clearWebPageRequests();
		cSetRecentStickers(RecentStickerPack());
		Global::SetStickerSets(Stickers::Sets());
		Global::SetStickerSetsOrder(Stickers::Order());
		Global::SetLastStickersUpdate(0);
		Global::SetLastRecentStickersUpdate(0);
		Global::SetFeaturedStickerSetsOrder(Stickers::Order());
		Global::SetFeaturedStickerSetsUnreadCount(0);
		Global::SetLastFeaturedStickersUpdate(0);
		Global::SetArchivedStickerSetsOrder(Stickers::Order());
		cSetSavedGifs(SavedGifs());
		cSetLastSavedGifsUpdate(0);
		cSetReportSpamStatuses(ReportSpamStatuses());
		cSetAutoDownloadPhoto(0);
		cSetAutoDownloadAudio(0);
		cSetAutoDownloadGif(0);
		::photoItems.clear();
		::documentItems.clear();
		::webPageItems.clear();
		::gameItems.clear();
		::sharedContactItems.clear();
		::gifItems.clear();
		lastPhotos.clear();
		lastPhotosMap.clear();
		::self = nullptr;
		Global::RefSelfChanged().notify(true);
	}

	void historyRegDependency(HistoryItem *dependent, HistoryItem *dependency) {
		::dependentItems[dependency].insert(dependent);
	}

	void historyUnregDependency(HistoryItem *dependent, HistoryItem *dependency) {
		auto i = ::dependentItems.find(dependency);
		if (i != ::dependentItems.cend()) {
			i.value().remove(dependent);
			if (i.value().isEmpty()) {
				::dependentItems.erase(i);
			}
		}
	}

	void historyRegRandom(uint64 randomId, const FullMsgId &itemId) {
		randomData.insert(randomId, itemId);
	}

	void historyUnregRandom(uint64 randomId) {
		randomData.remove(randomId);
	}

	FullMsgId histItemByRandom(uint64 randomId) {
		RandomData::const_iterator i = randomData.constFind(randomId);
		if (i != randomData.cend()) {
			return i.value();
		}
		return FullMsgId();
	}

	void historyRegSentData(uint64 randomId, const PeerId &peerId, const QString &text) {
		sentData.insert(randomId, qMakePair(peerId, text));
	}

	void historyUnregSentData(uint64 randomId) {
		sentData.remove(randomId);
	}

	void histSentDataByItem(uint64 randomId, PeerId &peerId, QString &text) {
		QPair<PeerId, QString> d = sentData.value(randomId);
		peerId = d.first;
		text = d.second;
	}

	void prepareCorners(RoundCorners index, int32 radius, const style::color &color, const style::color *shadow = 0, QImage *cors = 0) {
		int32 r = radius * cIntRetinaFactor(), s = st::msgShadow * cIntRetinaFactor();
		QImage rect(r * 3, r * 3 + (shadow ? s : 0), QImage::Format_ARGB32_Premultiplied), localCors[4];
		{
			QPainter p(&rect);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(QRect(0, 0, rect.width(), rect.height()), st::transparent->b);
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.setPen(Qt::NoPen);
			if (shadow) {
				p.setBrush((*shadow)->b);
				p.drawRoundedRect(0, s, r * 3, r * 3, r, r);
			}
			p.setBrush(color->b);
			p.drawRoundedRect(0, 0, r * 3, r * 3, r, r);
		}
		if (!cors) cors = localCors;
		cors[0] = rect.copy(0, 0, r, r);
		cors[1] = rect.copy(r * 2, 0, r, r);
		cors[2] = rect.copy(0, r * 2, r, r + (shadow ? s : 0));
		cors[3] = rect.copy(r * 2, r * 2, r, r + (shadow ? s : 0));
		if (index != SmallMaskCorners && index != LargeMaskCorners) {
			for (int i = 0; i < 4; ++i) {
				::corners[index].p[i] = new QPixmap(pixmapFromImageInPlace(std_::move(cors[i])));
				::corners[index].p[i]->setDevicePixelRatio(cRetinaFactor());
			}
		}
	}

	void tryFontFamily(QString &family, const QString &tryFamily) {
		if (family.isEmpty()) {
			if (!QFontInfo(QFont(tryFamily)).family().trimmed().compare(tryFamily, Qt::CaseInsensitive)) {
				family = tryFamily;
			}
		}
	}

	int msgRadius() {
		static int MsgRadius = ([]() {
			auto minMsgHeight = (st::msgPadding.top() + st::msgFont->height + st::msgPadding.bottom());
			return minMsgHeight / 2;
		})();
		return MsgRadius;
	}

	void initMedia() {
		if (!::monofont) {
			QString family;
			tryFontFamily(family, qsl("Consolas"));
			tryFontFamily(family, qsl("Liberation Mono"));
			tryFontFamily(family, qsl("Menlo"));
			tryFontFamily(family, qsl("Courier"));
			if (family.isEmpty()) family = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
			::monofont = style::font(st::normalFont->f.pixelSize(), 0, family);
		}
		emojiInit();
		if (!::emoji) {
			::emoji = new QPixmap(QLatin1String(EName));
            if (cRetina()) ::emoji->setDevicePixelRatio(cRetinaFactor());
		}
		if (!::emojiLarge) {
			::emojiLarge = new QPixmap(QLatin1String(EmojiNames[EIndex + 1]));
			if (cRetina()) ::emojiLarge->setDevicePixelRatio(cRetinaFactor());
		}

		QImage mask[4];
		prepareCorners(LargeMaskCorners, msgRadius(), st::white, nullptr, mask);
		for (int i = 0; i < 4; ++i) {
			::cornersMaskLarge[i] = new QImage(mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied));
			::cornersMaskLarge[i]->setDevicePixelRatio(cRetinaFactor());
		}
		prepareCorners(SmallMaskCorners, st::buttonRadius, st::white, nullptr, mask);
		for (int i = 0; i < 4; ++i) {
			::cornersMaskSmall[i] = new QImage(mask[i].convertToFormat(QImage::Format_ARGB32_Premultiplied));
			::cornersMaskSmall[i]->setDevicePixelRatio(cRetinaFactor());
		}
		prepareCorners(WhiteCorners, st::dateRadius, st::white);
		prepareCorners(StickerCorners, st::dateRadius, st::msgServiceBg);
		prepareCorners(StickerSelectedCorners, st::dateRadius, st::msgServiceSelectBg);
		prepareCorners(SelectedOverlaySmallCorners, st::buttonRadius, st::msgSelectOverlay);
		prepareCorners(SelectedOverlayLargeCorners, msgRadius(), st::msgSelectOverlay);
		prepareCorners(DateCorners, st::dateRadius, st::msgDateImgBg);
		prepareCorners(DateSelectedCorners, st::dateRadius, st::msgDateImgBgSelected);
		prepareCorners(InShadowCorners, msgRadius(), st::msgInShadow);
		prepareCorners(InSelectedShadowCorners, msgRadius(), st::msgInShadowSelected);
		prepareCorners(ForwardCorners, msgRadius(), st::forwardBg);
		prepareCorners(MediaviewSaveCorners, st::mediaviewControllerRadius, st::medviewSaveMsg);
		prepareCorners(EmojiHoverCorners, st::buttonRadius, st::emojiPanHover);
		prepareCorners(StickerHoverCorners, st::buttonRadius, st::emojiPanHover);
		prepareCorners(BotKeyboardCorners, st::buttonRadius, st::botKbBg);
		prepareCorners(BotKeyboardOverCorners, st::buttonRadius, st::botKbOverBg);
		prepareCorners(BotKeyboardDownCorners, st::buttonRadius, st::botKbDownBg);
		prepareCorners(PhotoSelectOverlayCorners, st::buttonRadius, st::overviewPhotoSelectOverlay);

		prepareCorners(DocBlueCorners, st::buttonRadius, st::msgFileBlueColor);
		prepareCorners(DocGreenCorners, st::buttonRadius, st::msgFileGreenColor);
		prepareCorners(DocRedCorners, st::buttonRadius, st::msgFileRedColor);
		prepareCorners(DocYellowCorners, st::buttonRadius, st::msgFileYellowColor);

		prepareCorners(MessageInCorners, msgRadius(), st::msgInBg, &st::msgInShadow);
		prepareCorners(MessageInSelectedCorners, msgRadius(), st::msgInBgSelected, &st::msgInShadowSelected);
		prepareCorners(MessageOutCorners, msgRadius(), st::msgOutBg, &st::msgOutShadow);
		prepareCorners(MessageOutSelectedCorners, msgRadius(), st::msgOutBgSelected, &st::msgOutShadowSelected);
	}

	void clearHistories() {
		ClickHandler::clearActive();
		ClickHandler::unpressed();

		histories().clear();

		clearStorageImages();
		cSetServerBackgrounds(WallPapers());

		serviceImageCacheSize = imageCacheSize();
	}

	void deinitMedia() {
		delete ::emoji;
		::emoji = 0;
		delete ::emojiLarge;
		::emojiLarge = 0;
		for (int j = 0; j < 4; ++j) {
			for (int i = 0; i < RoundCornersCount; ++i) {
				delete ::corners[i].p[j]; ::corners[i].p[j] = nullptr;
			}
			delete ::cornersMaskSmall[j]; ::cornersMaskSmall[j] = nullptr;
			delete ::cornersMaskLarge[j]; ::cornersMaskLarge[j] = nullptr;
		}
		for (auto i = ::cornersMap.cbegin(), e = ::cornersMap.cend(); i != e; ++i) {
			for (int j = 0; j < 4; ++j) {
				delete i->p[j];
			}
		}
		::cornersMap.clear();
		mainEmojiMap.clear();
		otherEmojiMap.clear();

		Data::clearGlobalStructures();

		clearAllImages();
	}

	void hoveredItem(HistoryItem *item) {
		::hoveredItem = item;
	}

	HistoryItem *hoveredItem() {
		return ::hoveredItem;
	}

	void pressedItem(HistoryItem *item) {
		::pressedItem = item;
	}

	HistoryItem *pressedItem() {
		return ::pressedItem;
	}

	void hoveredLinkItem(HistoryItem *item) {
		::hoveredLinkItem = item;
	}

	HistoryItem *hoveredLinkItem() {
		return ::hoveredLinkItem;
	}

	void pressedLinkItem(HistoryItem *item) {
		::pressedLinkItem = item;
	}

	HistoryItem *pressedLinkItem() {
		return ::pressedLinkItem;
	}

	void contextItem(HistoryItem *item) {
		::contextItem = item;
	}

	HistoryItem *contextItem() {
		return ::contextItem;
	}

	void mousedItem(HistoryItem *item) {
		::mousedItem = item;
	}

	HistoryItem *mousedItem() {
		return ::mousedItem;
	}

	void clearMousedItems() {
		hoveredItem(nullptr);
		pressedItem(nullptr);
		hoveredLinkItem(nullptr);
		pressedLinkItem(nullptr);
		contextItem(nullptr);
		mousedItem(nullptr);
	}

	const style::font &monofont() {
		return ::monofont;
	}

	const QPixmap &sprite() {
		return style::spritePixmap();
	}

	const QPixmap &emoji() {
		return *::emoji;
	}

	const QPixmap &emojiLarge() {
		return *::emojiLarge;
	}

	const QPixmap &emojiSingle(EmojiPtr emoji, int32 fontHeight) {
		EmojiMap *map = &(fontHeight == st::taDefFlat.font->height ? mainEmojiMap : otherEmojiMap[fontHeight]);
		EmojiMap::const_iterator i = map->constFind(emojiKey(emoji));
		if (i == map->cend()) {
			QImage img(ESize + st::emojiPadding * cIntRetinaFactor() * 2, fontHeight * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
            if (cRetina()) img.setDevicePixelRatio(cRetinaFactor());
			{
				QPainter p(&img);
				QPainter::CompositionMode m = p.compositionMode();
				p.setCompositionMode(QPainter::CompositionMode_Source);
				p.fillRect(0, 0, img.width(), img.height(), Qt::transparent);
				p.setCompositionMode(m);
				emojiDraw(p, emoji, st::emojiPadding * cIntRetinaFactor(), (fontHeight * cIntRetinaFactor() - ESize) / 2);
			}
			i = map->insert(emojiKey(emoji), App::pixmapFromImageInPlace(std_::move(img)));
		}
		return i.value();
	}

	void playSound() {
		if (Global::SoundNotify() && !Platform::Notifications::skipAudio()) {
			audioPlayNotify();
		}
	}

	void checkImageCacheSize() {
		int64 nowImageCacheSize = imageCacheSize();
		if (nowImageCacheSize > serviceImageCacheSize + MemoryForImageCache) {
			App::forgetMedia();
			serviceImageCacheSize = imageCacheSize();
		}
	}

	bool isValidPhone(QString phone) {
		phone = phone.replace(QRegularExpression(qsl("[^\\d]")), QString());
		return phone.length() >= 8 || phone == qsl("777") || phone == qsl("333") || phone == qsl("111") || (phone.startsWith(qsl("42")) && (phone.length() == 2 || phone.length() == 5 || phone == qsl("4242")));
	}

	void quit() {
		if (quitting()) return;
		setLaunchState(QuitRequested);

		if (auto window = wnd()) {
			if (!Sandbox::isSavingSession()) {
				window->hide();
			}
		}
		if (auto mainwidget = main()) {
			mainwidget->saveDraftToCloud();
		}
		if (auto apiwrap = api()) {
			if (apiwrap->hasUnsavedDrafts()) {
				apiwrap->saveDraftsToCloud();
				QTimer::singleShot(SaveDraftBeforeQuitTimeout, Application::instance(), SLOT(quit()));
				return;
			}
		}
		Application::quit();
	}

	bool quitting() {
		return _launchState != Launched;
	}

	void allDraftsSaved() {
		if (quitting()) {
			Application::quit();
		}
	}

	LaunchState launchState() {
		return _launchState;
	}

	void setLaunchState(LaunchState state) {
		_launchState = state;
	}

	QImage readImage(QByteArray data, QByteArray *format, bool opaque, bool *animated) {
        QByteArray tmpFormat;
		QImage result;
		QBuffer buffer(&data);
        if (!format) {
            format = &tmpFormat;
        }
		{
			QImageReader reader(&buffer, *format);
#ifndef OS_MAC_OLD
			reader.setAutoTransform(true);
#endif // OS_MAC_OLD
			if (animated) *animated = reader.supportsAnimation() && reader.imageCount() > 1;
			QByteArray fmt = reader.format();
			if (!fmt.isEmpty()) *format = fmt;
			if (!reader.read(&result)) {
				return QImage();
			}
			fmt = reader.format();
			if (!fmt.isEmpty()) *format = fmt;
		}
		buffer.seek(0);
        QString fmt = QString::fromUtf8(*format).toLower();
		if (fmt == "jpg" || fmt == "jpeg") {
#ifdef OS_MAC_OLD
			ExifData *exifData = exif_data_new_from_data((const uchar*)(data.constData()), data.size());
			if (exifData) {
				ExifByteOrder byteOrder = exif_data_get_byte_order(exifData);
				ExifEntry *exifEntry = exif_data_get_entry(exifData, EXIF_TAG_ORIENTATION);
				if (exifEntry) {
					QTransform orientationFix;
					int orientation = exif_get_short(exifEntry->data, byteOrder);
					switch (orientation) {
					case 2: orientationFix = QTransform(-1, 0, 0, 1, 0, 0); break;
					case 3: orientationFix = QTransform(-1, 0, 0, -1, 0, 0); break;
					case 4: orientationFix = QTransform(1, 0, 0, -1, 0, 0); break;
					case 5: orientationFix = QTransform(0, -1, -1, 0, 0, 0); break;
					case 6: orientationFix = QTransform(0, 1, -1, 0, 0, 0); break;
					case 7: orientationFix = QTransform(0, 1, 1, 0, 0, 0); break;
					case 8: orientationFix = QTransform(0, -1, 1, 0, 0, 0); break;
					}
					result = result.transformed(orientationFix);
				}
				exif_data_free(exifData);
			}
#endif // OS_MAC_OLD
		} else if (opaque && result.hasAlphaChannel()) {
			QImage solid(result.width(), result.height(), QImage::Format_ARGB32_Premultiplied);
			solid.fill(st::white->c);
			{
				QPainter(&solid).drawImage(0, 0, result);
			}
			result = solid;
		}
		return result;
	}

	QImage readImage(const QString &file, QByteArray *format, bool opaque, bool *animated, QByteArray *content) {
		QFile f(file);
		if (!f.open(QIODevice::ReadOnly)) {
			if (animated) *animated = false;
			return QImage();
		}
		QByteArray img = f.readAll();
		QImage result = readImage(img, format, opaque, animated);
		if (content && !result.isNull()) *content = img;
		return result;
	}

	QPixmap pixmapFromImageInPlace(QImage &&image) {
		return QPixmap::fromImage(std_::forward<QImage>(image), Qt::ColorOnly);
	}

	void regPhotoItem(PhotoData *data, HistoryItem *item) {
		::photoItems[data].insert(item);
	}

	void unregPhotoItem(PhotoData *data, HistoryItem *item) {
		::photoItems[data].remove(item);
	}

	const PhotoItems &photoItems() {
		return ::photoItems;
	}

	const PhotosData &photosData() {
		return ::photosData;
	}

	void regDocumentItem(DocumentData *data, HistoryItem *item) {
		::documentItems[data].insert(item);
	}

	void unregDocumentItem(DocumentData *data, HistoryItem *item) {
		::documentItems[data].remove(item);
	}

	const DocumentItems &documentItems() {
		return ::documentItems;
	}

	const DocumentsData &documentsData() {
		return ::documentsData;
	}

	void regWebPageItem(WebPageData *data, HistoryItem *item) {
		::webPageItems[data].insert(item);
	}

	void unregWebPageItem(WebPageData *data, HistoryItem *item) {
		::webPageItems[data].remove(item);
	}

	const WebPageItems &webPageItems() {
		return ::webPageItems;
	}

	void regGameItem(GameData *data, HistoryItem *item) {
		::gameItems[data].insert(item);
	}

	void unregGameItem(GameData *data, HistoryItem *item) {
		::gameItems[data].remove(item);
	}

	const GameItems &gameItems() {
		return ::gameItems;
	}

	void regSharedContactItem(int32 userId, HistoryItem *item) {
		auto user = App::userLoaded(userId);
		auto canShareThisContact = user ? user->canShareThisContact() : false;
		::sharedContactItems[userId].insert(item);
		if (canShareThisContact != (user ? user->canShareThisContact() : false)) {
			Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserCanShareContact);
		}
	}

	void unregSharedContactItem(int32 userId, HistoryItem *item) {
		auto user = App::userLoaded(userId);
		auto canShareThisContact = user ? user->canShareThisContact() : false;
		::sharedContactItems[userId].remove(item);
		if (canShareThisContact != (user ? user->canShareThisContact() : false)) {
			Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserCanShareContact);
		}
	}

	const SharedContactItems &sharedContactItems() {
		return ::sharedContactItems;
	}

	void regGifItem(Media::Clip::Reader *reader, HistoryItem *item) {
		::gifItems.insert(reader, item);
	}

	void unregGifItem(Media::Clip::Reader *reader) {
		::gifItems.remove(reader);
	}

	void stopGifItems() {
		if (!::gifItems.isEmpty()) {
			GifItems gifs = ::gifItems;
			for (GifItems::const_iterator i = gifs.cbegin(), e = gifs.cend(); i != e; ++i) {
				if (HistoryMedia *media = i.value()->getMedia()) {
					media->stopInline();
				}
			}
		}
	}

	QString phoneFromSharedContact(int32 userId) {
		auto i = ::sharedContactItems.constFind(userId);
		if (i != ::sharedContactItems.cend() && !i->isEmpty()) {
			if (auto media = (*i->cbegin())->getMedia()) {
				if (media->type() == MediaTypeContact) {
					return static_cast<HistoryContact*>(media)->phone();
				}
			}
		}
		return QString();
	}

	void regMuted(PeerData *peer, int32 changeIn) {
		::mutedPeers.insert(peer, true);
		if (App::main()) App::main()->updateMutedIn(changeIn);
	}

	void unregMuted(PeerData *peer) {
		::mutedPeers.remove(peer);
	}

	void updateMuted() {
		int32 changeInMin = 0;
		for (MutedPeers::iterator i = ::mutedPeers.begin(); i != ::mutedPeers.end();) {
			int32 changeIn = 0;
			History *h = App::history(i.key()->id);
			if (isNotifyMuted(i.key()->notify, &changeIn)) {
				h->setMute(true);
				if (changeIn && (!changeInMin || changeIn < changeInMin)) {
					changeInMin = changeIn;
				}
				++i;
			} else {
				h->setMute(false);
				i = ::mutedPeers.erase(i);
			}
		}
		if (changeInMin) App::main()->updateMutedIn(changeInMin);
	}

	void setProxySettings(QNetworkAccessManager &manager) {
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
		manager.setProxy(getHttpProxySettings());
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	}

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	QNetworkProxy getHttpProxySettings() {
		const ProxyData *proxy = nullptr;
		if (Global::started()) {
			proxy = (Global::ConnectionType() == dbictHttpProxy) ? (&Global::ConnectionProxy()) : nullptr;
		} else {
			proxy = Sandbox::PreLaunchProxy().host.isEmpty() ? nullptr : (&Sandbox::PreLaunchProxy());
		}
		if (proxy) {
			return QNetworkProxy(QNetworkProxy::HttpProxy, proxy->host, proxy->port, proxy->user, proxy->password);
		}
		return QNetworkProxy(QNetworkProxy::DefaultProxy);
	}
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY

	void setProxySettings(QTcpSocket &socket) {
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
		if (Global::ConnectionType() == dbictTcpProxy) {
			auto &p = Global::ConnectionProxy();
			socket.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, p.host, p.port, p.user, p.password));
		} else {
			socket.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
		}
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	}

	QImage **cornersMask(ImageRoundRadius radius) {
		switch (radius) {
		case ImageRoundRadius::Large: return ::cornersMaskLarge;
		case ImageRoundRadius::Small:
		default: break;
		}
		return ::cornersMaskSmall;
	}
	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &bg, const CornersPixmaps &c, const style::color *sh) {
		int32 cw = c.p[0]->width() / cIntRetinaFactor(), ch = c.p[0]->height() / cIntRetinaFactor();
		if (w < 2 * cw || h < 2 * ch) return;
		if (w > 2 * cw) {
			p.fillRect(QRect(x + cw, y, w - 2 * cw, ch), bg->b);
			p.fillRect(QRect(x + cw, y + h - ch, w - 2 * cw, ch), bg->b);
			if (sh) p.fillRect(QRect(x + cw, y + h, w - 2 * cw, st::msgShadow), (*sh)->b);
		}
		if (h > 2 * ch) {
			p.fillRect(QRect(x, y + ch, w, h - 2 * ch), bg->b);
		}
		p.drawPixmap(QPoint(x, y), *c.p[0]);
		p.drawPixmap(QPoint(x + w - cw, y), *c.p[1]);
		p.drawPixmap(QPoint(x, y + h - ch), *c.p[2]);
		p.drawPixmap(QPoint(x + w - cw, y + h - ch), *c.p[3]);
	}

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &bg, RoundCorners index, const style::color *sh) {
		roundRect(p, x, y, w, h, bg, ::corners[index], sh);
	}

	void roundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &sh, RoundCorners index) {
		const CornersPixmaps &c = ::corners[index];
		int32 cw = c.p[0]->width() / cIntRetinaFactor(), ch = c.p[0]->height() / cIntRetinaFactor();
		p.fillRect(x + cw, y + h, w - 2 * cw, st::msgShadow, sh->b);
		p.fillRect(x, y + h - ch, cw, st::msgShadow, sh->b);
		p.fillRect(x + w - cw, y + h - ch, cw, st::msgShadow, sh->b);
		p.drawPixmap(x, y + h - ch + st::msgShadow, *c.p[2]);
		p.drawPixmap(x + w - cw, y + h - ch + st::msgShadow, *c.p[3]);
	}

	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &bg, ImageRoundRadius radius) {
		uint32 colorKey = ((uint32(bg->c.alpha()) & 0xFF) << 24) | ((uint32(bg->c.red()) & 0xFF) << 16) | ((uint32(bg->c.green()) & 0xFF) << 8) | ((uint32(bg->c.blue()) & 0xFF) << 24);
		CornersMap::const_iterator i = cornersMap.find(colorKey);
		if (i == cornersMap.cend()) {
			QImage images[4];
			switch (radius) {
			case ImageRoundRadius::Small: prepareCorners(SmallMaskCorners, st::buttonRadius, bg, nullptr, images); break;
			case ImageRoundRadius::Large: prepareCorners(LargeMaskCorners, msgRadius(), bg, nullptr, images); break;
			default: p.fillRect(x, y, w, h, bg); return;
			}

			CornersPixmaps pixmaps;
			for (int j = 0; j < 4; ++j) {
				pixmaps.p[j] = new QPixmap(pixmapFromImageInPlace(std_::move(images[j])));
				pixmaps.p[j]->setDevicePixelRatio(cRetinaFactor());
			}
			i = cornersMap.insert(colorKey, pixmaps);
		}
		roundRect(p, x, y, w, h, bg, i.value(), 0);
	}

	void initBackground(int32 id, const QImage &p, bool nowrite) {
		if (Local::readBackground()) return;

		uint64 components[3] = { 0 }, componentsScroll[3] = { 0 }, componentsPoint[3] = { 0 };
		int size = 0;

		QImage img(p);
		bool remove = false;
		if (p.isNull()) {
			if (id == DefaultChatBackground) {
				img.load(st::msgBG);
			} else {
				img.load(st::msgBG0);
				if (cRetina()) {
					img = img.scaledToWidth(img.width() * 2, Qt::SmoothTransformation);
				} else if (cScale() != dbisOne) {
					img = img.scaledToWidth(convertScale(img.width()), Qt::SmoothTransformation);
				}
				id = 0;
			}
			remove = true;
		}
		if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_ARGB32_Premultiplied && img.format() != QImage::Format_RGB32) {
			img = img.convertToFormat(QImage::Format_RGB32);
		}
		img.setDevicePixelRatio(cRetinaFactor());

		if (!nowrite) {
			Local::writeBackground(id, remove ? QImage() : img);
		}

		int w = img.width(), h = img.height();
		size = w * h;
		const uchar *pix = img.constBits();
		if (pix) {
			for (int32 i = 0, l = size * 4; i < l; i += 4) {
				components[2] += pix[i + 0];
				components[1] += pix[i + 1];
				components[0] += pix[i + 2];
			}
		}

		if (size) {
			for (int32 i = 0; i < 3; ++i) components[i] /= size;
		}
		int maxtomin[3] = { 0, 1, 2 };
		if (components[maxtomin[0]] < components[maxtomin[1]]) {
			qSwap(maxtomin[0], maxtomin[1]);
		}
		if (components[maxtomin[1]] < components[maxtomin[2]]) {
			qSwap(maxtomin[1], maxtomin[2]);
			if (components[maxtomin[0]] < components[maxtomin[1]]) {
				qSwap(maxtomin[0], maxtomin[1]);
			}
		}

		uint64 max = qMax(1ULL, components[maxtomin[0]]), mid = qMax(1ULL, components[maxtomin[1]]), min = qMax(1ULL, components[maxtomin[2]]);

		Window::chatBackground()->init(id, pixmapFromImageInPlace(std_::move(img)));

		memcpy(componentsScroll, components, sizeof(components));
		memcpy(componentsPoint, components, sizeof(components));

		if (max != min) {
			if (min > uint64(qRound(0.77 * max))) {
				uint64 newmin = qRound(0.77 * max); // min saturation 23%
				uint64 newmid = max - ((max - mid) * (max - newmin)) / (max - min);
				components[maxtomin[1]] = newmid;
				components[maxtomin[2]] = newmin;
			}
			uint64 newmin = qRound(0.77 * max); // saturation 23% for scroll
			uint64 newmid = max - ((max - mid) * (max - newmin)) / (max - min);
			componentsScroll[maxtomin[1]] = newmid;
			componentsScroll[maxtomin[2]] = newmin;

			uint64 pmax = 227; // 89% brightness
			uint64 pmin = qRound(0.75 * pmax); // 41% saturation
			uint64 pmid = pmax - ((max - mid) * (pmax - pmin)) / (max - min);
			componentsPoint[maxtomin[0]] = pmax;
			componentsPoint[maxtomin[1]] = pmid;
			componentsPoint[maxtomin[2]] = pmin;
		} else {
			componentsPoint[0] = componentsPoint[1] = componentsPoint[2] = 227; // 89% brightness
		}

		float64 luminance = 0.299 * componentsScroll[0] + 0.587 * componentsScroll[1] + 0.114 * componentsScroll[2];
		uint64 maxScroll = max;
		if (luminance < 0.5 * 0xFF) {
			maxScroll += qRound(0.2 * 0xFF);
		} else {
			maxScroll -= qRound(0.2 * 0xFF);
		}
		componentsScroll[maxtomin[2]] = qMin(uint64(float64(componentsScroll[maxtomin[2]]) * maxScroll / float64(componentsScroll[maxtomin[0]])), 0xFFULL);
		componentsScroll[maxtomin[1]] = qMin(uint64(float64(componentsScroll[maxtomin[1]]) * maxScroll / float64(componentsScroll[maxtomin[0]])), 0xFFULL);
		componentsScroll[maxtomin[0]] = qMin(maxScroll, 0xFFULL);

        if (max > uint64(qRound(0.2 * 0xFF))) { // brightness greater than 20%
			max -= qRound(0.2 * 0xFF);
		} else {
			max = 0;
		}
		components[maxtomin[2]] = uint64(float64(components[maxtomin[2]]) * max / float64(components[maxtomin[0]]));
		components[maxtomin[1]] = uint64(float64(components[maxtomin[1]]) * max / float64(components[maxtomin[0]]));
		components[maxtomin[0]] = max;

		uchar r = uchar(components[0]), g = uchar(components[1]), b = uchar(components[2]);
		float64 alpha = st::msgServiceBg->c.alphaF();
		_msgServiceBg = style::color(r, g, b, qRound(alpha * 0xFF));

		float64 alphaSel = st::msgServiceSelectBg->c.alphaF(), addSel = (1. - ((1. - alphaSel) / (1. - alpha))) * 0xFF;
		uchar rsel = snap(qRound(((1. - alphaSel) * r + addSel) / alphaSel), 0, 0xFF);
		uchar gsel = snap(qRound(((1. - alphaSel) * g + addSel) / alphaSel), 0, 0xFF);
		uchar bsel = snap(qRound(((1. - alphaSel) * b + addSel) / alphaSel), 0, 0xFF);
		_msgServiceSelectBg = style::color(r, g, b, qRound(alphaSel * 0xFF));

		for (int i = 0; i < 4; ++i) {
			delete ::corners[StickerCorners].p[i]; ::corners[StickerCorners].p[i] = nullptr;
			delete ::corners[StickerSelectedCorners].p[i]; ::corners[StickerSelectedCorners].p[i] = nullptr;
		}
		prepareCorners(StickerCorners, st::dateRadius, _msgServiceBg);
		prepareCorners(StickerSelectedCorners, st::dateRadius, _msgServiceSelectBg);

		uchar rScroll = uchar(componentsScroll[0]), gScroll = uchar(componentsScroll[1]), bScroll = uchar(componentsScroll[2]);
		_historyScrollBarColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.barColor->c.alphaF() * 0xFF));
		_historyScrollBgColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.bgColor->c.alphaF() * 0xFF));
		_historyScrollBarOverColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.barOverColor->c.alphaF() * 0xFF));
		_historyScrollBgOverColor = style::color(rScroll, gScroll, bScroll, qRound(st::historyScroll.bgOverColor->c.alphaF() * 0xFF));

		uchar rPoint = uchar(componentsPoint[0]), gPoint = uchar(componentsPoint[1]), bPoint = uchar(componentsPoint[2]);
		_introPointHoverColor = style::color(rPoint, gPoint, bPoint);

		if (App::main()) {
			App::main()->updateScrollColors();
		}
		HistoryLayout::serviceColorsUpdated();
	}

	const style::color &msgServiceBg() {
		return _msgServiceBg;
	}

	const style::color &msgServiceSelectBg() {
		return _msgServiceSelectBg;
	}

	const style::color &historyScrollBarColor() {
		return _historyScrollBarColor;
	}

	const style::color &historyScrollBgColor() {
		return _historyScrollBgColor;
	}

	const style::color &historyScrollBarOverColor() {
		return _historyScrollBarOverColor;
	}

	const style::color &historyScrollBgOverColor() {
		return _historyScrollBgOverColor;
	}

	const style::color &introPointHoverColor() {
		return _introPointHoverColor;
	}

	WallPapers gServerBackgrounds;

}
