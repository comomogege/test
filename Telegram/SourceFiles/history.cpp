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
#include "history.h"

#include "history/history_media_types.h"
#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_dialogs.h"
#include "data/data_drafts.h"
#include "lang.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "localstorage.h"
#include "window/top_bar_widget.h"
#include "observer_peer.h"

namespace {

constexpr int kStatusShowClientsideRecordVideo = 6000;
constexpr int kStatusShowClientsideUploadVideo = 6000;
constexpr int kStatusShowClientsideRecordVoice = 6000;
constexpr int kStatusShowClientsideUploadVoice = 6000;
constexpr int kStatusShowClientsideUploadPhoto = 6000;
constexpr int kStatusShowClientsideUploadFile = 6000;
constexpr int kStatusShowClientsideChooseLocation = 6000;
constexpr int kStatusShowClientsideChooseContact = 6000;
constexpr int kStatusShowClientsidePlayGame = 10000;

} // namespace

void historyInit() {
	historyInitMessages();
	historyInitMedia();
}

History::History(const PeerId &peerId)
: peer(App::peer(peerId))
, lastItemTextCache(st::dialogsTextWidthMin)
, typingText(st::dialogsTextWidthMin)
, cloudDraftTextCache(st::dialogsTextWidthMin)
, _mute(isNotifyMuted(peer->notify)) {
	if (peer->isUser() && peer->asUser()->botInfo) {
		outboxReadBefore = INT_MAX;
	}
	for (auto &countData : overviewCountData) {
		countData = -1; // not loaded yet
	}
}

void History::clearLastKeyboard() {
	if (lastKeyboardId) {
		if (lastKeyboardId == lastKeyboardHiddenId) {
			lastKeyboardHiddenId = 0;
		}
		lastKeyboardId = 0;
		if (auto main = App::main()) {
			main->updateBotKeyboard(this);
		}
	}
	lastKeyboardInited = true;
	lastKeyboardFrom = 0;
}

bool History::canHaveFromPhotos() const {
	if (peer->isUser() && !Adaptive::Wide()) {
		return false;
	} else if (isChannel() && !peer->isMegagroup()) {
		return false;
	}
	return true;
}

void History::setHasPendingResizedItems() {
	_flags |= Flag::f_has_pending_resized_items;
	Global::RefHandleHistoryUpdate().call();
}

void History::setLocalDraft(std_::unique_ptr<Data::Draft> &&draft) {
	_localDraft = std_::move(draft);
}

void History::takeLocalDraft(History *from) {
	if (auto &draft = from->_localDraft) {
		if (!draft->textWithTags.text.isEmpty() && !_localDraft) {
			_localDraft = std_::move(draft);

			// Edit and reply to drafts can't migrate.
			// Cloud drafts do not migrate automatically.
			_localDraft->msgId = 0;
		}
		from->clearLocalDraft();
	}
}

void History::createLocalDraftFromCloud() {
	auto draft = cloudDraft();
	if (Data::draftIsNull(draft) || !draft->date.isValid()) return;

	auto existing = localDraft();
	if (Data::draftIsNull(existing) || !existing->date.isValid() || draft->date >= existing->date) {
		if (!existing) {
			setLocalDraft(std_::make_unique<Data::Draft>(draft->textWithTags, draft->msgId, draft->cursor, draft->previewCancelled));
			existing = localDraft();
		} else if (existing != draft) {
			existing->textWithTags = draft->textWithTags;
			existing->msgId = draft->msgId;
			existing->cursor = draft->cursor;
			existing->previewCancelled = draft->previewCancelled;
		}
		existing->date = draft->date;
	}
}

void History::setCloudDraft(std_::unique_ptr<Data::Draft> &&draft) {
	_cloudDraft = std_::move(draft);
	cloudDraftTextCache.clear();
}

Data::Draft *History::createCloudDraft(Data::Draft *fromDraft) {
	if (Data::draftIsNull(fromDraft)) {
		setCloudDraft(std_::make_unique<Data::Draft>(TextWithTags(), 0, MessageCursor(), false));
		cloudDraft()->date = QDateTime();
	} else {
		auto existing = cloudDraft();
		if (!existing) {
			setCloudDraft(std_::make_unique<Data::Draft>(fromDraft->textWithTags, fromDraft->msgId, fromDraft->cursor, fromDraft->previewCancelled));
			existing = cloudDraft();
		} else if (existing != fromDraft) {
			existing->textWithTags = fromDraft->textWithTags;
			existing->msgId = fromDraft->msgId;
			existing->cursor = fromDraft->cursor;
			existing->previewCancelled = fromDraft->previewCancelled;
		}
		existing->date = ::date(myunixtime());
	}

	cloudDraftTextCache.clear();
	updateChatListSortPosition();

	return cloudDraft();
}

void History::setEditDraft(std_::unique_ptr<Data::Draft> &&draft) {
	_editDraft = std_::move(draft);
}

void History::clearLocalDraft() {
	_localDraft = nullptr;
}

void History::clearCloudDraft() {
	if (_cloudDraft) {
		_cloudDraft = nullptr;
		cloudDraftTextCache.clear();
		updateChatListSortPosition();
	}
}

void History::clearEditDraft() {
	_editDraft = nullptr;
}

void History::draftSavedToCloud() {
	updateChatListEntry();
	if (App::main()) App::main()->writeDrafts(this);
}

bool History::updateTyping(uint64 ms, bool force) {
	bool changed = force;
	for (auto i = typing.begin(), e = typing.end(); i != e;) {
		if (ms >= i.value()) {
			i = typing.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	for (auto i = sendActions.begin(); i != sendActions.cend();) {
		if (ms >= i.value().until) {
			i = sendActions.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	if (changed) {
		QString newTypingStr;
		int typingCount = typing.size();
		if (typingCount > 2) {
			newTypingStr = lng_many_typing(lt_count, typingCount);
		} else if (typingCount > 1) {
			newTypingStr = lng_users_typing(lt_user, typing.begin().key()->firstName, lt_second_user, (typing.end() - 1).key()->firstName);
		} else if (typingCount) {
			newTypingStr = peer->isUser() ? lang(lng_typing) : lng_user_typing(lt_user, typing.begin().key()->firstName);
		} else if (!sendActions.isEmpty()) {
			// Handles all actions except game playing.
			auto sendActionString = [](SendActionType type, const QString &name) -> QString {
				switch (type) {
				case SendActionRecordVideo: return name.isEmpty() ? lang(lng_send_action_record_video) : lng_user_action_record_video(lt_user, name);
				case SendActionUploadVideo: return name.isEmpty() ? lang(lng_send_action_upload_video) : lng_user_action_upload_video(lt_user, name);
				case SendActionRecordVoice: return name.isEmpty() ? lang(lng_send_action_record_audio) : lng_user_action_record_audio(lt_user, name);
				case SendActionUploadVoice: return name.isEmpty() ? lang(lng_send_action_upload_audio) : lng_user_action_upload_audio(lt_user, name);
				case SendActionUploadPhoto: return name.isEmpty() ? lang(lng_send_action_upload_photo) : lng_user_action_upload_photo(lt_user, name);
				case SendActionUploadFile: return name.isEmpty() ? lang(lng_send_action_upload_file) : lng_user_action_upload_file(lt_user, name);
				case SendActionChooseLocation: return name.isEmpty() ? lang(lng_send_action_geo_location) : lng_user_action_geo_location(lt_user, name);
				case SendActionChooseContact: return name.isEmpty() ? lang(lng_send_action_choose_contact) : lng_user_action_choose_contact(lt_user, name);
				default: break;
				};
				return QString();
			};
			for (auto i = sendActions.cbegin(), e = sendActions.cend(); i != e; ++i) {
				newTypingStr = sendActionString(i->type, peer->isUser() ? QString() : i.key()->firstName);
				if (!newTypingStr.isEmpty()) {
					break;
				}
			}

			// Everyone in sendActions are playing a game.
			if (newTypingStr.isEmpty()) {
				int playingCount = sendActions.size();
				if (playingCount > 2) {
					newTypingStr = lng_many_playing_game(lt_count, playingCount);
				} else if (playingCount > 1) {
					newTypingStr = lng_users_playing_game(lt_user, sendActions.begin().key()->firstName, lt_second_user, (sendActions.end() - 1).key()->firstName);
				} else {
					newTypingStr = peer->isUser() ? lang(lng_playing_game) : lng_user_playing_game(lt_user, sendActions.begin().key()->firstName);
				}
			}
		}
		if (!newTypingStr.isEmpty()) {
			newTypingStr += qsl("...");
		}
		if (typingStr != newTypingStr) {
			typingText.setText(st::dialogsTextFont, (typingStr = newTypingStr), _textNameOptions);
		}
	}
	if (!typingStr.isEmpty()) {
		if (typingText.lastDots(typingDots % 4)) {
			changed = true;
		}
	}
	if (changed && App::main()) {
		updateChatListEntry();
		if (App::main()->historyPeer() == peer) {
			App::main()->topBar()->update();
		}
	}
	return changed;
}

ChannelHistory::ChannelHistory(const PeerId &peer) : History(peer)
, _joinedMessage(nullptr) {
}

void ChannelHistory::getRangeDifference() {
	MsgId fromId = 0, toId = 0;
	for (int32 blockIndex = 0, blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) {
		HistoryBlock *block = blocks.at(blockIndex);
		for (int32 itemIndex = 0, itemsCount = block->items.size(); itemIndex < itemsCount; ++itemIndex) {
			HistoryItem *item = block->items.at(itemIndex);
			if (item->type() == HistoryItemMsg && item->id > 0) {
				fromId = item->id;
				break;
			}
		}
		if (fromId) break;
	}
	if (!fromId) return;
	for (int32 blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int32 itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if (item->type() == HistoryItemMsg && item->id > 0) {
				toId = item->id;
				break;
			}
		}
		if (toId) break;
	}
	if (fromId > 0 && peer->asChannel()->pts() > 0) {
		if (_rangeDifferenceRequestId) {
			MTP::cancel(_rangeDifferenceRequestId);
		}
		_rangeDifferenceFromId = fromId;
		_rangeDifferenceToId = toId;

		MTP_LOG(0, ("getChannelDifference { good - after channelDifferenceTooLong was received, validating history part }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getRangeDifferenceNext(peer->asChannel()->pts());
	}
}

void ChannelHistory::getRangeDifferenceNext(int32 pts) {
	if (!App::main() || _rangeDifferenceToId < _rangeDifferenceFromId) return;

	int32 limit = _rangeDifferenceToId + 1 - _rangeDifferenceFromId;
	_rangeDifferenceRequestId = MTP::send(MTPupdates_GetChannelDifference(peer->asChannel()->inputChannel, MTP_channelMessagesFilter(MTP_flags(MTPDchannelMessagesFilter::Flags(0)), MTP_vector<MTPMessageRange>(1, MTP_messageRange(MTP_int(_rangeDifferenceFromId), MTP_int(_rangeDifferenceToId)))), MTP_int(pts), MTP_int(limit)), App::main()->rpcDone(&MainWidget::gotRangeDifference, peer->asChannel()));
}

HistoryJoined *ChannelHistory::insertJoinedMessage(bool unread) {
	if (_joinedMessage || !peer->asChannel()->amIn() || (peer->isMegagroup() && peer->asChannel()->mgInfo->joinedMessageFound)) {
		return _joinedMessage;
	}

	UserData *inviter = (peer->asChannel()->inviter > 0) ? App::userLoaded(peer->asChannel()->inviter) : nullptr;
	if (!inviter) return nullptr;

	MTPDmessage::Flags flags = 0;
	if (peerToUser(inviter->id) == MTP::authedId()) {
		unread = false;
	//} else if (unread) {
	//	flags |= MTPDmessage::Flag::f_unread;
	}

	QDateTime inviteDate = peer->asChannel()->inviteDate;
	if (unread) _maxReadMessageDate = inviteDate;
	if (isEmpty()) {
		_joinedMessage = HistoryJoined::create(this, inviteDate, inviter, flags);
		addNewItem(_joinedMessage, unread);
		return _joinedMessage;
	}

	for (int32 blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int32 itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			HistoryItemType type = item->type();
			if (type == HistoryItemMsg) {
				// Due to a server bug sometimes inviteDate is less (before) than the
				// first message in the megagroup (message about migration), let us
				// ignore that and think, that the inviteDate is always greater-or-equal.
				if (item->isGroupMigrate() && peer->isMegagroup() && peer->migrateFrom()) {
					peer->asChannel()->mgInfo->joinedMessageFound = true;
					return nullptr;
				}
				if (item->date <= inviteDate) {
					++itemIndex;
					_joinedMessage = HistoryJoined::create(this, inviteDate, inviter, flags);
					addNewInTheMiddle(_joinedMessage, blockIndex, itemIndex);
					if (lastMsgDate.isNull() || inviteDate >= lastMsgDate) {
						setLastMessage(_joinedMessage);
						if (unread) {
							newItemAdded(_joinedMessage);
						}
					}
					return _joinedMessage;
				}
			}
		}
	}

	startBuildingFrontBlock();

	_joinedMessage = HistoryJoined::create(this, inviteDate, inviter, flags);
	addItemToBlock(_joinedMessage);

	finishBuildingFrontBlock();

	return _joinedMessage;
}

void ChannelHistory::checkJoinedMessage(bool createUnread) {
	if (_joinedMessage || peer->asChannel()->inviter <= 0) {
		return;
	}
	if (isEmpty()) {
		if (loadedAtTop() && loadedAtBottom()) {
			if (insertJoinedMessage(createUnread)) {
				if (!_joinedMessage->detached()) {
					setLastMessage(_joinedMessage);
				}
			}
			return;
		}
	}

	QDateTime inviteDate = peer->asChannel()->inviteDate;
	QDateTime firstDate, lastDate;
	for (int blockIndex = 0, blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) {
		HistoryBlock *block = blocks.at(blockIndex);
		int itemIndex = 0, itemsCount = block->items.size();
		for (; itemIndex < itemsCount; ++itemIndex) {
			HistoryItem *item = block->items.at(itemIndex);
			HistoryItemType type = item->type();
			if (type == HistoryItemMsg) {
				firstDate = item->date;
				break;
			}
		}
		if (itemIndex < itemsCount) break;
	}
	for (int blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		int itemIndex = block->items.size();
		for (; itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			HistoryItemType type = item->type();
			if (type == HistoryItemMsg) {
				lastDate = item->date;
				++itemIndex;
				break;
			}
		}
		if (itemIndex) break;
	}

	if (!firstDate.isNull() && !lastDate.isNull() && (firstDate <= inviteDate || loadedAtTop()) && (lastDate > inviteDate || loadedAtBottom())) {
		bool willBeLastMsg = (inviteDate >= lastDate);
		if (insertJoinedMessage(createUnread && willBeLastMsg) && willBeLastMsg) {
			if (!_joinedMessage->detached()) {
				setLastMessage(_joinedMessage);
			}
		}
	}
}

void ChannelHistory::checkMaxReadMessageDate() {
	if (_maxReadMessageDate.isValid()) return;

	for (int blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if (!item->unread()) {
				_maxReadMessageDate = item->date;
				if (item->isGroupMigrate() && isMegagroup() && peer->migrateFrom()) {
					_maxReadMessageDate = date(MTP_int(peer->asChannel()->date + 1)); // no report spam panel
				}
				return;
			}
		}
	}
	if (loadedAtTop() && (!isMegagroup() || !isEmpty())) {
		_maxReadMessageDate = date(MTP_int(peer->asChannel()->date));
	}
}

const QDateTime &ChannelHistory::maxReadMessageDate() {
	return _maxReadMessageDate;
}

HistoryItem *ChannelHistory::addNewChannelMessage(const MTPMessage &msg, NewMessageType type) {
	if (type == NewMessageExisting) return addToHistory(msg);

	return addNewToBlocks(msg, type);
}

HistoryItem *ChannelHistory::addNewToBlocks(const MTPMessage &msg, NewMessageType type) {
	if (!loadedAtBottom()) {
		HistoryItem *item = addToHistory(msg);
		if (item) {
			setLastMessage(item);
			if (type == NewMessageUnread) {
				newItemAdded(item);
			}
		}
		return item;
	}

	return addNewToLastBlock(msg, type);
}

void ChannelHistory::cleared(bool leaveItems) {
	_joinedMessage = nullptr;
}

HistoryItem *ChannelHistory::findPrevItem(HistoryItem *item) const {
	if (item->detached()) return nullptr;

	int itemIndex = item->indexInBlock();
	int blockIndex = item->block()->indexInHistory();
	for (++blockIndex, ++itemIndex; blockIndex > 0;) {
		--blockIndex;
		HistoryBlock *block = blocks.at(blockIndex);
		if (!itemIndex) itemIndex = block->items.size();
		for (; itemIndex > 0;) {
			--itemIndex;
			if (block->items.at(itemIndex)->type() == HistoryItemMsg) {
				return block->items.at(itemIndex);
			}
		}
	}
	return nullptr;
}

void ChannelHistory::messageDetached(HistoryItem *msg) {
	if (_joinedMessage == msg) {
		_joinedMessage = nullptr;
	}
}

ChannelHistory::~ChannelHistory() {
	// all items must be destroyed before ChannelHistory is destroyed
	// or they will call history()->asChannelHistory() -> undefined behaviour
	clearOnDestroy();
}

History *Histories::find(const PeerId &peerId) {
	Map::const_iterator i = map.constFind(peerId);
	return (i == map.cend()) ? 0 : i.value();
}

History *Histories::findOrInsert(const PeerId &peerId) {
	auto i = map.constFind(peerId);
	if (i == map.cend()) {
		auto history = peerIsChannel(peerId) ? static_cast<History*>(new ChannelHistory(peerId)) : (new History(peerId));
		i = map.insert(peerId, history);
	}
	return i.value();
}

History *Histories::findOrInsert(const PeerId &peerId, int32 unreadCount, int32 maxInboxRead, int32 maxOutboxRead) {
	auto i = map.constFind(peerId);
	if (i == map.cend()) {
		auto history = peerIsChannel(peerId) ? static_cast<History*>(new ChannelHistory(peerId)) : (new History(peerId));
		i = map.insert(peerId, history);
		history->setUnreadCount(unreadCount);
		history->inboxReadBefore = maxInboxRead + 1;
		history->outboxReadBefore = maxOutboxRead + 1;
	} else {
		auto history = i.value();
		if (unreadCount > history->unreadCount()) {
			history->setUnreadCount(unreadCount);
		}
		accumulate_max(history->inboxReadBefore, maxInboxRead + 1);
		accumulate_max(history->outboxReadBefore, maxOutboxRead + 1);
	}
	return i.value();
}

void Histories::clear() {
	App::historyClearMsgs();

	Map temp;
	std::swap(temp, map);
	for_const (auto history, temp) {
		delete history;
	}

	_unreadFull = _unreadMuted = 0;
	Notify::unreadCounterUpdated();
	App::historyClearItems();
	typing.clear();
}

void Histories::regSendAction(History *history, UserData *user, const MTPSendMessageAction &action, TimeId when) {
	if (action.type() == mtpc_sendMessageCancelAction) {
		history->unregTyping(user);
		return;
	} else if (action.type() == mtpc_sendMessageGameStopAction) {
		auto it = history->sendActions.find(user);
		if (it != history->sendActions.end() && it->type == SendActionPlayGame) {
			history->unregTyping(user);
		}
		return;
	}

	uint64 ms = getms();
	switch (action.type()) {
	case mtpc_sendMessageTypingAction: history->typing[user] = ms + 6000; break;
	case mtpc_sendMessageRecordVideoAction: history->sendActions.insert(user, SendAction(SendActionRecordVideo, ms + kStatusShowClientsideRecordVideo)); break;
	case mtpc_sendMessageUploadVideoAction: history->sendActions.insert(user, SendAction(SendActionUploadVideo, ms + kStatusShowClientsideUploadVideo, action.c_sendMessageUploadVideoAction().vprogress.v)); break;
	case mtpc_sendMessageRecordAudioAction: history->sendActions.insert(user, SendAction(SendActionRecordVoice, ms + kStatusShowClientsideRecordVoice)); break;
	case mtpc_sendMessageUploadAudioAction: history->sendActions.insert(user, SendAction(SendActionUploadVoice, ms + kStatusShowClientsideUploadVoice, action.c_sendMessageUploadAudioAction().vprogress.v)); break;
	case mtpc_sendMessageUploadPhotoAction: history->sendActions.insert(user, SendAction(SendActionUploadPhoto, ms + kStatusShowClientsideUploadPhoto, action.c_sendMessageUploadPhotoAction().vprogress.v)); break;
	case mtpc_sendMessageUploadDocumentAction: history->sendActions.insert(user, SendAction(SendActionUploadFile, ms + kStatusShowClientsideUploadFile, action.c_sendMessageUploadDocumentAction().vprogress.v)); break;
	case mtpc_sendMessageGeoLocationAction: history->sendActions.insert(user, SendAction(SendActionChooseLocation, ms + kStatusShowClientsideChooseLocation)); break;
	case mtpc_sendMessageChooseContactAction: history->sendActions.insert(user, SendAction(SendActionChooseContact, ms + kStatusShowClientsideChooseContact)); break;
	case mtpc_sendMessageGamePlayAction: {
		auto it = history->sendActions.find(user);
		if (it == history->sendActions.end() || it->type == SendActionPlayGame || it->until <= ms) {
			history->sendActions.insert(user, SendAction(SendActionPlayGame, ms + kStatusShowClientsidePlayGame));
		}
	} break;
	default: return;
	}

	user->madeAction(when);

	auto i = typing.find(history);
	if (i == typing.cend()) {
		typing.insert(history, ms);
		history->typingDots = 0;
		_a_typings.start();
	}
	history->updateTyping(ms, true);
}

void Histories::step_typings(uint64 ms, bool timer) {
	for (TypingHistories::iterator i = typing.begin(), e = typing.end(); i != e;) {
		i.key()->typingDots = (ms - i.value()) / 150;
		i.key()->updateTyping(ms);
		if (i.key()->typing.isEmpty() && i.key()->sendActions.isEmpty()) {
			i = typing.erase(i);
		} else {
			++i;
		}
	}
	if (typing.isEmpty()) {
		_a_typings.stop();
	}
}

void Histories::remove(const PeerId &peer) {
	Map::iterator i = map.find(peer);
	if (i != map.cend()) {
		typing.remove(i.value());
		delete i.value();
		map.erase(i);
	}
}

namespace {

void checkForSwitchInlineButton(HistoryItem *item) {
	if (item->out() || !item->hasSwitchInlineButton()) {
		return;
	}
	if (UserData *user = item->history()->peer->asUser()) {
		if (!user->botInfo || !user->botInfo->inlineReturnPeerId) {
			return;
		}
		if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			for_const (auto &row, markup->rows) {
				for_const (auto &button, row) {
					if (button.type == HistoryMessageReplyMarkup::Button::Type::SwitchInline) {
						Notify::switchInlineBotButtonReceived(QString::fromUtf8(button.data));
						return;
					}
				}
			}
		}
	}
}

} // namespace

HistoryItem *Histories::addNewMessage(const MTPMessage &msg, NewMessageType type) {
	auto peer = peerFromMessage(msg);
	if (!peer) return nullptr;

	auto result = App::history(peer)->addNewMessage(msg, type);
	if (result && type == NewMessageUnread) {
		checkForSwitchInlineButton(result);
	}
	return result;
}

int Histories::unreadBadge() const {
	return _unreadFull - (Global::IncludeMuted() ? 0 : _unreadMuted);
}

bool Histories::unreadOnlyMuted() const {
	return Global::IncludeMuted() ? (_unreadMuted >= _unreadFull) : false;
}

HistoryItem *History::createItem(const MTPMessage &msg, bool applyServiceAction, bool detachExistingItem) {
	MsgId msgId = 0;
	switch (msg.type()) {
	case mtpc_messageEmpty: msgId = msg.c_messageEmpty().vid.v; break;
	case mtpc_message: msgId = msg.c_message().vid.v; break;
	case mtpc_messageService: msgId = msg.c_messageService().vid.v; break;
	}
	if (!msgId) return nullptr;

	HistoryItem *result = App::histItemById(channelId(), msgId);
	if (result) {
		if (!result->detached() && detachExistingItem) {
			result->detach();
		}
		if (msg.type() == mtpc_message) {
			result->updateMedia(msg.c_message().has_media() ? (&msg.c_message().vmedia) : 0);
			if (applyServiceAction) {
				App::checkSavedGif(result);
			}
		}
		return result;
	}

	switch (msg.type()) {
	case mtpc_messageEmpty:
		result = HistoryService::create(this, msg.c_messageEmpty().vid.v, date(), lang(lng_message_empty));
	break;

	case mtpc_message: {
		const auto &m(msg.c_message());
		int badMedia = 0; // 1 - unsupported, 2 - empty
		if (m.has_media()) switch (m.vmedia.type()) {
		case mtpc_messageMediaEmpty:
		case mtpc_messageMediaContact: break;
		case mtpc_messageMediaGeo:
			switch (m.vmedia.c_messageMediaGeo().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaVenue:
			switch (m.vmedia.c_messageMediaVenue().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaPhoto:
			switch (m.vmedia.c_messageMediaPhoto().vphoto.type()) {
			case mtpc_photo: break;
			case mtpc_photoEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaDocument:
			switch (m.vmedia.c_messageMediaDocument().vdocument.type()) {
			case mtpc_document: break;
			case mtpc_documentEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaWebPage:
			switch (m.vmedia.c_messageMediaWebPage().vwebpage.type()) {
			case mtpc_webPage:
			case mtpc_webPageEmpty:
			case mtpc_webPagePending: break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaGame:
		switch (m.vmedia.c_messageMediaGame().vgame.type()) {
			case mtpc_game: break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaUnsupported:
		default: badMedia = 1; break;
		}
		if (badMedia == 1) {
			QString text(lng_message_unsupported(lt_link, qsl("https://desktop.telegram.org")));
			EntitiesInText entities;
			textParseEntities(text, _historyTextNoMonoOptions.flags, &entities);
			entities.push_front(EntityInText(EntityInTextItalic, 0, text.size()));
			result = HistoryMessage::create(this, m.vid.v, m.vflags.v, m.vreply_to_msg_id.v, m.vvia_bot_id.v, date(m.vdate), m.vfrom_id.v, { text, entities });
		} else if (badMedia) {
			result = HistoryService::create(this, m.vid.v, date(m.vdate), lang(lng_message_empty), m.vflags.v, m.has_from_id() ? m.vfrom_id.v : 0);
		} else {
			result = HistoryMessage::create(this, m);
		}
	} break;

	case mtpc_messageService: {
		const auto &d(msg.c_messageService());
		result = HistoryService::create(this, d);

		if (applyServiceAction) {
			const auto &action(d.vaction);
			switch (d.vaction.type()) {
			case mtpc_messageActionChatAddUser: {
				const auto &d(action.c_messageActionChatAddUser());
				if (peer->isMegagroup()) {
					const auto &v(d.vusers.c_vector().v);
					for (int32 i = 0, l = v.size(); i < l; ++i) {
						if (UserData *user = App::userLoaded(peerFromUser(v.at(i)))) {
							if (peer->asChannel()->mgInfo->lastParticipants.indexOf(user) < 0) {
								peer->asChannel()->mgInfo->lastParticipants.push_front(user);
								peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
								Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
							}
							if (user->botInfo) {
								peer->asChannel()->mgInfo->bots.insert(user);
								if (peer->asChannel()->mgInfo->botStatus != 0 && peer->asChannel()->mgInfo->botStatus < 2) {
									peer->asChannel()->mgInfo->botStatus = 2;
								}
							}
						}
					}
				}
			} break;

			case mtpc_messageActionChatJoinedByLink: {
				const auto &d(action.c_messageActionChatJoinedByLink());
				if (peer->isMegagroup()) {
					if (result->from()->isUser()) {
						if (peer->asChannel()->mgInfo->lastParticipants.indexOf(result->from()->asUser()) < 0) {
							peer->asChannel()->mgInfo->lastParticipants.push_front(result->from()->asUser());
							Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
						}
						if (result->from()->asUser()->botInfo) {
							peer->asChannel()->mgInfo->bots.insert(result->from()->asUser());
							if (peer->asChannel()->mgInfo->botStatus != 0 && peer->asChannel()->mgInfo->botStatus < 2) {
								peer->asChannel()->mgInfo->botStatus = 2;
							}
						}
					}
				}
			} break;

			case mtpc_messageActionChatDeletePhoto: {
				ChatData *chat = peer->asChat();
				if (chat) chat->setPhoto(MTP_chatPhotoEmpty());
			} break;

			case mtpc_messageActionChatDeleteUser: {
				const auto &d(action.c_messageActionChatDeleteUser());
				PeerId uid = peerFromUser(d.vuser_id);
				if (lastKeyboardFrom == uid) {
					clearLastKeyboard();
				}
				if (peer->isMegagroup()) {
					if (auto user = App::userLoaded(uid)) {
						auto channel = peer->asChannel();
						auto megagroupInfo = channel->mgInfo;

						int32 index = megagroupInfo->lastParticipants.indexOf(user);
						if (index >= 0) {
							megagroupInfo->lastParticipants.removeAt(index);
							Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
						}
						if (peer->asChannel()->membersCount() > 1) {
							peer->asChannel()->setMembersCount(channel->membersCount() - 1);
						} else {
							megagroupInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
							megagroupInfo->lastParticipantsCount = 0;
						}
						if (megagroupInfo->lastAdmins.contains(user)) {
							megagroupInfo->lastAdmins.remove(user);
							if (channel->adminsCount() > 1) {
								channel->setAdminsCount(channel->adminsCount() - 1);
							}
							Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::AdminsChanged);
						}
						megagroupInfo->bots.remove(user);
						if (megagroupInfo->bots.isEmpty() && megagroupInfo->botStatus > 0) {
							megagroupInfo->botStatus = -1;
						}
					}
				}
			} break;

			case mtpc_messageActionChatEditPhoto: {
				const auto &d(action.c_messageActionChatEditPhoto());
				if (d.vphoto.type() == mtpc_photo) {
					const auto &sizes(d.vphoto.c_photo().vsizes.c_vector().v);
					if (!sizes.isEmpty()) {
						PhotoData *photo = App::feedPhoto(d.vphoto.c_photo());
						if (photo) photo->peer = peer;
						const auto &smallSize(sizes.front()), &bigSize(sizes.back());
						const MTPFileLocation *smallLoc = 0, *bigLoc = 0;
						switch (smallSize.type()) {
						case mtpc_photoSize: smallLoc = &smallSize.c_photoSize().vlocation; break;
						case mtpc_photoCachedSize: smallLoc = &smallSize.c_photoCachedSize().vlocation; break;
						}
						switch (bigSize.type()) {
						case mtpc_photoSize: bigLoc = &bigSize.c_photoSize().vlocation; break;
						case mtpc_photoCachedSize: bigLoc = &bigSize.c_photoCachedSize().vlocation; break;
						}
						if (smallLoc && bigLoc) {
							if (peer->isChat()) {
								peer->asChat()->setPhoto(MTP_chatPhoto(*smallLoc, *bigLoc), photo ? photo->id : 0);
							} else if (peer->isChannel()) {
								peer->asChannel()->setPhoto(MTP_chatPhoto(*smallLoc, *bigLoc), photo ? photo->id : 0);
							}
							peer->loadUserpic();
						}
					}
				}
			} break;

			case mtpc_messageActionChatEditTitle: {
				auto &d(action.c_messageActionChatEditTitle());
				if (auto chat = peer->asChat()) {
					chat->setName(qs(d.vtitle));
				}
			} break;

			case mtpc_messageActionChatMigrateTo: {
				peer->asChat()->flags |= MTPDchat::Flag::f_deactivated;

				//const auto &d(action.c_messageActionChatMigrateTo());
				//PeerData *channel = App::channelLoaded(d.vchannel_id.v);
			} break;

			case mtpc_messageActionChannelMigrateFrom: {
				//const auto &d(action.c_messageActionChannelMigrateFrom());
				//PeerData *chat = App::chatLoaded(d.vchat_id.v);
			} break;

			case mtpc_messageActionPinMessage: {
				if (d.has_reply_to_msg_id() && result && result->history()->peer->isMegagroup()) {
					result->history()->peer->asChannel()->mgInfo->pinnedMsgId = d.vreply_to_msg_id.v;
					if (App::main()) emit App::main()->peerUpdated(result->history()->peer);
				}
			} break;
			}
		}
	} break;
	}

	if (applyServiceAction) {
		App::checkSavedGif(result);
	}

	return result;
}

HistoryItem *History::createItemForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *msg) {
	return HistoryMessage::create(this, id, flags, date, from, msg);
}

HistoryItem *History::createItemDocument(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup) {
	return HistoryMessage::create(this, id, flags, replyTo, viaBotId, date, from, doc, caption, markup);
}

HistoryItem *History::createItemPhoto(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup) {
	return HistoryMessage::create(this, id, flags, replyTo, viaBotId, date, from, photo, caption, markup);
}

HistoryItem *History::createItemGame(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, GameData *game, const MTPReplyMarkup &markup) {
	return HistoryMessage::create(this, id, flags, replyTo, viaBotId, date, from, game, markup);
}

HistoryItem *History::addNewService(MsgId msgId, QDateTime date, const QString &text, MTPDmessage::Flags flags, bool newMsg) {
	return addNewItem(HistoryService::create(this, msgId, date, text, flags), newMsg);
}

HistoryItem *History::addNewMessage(const MTPMessage &msg, NewMessageType type) {
	if (isChannel()) return asChannelHistory()->addNewChannelMessage(msg, type);

	if (type == NewMessageExisting) return addToHistory(msg);
	if (!loadedAtBottom() || peer->migrateTo()) {
		HistoryItem *item = addToHistory(msg);
		if (item) {
			setLastMessage(item);
			if (type == NewMessageUnread) {
				newItemAdded(item);
			}
		}
		return item;
	}

	return addNewToLastBlock(msg, type);
}

HistoryItem *History::addNewToLastBlock(const MTPMessage &msg, NewMessageType type) {
	auto applyServiceAction = (type == NewMessageUnread);
	auto detachExistingItem = (type != NewMessageLast);
	auto item = createItem(msg, applyServiceAction, detachExistingItem);
	if (!item || !item->detached()) {
		return item;
	}
	return addNewItem(item, (type == NewMessageUnread));
}

HistoryItem *History::addToHistory(const MTPMessage &msg) {
	return createItem(msg, false, false);
}

HistoryItem *History::addNewForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *item) {
	return addNewItem(createItemForwarded(id, flags, date, from, item), true);
}

HistoryItem *History::addNewDocument(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup) {
	return addNewItem(createItemDocument(id, flags, viaBotId, replyTo, date, from, doc, caption, markup), true);
}

HistoryItem *History::addNewPhoto(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup) {
	return addNewItem(createItemPhoto(id, flags, viaBotId, replyTo, date, from, photo, caption, markup), true);
}

HistoryItem *History::addNewGame(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, GameData *game, const MTPReplyMarkup &markup) {
	return addNewItem(createItemGame(id, flags, viaBotId, replyTo, date, from, game, markup), true);
}

bool History::addToOverview(MediaOverviewType type, MsgId msgId, AddToOverviewMethod method) {
	bool adding = false;
	switch (method) {
	case AddToOverviewNew:
	case AddToOverviewFront: adding = (overviewIds[type].constFind(msgId) == overviewIds[type].cend()); break;
	case AddToOverviewBack: adding = (overviewCountData[type] != 0); break;
	}
	if (!adding) return false;

	overviewIds[type].insert(msgId);
	switch (method) {
	case AddToOverviewNew:
	case AddToOverviewBack: overview[type].push_back(msgId); break;
	case AddToOverviewFront: overview[type].push_front(msgId); break;
	}
	if (method == AddToOverviewNew) {
		if (overviewCountData[type] > 0) {
			++overviewCountData[type];
		}
		if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer, type);
	}
	return true;
}

void History::eraseFromOverview(MediaOverviewType type, MsgId msgId) {
	if (overviewIds[type].isEmpty()) return;

	auto i = overviewIds[type].find(msgId);
	if (i == overviewIds[type].cend()) return;

	overviewIds[type].erase(i);
	for (auto i = overview[type].begin(), e = overview[type].end(); i != e; ++i) {
		if ((*i) == msgId) {
			overview[type].erase(i);
			if (overviewCountData[type] > 0) {
				--overviewCountData[type];
			}
			break;
		}
	}
	if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer, type);
}

HistoryItem *History::addNewItem(HistoryItem *adding, bool newMsg) {
	t_assert(!isBuildingFrontBlock());
	addItemToBlock(adding);

	setLastMessage(adding);
	if (newMsg) {
		newItemAdded(adding);
	}

	adding->addToOverview(AddToOverviewNew);
	if (adding->from()->id) {
		if (adding->from()->isUser()) {
			QList<UserData*> *lastAuthors = 0;
			if (peer->isChat()) {
				lastAuthors = &peer->asChat()->lastAuthors;
			} else if (peer->isMegagroup()) {
				lastAuthors = &peer->asChannel()->mgInfo->lastParticipants;
				if (adding->from()->asUser()->botInfo) {
					peer->asChannel()->mgInfo->bots.insert(adding->from()->asUser());
					if (peer->asChannel()->mgInfo->botStatus != 0 && peer->asChannel()->mgInfo->botStatus < 2) {
						peer->asChannel()->mgInfo->botStatus = 2;
					}
				}
			}
			if (lastAuthors) {
				int prev = lastAuthors->indexOf(adding->from()->asUser());
				if (prev > 0) {
					lastAuthors->removeAt(prev);
				} else if (prev < 0 && peer->isMegagroup()) { // nothing is outdated if just reordering
					peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
				}
				if (prev) {
					lastAuthors->push_front(adding->from()->asUser());
				}
				if (peer->isMegagroup()) {
					Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
				}
			}
		}
		if (adding->definesReplyKeyboard()) {
			MTPDreplyKeyboardMarkup::Flags markupFlags = adding->replyKeyboardFlags();
			if (!(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_selective) || adding->mentionsMe()) {
				OrderedSet<PeerData*> *markupSenders = 0;
				if (peer->isChat()) {
					markupSenders = &peer->asChat()->markupSenders;
				} else if (peer->isMegagroup()) {
					markupSenders = &peer->asChannel()->mgInfo->markupSenders;
				}
				if (markupSenders) {
					markupSenders->insert(adding->from());
				}
				if (markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_zero) { // zero markup means replyKeyboardHide
					if (lastKeyboardFrom == adding->from()->id || (!lastKeyboardInited && !peer->isChat() && !peer->isMegagroup() && !adding->out())) {
						clearLastKeyboard();
					}
				} else {
					bool botNotInChat = false;
					if (peer->isChat()) {
						botNotInChat = adding->from()->isUser() && (!peer->canWrite() || !peer->asChat()->participants.isEmpty()) && !peer->asChat()->participants.contains(adding->from()->asUser());
					} else if (peer->isMegagroup()) {
						botNotInChat = adding->from()->isUser() && (!peer->canWrite() || peer->asChannel()->mgInfo->botStatus != 0) && !peer->asChannel()->mgInfo->bots.contains(adding->from()->asUser());
					}
					if (botNotInChat) {
						clearLastKeyboard();
					} else {
						lastKeyboardInited = true;
						lastKeyboardId = adding->id;
						lastKeyboardFrom = adding->from()->id;
						lastKeyboardUsed = false;
					}
				}
			}
		}
	}

	return adding;
}

void History::unregTyping(UserData *from) {
	uint64 updateAtMs = 0;
	auto i = typing.find(from);
	if (i != typing.cend()) {
		updateAtMs = getms();
		i.value() = updateAtMs;
	}
	auto j = sendActions.find(from);
	if (j != sendActions.cend()) {
		if (!updateAtMs) updateAtMs = getms();
		j.value().until = updateAtMs;
	}
	if (updateAtMs) {
		updateTyping(updateAtMs, true);
	}
}

void History::newItemAdded(HistoryItem *item) {
	App::checkImageCacheSize();
	if (item->from() && item->from()->isUser()) {
		if (item->from() == item->author()) {
			unregTyping(item->from()->asUser());
		}
		MTPint itemServerTime;
		toServerTime(item->date.toTime_t(), itemServerTime);
		item->from()->asUser()->madeAction(itemServerTime.v);
	}
	if (item->out()) {
		if (unreadBar) unreadBar->destroyUnreadBar();
		if (!item->unread()) {
			outboxRead(item);
		}
	} else if (item->unread()) {
		if (!isChannel() || peer->asChannel()->amIn()) {
			notifies.push_back(item);
			App::main()->newUnreadMsg(this, item);
		}
	} else if (!item->isGroupMigrate() || !peer->isMegagroup()) {
		inboxRead(item);
	}
}

HistoryBlock *History::prepareBlockForAddingItem() {
	if (isBuildingFrontBlock()) {
		if (_buildingFrontBlock->block) {
			return _buildingFrontBlock->block;
		}

		auto result = _buildingFrontBlock->block = new HistoryBlock(this);
		if (_buildingFrontBlock->expectedItemsCount > 0) {
			result->items.reserve(_buildingFrontBlock->expectedItemsCount + 1);
		}
		result->setIndexInHistory(0);
		blocks.push_front(result);
		for (int i = 1, l = blocks.size(); i < l; ++i) {
			blocks.at(i)->setIndexInHistory(i);
		}
		return result;
	}

	bool addNewBlock = blocks.isEmpty() || (blocks.back()->items.size() >= MessagesPerPage);
	if (!addNewBlock) {
		return blocks.back();
	}

	auto result = new HistoryBlock(this);
	result->setIndexInHistory(blocks.size());
	blocks.push_back(result);

	result->items.reserve(MessagesPerPage);
	return result;
};

void History::addItemToBlock(HistoryItem *item) {
	t_assert(item != nullptr);
	t_assert(item->detached());

	auto block = prepareBlockForAddingItem();

	item->attachToBlock(block, block->items.size());
	block->items.push_back(item);
	item->previousItemChanged();

	if (isBuildingFrontBlock() && _buildingFrontBlock->expectedItemsCount > 0) {
		--_buildingFrontBlock->expectedItemsCount;
	}
}

void History::addOlderSlice(const QVector<MTPMessage> &slice) {
	if (slice.isEmpty()) {
		oldLoaded = true;
		if (isChannel()) {
			asChannelHistory()->checkJoinedMessage();
			asChannelHistory()->checkMaxReadMessageDate();
		}
		return;
	}

	startBuildingFrontBlock(slice.size());

	for (auto i = slice.cend(), e = slice.cbegin(); i != e;) {
		--i;
		auto adding = createItem(*i, false, true);
		if (!adding) continue;

		addItemToBlock(adding);
	}

	auto block = finishBuildingFrontBlock();
	if (!block) {
		// If no items were added it means we've loaded everything old.
		oldLoaded = true;
	} else if (loadedAtBottom()) { // add photos to overview and authors to lastAuthors / lastParticipants
		bool channel = isChannel();
		int32 mask = 0;
		QList<UserData*> *lastAuthors = nullptr;
		OrderedSet<PeerData*> *markupSenders = nullptr;
		if (peer->isChat()) {
			lastAuthors = &peer->asChat()->lastAuthors;
			markupSenders = &peer->asChat()->markupSenders;
		} else if (peer->isMegagroup()) {
			lastAuthors = &peer->asChannel()->mgInfo->lastParticipants;
			markupSenders = &peer->asChannel()->mgInfo->markupSenders;
		}
		for (int32 i = block->items.size(); i > 0; --i) {
			auto item = block->items[i - 1];
			mask |= item->addToOverview(AddToOverviewFront);
			if (item->from()->id) {
				if (lastAuthors) { // chats
					if (item->from()->isUser()) {
						if (!lastAuthors->contains(item->from()->asUser())) {
							lastAuthors->push_back(item->from()->asUser());
							if (peer->isMegagroup()) {
								peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
								Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
							}
						}
					}
				}
			}
			if (item->author()->id) {
				if (markupSenders) { // chats with bots
					if (!lastKeyboardInited && item->definesReplyKeyboard() && !item->out()) {
						auto markupFlags = item->replyKeyboardFlags();
						if (!(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_selective) || item->mentionsMe()) {
							bool wasKeyboardHide = markupSenders->contains(item->author());
							if (!wasKeyboardHide) {
								markupSenders->insert(item->author());
							}
							if (!(markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_zero)) {
								if (!lastKeyboardInited) {
									bool botNotInChat = false;
									if (peer->isChat()) {
										botNotInChat = (!peer->canWrite() || !peer->asChat()->participants.isEmpty()) && item->author()->isUser() && !peer->asChat()->participants.contains(item->author()->asUser());
									} else if (peer->isMegagroup()) {
										botNotInChat = (!peer->canWrite() || peer->asChannel()->mgInfo->botStatus != 0) && item->author()->isUser() && !peer->asChannel()->mgInfo->bots.contains(item->author()->asUser());
									}
									if (wasKeyboardHide || botNotInChat) {
										clearLastKeyboard();
									} else {
										lastKeyboardInited = true;
										lastKeyboardId = item->id;
										lastKeyboardFrom = item->author()->id;
										lastKeyboardUsed = false;
									}
								}
							}
						}
					}
				} else if (!lastKeyboardInited && item->definesReplyKeyboard() && !item->out()) { // conversations with bots
					MTPDreplyKeyboardMarkup::Flags markupFlags = item->replyKeyboardFlags();
					if (!(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_selective) || item->mentionsMe()) {
						if (markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_zero) {
							clearLastKeyboard();
						} else {
							lastKeyboardInited = true;
							lastKeyboardId = item->id;
							lastKeyboardFrom = item->author()->id;
							lastKeyboardUsed = false;
						}
					}
				}
			}
		}
		for (int32 t = 0; t < OverviewCount; ++t) {
			if ((mask & (1 << t)) && App::wnd()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(t));
		}
	}

	if (isChannel()) {
		asChannelHistory()->checkJoinedMessage();
		asChannelHistory()->checkMaxReadMessageDate();
	}
	checkLastMsg();
}

void History::addNewerSlice(const QVector<MTPMessage> &slice) {
	bool wasEmpty = isEmpty(), wasLoadedAtBottom = loadedAtBottom();

	if (slice.isEmpty()) {
		newLoaded = true;
		if (!lastMsg) {
			setLastMessage(lastImportantMessage());
		}
	}

	t_assert(!isBuildingFrontBlock());
	if (!slice.isEmpty()) {
		bool atLeastOneAdded = false;
		for (auto i = slice.cend(), e = slice.cbegin(); i != e;) {
			--i;
			auto adding = createItem(*i, false, true);
			if (!adding) continue;

			addItemToBlock(adding);
			atLeastOneAdded = true;
		}

		if (!atLeastOneAdded) {
			newLoaded = true;
			setLastMessage(lastImportantMessage());
		}
	}

	if (!wasLoadedAtBottom) {
		checkAddAllToOverview();
	}

	if (isChannel()) asChannelHistory()->checkJoinedMessage();
	checkLastMsg();
}

void History::checkLastMsg() {
	if (lastMsg) {
		if (!newLoaded && !lastMsg->detached()) {
			newLoaded = true;
			checkAddAllToOverview();
		}
	} else if (newLoaded) {
		setLastMessage(lastImportantMessage());
	}
}

void History::checkAddAllToOverview() {
	if (!loadedAtBottom()) {
		return;
	}

	int32 mask = 0;
	for (int32 i = 0; i < OverviewCount; ++i) {
		if (overviewCountData[i] == 0) continue; // all loaded
		if (!overview[i].isEmpty() || !overviewIds[i].isEmpty()) {
			overview[i].clear();
			overviewIds[i].clear();
			mask |= (1 << i);
		}
	}
	for_const (HistoryBlock *block, blocks) {
		for_const (HistoryItem *item, block->items) {
			mask |= item->addToOverview(AddToOverviewBack);
		}
	}
	for (int32 t = 0; t < OverviewCount; ++t) {
		if ((mask & (1 << t)) && App::wnd()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(t));
	}
}

int History::countUnread(MsgId upTo) {
	int result = 0;
	for (auto i = blocks.cend(), e = blocks.cbegin(); i != e;) {
		--i;
		for (auto j = (*i)->items.cend(), en = (*i)->items.cbegin(); j != en;) {
			--j;
			if ((*j)->id > 0 && (*j)->id <= upTo) {
				break;
			} else if (!(*j)->out() && (*j)->unread() && (*j)->id > upTo) {
				++result;
			}
		}
	}
	return result;
}

void History::updateShowFrom() {
	if (showFrom) return;

	for (auto i = blocks.cend(); i != blocks.cbegin();) {
		--i;
		for (auto j = (*i)->items.cend(); j != (*i)->items.cbegin();) {
			--j;
			if ((*j)->type() == HistoryItemMsg && (*j)->id > 0 && (!(*j)->out() || !showFrom)) {
				if ((*j)->id >= inboxReadBefore) {
					showFrom = *j;
				} else {
					return;
				}
			}
		}
	}
}

MsgId History::inboxRead(MsgId upTo) {
	if (upTo < 0) return upTo;
	if (unreadCount()) {
		if (upTo && loadedAtBottom()) App::main()->historyToDown(this);
		setUnreadCount(upTo ? countUnread(upTo) : 0);
	}

	if (!upTo) upTo = msgIdForRead();
	accumulate_max(inboxReadBefore, upTo + 1);

	updateChatListEntry();
	if (peer->migrateTo()) {
		if (auto migrateTo = App::historyLoaded(peer->migrateTo()->id)) {
			migrateTo->updateChatListEntry();
		}
	}

	showFrom = nullptr;
	App::wnd()->notifyClear(this);

	return upTo;
}

MsgId History::inboxRead(HistoryItem *wasRead) {
	return inboxRead(wasRead ? wasRead->id : 0);
}

MsgId History::outboxRead(int32 upTo) {
	if (upTo < 0) return upTo;
	if (!upTo) upTo = msgIdForRead();
	accumulate_max(outboxReadBefore, upTo + 1);

	return upTo;
}

MsgId History::outboxRead(HistoryItem *wasRead) {
	return outboxRead(wasRead ? wasRead->id : 0);
}

HistoryItem *History::lastImportantMessage() const {
	if (isEmpty()) {
		return nullptr;
	}
	bool importantOnly = isChannel() && !isMegagroup();
	for (int blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if (item->type() == HistoryItemMsg) {
				return item;
			}
		}
	}
	return nullptr;
}

void History::setUnreadCount(int newUnreadCount) {
	if (_unreadCount != newUnreadCount) {
		if (newUnreadCount == 1) {
			if (loadedAtBottom()) showFrom = lastImportantMessage();
			inboxReadBefore = qMax(inboxReadBefore, msgIdForRead());
		} else if (!newUnreadCount) {
			showFrom = nullptr;
			inboxReadBefore = qMax(inboxReadBefore, msgIdForRead() + 1);
		} else {
			if (!showFrom && !unreadBar && loadedAtBottom()) updateShowFrom();
		}
		if (inChatList(Dialogs::Mode::All)) {
			App::histories().unreadIncrement(newUnreadCount - _unreadCount, mute());
			if (!mute() || Global::IncludeMuted()) {
				Notify::unreadCounterUpdated();
			}
		}
		_unreadCount = newUnreadCount;
		if (auto main = App::main()) {
			main->unreadCountChanged(this);
		}
		if (unreadBar) {
			int32 count = _unreadCount;
			if (peer->migrateTo()) {
				if (History *h = App::historyLoaded(peer->migrateTo()->id)) {
					count += h->unreadCount();
				}
			}
			if (count > 0) {
				unreadBar->setUnreadBarCount(count);
			} else {
				unreadBar->setUnreadBarFreezed();
			}
		}
	}
}

 void History::setMute(bool newMute) {
	if (_mute != newMute) {
		_mute = newMute;
		if (inChatList(Dialogs::Mode::All)) {
			if (_unreadCount) {
				App::histories().unreadMuteChanged(_unreadCount, newMute);
				Notify::unreadCounterUpdated();
			}
			Notify::historyMuteUpdated(this);
		}
		updateChatListEntry();
	}
}

void History::getNextShowFrom(HistoryBlock *block, int i) {
	if (i >= 0) {
		int l = block->items.size();
		for (++i; i < l; ++i) {
			if (block->items.at(i)->type() == HistoryItemMsg) {
				showFrom = block->items.at(i);
				return;
			}
		}
	}

	for (int j = block->indexInHistory() + 1, s = blocks.size(); j < s; ++j) {
		block = blocks.at(j);
		for_const (HistoryItem *item, block->items) {
			if (item->type() == HistoryItemMsg) {
				showFrom = item;
				return;
			}
		}
	}
	showFrom = nullptr;
}

void History::countScrollState(int top) {
	countScrollTopItem(top);
	if (scrollTopItem) {
		scrollTopOffset = (top - scrollTopItem->block()->y - scrollTopItem->y);
	}
}

void History::countScrollTopItem(int top) {
	if (isEmpty()) {
		forgetScrollState();
		return;
	}

	int itemIndex = 0, blockIndex = 0, itemTop = 0;
	if (scrollTopItem && !scrollTopItem->detached()) {
		itemIndex = scrollTopItem->indexInBlock();
		blockIndex = scrollTopItem->block()->indexInHistory();
		itemTop = blocks.at(blockIndex)->y + scrollTopItem->y;
	}
	if (itemTop > top) {
		// go backward through history while we don't find an item that starts above
		do {
			HistoryBlock *block = blocks.at(blockIndex);
			for (--itemIndex; itemIndex >= 0; --itemIndex) {
				HistoryItem *item = block->items.at(itemIndex);
				itemTop = block->y + item->y;
				if (itemTop <= top) {
					scrollTopItem = item;
					return;
				}
			}
			if (--blockIndex >= 0) {
				itemIndex = blocks.at(blockIndex)->items.size();
			} else {
				break;
			}
		} while (true);

		scrollTopItem = blocks.front()->items.front();
	} else {
		// go forward through history while we don't find the last item that starts above
		for (int blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) {
			HistoryBlock *block = blocks.at(blockIndex);
			for (int itemsCount = block->items.size(); itemIndex < itemsCount; ++itemIndex) {
				HistoryItem *item = block->items.at(itemIndex);
				itemTop = block->y + item->y;
				if (itemTop > top) {
					t_assert(itemIndex > 0 || blockIndex > 0);
					if (itemIndex > 0) {
						scrollTopItem = block->items.at(itemIndex - 1);
					} else {
						scrollTopItem = blocks.at(blockIndex - 1)->items.back();
					}
					return;
				}
			}
			itemIndex = 0;
		}
		scrollTopItem = blocks.back()->items.back();
	}
}

void History::getNextScrollTopItem(HistoryBlock *block, int32 i) {
	++i;
	if (i > 0 && i < block->items.size()) {
		scrollTopItem = block->items.at(i);
		return;
	}
	int j = block->indexInHistory() + 1;
	if (j > 0 && j < blocks.size()) {
		scrollTopItem = blocks.at(j)->items.front();
		return;
	}
	scrollTopItem = nullptr;
}

void History::addUnreadBar() {
	if (unreadBar || !showFrom || showFrom->detached() || !unreadCount()) return;

	int32 count = unreadCount();
	if (peer->migrateTo()) {
		if (History *h = App::historyLoaded(peer->migrateTo()->id)) {
			count += h->unreadCount();
		}
	}
	showFrom->setUnreadBarCount(count);
	unreadBar = showFrom;
}

void History::destroyUnreadBar() {
	if (unreadBar) {
		unreadBar->destroyUnreadBar();
	}
}

HistoryItem *History::addNewInTheMiddle(HistoryItem *newItem, int32 blockIndex, int32 itemIndex) {
	t_assert(blockIndex >= 0);
	t_assert(blockIndex < blocks.size());
	t_assert(itemIndex >= 0);
	t_assert(itemIndex <= blocks.at(blockIndex)->items.size());

	HistoryBlock *block = blocks.at(blockIndex);

	newItem->attachToBlock(block, itemIndex);
	block->items.insert(itemIndex, newItem);
	newItem->previousItemChanged();
	for (int i = itemIndex + 1, l = block->items.size(); i < l; ++i) {
		block->items.at(i)->setIndexInBlock(i);
	}
	if (itemIndex + 1 < block->items.size()) {
		block->items.at(itemIndex + 1)->previousItemChanged();
	}

	return newItem;
}

void History::startBuildingFrontBlock(int expectedItemsCount) {
	t_assert(!isBuildingFrontBlock());
	t_assert(expectedItemsCount > 0);

	_buildingFrontBlock.reset(new BuildingBlock());
	_buildingFrontBlock->expectedItemsCount = expectedItemsCount;
}

HistoryBlock *History::finishBuildingFrontBlock() {
	t_assert(isBuildingFrontBlock());

	// Some checks if there was some message history already
	HistoryBlock *block = _buildingFrontBlock->block;
	if (block && blocks.size() > 1) {
		HistoryItem *last = block->items.back(); // ... item, item, item, last ], [ first, item, item ...
		HistoryItem *first = blocks.at(1)->items.front();

		// we've added a new front block, so previous item for
		// the old first item of a first block was changed
		first->previousItemChanged();
	}

	_buildingFrontBlock = nullptr;
	return block;
}

void History::clearNotifications() {
	notifies.clear();
}

bool History::loadedAtBottom() const {
	return newLoaded;
}

bool History::loadedAtTop() const {
	return oldLoaded;
}

bool History::isReadyFor(MsgId msgId) {
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) { // old group history
		return App::history(peer->migrateFrom()->id)->isReadyFor(-msgId);
	}

	if (msgId == ShowAtTheEndMsgId) {
		return loadedAtBottom();
	}
	if (msgId == ShowAtUnreadMsgId) {
		if (peer->migrateFrom()) { // old group history
			if (History *h = App::historyLoaded(peer->migrateFrom()->id)) {
				if (h->unreadCount()) {
					return h->isReadyFor(msgId);
				}
			}
		}
		if (unreadCount()) {
			if (!isEmpty()) {
				return (loadedAtTop() || minMsgId() <= inboxReadBefore) && (loadedAtBottom() || maxMsgId() >= inboxReadBefore);
			}
			return false;
		}
		return loadedAtBottom();
	}
	HistoryItem *item = App::histItemById(channelId(), msgId);
	return item && (item->history() == this) && !item->detached();
}

void History::getReadyFor(MsgId msgId) {
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) {
		History *h = App::history(peer->migrateFrom()->id);
		h->getReadyFor(-msgId);
		if (h->isEmpty()) {
			clear(true);
		}
		return;
	}
	if (msgId == ShowAtUnreadMsgId && peer->migrateFrom()) {
		if (History *h = App::historyLoaded(peer->migrateFrom()->id)) {
			if (h->unreadCount()) {
				clear(true);
				h->getReadyFor(msgId);
				return;
			}
		}
	}
	if (!isReadyFor(msgId)) {
		clear(true);

		if (msgId == ShowAtTheEndMsgId) {
			newLoaded = true;
		}
	}
}

void History::setNotLoadedAtBottom() {
	newLoaded = false;
}

namespace {
	uint32 _dialogsPosToTopShift = 0x80000000UL;
}

inline uint64 dialogPosFromDate(const QDateTime &date) {
	if (date.isNull()) return 0;
	return (uint64(date.toTime_t()) << 32) | (++_dialogsPosToTopShift);
}

void History::setLastMessage(HistoryItem *msg) {
	if (msg) {
		if (!lastMsg) Local::removeSavedPeer(peer);
		lastMsg = msg;
		setChatsListDate(msg->date);
	} else {
		lastMsg = 0;
		updateChatListEntry();
	}
}

bool History::needUpdateInChatList() const {
	if (inChatList(Dialogs::Mode::All)) {
		return true;
	} else if (peer->migrateTo()) {
		return false;
	}
	return (!peer->isChannel() || peer->asChannel()->amIn());
}

void History::setChatsListDate(const QDateTime &date) {
	bool updateDialog = needUpdateInChatList();
	if (!lastMsgDate.isNull() && lastMsgDate >= date) {
		if (!updateDialog || !inChatList(Dialogs::Mode::All)) {
			return;
		}
	}
	lastMsgDate = date;
	updateChatListSortPosition();
}

void History::updateChatListSortPosition() {
	auto chatListDate = [this]() {
		if (auto draft = cloudDraft()) {
			if (!Data::draftIsNull(draft) && draft->date > lastMsgDate) {
				return draft->date;
			}
		}
		return lastMsgDate;
	};

	_sortKeyInChatList = dialogPosFromDate(chatListDate());
	if (auto m = App::main()) {
		if (needUpdateInChatList()) {
			if (_sortKeyInChatList) {
				m->createDialog(this);
				updateChatListEntry();
			} else {
				m->deleteConversation(peer, false);
			}
		}
	}
}

void History::fixLastMessage(bool wasAtBottom) {
	setLastMessage(wasAtBottom ? lastImportantMessage() : 0);
}

MsgId History::minMsgId() const {
	for_const (const HistoryBlock *block, blocks) {
		for_const (const HistoryItem *item, block->items) {
			if (item->id > 0) {
				return item->id;
			}
		}
	}
	return 0;
}

MsgId History::maxMsgId() const {
	for (auto i = blocks.cend(), e = blocks.cbegin(); i != e;) {
		--i;
		for (auto j = (*i)->items.cend(), en = (*i)->items.cbegin(); j != en;) {
			--j;
			if ((*j)->id > 0) {
				return (*j)->id;
			}
		}
	}
	return 0;
}

MsgId History::msgIdForRead() const {
	MsgId result = (lastMsg && lastMsg->id > 0) ? lastMsg->id : 0;
	if (loadedAtBottom()) result = qMax(result, maxMsgId());
	return result;
}

int History::resizeGetHeight(int newWidth) {
	bool resizeAllItems = (_flags & Flag::f_pending_resize) || (width != newWidth);

	if (!resizeAllItems && !hasPendingResizedItems()) {
		return height;
	}
	_flags &= ~(Flag::f_pending_resize | Flag::f_has_pending_resized_items);

	width = newWidth;
	int y = 0;
	for_const (HistoryBlock *block, blocks) {
		block->y = y;
		y += block->resizeGetHeight(newWidth, resizeAllItems);
	}
	height = y;
	return height;
}

ChannelHistory *History::asChannelHistory() {
	return isChannel() ? static_cast<ChannelHistory*>(this) : 0;
}

const ChannelHistory *History::asChannelHistory() const {
	return isChannel() ? static_cast<const ChannelHistory*>(this) : 0;
}

bool History::isDisplayedEmpty() const {
	return isEmpty() || ((blocks.size() == 1) && blocks.front()->items.size() == 1 && blocks.front()->items.front()->isEmpty());
}

void History::clear(bool leaveItems) {
	if (unreadBar) {
		unreadBar = nullptr;
	}
	if (showFrom) {
		showFrom = nullptr;
	}
	if (lastSentMsg) {
		lastSentMsg = nullptr;
	}
	if (scrollTopItem) {
		forgetScrollState();
	}
	if (!leaveItems) {
		setLastMessage(nullptr);
		notifies.clear();
		auto &pending = Global::RefPendingRepaintItems();
		for (auto i = pending.begin(); i != pending.end();) {
			if ((*i)->history() == this) {
				i = pending.erase(i);
			} else {
				++i;
			}
		}
	}
	for (int32 i = 0; i < OverviewCount; ++i) {
		if (!overview[i].isEmpty() || !overviewIds[i].isEmpty()) {
			if (leaveItems) {
				if (overviewCountData[i] == 0) {
					overviewCountData[i] = overview[i].size();
				}
			} else {
				overviewCountData[i] = -1; // not loaded yet
			}
			overview[i].clear();
			overviewIds[i].clear();
			if (App::wnd() && !App::quitting()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(i));
		}
	}
	clearBlocks(leaveItems);
	if (leaveItems) {
		lastKeyboardInited = false;
	} else {
		setUnreadCount(0);
		if (peer->isMegagroup()) {
			peer->asChannel()->mgInfo->pinnedMsgId = 0;
		}
		clearLastKeyboard();
	}
	setPendingResize();

	newLoaded = oldLoaded = false;
	forgetScrollState();

	if (peer->isChat()) {
		peer->asChat()->lastAuthors.clear();
		peer->asChat()->markupSenders.clear();
	} else if (isChannel()) {
		asChannelHistory()->cleared(leaveItems);
		if (isMegagroup()) {
			peer->asChannel()->mgInfo->markupSenders.clear();
		}
	}
	if (leaveItems && App::main()) App::main()->historyCleared(this);
}

void History::clearBlocks(bool leaveItems) {
	Blocks lst;
	std::swap(lst, blocks);
	for_const (HistoryBlock *block, lst) {
		if (leaveItems) {
			block->clear(true);
		}
		delete block;
	}
}

void History::clearOnDestroy() {
	clearBlocks(false);
}

History::PositionInChatListChange History::adjustByPosInChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed) {
	t_assert(indexed != nullptr);
	Dialogs::Row *lnk = mainChatListLink(list);
	int32 movedFrom = lnk->pos();
	indexed->adjustByPos(chatListLinks(list));
	int32 movedTo = lnk->pos();
	return { movedFrom, movedTo };
}

int History::posInChatList(Dialogs::Mode list) const {
	return mainChatListLink(list)->pos();
}

Dialogs::Row *History::addToChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed) {
	t_assert(indexed != nullptr);
	if (!inChatList(list)) {
		chatListLinks(list) = indexed->addToEnd(this);
		if (list == Dialogs::Mode::All && unreadCount()) {
			App::histories().unreadIncrement(unreadCount(), mute());
			Notify::unreadCounterUpdated();
		}
	}
	return mainChatListLink(list);
}

void History::removeFromChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed) {
	t_assert(indexed != nullptr);
	if (inChatList(list)) {
		indexed->del(peer);
		chatListLinks(list).clear();
		if (list == Dialogs::Mode::All && unreadCount()) {
			App::histories().unreadIncrement(-unreadCount(), mute());
			Notify::unreadCounterUpdated();
		}
	}
}

void History::removeChatListEntryByLetter(Dialogs::Mode list, QChar letter) {
	t_assert(letter != 0);
	if (inChatList(list)) {
		chatListLinks(list).remove(letter);
	}
}

void History::addChatListEntryByLetter(Dialogs::Mode list, QChar letter, Dialogs::Row *row) {
	t_assert(letter != 0);
	if (inChatList(list)) {
		chatListLinks(list).insert(letter, row);
	}
}

void History::updateChatListEntry() const {
	if (MainWidget *m = App::main()) {
		if (inChatList(Dialogs::Mode::All)) {
			m->dlgUpdated(Dialogs::Mode::All, mainChatListLink(Dialogs::Mode::All));
			if (inChatList(Dialogs::Mode::Important)) {
				m->dlgUpdated(Dialogs::Mode::Important, mainChatListLink(Dialogs::Mode::Important));
			}
		}
	}
}

void History::overviewSliceDone(int32 overviewIndex, const MTPmessages_Messages &result, bool onlyCounts) {
	const QVector<MTPMessage> *v = 0;
	switch (result.type()) {
	case mtpc_messages_messages: {
		auto &d(result.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
		overviewCountData[overviewIndex] = 0;
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d(result.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		overviewCountData[overviewIndex] = d.vcount.v;
		v = &d.vmessages.c_vector().v;
	} break;

	case mtpc_messages_channelMessages: {
		auto &d(result.c_messages_channelMessages());
		if (peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (History::overviewSliceDone, onlyCounts %1)").arg(Logs::b(onlyCounts)));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		overviewCountData[overviewIndex] = d.vcount.v;
		v = &d.vmessages.c_vector().v;
	} break;

	default: return;
	}

	if (!onlyCounts && v->isEmpty()) {
		overviewCountData[overviewIndex] = 0;
	} else if (overviewCountData[overviewIndex] > 0) {
		for_const (auto msgId, overviewIds[overviewIndex]) {
			if (msgId < 0) {
				++overviewCountData[overviewIndex];
			} else {
				break;
			}
		}
	}

	for (QVector<MTPMessage>::const_iterator i = v->cbegin(), e = v->cend(); i != e; ++i) {
		HistoryItem *item = App::histories().addNewMessage(*i, NewMessageExisting);
		if (item && overviewIds[overviewIndex].constFind(item->id) == overviewIds[overviewIndex].cend()) {
			overviewIds[overviewIndex].insert(item->id);
			overview[overviewIndex].push_front(item->id);
		}
	}
}

void History::changeMsgId(MsgId oldId, MsgId newId) {
	for (int32 i = 0; i < OverviewCount; ++i) {
		auto j = overviewIds[i].find(oldId);
		if (j != overviewIds[i].cend()) {
			overviewIds[i].erase(j);
			int32 index = overview[i].indexOf(oldId);
			if (overviewIds[i].constFind(newId) == overviewIds[i].cend()) {
				overviewIds[i].insert(newId);
				if (index >= 0) {
					overview[i][index] = newId;
				} else {
					overview[i].push_back(newId);
				}
			} else if (index >= 0) {
				overview[i].removeAt(index);
			}
		}
	}
}

void History::removeBlock(HistoryBlock *block) {
	t_assert(block->items.isEmpty());

	if (_buildingFrontBlock && block == _buildingFrontBlock->block) {
		_buildingFrontBlock->block = nullptr;
	}

	int index = block->indexInHistory();
	blocks.removeAt(index);
	for (int i = index, l = blocks.size(); i < l; ++i) {
		blocks.at(i)->setIndexInHistory(i);
	}
	if (index < blocks.size()) {
		blocks.at(index)->items.front()->previousItemChanged();
	}
}

History::~History() {
	clearOnDestroy();
}

int HistoryBlock::resizeGetHeight(int newWidth, bool resizeAllItems) {
	int y = 0;
	for_const (HistoryItem *item, items) {
		item->y = y;
		if (resizeAllItems || item->pendingResize()) {
			y += item->resizeGetHeight(newWidth);
		} else {
			y += item->height();
		}
	}
	height = y;
	return height;
}

void HistoryBlock::clear(bool leaveItems) {
	Items lst;
	std::swap(lst, items);

	if (leaveItems) {
		for_const (HistoryItem *item, lst) {
			item->detachFast();
		}
	} else {
		for_const (HistoryItem *item, lst) {
			delete item;
		}
	}
}

void HistoryBlock::removeItem(HistoryItem *item) {
	t_assert(item->block() == this);

	int blockIndex = indexInHistory();
	int itemIndex = item->indexInBlock();
	if (history->showFrom == item) {
		history->getNextShowFrom(this, itemIndex);
	}
	if (history->lastSentMsg == item) {
		history->lastSentMsg = nullptr;
	}
	if (history->unreadBar == item) {
		history->unreadBar = nullptr;
	}
	if (history->scrollTopItem == item) {
		history->getNextScrollTopItem(this, itemIndex);
	}

	item->detachFast();
	items.remove(itemIndex);
	for (int i = itemIndex, l = items.size(); i < l; ++i) {
		items.at(i)->setIndexInBlock(i);
	}
	if (items.isEmpty()) {
		history->removeBlock(this);
	} else if (itemIndex < items.size()) {
		items.at(itemIndex)->previousItemChanged();
	} else if (blockIndex + 1 < history->blocks.size()) {
		history->blocks.at(blockIndex + 1)->items.front()->previousItemChanged();
	}

	if (items.isEmpty()) {
		delete this;
	}
}
