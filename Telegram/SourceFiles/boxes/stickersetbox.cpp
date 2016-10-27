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
#include "boxes/stickersetbox.h"

#include "lang.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "stickers/stickers.h"
#include "boxes/confirmbox.h"
#include "apiwrap.h"
#include "localstorage.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_stickers.h"

StickerSetBox::StickerSetBox(const MTPInputStickerSet &set) : ScrollableBox(st::stickersScroll)
, _inner(this, set)
, _shadow(this)
, _add(this, lang(lng_stickers_add_pack), st::defaultBoxButton)
, _share(this, lang(lng_stickers_share_pack), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _done(this, lang(lng_about_done), st::defaultBoxButton) {
	setMaxHeight(st::stickersMaxHeight);
	connect(App::main(), SIGNAL(stickersUpdated()), this, SLOT(onStickersUpdated()));

	init(_inner, st::boxButtonPadding.bottom() + _cancel.height() + st::boxButtonPadding.top());

	connect(&_add, SIGNAL(clicked()), this, SLOT(onAddStickers()));
	connect(&_share, SIGNAL(clicked()), this, SLOT(onShareStickers()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(_inner, SIGNAL(updateButtons()), this, SLOT(onUpdateButtons()));
	connect(scrollArea(), SIGNAL(scrolled()), this, SLOT(onScroll()));

	connect(_inner, SIGNAL(installed(uint64)), this, SLOT(onInstalled(uint64)));

	onStickersUpdated();

	onScroll();

	prepare();
}

void StickerSetBox::onInstalled(uint64 setId) {
	emit installed(setId);
	onClose();
}

void StickerSetBox::onStickersUpdated() {
	showAll();
}

void StickerSetBox::onAddStickers() {
	_inner->install();
}

void StickerSetBox::onShareStickers() {
	QString url = qsl("https://telegram.me/addstickers/") + _inner->shortName();
	QApplication::clipboard()->setText(url);
	Ui::showLayer(new InformBox(lang(lng_stickers_copied)));
}

void StickerSetBox::onUpdateButtons() {
	if (!_cancel.isHidden() || !_done.isHidden()) {
		showAll();
	}
}

void StickerSetBox::onScroll() {
	auto scroll = scrollArea();
	auto scrollTop = scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + scroll->height());
}

void StickerSetBox::showAll() {
	ScrollableBox::showAll();
	int32 cnt = _inner->notInstalled();
	if (_inner->loaded()) {
		_shadow.show();
		if (_inner->notInstalled()) {
			_add.show();
			_cancel.show();
			_share.hide();
			_done.hide();
		} else if (_inner->official()) {
			_add.hide();
			_share.hide();
			_cancel.hide();
			_done.show();
		} else {
			_share.show();
			_cancel.show();
			_add.hide();
			_done.hide();
		}
	} else {
		_shadow.hide();
		_add.hide();
		_share.hide();
		_cancel.show();
		_done.hide();
	}
	resizeEvent(0);
	update();
}

void StickerSetBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, _inner->title());
}

void StickerSetBox::resizeEvent(QResizeEvent *e) {
	ScrollableBox::resizeEvent(e);
	_inner->resize(width(), _inner->height());
	_shadow.setGeometry(0, height() - st::boxButtonPadding.bottom() - _cancel.height() - st::boxButtonPadding.top() - st::lineWidth, width(), st::lineWidth);
	_add.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _add.height());
	_share.moveToRight(st::boxButtonPadding.right(), _add.y());
	_done.moveToRight(st::boxButtonPadding.right(), _add.y());
	if (_add.isHidden() && _share.isHidden()) {
		_cancel.moveToRight(st::boxButtonPadding.right(), _add.y());
	} else if (_add.isHidden()) {
		_cancel.moveToRight(st::boxButtonPadding.right() + _share.width() + st::boxButtonPadding.left(), _add.y());
	} else {
		_cancel.moveToRight(st::boxButtonPadding.right() + _add.width() + st::boxButtonPadding.left(), _add.y());
	}
}

StickerSetBox::Inner::Inner(QWidget *parent, const MTPInputStickerSet &set) : ScrolledWidget(parent)
, _input(set) {
	switch (set.type()) {
	case mtpc_inputStickerSetID: _setId = set.c_inputStickerSetID().vid.v; _setAccess = set.c_inputStickerSetID().vaccess_hash.v; break;
	case mtpc_inputStickerSetShortName: _setShortName = qs(set.c_inputStickerSetShortName().vshort_name); break;
	}
	MTP::send(MTPmessages_GetStickerSet(_input), rpcDone(&Inner::gotSet), rpcFail(&Inner::failedSet));
	App::main()->updateStickers();

	subscribe(FileDownload::ImageLoaded(), [this] { update(); });

	setMouseTracking(true);

	_previewTimer.setSingleShot(true);
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreview()));
}

void StickerSetBox::Inner::gotSet(const MTPmessages_StickerSet &set) {
	_pack.clear();
	_emoji.clear();
	_packOvers.clear();
	_selected = -1;
	setCursor(style::cur_default);
	if (set.type() == mtpc_messages_stickerSet) {
		auto &d = set.c_messages_stickerSet();
		auto &v = d.vdocuments.c_vector().v;
		_pack.reserve(v.size());
		_packOvers.reserve(v.size());
		for (int i = 0, l = v.size(); i < l; ++i) {
			auto doc = App::feedDocument(v.at(i));
			if (!doc || !doc->sticker()) continue;

			_pack.push_back(doc);
			_packOvers.push_back(FloatAnimation());
		}
		auto &packs(d.vpacks.c_vector().v);
		for (int i = 0, l = packs.size(); i < l; ++i) {
			if (packs.at(i).type() != mtpc_stickerPack) continue;
			auto &pack(packs.at(i).c_stickerPack());
			if (auto e = emojiGetNoColor(emojiFromText(qs(pack.vemoticon)))) {
				auto &stickers(pack.vdocuments.c_vector().v);
				StickerPack p;
				p.reserve(stickers.size());
				for (int32 j = 0, c = stickers.size(); j < c; ++j) {
					DocumentData *doc = App::document(stickers.at(j).v);
					if (!doc || !doc->sticker()) continue;

					p.push_back(doc);
				}
				_emoji.insert(e, p);
			}
		}
		if (d.vset.type() == mtpc_stickerSet) {
			auto &s(d.vset.c_stickerSet());
			_setTitle = stickerSetTitle(s);
			_title = st::boxTitleFont->elided(_setTitle, width() - st::boxTitlePosition.x() - st::boxTitleHeight);
			_setShortName = qs(s.vshort_name);
			_setId = s.vid.v;
			_setAccess = s.vaccess_hash.v;
			_setCount = s.vcount.v;
			_setHash = s.vhash.v;
			_setFlags = s.vflags.v;
			auto &sets = Global::RefStickerSets();
			auto it = sets.find(_setId);
			if (it != sets.cend()) {
				auto clientFlags = it->flags & (MTPDstickerSet_ClientFlag::f_featured | MTPDstickerSet_ClientFlag::f_not_loaded | MTPDstickerSet_ClientFlag::f_unread | MTPDstickerSet_ClientFlag::f_special);
				_setFlags |= clientFlags;
				it->flags = _setFlags;
				it->stickers = _pack;
				it->emoji = _emoji;
			}
		}
	}

	if (_pack.isEmpty()) {
		Ui::showLayer(new InformBox(lang(lng_stickers_not_found)));
	} else {
		int32 rows = _pack.size() / StickerPanPerRow + ((_pack.size() % StickerPanPerRow) ? 1 : 0);
		resize(st::stickersPadding.left() + StickerPanPerRow * st::stickersSize.width(), st::stickersPadding.top() + rows * st::stickersSize.height() + st::stickersPadding.bottom());
	}
	_loaded = true;

	updateSelected();

	emit updateButtons();
}

bool StickerSetBox::Inner::failedSet(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_loaded = true;

	Ui::showLayer(new InformBox(lang(lng_stickers_not_found)));

	return true;
}

void StickerSetBox::Inner::installDone(const MTPmessages_StickerSetInstallResult &result) {
	auto &sets = Global::RefStickerSets();

	bool wasArchived = (_setFlags & MTPDstickerSet::Flag::f_archived);
	if (wasArchived) {
		auto index = Global::RefArchivedStickerSetsOrder().indexOf(_setId);
		if (index >= 0) {
			Global::RefArchivedStickerSetsOrder().removeAt(index);
		}
	}
	_setFlags &= ~MTPDstickerSet::Flag::f_archived;
	_setFlags |= MTPDstickerSet::Flag::f_installed;
	auto it = sets.find(_setId);
	if (it == sets.cend()) {
		it = sets.insert(_setId, Stickers::Set(_setId, _setAccess, _setTitle, _setShortName, _setCount, _setHash, _setFlags));
	} else {
		it->flags = _setFlags;
	}
	it->stickers = _pack;
	it->emoji = _emoji;

	auto &order = Global::RefStickerSetsOrder();
	int insertAtIndex = 0, currentIndex = order.indexOf(_setId);
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, _setId);
	}

	auto custom = sets.find(Stickers::CustomSetId);
	if (custom != sets.cend()) {
		for_const (auto sticker, _pack) {
			int removeIndex = custom->stickers.indexOf(sticker);
			if (removeIndex >= 0) custom->stickers.removeAt(removeIndex);
		}
		if (custom->stickers.isEmpty()) {
			sets.erase(custom);
		}
	}

	if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
		Stickers::applyArchivedResult(result.c_messages_stickerSetInstallResultArchive());
	} else {
		if (wasArchived) {
			Local::writeArchivedStickers();
		}
		Local::writeInstalledStickers();
		emit App::main()->stickersUpdated();
	}
	emit installed(_setId);
}

bool StickerSetBox::Inner::installFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	Ui::showLayer(new InformBox(lang(lng_stickers_not_found)));

	return true;
}

void StickerSetBox::Inner::mousePressEvent(QMouseEvent *e) {
	int index = stickerFromGlobalPos(e->globalPos());
	if (index >= 0 && index < _pack.size()) {
		_previewTimer.start(QApplication::startDragTime());
	}
}

void StickerSetBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateSelected();
	if (_previewShown >= 0) {
		int index = stickerFromGlobalPos(e->globalPos());
		if (index >= 0 && index < _pack.size() && index != _previewShown) {
			_previewShown = index;
			Ui::showMediaPreview(_pack.at(_previewShown));
		}
	}
}

void StickerSetBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	if (_previewShown >= 0) {
		_previewShown = -1;
		return;
	}
	if (_previewTimer.isActive()) {
		_previewTimer.stop();
		int index = stickerFromGlobalPos(e->globalPos());
		if (index >= 0 && index < _pack.size()) {
			if (auto main = App::main()) {
				if (main->onSendSticker(_pack.at(index))) {
					Ui::hideSettingsAndLayer();
				}
			}
		}
	}
}

void StickerSetBox::Inner::updateSelected() {
	auto index = stickerFromGlobalPos(QCursor::pos());
	if (isMasksSet()) {
		index = -1;
	}
	if (index != _selected) {
		startOverAnimation(_selected, 1., 0.);
		_selected = index;
		startOverAnimation(_selected, 0., 1.);
		setCursor(_selected >= 0 ? style::cur_pointer : style::cur_default);
	}
}

void StickerSetBox::Inner::startOverAnimation(int index, float64 from, float64 to) {
	if (index >= 0 && index < _packOvers.size()) {
		_packOvers[index].start([this, index] {
			int row = index / StickerPanPerRow;
			int column = index % StickerPanPerRow;
			int left = st::stickersPadding.left() + column * st::stickersSize.width();
			int top = st::stickersPadding.top() + row * st::stickersSize.height();
			rtlupdate(left, top, st::stickersSize.width(), st::stickersSize.height());
		}, from, to, st::emojiPanDuration);
	}
}

void StickerSetBox::Inner::onPreview() {
	int index = stickerFromGlobalPos(QCursor::pos());
	if (index >= 0 && index < _pack.size()) {
		_previewShown = index;
		Ui::showMediaPreview(_pack.at(_previewShown));
	}
}

int32 StickerSetBox::Inner::stickerFromGlobalPos(const QPoint &p) const {
	QPoint l(mapFromGlobal(p));
	if (rtl()) l.setX(width() - l.x());
	int32 row = (l.y() >= st::stickersPadding.top()) ? qFloor((l.y() - st::stickersPadding.top()) / st::stickersSize.height()) : -1;
	int32 col = (l.x() >= st::stickersPadding.left()) ? qFloor((l.x() - st::stickersPadding.left()) / st::stickersSize.width()) : -1;
	if (row >= 0 && col >= 0 && col < StickerPanPerRow) {
		int32 result = row * StickerPanPerRow + col;
		return (result < _pack.size()) ? result : -1;
	}
	return -1;
}

void StickerSetBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	if (_pack.isEmpty()) return;

	int32 rows = _pack.size() / StickerPanPerRow + ((_pack.size() % StickerPanPerRow) ? 1 : 0);
	int32 from = qFloor(e->rect().top() / st::stickersSize.height()), to = qFloor(e->rect().bottom() / st::stickersSize.height()) + 1;

	for (int32 i = from; i < to; ++i) {
		for (int32 j = 0; j < StickerPanPerRow; ++j) {
			int32 index = i * StickerPanPerRow + j;
			if (index >= _pack.size()) break;
			t_assert(index < _packOvers.size());

			DocumentData *doc = _pack.at(index);
			QPoint pos(st::stickersPadding.left() + j * st::stickersSize.width(), st::stickersPadding.top() + i * st::stickersSize.height());

			if (auto over = _packOvers[index].current((index == _selected) ? 1. : 0.)) {
				p.setOpacity(over);
				QPoint tl(pos);
				if (rtl()) tl.setX(width() - tl.x() - st::stickersSize.width());
				App::roundRect(p, QRect(tl, st::stickersSize), st::emojiPanHover, StickerHoverCorners);
				p.setOpacity(1);

			}
			bool goodThumb = !doc->thumb->isNull() && ((doc->thumb->width() >= 128) || (doc->thumb->height() >= 128));
			if (goodThumb) {
				doc->thumb->load();
			} else {
				if (doc->status == FileReady) {
					doc->automaticLoad(0);
				}
				if (doc->sticker()->img->isNull() && doc->loaded(DocumentData::FilePathResolveChecked)) {
					doc->sticker()->img = doc->data().isEmpty() ? ImagePtr(doc->filepath()) : ImagePtr(doc->data());
				}
			}

			float64 coef = qMin((st::stickersSize.width() - st::buttonRadius * 2) / float64(doc->dimensions.width()), (st::stickersSize.height() - st::buttonRadius * 2) / float64(doc->dimensions.height()));
			if (coef > 1) coef = 1;
			int32 w = qRound(coef * doc->dimensions.width()), h = qRound(coef * doc->dimensions.height());
			if (w < 1) w = 1;
			if (h < 1) h = 1;
			QPoint ppos = pos + QPoint((st::stickersSize.width() - w) / 2, (st::stickersSize.height() - h) / 2);
			if (goodThumb) {
				p.drawPixmapLeft(ppos, width(), doc->thumb->pix(w, h));
			} else if (!doc->sticker()->img->isNull()) {
				p.drawPixmapLeft(ppos, width(), doc->sticker()->img->pix(w, h));
			}
		}
	}
}

void StickerSetBox::Inner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
}

bool StickerSetBox::Inner::loaded() const {
	return _loaded && !_pack.isEmpty();
}

int32 StickerSetBox::Inner::notInstalled() const {
	if (!_loaded) return 0;
	auto it = Global::StickerSets().constFind(_setId);
	if (it == Global::StickerSets().cend() || !(it->flags & MTPDstickerSet::Flag::f_installed) || (it->flags & MTPDstickerSet::Flag::f_archived)) return _pack.size();
	return 0;
}

bool StickerSetBox::Inner::official() const {
	return _loaded && _setShortName.isEmpty();
}

QString StickerSetBox::Inner::title() const {
	return _loaded ? (_pack.isEmpty() ? lang(lng_attach_failed) : _title) : lang(lng_contacts_loading);
}

QString StickerSetBox::Inner::shortName() const {
	return _setShortName;
}

void StickerSetBox::Inner::install() {
	if (isMasksSet()) {
		Ui::showLayer(new InformBox(lang(lng_stickers_masks_pack)), KeepOtherLayers);
		return;
	}
	if (_installRequest) return;
	_installRequest = MTP::send(MTPmessages_InstallStickerSet(_input, MTP_bool(false)), rpcDone(&Inner::installDone), rpcFail(&Inner::installFail));
}

StickerSetBox::Inner::~Inner() {
}
