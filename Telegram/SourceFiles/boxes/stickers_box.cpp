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
#include "stickers_box.h"

#include "lang.h"
#include "mainwidget.h"
#include "stickers/stickers.h"
#include "boxes/confirmbox.h"
#include "boxes/stickersetbox.h"
#include "apiwrap.h"
#include "localstorage.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_stickers.h"

namespace {

constexpr int kArchivedLimitFirstRequest = 10;
constexpr int kArchivedLimitPerPage = 30;

} // namespace

int32 stickerPacksCount(bool includeDisabledOfficial) {
	int32 result = 0;
	auto &order = Global::StickerSetsOrder();
	auto &sets = Global::StickerSets();
	for (int i = 0, l = order.size(); i < l; ++i) {
		auto it = sets.constFind(order.at(i));
		if (it != sets.cend()) {
			if (!(it->flags & MTPDstickerSet::Flag::f_archived) || ((it->flags & MTPDstickerSet::Flag::f_official) && includeDisabledOfficial)) {
				++result;
			}
		}
	}
	return result;
}

StickersBox::StickersBox(Section section) : ItemListBox(st::boxScroll)
, _section(section)
, _inner(this, section)
, _aboutWidth(st::boxWideWidth - 2 * st::stickersReorderPadding.top())
, _about(st::boxTextFont, lang((section == Section::Archived) ? lng_stickers_packs_archived : lng_stickers_reorder), _defaultOptions, _aboutWidth) {
	setup();
}

StickersBox::StickersBox(const Stickers::Order &archivedIds) : ItemListBox(st::boxScroll)
, _section(Section::ArchivedPart)
, _inner(this, archivedIds)
, _aboutWidth(st::boxWideWidth - 2 * st::stickersReorderPadding.top())
, _about(st::boxTextFont, lang(lng_stickers_packs_archived), _defaultOptions, _aboutWidth) {
	setup();
}

void StickersBox::getArchivedDone(uint64 offsetId, const MTPmessages_ArchivedStickers &result) {
	_archivedRequestId = 0;
	if (result.type() != mtpc_messages_archivedStickers) {
		return;
	}

	auto &stickers = result.c_messages_archivedStickers();
	auto &archived = Global::RefArchivedStickerSetsOrder();
	if (offsetId) {
		auto index = archived.indexOf(offsetId);
		if (index >= 0) {
			archived = archived.mid(0, index + 1);
		}
	} else {
		archived.clear();
	}

	bool addedSet = false;
	auto &v = stickers.vsets.c_vector().v;
	for_const (auto &stickerSet, v) {
		const MTPDstickerSet *setData = nullptr;
		switch (stickerSet.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = stickerSet.c_stickerSetCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = stickerSet.c_stickerSetMultiCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
		} break;
		}
		if (!setData) continue;

		if (auto set = Stickers::feedSet(*setData)) {
			auto index = archived.indexOf(set->id);
			if (archived.isEmpty() || index != archived.size() - 1) {
				if (index < archived.size() - 1) {
					archived.removeAt(index);
				}
				archived.push_back(set->id);
			}
			if (_section == Section::Archived) {
				if (_inner->appendSet(*set)) {
					addedSet = true;
					if (set->stickers.isEmpty() || (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
						App::api()->scheduleStickerSetRequest(set->id, set->access);
					}
				}
			}
		}
	}
	if (_section == Section::Installed && !archived.isEmpty()) {
		Local::writeArchivedStickers();
		rebuildList();
	} else if (_section == Section::Archived) {
		if (addedSet) {
			_inner->updateSize();
			setMaxHeight(snap(countHeight(), int32(st::sessionsHeight), int32(st::boxMaxListHeight)));
			_inner->setVisibleScrollbar((scrollArea()->scrollTopMax() > 0) ? (st::boxScroll.width - st::boxScroll.deltax) : 0);
			App::api()->requestStickerSets();
		} else {
			_allArchivedLoaded = v.isEmpty() || (offsetId != 0);
		}
	}
	checkLoadMoreArchived();
}

void StickersBox::setup() {
	if (_section == Section::Installed) {
		Local::readArchivedStickers();
		if (Global::ArchivedStickerSetsOrder().isEmpty()) {
			MTPmessages_GetArchivedStickers::Flags flags = 0;
			_archivedRequestId = MTP::send(MTPmessages_GetArchivedStickers(MTP_flags(flags), MTP_long(0), MTP_int(kArchivedLimitFirstRequest)), rpcDone(&StickersBox::getArchivedDone, 0ULL));
		}
	} else if (_section == Section::Archived) {
		// Reload the archived list.
		MTPmessages_GetArchivedStickers::Flags flags = 0;
		_archivedRequestId = MTP::send(MTPmessages_GetArchivedStickers(MTP_flags(flags), MTP_long(0), MTP_int(kArchivedLimitFirstRequest)), rpcDone(&StickersBox::getArchivedDone, 0ULL));

		auto &sets = Global::StickerSets();
		for_const (auto setId, Global::ArchivedStickerSetsOrder()) {
			auto it = sets.constFind(setId);
			if (it != sets.cend()) {
				if (it->stickers.isEmpty() && (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
					App::api()->scheduleStickerSetRequest(setId, it->access);
				}
			}
		}
		App::api()->requestStickerSets();
	}

	int bottomSkip = st::boxPadding.bottom();
	if (_section == Section::Installed) {
		_aboutHeight = st::stickersReorderPadding.top() + _about.countHeight(_aboutWidth) + st::stickersReorderPadding.bottom();
		_topShadow.create(this, st::contactsAboutShadow);

		_save.create(this, lang(lng_settings_save), st::defaultBoxButton);
		connect(_save, SIGNAL(clicked()), this, SLOT(onSave()));

		_cancel.create(this, lang(lng_cancel), st::cancelBoxButton);
		connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

		_bottomShadow.create(this);
		bottomSkip = st::boxButtonPadding.top() + _save->height() + st::boxButtonPadding.bottom();
	} else if (_section == Section::ArchivedPart) {
		_aboutHeight = st::stickersReorderPadding.top() + _about.countHeight(_aboutWidth) + st::stickersReorderPadding.bottom();
		_topShadow.create(this, st::contactsAboutShadow);

		_save.create(this, lang(lng_box_ok), st::defaultBoxButton);
		connect(_save, SIGNAL(clicked()), this, SLOT(onClose()));
	} else if (_section == Section::Archived) {
		_aboutHeight = st::stickersReorderPadding.top() + _about.countHeight(_aboutWidth) + st::stickersReorderPadding.bottom();
		_topShadow.create(this, st::contactsAboutShadow);
	}
	ItemListBox::init(_inner, bottomSkip, st::boxTitleHeight + _aboutHeight);
	setMaxHeight(snap(countHeight(), int32(st::sessionsHeight), int32(st::boxMaxListHeight)));

	connect(App::main(), SIGNAL(stickersUpdated()), this, SLOT(onStickersUpdated()));
	App::main()->updateStickers();

	connect(_inner, SIGNAL(checkDraggingScroll(int)), this, SLOT(onCheckDraggingScroll(int)));
	connect(_inner, SIGNAL(noDraggingScroll()), this, SLOT(onNoDraggingScroll()));
	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	connect(scrollArea(), SIGNAL(scrolled()), this, SLOT(onScroll()));
	_scrollTimer.setSingleShot(false);

	rebuildList();

	prepare();
}

void StickersBox::onScroll() {
	updateVisibleTopBottom();
	checkLoadMoreArchived();
}

void StickersBox::updateVisibleTopBottom() {
	auto visibleTop = scrollArea()->scrollTop();
	auto visibleBottom = visibleTop + scrollArea()->height();
	_inner->setVisibleTopBottom(visibleTop, visibleBottom);
}

void StickersBox::checkLoadMoreArchived() {
	if (_section != Section::Archived) return;

	int scrollTop = scrollArea()->scrollTop(), scrollTopMax = scrollArea()->scrollTopMax();
	if (scrollTop + PreloadHeightsCount * scrollArea()->height() >= scrollTopMax) {
		if (!_archivedRequestId && !_allArchivedLoaded) {
			uint64 lastId = 0;
			for (auto setId = Global::ArchivedStickerSetsOrder().cend(), e = Global::ArchivedStickerSetsOrder().cbegin(); setId != e;) {
				--setId;
				auto it = Global::StickerSets().constFind(*setId);
				if (it != Global::StickerSets().cend()) {
					if (it->flags & MTPDstickerSet::Flag::f_archived) {
						lastId = it->id;
						break;
					}
				}
			}
			MTPmessages_GetArchivedStickers::Flags flags = 0;
			_archivedRequestId = MTP::send(MTPmessages_GetArchivedStickers(MTP_flags(flags), MTP_long(lastId), MTP_int(kArchivedLimitPerPage)), rpcDone(&StickersBox::getArchivedDone, lastId));
		}
	}
}

int32 StickersBox::countHeight() const {
	int bottomSkip = st::boxPadding.bottom();
	if (_section == Section::Installed) {
		bottomSkip = st::boxButtonPadding.top() + _save->height() + st::boxButtonPadding.bottom();
	}
	return st::boxTitleHeight + _aboutHeight + _inner->height() + bottomSkip;
}

void StickersBox::disenableDone(const MTPmessages_StickerSetInstallResult &result, mtpRequestId req) {
	_disenableRequests.remove(req);
	if (_disenableRequests.isEmpty()) {
		saveOrder();
	}
}

bool StickersBox::disenableFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;
	_disenableRequests.remove(req);
	if (_disenableRequests.isEmpty()) {
		saveOrder();
	}
	return true;
}

void StickersBox::saveOrder() {
	auto order = _inner->getOrder();
	if (order.size() > 1) {
		QVector<MTPlong> mtpOrder;
		mtpOrder.reserve(order.size());
		for (int i = 0, l = order.size(); i < l; ++i) {
			mtpOrder.push_back(MTP_long(order.at(i)));
		}

		MTPmessages_ReorderStickerSets::Flags flags = 0;
		_reorderRequest = MTP::send(MTPmessages_ReorderStickerSets(MTP_flags(flags), MTP_vector<MTPlong>(mtpOrder)), rpcDone(&StickersBox::reorderDone), rpcFail(&StickersBox::reorderFail));
	} else {
		reorderDone(MTP_boolTrue());
	}
}

void StickersBox::reorderDone(const MTPBool &result) {
	_reorderRequest = 0;
	onClose();
}

bool StickersBox::reorderFail(const RPCError &result) {
	if (MTP::isDefaultHandledError(result)) return false;
	_reorderRequest = 0;
	Global::SetLastStickersUpdate(0);
	App::main()->updateStickers();
	onClose();
	return true;
}

void StickersBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	auto title = ([this]() {
		if (_section == Section::Installed) {
			return lang(lng_stickers_packs);
		} else if (_section == Section::Featured) {
			return lang(lng_stickers_featured);
		}
		return lang(lng_stickers_archived);
	})();
	paintTitle(p, title);
	p.translate(0, st::boxTitleHeight);

	if (_aboutHeight > 0) {
		p.fillRect(0, 0, width(), _aboutHeight, st::contactsAboutBg);
		p.setPen(st::stickersReorderFg);
		_about.draw(p, st::stickersReorderPadding.top(), st::stickersReorderPadding.top(), _aboutWidth, style::al_center);
	}
}

void StickersBox::closePressed() {
	if (!_disenableRequests.isEmpty()) {
		for_const (auto requestId, _disenableRequests) {
			MTP::cancel(requestId);
		}
		_disenableRequests.clear();
		Global::SetLastStickersUpdate(0);
		App::main()->updateStickers();
	} else if (_reorderRequest) {
		MTP::cancel(_reorderRequest);
		_reorderRequest = 0;
		Global::SetLastStickersUpdate(0);
		App::main()->updateStickers();
	}
}

StickersBox::~StickersBox() {
	if (_section == Section::Archived) {
		Local::writeArchivedStickers();
	}
}

void StickersBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);
	_inner->resize(width(), _inner->height());
	_inner->setVisibleScrollbar((scrollArea()->scrollTopMax() > 0) ? (st::boxScroll.width - st::boxScroll.deltax) : 0);
	updateVisibleTopBottom();
	if (_topShadow) {
		_topShadow->setGeometry(0, st::boxTitleHeight + _aboutHeight, width(), st::lineWidth);
	}
	if (_save) {
		_save->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save->height());
	}
	if (_cancel) {
		_cancel->moveToRight(st::boxButtonPadding.right() + _save->width() + st::boxButtonPadding.left(), _save->y());
		_bottomShadow->setGeometry(0, height() - st::boxButtonPadding.bottom() - _save->height() - st::boxButtonPadding.top() - st::lineWidth, width(), st::lineWidth);
	}
}

void StickersBox::onStickersUpdated() {
	if (_section == Section::Installed || _section == Section::Featured) {
		rebuildList();
	} else {
		_inner->updateRows();
	}
}

void StickersBox::rebuildList() {
	_inner->rebuild();
	setMaxHeight(snap(countHeight(), int32(st::sessionsHeight), int32(st::boxMaxListHeight)));
	_inner->setVisibleScrollbar((scrollArea()->scrollTopMax() > 0) ? (st::boxScroll.width - st::boxScroll.deltax) : 0);
}

void StickersBox::onCheckDraggingScroll(int localY) {
	if (localY < scrollArea()->scrollTop()) {
		_scrollDelta = localY - scrollArea()->scrollTop();
	} else if (localY >= scrollArea()->scrollTop() + scrollArea()->height()) {
		_scrollDelta = localY - scrollArea()->scrollTop() - scrollArea()->height() + 1;
	} else {
		_scrollDelta = 0;
	}
	if (_scrollDelta) {
		_scrollTimer.start(15);
	} else {
		_scrollTimer.stop();
	}
}

void StickersBox::onNoDraggingScroll() {
	_scrollTimer.stop();
}

void StickersBox::onScrollTimer() {
	int32 d = (_scrollDelta > 0) ? qMin(_scrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_scrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	scrollArea()->scrollToY(scrollArea()->scrollTop() + d);
}

void StickersBox::onSave() {
	if (!_inner->savingStart()) {
		return;
	}

	bool writeRecent = false, writeArchived = false;
	auto &recent = cGetRecentStickers();
	auto &sets = Global::RefStickerSets();

	auto reorder = _inner->getOrder(), disabled = _inner->getDisabledSets();
	for (int32 i = 0, l = disabled.size(); i < l; ++i) {
		auto it = sets.find(disabled.at(i));
		if (it != sets.cend()) {
			for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
				if (it->stickers.indexOf(i->first) >= 0) {
					i = recent.erase(i);
					writeRecent = true;
				} else {
					++i;
				}
			}
			if (!(it->flags & MTPDstickerSet::Flag::f_archived)) {
				MTPInputStickerSet setId = (it->id && it->access) ? MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access)) : MTP_inputStickerSetShortName(MTP_string(it->shortName));
				if (it->flags & MTPDstickerSet::Flag::f_official) {
					_disenableRequests.insert(MTP::send(MTPmessages_InstallStickerSet(setId, MTP_boolTrue()), rpcDone(&StickersBox::disenableDone), rpcFail(&StickersBox::disenableFail), 0, 5));
					it->flags |= MTPDstickerSet::Flag::f_archived;
					auto index = Global::RefArchivedStickerSetsOrder().indexOf(it->id);
					if (index < 0) {
						Global::RefArchivedStickerSetsOrder().push_front(it->id);
						writeArchived = true;
					}
				} else {
					_disenableRequests.insert(MTP::send(MTPmessages_UninstallStickerSet(setId), rpcDone(&StickersBox::disenableDone), rpcFail(&StickersBox::disenableFail), 0, 5));
					int removeIndex = Global::StickerSetsOrder().indexOf(it->id);
					if (removeIndex >= 0) Global::RefStickerSetsOrder().removeAt(removeIndex);
					if (!(it->flags & MTPDstickerSet_ClientFlag::f_featured) && !(it->flags & MTPDstickerSet_ClientFlag::f_special)) {
						sets.erase(it);
					} else {
						if (it->flags & MTPDstickerSet::Flag::f_archived) {
							writeArchived = true;
						}
						it->flags &= ~(MTPDstickerSet::Flag::f_installed | MTPDstickerSet::Flag::f_archived);
					}
				}
			}
		}
	}

	// Clear all installed flags, set only for sets from order.
	for (auto &set : sets) {
		if (!(set.flags & MTPDstickerSet::Flag::f_archived)) {
			set.flags &= ~MTPDstickerSet::Flag::f_installed;
		}
	}

	auto &order(Global::RefStickerSetsOrder());
	order.clear();
	for (int i = 0, l = reorder.size(); i < l; ++i) {
		auto it = sets.find(reorder.at(i));
		if (it != sets.cend()) {
			if ((it->flags & MTPDstickerSet::Flag::f_archived) && !disabled.contains(it->id)) {
				MTPInputStickerSet setId = (it->id && it->access) ? MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access)) : MTP_inputStickerSetShortName(MTP_string(it->shortName));
				_disenableRequests.insert(MTP::send(MTPmessages_InstallStickerSet(setId, MTP_boolFalse()), rpcDone(&StickersBox::disenableDone), rpcFail(&StickersBox::disenableFail), 0, 5));
				it->flags &= ~MTPDstickerSet::Flag::f_archived;
				writeArchived = true;
			}
			order.push_back(reorder.at(i));
			it->flags |= MTPDstickerSet::Flag::f_installed;
		}
	}
	for (auto it = sets.begin(); it != sets.cend();) {
		if ((it->flags & MTPDstickerSet_ClientFlag::f_featured)
			|| (it->flags & MTPDstickerSet::Flag::f_installed)
			|| (it->flags & MTPDstickerSet::Flag::f_archived)
			|| (it->flags & MTPDstickerSet_ClientFlag::f_special)) {
			++it;
		} else {
			it = sets.erase(it);
		}
	}

	Local::writeInstalledStickers();
	if (writeRecent) Local::writeUserSettings();
	if (writeArchived) Local::writeArchivedStickers();
	emit App::main()->stickersUpdated();

	if (_disenableRequests.isEmpty()) {
		saveOrder();
	} else {
		MTP::sendAnything();
	}
}

void StickersBox::showAll() {
	if (_topShadow) {
		_topShadow->show();
	}
	if (_save) {
		_save->show();
	}
	if (_cancel) {
		_cancel->show();
		_bottomShadow->show();
	}
	ItemListBox::showAll();
}

StickersBox::Inner::Inner(QWidget *parent, StickersBox::Section section) : ScrolledWidget(parent)
, _section(section)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _a_shifting(animation(this, &Inner::step_shifting))
, _itemsTop(st::membersPadding.top())
, _clearWidth(st::normalFont->width(lang(lng_stickers_clear_recent)))
, _removeWidth(st::normalFont->width(lang(lng_stickers_remove)))
, _returnWidth(st::normalFont->width(lang(lng_stickers_return)))
, _restoreWidth(st::normalFont->width(lang(lng_stickers_restore)))
, _aboveShadow(st::boxShadow) {
	setup();
}

StickersBox::Inner::Inner(QWidget *parent, const Stickers::Order &archivedIds) : ScrolledWidget(parent)
, _section(StickersBox::Section::ArchivedPart)
, _archivedIds(archivedIds)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _a_shifting(animation(this, &Inner::step_shifting))
, _itemsTop(st::membersPadding.top())
, _clearWidth(st::normalFont->width(lang(lng_stickers_clear_recent)))
, _removeWidth(st::normalFont->width(lang(lng_stickers_remove)))
, _returnWidth(st::normalFont->width(lang(lng_stickers_return)))
, _restoreWidth(st::normalFont->width(lang(lng_stickers_restore)))
, _aboveShadow(st::boxShadow) {
	setup();
}

void StickersBox::Inner::setup() {
	subscribe(FileDownload::ImageLoaded(), [this] { update(); });
	setMouseTracking(true);
}

void StickersBox::Inner::onImageLoaded() {
	update();
	readVisibleSets();
}

void StickersBox::Inner::paintButton(Painter &p, int y, bool selected, const QString &text, int badgeCounter) const {
	if (selected) {
		p.fillRect(0, y, width(), _buttonHeight, st::contactsBgOver);
	}
	p.setFont(st::stickersFeaturedFont);
	p.setPen(st::stickersFeaturedPen);
	p.drawTextLeft(st::stickersFeaturedPosition.x(), y + st::stickersFeaturedPosition.y(), width(), text);

	if (badgeCounter) {
		Dialogs::Layout::UnreadBadgeStyle unreadSt;
		unreadSt.sizeId = Dialogs::Layout::UnreadBadgeInStickersBox;
		unreadSt.size = st::stickersFeaturedBadgeSize;
		int unreadRight = width() - (st::contactsPadding.right() + st::contactsCheckPosition.x());
		if (rtl()) unreadRight = width() - unreadRight;
		int unreadTop = y + (_buttonHeight - st::stickersFeaturedBadgeSize) / 2;
		Dialogs::Layout::paintUnreadCount(p, QString::number(badgeCounter), unreadRight, unreadTop, unreadSt);
	}
}

void StickersBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	_a_shifting.step();

	p.fillRect(r, st::white);
	p.setClipRect(r);

	int y = st::membersPadding.top();
	if (_hasFeaturedButton) {
		auto selected = (_selected == -2);
		paintButton(p, y, selected, lang(lng_stickers_featured), Global::FeaturedStickerSetsUnreadCount());
		y += _buttonHeight;
	}
	if (_hasArchivedButton) {
		auto selected = (_selected == -1);
		paintButton(p, y, selected, lang(lng_stickers_archived), 0);
		y += _buttonHeight;
	}

	if (_rows.isEmpty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, y, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	} else {
		p.translate(0, _itemsTop);

		int32 yFrom = r.y() - _itemsTop, yTo = r.y() + r.height() - _itemsTop;
		int32 from = floorclamp(yFrom - _rowHeight, _rowHeight, 0, _rows.size());
		int32 to = ceilclamp(yTo + _rowHeight, _rowHeight, 0, _rows.size());
		p.translate(0, from * _rowHeight);
		for (int32 i = from; i < to; ++i) {
			if (i != _above) {
				paintRow(p, i);
			}
			p.translate(0, _rowHeight);
		}
		if (from <= _above && _above < to) {
			p.translate(0, (_above - to) * _rowHeight);
			paintRow(p, _above);
		}
	}
}

void StickersBox::Inner::paintRow(Painter &p, int32 index) {
	const StickerSetRow *s(_rows.at(index));

	int32 xadd = 0, yadd = s->yadd.current();
	if (xadd || yadd) p.translate(xadd, yadd);

	if (_section == Section::Installed) {
		bool removeSel = (index == _actionSel && (_actionDown < 0 || index == _actionDown));
		bool removeDown = removeSel && (index == _actionDown);

		p.setFont(removeSel ? st::linkOverFont : st::linkFont);
		if (removeDown) {
			p.setPen(st::btnDefLink.downColor);
		} else {
			p.setPen(st::btnDefLink.color);
		}
		int32 remWidth = s->recent ? _clearWidth : (s->disabled ? (s->official ? _restoreWidth : _returnWidth) : _removeWidth);
		QString remText = lang(s->recent ? lng_stickers_clear_recent : (s->disabled ? (s->official ? lng_stickers_restore : lng_stickers_return) : lng_stickers_remove));
		p.drawTextRight(st::contactsPadding.right() + st::contactsCheckPosition.x(), st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, width(), remText, remWidth);

		if (index == _above) {
			float64 current = _aboveShadowFadeOpacity.current();
			if (_started >= 0) {
				float64 o = aboveShadowOpacity();
				if (o > current) {
					_aboveShadowFadeOpacity = anim::fvalue(o, o);
					current = o;
				}
			}
			p.setOpacity(current);
			QRect row(myrtlrect(_aboveShadow.getDimensions(st::boxShadowShift).left(), st::contactsPadding.top() / 2, width() - (st::contactsPadding.left() / 2) - _scrollbar - _aboveShadow.getDimensions(st::boxShadowShift).right(), _rowHeight - ((st::contactsPadding.top() + st::contactsPadding.bottom()) / 2)));
			_aboveShadow.paint(p, row, st::boxShadowShift);
			p.fillRect(row, st::white);
			p.setOpacity(1);
		}
	} else if (s->installed && !s->disabled) {
		int addw = st::stickersAddSize.width();
		int checkx = width() - (st::contactsPadding.right() + st::contactsCheckPosition.x() + (addw + st::stickersFeaturedInstalled.width()) / 2);
		int checky = st::contactsPadding.top() + (st::contactsPhotoSize - st::stickersFeaturedInstalled.height()) / 2;
		st::stickersFeaturedInstalled.paint(p, QPoint(checkx, checky), width());
	} else {
		int addw = st::stickersAddSize.width();
		int addx = width() - st::contactsPadding.right() - st::contactsCheckPosition.x() - addw;
		int addy = st::contactsPadding.top() + (st::contactsPhotoSize - st::stickersAddSize.height()) / 2;
		QRect add(myrtlrect(addx, addy, addw, st::stickersAddSize.height()));

		auto textBg = (_actionSel == index) ? st::defaultActiveButton.textBgOver : st::defaultActiveButton.textBg;
		App::roundRect(p, add, textBg, ImageRoundRadius::Small);
		int iconx = addx + (st::stickersAddSize.width() - st::stickersAddIcon.width()) / 2;
		int icony = addy + (st::stickersAddSize.height() - st::stickersAddIcon.height()) / 2;
		icony += (_actionSel == index && _actionDown == index) ? (st::defaultActiveButton.downTextTop - st::defaultActiveButton.textTop) : 0;
		st::stickersAddIcon.paint(p, QPoint(iconx, icony), width());
	}

	if (s->disabled && _section == Section::Installed) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}
	if (s->sticker) {
		s->sticker->thumb->load();
		QPixmap pix(s->sticker->thumb->pix(s->pixw, s->pixh));
		p.drawPixmapLeft(st::contactsPadding.left() + (st::contactsPhotoSize - s->pixw) / 2, st::contactsPadding.top() + (st::contactsPhotoSize - s->pixh) / 2, width(), pix);
	}

	int namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	int namey = st::contactsPadding.top() + st::contactsNameTop;
	int statusx = namex;
	int statusy = st::contactsPadding.top() + st::contactsStatusTop;

	p.setFont(st::contactsNameFont);
	p.setPen(st::black);
	p.drawTextLeft(namex, namey, width(), s->title, s->titleWidth);

	if (s->unread) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::stickersFeaturedUnreadBg);

		p.setRenderHint(QPainter::HighQualityAntialiasing, true);
		p.drawEllipse(rtlrect(namex + s->titleWidth + st::stickersFeaturedUnreadSkip, namey + st::stickersFeaturedUnreadTop, st::stickersFeaturedUnreadSize, st::stickersFeaturedUnreadSize, width()));
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);
	}

	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextLeft(statusx, statusy, width(), lng_stickers_count(lt_count, s->count));

	p.setOpacity(1);
	if (xadd || yadd) p.translate(-xadd, -yadd);
}

void StickersBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (_saving) return;
	if (_dragging >= 0) mouseReleaseEvent(e);
	_mouse = e->globalPos();
	onUpdateSelected();

	_pressed = _selected;
	if (_actionSel >= 0) {
		_actionDown = _actionSel;
		update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
	} else if (_selected >= 0 && _section == Section::Installed && !_rows.at(_selected)->recent) {
		_above = _dragging = _started = _selected;
		_dragStart = mapFromGlobal(_mouse);
	}
}

void StickersBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	if (_saving) return;
	_mouse = e->globalPos();
	onUpdateSelected();
}

void StickersBox::Inner::onUpdateSelected() {
	if (_saving) return;
	QPoint local(mapFromGlobal(_mouse));
	if (_dragging >= 0) {
		int32 shift = 0;
		uint64 ms = getms();
		int firstSetIndex = 0;
		if (_rows.at(firstSetIndex)->recent) {
			++firstSetIndex;
		}
		if (_dragStart.y() > local.y() && _dragging > 0) {
			shift = -floorclamp(_dragStart.y() - local.y() + (_rowHeight / 2), _rowHeight, 0, _dragging - firstSetIndex);
			for (int32 from = _dragging, to = _dragging + shift; from > to; --from) {
				qSwap(_rows[from], _rows[from - 1]);
				_rows.at(from)->yadd = anim::ivalue(_rows.at(from)->yadd.current() - _rowHeight, 0);
				_animStartTimes[from] = ms;
			}
		} else if (_dragStart.y() < local.y() && _dragging + 1 < _rows.size()) {
			shift = floorclamp(local.y() - _dragStart.y() + (_rowHeight / 2), _rowHeight, 0, _rows.size() - _dragging - 1);
			for (int32 from = _dragging, to = _dragging + shift; from < to; ++from) {
				qSwap(_rows[from], _rows[from + 1]);
				_rows.at(from)->yadd = anim::ivalue(_rows.at(from)->yadd.current() + _rowHeight, 0);
				_animStartTimes[from] = ms;
			}
		}
		if (shift) {
			_dragging += shift;
			_above = _dragging;
			_dragStart.setY(_dragStart.y() + shift * _rowHeight);
			if (!_a_shifting.animating()) {
				_a_shifting.start();
			}
		}
		_rows.at(_dragging)->yadd = anim::ivalue(local.y() - _dragStart.y(), local.y() - _dragStart.y());
		_animStartTimes[_dragging] = 0;
		_a_shifting.step(getms(), true);

		emit checkDraggingScroll(local.y());
	} else {
		bool in = rect().marginsRemoved(QMargins(0, _itemsTop, 0, st::membersPadding.bottom())).contains(local);
		int selected = -2;
		int actionSel = -1;
		if (in) {
			selected = floorclamp(local.y() - _itemsTop, _rowHeight, 0, _rows.size() - 1);

			if (_section == Section::Installed) {
				int remw = _rows.at(selected)->recent ? _clearWidth : (_rows.at(selected)->disabled ? (_rows.at(selected)->official ? _restoreWidth : _returnWidth) : _removeWidth);
				QRect rem(myrtlrect(width() - st::contactsPadding.right() - st::contactsCheckPosition.x() - remw, st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, remw, st::normalFont->height));
				actionSel = rem.contains(local.x(), local.y() - _itemsTop - selected * _rowHeight) ? selected : -1;
			} else if (_rows.at(selected)->installed && !_rows.at(selected)->disabled) {
				actionSel = -1;
			} else {
				int addw = st::stickersAddSize.width();
				int addx = width() - st::contactsPadding.right() - st::contactsCheckPosition.x() - addw;
				int addy = st::contactsPadding.top() + (st::contactsPhotoSize - st::stickersAddSize.height()) / 2;
				QRect add(myrtlrect(addx, addy, addw, st::stickersAddSize.height()));
				actionSel = add.contains(local.x(), local.y() - _itemsTop - selected * _rowHeight) ? selected : -1;
			}
		} else if (_hasFeaturedButton && QRect(0, st::membersPadding.top(), width(), _buttonHeight).contains(local)) {
			selected = -2;
		} else if (_hasArchivedButton && QRect(0, st::membersPadding.top() + (_hasFeaturedButton ? _buttonHeight : 0), width(), _buttonHeight).contains(local)) {
			selected = -1;
		} else {
			selected = -3;
		}
		if (_selected != selected) {
			if (((_selected == -1) != (selected == -1)) || ((_selected == -2) != (selected == -2))) {
				update();
			}
			if (_section != Section::Installed && ((_selected >= 0 || _pressed >= 0) != (selected >= 0 || _pressed >= 0))) {
				setCursor((selected >= 0 || _pressed >= 0) ? style::cur_pointer : style::cur_default);
			}
			_selected = selected;
		}
		setActionSel(actionSel);
		emit noDraggingScroll();
	}
}

void StickersBox::Inner::onClearRecent() {
	if (_clearBox) {
		_clearBox->onClose();
	}

	auto &sets = Global::RefStickerSets();
	bool removedCloud = (sets.remove(Stickers::CloudRecentSetId) != 0);
	bool removedCustom = (sets.remove(Stickers::CustomSetId) != 0);

	auto &recent = cGetRecentStickers();
	if (!recent.isEmpty()) {
		recent.clear();
		Local::writeUserSettings();
	}

	if (removedCustom) Local::writeInstalledStickers();
	if (removedCloud) Local::writeRecentStickers();
	emit App::main()->updateStickers();
	rebuild();

	MTPmessages_ClearRecentStickers::Flags flags = 0;
	MTP::send(MTPmessages_ClearRecentStickers(MTP_flags(flags)));
}

void StickersBox::Inner::onClearBoxDestroyed(QObject *box) {
	if (box == _clearBox) {
		_clearBox = nullptr;
	}
}

float64 StickersBox::Inner::aboveShadowOpacity() const {
	if (_above < 0) return 0;

	int32 dx = 0;
	int32 dy = qAbs(_above * _rowHeight + _rows.at(_above)->yadd.current() - _started * _rowHeight);
	return qMin((dx + dy)  * 2. / _rowHeight, 1.);
}

void StickersBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	_pressed = -2;

	if (_section != Section::Installed && _selected < 0 && pressed >= 0) {
		setCursor(style::cur_default);
	}

	if (_saving) return;

	_mouse = e->globalPos();
	onUpdateSelected();
	if (_actionDown == _actionSel && _actionSel >= 0) {
		if (_section == Section::Installed) {
			if (_rows[_actionDown]->recent) {
				_clearBox = new ConfirmBox(lang(lng_stickers_clear_recent_sure), lang(lng_stickers_clear_recent));
				connect(_clearBox, SIGNAL(confirmed()), this, SLOT(onClearRecent()));
				connect(_clearBox, SIGNAL(destroyed(QObject*)), this, SLOT(onClearBoxDestroyed(QObject*)));
				Ui::showLayer(_clearBox, KeepOtherLayers);
			} else {
				_rows[_actionDown]->disabled = !_rows[_actionDown]->disabled;
			}
		} else {
			installSet(_rows[_actionDown]->id);
		}
	} else if (_dragging >= 0) {
		QPoint local(mapFromGlobal(_mouse));
		_rows[_dragging]->yadd.start(0);
		_aboveShadowFadeStart = _animStartTimes[_dragging] = getms();
		_aboveShadowFadeOpacity = anim::fvalue(aboveShadowOpacity(), 0);
		if (!_a_shifting.animating()) {
			_a_shifting.start();
		}

		_dragging = _started = -1;
	} else if (pressed == _selected && _actionSel < 0 && _actionDown < 0) {
		if (_selected == -2) {
			_selected = -3;
			Ui::showLayer(new StickersBox(Section::Featured), KeepOtherLayers);
		} else if (_selected == -1) {
			_selected = -3;
			Ui::showLayer(new StickersBox(Section::Archived), KeepOtherLayers);
		} else if (_selected >= 0 && _section != Section::Installed) {
			auto &sets = Global::RefStickerSets();
			auto it = sets.find(_rows.at(pressed)->id);
			if (it != sets.cend()) {
				_selected = -3;
				Ui::showLayer(new StickerSetBox(Stickers::inputSetId(*it)), KeepOtherLayers);
			}
		}
	}
	if (_actionDown >= 0) {
		update(0, _itemsTop + _actionDown * _rowHeight, width(), _rowHeight);
		_actionDown = -1;
	}
}

void StickersBox::Inner::leaveEvent(QEvent *e) {
	_mouse = QPoint(-1, -1);
	onUpdateSelected();
}

void StickersBox::Inner::installSet(uint64 setId) {
	auto &sets = Global::RefStickerSets();
	auto it = sets.find(setId);
	if (it == sets.cend()) {
		rebuild();
		return;
	}

	MTP::send(MTPmessages_InstallStickerSet(Stickers::inputSetId(*it), MTP_boolFalse()), rpcDone(&Inner::installDone), rpcFail(&Inner::installFail, setId));

	Stickers::installLocally(setId);
}

void StickersBox::Inner::installDone(const MTPmessages_StickerSetInstallResult &result) {
	if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
		Stickers::applyArchivedResult(result.c_messages_stickerSetInstallResultArchive());
	}
}

bool StickersBox::Inner::installFail(uint64 setId, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	auto &sets = Global::RefStickerSets();
	auto it = sets.find(setId);
	if (it == sets.cend()) {
		rebuild();
		return true;
	}

	Stickers::undoInstallLocally(setId);
	return true;
}

void StickersBox::Inner::step_shifting(uint64 ms, bool timer) {
	bool animating = false;
	int32 updateMin = -1, updateMax = 0;
	for (int32 i = 0, l = _animStartTimes.size(); i < l; ++i) {
		uint64 start = _animStartTimes.at(i);
		if (start) {
			if (updateMin < 0) updateMin = i;
			updateMax = i;
			if (start + st::stickersRowDuration > ms && ms >= start) {
				_rows.at(i)->yadd.update(float64(ms - start) / st::stickersRowDuration, anim::sineInOut);
				animating = true;
			} else {
				_rows.at(i)->yadd.finish();
				_animStartTimes[i] = 0;
			}
		}
	}
	if (_aboveShadowFadeStart) {
		if (updateMin < 0 || updateMin > _above) updateMin = _above;
		if (updateMax < _above) updateMin = _above;
		if (_aboveShadowFadeStart + st::stickersRowDuration > ms && ms > _aboveShadowFadeStart) {
			_aboveShadowFadeOpacity.update(float64(ms - _aboveShadowFadeStart) / st::stickersRowDuration, anim::sineInOut);
			animating = true;
		} else {
			_aboveShadowFadeOpacity.finish();
			_aboveShadowFadeStart = 0;
		}
	}
	if (timer) {
		if (_dragging >= 0) {
			if (updateMin < 0 || updateMin > _dragging) updateMin = _dragging;
			if (updateMax < _dragging) updateMax = _dragging;
		}
		if (updateMin >= 0) {
			update(0, _itemsTop + _rowHeight * (updateMin - 1), width(), _rowHeight * (updateMax - updateMin + 3));
		}
	}
	if (!animating) {
		_above = _dragging;
		_a_shifting.stop();
	}
}

void StickersBox::Inner::clear() {
	for (int32 i = 0, l = _rows.size(); i < l; ++i) {
		delete _rows.at(i);
	}
	_rows.clear();
	_animStartTimes.clear();
	_aboveShadowFadeStart = 0;
	_aboveShadowFadeOpacity = anim::fvalue(0, 0);
	_a_shifting.stop();
	_above = _dragging = _started = -1;
	_selected = -3;
	_pressed = -3;
	_actionDown = -1;
	setActionSel(-1);
	update();
}

void StickersBox::Inner::setActionSel(int32 actionSel) {
	if (actionSel != _actionSel) {
		if (_actionSel >= 0) update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
		_actionSel = actionSel;
		if (_actionSel >= 0) update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
		if (_section == Section::Installed) {
			setCursor((_actionSel >= 0 && (_actionDown < 0 || _actionDown == _actionSel)) ? style::cur_pointer : style::cur_default);
		}
	}
}

void StickersBox::Inner::rebuild() {
	_hasFeaturedButton = _hasArchivedButton = false;
	_itemsTop = st::membersPadding.top();
	_buttonHeight = st::stickersFeaturedHeight;
	if (_section == Section::Installed) {
		if (!Global::FeaturedStickerSetsOrder().isEmpty()) {
			_itemsTop += _buttonHeight;
			_hasFeaturedButton = true;
		}
		if (!Global::ArchivedStickerSetsOrder().isEmpty()) {
			_itemsTop += _buttonHeight;
			_hasArchivedButton = true;
		}
		if (_itemsTop > st::membersPadding.top()) {
			_itemsTop += st::membersPadding.top();
		}
	}

	int maxNameWidth = countMaxNameWidth();

	clear();
	auto &order = ([this]() -> const Stickers::Order & {
		if (_section == Section::Installed) {
			return Global::StickerSetsOrder();
		} else if (_section == Section::Featured) {
			return Global::FeaturedStickerSetsOrder();
		} else if (_section == Section::Archived) {
			return Global::ArchivedStickerSetsOrder();
		}
		return _archivedIds;
	})();
	_rows.reserve(order.size() + 1);
	_animStartTimes.reserve(order.size() + 1);

	auto &sets = Global::StickerSets();
	if (_section == Section::Installed) {
		auto cloudIt = sets.constFind(Stickers::CloudRecentSetId);
		if (cloudIt != sets.cend() && !cloudIt->stickers.isEmpty()) {
			rebuildAppendSet(cloudIt.value(), maxNameWidth);
		}
	}
	for_const (auto setId, order) {
		auto it = sets.constFind(setId);
		if (it == sets.cend()) {
			continue;
		}

		rebuildAppendSet(it.value(), maxNameWidth);

		if (it->stickers.isEmpty() || (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
			App::api()->scheduleStickerSetRequest(it->id, it->access);
		}
	}
	App::api()->requestStickerSets();
	updateSize();
}

void StickersBox::Inner::updateSize() {
	resize(width(), _itemsTop + _rows.size() * _rowHeight + st::membersPadding.bottom());
}

void StickersBox::Inner::updateRows() {
	int maxNameWidth = countMaxNameWidth();
	auto &sets = Global::StickerSets();
	for_const (auto row, _rows) {
		auto it = sets.constFind(row->id);
		if (it != sets.cend()) {
			auto &set = it.value();
			if (!row->sticker) {
				DocumentData *sticker = nullptr;
				int pixw = 0, pixh = 0;
				fillSetCover(set, &sticker, &pixw, &pixh);
				if (sticker) {
					row->sticker = sticker;
					row->pixw = pixw;
					row->pixh = pixh;
				}
			}
			fillSetFlags(set, &row->recent, &row->installed, &row->official, &row->unread, &row->disabled);
			if (_section == Section::Installed) {
				row->disabled = false;
			}
			row->title = fillSetTitle(set, maxNameWidth, &row->titleWidth);
			row->count = fillSetCount(set);
		}
	}
	update();
}

bool StickersBox::Inner::appendSet(const Stickers::Set &set) {
	for_const (auto row, _rows) {
		if (row->id == set.id) {
			return false;
		}
	}
	rebuildAppendSet(set, countMaxNameWidth());
	return true;
}

int StickersBox::Inner::countMaxNameWidth() const {
	int namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	int namew = st::boxWideWidth - namex - st::contactsPadding.right() - st::contactsCheckPosition.x();
	if (_section == Section::Installed) {
		namew -= qMax(qMax(qMax(_returnWidth, _removeWidth), _restoreWidth), _clearWidth);
	} else {
		namew -= st::stickersAddIcon.width() - st::defaultActiveButton.width;
		namew -= st::stickersFeaturedUnreadSize + st::stickersFeaturedUnreadSkip;
	}
	return namew;
}

void StickersBox::Inner::rebuildAppendSet(const Stickers::Set &set, int maxNameWidth) {
	bool recent = false, installed = false, official = false, unread = false, disabled = false;
	fillSetFlags(set, &recent, &installed, &official, &unread, &disabled);
	if (_section == Section::Installed && disabled) {
		return;
	}

	DocumentData *sticker = nullptr;
	int pixw = 0, pixh = 0;
	fillSetCover(set, &sticker, &pixw, &pixh);

	int titleWidth = 0;
	QString title = fillSetTitle(set, maxNameWidth, &titleWidth);
	int count = fillSetCount(set);

	_rows.push_back(new StickerSetRow(set.id, sticker, count, title, titleWidth, installed, official, unread, disabled, recent, pixw, pixh));
	_animStartTimes.push_back(0);
}

void StickersBox::Inner::fillSetCover(const Stickers::Set &set, DocumentData **outSticker, int *outWidth, int *outHeight) const {
	if (set.stickers.isEmpty()) {
		*outSticker = nullptr;
		*outWidth = *outHeight = 0;
		return;
	}
	auto sticker = *outSticker = set.stickers.front();

	auto pixw = sticker->thumb->width();
	auto pixh = sticker->thumb->height();
	if (pixw > st::contactsPhotoSize) {
		if (pixw > pixh) {
			pixh = (pixh * st::contactsPhotoSize) / pixw;
			pixw = st::contactsPhotoSize;
		} else {
			pixw = (pixw * st::contactsPhotoSize) / pixh;
			pixh = st::contactsPhotoSize;
		}
	} else if (pixh > st::contactsPhotoSize) {
		pixw = (pixw * st::contactsPhotoSize) / pixh;
		pixh = st::contactsPhotoSize;
	}
	*outWidth = pixw;
	*outHeight = pixh;
}

int StickersBox::Inner::fillSetCount(const Stickers::Set &set) const {
	int result = set.stickers.isEmpty() ? set.count : set.stickers.size(), added = 0;
	if (set.id == Stickers::CloudRecentSetId) {
		auto customIt = Global::StickerSets().constFind(Stickers::CustomSetId);
		if (customIt != Global::StickerSets().cend()) {
			added = customIt->stickers.size();
			for_const (auto &sticker, cGetRecentStickers()) {
				if (customIt->stickers.indexOf(sticker.first) < 0) {
					++added;
				}
			}
		} else {
			added = cGetRecentStickers().size();
		}
	}
	return result + added;
}

QString StickersBox::Inner::fillSetTitle(const Stickers::Set &set, int maxNameWidth, int *outTitleWidth) const {
	auto result = set.title;
	int titleWidth = st::contactsNameFont->width(result);
	if (titleWidth > maxNameWidth) {
		result = st::contactsNameFont->elided(result, maxNameWidth);
		titleWidth = st::contactsNameFont->width(result);
	}
	if (outTitleWidth) {
		*outTitleWidth = titleWidth;
	}
	return result;
}

void StickersBox::Inner::fillSetFlags(const Stickers::Set &set, bool *outRecent, bool *outInstalled, bool *outOfficial, bool *outUnread, bool *outDisabled) {
	*outRecent = (set.id == Stickers::CloudRecentSetId);
	*outInstalled = true;
	*outOfficial = true;
	*outUnread = false;
	*outDisabled = false;
	if (!*outRecent) {
		*outInstalled = (set.flags & MTPDstickerSet::Flag::f_installed);
		*outOfficial = (set.flags & MTPDstickerSet::Flag::f_official);
		*outDisabled = (set.flags & MTPDstickerSet::Flag::f_archived);
		if (_section == Section::Featured) {
			*outUnread = (set.flags & MTPDstickerSet_ClientFlag::f_unread);
		}
	}
}

Stickers::Order StickersBox::Inner::getOrder() const {
	Stickers::Order result;
	result.reserve(_rows.size());
	for (int32 i = 0, l = _rows.size(); i < l; ++i) {
		if (_rows.at(i)->disabled || _rows.at(i)->recent) {
			continue;
		}
		result.push_back(_rows.at(i)->id);
	}
	return result;
}

Stickers::Order StickersBox::Inner::getDisabledSets() const {
	Stickers::Order result;
	result.reserve(_rows.size());
	for (int32 i = 0, l = _rows.size(); i < l; ++i) {
		if (_rows.at(i)->disabled) {
			result.push_back(_rows.at(i)->id);
		}
	}
	return result;
}

void StickersBox::Inner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	if (_section == Section::Featured) {
		_visibleTop = visibleTop;
		_visibleBottom = visibleBottom;
		readVisibleSets();
	}
}

void StickersBox::Inner::readVisibleSets() {
	auto itemsVisibleTop = _visibleTop - _itemsTop;
	auto itemsVisibleBottom = _visibleBottom - _itemsTop;
	int rowFrom = floorclamp(itemsVisibleTop, _rowHeight, 0, _rows.size());
	int rowTo = ceilclamp(itemsVisibleBottom, _rowHeight, 0, _rows.size());
	for (int i = rowFrom; i < rowTo; ++i) {
		if (!_rows[i]->unread) {
			continue;
		}
		if (i * _rowHeight < itemsVisibleTop || (i + 1) * _rowHeight > itemsVisibleBottom) {
			continue;
		}
		if (!_rows[i]->sticker || _rows[i]->sticker->thumb->loaded() || _rows[i]->sticker->loaded()) {
			Stickers::markFeaturedAsRead(_rows[i]->id);
		}
	}
}

void StickersBox::Inner::setVisibleScrollbar(int32 width) {
	_scrollbar = width;
}

StickersBox::Inner::~Inner() {
	clear();
}
