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

#include "abstractbox.h"
#include "core/lambda_wrap.h"
#include "core/observer.h"
#include "core/vector_of_moveable.h"
#include "ui/effects/round_image_checkbox.h"

namespace Dialogs {
class Row;
class IndexedList;
} // namespace Dialogs

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Ui {
class MultiSelect;
} // namespace Ui

QString appendShareGameScoreUrl(const QString &url, const FullMsgId &fullId);
void shareGameScoreByHash(const QString &hash);

class ShareBox : public ItemListBox, public RPCSender {
	Q_OBJECT

public:
	using CopyCallback = base::lambda_unique<void()>;
	using SubmitCallback = base::lambda_unique<void(const QVector<PeerData*> &)>;
	using FilterCallback = base::lambda_unique<bool(PeerData*)>;
	ShareBox(CopyCallback &&copyCallback, SubmitCallback &&submitCallback, FilterCallback &&filterCallback);

private slots:
	void onScroll();

	bool onSearchByUsername(bool searchCache = false);
	void onNeedSearchByUsername();

	void onSubmit();
	void onCopyLink();

	void onMustScrollTo(int top, int bottom);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	void doSetInnerFocus() override;

private:
	void onFilterUpdate(const QString &query);
	void onSelectedChanged();
	void moveButtons();
	void updateButtonsVisibility();
	int getTopScrollSkip() const;
	void updateScrollSkips();

	void addPeerToMultiSelect(PeerData *peer, bool skipAnimation = false);
	void onPeerSelectedChanged(PeerData *peer, bool checked);

	void peopleReceived(const MTPcontacts_Found &result, mtpRequestId requestId);
	bool peopleFailed(const RPCError &error, mtpRequestId requestId);

	CopyCallback _copyCallback;
	SubmitCallback _submitCallback;

	class Inner;
	ChildWidget<Inner> _inner;
	ChildWidget<Ui::MultiSelect> _select;

	ChildWidget<BoxButton> _copy;
	ChildWidget<BoxButton> _share;
	ChildWidget<BoxButton> _cancel;

	ChildWidget<ScrollableBoxShadow> _topShadow;
	ChildWidget<ScrollableBoxShadow> _bottomShadow;

	QTimer _searchTimer;
	QString _peopleQuery;
	bool _peopleFull = false;
	mtpRequestId _peopleRequest = 0;

	using PeopleCache = QMap<QString, MTPcontacts_Found>;
	PeopleCache _peopleCache;

	using PeopleQueries = QMap<mtpRequestId, QString>;
	PeopleQueries _peopleQueries;

	IntAnimation _scrollAnimation;

};

// This class is hold in header because it requires Qt preprocessing.
class ShareBox::Inner : public ScrolledWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, ShareBox::FilterCallback &&filterCallback);

	void setPeerSelectedChangedCallback(base::lambda_unique<void(PeerData *peer, bool selected)> callback);
	void peerUnselected(PeerData *peer);

	QVector<PeerData*> selected() const;
	bool hasSelected() const;

	void peopleReceived(const QString &query, const QVector<MTPPeer> &people);

	void activateSkipRow(int direction);
	void activateSkipColumn(int direction);
	void activateSkipPage(int pageHeight, int direction);
	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;
	void updateFilter(QString filter = QString());

	~Inner();

public slots:
	void onSelectActive();

signals:
	void mustScrollTo(int ymin, int ymax);
	void searchByUsername();

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	int displayedChatsCount() const;

	struct Chat {
		Chat(PeerData *peer, base::lambda_wrap<void()> updateCallback);

		PeerData *peer;
		Ui::RoundImageCheckbox checkbox;
		Text name;
		ColorAnimation nameFg;
	};
	void paintChat(Painter &p, uint64 ms, Chat *chat, int index);
	void updateChat(PeerData *peer);
	void updateChatName(Chat *chat, PeerData *peer);
	void repaintChat(PeerData *peer);
	int chatIndex(PeerData *peer) const;
	void repaintChatAtIndex(int index);
	Chat *getChatAtIndex(int index);

	void loadProfilePhotos(int yFrom);
	void changeCheckState(Chat *chat);
	enum class ChangeStateWay {
		Default,
		SkipCallback,
	};
	void changePeerCheckState(Chat *chat, bool checked, ChangeStateWay useCallback = ChangeStateWay::Default);

	Chat *getChat(Dialogs::Row *row);
	void setActive(int active);
	void updateUpon(const QPoint &pos);

	void refresh();

	float64 _columnSkip = 0.;
	float64 _rowWidthReal = 0.;
	int _rowsLeft = 0;
	int _rowsTop = 0;
	int _rowWidth = 0;
	int _rowHeight = 0;
	int _columnCount = 4;
	int _active = -1;
	int _upon = -1;

	ShareBox::FilterCallback _filterCallback;
	std_::unique_ptr<Dialogs::IndexedList> _chatsIndexed;
	QString _filter;
	using FilteredDialogs = QVector<Dialogs::Row*>;
	FilteredDialogs _filtered;

	using DataMap = QMap<PeerData*, Chat*>;
	DataMap _dataMap;
	using SelectedChats = OrderedSet<PeerData*>;
	SelectedChats _selected;

	base::lambda_unique<void(PeerData *peer, bool selected)> _peerSelectedChangedCallback;

	ChatData *data(Dialogs::Row *row);

	bool _searching = false;
	QString _lastQuery;
	using ByUsernameRows = QVector<PeerData*>;
	using ByUsernameDatas = QVector<Chat*>;
	ByUsernameRows _byUsernameFiltered;
	ByUsernameDatas d_byUsernameFiltered;

};
