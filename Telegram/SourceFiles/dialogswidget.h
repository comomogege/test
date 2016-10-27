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

namespace Dialogs {
class Row;
class FakeRow;
class IndexedList;
} // namespace Dialogs

namespace Ui {
class RoundButton;
} // namespace Ui

class MainWidget;
class PopupMenu;

enum DialogsSearchRequestType {
	DialogsSearchFromStart,
	DialogsSearchFromOffset,
	DialogsSearchPeerFromStart,
	DialogsSearchPeerFromOffset,
	DialogsSearchMigratedFromStart,
	DialogsSearchMigratedFromOffset,
};

class DialogsInner : public SplittedWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	DialogsInner(QWidget *parent, MainWidget *main);

	void dialogsReceived(const QVector<MTPDialog> &dialogs);
	void addSavedPeersAfter(const QDateTime &date);
	void addAllSavedPeers();
	bool searchReceived(const QVector<MTPMessage> &messages, DialogsSearchRequestType type, int32 fullCount);
	void peopleReceived(const QString &query, const QVector<MTPPeer> &people);
	void showMore(int32 pixels);

	void activate();

	void contactsReceived(const QVector<MTPContact> &contacts);

	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

	void selectSkip(int32 direction);
	void selectSkipPage(int32 pixels, int32 direction);

	void createDialog(History *history);
	void dlgUpdated(Dialogs::Mode list, Dialogs::Row *row);
	void dlgUpdated(History *row, MsgId msgId);
	void removeDialog(History *history);

	void loadPeerPhotos(int32 yFrom);
	void clearFilter();
	void refresh(bool toTop = false);

	bool choosePeer();
	void saveRecentHashtags(const QString &text);

	void destroyData();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void scrollToPeer(const PeerId &peer, MsgId msgId);

	typedef QVector<Dialogs::Row*> FilteredDialogs;
	typedef QVector<PeerData*> PeopleResults;
	typedef QVector<Dialogs::FakeRow*> SearchResults;

	Dialogs::IndexedList *contactsList();
	Dialogs::IndexedList *dialogsList();
	FilteredDialogs &filteredList();
	PeopleResults &peopleList();
	SearchResults &searchList();
	int32 lastSearchDate() const;
	PeerData *lastSearchPeer() const;
	MsgId lastSearchId() const;
	MsgId lastSearchMigratedId() const;

	void setMouseSel(bool msel, bool toTop = false);

	enum State {
		DefaultState = 0,
		FilteredState = 1,
		SearchedState = 2,
	};
	void setState(State newState);
	State state() const;
	bool hasFilteredResults() const;

	void searchInPeer(PeerData *peer);

	void onFilterUpdate(QString newFilter, bool force = false);
	void onHashtagFilterUpdate(QStringRef newFilter);

	PeerData *updateFromParentDrag(QPoint globalPos);

	void updateNotifySettings(PeerData *peer);

	void notify_userIsContactChanged(UserData *user, bool fromThisApp);
	void notify_historyMuteUpdated(History *history);

	~DialogsInner();

public slots:
	void onUpdateSelected(bool force = false);
	void onParentGeometryChanged();
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void onPeerPhotoChanged(PeerData *peer);
	void onDialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow);

	void onContextProfile();
	void onContextToggleNotifications();
	void onContextSearch();
	void onContextClearHistory();
	void onContextClearHistorySure();
	void onContextDeleteAndLeave();
	void onContextDeleteAndLeaveSure();
	void onContextToggleBlock();

	void onMenuDestroyed(QObject*);

	void peerUpdated(PeerData *peer);

signals:
	void mustScrollTo(int scrollToTop, int scrollToBottom);
	void dialogMoved(int movedFrom, int movedTo);
	void searchMessages();
	void searchResultChosen();
	void cancelSearchInPeer();
	void completeHashtag(QString tag);
	void refreshHashtags();

protected:
	void paintRegion(Painter &p, const QRegion &region, bool paintingOther);

private:
	void itemRemoved(HistoryItem *item);

	int dialogsOffset() const;
	int filteredOffset() const;
	int peopleOffset() const;
	int searchedOffset() const;

	void peopleResultPaint(PeerData *peer, Painter &p, int32 w, bool active, bool selected, bool onlyBackground) const;
	void searchInPeerPaint(Painter &p, int32 w, bool onlyBackground) const;

	void clearSelection();
	void clearSearchResults(bool clearPeople = true);
	void updateSelectedRow(PeerData *peer = 0);
	bool menuPeerMuted();
	void contextBlockDone(QPair<UserData*, bool> data, const MTPBool &result);

	Dialogs::IndexedList *shownDialogs() const {
		return (Global::DialogsMode() == Dialogs::Mode::Important) ? importantDialogs.get() : dialogs.get();
	}

	using DialogsList = std_::unique_ptr<Dialogs::IndexedList>;
	DialogsList dialogs;
	DialogsList importantDialogs;

	DialogsList contactsNoDialogs;
	DialogsList contacts;

	bool _importantSwitchSel = false;
	Dialogs::Row *_sel = nullptr;
	bool _selByMouse = false;

	QString _filter, _hashtagFilter;

	QStringList _hashtagResults;
	int _hashtagSel = -1;

	FilteredDialogs _filterResults;
	int _filteredSel = -1;

	SearchResults _searchResults;
	int _searchedCount = 0;
	int _searchedMigratedCount = 0;
	int _searchedSel = -1;

	QString _peopleQuery;
	PeopleResults _peopleResults;
	int _peopleSel = -1;

	int _lastSearchDate = 0;
	PeerData *_lastSearchPeer = nullptr;
	MsgId _lastSearchId = 0;
	MsgId _lastSearchMigratedId = 0;

	State _state = DefaultState;

	QPoint lastMousePos;

	void paintDialog(QPainter &p, Dialogs::Row *dialog);

	LinkButton _addContactLnk;
	IconedButton _cancelSearchInPeer;

	bool _overDelete = false;

	PeerData *_searchInPeer = nullptr;
	PeerData *_searchInMigrated = nullptr;
	PeerData *_menuPeer = nullptr;
	PeerData *_menuActionPeer = nullptr;

	PopupMenu *_menu = nullptr;

};

class DialogsWidget : public TWidget, public RPCSender {
	Q_OBJECT

public:
	DialogsWidget(MainWidget *parent);

	void dialogsReceived(const MTPmessages_Dialogs &dialogs, mtpRequestId req);
	void contactsReceived(const MTPcontacts_Contacts &contacts);
	void searchReceived(DialogsSearchRequestType type, const MTPmessages_Messages &result, mtpRequestId req);
	void peopleReceived(const MTPcontacts_Found &result, mtpRequestId req);

	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void updateDragInScroll(bool inScroll);

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void searchInPeer(PeerData *peer);

	void loadDialogs();
	void createDialog(History *history);
	void dlgUpdated(Dialogs::Mode list, Dialogs::Row *row);
	void dlgUpdated(History *row, MsgId msgId);

	void dialogsToUp();

	bool hasTopBarShadow() const {
		return true;
	}
	void showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params);
	void step_show(float64 ms, bool timer);

	void destroyData();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void scrollToPeer(const PeerId &peer, MsgId msgId);

	void removeDialog(History *history);

	Dialogs::IndexedList *contactsList();
	Dialogs::IndexedList *dialogsList();

	void searchMessages(const QString &query, PeerData *inPeer = 0);
	void onSearchMore();

	void updateNotifySettings(PeerData *peer);

	void rpcClear() override {
		_inner.rpcClear();
		RPCSender::rpcClear();
	}

	void notify_userIsContactChanged(UserData *user, bool fromThisApp);
	void notify_historyMuteUpdated(History *history);

signals:

	void cancelled();

public slots:

	void onCancel();
	void onListScroll();
	void activate();
	void onFilterUpdate(bool force = false);
	void onAddContact();
	void onNewGroup();
	bool onCancelSearch();
	void onCancelSearchInPeer();

	void onFilterCursorMoved(int from = -1, int to = -1);
	void onCompleteHashtag(QString tag);

	void onDialogMoved(int movedFrom, int movedTo);
	bool onSearchMessages(bool searchCache = false);
	void onNeedSearchMessages();

	void onChooseByDrag();

private:

	bool _dragInScroll, _dragForward;
	QTimer _chooseByDragTimer;

	void unreadCountsReceived(const QVector<MTPDialog> &dialogs);
	bool dialogsFailed(const RPCError &error, mtpRequestId req);
	bool contactsFailed(const RPCError &error);
	bool searchFailed(DialogsSearchRequestType type, const RPCError &error, mtpRequestId req);
	bool peopleFailed(const RPCError &error, mtpRequestId req);

	bool _dialogsFull;
	int32 _dialogsOffsetDate;
	MsgId _dialogsOffsetId;
	PeerData *_dialogsOffsetPeer;
	mtpRequestId _dialogsRequest, _contactsRequest;

	FlatInput _filter;
	ChildWidget<Ui::RoundButton> _newGroup;
	IconedButton _addContact, _cancelSearch;
	ScrollArea _scroll;
	DialogsInner _inner;

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_progress;

	PeerData *_searchInPeer, *_searchInMigrated;

	QTimer _searchTimer;
	QString _searchQuery, _peopleQuery;
	bool _searchFull, _searchFullMigrated, _peopleFull;
	mtpRequestId _searchRequest, _peopleRequest;

	typedef QMap<QString, MTPmessages_Messages> SearchCache;
	SearchCache _searchCache;

	typedef QMap<mtpRequestId, QString> SearchQueries;
	SearchQueries _searchQueries;

	typedef QMap<QString, MTPcontacts_Found> PeopleCache;
	PeopleCache _peopleCache;

	typedef QMap<mtpRequestId, QString> PeopleQueries;
	PeopleQueries _peopleQueries;

};
