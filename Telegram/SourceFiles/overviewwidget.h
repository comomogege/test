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

#include "window/section_widget.h"
#include "ui/popupmenu.h"

namespace Overview {
namespace Layout {
class AbstractItem;
class ItemBase;
class Date;
} // namespace Layout
} // namespace Overview

namespace Ui {
class PlainShadow;
} // namespace Ui

class OverviewWidget;
class OverviewInner : public TWidget, public AbstractTooltipShower, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	OverviewInner(OverviewWidget *overview, ScrollArea *scroll, PeerData *peer, MediaOverviewType type);

	void activate();

	void clear();
	int32 itemTop(const FullMsgId &msgId) const;

	bool preloadLocal();
	void preloadMore();

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);

	void dragActionStart(const QPoint &screenPos, Qt::MouseButton button = Qt::LeftButton);
	void dragActionUpdate(const QPoint &screenPos);
	void dragActionFinish(const QPoint &screenPos, Qt::MouseButton button = Qt::LeftButton);
	void dragActionCancel();

	void touchScrollUpdated(const QPoint &screenPos);
	QPoint mapMouseToItem(QPoint p, MsgId itemId, int32 itemIndex);

	int32 resizeToWidth(int32 nwidth, int32 scrollTop, int32 minHeight, bool force = false); // returns new scroll top
	void dropResizeIndex();

	PeerData *peer() const;
	PeerData *migratePeer() const;
	MediaOverviewType type() const;
	void switchType(MediaOverviewType type);

	void setSelectMode(bool enabled);

	void mediaOverviewUpdated();
	void changingMsgId(HistoryItem *row, MsgId newId);
	void repaintItem(const HistoryItem *msg);

	void getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const;
	void clearSelectedItems(bool onlyTextSelection = false);
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete = true);

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

	~OverviewInner();

protected:
	bool event(QEvent *e) override;
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

public slots:
	void onUpdateSelected();

	void copyContextUrl();
	void cancelContextDownload();
	void showContextInFolder();
	void saveContextFile();

	void goToMessage();
	void deleteMessage();
	void forwardMessage();
	void selectMessage();

	void onSearchUpdate();
	void onCancel();
	bool onCancelSearch();

	void onMenuDestroy(QObject *obj);
	void onTouchSelect();
	void onTouchScrollTimer();

	void onDragExec();

	bool onSearchMessages(bool searchCache = false);
	void onNeedSearchMessages();

private:
	void itemRemoved(HistoryItem *item);
	MsgId complexMsgId(const HistoryItem *item) const;

	bool itemMigrated(MsgId msgId) const;
	ChannelId itemChannel(MsgId msgId) const;
	MsgId itemMsgId(MsgId msgId) const;
	int32 migratedIndexSkip() const;

	void fixItemIndex(int32 &current, MsgId msgId) const;
	bool itemHasPoint(MsgId msgId, int32 index, int32 x, int32 y) const;
	int32 itemHeight(MsgId msgId, int32 index) const;
	void moveToNextItem(MsgId &msgId, int32 &index, MsgId upTo, int32 delta) const;

	void updateDragSelection(MsgId dragSelFrom, int32 dragSelFromIndex, MsgId dragSelTo, int32 dragSelToIndex, bool dragSelecting);

	void repaintItem(MsgId itemId, int32 itemIndex);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	void applyDragSelection();
	void addSelectionRange(int32 selFrom, int32 selTo, History *history);

	void recountMargins();
	int32 countHeight();

	OverviewWidget *_overview;
	ScrollArea *_scroll;
	int _resizeIndex = -1;
	int _resizeSkip = 0;

	PeerData *_peer;
	MediaOverviewType _type;
	bool _reversed;
	History *_migrated, *_history;
	ChannelId _channel;

	bool _selMode = false;
	TextSelection itemSelectedValue(int32 index) const;

	int _rowsLeft = 0;
	int _rowWidth = 0;

	typedef QVector<Overview::Layout::AbstractItem*> Items;
	Items _items;
	typedef QMap<HistoryItem*, Overview::Layout::ItemBase*> LayoutItems;
	LayoutItems _layoutItems;
	typedef QMap<int32, Overview::Layout::Date*> LayoutDates;
	LayoutDates _layoutDates;
	Overview::Layout::ItemBase *layoutPrepare(HistoryItem *item);
	Overview::Layout::AbstractItem *layoutPrepare(const QDate &date, bool month);
	int32 setLayoutItem(int32 index, Overview::Layout::AbstractItem *item, int32 top);

	FlatInput _search;
	IconedButton _cancelSearch;
	QVector<MsgId> _results;
	int32 _itemsToBeLoaded;

	// photos
	int32 _photosInRow = 1;

	QTimer _searchTimer;
	QString _searchQuery;
	bool _inSearch = false;
	bool _searchFull = false;
	bool _searchFullMigrated = false;
	mtpRequestId _searchRequest = 0;
	History::MediaOverview _searchResults;
	MsgId _lastSearchId = 0;
	MsgId _lastSearchMigratedId = 0;
	int _searchedCount = 0;
	enum SearchRequestType {
		SearchFromStart,
		SearchFromOffset,
		SearchMigratedFromStart,
		SearchMigratedFromOffset
	};
	void searchReceived(SearchRequestType type, const MTPmessages_Messages &result, mtpRequestId req);
	bool searchFailed(SearchRequestType type, const RPCError &error, mtpRequestId req);

	typedef QMap<QString, MTPmessages_Messages> SearchCache;
	SearchCache _searchCache;

	typedef QMap<mtpRequestId, QString> SearchQueries;
	SearchQueries _searchQueries;

	int _width = 0;
	int _height = 0;
	int _minHeight = 0;
	int _marginTop = 0;
	int _marginBottom = 0;

	QTimer _linkTipTimer;

	// selection support, like in HistoryWidget
	style::cursor _cursor = style::cur_default;
	HistoryCursorState _cursorState = HistoryDefaultCursorState;
	using SelectedItems = QMap<MsgId, TextSelection>;
	SelectedItems _selected;
	enum DragAction {
		NoDrag = 0x00,
		PrepareDrag = 0x01,
		Dragging = 0x02,
		PrepareSelect = 0x03,
		Selecting = 0x04,
	};
	DragAction _dragAction = NoDrag;
	QPoint _dragStartPos, _dragPos;
	MsgId _dragItem = 0;
	MsgId _selectedMsgId = 0;
	int _dragItemIndex = -1;
	MsgId _mousedItem = 0;
	int _mousedItemIndex = -1;
	uint16 _dragSymbol;
	bool _dragWasInactive = false;

	ClickHandlerPtr _contextMenuLnk;

	MsgId _dragSelFrom = 0;
	MsgId _dragSelTo = 0;
	int _dragSelFromIndex = -1;
	int _dragSelToIndex = -1;
	bool _dragSelecting = false;

	bool _touchScroll = false;
	bool _touchSelect = false;
	bool _touchInProgress = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	QTimer _touchSelectTimer;

	TouchScrollState _touchScrollState = TouchScrollManual;
	bool _touchPrevPosValid = false;
	bool _touchWaitingAcceleration = false;
	QPoint _touchSpeed;
	uint64 _touchSpeedTime = 0;
	uint64 _touchAccelerationTime = 0;
	uint64 _touchTime = 0;
	QTimer _touchScrollTimer;

	PopupMenu *_menu = nullptr;
};

class OverviewWidget : public TWidget, public RPCSender {
	Q_OBJECT

public:
	OverviewWidget(QWidget *parent, PeerData *peer, MediaOverviewType type);

	void clear();

	void scrollBy(int32 add);
	void scrollReset();

	void paintTopBar(Painter &p, float64 over, int32 decreaseWidth);
	void topBarClick();

	PeerData *peer() const;
	PeerData *migratePeer() const;
	MediaOverviewType type() const;
	void switchType(MediaOverviewType type);
	void updateTopBarSelection();

	int32 lastWidth() const;
	int32 lastScrollTop() const;
	int32 countBestScroll() const;

	void fastShow(bool back = false, int32 lastScrollTop = -1);
	bool hasTopBarShadow() const {
		return true;
	}
	void setLastScrollTop(int lastScrollTop);
	void showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params);
	void step_show(float64 ms, bool timer);

	void doneShow();

	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void itemRemoved(HistoryItem *item);

	QPoint clampMousePosition(QPoint point);

	void checkSelectingScroll(QPoint point);
	void noSelectingScroll();

	bool touchScroll(const QPoint &delta);

	void fillSelectedItems(SelectedItemSet &sel, bool forDelete);

	void updateScrollColors();

	void updateAfterDrag();

	void grabStart() override {
		_inGrab = true;
		resizeEvent(0);
	}
	void grapWithoutTopBarShadow();
	void grabFinish() override;
	void rpcClear() override {
		_inner.rpcClear();
		RPCSender::rpcClear();
	}

	void ui_repaintHistoryItem(const HistoryItem *item);

	void notify_historyItemLayoutChanged(const HistoryItem *item);

	~OverviewWidget();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

public slots:
	void activate();
	void onScroll();

	void onScrollTimer();

	void onForwardSelected();
	void onDeleteSelected();
	void onDeleteSelectedSure();
	void onDeleteContextSure();
	void onClearSelected();

private:
	ScrollArea _scroll;
	OverviewInner _inner;
	bool _noDropResizeIndex = false;

	QString _header;

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_progress;

	int32 _scrollSetAfterShow = 0;

	QTimer _scrollTimer;
	int32 _scrollDelta = 0;

	int32 _selCount = 0;

	ChildWidget<Ui::PlainShadow> _topShadow;
	bool _inGrab = false;

};

