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

#include "core/basic_types.h"
#include "history.h"
#include "history/history_item.h"
#include "history/history_media.h"
#include "history/history_message.h"
#include "layout.h"

class AppClass;
class MainWindow;
class MainWidget;
class ApiWrap;
class FileUploader;

using HistoryItemsMap = OrderedSet<HistoryItem*>;
using PhotoItems = QHash<PhotoData*, HistoryItemsMap>;
using DocumentItems = QHash<DocumentData*, HistoryItemsMap>;
using WebPageItems = QHash<WebPageData*, HistoryItemsMap>;
using GameItems = QHash<GameData*, HistoryItemsMap>;
using SharedContactItems = QHash<int32, HistoryItemsMap>;
using GifItems = QHash<Media::Clip::Reader*, HistoryItem*>;

using PhotosData = QHash<PhotoId, PhotoData*>;
using DocumentsData = QHash<DocumentId, DocumentData*>;

struct LocationCoords;
struct LocationData;

namespace App {
	AppClass *app();
	MainWindow *wnd();
	MainWidget *main();
	bool passcoded();
	FileUploader *uploader();
	ApiWrap *api();

	void logOut();

	QString formatPhone(QString phone);

	TimeId onlineForSort(UserData *user, TimeId now);
	int32 onlineWillChangeIn(UserData *user, TimeId now);
	int32 onlineWillChangeIn(TimeId online, TimeId now);
	QString onlineText(UserData *user, TimeId now, bool precise = false);
	QString onlineText(TimeId online, TimeId now, bool precise = false);
	bool onlineColorUse(UserData *user, TimeId now);
	bool onlineColorUse(TimeId online, TimeId now);

	UserData *feedUser(const MTPUser &user);
	UserData *feedUsers(const MTPVector<MTPUser> &users); // returns last user
	PeerData *feedChat(const MTPChat &chat);
	PeerData *feedChats(const MTPVector<MTPChat> &chats); // returns last chat

	void feedParticipants(const MTPChatParticipants &p, bool requestBotInfos, bool emitPeerUpdated = true);
	void feedParticipantAdd(const MTPDupdateChatParticipantAdd &d, bool emitPeerUpdated = true);
	void feedParticipantDelete(const MTPDupdateChatParticipantDelete &d, bool emitPeerUpdated = true);
	void feedChatAdmins(const MTPDupdateChatAdmins &d, bool emitPeerUpdated = true);
	void feedParticipantAdmin(const MTPDupdateChatParticipantAdmin &d, bool emitPeerUpdated = true);
	bool checkEntitiesAndViewsUpdate(const MTPDmessage &m); // returns true if item found and it is not detached
	void updateEditedMessage(const MTPMessage &m);
	void addSavedGif(DocumentData *doc);
	void checkSavedGif(HistoryItem *item);
	void feedMsgs(const QVector<MTPMessage> &msgs, NewMessageType type);
	void feedMsgs(const MTPVector<MTPMessage> &msgs, NewMessageType type);
	void feedInboxRead(const PeerId &peer, MsgId upTo);
	void feedOutboxRead(const PeerId &peer, MsgId upTo, TimeId when);
	void feedWereDeleted(ChannelId channelId, const QVector<MTPint> &msgsIds);
	void feedUserLink(MTPint userId, const MTPContactLink &myLink, const MTPContactLink &foreignLink);

	void markPeerUpdated(PeerData *data);
	void clearPeerUpdated(PeerData *data);

	ImagePtr image(const MTPPhotoSize &size);
	StorageImageLocation imageLocation(int32 w, int32 h, const MTPFileLocation &loc);
	StorageImageLocation imageLocation(const MTPPhotoSize &size);

	PhotoData *feedPhoto(const MTPPhoto &photo, const PreparedPhotoThumbs &thumbs);
	PhotoData *feedPhoto(const MTPPhoto &photo, PhotoData *convert = nullptr);
	PhotoData *feedPhoto(const MTPDphoto &photo, PhotoData *convert = nullptr);
	DocumentData *feedDocument(const MTPdocument &document, const QPixmap &thumb);
	DocumentData *feedDocument(const MTPdocument &document, DocumentData *convert = nullptr);
	DocumentData *feedDocument(const MTPDdocument &document, DocumentData *convert = nullptr);
	WebPageData *feedWebPage(const MTPDwebPage &webpage, WebPageData *convert = nullptr);
	WebPageData *feedWebPage(const MTPDwebPagePending &webpage, WebPageData *convert = nullptr);
	WebPageData *feedWebPage(const MTPWebPage &webpage);
	GameData *feedGame(const MTPDgame &game, GameData *convert = nullptr);

	PeerData *peer(const PeerId &id, PeerData::LoadedStatus restriction = PeerData::NotLoaded);
	inline UserData *user(const PeerId &id, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asUser(peer(id, restriction));
	}
	inline ChatData *chat(const PeerId &id, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asChat(peer(id, restriction));
	}
	inline ChannelData *channel(const PeerId &id, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asChannel(peer(id, restriction));
	}
	inline UserData *user(UserId userId, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asUser(peer(peerFromUser(userId), restriction));
	}
	inline ChatData *chat(ChatId chatId, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asChat(peer(peerFromChat(chatId), restriction));
	}
	inline ChannelData *channel(ChannelId channelId, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asChannel(peer(peerFromChannel(channelId), restriction));
	}
	inline PeerData *peerLoaded(const PeerId &id) {
		return peer(id, PeerData::FullLoaded);
	}
	inline UserData *userLoaded(const PeerId &id) {
		return user(id, PeerData::FullLoaded);
	}
	inline ChatData *chatLoaded(const PeerId &id) {
		return chat(id, PeerData::FullLoaded);
	}
	inline ChannelData *channelLoaded(const PeerId &id) {
		return channel(id, PeerData::FullLoaded);
	}
	inline UserData *userLoaded(UserId userId) {
		return user(userId, PeerData::FullLoaded);
	}
	inline ChatData *chatLoaded(ChatId chatId) {
		return chat(chatId, PeerData::FullLoaded);
	}
	inline ChannelData *channelLoaded(ChannelId channelId) {
		return channel(channelId, PeerData::FullLoaded);
	}

	UserData *self();
	PeerData *peerByName(const QString &username);
	QString peerName(const PeerData *peer, bool forDialogs = false);
	PhotoData *photo(const PhotoId &photo);
	PhotoData *photoSet(const PhotoId &photo, PhotoData *convert, const uint64 &access, int32 date, const ImagePtr &thumb, const ImagePtr &medium, const ImagePtr &full);
	DocumentData *document(const DocumentId &document);
	DocumentData *documentSet(const DocumentId &document, DocumentData *convert, const uint64 &access, int32 version, int32 date, const QVector<MTPDocumentAttribute> &attributes, const QString &mime, const ImagePtr &thumb, int32 dc, int32 size, const StorageImageLocation &thumbLocation);
	WebPageData *webPage(const WebPageId &webPage);
	WebPageData *webPageSet(const WebPageId &webPage, WebPageData *convert, const QString &type, const QString &url, const QString &displayUrl, const QString &siteName, const QString &title, const QString &description, PhotoData *photo, DocumentData *doc, int32 duration, const QString &author, int32 pendingTill);
	GameData *game(const GameId &game);
	GameData *gameSet(const GameId &game, GameData *convert, const uint64 &accessHash, const QString &shortName, const QString &title, const QString &description, PhotoData *photo, DocumentData *doc);
	LocationData *location(const LocationCoords &coords);
	void forgetMedia();

	MTPPhoto photoFromUserPhoto(MTPint userId, MTPint date, const MTPUserProfilePhoto &photo);

	Histories &histories();
	History *history(const PeerId &peer);
	History *historyFromDialog(const PeerId &peer, int32 unreadCnt, int32 maxInboxRead, int32 maxOutboxRead);
	History *historyLoaded(const PeerId &peer);
	HistoryItem *histItemById(ChannelId channelId, MsgId itemId);
	inline History *history(const PeerData *peer) {
		t_assert(peer != nullptr);
		return history(peer->id);
	}
	inline History *historyLoaded(const PeerData *peer) {
		return peer ? historyLoaded(peer->id) : nullptr;
	}
	inline HistoryItem *histItemById(const ChannelData *channel, MsgId itemId) {
		return histItemById(channel ? peerToChannel(channel->id) : 0, itemId);
	}
	inline HistoryItem *histItemById(const FullMsgId &msgId) {
		return histItemById(msgId.channel, msgId.msg);
	}
	void historyRegItem(HistoryItem *item);
	void historyItemDetached(HistoryItem *item);
	void historyUnregItem(HistoryItem *item);
	void historyUpdateDependent(HistoryItem *item);
	void historyClearMsgs();
	void historyClearItems();
	void historyRegDependency(HistoryItem *dependent, HistoryItem *dependency);
	void historyUnregDependency(HistoryItem *dependent, HistoryItem *dependency);

	void historyRegRandom(uint64 randomId, const FullMsgId &itemId);
	void historyUnregRandom(uint64 randomId);
	FullMsgId histItemByRandom(uint64 randomId);
	void historyRegSentData(uint64 randomId, const PeerId &peerId, const QString &text);
	void historyUnregSentData(uint64 randomId);
	void histSentDataByItem(uint64 randomId, PeerId &peerId, QString &text);

	void hoveredItem(HistoryItem *item);
	HistoryItem *hoveredItem();
	void pressedItem(HistoryItem *item);
	HistoryItem *pressedItem();
	void hoveredLinkItem(HistoryItem *item);
	HistoryItem *hoveredLinkItem();
	void pressedLinkItem(HistoryItem *item);
	HistoryItem *pressedLinkItem();
	void contextItem(HistoryItem *item);
	HistoryItem *contextItem();
	void mousedItem(HistoryItem *item);
	HistoryItem *mousedItem();
	void clearMousedItems();

	const style::font &monofont();
	const QPixmap &sprite();
	const QPixmap &emoji();
	const QPixmap &emojiLarge();
	const QPixmap &emojiSingle(EmojiPtr emoji, int32 fontHeight);

	void clearHistories();

	void initMedia();
	void deinitMedia();
	void playSound();

	void checkImageCacheSize();

	bool isValidPhone(QString phone);

	enum LaunchState {
		Launched = 0,
		QuitRequested = 1,
		QuitProcessed = 2,
	};
	void quit();
	bool quitting();
	void allDraftsSaved();
	LaunchState launchState();
	void setLaunchState(LaunchState state);

	QImage readImage(QByteArray data, QByteArray *format = 0, bool opaque = true, bool *animated = 0);
	QImage readImage(const QString &file, QByteArray *format = 0, bool opaque = true, bool *animated = 0, QByteArray *content = 0);
	QPixmap pixmapFromImageInPlace(QImage &&image);

	void regPhotoItem(PhotoData *data, HistoryItem *item);
	void unregPhotoItem(PhotoData *data, HistoryItem *item);
	const PhotoItems &photoItems();
	const PhotosData &photosData();

	void regDocumentItem(DocumentData *data, HistoryItem *item);
	void unregDocumentItem(DocumentData *data, HistoryItem *item);
	const DocumentItems &documentItems();
	const DocumentsData &documentsData();

	void regWebPageItem(WebPageData *data, HistoryItem *item);
	void unregWebPageItem(WebPageData *data, HistoryItem *item);
	const WebPageItems &webPageItems();

	void regGameItem(GameData *data, HistoryItem *item);
	void unregGameItem(GameData *data, HistoryItem *item);
	const GameItems &gameItems();

	void regSharedContactItem(int32 userId, HistoryItem *item);
	void unregSharedContactItem(int32 userId, HistoryItem *item);
	const SharedContactItems &sharedContactItems();
	QString phoneFromSharedContact(int32 userId);

	void regGifItem(Media::Clip::Reader *reader, HistoryItem *item);
	void unregGifItem(Media::Clip::Reader *reader);
	void stopGifItems();

	void regMuted(PeerData *peer, int32 changeIn);
	void unregMuted(PeerData *peer);
	void updateMuted();

	void setProxySettings(QNetworkAccessManager &manager);
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	QNetworkProxy getHttpProxySettings();
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	void setProxySettings(QTcpSocket &socket);

	QImage **cornersMask(ImageRoundRadius radius);
	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &bg, RoundCorners index, const style::color *sh = 0);
	inline void roundRect(Painter &p, const QRect &rect, const style::color &bg, RoundCorners index, const style::color *sh = 0) {
		return roundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, index, sh);
	}
	void roundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &sh, RoundCorners index);
	inline void roundShadow(Painter &p, const QRect &rect, const style::color &sh, RoundCorners index) {
		return roundShadow(p, rect.x(), rect.y(), rect.width(), rect.height(), sh, index);
	}
	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, const style::color &bg, ImageRoundRadius radius);
	inline void roundRect(Painter &p, const QRect &rect, const style::color &bg, ImageRoundRadius radius) {
		return roundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, radius);
	}

	void initBackground(int32 id = DefaultChatBackground, const QImage &p = QImage(), bool nowrite = false);

	const style::color &msgServiceBg();
	const style::color &msgServiceSelectBg();
	const style::color &historyScrollBarColor();
	const style::color &historyScrollBgColor();
	const style::color &historyScrollBarOverColor();
	const style::color &historyScrollBgOverColor();
	const style::color &introPointHoverColor();

	struct WallPaper {
		WallPaper(int32 id, ImagePtr thumb, ImagePtr full) : id(id), thumb(thumb), full(full) {
		}
		int32 id;
		ImagePtr thumb;
		ImagePtr full;
	};
	typedef QList<WallPaper> WallPapers;
	DeclareSetting(WallPapers, ServerBackgrounds);

};
