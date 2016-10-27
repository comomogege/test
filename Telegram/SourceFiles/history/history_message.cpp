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
#include "history/history_message.h"

#include "lang.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "history/history_location_manager.h"
#include "history/history_service_layout.h"
#include "history/history_media_types.h"
#include "styles/style_dialogs.h"
#include "styles/style_history.h"

namespace {

TextParseOptions _historySrvOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags/* | TextParseMultiline*/ | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};

inline void initTextOptions() {
	_historySrvOptions.dir = _textNameOptions.dir = _textDlgOptions.dir = cLangDir();
	_textDlgOptions.maxw = st::dialogsWidthMax * 2;
}

MediaOverviewType messageMediaToOverviewType(HistoryMedia *media) {
	switch (media->type()) {
	case MediaTypePhoto: return OverviewPhotos;
	case MediaTypeVideo: return OverviewVideos;
	case MediaTypeFile: return OverviewFiles;
	case MediaTypeMusicFile: return media->getDocument()->isMusic() ? OverviewMusicFiles : OverviewCount;
	case MediaTypeVoiceFile: return OverviewVoiceFiles;
	case MediaTypeGif: return media->getDocument()->isGifv() ? OverviewCount : OverviewFiles;
	default: break;
	}
	return OverviewCount;
}

MediaOverviewType serviceMediaToOverviewType(HistoryMedia *media) {
	switch (media->type()) {
	case MediaTypePhoto: return OverviewChatPhotos;
	default: break;
	}
	return OverviewCount;
}

ApiWrap::RequestMessageDataCallback historyDependentItemCallback(const FullMsgId &msgId) {
	return [dependent = msgId](ChannelData *channel, MsgId msgId) {
		if (HistoryItem *item = App::histItemById(dependent)) {
			item->updateDependencyItem();
		}
	};
}

} // namespace

void historyInitMessages() {
	initTextOptions();
}

void HistoryMessageVia::create(int32 userId) {
	_bot = App::user(peerFromUser(userId));
	_maxWidth = st::msgServiceNameFont->width(lng_inline_bot_via(lt_inline_bot, '@' + _bot->username));
	_lnk.reset(new ViaInlineBotClickHandler(_bot));
}

void HistoryMessageVia::resize(int32 availw) const {
	if (availw < 0) {
		_text = QString();
		_width = 0;
	} else {
		_text = lng_inline_bot_via(lt_inline_bot, '@' + _bot->username);
		if (availw < _maxWidth) {
			_text = st::msgServiceNameFont->elided(_text, availw);
			_width = st::msgServiceNameFont->width(_text);
		} else if (_width < _maxWidth) {
			_width = _maxWidth;
		}
	}
}

void HistoryMessageSigned::create(UserData *from, const QDateTime &date) {
	QString time = qsl(", ") + date.toString(cTimeFormat()), name = App::peerName(from);
	int32 timew = st::msgDateFont->width(time), namew = st::msgDateFont->width(name);
	if (timew + namew > st::maxSignatureSize) {
		name = st::msgDateFont->elided(from->firstName, st::maxSignatureSize - timew);
	}
	_signature.setText(st::msgDateFont, name + time, _textNameOptions);
}

int HistoryMessageSigned::maxWidth() const {
	return _signature.maxWidth();
}

void HistoryMessageEdited::create(const QDateTime &editDate, const QDateTime &date) {
	_editDate = editDate;

	QString time = date.toString(cTimeFormat());
	_edited.setText(st::msgDateFont, lang(lng_edited) + ' ' + time, _textNameOptions);
}

int HistoryMessageEdited::maxWidth() const {
	return _edited.maxWidth();
}

void HistoryMessageForwarded::create(const HistoryMessageVia *via) const {
	QString text;
	if (_authorOriginal != _fromOriginal) {
		text = lng_forwarded_signed(lt_channel, App::peerName(_authorOriginal), lt_user, App::peerName(_fromOriginal));
	} else {
		text = App::peerName(_authorOriginal);
	}
	if (via) {
		if (_authorOriginal->isChannel()) {
			text = lng_forwarded_channel_via(lt_channel, textcmdLink(1, text), lt_inline_bot, textcmdLink(2, '@' + via->_bot->username));
		} else {
			text = lng_forwarded_via(lt_user, textcmdLink(1, text), lt_inline_bot, textcmdLink(2, '@' + via->_bot->username));
		}
	} else {
		if (_authorOriginal->isChannel()) {
			text = lng_forwarded_channel(lt_channel, textcmdLink(1, text));
		} else {
			text = lng_forwarded(lt_user, textcmdLink(1, text));
		}
	}
	TextParseOptions opts = { TextParseRichText, 0, 0, Qt::LayoutDirectionAuto };
	textstyleSet(&st::inFwdTextStyle);
	_text.setText(st::msgServiceNameFont, text, opts);
	textstyleRestore();
	_text.setLink(1, (_originalId && _authorOriginal->isChannel()) ? ClickHandlerPtr(new GoToMessageClickHandler(_authorOriginal->id, _originalId)) : _authorOriginal->openLink());
	if (via) {
		_text.setLink(2, via->_lnk);
	}
}

bool HistoryMessageReply::updateData(HistoryMessage *holder, bool force) {
	if (!force) {
		if (replyToMsg || !replyToMsgId) {
			return true;
		}
	}
	if (!replyToMsg) {
		replyToMsg = App::histItemById(holder->channelId(), replyToMsgId);
		if (replyToMsg) {
			App::historyRegDependency(holder, replyToMsg);
		}
	}

	if (replyToMsg) {
		replyToText.setText(st::msgFont, textClean(replyToMsg->inReplyText()), _textDlgOptions);

		updateName();

		replyToLnk.reset(new GoToMessageClickHandler(replyToMsg->history()->peer->id, replyToMsg->id));
		if (!replyToMsg->Has<HistoryMessageForwarded>()) {
			if (auto bot = replyToMsg->viaBot()) {
				_replyToVia.reset(new HistoryMessageVia());
				_replyToVia->create(peerToUser(bot->id));
			}
		}
	} else if (force) {
		replyToMsgId = 0;
	}
	if (force) {
		holder->setPendingInitDimensions();
	}
	return (replyToMsg || !replyToMsgId);
}

void HistoryMessageReply::clearData(HistoryMessage *holder) {
	_replyToVia = nullptr;
	if (replyToMsg) {
		App::historyUnregDependency(holder, replyToMsg);
		replyToMsg = nullptr;
	}
	replyToMsgId = 0;
}

bool HistoryMessageReply::isNameUpdated() const {
	if (replyToMsg && replyToMsg->author()->nameVersion > replyToVersion) {
		updateName();
		return true;
	}
	return false;
}

void HistoryMessageReply::updateName() const {
	if (replyToMsg) {
		QString name = (_replyToVia && replyToMsg->author()->isUser()) ? replyToMsg->author()->asUser()->firstName : App::peerName(replyToMsg->author());
		replyToName.setText(st::msgServiceNameFont, name, _textNameOptions);
		replyToVersion = replyToMsg->author()->nameVersion;
		bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
		int32 previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		int32 w = replyToName.maxWidth();
		if (_replyToVia) {
			w += st::msgServiceFont->spacew + _replyToVia->_maxWidth;
		}

		_maxReplyWidth = previewSkip + qMax(w, qMin(replyToText.maxWidth(), int32(st::maxSignatureSize)));
	} else {
		_maxReplyWidth = st::msgDateFont->width(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message));
	}
	_maxReplyWidth = st::msgReplyPadding.left() + st::msgReplyBarSkip + _maxReplyWidth + st::msgReplyPadding.right();
}

void HistoryMessageReply::resize(int width) const {
	if (_replyToVia) {
		bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
		int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		_replyToVia->resize(width - st::msgReplyBarSkip - previewSkip - replyToName.maxWidth() - st::msgServiceFont->spacew);
	}
}

void HistoryMessageReply::itemRemoved(HistoryMessage *holder, HistoryItem *removed) {
	if (replyToMsg == removed) {
		clearData(holder);
		holder->setPendingInitDimensions();
	}
}

void HistoryMessageReply::paint(Painter &p, const HistoryItem *holder, int x, int y, int w, PaintFlags flags) const {
	bool selected = (flags & PaintSelected), outbg = holder->hasOutLayout();

	style::color bar;
	if (flags & PaintInBubble) {
		bar = ((flags & PaintSelected) ? (outbg ? st::historyOutSelectedFg : st::msgInReplyBarSelColor) : (outbg ? st::historyOutFg : st::msgInReplyBarColor));
	} else {
		bar = st::white;
	}
	QRect rbar(rtlrect(x + st::msgReplyBarPos.x(), y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), w + 2 * x));
	p.fillRect(rbar, bar);

	if (w > st::msgReplyBarSkip) {
		if (replyToMsg) {
			bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
			int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;

			if (hasPreview) {
				ImagePtr replyPreview = replyToMsg->getMedia()->replyPreview();
				if (!replyPreview->isNull()) {
					QRect to(rtlrect(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height(), w + 2 * x));
					p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(ImageRoundRadius::Small, replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height()));
					if (selected) {
						App::roundRect(p, to, textstyleCurrent()->selectOverlay, SelectedOverlaySmallCorners);
					}
				}
			}
			if (w > st::msgReplyBarSkip + previewSkip) {
				if (flags & PaintInBubble) {
					p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
				} else {
					p.setPen(st::white);
				}
				replyToName.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top(), w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
				if (_replyToVia && w > st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew) {
					p.setFont(st::msgServiceFont);
					p.drawText(x + st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew, y + st::msgReplyPadding.top() + st::msgServiceFont->ascent, _replyToVia->_text);
				}

				HistoryMessage *replyToAsMsg = replyToMsg->toHistoryMessage();
				if (!(flags & PaintInBubble)) {
				} else if ((replyToAsMsg && replyToAsMsg->emptyText()) || replyToMsg->serviceMsg()) {
					style::color date(outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg));
					p.setPen(date);
				} else {
					p.setPen(st::msgColor);
				}
				replyToText.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top() + st::msgServiceNameFont->height, w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
			}
		} else {
			p.setFont(st::msgDateFont);
			style::color date(outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg));
			p.setPen((flags & PaintInBubble) ? date : st::white);
			p.drawTextLeft(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2, w + 2 * x, st::msgDateFont->elided(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message), w - st::msgReplyBarSkip));
		}
	}
}

void HistoryMessage::KeyboardStyle::startPaint(Painter &p) const {
	p.setPen(st::msgServiceColor);
}

style::font HistoryMessage::KeyboardStyle::textFont() const {
	return st::msgServiceFont;
}

void HistoryMessage::KeyboardStyle::repaint(const HistoryItem *item) const {
	Ui::repaintHistoryItem(item);
}

void HistoryMessage::KeyboardStyle::paintButtonBg(Painter &p, const QRect &rect, bool down, float64 howMuchOver) const {
	App::roundRect(p, rect, App::msgServiceBg(), StickerCorners);
	if (down) {
		howMuchOver = 1.;
	}
	if (howMuchOver > 0) {
		float64 o = p.opacity();
		p.setOpacity(o * (howMuchOver * st::msgBotKbOverOpacity));
		App::roundRect(p, rect, st::white, WhiteCorners);
		p.setOpacity(o);
	}
}

void HistoryMessage::KeyboardStyle::paintButtonIcon(Painter &p, const QRect &rect, int outerWidth, HistoryMessageReplyMarkup::Button::Type type) const {
	using Button = HistoryMessageReplyMarkup::Button;
	auto getIcon = [](Button::Type type) -> const style::icon* {
		switch (type) {
		case Button::Type::Url: return &st::msgBotKbUrlIcon;
		case Button::Type::SwitchInlineSame:
		case Button::Type::SwitchInline: return &st::msgBotKbSwitchPmIcon;
		}
		return nullptr;
	};
	if (auto icon = getIcon(type)) {
		icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + st::msgBotKbIconPadding, outerWidth);
	}
}

void HistoryMessage::KeyboardStyle::paintButtonLoading(Painter &p, const QRect &rect) const {
	auto icon = &st::historySendingInvertedIcon;
	icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + rect.height() - icon->height() - st::msgBotKbIconPadding, rect.x() * 2 + rect.width());
}

int HistoryMessage::KeyboardStyle::minButtonWidth(HistoryMessageReplyMarkup::Button::Type type) const {
	using Button = HistoryMessageReplyMarkup::Button;
	int result = 2 * buttonPadding(), iconWidth = 0;
	switch (type) {
	case Button::Type::Url: iconWidth = st::msgBotKbUrlIcon.width(); break;
	case Button::Type::SwitchInlineSame:
	case Button::Type::SwitchInline: iconWidth = st::msgBotKbSwitchPmIcon.width(); break;
	case Button::Type::Callback:
	case Button::Type::Game: iconWidth = st::historySendingInvertedIcon.width(); break;
	}
	if (iconWidth > 0) {
		result = std::max(result, 2 * iconWidth + 4 * int(st::msgBotKbIconPadding));
	}
	return result;
}

HistoryMessage::HistoryMessage(History *history, const MTPDmessage &msg)
	: HistoryItem(history, msg.vid.v, msg.vflags.v, ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0) {
	CreateConfig config;

	if (msg.has_fwd_from() && msg.vfwd_from.type() == mtpc_messageFwdHeader) {
		auto &f = msg.vfwd_from.c_messageFwdHeader();
		if (f.has_from_id() || f.has_channel_id()) {
			config.authorIdOriginal = f.has_channel_id() ? peerFromChannel(f.vchannel_id) : peerFromUser(f.vfrom_id);
			config.fromIdOriginal = f.has_from_id() ? peerFromUser(f.vfrom_id) : peerFromChannel(f.vchannel_id);
			if (f.has_channel_post()) config.originalId = f.vchannel_post.v;
		}
	}
	if (msg.has_reply_to_msg_id()) config.replyTo = msg.vreply_to_msg_id.v;
	if (msg.has_via_bot_id()) config.viaBotId = msg.vvia_bot_id.v;
	if (msg.has_views()) config.viewsCount = msg.vviews.v;
	if (msg.has_reply_markup()) config.mtpMarkup = &msg.vreply_markup;
	if (msg.has_edit_date()) config.editDate = ::date(msg.vedit_date);

	createComponents(config);

	initMedia(msg.has_media() ? (&msg.vmedia) : nullptr);

	TextWithEntities textWithEntities = {
		textClean(qs(msg.vmessage)),
		msg.has_entities() ? entitiesFromMTP(msg.ventities.c_vector().v) : EntitiesInText(),
	};
	setText(textWithEntities);
}

namespace {

MTPDmessage::Flags newForwardedFlags(PeerData *p, int32 from, HistoryMessage *fwd) {
	MTPDmessage::Flags result = newMessageFlags(p) | MTPDmessage::Flag::f_fwd_from;
	if (from) {
		result |= MTPDmessage::Flag::f_from_id;
	}
	if (fwd->Has<HistoryMessageVia>()) {
		result |= MTPDmessage::Flag::f_via_bot_id;
	}
	if (!p->isChannel()) {
		if (HistoryMedia *media = fwd->getMedia()) {
			if (media->type() == MediaTypeVoiceFile) {
				result |= MTPDmessage::Flag::f_media_unread;
				//			} else if (media->type() == MediaTypeVideo) {
				//				result |= MTPDmessage::flag_media_unread;
			}
		}
	}
	if (fwd->hasViews()) {
		result |= MTPDmessage::Flag::f_views;
	}
	return result;
}

} // namespace

HistoryMessage::HistoryMessage(History *history, MsgId id, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *fwd)
	: HistoryItem(history, id, newForwardedFlags(history->peer, from, fwd) | flags, date, from) {
	CreateConfig config;

	config.authorIdOriginal = fwd->authorOriginal()->id;
	config.fromIdOriginal = fwd->fromOriginal()->id;
	if (fwd->authorOriginal()->isChannel()) {
		config.originalId = fwd->id;
	}
	auto fwdViaBot = fwd->viaBot();
	if (fwdViaBot) config.viaBotId = peerToUser(fwdViaBot->id);
	int fwdViewsCount = fwd->viewsCount();
	if (fwdViewsCount > 0) {
		config.viewsCount = fwdViewsCount;
	} else if (isPost()) {
		config.viewsCount = 1;
	}

	// Copy inline keyboard when forwarding messages with a game.
	auto mediaOriginal = fwd->getMedia();
	if (mediaOriginal && mediaOriginal->type() == MediaTypeGame) {
		config.inlineMarkup = fwd->inlineReplyMarkup();
	}

	createComponents(config);

	if (mediaOriginal) {
		_media.reset(mediaOriginal->clone(this));
	}
	setText(fwd->originalText());
}

HistoryMessage::HistoryMessage(History *history, MsgId id, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, const TextWithEntities &textWithEntities)
	: HistoryItem(history, id, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, MTPnullMarkup);

	setText(textWithEntities);
}

HistoryMessage::HistoryMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup)
	: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, markup);

	initMediaFromDocument(doc, caption);
	setText(TextWithEntities());
}

HistoryMessage::HistoryMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup)
	: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, markup);

	_media.reset(new HistoryPhoto(this, photo, caption));
	setText(TextWithEntities());
}

HistoryMessage::HistoryMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, GameData *game, const MTPReplyMarkup &markup)
	: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, markup);

	_media.reset(new HistoryGame(this, game));
	setText(TextWithEntities());
}

void HistoryMessage::createComponentsHelper(MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, const MTPReplyMarkup &markup) {
	CreateConfig config;

	if (flags & MTPDmessage::Flag::f_via_bot_id) config.viaBotId = viaBotId;
	if (flags & MTPDmessage::Flag::f_reply_to_msg_id) config.replyTo = replyTo;
	if (flags & MTPDmessage::Flag::f_reply_markup) config.mtpMarkup = &markup;
	if (isPost()) config.viewsCount = 1;

	createComponents(config);
}

void HistoryMessage::updateMediaInBubbleState() {
	if (!_media) {
		return;
	}

	if (!drawBubble()) {
		_media->setInBubbleState(MediaInBubbleState::None);
		return;
	}

	bool hasSomethingAbove = displayFromName() || displayForwardedFrom() || Has<HistoryMessageReply>() || Has<HistoryMessageVia>();
	bool hasSomethingBelow = false;
	if (!emptyText()) {
		if (_media->isAboveMessage()) {
			hasSomethingBelow = true;
		} else {
			hasSomethingAbove = true;
		}
	}
	auto computeState = [hasSomethingAbove, hasSomethingBelow] {
		if (hasSomethingAbove) {
			if (hasSomethingBelow) {
				return MediaInBubbleState::Middle;
			}
			return MediaInBubbleState::Bottom;
		} else if (hasSomethingBelow) {
			return MediaInBubbleState::Top;
		}
		return MediaInBubbleState::None;
	};
	_media->setInBubbleState(computeState());
}

bool HistoryMessage::displayEditedBadge(bool hasViaBotOrInlineMarkup) const {
	if (hasViaBotOrInlineMarkup) {
		return false;
	} else if (!(_flags & MTPDmessage::Flag::f_edit_date)) {
		return false;
	}
	if (auto fromUser = from()->asUser()) {
		if (fromUser->botInfo) {
			return false;
		}
	}
	return true;
}


void HistoryMessage::createComponents(const CreateConfig &config) {
	uint64 mask = 0;
	if (config.replyTo) {
		mask |= HistoryMessageReply::Bit();
	}
	if (config.viaBotId) {
		mask |= HistoryMessageVia::Bit();
	}
	if (config.viewsCount >= 0) {
		mask |= HistoryMessageViews::Bit();
	}
	if (isPost() && _from->isUser()) {
		mask |= HistoryMessageSigned::Bit();
	}
	auto hasViaBot = (config.viaBotId != 0);
	auto hasInlineMarkup = [&config] {
		if (config.mtpMarkup) {
			return (config.mtpMarkup->type() == mtpc_replyInlineMarkup);
		}
		return (config.inlineMarkup != nullptr);
	};
	if (displayEditedBadge(hasViaBot || hasInlineMarkup())) {
		mask |= HistoryMessageEdited::Bit();
	}
	if (config.authorIdOriginal && config.fromIdOriginal) {
		mask |= HistoryMessageForwarded::Bit();
	}
	if (config.mtpMarkup) {
		// optimization: don't create markup component for the case
		// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
		if (config.mtpMarkup->type() != mtpc_replyKeyboardHide || config.mtpMarkup->c_replyKeyboardHide().vflags.v != 0) {
			mask |= HistoryMessageReplyMarkup::Bit();
		}
	} else if (config.inlineMarkup) {
		mask |= HistoryMessageReplyMarkup::Bit();
	}

	UpdateComponents(mask);

	if (auto reply = Get<HistoryMessageReply>()) {
		reply->replyToMsgId = config.replyTo;
		if (!reply->updateData(this) && App::api()) {
			App::api()->requestMessageData(history()->peer->asChannel(), reply->replyToMsgId, historyDependentItemCallback(fullId()));
		}
	}
	if (auto via = Get<HistoryMessageVia>()) {
		via->create(config.viaBotId);
	}
	if (auto views = Get<HistoryMessageViews>()) {
		views->_views = config.viewsCount;
	}
	if (auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->create(_from->asUser(), date);
	}
	if (auto edited = Get<HistoryMessageEdited>()) {
		edited->create(config.editDate, date);
	}
	if (auto fwd = Get<HistoryMessageForwarded>()) {
		fwd->_authorOriginal = App::peer(config.authorIdOriginal);
		fwd->_fromOriginal = App::peer(config.fromIdOriginal);
		fwd->_originalId = config.originalId;
	}
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (config.mtpMarkup) {
			markup->create(*config.mtpMarkup);
		} else if (config.inlineMarkup) {
			markup->create(*config.inlineMarkup);
		}
		if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_has_switch_inline_button) {
			_flags |= MTPDmessage_ClientFlag::f_has_switch_inline_button;
		}
	}
	initTime();
}

QString formatViewsCount(int32 views) {
	if (views > 999999) {
		views /= 100000;
		if (views % 10) {
			return QString::number(views / 10) + '.' + QString::number(views % 10) + 'M';
		}
		return QString::number(views / 10) + 'M';
	} else if (views > 9999) {
		views /= 100;
		if (views % 10) {
			return QString::number(views / 10) + '.' + QString::number(views % 10) + 'K';
		}
		return QString::number(views / 10) + 'K';
	} else if (views > 0) {
		return QString::number(views);
	}
	return qsl("1");
}

void HistoryMessage::initTime() {
	if (auto msgsigned = Get<HistoryMessageSigned>()) {
		_timeWidth = msgsigned->maxWidth();
	} else if (auto edited = Get<HistoryMessageEdited>()) {
		_timeWidth = edited->maxWidth();
	} else {
		_timeText = date.toString(cTimeFormat());
		_timeWidth = st::msgDateFont->width(_timeText);
	}
	if (auto views = Get<HistoryMessageViews>()) {
		views->_viewsText = (views->_views >= 0) ? formatViewsCount(views->_views) : QString();
		views->_viewsWidth = views->_viewsText.isEmpty() ? 0 : st::msgDateFont->width(views->_viewsText);
	}
}

void HistoryMessage::initMedia(const MTPMessageMedia *media) {
	switch (media ? media->type() : mtpc_messageMediaEmpty) {
	case mtpc_messageMediaContact: {
		auto &d = media->c_messageMediaContact();
		_media.reset(new HistoryContact(this, d.vuser_id.v, qs(d.vfirst_name), qs(d.vlast_name), qs(d.vphone_number)));
	} break;
	case mtpc_messageMediaGeo: {
		auto &point = media->c_messageMediaGeo().vgeo;
		if (point.type() == mtpc_geoPoint) {
			_media.reset(new HistoryLocation(this, LocationCoords(point.c_geoPoint())));
		}
	} break;
	case mtpc_messageMediaVenue: {
		auto &d = media->c_messageMediaVenue();
		if (d.vgeo.type() == mtpc_geoPoint) {
			_media.reset(new HistoryLocation(this, LocationCoords(d.vgeo.c_geoPoint()), qs(d.vtitle), qs(d.vaddress)));
		}
	} break;
	case mtpc_messageMediaPhoto: {
		auto &photo = media->c_messageMediaPhoto();
		if (photo.vphoto.type() == mtpc_photo) {
			_media.reset(new HistoryPhoto(this, App::feedPhoto(photo.vphoto.c_photo()), qs(photo.vcaption)));
		}
	} break;
	case mtpc_messageMediaDocument: {
		auto &document = media->c_messageMediaDocument().vdocument;
		if (document.type() == mtpc_document) {
			return initMediaFromDocument(App::feedDocument(document), qs(media->c_messageMediaDocument().vcaption));
		}
	} break;
	case mtpc_messageMediaWebPage: {
		auto &d = media->c_messageMediaWebPage().vwebpage;
		switch (d.type()) {
		case mtpc_webPageEmpty: break;
		case mtpc_webPagePending: {
			_media.reset(new HistoryWebPage(this, App::feedWebPage(d.c_webPagePending())));
		} break;
		case mtpc_webPage: {
			_media.reset(new HistoryWebPage(this, App::feedWebPage(d.c_webPage())));
		} break;
		}
	} break;
	case mtpc_messageMediaGame: {
		auto &d = media->c_messageMediaGame().vgame;
		if (d.type() == mtpc_game) {
			_media.reset(new HistoryGame(this, App::feedGame(d.c_game())));
		}
	} break;
	};
}

void HistoryMessage::initMediaFromDocument(DocumentData *doc, const QString &caption) {
	if (doc->sticker()) {
		_media.reset(new HistorySticker(this, doc));
	} else if (doc->isAnimation()) {
		_media.reset(new HistoryGif(this, doc, caption));
	} else if (doc->isVideo()) {
		_media.reset(new HistoryVideo(this, doc, caption));
	} else {
		_media.reset(new HistoryDocument(this, doc, caption));
	}
}

int32 HistoryMessage::plainMaxWidth() const {
	return st::msgPadding.left() + _text.maxWidth() + st::msgPadding.right();
}

void HistoryMessage::initDimensions() {
	auto reply = Get<HistoryMessageReply>();
	if (reply) {
		reply->updateName();
	}

	updateMediaInBubbleState();
	if (drawBubble()) {
		auto fwd = Get<HistoryMessageForwarded>();
		auto via = Get<HistoryMessageVia>();
		if (fwd) {
			fwd->create(via);
		}

		auto mediaDisplayed = false;
		if (_media) {
			mediaDisplayed = _media->isDisplayed();
			_media->initDimensions();
			if (mediaDisplayed && _media->isBubbleBottom()) {
				if (_text.hasSkipBlock()) {
					_text.removeSkipBlock();
					_textWidth = -1;
					_textHeight = 0;
				}
			} else if (!_text.hasSkipBlock()) {
				_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
				_textWidth = -1;
				_textHeight = 0;
			}
		}

		_maxw = plainMaxWidth();
		_minh = emptyText() ? 0 : _text.minHeight();
		if (mediaDisplayed) {
			if (!_media->isBubbleTop()) {
				_minh += st::msgPadding.top() + st::mediaInBubbleSkip;
			}
			if (!_media->isBubbleBottom()) {
				_minh += st::msgPadding.bottom() + st::mediaInBubbleSkip;
			}
			auto maxw = _media->maxWidth();
			if (maxw > _maxw) _maxw = maxw;
			_minh += _media->minHeight();
		} else {
			_minh += st::msgPadding.top() + st::msgPadding.bottom();
			if (displayFromName()) {
				auto namew = st::msgPadding.left() + author()->nameText.maxWidth() + st::msgPadding.right();
				if (via && !fwd) {
					namew += st::msgServiceFont->spacew + via->_maxWidth;
				}
				if (namew > _maxw) _maxw = namew;
			} else if (via && !fwd) {
				if (st::msgPadding.left() + via->_maxWidth + st::msgPadding.right() > _maxw) {
					_maxw = st::msgPadding.left() + via->_maxWidth + st::msgPadding.right();
				}
			}
			if (fwd) {
				auto _namew = st::msgPadding.left() + fwd->_text.maxWidth() + st::msgPadding.right();
				if (via) {
					_namew += st::msgServiceFont->spacew + via->_maxWidth;
				}
				if (_namew > _maxw) _maxw = _namew;
			}
		}
	} else if (_media) {
		_media->initDimensions();
		_maxw = _media->maxWidth();
		_minh = _media->minHeight();
	} else {
		_maxw = st::msgMinWidth;
		_minh = 0;
	}
	if (reply && !emptyText()) {
		int replyw = st::msgPadding.left() + reply->_maxReplyWidth - st::msgReplyPadding.left() - st::msgReplyPadding.right() + st::msgPadding.right();
		if (reply->_replyToVia) {
			replyw += st::msgServiceFont->spacew + reply->_replyToVia->_maxWidth;
		}
		if (replyw > _maxw) _maxw = replyw;
	}
	if (auto markup = inlineReplyMarkup()) {
		if (!markup->inlineKeyboard) {
			markup->inlineKeyboard.reset(new ReplyKeyboard(this, std_::make_unique<KeyboardStyle>(st::msgBotKbButton)));
		}

		// if we have a text bubble we can resize it to fit the keyboard
		// but if we have only media we don't do that
		if (!emptyText()) {
			_maxw = qMax(_maxw, markup->inlineKeyboard->naturalWidth());
		}
	}
}

void HistoryMessage::countPositionAndSize(int32 &left, int32 &width) const {
	int32 maxwidth = qMin(int(st::msgMaxWidth), _maxw), hwidth = _history->width;
	if (_media && _media->currentWidth() < maxwidth) {
		maxwidth = qMax(_media->currentWidth(), qMin(maxwidth, plainMaxWidth()));
	}

	left = (!isPost() && out() && !Adaptive::Wide()) ? st::msgMargin.right() : st::msgMargin.left();
	if (hasFromPhoto()) {
		left += st::msgPhotoSkip;
		//	} else if (!Adaptive::Wide() && !out() && !fromChannel() && st::msgPhotoSkip - (hmaxwidth - hwidth) > 0) {
		//		left += st::msgPhotoSkip - (hmaxwidth - hwidth);
	}

	width = hwidth - st::msgMargin.left() - st::msgMargin.right();
	if (width > maxwidth) {
		if (!isPost() && out() && !Adaptive::Wide()) {
			left += width - maxwidth;
		}
		width = maxwidth;
	}
}

void HistoryMessage::fromNameUpdated(int32 width) const {
	_authorNameVersion = author()->nameVersion;
	if (!Has<HistoryMessageForwarded>()) {
		if (auto via = Get<HistoryMessageVia>()) {
			via->resize(width - st::msgPadding.left() - st::msgPadding.right() - author()->nameText.maxWidth() - st::msgServiceFont->spacew);
		}
	}
}

void HistoryMessage::applyEdition(const MTPDmessage &message) {
	int keyboardTop = -1;
	if (!pendingResize()) {
		if (auto keyboard = inlineReplyKeyboard()) {
			int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
			keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
		}
	}

	if (message.has_edit_date()) {
		_flags |= MTPDmessage::Flag::f_edit_date;
		auto hasViaBotId = Has<HistoryMessageVia>();
		auto hasInlineMarkup = (inlineReplyMarkup() != nullptr);
		if (displayEditedBadge(hasViaBotId || hasInlineMarkup)) {
			if (!Has<HistoryMessageEdited>()) {
				AddComponents(HistoryMessageEdited::Bit());
			}
			Get<HistoryMessageEdited>()->create(::date(message.vedit_date), date);
		} else if (Has<HistoryMessageEdited>()) {
			RemoveComponents(HistoryMessageEdited::Bit());
		}
		initTime();
	}

	TextWithEntities textWithEntities = { qs(message.vmessage), EntitiesInText() };
	if (message.has_entities()) {
		textWithEntities.entities = entitiesFromMTP(message.ventities.c_vector().v);
	}
	setText(textWithEntities);
	setMedia(message.has_media() ? (&message.vmedia) : nullptr);
	setReplyMarkup(message.has_reply_markup() ? (&message.vreply_markup) : nullptr);
	setViewsCount(message.has_views() ? message.vviews.v : -1);

	finishEdition(keyboardTop);
}

void HistoryMessage::applyEdition(const MTPDmessageService &message) {
	if (message.vaction.type() == mtpc_messageActionHistoryClear) {
		applyEditionToEmpty();
	}
}

void HistoryMessage::applyEditionToEmpty() {
	setEmptyText();
	setMedia(nullptr);
	setReplyMarkup(nullptr);
	setViewsCount(-1);

	finishEditionToEmpty();
}

void HistoryMessage::updateMedia(const MTPMessageMedia *media) {
	auto setMediaAllowed = [](HistoryMediaType type) {
		return (type == MediaTypeWebPage || type == MediaTypeGame || type == MediaTypeLocation);
	};
	if (_flags & MTPDmessage_ClientFlag::f_from_inline_bot) {
		bool needReSet = true;
		if (media && _media) {
			needReSet = _media->needReSetInlineResultMedia(*media);
		}
		if (needReSet) {
			setMedia(media);
		}
		_flags &= ~MTPDmessage_ClientFlag::f_from_inline_bot;
	} else if (media && _media && !setMediaAllowed(_media->type())) {
		_media->updateSentMedia(*media);
	} else {
		setMedia(media);
	}
	setPendingInitDimensions();
}

int32 HistoryMessage::addToOverview(AddToOverviewMethod method) {
	if (!indexInOverview()) return 0;

	int32 result = 0;
	if (HistoryMedia *media = getMedia()) {
		MediaOverviewType type = messageMediaToOverviewType(media);
		if (type != OverviewCount) {
			if (history()->addToOverview(type, id, method)) {
				result |= (1 << type);
			}
		}
	}
	if (hasTextLinks()) {
		if (history()->addToOverview(OverviewLinks, id, method)) {
			result |= (1 << OverviewLinks);
		}
	}
	return result;
}

void HistoryMessage::eraseFromOverview() {
	if (HistoryMedia *media = getMedia()) {
		MediaOverviewType type = messageMediaToOverviewType(media);
		if (type != OverviewCount) {
			history()->eraseFromOverview(type, id);
		}
	}
	if (hasTextLinks()) {
		history()->eraseFromOverview(OverviewLinks, id);
	}
}

TextWithEntities HistoryMessage::selectedText(TextSelection selection) const {
	TextWithEntities result, textResult, mediaResult;
	if (selection == FullSelection) {
		textResult = _text.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
	} else {
		textResult = _text.originalTextWithEntities(selection, ExpandLinksAll);
	}
	if (_media) {
		mediaResult = _media->selectedText(toMediaSelection(selection));
	}
	if (textResult.text.isEmpty()) {
		result = mediaResult;
	} else if (mediaResult.text.isEmpty()) {
		result = textResult;
	} else {
		result.text = textResult.text + qstr("\n\n");
		result.entities = textResult.entities;
		appendTextWithEntities(result, std_::move(mediaResult));
	}
	if (auto fwd = Get<HistoryMessageForwarded>()) {
		if (selection == FullSelection) {
			auto fwdinfo = fwd->_text.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
			TextWithEntities wrapped;
			wrapped.text.reserve(fwdinfo.text.size() + 4 + result.text.size());
			wrapped.entities.reserve(fwdinfo.entities.size() + result.entities.size());
			wrapped.text.append('[');
			appendTextWithEntities(wrapped, std_::move(fwdinfo));
			wrapped.text.append(qsl("]\n"));
			appendTextWithEntities(wrapped, std_::move(result));
			result = wrapped;
		}
	}
	if (auto reply = Get<HistoryMessageReply>()) {
		if (selection == FullSelection && reply->replyToMsg) {
			TextWithEntities wrapped;
			wrapped.text.reserve(lang(lng_in_reply_to).size() + reply->replyToMsg->author()->name.size() + 4 + result.text.size());
			wrapped.text.append('[').append(lang(lng_in_reply_to)).append(' ').append(reply->replyToMsg->author()->name).append(qsl("]\n"));
			appendTextWithEntities(wrapped, std_::move(result));
			result = wrapped;
		}
	}
	return result;
}

void HistoryMessage::setMedia(const MTPMessageMedia *media) {
	if (!_media && (!media || media->type() == mtpc_messageMediaEmpty)) return;

	bool mediaRemovedSkipBlock = false;
	if (_media) {
		// Don't update Game media because we loose the consumed text of the message.
		if (_media->type() == MediaTypeGame) return;

		mediaRemovedSkipBlock = _media->isDisplayed() && _media->isBubbleBottom();
		_media.clear();
	}
	initMedia(media);
	auto mediaDisplayed = _media && _media->isDisplayed();
	if (mediaDisplayed && _media->isBubbleBottom() && !mediaRemovedSkipBlock) {
		_text.removeSkipBlock();
		_textWidth = -1;
		_textHeight = 0;
	} else if (mediaRemovedSkipBlock && (!mediaDisplayed || !_media->isBubbleBottom())) {
		_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
		_textWidth = -1;
		_textHeight = 0;
	}
}

void HistoryMessage::setText(const TextWithEntities &textWithEntities) {
	for_const (auto &entity, textWithEntities.entities) {
		auto type = entity.type();
		if (type == EntityInTextUrl || type == EntityInTextCustomUrl || type == EntityInTextEmail) {
			_flags |= MTPDmessage_ClientFlag::f_has_text_links;
			break;
		}
	}

	auto mediaDisplayed = _media && _media->isDisplayed();
	if (mediaDisplayed && _media->consumeMessageText(textWithEntities)) {
		setEmptyText();
	} else {
		textstyleSet(&((out() && !isPost()) ? st::outTextStyle : st::inTextStyle));
		if (_media && _media->isDisplayed() && !_media->isAboveMessage()) {
			_text.setMarkedText(st::msgFont, textWithEntities, itemTextOptions(this));
		} else {
			_text.setMarkedText(st::msgFont, { textWithEntities.text + skipBlock(), textWithEntities.entities }, itemTextOptions(this));
		}
		textstyleRestore();
		_textWidth = -1;
		_textHeight = 0;
	}
}

void HistoryMessage::setEmptyText() {
	textstyleSet(&((out() && !isPost()) ? st::outTextStyle : st::inTextStyle));
	_text.setMarkedText(st::msgFont, { QString(), EntitiesInText() }, itemTextOptions(this));
	textstyleRestore();

	_textWidth = -1;
	_textHeight = 0;
}

void HistoryMessage::setReplyMarkup(const MTPReplyMarkup *markup) {
	if (!markup) {
		if (_flags & MTPDmessage::Flag::f_reply_markup) {
			_flags &= ~MTPDmessage::Flag::f_reply_markup;
			if (Has<HistoryMessageReplyMarkup>()) {
				RemoveComponents(HistoryMessageReplyMarkup::Bit());
			}
			setPendingInitDimensions();
			Notify::replyMarkupUpdated(this);
		}
		return;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	if (markup->type() == mtpc_replyKeyboardHide && markup->c_replyKeyboardHide().vflags.v == 0) {
		bool changed = false;
		if (Has<HistoryMessageReplyMarkup>()) {
			RemoveComponents(HistoryMessageReplyMarkup::Bit());
			changed = true;
		}
		if (!(_flags & MTPDmessage::Flag::f_reply_markup)) {
			_flags |= MTPDmessage::Flag::f_reply_markup;
			changed = true;
		}
		if (changed) {
			setPendingInitDimensions();

			Notify::replyMarkupUpdated(this);
		}
	} else {
		if (!(_flags & MTPDmessage::Flag::f_reply_markup)) {
			_flags |= MTPDmessage::Flag::f_reply_markup;
		}
		if (!Has<HistoryMessageReplyMarkup>()) {
			AddComponents(HistoryMessageReplyMarkup::Bit());
		}
		Get<HistoryMessageReplyMarkup>()->create(*markup);
		setPendingInitDimensions();

		Notify::replyMarkupUpdated(this);
	}
}

TextWithEntities HistoryMessage::originalText() const {
	if (emptyText()) {
		return { QString(), EntitiesInText() };
	}
	return _text.originalTextWithEntities();
}

bool HistoryMessage::textHasLinks() const {
	return emptyText() ? false : _text.hasLinks();
}

int HistoryMessage::infoWidth() const {
	int result = _timeWidth;
	if (auto views = Get<HistoryMessageViews>()) {
		result += st::historyViewsSpace + views->_viewsWidth + st::historyViewsWidth;
	} else if (id < 0 && history()->peer->isSelf()) {
		result += st::historySendStateSpace;
	}
	if (out() && !isPost()) {
		result += st::historySendStateSpace;
	}
	return result;
}
int HistoryMessage::timeLeft() const {
	int result = 0;
	if (auto views = Get<HistoryMessageViews>()) {
		result += st::historyViewsSpace + views->_viewsWidth + st::historyViewsWidth;
	} else if (id < 0 && history()->peer->isSelf()) {
		result += st::historySendStateSpace;
	}
	return result;
}

void HistoryMessage::drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const {
	p.setFont(st::msgDateFont);

	bool outbg = out() && !isPost();
	bool invertedsprites = (type == InfoDisplayOverImage || type == InfoDisplayOverBackground);
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		p.setPen(selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg));
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgDateImgColor);
	break;
	case InfoDisplayOverBackground:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgServiceColor);
	break;
	}

	int32 infoW = HistoryMessage::infoWidth();
	if (rtl()) infoRight = width - infoRight + infoW;

	int32 dateX = infoRight - infoW;
	int32 dateY = infoBottom - st::msgDateFont->height;
	if (type == InfoDisplayOverImage) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	} else if (type == InfoDisplayOverBackground) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? App::msgServiceSelectBg() : App::msgServiceBg(), selected ? StickerSelectedCorners : StickerCorners);
	}
	dateX += HistoryMessage::timeLeft();

	if (auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->_signature.drawElided(p, dateX, dateY, _timeWidth);
	} else if (auto edited = Get<HistoryMessageEdited>()) {
		edited->_edited.drawElided(p, dateX, dateY, _timeWidth);
	} else {
		p.drawText(dateX, dateY + st::msgDateFont->ascent, _timeText);
	}

	if (auto views = Get<HistoryMessageViews>()) {
		auto icon = ([this, outbg, invertedsprites, selected] {
			if (id > 0) {
				if (outbg) {
					return &(invertedsprites ? st::historyViewsInvertedIcon : (selected ? st::historyViewsOutSelectedIcon : st::historyViewsOutIcon));
				}
				return &(invertedsprites ? st::historyViewsInvertedIcon : (selected ? st::historyViewsInSelectedIcon : st::historyViewsInIcon));
			}
			return &(invertedsprites ? st::historyViewsSendingInvertedIcon : st::historyViewsSendingIcon);
		})();
		if (id > 0) {
			icon->paint(p, infoRight - infoW, infoBottom + st::historyViewsTop, width);
			p.drawText(infoRight - infoW + st::historyViewsWidth, infoBottom - st::msgDateFont->descent, views->_viewsText);
		} else if (!outbg) { // sending outbg icon will be painted below
			auto iconSkip = st::historyViewsSpace + views->_viewsWidth;
			icon->paint(p, infoRight - infoW + iconSkip, infoBottom + st::historyViewsTop, width);
		}
	} else if (id < 0 && history()->peer->isSelf()) {
		auto icon = &(invertedsprites ? st::historyViewsSendingInvertedIcon : st::historyViewsSendingIcon);
		icon->paint(p, infoRight - infoW, infoBottom + st::historyViewsTop, width);
	}
	if (outbg) {
		auto icon = ([this, invertedsprites, selected] {
			if (id > 0) {
				if (unread()) {
					return &(invertedsprites ? st::historySentInvertedIcon : (selected ? st::historySentSelectedIcon : st::historySentIcon));
				}
				return &(invertedsprites ? st::historyReceivedInvertedIcon : (selected ? st::historyReceivedSelectedIcon : st::historyReceivedIcon));
			}
			return &(invertedsprites ? st::historySendingInvertedIcon : st::historySendingIcon);
		})();
		icon->paint(p, QPoint(infoRight, infoBottom) + st::historySendStatePosition, width);
	}
}

void HistoryMessage::setViewsCount(int32 count) {
	auto views = Get<HistoryMessageViews>();
	if (!views || views->_views == count || (count >= 0 && views->_views > count)) return;

	int32 was = views->_viewsWidth;
	views->_views = count;
	views->_viewsText = (views->_views >= 0) ? formatViewsCount(views->_views) : QString();
	views->_viewsWidth = views->_viewsText.isEmpty() ? 0 : st::msgDateFont->width(views->_viewsText);
	if (was == views->_viewsWidth) {
		Ui::repaintHistoryItem(this);
	} else {
		if (_text.hasSkipBlock()) {
			_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
			_textWidth = -1;
			_textHeight = 0;
		}
		setPendingInitDimensions();
	}
}

void HistoryMessage::setId(MsgId newId) {
	bool wasPositive = (id > 0), positive = (newId > 0);
	HistoryItem::setId(newId);
	if (wasPositive == positive) {
		Ui::repaintHistoryItem(this);
	} else {
		if (_text.hasSkipBlock()) {
			_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
			_textWidth = -1;
			_textHeight = 0;
		}
		setPendingInitDimensions();
	}
}

void HistoryMessage::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	bool outbg = out() && !isPost(), bubble = drawBubble(), selected = (selection == FullSelection);

	int left = 0, width = 0, height = _height;
	countPositionAndSize(left, width);
	if (width < 1) return;

	int dateh = 0, unreadbarh = 0;
	if (auto date = Get<HistoryMessageDate>()) {
		dateh = date->height();
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		unreadbarh = unreadbar->height();
		if (r.intersects(QRect(0, dateh, _history->width, unreadbarh))) {
			p.translate(0, dateh);
			unreadbar->paint(p, 0, _history->width);
			p.translate(0, -dateh);
		}
	}

	uint64 fullAnimMs = App::main() ? App::main()->animActiveTimeStart(this) : 0;
	if (fullAnimMs > 0 && fullAnimMs <= ms) {
		int animms = ms - fullAnimMs;
		if (animms > st::activeFadeInDuration + st::activeFadeOutDuration) {
			App::main()->stopAnimActive();
		} else {
			int skiph = marginTop() - marginBottom();

			float64 dt = (animms > st::activeFadeInDuration) ? (1 - (animms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (animms / float64(st::activeFadeInDuration));
			float64 o = p.opacity();
			p.setOpacity(o * dt);
			p.fillRect(0, skiph, _history->width, height - skiph, textstyleCurrent()->selectOverlay->b);
			p.setOpacity(o);
		}
	}

	textstyleSet(&(outbg ? st::outTextStyle : st::inTextStyle));

	if (auto keyboard = inlineReplyKeyboard()) {
		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
		height -= h;
		int top = height + st::msgBotKbButton.margin - marginBottom();
		p.translate(left, top);
		keyboard->paint(p, width, r.translated(-left, -top));
		p.translate(-left, -top);
	}

	if (bubble) {
		if (displayFromName() && author()->nameVersion > _authorNameVersion) {
			fromNameUpdated(width);
		}

		auto mediaDisplayed = _media && _media->isDisplayed();
		auto top = marginTop();
		QRect r(left, top, width, height - top - marginBottom());

		style::color bg(selected ? (outbg ? st::msgOutBgSelected : st::msgInBgSelected) : (outbg ? st::msgOutBg : st::msgInBg));
		style::color sh(selected ? (outbg ? st::msgOutShadowSelected : st::msgInShadowSelected) : (outbg ? st::msgOutShadow : st::msgInShadow));
		RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
		App::roundRect(p, r, bg, cors, &sh);

		QRect trect(r.marginsAdded(-st::msgPadding));
		if (mediaDisplayed && _media->isBubbleTop()) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			paintFromName(p, trect, selected);
			paintForwardedInfo(p, trect, selected);
			paintReplyInfo(p, trect, selected);
			paintViaBotIdInfo(p, trect, selected);
		}
		if (mediaDisplayed && _media->isBubbleBottom()) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}
		auto needDrawInfo = true;
		if (mediaDisplayed) {
			auto mediaAboveText = _media->isAboveMessage();
			auto mediaHeight = _media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = mediaAboveText ? trect.y() : (trect.y() + trect.height() - mediaHeight);
			if (!mediaAboveText) {
				paintText(p, trect, selection);
			}
			p.translate(mediaLeft, mediaTop);
			_media->draw(p, r.translated(-mediaLeft, -mediaTop), toMediaSelection(selection), ms);
			p.translate(-mediaLeft, -mediaTop);

			if (mediaAboveText) {
				trect.setY(trect.y() + mediaHeight);
				paintText(p, trect, selection);
			}

			needDrawInfo = !_media->customInfoLayout();
		} else {
			paintText(p, trect, selection);
		}
		if (needDrawInfo) {
			HistoryMessage::drawInfo(p, r.x() + r.width(), r.y() + r.height(), 2 * r.x() + r.width(), selected, InfoDisplayDefault);
		}
	} else if (_media) {
		int32 top = marginTop();
		p.translate(left, top);
		_media->draw(p, r.translated(-left, -top), toMediaSelection(selection), ms);
		p.translate(-left, -top);
	}

	textstyleRestore();

	auto reply = Get<HistoryMessageReply>();
	if (reply && reply->isNameUpdated()) {
		const_cast<HistoryMessage*>(this)->setPendingInitDimensions();
	}
}

void HistoryMessage::paintFromName(Painter &p, QRect &trect, bool selected) const {
	if (displayFromName()) {
		p.setFont(st::msgNameFont);
		if (isPost()) {
			p.setPen(selected ? st::msgInServiceFgSelected : st::msgInServiceFg);
		} else {
			p.setPen(author()->color);
		}
		author()->nameText.drawElided(p, trect.left(), trect.top(), trect.width());

		auto fwd = Get<HistoryMessageForwarded>();
		auto via = Get<HistoryMessageVia>();
		if (via && !fwd && trect.width() > author()->nameText.maxWidth() + st::msgServiceFont->spacew) {
			bool outbg = out() && !isPost();
			p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
			p.drawText(trect.left() + author()->nameText.maxWidth() + st::msgServiceFont->spacew, trect.top() + st::msgServiceFont->ascent, via->_text);
		}
		trect.setY(trect.y() + st::msgNameFont->height);
	}
}

void HistoryMessage::paintForwardedInfo(Painter &p, QRect &trect, bool selected) const {
	if (displayForwardedFrom()) {
		style::font serviceFont(st::msgServiceFont), serviceName(st::msgServiceNameFont);

		p.setPen(selected ? (hasOutLayout() ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (hasOutLayout() ? st::msgOutServiceFg : st::msgInServiceFg));
		p.setFont(serviceFont);

		auto fwd = Get<HistoryMessageForwarded>();
		bool breakEverywhere = (fwd->_text.countHeight(trect.width()) > 2 * serviceFont->height);
		textstyleSet(&(selected ? (hasOutLayout() ? st::outFwdTextStyleSelected : st::inFwdTextStyleSelected) : (hasOutLayout() ? st::outFwdTextStyle : st::inFwdTextStyle)));
		fwd->_text.drawElided(p, trect.x(), trect.y(), trect.width(), 2, style::al_left, 0, -1, 0, breakEverywhere);
		textstyleSet(&(hasOutLayout() ? st::outTextStyle : st::inTextStyle));

		trect.setY(trect.y() + (((fwd->_text.maxWidth() > trect.width()) ? 2 : 1) * serviceFont->height));
	}
}

void HistoryMessage::paintReplyInfo(Painter &p, QRect &trect, bool selected) const {
	if (auto reply = Get<HistoryMessageReply>()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();

		HistoryMessageReply::PaintFlags flags = HistoryMessageReply::PaintInBubble;
		if (selected) {
			flags |= HistoryMessageReply::PaintSelected;
		}
		reply->paint(p, this, trect.x(), trect.y(), trect.width(), flags);

		trect.setY(trect.y() + h);
	}
}

void HistoryMessage::paintViaBotIdInfo(Painter &p, QRect &trect, bool selected) const {
	if (!displayFromName() && !Has<HistoryMessageForwarded>()) {
		if (auto via = Get<HistoryMessageVia>()) {
			p.setFont(st::msgServiceNameFont);
			p.setPen(selected ? (hasOutLayout() ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (hasOutLayout() ? st::msgOutServiceFg : st::msgInServiceFg));
			p.drawTextLeft(trect.left(), trect.top(), _history->width, via->_text);
			trect.setY(trect.y() + st::msgServiceNameFont->height);
		}
	}
}

void HistoryMessage::paintText(Painter &p, QRect &trect, TextSelection selection) const {
	p.setPen(st::msgColor);
	p.setFont(st::msgFont);
	_text.draw(p, trect.x(), trect.y(), trect.width(), style::al_left, 0, -1, selection);
}

void HistoryMessage::dependencyItemRemoved(HistoryItem *dependency) {
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->itemRemoved(this, dependency);
	}
}

int HistoryMessage::resizeGetHeight_(int width) {
	int result = performResizeGetHeight(width);

	auto keyboard = inlineReplyKeyboard();
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		int oldTop = markup->oldTop;
		if (oldTop >= 0) {
			markup->oldTop = -1;
			if (keyboard) {
				int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
				int keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
				if (keyboardTop != oldTop) {
					Notify::inlineKeyboardMoved(this, oldTop, keyboardTop);
				}
			}
		}
	}

	return result;
}

int HistoryMessage::performResizeGetHeight(int width) {
	if (width < st::msgMinWidth) return _height;

	width -= st::msgMargin.left() + st::msgMargin.right();
	if (width < st::msgPadding.left() + st::msgPadding.right() + 1) {
		width = st::msgPadding.left() + st::msgPadding.right() + 1;
	} else if (width > st::msgMaxWidth) {
		width = st::msgMaxWidth;
	}
	if (drawBubble()) {
		auto fwd = Get<HistoryMessageForwarded>();
		auto reply = Get<HistoryMessageReply>();
		auto via = Get<HistoryMessageVia>();

		auto mediaDisplayed = false;
		auto mediaInBubbleState = MediaInBubbleState::None;
		if (_media) {
			mediaDisplayed = _media->isDisplayed();
			mediaInBubbleState = _media->inBubbleState();
		}
		if (width >= _maxw) {
			_height = _minh;
			if (mediaDisplayed) _media->resizeGetHeight(_maxw);
		} else {
			if (emptyText()) {
				_height = 0;
			} else {
				auto textWidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 1);
				if (textWidth != _textWidth) {
					textstyleSet(&((out() && !isPost()) ? st::outTextStyle : st::inTextStyle));
					_textWidth = textWidth;
					_textHeight = _text.countHeight(textWidth);
					textstyleRestore();
				}
				_height = _textHeight;
			}
			if (mediaDisplayed) {
				if (!_media->isBubbleTop()) {
					_height += st::msgPadding.top() + st::mediaInBubbleSkip;
				}
				if (!_media->isBubbleBottom()) {
					_height += st::msgPadding.bottom() + st::mediaInBubbleSkip;
				}
				_height += _media->resizeGetHeight(width);
			} else {
				_height += st::msgPadding.top() + st::msgPadding.bottom();
			}
		}

		if (displayFromName()) {
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);
			fromNameUpdated(w);
			_height += st::msgNameFont->height;
		} else if (via && !fwd) {
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);
			via->resize(w - st::msgPadding.left() - st::msgPadding.right());
			_height += st::msgNameFont->height;
		}

		if (displayForwardedFrom()) {
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);
			int32 fwdheight = ((fwd->_text.maxWidth() > (w - st::msgPadding.left() - st::msgPadding.right())) ? 2 : 1) * st::semiboldFont->height;
			_height += fwdheight;
		}

		if (reply) {
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);
			reply->resize(w - st::msgPadding.left() - st::msgPadding.right());
			_height += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		}
	} else if (_media) {
		_height = _media->resizeGetHeight(width);
	} else {
		_height = 0;
	}
	if (auto keyboard = inlineReplyKeyboard()) {
		int32 l = 0, w = 0;
		countPositionAndSize(l, w);

		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
		_height += h;
		keyboard->resize(w, h - st::msgBotKbButton.margin);
	}

	_height += marginTop() + marginBottom();
	return _height;
}

bool HistoryMessage::hasPoint(int x, int y) const {
	int left = 0, width = 0, height = _height;
	countPositionAndSize(left, width);
	if (width < 1) return false;

	if (drawBubble()) {
		int top = marginTop();
		QRect r(left, top, width, height - top - marginBottom());
		return r.contains(x, y);
	} else if (_media) {
		return _media->hasPoint(x - left, y - marginTop());
	} else {
		return false;
	}
}

bool HistoryMessage::pointInTime(int32 right, int32 bottom, int x, int y, InfoDisplayType type) const {
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
	break;
	}
	int32 dateX = infoRight - HistoryMessage::infoWidth() + HistoryMessage::timeLeft();
	int32 dateY = infoBottom - st::msgDateFont->height;
	return QRect(dateX, dateY, HistoryMessage::timeWidth(), st::msgDateFont->height).contains(x, y);
}

HistoryTextState HistoryMessage::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	int left = 0, width = 0, height = _height;
	countPositionAndSize(left, width);

	if (width < 1) return result;

	auto keyboard = inlineReplyKeyboard();
	if (keyboard) {
		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
		height -= h;
	}

	if (drawBubble()) {
		auto mediaDisplayed = _media && _media->isDisplayed();
		auto top = marginTop();
		QRect r(left, top, width, height - top - marginBottom());
		QRect trect(r.marginsAdded(-st::msgPadding));
		if (mediaDisplayed && _media->isBubbleTop()) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			if (getStateFromName(x, y, trect, &result)) return result;
			if (getStateForwardedInfo(x, y, trect, &result, request)) return result;
			if (getStateReplyInfo(x, y, trect, &result)) return result;
			if (getStateViaBotIdInfo(x, y, trect, &result)) return result;
		}
		if (mediaDisplayed && _media->isBubbleBottom()) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}

		auto needDateCheck = true;
		if (mediaDisplayed) {
			auto mediaAboveText = _media->isAboveMessage();
			auto mediaHeight = _media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = mediaAboveText ? trect.y() : (trect.y() + trect.height() - mediaHeight);

			if (y >= mediaTop && y < mediaTop + mediaHeight) {
				result = _media->getState(x - mediaLeft, y - mediaTop, request);
				result.symbol += _text.length();
			} else {
				if (mediaAboveText) {
					trect.setY(trect.y() + mediaHeight);
				}
				getStateText(x, y, trect, &result, request);
			}

			needDateCheck = !_media->customInfoLayout();
		} else {
			getStateText(x, y, trect, &result, request);
		}
		if (needDateCheck) {
			if (HistoryMessage::pointInTime(r.x() + r.width(), r.y() + r.height(), x, y, InfoDisplayDefault)) {
				result.cursor = HistoryInDateCursorState;
			}
		}
	} else if (_media) {
		result = _media->getState(x - left, y - marginTop(), request);
		result.symbol += _text.length();
	}

	if (keyboard) {
		int top = height + st::msgBotKbButton.margin - marginBottom();
		if (x >= left && x < left + width && y >= top && y < _height - marginBottom()) {
			result.link = keyboard->getState(x - left, y - top);
			return result;
		}
	}

	return result;
}

bool HistoryMessage::getStateFromName(int x, int y, QRect &trect, HistoryTextState *outResult) const {
	if (displayFromName()) {
		if (y >= trect.top() && y < trect.top() + st::msgNameFont->height) {
			if (x >= trect.left() && x < trect.left() + trect.width() && x < trect.left() + author()->nameText.maxWidth()) {
				outResult->link = author()->openLink();
				return true;
			}
			auto fwd = Get<HistoryMessageForwarded>();
			auto via = Get<HistoryMessageVia>();
			if (via && !fwd && x >= trect.left() + author()->nameText.maxWidth() + st::msgServiceFont->spacew && x < trect.left() + author()->nameText.maxWidth() + st::msgServiceFont->spacew + via->_width) {
				outResult->link = via->_lnk;
				return true;
			}
		}
		trect.setTop(trect.top() + st::msgNameFont->height);
	}
	return false;
}

bool HistoryMessage::getStateForwardedInfo(int x, int y, QRect &trect, HistoryTextState *outResult, const HistoryStateRequest &request) const {
	if (displayForwardedFrom()) {
		auto fwd = Get<HistoryMessageForwarded>();
		int32 fwdheight = ((fwd->_text.maxWidth() > trect.width()) ? 2 : 1) * st::semiboldFont->height;
		if (y >= trect.top() && y < trect.top() + fwdheight) {
			bool breakEverywhere = (fwd->_text.countHeight(trect.width()) > 2 * st::semiboldFont->height);
			auto textRequest = request.forText();
			if (breakEverywhere) {
				textRequest.flags |= Text::StateRequest::Flag::BreakEverywhere;
			}
			textstyleSet(&st::inFwdTextStyle);
			*outResult = fwd->_text.getState(x - trect.left(), y - trect.top(), trect.width(), textRequest);
			textstyleRestore();
			outResult->symbol = 0;
			outResult->afterSymbol = false;
			if (breakEverywhere) {
				outResult->cursor = HistoryInForwardedCursorState;
			} else {
				outResult->cursor = HistoryDefaultCursorState;
			}
			return true;
		}
		trect.setTop(trect.top() + fwdheight);
	}
	return false;
}

bool HistoryMessage::getStateReplyInfo(int x, int y, QRect &trect, HistoryTextState *outResult) const {
	if (auto reply = Get<HistoryMessageReply>()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		if (y >= trect.top() && y < trect.top() + h) {
			if (reply->replyToMsg && y >= trect.top() + st::msgReplyPadding.top() && y < trect.top() + st::msgReplyPadding.top() + st::msgReplyBarSize.height() && x >= trect.left() && x < trect.left() + trect.width()) {
				outResult->link = reply->replyToLink();
			}
			return true;
		}
		trect.setTop(trect.top() + h);
	}
	return false;
}

bool HistoryMessage::getStateViaBotIdInfo(int x, int y, QRect &trect, HistoryTextState *outResult) const {
	if (!displayFromName() && !Has<HistoryMessageForwarded>()) {
		if (auto via = Get<HistoryMessageVia>()) {
			if (x >= trect.left() && y >= trect.top() && y < trect.top() + st::msgNameFont->height && x < trect.left() + via->_width) {
				outResult->link = via->_lnk;
				return true;
			}
			trect.setTop(trect.top() + st::msgNameFont->height);
		}
	}
	return false;
}

bool HistoryMessage::getStateText(int x, int y, QRect &trect, HistoryTextState *outResult, const HistoryStateRequest &request) const {
	if (trect.contains(x, y)) {
		textstyleSet(&((out() && !isPost()) ? st::outTextStyle : st::inTextStyle));
		*outResult = _text.getState(x - trect.x(), y - trect.y(), trect.width(), request.forText());
		textstyleRestore();
		return true;
	}
	return false;
}

TextSelection HistoryMessage::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_media || selection.to <= _text.length()) {
		return _text.adjustSelection(selection, type);
	}
	auto mediaSelection = _media->adjustSelection(toMediaSelection(selection), type);
	if (selection.from >= _text.length()) {
		return fromMediaSelection(mediaSelection);
	}
	auto textSelection = _text.adjustSelection(selection, type);
	return { textSelection.from, fromMediaSelection(mediaSelection).to };
}

QString HistoryMessage::notificationHeader() const {
	return (!_history->peer->isUser() && !isPost()) ? from()->name : QString();
}

bool HistoryMessage::displayFromPhoto() const {
	return hasFromPhoto() && !isAttachedToPrevious();
}

bool HistoryMessage::hasFromPhoto() const {
	return (Adaptive::Wide() || (!out() && !history()->peer->isUser())) && !isPost() && !isEmpty();
}

HistoryMessage::~HistoryMessage() {
	_media.clear();
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->clearData(this);
	}
}

void HistoryService::setMessageByAction(const MTPmessageAction &action) {
	auto text = lang(lng_message_empty);
	auto from = textcmdLink(1, _from->name);

	Links links;
	links.push_back(MakeShared<PeerOpenClickHandler>(_from));

	switch (action.type()) {
	case mtpc_messageActionChatAddUser: {
		auto &d = action.c_messageActionChatAddUser();
		auto &v = d.vusers.c_vector().v;
		bool foundSelf = false;
		for (int i = 0, l = v.size(); i < l; ++i) {
			if (v.at(i).v == MTP::authedId()) {
				foundSelf = true;
				break;
			}
		}
		if (v.size() == 1) {
			auto u = App::user(peerFromUser(v.at(0)));
			if (u == _from) {
				text = lng_action_user_joined(lt_from, from);
			} else {
				links.push_back(MakeShared<PeerOpenClickHandler>(u));
				text = lng_action_add_user(lt_from, from, lt_user, textcmdLink(2, u->name));
			}
		} else if (v.isEmpty()) {
			text = lng_action_add_user(lt_from, from, lt_user, "somebody");
		} else {
			for (int i = 0, l = v.size(); i < l; ++i) {
				auto u = App::user(peerFromUser(v.at(i)));
				auto linkText = textcmdLink(i + 2, u->name);
				if (i == 0) {
					text = linkText;
				} else if (i + 1 < l) {
					text = lng_action_add_users_and_one(lt_accumulated, text, lt_user, linkText);
				} else {
					text = lng_action_add_users_and_last(lt_accumulated, text, lt_user, linkText);
				}
				links.push_back(MakeShared<PeerOpenClickHandler>(u));
			}
			text = lng_action_add_users_many(lt_from, from, lt_users, text);
		}
		if (foundSelf) {
			if (history()->peer->isMegagroup()) {
				history()->peer->asChannel()->mgInfo->joinedMessageFound = true;
			}
		}
	} break;

	case mtpc_messageActionChatJoinedByLink: {
		auto &d = action.c_messageActionChatJoinedByLink();
		//if (true || peerFromUser(d.vinviter_id) == _from->id) {
		text = lng_action_user_joined_by_link(lt_from, from);
		//} else {
		//	UserData *u = App::user(App::peerFromUser(d.vinviter_id));
		//	links.push_back(MakeShared<PeerOpenClickHandler>(u));
		//	text = lng_action_user_joined_by_link_from(lt_from, from, lt_inviter, textcmdLink(2, u->name));
		//}
		if (_from->isSelf() && history()->peer->isMegagroup()) {
			history()->peer->asChannel()->mgInfo->joinedMessageFound = true;
		}
	} break;

	case mtpc_messageActionChatCreate: {
		auto &d = action.c_messageActionChatCreate();
		text = lng_action_created_chat(lt_from, from, lt_title, textClean(qs(d.vtitle)));
	} break;

	case mtpc_messageActionChannelCreate: {
		auto &d = action.c_messageActionChannelCreate();
		if (isPost()) {
			text = lang(lng_action_created_channel);
		} else {
			text = lng_action_created_chat(lt_from, from, lt_title, textClean(qs(d.vtitle)));
		}
	} break;

	case mtpc_messageActionHistoryClear: {
		text = QString();
	} break;

	case mtpc_messageActionChatDeletePhoto: {
		text = isPost() ? lang(lng_action_removed_photo_channel) : lng_action_removed_photo(lt_from, from);
	} break;

	case mtpc_messageActionChatDeleteUser: {
		auto &d = action.c_messageActionChatDeleteUser();
		if (peerFromUser(d.vuser_id) == _from->id) {
			text = lng_action_user_left(lt_from, from);
		} else {
			auto u = App::user(peerFromUser(d.vuser_id));
			links.push_back(MakeShared<PeerOpenClickHandler>(u));
			text = lng_action_kick_user(lt_from, from, lt_user, textcmdLink(2, u->name));
		}
	} break;

	case mtpc_messageActionChatEditPhoto: {
		auto &d = action.c_messageActionChatEditPhoto();
		if (d.vphoto.type() == mtpc_photo) {
			_media.reset(new HistoryPhoto(this, history()->peer, d.vphoto.c_photo(), st::msgServicePhotoWidth));
		}
		text = isPost() ? lang(lng_action_changed_photo_channel) : lng_action_changed_photo(lt_from, from);
	} break;

	case mtpc_messageActionChatEditTitle: {
		auto &d = action.c_messageActionChatEditTitle();
		text = isPost() ? lng_action_changed_title_channel(lt_title, textClean(qs(d.vtitle))) : lng_action_changed_title(lt_from, from, lt_title, textClean(qs(d.vtitle)));
	} break;

	case mtpc_messageActionChatMigrateTo: {
		_flags |= MTPDmessage_ClientFlag::f_is_group_migrate;
		auto &d = action.c_messageActionChatMigrateTo();
		if (true/*PeerData *channel = App::channelLoaded(d.vchannel_id.v)*/) {
			text = lang(lng_action_group_migrate);
		} else {
			text = lang(lng_contacts_loading);
		}
	} break;

	case mtpc_messageActionChannelMigrateFrom: {
		_flags |= MTPDmessage_ClientFlag::f_is_group_migrate;
		auto &d = action.c_messageActionChannelMigrateFrom();
		if (true/*PeerData *chat = App::chatLoaded(d.vchat_id.v)*/) {
			text = lang(lng_action_group_migrate);
		} else {
			text = lang(lng_contacts_loading);
		}
	} break;

	case mtpc_messageActionPinMessage: {
		preparePinnedText(from, &text, &links);
	} break;

	case mtpc_messageActionGameScore: {
		prepareGameScoreText(from, &text, &links);
	} break;

	default: from = QString(); break;
	}

	setServiceText(text, links);
	for (int i = 0, count = links.size(); i != count; ++i) {
		_text.setLink(1 + i, links.at(i));
	}
}

bool HistoryService::updateDependent(bool force) {
	auto dependent = GetDependentData();
	t_assert(dependent != nullptr);

	if (!force) {
		if (!dependent->msgId || dependent->msg) {
			return true;
		}
	}

	if (!dependent->lnk) {
		dependent->lnk.reset(new GoToMessageClickHandler(history()->peer->id, dependent->msgId));
	}
	bool gotDependencyItem = false;
	if (!dependent->msg) {
		dependent->msg = App::histItemById(channelId(), dependent->msgId);
		if (dependent->msg) {
			App::historyRegDependency(this, dependent->msg);
			gotDependencyItem = true;
		}
	}
	if (dependent->msg) {
		updateDependentText();
	} else if (force) {
		if (dependent->msgId > 0) {
			dependent->msgId = 0;
			gotDependencyItem = true;
		}
		updateDependentText();
	}
	if (force) {
		if (gotDependencyItem && App::wnd()) {
			App::wnd()->notifySettingGot();
		}
	}
	return (dependent->msg || !dependent->msgId);
}

bool HistoryService::preparePinnedText(const QString &from, QString *outText, Links *outLinks) {
	bool result = false;
	QString text;

	ClickHandlerPtr second;
	auto pinned = Get<HistoryServicePinned>();
	if (pinned && pinned->msg) {
		auto media = pinned->msg->getMedia();
		QString mediaText;
		switch (media ? media->type() : MediaTypeCount) {
		case MediaTypePhoto: mediaText = lang(lng_action_pinned_media_photo); break;
		case MediaTypeVideo: mediaText = lang(lng_action_pinned_media_video); break;
		case MediaTypeContact: mediaText = lang(lng_action_pinned_media_contact); break;
		case MediaTypeFile: mediaText = lang(lng_action_pinned_media_file); break;
		case MediaTypeGif: mediaText = lang(lng_action_pinned_media_gif); break;
		case MediaTypeSticker: {
			auto emoji = static_cast<HistorySticker*>(media)->emoji();
			if (emoji.isEmpty()) {
				mediaText = lang(lng_action_pinned_media_sticker);
			} else {
				mediaText = lng_action_pinned_media_emoji_sticker(lt_emoji, emoji);
			}
		} break;
		case MediaTypeLocation: mediaText = lang(lng_action_pinned_media_location); break;
		case MediaTypeMusicFile: mediaText = lang(lng_action_pinned_media_audio); break;
		case MediaTypeVoiceFile: mediaText = lang(lng_action_pinned_media_voice); break;
		case MediaTypeGame: {
			auto title = static_cast<HistoryGame*>(media)->game()->title;
			mediaText = lng_action_pinned_media_game(lt_game, title);
		} break;
		}
		if (mediaText.isEmpty()) {
			QString original = pinned->msg->originalText().text;
			int32 cutat = 0, limit = PinnedMessageTextLimit, size = original.size();
			for (; limit > 0;) {
				--limit;
				if (cutat >= size) break;
				if (original.at(cutat).isLowSurrogate() && cutat + 1 < size && original.at(cutat + 1).isHighSurrogate()) {
					cutat += 2;
				} else {
					++cutat;
				}
			}
			if (!limit && cutat + 5 < size) {
				original = original.mid(0, cutat) + qstr("...");
			}
			text = lng_action_pinned_message(lt_from, from, lt_text, textcmdLink(2, original));
		} else {
			text = lng_action_pinned_media(lt_from, from, lt_media, textcmdLink(2, mediaText));
		}
		second = pinned->lnk;
		result = true;
	} else if (pinned && pinned->msgId) {
		text = lng_action_pinned_media(lt_from, from, lt_media, textcmdLink(2, lang(lng_contacts_loading)));
		second = pinned->lnk;
		result = true;
	} else {
		text = lng_action_pinned_media(lt_from, from, lt_media, lang(lng_deleted_message));
	}
	*outText = text;
	if (second) {
		outLinks->push_back(second);
	}
	return result;
}

bool HistoryService::prepareGameScoreText(const QString &from, QString *outText, Links *outLinks) {
	bool result = false;
	QString gameTitle;

	ClickHandlerPtr second;
	auto gamescore = Get<HistoryServiceGameScore>();
	if (gamescore && gamescore->msg) {
		auto getGameTitle = [item = gamescore->msg, &second]()->QString {
			if (auto media = item->getMedia()) {
				if (media->type() == MediaTypeGame) {
					second = MakeShared<ReplyMarkupClickHandler>(item, 0, 0);
					return textcmdLink(2, static_cast<HistoryGame*>(media)->game()->title);
				}
			}
			return lang(lng_deleted_message);
		};
		gameTitle = getGameTitle();
		result = true;
	} else if (gamescore && gamescore->msgId) {
		gameTitle = lang(lng_contacts_loading);
		result = true;
	} else {
		gameTitle = QString();
	}
	auto scoreNumber = gamescore ? gamescore->score : 0;
	if (_from->isSelf()) {
		if (gameTitle.isEmpty()) {
			*outText = lng_action_game_you_scored_no_game(lt_count, scoreNumber);
		} else {
			*outText = lng_action_game_you_scored(lt_count, scoreNumber, lt_game, gameTitle);
		}
	} else {
		if (gameTitle.isEmpty()) {
			*outText = lng_action_game_score_no_game(lt_from, from, lt_count, scoreNumber);
		} else {
			*outText = lng_action_game_score(lt_from, from, lt_count, scoreNumber, lt_game, gameTitle);
		}
	}
	if (second) {
		outLinks->push_back(second);
	}
	return result;
}

HistoryService::HistoryService(History *history, const MTPDmessageService &msg) :
	HistoryItem(history, msg.vid.v, mtpCastFlags(msg.vflags.v), ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0) {
	createFromMtp(msg);
	setMessageByAction(msg.vaction);
}

HistoryService::HistoryService(History *history, MsgId msgId, QDateTime date, const QString &msg, MTPDmessage::Flags flags, int32 from) :
	HistoryItem(history, msgId, flags, date, from) {
	setServiceText(msg, Links());
}

void HistoryService::initDimensions() {
	_maxw = _text.maxWidth() + st::msgServicePadding.left() + st::msgServicePadding.right();
	_minh = _text.minHeight();
	if (_media) _media->initDimensions();
}

bool HistoryService::updateDependencyItem() {
	if (GetDependentData()) {
		return updateDependent(true);
	}
	return HistoryItem::updateDependencyItem();
}

void HistoryService::countPositionAndSize(int32 &left, int32 &width) const {
	left = st::msgServiceMargin.left();
	int32 maxwidth = _history->width;
	if (Adaptive::Wide()) {
		maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
	}
	width = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();
}

TextWithEntities HistoryService::selectedText(TextSelection selection) const {
	return _text.originalTextWithEntities((selection == FullSelection) ? AllTextSelection : selection);
}

QString HistoryService::inDialogsText() const {
	return textcmdLink(1, textClean(notificationText()));
}

QString HistoryService::inReplyText() const {
	QString result = HistoryService::notificationText();
	return result.trimmed().startsWith(author()->name) ? result.trimmed().mid(author()->name.size()).trimmed() : result;
}

void HistoryService::setServiceText(const QString &text, const Links &links) {
	textstyleSet(&st::serviceTextStyle);
	_text.setText(st::msgServiceFont, text, _historySrvOptions);
	textstyleRestore();
	for (int i = 0, count = links.size(); i != count; ++i) {
		_text.setLink(1 + i, links.at(i));
	}

	setPendingInitDimensions();
	_textWidth = -1;
	_textHeight = 0;
}

void HistoryService::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	int height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom();

	QRect clip(r);
	int dateh = 0, unreadbarh = 0;
	if (auto date = Get<HistoryMessageDate>()) {
		dateh = date->height();
		//if (clip.intersects(QRect(0, 0, _history->width, dateh))) {
		//	date->paint(p, 0, _history->width);
		//}
		p.translate(0, dateh);
		clip.translate(0, -dateh);
		height -= dateh;
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		unreadbarh = unreadbar->height();
		if (clip.intersects(QRect(0, 0, _history->width, unreadbarh))) {
			unreadbar->paint(p, 0, _history->width);
		}
		p.translate(0, unreadbarh);
		clip.translate(0, -unreadbarh);
		height -= unreadbarh;
	}

	HistoryLayout::PaintContext context(ms, clip, selection);
	HistoryLayout::ServiceMessagePainter::paint(p, this, context, height);

	if (int skiph = dateh + unreadbarh) {
		p.translate(0, -skiph);
	}
}

int32 HistoryService::resizeGetHeight_(int32 width) {
	_height = displayedDateHeight();
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		_height += unreadbar->height();
	}

	if (_text.isEmpty()) {
		_textHeight = 0;
	} else {
		int32 maxwidth = _history->width;
		if (Adaptive::Wide()) {
			maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
		}
		if (width > maxwidth) width = maxwidth;
		width -= st::msgServiceMargin.left() + st::msgServiceMargin.left(); // two small margins
		if (width < st::msgServicePadding.left() + st::msgServicePadding.right() + 1) width = st::msgServicePadding.left() + st::msgServicePadding.right() + 1;

		int32 nwidth = qMax(width - st::msgServicePadding.left() - st::msgServicePadding.right(), 0);
		if (nwidth != _textWidth) {
			_textWidth = nwidth;
			textstyleSet(&st::serviceTextStyle);
			_textHeight = _text.countHeight(nwidth);
			textstyleRestore();
		}
		if (width >= _maxw) {
			_height += _minh;
		} else {
			_height += _textHeight;
		}
		_height += st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom();
		if (_media) {
			_height += st::msgServiceMargin.top() + _media->resizeGetHeight(_media->currentWidth());
		}
	}

	return _height;
}

bool HistoryService::hasPoint(int x, int y) const {
	int left = 0, width = 0, height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	countPositionAndSize(left, width);
	if (width < 1) return false;

	if (int dateh = displayedDateHeight()) {
		y -= dateh;
		height -= dateh;
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		int unreadbarh = unreadbar->height();
		y -= unreadbarh;
		height -= unreadbarh;
	}

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	return QRect(left, st::msgServiceMargin.top(), width, height).contains(x, y);
}

HistoryTextState HistoryService::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	int left = 0, width = 0, height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	countPositionAndSize(left, width);
	if (width < 1) return result;

	if (int dateh = displayedDateHeight()) {
		y -= dateh;
		height -= dateh;
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		int unreadbarh = unreadbar->height();
		y -= unreadbarh;
		height -= unreadbarh;
	}

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	auto outer = QRect(left, st::msgServiceMargin.top(), width, height);
	auto trect = outer.marginsAdded(-st::msgServicePadding);
	if (trect.contains(x, y)) {
		textstyleSet(&st::serviceTextStyle);
		auto textRequest = request.forText();
		textRequest.align = style::al_center;
		result = _text.getState(x - trect.x(), y - trect.y(), trect.width(), textRequest);
		textstyleRestore();
		if (auto gamescore = Get<HistoryServiceGameScore>()) {
			if (!result.link && result.cursor == HistoryInTextCursorState && outer.contains(x, y)) {
				result.link = gamescore->lnk;
			}
		}
	} else if (_media) {
		result = _media->getState(x - st::msgServiceMargin.left() - (width - _media->maxWidth()) / 2, y - st::msgServiceMargin.top() - height - st::msgServiceMargin.top(), request);
	}
	return result;
}

void HistoryService::createFromMtp(const MTPDmessageService &message) {
	if (message.vaction.type() == mtpc_messageActionGameScore) {
		UpdateComponents(HistoryServiceGameScore::Bit());
		Get<HistoryServiceGameScore>()->score = message.vaction.c_messageActionGameScore().vscore.v;
	}
	if (message.has_reply_to_msg_id()) {
		if (message.vaction.type() == mtpc_messageActionPinMessage) {
			UpdateComponents(HistoryServicePinned::Bit());
		}
		if (auto dependent = GetDependentData()) {
			dependent->msgId = message.vreply_to_msg_id.v;
			if (!updateDependent() && App::api()) {
				App::api()->requestMessageData(history()->peer->asChannel(), dependent->msgId, historyDependentItemCallback(fullId()));
			}
		}
	}
	setMessageByAction(message.vaction);
}

void HistoryService::applyEdition(const MTPDmessageService &message) {
	clearDependency();
	UpdateComponents(0);

	createFromMtp(message);

	if (message.vaction.type() == mtpc_messageActionHistoryClear) {
		removeMedia();
		finishEditionToEmpty();
	} else {
		finishEdition(-1);
	}
}

void HistoryService::removeMedia() {
	if (!_media) return;

	bool mediaWasDisplayed = _media->isDisplayed();
	_media.clear();
	if (mediaWasDisplayed) {
		_textWidth = -1;
		_textHeight = 0;
	}
}

int32 HistoryService::addToOverview(AddToOverviewMethod method) {
	if (!indexInOverview()) return 0;

	int32 result = 0;
	if (auto media = getMedia()) {
		MediaOverviewType type = serviceMediaToOverviewType(media);
		if (type != OverviewCount) {
			if (history()->addToOverview(type, id, method)) {
				result |= (1 << type);
			}
		}
	}
	return result;
}

void HistoryService::eraseFromOverview() {
	if (auto media = getMedia()) {
		MediaOverviewType type = serviceMediaToOverviewType(media);
		if (type != OverviewCount) {
			history()->eraseFromOverview(type, id);
		}
	}
}

bool HistoryService::updateDependentText() {
	auto result = false;
	auto from = textcmdLink(1, _from->name);
	QString text;
	Links links;
	links.push_back(MakeShared<PeerOpenClickHandler>(_from));
	if (Has<HistoryServicePinned>()) {
		result = preparePinnedText(from, &text, &links);
	} else if (Has<HistoryServiceGameScore>()) {
		result = prepareGameScoreText(from, &text, &links);
	} else {
		return result;
	}

	setServiceText(text, links);
	if (history()->textCachedFor == this) {
		history()->textCachedFor = 0;
	}
	if (App::main()) {
		App::main()->dlgUpdated(history(), id);
	}
	App::historyUpdateDependent(this);
	return result;
}

void HistoryService::clearDependency() {
	if (auto dependent = GetDependentData()) {
		if (dependent->msg) {
			App::historyUnregDependency(this, dependent->msg);
		}
	}
}

HistoryService::~HistoryService() {
	clearDependency();
	_media.clear();
}

HistoryJoined::HistoryJoined(History *history, const QDateTime &inviteDate, UserData *inviter, MTPDmessage::Flags flags)
	: HistoryService(history, clientMsgId(), inviteDate, QString(), flags) {
	Links links;
	auto text = ([history, inviter, &links]() {
		if (peerToUser(inviter->id) == MTP::authedId()) {
			return lang(history->isMegagroup() ? lng_action_you_joined_group : lng_action_you_joined);
		}
		links.push_back(MakeShared<PeerOpenClickHandler>(inviter));
		if (history->isMegagroup()) {
			return lng_action_add_you_group(lt_from, textcmdLink(1, inviter->name));
		}
		return lng_action_add_you(lt_from, textcmdLink(1, inviter->name));
	})();
	setServiceText(text, links);
}

void ViaInlineBotClickHandler::onClickImpl() const {
	App::insertBotCommand('@' + _bot->username);
}
