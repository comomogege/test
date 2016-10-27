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
#include "boxes/sharebox.h"

#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_boxes.h"
#include "styles/style_history.h"
#include "observer_peer.h"
#include "lang.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/qthelp_url.h"
#include "localstorage.h"
#include "boxes/confirmbox.h"
#include "apiwrap.h"
#include "ui/toast/toast.h"
#include "ui/widgets/multi_select.h"
#include "history/history_media_types.h"
#include "boxes/contactsbox.h"

ShareBox::ShareBox(CopyCallback &&copyCallback, SubmitCallback &&submitCallback, FilterCallback &&filterCallback) : ItemListBox(st::boxScroll)
, _copyCallback(std_::move(copyCallback))
, _submitCallback(std_::move(submitCallback))
, _inner(this, std_::move(filterCallback))
, _select(this, st::contactsMultiSelect, lang(lng_participant_filter))
, _copy(this, lang(lng_share_copy_link), st::defaultBoxButton)
, _share(this, lang(lng_share_confirm), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _topShadow(this)
, _bottomShadow(this) {
	_select->resizeToWidth(st::boxWideWidth);
	myEnsureResized(_select);

	auto topSkip = getTopScrollSkip();
	auto bottomSkip = st::boxButtonPadding.top() + _share->height() + st::boxButtonPadding.bottom();
	init(_inner, bottomSkip, topSkip);

	connect(_inner, SIGNAL(mustScrollTo(int,int)), this, SLOT(onMustScrollTo(int,int)));
	connect(_copy, SIGNAL(clicked()), this, SLOT(onCopyLink()));
	connect(_share, SIGNAL(clicked()), this, SLOT(onSubmit()));
	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(scrollArea(), SIGNAL(scrolled()), this, SLOT(onScroll()));
	_select->setQueryChangedCallback([this](const QString &query) { onFilterUpdate(query); });
	_select->setItemRemovedCallback([this](uint64 itemId) {
		if (auto peer = App::peerLoaded(itemId)) {
			_inner->peerUnselected(peer);
			onSelectedChanged();
			update();
		}
	});
	_select->setResizedCallback([this] { updateScrollSkips(); });
	_select->setSubmittedCallback([this](bool) { _inner->onSelectActive(); });
	connect(_inner, SIGNAL(searchByUsername()), this, SLOT(onNeedSearchByUsername()));
	_inner->setPeerSelectedChangedCallback([this](PeerData *peer, bool checked) {
		onPeerSelectedChanged(peer, checked);
	});

	_searchTimer.setSingleShot(true);
	connect(&_searchTimer, SIGNAL(timeout()), this, SLOT(onSearchByUsername()));

	updateButtonsVisibility();

	prepare();
}

int ShareBox::getTopScrollSkip() const {
	auto result = st::boxTitleHeight;
	if (!_select->isHidden()) {
		result += _select->height();
	}
	return result;
}

void ShareBox::updateScrollSkips() {
	auto oldScrollHeight = scrollArea()->height();
	auto topSkip = getTopScrollSkip();
	auto bottomSkip = st::boxButtonPadding.top() + _share->height() + st::boxButtonPadding.bottom();
	setScrollSkips(bottomSkip, topSkip);
	auto scrollHeightDelta = scrollArea()->height() - oldScrollHeight;
	if (scrollHeightDelta) {
		scrollArea()->scrollToY(scrollArea()->scrollTop() - scrollHeightDelta);
	}

	_topShadow->setGeometry(0, topSkip, width(), st::lineWidth);
}

bool ShareBox::onSearchByUsername(bool searchCache) {
	auto query = _select->getQuery();
	if (query.isEmpty()) {
		if (_peopleRequest) {
			_peopleRequest = 0;
		}
		return true;
	}
	if (query.size() >= MinUsernameLength) {
		if (searchCache) {
			auto i = _peopleCache.constFind(query);
			if (i != _peopleCache.cend()) {
				_peopleQuery = query;
				_peopleRequest = 0;
				peopleReceived(i.value(), 0);
				return true;
			}
		} else if (_peopleQuery != query) {
			_peopleQuery = query;
			_peopleFull = false;
			_peopleRequest = MTP::send(MTPcontacts_Search(MTP_string(_peopleQuery), MTP_int(SearchPeopleLimit)), rpcDone(&ShareBox::peopleReceived), rpcFail(&ShareBox::peopleFailed));
			_peopleQueries.insert(_peopleRequest, _peopleQuery);
		}
	}
	return false;
}

void ShareBox::onNeedSearchByUsername() {
	if (!onSearchByUsername(true)) {
		_searchTimer.start(AutoSearchTimeout);
	}
}

void ShareBox::peopleReceived(const MTPcontacts_Found &result, mtpRequestId requestId) {
	auto query = _peopleQuery;

	auto i = _peopleQueries.find(requestId);
	if (i != _peopleQueries.cend()) {
		query = i.value();
		_peopleCache[query] = result;
		_peopleQueries.erase(i);
	}

	if (_peopleRequest == requestId) {
		switch (result.type()) {
		case mtpc_contacts_found: {
			auto &found = result.c_contacts_found();
			App::feedUsers(found.vusers);
			App::feedChats(found.vchats);
			_inner->peopleReceived(query, found.vresults.c_vector().v);
		} break;
		}

		_peopleRequest = 0;
		onScroll();
	}
}

bool ShareBox::peopleFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_peopleRequest == requestId) {
		_peopleRequest = 0;
		_peopleFull = true;
	}
	return true;
}

void ShareBox::doSetInnerFocus() {
	_select->setInnerFocus();
}

void ShareBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_share_title));
}

void ShareBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, st::boxTitleHeight);

	updateScrollSkips();

	_inner->resizeToWidth(width());
	moveButtons();
	_topShadow->setGeometry(0, getTopScrollSkip(), width(), st::lineWidth);
	_bottomShadow->setGeometry(0, height() - st::boxButtonPadding.bottom() - _share->height() - st::boxButtonPadding.top() - st::lineWidth, width(), st::lineWidth);
}

void ShareBox::keyPressEvent(QKeyEvent *e) {
	auto focused = focusWidget();
	if (_select == focused || _select->isAncestorOf(focusWidget())) {
		if (e->key() == Qt::Key_Up) {
			_inner->activateSkipColumn(-1);
		} else if (e->key() == Qt::Key_Down) {
			_inner->activateSkipColumn(1);
		} else if (e->key() == Qt::Key_PageUp) {
			_inner->activateSkipPage(scrollArea()->height(), -1);
		} else if (e->key() == Qt::Key_PageDown) {
			_inner->activateSkipPage(scrollArea()->height(), 1);
		} else {
			ItemListBox::keyPressEvent(e);
		}
	} else {
		ItemListBox::keyPressEvent(e);
	}
}

void ShareBox::moveButtons() {
	_copy->moveToRight(st::boxButtonPadding.right(), _share->y());
	_share->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _share->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _share->width() + st::boxButtonPadding.left(), _share->y());
}

void ShareBox::updateButtonsVisibility() {
	auto hasSelected = _inner->hasSelected();
	_copy->setVisible(!hasSelected);
	_share->setVisible(hasSelected);
	_cancel->setVisible(hasSelected);
}

void ShareBox::onFilterUpdate(const QString &query) {
	scrollArea()->scrollToY(0);
	_inner->updateFilter(query);
}

void ShareBox::addPeerToMultiSelect(PeerData *peer, bool skipAnimation) {
	using AddItemWay = Ui::MultiSelect::AddItemWay;
	auto addItemWay = skipAnimation ? AddItemWay::SkipAnimation : AddItemWay::Default;
	_select->addItem(peer->id, peer->shortName(), st::windowActiveBg, PaintUserpicCallback(peer), addItemWay);
}

void ShareBox::onPeerSelectedChanged(PeerData *peer, bool checked) {
	if (checked) {
		addPeerToMultiSelect(peer);
		_select->clearQuery();
	} else {
		_select->removeItem(peer->id);
	}
	onSelectedChanged();
	update();
}

void ShareBox::onSubmit() {
	if (_submitCallback) {
		_submitCallback(_inner->selected());
	}
}

void ShareBox::onCopyLink() {
	if (_copyCallback) {
		_copyCallback();
	}
}

void ShareBox::onSelectedChanged() {
	updateButtonsVisibility();
	moveButtons();
	update();
}

void ShareBox::onMustScrollTo(int top, int bottom) {
	auto scrollTop = scrollArea()->scrollTop(), scrollBottom = scrollTop + scrollArea()->height();
	auto from = scrollTop, to = scrollTop;
	if (scrollTop > top) {
		to = top;
	} else if (scrollBottom < bottom) {
		to = bottom - (scrollBottom - scrollTop);
	}
	if (from != to) {
		_scrollAnimation.start([this]() {
			scrollArea()->scrollToY(_scrollAnimation.current(scrollArea()->scrollTop()));
		}, from, to, st::shareScrollDuration, anim::sineInOut);
	}
}

void ShareBox::onScroll() {
	auto scroll = scrollArea();
	auto scrollTop = scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + scroll->height());
}

ShareBox::Inner::Inner(QWidget *parent, ShareBox::FilterCallback &&filterCallback) : ScrolledWidget(parent)
, _filterCallback(std_::move(filterCallback))
, _chatsIndexed(std_::make_unique<Dialogs::IndexedList>(Dialogs::SortMode::Add)) {
	_rowsTop = st::shareRowsTop;
	_rowHeight = st::shareRowHeight;
	setAttribute(Qt::WA_OpaquePaintEvent);

	auto dialogs = App::main()->dialogsList();
	for_const (auto row, dialogs->all()) {
		auto history = row->history();
		if (_filterCallback(history->peer)) {
			_chatsIndexed->addToEnd(history);
		}
	}

	_filter = qsl("a");
	updateFilter();

	using UpdateFlag = Notify::PeerUpdate::Flag;
	auto observeEvents = UpdateFlag::NameChanged | UpdateFlag::PhotoChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));
	subscribe(FileDownload::ImageLoaded(), [this] { update(); });
}

void ShareBox::Inner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	loadProfilePhotos(visibleTop);
}

void ShareBox::Inner::activateSkipRow(int direction) {
	activateSkipColumn(direction * _columnCount);
}

int ShareBox::Inner::displayedChatsCount() const {
	return _filter.isEmpty() ? _chatsIndexed->size() : (_filtered.size() + d_byUsernameFiltered.size());
}

void ShareBox::Inner::activateSkipColumn(int direction) {
	if (_active < 0) {
		if (direction > 0) {
			setActive(0);
		}
		return;
	}
	auto count = displayedChatsCount();
	auto active = _active + direction;
	if (active < 0) {
		active = (_active > 0) ? 0 : -1;
	}
	if (active >= count) {
		active = count - 1;
	}
	setActive(active);
}

void ShareBox::Inner::activateSkipPage(int pageHeight, int direction) {
	activateSkipRow(direction * (pageHeight / _rowHeight));
}

void ShareBox::Inner::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.flags & Notify::PeerUpdate::Flag::NameChanged) {
		_chatsIndexed->peerNameChanged(update.peer, update.oldNames, update.oldNameFirstChars);
	}

	updateChat(update.peer);
}

void ShareBox::Inner::updateChat(PeerData *peer) {
	auto i = _dataMap.find(peer);
	if (i != _dataMap.cend()) {
		updateChatName(i.value(), peer);
		repaintChat(peer);
	}
}

void ShareBox::Inner::updateChatName(Chat *chat, PeerData *peer) {
	chat->name.setText(st::shareNameFont, peer->name, _textNameOptions);
}

void ShareBox::Inner::repaintChatAtIndex(int index) {
	if (index < 0) return;

	auto row = index / _columnCount;
	auto column = index % _columnCount;
	update(rtlrect(_rowsLeft + qFloor(column * _rowWidthReal), row * _rowHeight, _rowWidth, _rowHeight, width()));
}

ShareBox::Inner::Chat *ShareBox::Inner::getChatAtIndex(int index) {
	if (index < 0) return nullptr;
	auto row = ([this, index]() -> Dialogs::Row* {
		if (_filter.isEmpty()) return _chatsIndexed->rowAtY(index, 1);
		return (index < _filtered.size()) ? _filtered[index] : nullptr;
	})();
	if (row) {
		return static_cast<Chat*>(row->attached);
	}

	if (!_filter.isEmpty()) {
		index -= _filtered.size();
		if (index >= 0 && index < d_byUsernameFiltered.size()) {
			return d_byUsernameFiltered[index];
		}
	}
	return nullptr;
}

void ShareBox::Inner::repaintChat(PeerData *peer) {
	repaintChatAtIndex(chatIndex(peer));
}

int ShareBox::Inner::chatIndex(PeerData *peer) const {
	int index = 0;
	if (_filter.isEmpty()) {
		for_const (auto row, _chatsIndexed->all()) {
			if (row->history()->peer == peer) {
				return index;
			}
			++index;
		}
	} else {
		for_const (auto row, _filtered) {
			if (row->history()->peer == peer) {
				return index;
			}
			++index;
		}
		for_const (auto row, d_byUsernameFiltered) {
			if (row->peer == peer) {
				return index;
			}
			++index;
		}
	}
	return -1;
}

void ShareBox::Inner::loadProfilePhotos(int yFrom) {
	if (yFrom < 0) {
		yFrom = 0;
	}
	if (auto part = (yFrom % _rowHeight)) {
		yFrom -= part;
	}
	int yTo = yFrom + (parentWidget() ? parentWidget()->height() : App::wnd()->height()) * 5 * _columnCount;
	if (!yTo) {
		return;
	}
	yFrom *= _columnCount;
	yTo *= _columnCount;

	MTP::clearLoaderPriorities();
	if (_filter.isEmpty()) {
		if (!_chatsIndexed->isEmpty()) {
			auto i = _chatsIndexed->cfind(yFrom, _rowHeight);
			for (auto end = _chatsIndexed->cend(); i != end; ++i) {
				if (((*i)->pos() * _rowHeight) >= yTo) {
					break;
				}
				(*i)->history()->peer->loadUserpic();
			}
		}
	} else if (!_filtered.isEmpty()) {
		int from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < _filtered.size()) {
			int to = (yTo / _rowHeight) + 1;
			if (to > _filtered.size()) to = _filtered.size();

			for (; from < to; ++from) {
				_filtered[from]->history()->peer->loadUserpic();
			}
		}
	}
}

ShareBox::Inner::Chat *ShareBox::Inner::getChat(Dialogs::Row *row) {
	auto data = static_cast<Chat*>(row->attached);
	if (!data) {
		auto peer = row->history()->peer;
		auto i = _dataMap.constFind(peer);
		if (i == _dataMap.cend()) {
			data = new Chat(peer, [this, peer] { repaintChat(peer); });
			_dataMap.insert(peer, data);
			updateChatName(data, peer);
		} else {
			data = i.value();
		}
		row->attached = data;
	}
	return data;
}

void ShareBox::Inner::setActive(int active) {
	if (active != _active) {
		auto changeNameFg = [this](int index, style::color from, style::color to) {
			if (auto chat = getChatAtIndex(index)) {
				chat->nameFg.start([this, peer = chat->peer] {
					repaintChat(peer);
				}, from->c, to->c, st::shareActivateDuration);
			}
		};
		changeNameFg(_active, st::shareNameActiveFg, st::shareNameFg);
		_active = active;
		changeNameFg(_active, st::shareNameFg, st::shareNameActiveFg);
	}
	auto y = (_active < _columnCount) ? 0 : (_rowsTop + ((_active / _columnCount) * _rowHeight));
	emit mustScrollTo(y, y + _rowHeight);
}

void ShareBox::Inner::paintChat(Painter &p, uint64 ms, Chat *chat, int index) {
	auto x = _rowsLeft + qFloor((index % _columnCount) * _rowWidthReal);
	auto y = _rowsTop + (index / _columnCount) * _rowHeight;

	auto outerWidth = width();
	auto photoLeft = (_rowWidth - (st::sharePhotoCheckbox.imageRadius * 2)) / 2;
	auto photoTop = st::sharePhotoTop;
	chat->checkbox.paint(p, ms, x + photoLeft, y + photoTop, outerWidth);

	if (chat->nameFg.animating()) {
		p.setPen(chat->nameFg.current());
	} else {
		p.setPen((index == _active) ? st::shareNameActiveFg : st::shareNameFg);
	}

	auto nameWidth = (_rowWidth - st::shareColumnSkip);
	auto nameLeft = st::shareColumnSkip / 2;
	auto nameTop = photoTop + st::sharePhotoCheckbox.imageRadius * 2 + st::shareNameTop;
	chat->name.drawLeftElided(p, x + nameLeft, y + nameTop, nameWidth, outerWidth, 2, style::al_top, 0, -1, 0, true);
}

ShareBox::Inner::Chat::Chat(PeerData *peer, base::lambda_wrap<void()> updateCallback)
: peer(peer)
, checkbox(st::sharePhotoCheckbox, std_::move(updateCallback), PaintUserpicCallback(peer))
, name(st::sharePhotoCheckbox.imageRadius * 2) {
}

void ShareBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto r = e->rect();
	p.setClipRect(r);
	p.fillRect(r, st::white);
	auto yFrom = r.y(), yTo = r.y() + r.height();
	auto rowFrom = yFrom / _rowHeight;
	auto rowTo = (yTo + _rowHeight - 1) / _rowHeight;
	auto indexFrom = rowFrom * _columnCount;
	auto indexTo = rowTo * _columnCount;
	if (_filter.isEmpty()) {
		if (!_chatsIndexed->isEmpty()) {
			auto i = _chatsIndexed->cfind(indexFrom, 1);
			for (auto end = _chatsIndexed->cend(); i != end; ++i) {
				if (indexFrom >= indexTo) {
					break;
				}
				paintChat(p, ms, getChat(*i), indexFrom);
				++indexFrom;
			}
		} else {
			// empty
			p.setFont(st::noContactsFont);
			p.setPen(st::noContactsColor);
		}
	} else {
		if (_filtered.isEmpty() && _byUsernameFiltered.isEmpty()) {
			// empty
			p.setFont(st::noContactsFont);
			p.setPen(st::noContactsColor);
		} else {
			auto filteredSize = _filtered.size();
			if (filteredSize) {
				if (indexFrom < 0) indexFrom = 0;
				while (indexFrom < indexTo) {
					if (indexFrom >= _filtered.size()) {
						break;
					}
					paintChat(p, ms, getChat(_filtered[indexFrom]), indexFrom);
					++indexFrom;
				}
				indexFrom -= filteredSize;
				indexTo -= filteredSize;
			}
			if (!_byUsernameFiltered.isEmpty()) {
				if (indexFrom < 0) indexFrom = 0;
				while (indexFrom < indexTo) {
					if (indexFrom >= d_byUsernameFiltered.size()) {
						break;
					}
					paintChat(p, ms, d_byUsernameFiltered[indexFrom], filteredSize + indexFrom);
					++indexFrom;
				}
			}
		}
	}
}

void ShareBox::Inner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void ShareBox::Inner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
}

void ShareBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateUpon(e->pos());
	setCursor((_upon >= 0) ? style::cur_pointer : style::cur_default);
}

void ShareBox::Inner::updateUpon(const QPoint &pos) {
	auto x = pos.x(), y = pos.y();
	auto row = (y - _rowsTop) / _rowHeight;
	auto column = qFloor((x - _rowsLeft) / _rowWidthReal);
	auto left = _rowsLeft + qFloor(column * _rowWidthReal) + st::shareColumnSkip / 2;
	auto top = _rowsTop + row * _rowHeight + st::sharePhotoTop;
	auto xupon = (x >= left) && (x < left + (_rowWidth - st::shareColumnSkip));
	auto yupon = (y >= top) && (y < top + st::sharePhotoCheckbox.imageRadius * 2 + st::shareNameTop + st::shareNameFont->height * 2);
	auto upon = (xupon && yupon) ? (row * _columnCount + column) : -1;
	if (upon >= displayedChatsCount()) {
		upon = -1;
	}
	_upon = upon;
}

void ShareBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		updateUpon(e->pos());
		changeCheckState(getChatAtIndex(_upon));
	}
}

void ShareBox::Inner::onSelectActive() {
	changeCheckState(getChatAtIndex(_active > 0 ? _active : 0));
}

void ShareBox::Inner::resizeEvent(QResizeEvent *e) {
	_columnSkip = (width() - _columnCount * st::sharePhotoCheckbox.imageRadius * 2) / float64(_columnCount + 1);
	_rowWidthReal = st::sharePhotoCheckbox.imageRadius * 2 + _columnSkip;
	_rowsLeft = qFloor(_columnSkip / 2);
	_rowWidth = qFloor(_rowWidthReal);
	update();
}

void ShareBox::Inner::changeCheckState(Chat *chat) {
	if (!chat) return;

	if (!_filter.isEmpty()) {
		auto row = _chatsIndexed->getRow(chat->peer->id);
		if (!row) {
			row = _chatsIndexed->addToEnd(App::history(chat->peer)).value(0);
		}
		chat = getChat(row);
		if (!chat->checkbox.checked()) {
			_chatsIndexed->moveToTop(chat->peer);
		}
	}

	changePeerCheckState(chat, !chat->checkbox.checked());
}

void ShareBox::Inner::peerUnselected(PeerData *peer) {
	// If data is nullptr we simply won't do anything.
	auto chat = _dataMap.value(peer, nullptr);
	changePeerCheckState(chat, false, ChangeStateWay::SkipCallback);
}

void ShareBox::Inner::setPeerSelectedChangedCallback(base::lambda_unique<void(PeerData *peer, bool selected)> callback) {
	_peerSelectedChangedCallback = std_::move(callback);
}

void ShareBox::Inner::changePeerCheckState(Chat *chat, bool checked, ChangeStateWay useCallback) {
	if (chat) {
		chat->checkbox.setChecked(checked);
	}
	if (checked) {
		_selected.insert(chat->peer);
		setActive(chatIndex(chat->peer));
	} else {
		_selected.remove(chat->peer);
	}
	if (useCallback != ChangeStateWay::SkipCallback && _peerSelectedChangedCallback) {
		_peerSelectedChangedCallback(chat->peer, checked);
	}
}

bool ShareBox::Inner::hasSelected() const {
	return _selected.size();
}

void ShareBox::Inner::updateFilter(QString filter) {
	_lastQuery = filter.toLower().trimmed();
	filter = textSearchKey(filter);

	QStringList f;
	if (!filter.isEmpty()) {
		QStringList filterList = filter.split(cWordSplit(), QString::SkipEmptyParts);
		int l = filterList.size();

		f.reserve(l);
		for (int i = 0; i < l; ++i) {
			QString filterName = filterList[i].trimmed();
			if (filterName.isEmpty()) continue;
			f.push_back(filterName);
		}
		filter = f.join(' ');
	}
	if (_filter != filter) {
		_filter = filter;

		_byUsernameFiltered.clear();
		for (int i = 0, l = d_byUsernameFiltered.size(); i < l; ++i) {
			delete d_byUsernameFiltered[i];
		}
		d_byUsernameFiltered.clear();

		if (_filter.isEmpty()) {
			refresh();
		} else {
			QStringList::const_iterator fb = f.cbegin(), fe = f.cend(), fi;

			_filtered.clear();
			if (!f.isEmpty()) {
				const Dialogs::List *toFilter = nullptr;
				if (!_chatsIndexed->isEmpty()) {
					for (fi = fb; fi != fe; ++fi) {
						auto found = _chatsIndexed->filtered(fi->at(0));
						if (found->isEmpty()) {
							toFilter = nullptr;
							break;
						}
						if (!toFilter || toFilter->size() > found->size()) {
							toFilter = found;
						}
					}
				}
				if (toFilter) {
					_filtered.reserve(toFilter->size());
					for_const (auto row, *toFilter) {
						auto &names = row->history()->peer->names;
						PeerData::Names::const_iterator nb = names.cbegin(), ne = names.cend(), ni;
						for (fi = fb; fi != fe; ++fi) {
							auto filterName = *fi;
							for (ni = nb; ni != ne; ++ni) {
								if (ni->startsWith(*fi)) {
									break;
								}
							}
							if (ni == ne) {
								break;
							}
						}
						if (fi == fe) {
							_filtered.push_back(row);
						}
					}
				}
			}
			refresh();

			_searching = true;
			emit searchByUsername();
		}
		setActive(-1);
		update();
		loadProfilePhotos(0);
	}
}

void ShareBox::Inner::peopleReceived(const QString &query, const QVector<MTPPeer> &people) {
	_lastQuery = query.toLower().trimmed();
	if (_lastQuery.at(0) == '@') _lastQuery = _lastQuery.mid(1);
	int32 already = _byUsernameFiltered.size();
	_byUsernameFiltered.reserve(already + people.size());
	d_byUsernameFiltered.reserve(already + people.size());
	for_const (auto &mtpPeer, people) {
		auto peerId = peerFromMTP(mtpPeer);
		int j = 0;
		for (; j < already; ++j) {
			if (_byUsernameFiltered[j]->id == peerId) break;
		}
		if (j == already) {
			auto *peer = App::peer(peerId);
			if (!peer || !_filterCallback(peer)) continue;

			auto chat = new Chat(peer, [this, peer] { repaintChat(peer); });
			updateChatName(chat, peer);
			if (auto row = _chatsIndexed->getRow(peer->id)) {
				continue;
			}

			_byUsernameFiltered.push_back(peer);
			d_byUsernameFiltered.push_back(chat);
		}
	}
	_searching = false;
	refresh();
}

void ShareBox::Inner::refresh() {
	auto count = displayedChatsCount();
	if (count) {
		auto rows = (count / _columnCount) + (count % _columnCount ? 1 : 0);
		resize(width(), _rowsTop + rows * _rowHeight);
	} else {
		resize(width(), st::noContactsHeight);
	}
	update();
}

ShareBox::Inner::~Inner() {
	for_const (auto chat, _dataMap) {
		delete chat;
	}
}

QVector<PeerData*> ShareBox::Inner::selected() const {
	QVector<PeerData*> result;
	result.reserve(_dataMap.size());
	for_const (auto chat, _dataMap) {
		if (chat->checkbox.checked()) {
			result.push_back(chat->peer);
		}
	}
	return result;
}

QString appendShareGameScoreUrl(const QString &url, const FullMsgId &fullId) {
	auto shareHashData = QByteArray(0x10, Qt::Uninitialized);
	auto shareHashDataInts = reinterpret_cast<int32*>(shareHashData.data());
	auto channel = fullId.channel ? App::channelLoaded(fullId.channel) : static_cast<ChannelData*>(nullptr);
	auto channelAccessHash = channel ? channel->access : 0ULL;
	auto channelAccessHashInts = reinterpret_cast<int32*>(&channelAccessHash);
	shareHashDataInts[0] = MTP::authedId();
	shareHashDataInts[1] = fullId.channel;
	shareHashDataInts[2] = fullId.msg;
	shareHashDataInts[3] = channelAccessHashInts[0];

	// Count SHA1() of data.
	auto key128Size = 0x10;
	auto shareHashEncrypted = QByteArray(key128Size + shareHashData.size(), Qt::Uninitialized);
	hashSha1(shareHashData.constData(), shareHashData.size(), shareHashEncrypted.data());

	// Mix in channel access hash to the first 64 bits of SHA1 of data.
	*reinterpret_cast<uint64*>(shareHashEncrypted.data()) ^= *reinterpret_cast<uint64*>(channelAccessHashInts);

	// Encrypt data.
	if (!Local::encrypt(shareHashData.constData(), shareHashEncrypted.data() + key128Size, shareHashData.size(), shareHashEncrypted.constData())) {
		return url;
	}

	auto shareHash = shareHashEncrypted.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	auto shareUrl = qsl("tg://share_game_score?hash=") + QString::fromLatin1(shareHash);

	auto shareComponent = qsl("tgShareScoreUrl=") + qthelp::url_encode(shareUrl);

	auto hashPosition = url.indexOf('#');
	if (hashPosition < 0) {
		return url + '#' + shareComponent;
	}
	auto hash = url.mid(hashPosition + 1);
	if (hash.indexOf('=') >= 0 || hash.indexOf('?') >= 0) {
		return url + '&' + shareComponent;
	}
	if (!hash.isEmpty()) {
		return url + '?' + shareComponent;
	}
	return url + shareComponent;
}

namespace {

void shareGameScoreFromItem(HistoryItem *item) {
	struct ShareGameScoreData {
		ShareGameScoreData(const FullMsgId &msgId) : msgId(msgId) {
		}
		FullMsgId msgId;
		OrderedSet<mtpRequestId> requests;
	};
	auto data = MakeShared<ShareGameScoreData>(item->fullId());

	auto copyCallback = [data]() {
		if (auto main = App::main()) {
			if (auto item = App::histItemById(data->msgId)) {
				if (auto bot = item->getMessageBot()) {
					if (auto media = item->getMedia()) {
						if (media->type() == MediaTypeGame) {
							auto shortName = static_cast<HistoryGame*>(media)->game()->shortName;

							QApplication::clipboard()->setText(qsl("https://telegram.me/") + bot->username + qsl("?game=") + shortName);

							Ui::Toast::Config toast;
							toast.text = lang(lng_share_game_link_copied);
							Ui::Toast::Show(App::wnd(), toast);
						}
					}
				}
			}
		}
	};
	auto submitCallback = [data](const QVector<PeerData*> &result) {
		if (!data->requests.empty()) {
			return; // Share clicked already.
		}

		auto doneCallback = [data](const MTPUpdates &updates, mtpRequestId requestId) {
			if (auto main = App::main()) {
				main->sentUpdatesReceived(updates);
			}
			data->requests.remove(requestId);
			if (data->requests.empty()) {
				Ui::Toast::Config toast;
				toast.text = lang(lng_share_done);
				Ui::Toast::Show(App::wnd(), toast);

				Ui::hideLayer();
			}
		};

		MTPmessages_ForwardMessages::Flags sendFlags = MTPmessages_ForwardMessages::Flag::f_with_my_score;
		MTPVector<MTPint> msgIds = MTP_vector<MTPint>(1, MTP_int(data->msgId.msg));
		if (auto main = App::main()) {
			if (auto item = App::histItemById(data->msgId)) {
				for_const (auto peer, result) {
					MTPVector<MTPlong> random = MTP_vector<MTPlong>(1, rand_value<MTPlong>());
					auto request = MTPmessages_ForwardMessages(MTP_flags(sendFlags), item->history()->peer->input, msgIds, random, peer->input);
					auto callback = doneCallback;
					auto requestId = MTP::send(request, rpcDone(std_::move(callback)));
					data->requests.insert(requestId);
				}
			}
		}
	};
	auto filterCallback = [](PeerData *peer) {
		if (peer->canWrite()) {
			if (auto channel = peer->asChannel()) {
				return !channel->isBroadcast();
			}
			return true;
		}
		return false;
	};
	Ui::showLayer(new ShareBox(std_::move(copyCallback), std_::move(submitCallback), std_::move(filterCallback)));
}

} // namespace

void shareGameScoreByHash(const QString &hash) {
	auto key128Size = 0x10;

	auto hashEncrypted = QByteArray::fromBase64(hash.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	if (hashEncrypted.size() <= key128Size || (hashEncrypted.size() % 0x10) != 0) {
		Ui::showLayer(new InformBox(lang(lng_confirm_phone_link_invalid)));
		return;
	}

	// Decrypt data.
	auto hashData = QByteArray(hashEncrypted.size() - key128Size, Qt::Uninitialized);
	if (!Local::decrypt(hashEncrypted.constData() + key128Size, hashData.data(), hashEncrypted.size() - key128Size, hashEncrypted.constData())) {
		return;
	}

	// Count SHA1() of data.
	char dataSha1[20] = { 0 };
	hashSha1(hashData.constData(), hashData.size(), dataSha1);

	// Mix out channel access hash from the first 64 bits of SHA1 of data.
	auto channelAccessHash = *reinterpret_cast<uint64*>(hashEncrypted.data()) ^ *reinterpret_cast<uint64*>(dataSha1);

	// Check next 64 bits of SHA1() of data.
	auto skipSha1Part = sizeof(channelAccessHash);
	if (memcmp(dataSha1 + skipSha1Part, hashEncrypted.constData() + skipSha1Part, key128Size - skipSha1Part) != 0) {
		Ui::showLayer(new InformBox(lang(lng_share_wrong_user)));
		return;
	}

	auto hashDataInts = reinterpret_cast<int32*>(hashData.data());
	if (hashDataInts[0] != MTP::authedId()) {
		Ui::showLayer(new InformBox(lang(lng_share_wrong_user)));
		return;
	}

	// Check first 32 bits of channel access hash.
	auto channelAccessHashInts = reinterpret_cast<int32*>(&channelAccessHash);
	if (channelAccessHashInts[0] != hashDataInts[3]) {
		Ui::showLayer(new InformBox(lang(lng_share_wrong_user)));
		return;
	}

	auto channelId = hashDataInts[1];
	auto msgId = hashDataInts[2];
	if (!channelId && channelAccessHash) {
		// If there is no channel id, there should be no channel access_hash.
		Ui::showLayer(new InformBox(lang(lng_share_wrong_user)));
		return;
	}

	if (auto item = App::histItemById(channelId, msgId)) {
		shareGameScoreFromItem(item);
	} else if (App::api()) {
		auto resolveMessageAndShareScore = [msgId](ChannelData *channel) {
			App::api()->requestMessageData(channel, msgId, [](ChannelData *channel, MsgId msgId) {
				if (auto item = App::histItemById(channel, msgId)) {
					shareGameScoreFromItem(item);
				} else {
					Ui::showLayer(new InformBox(lang(lng_edit_deleted)));
				}
			});
		};

		auto channel = channelId ? App::channelLoaded(channelId) : nullptr;
		if (channel || !channelId) {
			resolveMessageAndShareScore(channel);
		} else {
			auto requestChannelIds = MTP_vector<MTPInputChannel>(1, MTP_inputChannel(MTP_int(channelId), MTP_long(channelAccessHash)));
			auto requestChannel = MTPchannels_GetChannels(requestChannelIds);
			MTP::send(requestChannel, rpcDone([channelId, resolveMessageAndShareScore](const MTPmessages_Chats &result) {
				if (result.type() == mtpc_messages_chats) {
					App::feedChats(result.c_messages_chats().vchats);
				}
				if (auto channel = App::channelLoaded(channelId)) {
					resolveMessageAndShareScore(channel);
				}
			}));
		}
	}
}
