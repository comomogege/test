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
#include "history/history_item.h"

#include "lang.h"
#include "mainwidget.h"
#include "history/history_service_layout.h"
#include "media/media_clip_reader.h"
#include "styles/style_dialogs.h"
#include "fileuploader.h"

namespace {

// a new message from the same sender is attached to previous within 15 minutes
constexpr int kAttachMessageToPreviousSecondsDelta = 900;

} // namespace

ReplyMarkupClickHandler::ReplyMarkupClickHandler(const HistoryItem *item, int row, int col)
: _itemId(item->fullId())
, _row(row)
, _col(col) {
}

// Copy to clipboard support.
void ReplyMarkupClickHandler::copyToClipboard() const {
	if (auto button = getButton()) {
		if (button->type == HistoryMessageReplyMarkup::Button::Type::Url) {
			auto url = QString::fromUtf8(button->data);
			if (!url.isEmpty()) {
				QApplication::clipboard()->setText(url);
			}
		}
	}
}

QString ReplyMarkupClickHandler::copyToClipboardContextItemText() const {
	if (auto button = getButton()) {
		if (button->type == HistoryMessageReplyMarkup::Button::Type::Url) {
			return lang(lng_context_copy_link);
		}
	}
	return QString();
}

// Finds the corresponding button in the items markup struct.
// If the button is not found it returns nullptr.
// Note: it is possible that we will point to the different button
// than the one was used when constructing the handler, but not a big deal.
const HistoryMessageReplyMarkup::Button *ReplyMarkupClickHandler::getButton() const {
	if (auto item = App::histItemById(_itemId)) {
		if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (_row < markup->rows.size()) {
				auto &row = markup->rows.at(_row);
				if (_col < row.size()) {
					return &row.at(_col);
				}
			}
		}
	}
	return nullptr;
}

void ReplyMarkupClickHandler::onClickImpl() const {
	if (auto item = App::histItemById(_itemId)) {
		App::activateBotCommand(item, _row, _col);
	}
}

// Returns the full text of the corresponding button.
QString ReplyMarkupClickHandler::buttonText() const {
	if (auto button = getButton()) {
		return button->text;
	}
	return QString();
}

ReplyKeyboard::ReplyKeyboard(const HistoryItem *item, StylePtr &&s)
	: _item(item)
	, _a_selected(animation(this, &ReplyKeyboard::step_selected))
	, _st(std_::forward<StylePtr>(s)) {
	if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
		_rows.reserve(markup->rows.size());
		for (int i = 0, l = markup->rows.size(); i != l; ++i) {
			auto &row = markup->rows.at(i);
			int s = row.size();
			ButtonRow newRow(s, Button());
			for (int j = 0; j != s; ++j) {
				auto &button = newRow[j];
				auto str = row.at(j).text;
				button.type = row.at(j).type;
				button.link = MakeShared<ReplyMarkupClickHandler>(item, i, j);
				button.text.setText(_st->textFont(), textOneLine(str), _textPlainOptions);
				button.characters = str.isEmpty() ? 1 : str.size();
			}
			_rows.push_back(newRow);
		}
	}
}

void ReplyKeyboard::updateMessageId() {
	auto msgId = _item->fullId();
	for_const (auto &row, _rows) {
		for_const (auto &button, row) {
			button.link->setMessageId(msgId);
		}
	}

}

void ReplyKeyboard::resize(int width, int height) {
	_width = width;

	auto markup = _item->Get<HistoryMessageReplyMarkup>();
	float64 y = 0, buttonHeight = _rows.isEmpty() ? _st->buttonHeight() : (float64(height + _st->buttonSkip()) / _rows.size());
	for (auto &row : _rows) {
		int s = row.size();

		int widthForButtons = _width - ((s - 1) * _st->buttonSkip());
		int widthForText = widthForButtons;
		int widthOfText = 0;
		int maxMinButtonWidth = 0;
		for_const (auto &button, row) {
			widthOfText += qMax(button.text.maxWidth(), 1);
			int minButtonWidth = _st->minButtonWidth(button.type);
			widthForText -= minButtonWidth;
			accumulate_max(maxMinButtonWidth, minButtonWidth);
		}
		bool exact = (widthForText == widthOfText);
		bool enough = (widthForButtons - s * maxMinButtonWidth) >= widthOfText;

		float64 x = 0;
		for (Button &button : row) {
			int buttonw = qMax(button.text.maxWidth(), 1);
			float64 textw = buttonw, minw = _st->minButtonWidth(button.type);
			float64 w = textw;
			if (exact) {
				w += minw;
			} else if (enough) {
				w = (widthForButtons / float64(s));
				textw = w - minw;
			} else {
				textw = (widthForText / float64(s));
				w = minw + textw;
				accumulate_max(w, 2 * float64(_st->buttonPadding()));
			}

			int rectx = static_cast<int>(std::floor(x));
			int rectw = static_cast<int>(std::floor(x + w)) - rectx;
			button.rect = QRect(rectx, qRound(y), rectw, qRound(buttonHeight - _st->buttonSkip()));
			if (rtl()) button.rect.setX(_width - button.rect.x() - button.rect.width());
			x += w + _st->buttonSkip();

			button.link->setFullDisplayed(textw >= buttonw);
		}
		y += buttonHeight;
	}
}

bool ReplyKeyboard::isEnoughSpace(int width, const style::botKeyboardButton &st) const {
	for_const (auto &row, _rows) {
		int s = row.size();
		int widthLeft = width - ((s - 1) * st.margin + s * 2 * st.padding);
		for_const (auto &button, row) {
			widthLeft -= qMax(button.text.maxWidth(), 1);
			if (widthLeft < 0) {
				if (row.size() > 3) {
					return false;
				} else {
					break;
				}
			}
		}
	}
	return true;
}

void ReplyKeyboard::setStyle(StylePtr &&st) {
	_st = std_::move(st);
}

int ReplyKeyboard::naturalWidth() const {
	auto result = 0;
	for_const (auto &row, _rows) {
		auto maxMinButtonWidth = 0;
		for_const (auto &button, row) {
			accumulate_max(maxMinButtonWidth, _st->minButtonWidth(button.type));
		}
		auto rowMaxButtonWidth = 0;
		for_const (auto &button, row) {
			accumulate_max(rowMaxButtonWidth, qMax(button.text.maxWidth(), 1) + maxMinButtonWidth);
		}

		auto rowSize = row.size();
		accumulate_max(result, rowSize * rowMaxButtonWidth + (rowSize - 1) * _st->buttonSkip());
	}
	return result;
}

int ReplyKeyboard::naturalHeight() const {
	return (_rows.size() - 1) * _st->buttonSkip() + _rows.size() * _st->buttonHeight();
}

void ReplyKeyboard::paint(Painter &p, int outerWidth, const QRect &clip) const {
	t_assert(_st != nullptr);
	t_assert(_width > 0);

	_st->startPaint(p);
	for_const (auto &row, _rows) {
		for_const (auto &button, row) {
			QRect rect(button.rect);
			if (rect.y() >= clip.y() + clip.height()) return;
			if (rect.y() + rect.height() < clip.y()) continue;

			// just ignore the buttons that didn't layout well
			if (rect.x() + rect.width() > _width) break;

			_st->paintButton(p, outerWidth, button);
		}
	}
}

ClickHandlerPtr ReplyKeyboard::getState(int x, int y) const {
	t_assert(_width > 0);

	for_const (auto &row, _rows) {
		for_const (auto &button, row) {
			QRect rect(button.rect);

			// just ignore the buttons that didn't layout well
			if (rect.x() + rect.width() > _width) break;

			if (rect.contains(x, y)) {
				return button.link;
			}
		}
	}
	return ClickHandlerPtr();
}

void ReplyKeyboard::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!p) return;

	bool startAnimation = false;
	for (int i = 0, rows = _rows.size(); i != rows; ++i) {
		auto &row = _rows.at(i);
		for (int j = 0, cols = row.size(); j != cols; ++j) {
			if (row.at(j).link == p) {
				bool startAnimation = _animations.isEmpty();

				int indexForAnimation = i * MatrixRowShift + j + 1;
				if (!active) {
					indexForAnimation = -indexForAnimation;
				}

				_animations.remove(-indexForAnimation);
				if (!_animations.contains(indexForAnimation)) {
					_animations.insert(indexForAnimation, getms());
				}

				if (startAnimation && !_a_selected.animating()) {
					_a_selected.start();
				}
				return;
			}
		}
	}
}

void ReplyKeyboard::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	_st->repaint(_item);
}

void ReplyKeyboard::step_selected(uint64 ms, bool timer) {
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		int index = qAbs(i.key()) - 1, row = (index / MatrixRowShift), col = index % MatrixRowShift;
		float64 dt = float64(ms - i.value()) / st::botKbDuration;
		if (dt >= 1) {
			_rows[row][col].howMuchOver = (i.key() > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			_rows[row][col].howMuchOver = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}
	if (timer) _st->repaint(_item);
	if (_animations.isEmpty()) {
		_a_selected.stop();
	}
}

void ReplyKeyboard::clearSelection() {
	for (auto i = _animations.cbegin(), e = _animations.cend(); i != e; ++i) {
		int index = qAbs(i.key()) - 1, row = (index / MatrixRowShift), col = index % MatrixRowShift;
		_rows[row][col].howMuchOver = 0;
	}
	_animations.clear();
	_a_selected.stop();
}

void ReplyKeyboard::Style::paintButton(Painter &p, int outerWidth, const ReplyKeyboard::Button &button) const {
	const QRect &rect = button.rect;
	bool pressed = ClickHandler::showAsPressed(button.link);

	paintButtonBg(p, rect, pressed, button.howMuchOver);
	paintButtonIcon(p, rect, outerWidth, button.type);
	if (button.type == HistoryMessageReplyMarkup::Button::Type::Callback
		|| button.type == HistoryMessageReplyMarkup::Button::Type::Game) {
		if (auto data = button.link->getButton()) {
			if (data->requestId) {
				paintButtonLoading(p, rect);
			}
		}
	}

	int tx = rect.x(), tw = rect.width();
	if (tw >= st::botKbFont->elidew + _st->padding * 2) {
		tx += _st->padding;
		tw -= _st->padding * 2;
	} else if (tw > st::botKbFont->elidew) {
		tx += (tw - st::botKbFont->elidew) / 2;
		tw = st::botKbFont->elidew;
	}
	int textTop = rect.y() + (pressed ? _st->downTextTop : _st->textTop);
	button.text.drawElided(p, tx, textTop + ((rect.height() - _st->height) / 2), tw, 1, style::al_top);
}

void HistoryMessageReplyMarkup::createFromButtonRows(const QVector<MTPKeyboardButtonRow> &v) {
	if (v.isEmpty()) {
		rows.clear();
		return;
	}

	rows.reserve(v.size());
	for_const (auto &row, v) {
		switch (row.type()) {
		case mtpc_keyboardButtonRow: {
			auto &r = row.c_keyboardButtonRow();
			auto &b = r.vbuttons.c_vector().v;
			if (!b.isEmpty()) {
				ButtonRow buttonRow;
				buttonRow.reserve(b.size());
				for_const (auto &button, b) {
					switch (button.type()) {
					case mtpc_keyboardButton: {
						buttonRow.push_back({ Button::Type::Default, qs(button.c_keyboardButton().vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonCallback: {
						auto &buttonData = button.c_keyboardButtonCallback();
						buttonRow.push_back({ Button::Type::Callback, qs(buttonData.vtext), qba(buttonData.vdata), 0 });
					} break;
					case mtpc_keyboardButtonRequestGeoLocation: {
						buttonRow.push_back({ Button::Type::RequestLocation, qs(button.c_keyboardButtonRequestGeoLocation().vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonRequestPhone: {
						buttonRow.push_back({ Button::Type::RequestPhone, qs(button.c_keyboardButtonRequestPhone().vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonUrl: {
						auto &buttonData = button.c_keyboardButtonUrl();
						buttonRow.push_back({ Button::Type::Url, qs(buttonData.vtext), qba(buttonData.vurl), 0 });
					} break;
					case mtpc_keyboardButtonSwitchInline: {
						auto &buttonData = button.c_keyboardButtonSwitchInline();
						auto buttonType = buttonData.is_same_peer() ? Button::Type::SwitchInlineSame : Button::Type::SwitchInline;
						buttonRow.push_back({ buttonType, qs(buttonData.vtext), qba(buttonData.vquery), 0 });
						if (buttonType == Button::Type::SwitchInline) {
							// Optimization flag.
							// Fast check on all new messages if there is a switch button to auto-click it.
							flags |= MTPDreplyKeyboardMarkup_ClientFlag::f_has_switch_inline_button;
						}
					} break;
					case mtpc_keyboardButtonGame: {
						auto &buttonData = button.c_keyboardButtonGame();
						buttonRow.push_back({ Button::Type::Game, qs(buttonData.vtext), QByteArray(), 0 });
					} break;
					}
				}
				if (!buttonRow.isEmpty()) rows.push_back(buttonRow);
			}
		} break;
		}
	}
}

void HistoryMessageReplyMarkup::create(const MTPReplyMarkup &markup) {
	flags = 0;
	rows.clear();
	inlineKeyboard = nullptr;

	switch (markup.type()) {
	case mtpc_replyKeyboardMarkup: {
		auto &d = markup.c_replyKeyboardMarkup();
		flags = d.vflags.v;

		createFromButtonRows(d.vrows.c_vector().v);
	} break;

	case mtpc_replyInlineMarkup: {
		auto &d = markup.c_replyInlineMarkup();
		flags = MTPDreplyKeyboardMarkup::Flags(0) | MTPDreplyKeyboardMarkup_ClientFlag::f_inline;

		createFromButtonRows(d.vrows.c_vector().v);
	} break;

	case mtpc_replyKeyboardHide: {
		auto &d = markup.c_replyKeyboardHide();
		flags = mtpCastFlags(d.vflags) | MTPDreplyKeyboardMarkup_ClientFlag::f_zero;
	} break;

	case mtpc_replyKeyboardForceReply: {
		auto &d = markup.c_replyKeyboardForceReply();
		flags = mtpCastFlags(d.vflags) | MTPDreplyKeyboardMarkup_ClientFlag::f_force_reply;
	} break;
	}
}

void HistoryMessageReplyMarkup::create(const HistoryMessageReplyMarkup &markup) {
	flags = markup.flags;
	inlineKeyboard = nullptr;

	rows.clear();
	for_const (auto &row, markup.rows) {
		ButtonRow buttonRow;
		buttonRow.reserve(row.size());
		for_const (auto &button, row) {
			buttonRow.push_back({ button.type, button.text, button.data, 0 });
		}
		if (!buttonRow.isEmpty()) rows.push_back(buttonRow);
	}
}

void HistoryMessageUnreadBar::init(int count) {
	if (_freezed) return;
	_text = lng_unread_bar(lt_count, count);
	_width = st::semiboldFont->width(_text);
}

int HistoryMessageUnreadBar::height() {
	return st::unreadBarHeight + st::unreadBarMargin;
}

int HistoryMessageUnreadBar::marginTop() {
	return st::lineWidth + st::unreadBarMargin;
}

void HistoryMessageUnreadBar::paint(Painter &p, int y, int w) const {
	p.fillRect(0, y + marginTop(), w, height() - marginTop() - st::lineWidth, st::unreadBarBG);
	p.fillRect(0, y + height() - st::lineWidth, w, st::lineWidth, st::unreadBarBorder);
	p.setFont(st::unreadBarFont);
	p.setPen(st::unreadBarColor);

	int left = st::msgServiceMargin.left();
	int maxwidth = w;
	if (Adaptive::Wide()) {
		maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
	}
	w = maxwidth;

	p.drawText((w - _width) / 2, y + marginTop() + (st::unreadBarHeight - 2 * st::lineWidth - st::unreadBarFont->height) / 2 + st::unreadBarFont->ascent, _text);
}

void HistoryMessageDate::init(const QDateTime &date) {
	_text = langDayOfMonthFull(date.date());
	_width = st::msgServiceFont->width(_text);
}

int HistoryMessageDate::height() const {
	return st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom() + st::msgServiceMargin.bottom();
}

void HistoryMessageDate::paint(Painter &p, int y, int w) const {
	HistoryLayout::ServiceMessagePainter::paintDate(p, _text, _width, y, w);
}

void HistoryMediaPtr::reset(HistoryMedia *p) {
	if (_p) {
		_p->detachFromParent();
		delete _p;
	}
	_p = p;
	if (_p) {
		_p->attachToParent();
	}
}

namespace internal {

TextSelection unshiftSelection(TextSelection selection, const Text &byText) {
	if (selection == FullSelection) {
		return selection;
	}
	return ::unshiftSelection(selection, byText);
}

TextSelection shiftSelection(TextSelection selection, const Text &byText) {
	if (selection == FullSelection) {
		return selection;
	}
	return ::shiftSelection(selection, byText);
}

} // namespace internal

HistoryItem::HistoryItem(History *history, MsgId msgId, MTPDmessage::Flags flags, QDateTime msgDate, int32 from) : HistoryElement()
, y(0)
, id(msgId)
, date(msgDate)
, _from(from ? App::user(from) : history->peer)
, _history(history)
, _flags(flags | MTPDmessage_ClientFlag::f_pending_init_dimensions | MTPDmessage_ClientFlag::f_pending_resize)
, _authorNameVersion(author()->nameVersion) {
}

void HistoryItem::finishCreate() {
	App::historyRegItem(this);
}

void HistoryItem::finishEdition(int oldKeyboardTop) {
	setPendingInitDimensions();
	if (App::main()) {
		App::main()->dlgUpdated(history(), id);
	}

	// invalidate cache for drawInDialog
	if (history()->textCachedFor == this) {
		history()->textCachedFor = nullptr;
	}

	if (oldKeyboardTop >= 0) {
		if (auto keyboard = Get<HistoryMessageReplyMarkup>()) {
			keyboard->oldTop = oldKeyboardTop;
		}
	}

	App::historyUpdateDependent(this);
}

void HistoryItem::finishEditionToEmpty() {
	recountDisplayDate();
	finishEdition(-1);

	_history->removeNotification(this);
	if (history()->isChannel()) {
		if (history()->peer->isMegagroup() && history()->peer->asChannel()->mgInfo->pinnedMsgId == id) {
			history()->peer->asChannel()->mgInfo->pinnedMsgId = 0;
		}
	}
	if (history()->lastKeyboardId == id) {
		history()->clearLastKeyboard();
	}
	if ((!out() || isPost()) && unread() && history()->unreadCount() > 0) {
		history()->setUnreadCount(history()->unreadCount() - 1);
	}

	if (auto next = nextItem()) {
		next->previousItemChanged();
	}
}

void HistoryItem::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->clickHandlerActiveChanged(p, active);
		}
	}
	App::hoveredLinkItem(active ? this : nullptr);
	Ui::repaintHistoryItem(this);
}

void HistoryItem::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->clickHandlerPressedChanged(p, pressed);
		}
	}
	App::pressedLinkItem(pressed ? this : nullptr);
	Ui::repaintHistoryItem(this);
}

void HistoryItem::destroy() {
	// All this must be done for all items manually in History::clear(false)!
	eraseFromOverview();

	bool wasAtBottom = history()->loadedAtBottom();
	_history->removeNotification(this);
	detach();
	if (history()->isChannel()) {
		if (history()->peer->isMegagroup() && history()->peer->asChannel()->mgInfo->pinnedMsgId == id) {
			history()->peer->asChannel()->mgInfo->pinnedMsgId = 0;
		}
	}
	if (history()->lastMsg == this) {
		history()->fixLastMessage(wasAtBottom);
	}
	if (history()->lastKeyboardId == id) {
		history()->clearLastKeyboard();
	}
	if ((!out() || isPost()) && unread() && history()->unreadCount() > 0) {
		history()->setUnreadCount(history()->unreadCount() - 1);
	}
	Global::RefPendingRepaintItems().remove(this);
	delete this;
}

void HistoryItem::detach() {
	if (detached()) return;

	if (_history->isChannel()) {
		_history->asChannelHistory()->messageDetached(this);
	}
	_block->removeItem(this);
	App::historyItemDetached(this);

	_history->setPendingResize();
}

void HistoryItem::detachFast() {
	_block = nullptr;
	_indexInBlock = -1;
}

void HistoryItem::previousItemChanged() {
	recountDisplayDate();
	recountAttachToPrevious();
}

void HistoryItem::recountAttachToPrevious() {
	bool attach = false;
	if (!isPost() && !Has<HistoryMessageDate>() && !Has<HistoryMessageUnreadBar>()) {
		if (auto previos = previousItem()) {
			attach = !previos->isPost()
				&& !previos->serviceMsg()
				&& !previos->isEmpty()
				&& previos->from() == from()
				&& (qAbs(previos->date.secsTo(date)) < kAttachMessageToPreviousSecondsDelta);
		}
	}
	if (attach && !(_flags & MTPDmessage_ClientFlag::f_attach_to_previous)) {
		_flags |= MTPDmessage_ClientFlag::f_attach_to_previous;
		setPendingInitDimensions();
	} else if (!attach && (_flags & MTPDmessage_ClientFlag::f_attach_to_previous)) {
		_flags &= ~MTPDmessage_ClientFlag::f_attach_to_previous;
		setPendingInitDimensions();
	}
}

void HistoryItem::setId(MsgId newId) {
	history()->changeMsgId(id, newId);
	id = newId;

	// We don't need to call Notify::replyMarkupUpdated(this) and update keyboard
	// in history widget, because it can't exist for an outgoing message.
	// Only inline keyboards can be in outgoing messages.
	if (auto markup = inlineReplyMarkup()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->updateMessageId();
		}
	}
}

bool HistoryItem::canEdit(const QDateTime &cur) const {
	auto messageToMyself = (peerToUser(_history->peer->id) == MTP::authedId());
	auto messageTooOld = messageToMyself ? false : (date.secsTo(cur) >= Global::EditTimeLimit());
	if (id < 0 || messageTooOld) return false;

	if (auto msg = toHistoryMessage()) {
		if (msg->Has<HistoryMessageVia>() || msg->Has<HistoryMessageForwarded>()) return false;

		if (auto media = msg->getMedia()) {
			auto type = media->type();
			if (type != MediaTypePhoto &&
				type != MediaTypeVideo &&
				type != MediaTypeFile &&
				type != MediaTypeGif &&
				type != MediaTypeMusicFile &&
				type != MediaTypeVoiceFile &&
				type != MediaTypeWebPage) {
				return false;
			}
		}
		if (isPost()) {
			auto channel = _history->peer->asChannel();
			return (channel->amCreator() || (channel->amEditor() && out()));
		}
		return out() || messageToMyself;
	}
	return false;
}

bool HistoryItem::unread() const {
	// Messages from myself are always read.
	if (history()->peer->isSelf()) return false;

	if (out()) {
		// Outgoing messages in converted chats are always read.
		if (history()->peer->migrateTo()) return false;

		if (id > 0) {
			if (id < history()->outboxReadBefore) return false;
			if (auto user = history()->peer->asUser()) {
				if (user->botInfo) return false;
			} else if (auto channel = history()->peer->asChannel()) {
				if (!channel->isMegagroup()) return false;
			}
		}
		return true;
	}

	if (id > 0) {
		if (id < history()->inboxReadBefore) return false;
		return true;
	}
	return (_flags & MTPDmessage_ClientFlag::f_clientside_unread);
}

void HistoryItem::destroyUnreadBar() {
	if (Has<HistoryMessageUnreadBar>()) {
		RemoveComponents(HistoryMessageUnreadBar::Bit());
		setPendingInitDimensions();
		if (_history->unreadBar == this) {
			_history->unreadBar = nullptr;
		}

		recountAttachToPrevious();
	}
}

void HistoryItem::setUnreadBarCount(int count) {
	if (count > 0) {
		HistoryMessageUnreadBar *bar;
		if (!Has<HistoryMessageUnreadBar>()) {
			AddComponents(HistoryMessageUnreadBar::Bit());
			setPendingInitDimensions();

			recountAttachToPrevious();

			bar = Get<HistoryMessageUnreadBar>();
		} else {
			bar = Get<HistoryMessageUnreadBar>();
			if (bar->_freezed) {
				return;
			}
			Global::RefPendingRepaintItems().insert(this);
		}
		bar->init(count);
	} else {
		destroyUnreadBar();
	}
}

void HistoryItem::setUnreadBarFreezed() {
	if (auto bar = Get<HistoryMessageUnreadBar>()) {
		bar->_freezed = true;
	}
}

void HistoryItem::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;

	HistoryMedia *media = getMedia();
	if (!media) return;

	Reader *reader = media ? media->getClipReader() : 0;
	if (!reader) return;

	switch (notification) {
	case NotificationReinit: {
		bool stopped = false;
		if (reader->autoPausedGif()) {
			if (MainWidget *m = App::main()) {
				if (!m->isItemVisible(this)) { // stop animation if it is not visible
					media->stopInline();
					if (DocumentData *document = media->getDocument()) { // forget data from memory
						document->forget();
					}
					stopped = true;
				}
			}
		}
		if (!stopped) {
			setPendingInitDimensions();
			Notify::historyItemLayoutChanged(this);
		}
	} break;

	case NotificationRepaint: {
		if (!reader->currentDisplayed()) {
			Ui::repaintHistoryItem(this);
		}
	} break;
	}
}

void HistoryItem::recountDisplayDate() {
	bool displayingDate = ([this]() {
		if (isEmpty()) return false;

		if (auto previous = previousItem()) {
			return previous->isEmpty() || (previous->date.date() != date.date());
		}
		return true;
	})();

	if (displayingDate && !Has<HistoryMessageDate>()) {
		AddComponents(HistoryMessageDate::Bit());
		Get<HistoryMessageDate>()->init(date);
		setPendingInitDimensions();
	} else if (!displayingDate && Has<HistoryMessageDate>()) {
		RemoveComponents(HistoryMessageDate::Bit());
		setPendingInitDimensions();
	}
}

QString HistoryItem::notificationText() const {
	auto getText = [this]() {
		if (emptyText()) {
			return _media ? _media->notificationText() : QString();
		}
		return _text.originalText();
	};

	auto result = getText();
	if (result.size() > 0xFF) result = result.mid(0, 0xFF) + qsl("...");
	return result;
}

QString HistoryItem::inDialogsText() const {
	auto getText = [this]() {
		if (emptyText()) {
			return _media ? _media->inDialogsText() : QString();
		}
		return textClean(_text.originalText());
	};
	auto plainText = getText();
	if ((!_history->peer->isUser() || out()) && !isPost() && !isEmpty()) {
		auto fromText = author()->isSelf() ? lang(lng_from_you) : author()->shortName();
		auto fromWrapped = textcmdLink(1, lng_dialogs_text_from_wrapped(lt_from, textClean(fromText)));
		return lng_dialogs_text_with_from(lt_from_part, fromWrapped, lt_message, plainText);
	}
	return plainText;
}

void HistoryItem::drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const {
	if (cacheFor != this) {
		cacheFor = this;
		cache.setText(st::dialogsTextFont, inDialogsText(), _textDlgOptions);
	}
	if (r.width()) {
		textstyleSet(&(act ? st::dialogsTextStyleActive : st::dialogsTextStyle));
		p.setFont(st::dialogsTextFont);
		p.setPen(act ? st::dialogsTextFgActive : st::dialogsTextFg);
		cache.drawElided(p, r.left(), r.top(), r.width(), r.height() / st::dialogsTextFont->height);
		textstyleRestore();
	}
}

HistoryItem::~HistoryItem() {
	App::historyUnregItem(this);
	if (id < 0 && App::uploader()) {
		App::uploader()->cancel(fullId());
	}
}

void GoToMessageClickHandler::onClickImpl() const {
	if (App::main()) {
		HistoryItem *current = App::mousedItem();
		if (current && current->history()->peer->id == peer()) {
			App::main()->pushReplyReturn(current);
		}
		Ui::showPeerHistory(peer(), msgid(), Ui::ShowWay::Forward);
	}
}
