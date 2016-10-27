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

#include "title.h"
#include "pspecific.h"
#include "ui/effects/rect_shadow.h"
#include "platform/platform_main_window.h"
#include "core/single_timer.h"

class MediaView;
class TitleWidget;
class PasscodeWidget;
class IntroWidget;
class MainWidget;
class LayerStackWidget;
class LayerWidget;

namespace Local {
class ClearManager;
} // namespace Local

namespace Settings {
class Widget;
} // namespace Settings

class ConnectingWidget : public QWidget {
	Q_OBJECT

public:
	ConnectingWidget(QWidget *parent, const QString &text, const QString &reconnect);
	void set(const QString &text, const QString &reconnect);
	void paintEvent(QPaintEvent *e);

public slots:
	void onReconnect();

private:
	Ui::RectShadow _shadow;
	QString _text;
	int32 _textWidth;
	LinkButton _reconnect;

};

class MediaPreviewWidget;

class MainWindow : public Platform::MainWindow, private base::Subscriber {
	Q_OBJECT

public:
	MainWindow();
	~MainWindow();

	void init();
	void firstShow();

	QWidget *filedialogParent();

	bool eventFilter(QObject *obj, QEvent *evt);

	void inactivePress(bool inactive);
	bool inactivePress() const;

	void wStartDrag(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void closeEvent(QCloseEvent *e);

	void paintEvent(QPaintEvent *e);

	void resizeEvent(QResizeEvent *e);

	void setupPasscode(bool anim);
	void clearPasscode();
	void checkAutoLockIn(int msec);
	void setupIntro(bool anim);
	void setupMain(bool anim, const MTPUser *user = 0);
	void serviceNotification(const QString &msg, const MTPMessageMedia &media = MTP_messageMediaEmpty(), bool force = false);
	void sendServiceHistoryRequest();
	void showDelayedServiceMsgs();

	void mtpStateChanged(int32 dc, int32 state);

	TitleWidget *getTitle();

	HitTestType hitTest(const QPoint &p) const;
	QRect iconRect() const;

	QRect clientRect() const;
	QRect photoRect() const;

	IntroWidget *introWidget();
	MainWidget *mainWidget();
	PasscodeWidget *passcodeWidget();

	void showPhoto(const PhotoOpenClickHandler *lnk, HistoryItem *item = 0);
	void showPhoto(PhotoData *photo, HistoryItem *item);
	void showPhoto(PhotoData *photo, PeerData *item);
	void showDocument(DocumentData *doc, HistoryItem *item);

	bool doWeReadServerHistory() const;

	void activate();

	void noIntro(IntroWidget *was);
	void noMain(MainWidget *was);
	void noLayerStack(LayerStackWidget *was);
	void layerFinishedHide(LayerStackWidget *was);

	void checkHistoryActivation();

	void fixOrder();

	enum TempDirState {
		TempDirRemoving,
		TempDirExists,
		TempDirEmpty,
	};
	TempDirState tempDirState();
	TempDirState localStorageState();
	void tempDirDelete(int task);

	void notifySettingGot();
	void notifySchedule(History *history, HistoryItem *item);
	void notifyClear(History *history = 0);
	void notifyClearFast();
	void notifyUpdateAll();

	QImage iconLarge() const;

	void sendPaths();

	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void documentUpdated(DocumentData *doc);
	void changingMsgId(HistoryItem *row, MsgId newId);

	bool isActive(bool cached = true) const;
	void hideMediaview();

	void updateUnreadCounter();

	QImage iconWithCounter(int size, int count, style::color bg, bool smallIcon);

	bool contentOverlapped(const QRect &globalRect);
	bool contentOverlapped(QWidget *w, QPaintEvent *e) {
		return contentOverlapped(QRect(w->mapToGlobal(e->rect().topLeft()), e->rect().size()));
	}
	bool contentOverlapped(QWidget *w, const QRegion &r) {
		return contentOverlapped(QRect(w->mapToGlobal(r.boundingRect().topLeft()), r.boundingRect().size()));
	}

	void ui_showLayer(LayerWidget *box, ShowLayerOptions options);
	void ui_hideSettingsAndLayer(ShowLayerOptions options);
	bool ui_isLayerShown();
	bool ui_isMediaViewShown();
	void ui_showMediaPreview(DocumentData *document);
	void ui_showMediaPreview(PhotoData *photo);
	void ui_hideMediaPreview();
	PeerData *ui_getPeerForMouseAction();

public slots:
	void updateIsActive(int timeout = 0);

	void checkAutoLock();

	void showSettings();
	void layerHidden();
	void setInnerFocus();
	void updateConnectingStatus();

	void quitFromTray();
	void showFromTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	bool minimizeToTray();
	void toggleTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	void toggleDisplayNotifyFromTray();

	void onInactiveTimer();

	void onClearFinished(int task, void *manager);
	void onClearFailed(int task, void *manager);

	void notifyShowNext();
	void updateTrayMenu(bool force = false);

	void onShowAddContact();
	void onShowNewGroup();
	void onShowNewChannel();
	void onLogout();
	void onLogoutSure();
	void updateGlobalMenu(); // for OS X top menu

	void onReActivate();

	void app_activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button);

signals:
	void resized(const QSize &size);
	void tempDirCleared(int task);
	void tempDirClearFailed(int task);
	void newAuthorization();

private slots:
	void onStateChanged(Qt::WindowState state);
	void onSettingsDestroyed(QObject *was);

	void onWindowActiveChanged();

private:
	void showConnecting(const QString &text, const QString &reconnect = QString());
	void hideConnecting();

	void updateControlsGeometry();

	QPixmap grabInner();

	void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color);
	QImage icon16, icon32, icon64, iconbig16, iconbig32, iconbig64;

	QWidget *centralwidget;

	typedef QPair<QString, MTPMessageMedia> DelayedServiceMsg;
	QVector<DelayedServiceMsg> _delayedServiceMsgs;
	mtpRequestId _serviceHistoryRequest = 0;

	TitleWidget *title = nullptr;
	PasscodeWidget *_passcode = nullptr;
	IntroWidget *intro = nullptr;
	MainWidget *main = nullptr;
	ChildWidget<Settings::Widget> settings = { nullptr };
	ChildWidget<LayerStackWidget> layerBg = { nullptr };
	std_::unique_ptr<MediaPreviewWidget> _mediaPreview;

	QTimer _isActiveTimer;
	bool _isActive = false;

	ChildWidget<ConnectingWidget> _connecting = { nullptr };

	Local::ClearManager *_clearManager = nullptr;

	void clearWidgets();

	bool dragging = false;
	QPoint dragStart;

	bool _inactivePress = false;
	QTimer _inactiveTimer;

	SingleTimer _autoLockTimer;
	uint64 _shouldLockAt = 0;

	using NotifyWhenMap = QMap<MsgId, uint64>;
	using NotifyWhenMaps = QMap<History*, NotifyWhenMap>;
	NotifyWhenMaps _notifyWhenMaps;
	struct NotifyWaiter {
		NotifyWaiter(MsgId msg, uint64 when, PeerData *notifyByFrom)
		: msg(msg)
		, when(when)
		, notifyByFrom(notifyByFrom) {
		}
		MsgId msg;
		uint64 when;
		PeerData *notifyByFrom;
	};
	using NotifyWaiters = QMap<History*, NotifyWaiter>;
	NotifyWaiters _notifyWaiters;
	NotifyWaiters _notifySettingWaiters;
	SingleTimer _notifyWaitTimer;

	using NotifyWhenAlert = QMap<uint64, PeerData*>;
	using NotifyWhenAlerts = QMap<History*, NotifyWhenAlert>;
	NotifyWhenAlerts _notifyWhenAlerts;

	MediaView *_mediaView = nullptr;

};

class PreLaunchWindow : public TWidget {
public:

	PreLaunchWindow(QString title = QString());
	void activate();
	int basicSize() const {
		return _size;
	}
	~PreLaunchWindow();

	static PreLaunchWindow *instance();

protected:

	int _size;

};

class PreLaunchLabel : public QLabel {
public:
	PreLaunchLabel(QWidget *parent);
	void setText(const QString &text);
};

class PreLaunchInput : public QLineEdit {
public:
	PreLaunchInput(QWidget *parent, bool password = false);
};

class PreLaunchLog : public QTextEdit {
public:
	PreLaunchLog(QWidget *parent);
};

class PreLaunchButton : public QPushButton {
public:
	PreLaunchButton(QWidget *parent, bool confirm = true);
	void setText(const QString &text);
};

class PreLaunchCheckbox : public QCheckBox {
public:
	PreLaunchCheckbox(QWidget *parent);
	void setText(const QString &text);
};

class NotStartedWindow : public PreLaunchWindow {
public:

	NotStartedWindow();

protected:

	void closeEvent(QCloseEvent *e);
	void resizeEvent(QResizeEvent *e);

private:

	void updateControls();

	PreLaunchLabel _label;
	PreLaunchLog _log;
	PreLaunchButton _close;

};

class LastCrashedWindow : public PreLaunchWindow {
	 Q_OBJECT

public:

	LastCrashedWindow();

public slots:

	void onViewReport();
	void onSaveReport();
	void onSendReport();
	void onGetApp();

	void onNetworkSettings();
	void onNetworkSettingsSaved(QString host, quint32 port, QString username, QString password);
	void onContinue();

	void onCheckingFinished();
	void onSendingError(QNetworkReply::NetworkError e);
	void onSendingFinished();
	void onSendingProgress(qint64 uploaded, qint64 total);

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	void onUpdateRetry();
	void onUpdateSkip();

	void onUpdateChecking();
	void onUpdateLatest();
	void onUpdateDownloading(qint64 ready, qint64 total);
	void onUpdateReady();
	void onUpdateFailed();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

protected:

	void closeEvent(QCloseEvent *e);
	void resizeEvent(QResizeEvent *e);

private:

	QString minidumpFileName();
	void updateControls();

	QString _host, _username, _password;
	quint32 _port;

	PreLaunchLabel _label, _pleaseSendReport, _yourReportName, _minidump;
	PreLaunchLog _report;
	PreLaunchButton _send, _sendSkip, _networkSettings, _continue, _showReport, _saveReport, _getApp;
	PreLaunchCheckbox _includeUsername;

	QString _minidumpName, _minidumpFull, _reportText;
	QString _reportUsername, _reportTextNoUsername;
	QByteArray getCrashReportRaw() const;

	bool _reportShown, _reportSaved;

	void excludeReportUsername();

	enum SendingState {
		SendingNoReport,
		SendingUpdateCheck,
		SendingNone,
		SendingTooOld,
		SendingTooMany,
		SendingUnofficial,
		SendingProgress,
		SendingUploading,
		SendingFail,
		SendingDone,
	};
	SendingState _sendingState;

	PreLaunchLabel _updating;
	qint64 _sendingProgress, _sendingTotal;

	QNetworkAccessManager _sendManager;
	QNetworkReply *_checkReply, *_sendReply;

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	PreLaunchButton _updatingCheck, _updatingSkip;
	enum UpdatingState {
		UpdatingNone,
		UpdatingCheck,
		UpdatingLatest,
		UpdatingDownload,
		UpdatingFail,
		UpdatingReady
	};
	UpdatingState _updatingState;
	QString _newVersionDownload;

	void setUpdatingState(UpdatingState state, bool force = false);
	void setDownloadProgress(qint64 ready, qint64 total);
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

	QString getReportField(const QLatin1String &name, const QLatin1String &prefix);
	void addReportFieldPart(const QLatin1String &name, const QLatin1String &prefix, QHttpMultiPart *multipart);

};

class NetworkSettingsWindow : public PreLaunchWindow {
	Q_OBJECT

public:

	NetworkSettingsWindow(QWidget *parent, QString host, quint32 port, QString username, QString password);

signals:

	void saved(QString host, quint32 port, QString username, QString password);

public slots:

	void onSave();

protected:

	void closeEvent(QCloseEvent *e);
	void resizeEvent(QResizeEvent *e);

private:

	void updateControls();

	PreLaunchLabel _hostLabel, _portLabel, _usernameLabel, _passwordLabel;
	PreLaunchInput _hostInput, _portInput, _usernameInput, _passwordInput;
	PreLaunchButton _save, _cancel;

	QWidget *_parent;

};

class ShowCrashReportWindow : public PreLaunchWindow {
public:

	ShowCrashReportWindow(const QString &text);

protected:

	void resizeEvent(QResizeEvent *e);
    void closeEvent(QCloseEvent *e);

private:

	PreLaunchLog _log;

};

#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
int showCrashReportWindow(const QString &crashdump);
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
