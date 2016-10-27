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
#include "mainwindow.h"

#include "dialogs/dialogs_layout.h"
#include "styles/style_dialogs.h"
#include "ui/popupmenu.h"
#include "zip.h"
#include "lang.h"
#include "shortcuts.h"
#include "application.h"
#include "pspecific.h"
#include "title.h"
#include "passcodewidget.h"
#include "intro/introwidget.h"
#include "mainwidget.h"
#include "layerwidget.h"
#include "boxes/confirmbox.h"
#include "boxes/contactsbox.h"
#include "boxes/addcontactbox.h"
#include "observer_peer.h"
#include "autoupdater.h"
#include "mediaview.h"
#include "localstorage.h"
#include "apiwrap.h"
#include "settings/settings_widget.h"
#include "window/notifications_manager.h"
#include "platform/platform_notifications_manager.h"

ConnectingWidget::ConnectingWidget(QWidget *parent, const QString &text, const QString &reconnect) : QWidget(parent)
, _shadow(st::boxShadow)
, _reconnect(this, QString()) {
	set(text, reconnect);
	connect(&_reconnect, SIGNAL(clicked()), this, SLOT(onReconnect()));
}

void ConnectingWidget::set(const QString &text, const QString &reconnect) {
	_text = text;
	_textWidth = st::linkFont->width(_text) + st::linkFont->spacew;
	int32 _reconnectWidth = 0;
	if (reconnect.isEmpty()) {
		_reconnect.hide();
	} else {
		_reconnect.setText(reconnect);
		_reconnect.show();
		_reconnect.move(st::connectingPadding.left() + _textWidth, st::boxShadow.height() + st::connectingPadding.top());
		_reconnectWidth = _reconnect.width();
	}
	resize(st::connectingPadding.left() + _textWidth + _reconnectWidth + st::connectingPadding.right() + st::boxShadow.width(), st::boxShadow.height() + st::connectingPadding.top() + st::linkFont->height + st::connectingPadding.bottom());
	update();
}

void ConnectingWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	_shadow.paint(p, QRect(0, st::boxShadow.height(), width() - st::boxShadow.width(), height() - st::boxShadow.height()), 0, Ui::RectShadow::Side::Top | Ui::RectShadow::Side::Right);
	p.fillRect(0, st::boxShadow.height(), width() - st::boxShadow.width(), height() - st::boxShadow.height(), st::connectingBG->b);
	p.setFont(st::linkFont->f);
	p.setPen(st::connectingColor->p);
	p.drawText(st::connectingPadding.left(), st::boxShadow.height() + st::connectingPadding.top() + st::linkFont->ascent, _text);
}

void ConnectingWidget::onReconnect() {
	MTP::restart();
}

MainWindow::MainWindow() {
	icon16 = icon256.scaledToWidth(16, Qt::SmoothTransformation);
	icon32 = icon256.scaledToWidth(32, Qt::SmoothTransformation);
	icon64 = icon256.scaledToWidth(64, Qt::SmoothTransformation);
	iconbig16 = iconbig256.scaledToWidth(16, Qt::SmoothTransformation);
	iconbig32 = iconbig256.scaledToWidth(32, Qt::SmoothTransformation);
	iconbig64 = iconbig256.scaledToWidth(64, Qt::SmoothTransformation);

	subscribe(Global::RefNotifySettingsChanged(), [this](Notify::ChangeType type) {
		if (type == Notify::ChangeType::DesktopEnabled) {
			updateTrayMenu();
			notifyClear();
		} else if (type == Notify::ChangeType::ViewParams) {
			notifyUpdateAll();
		} else if (type == Notify::ChangeType::IncludeMuted) {
			Notify::unreadCounterUpdated();
		}
	});

	if (objectName().isEmpty()) {
		setObjectName(qsl("MainWindow"));
	}
	resize(st::wndDefWidth, st::wndDefHeight);

	setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
	centralwidget = new QWidget(this);
	centralwidget->setObjectName(qsl("centralwidget"));
	setCentralWidget(centralwidget);

	QMetaObject::connectSlotsByName(this);

	_inactiveTimer.setSingleShot(true);
	connect(&_inactiveTimer, SIGNAL(timeout()), this, SLOT(onInactiveTimer()));

	connect(&_notifyWaitTimer, SIGNAL(timeout()), this, SLOT(notifyShowNext()));

	_isActiveTimer.setSingleShot(true);
	connect(&_isActiveTimer, SIGNAL(timeout()), this, SLOT(updateIsActive()));

	connect(&_autoLockTimer, SIGNAL(timeout()), this, SLOT(checkAutoLock()));

	subscribe(Global::RefSelfChanged(), [this]() { updateGlobalMenu(); });

	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void MainWindow::inactivePress(bool inactive) {
	_inactivePress = inactive;
	if (_inactivePress) {
		_inactiveTimer.start(200);
	} else {
		_inactiveTimer.stop();
	}
}

bool MainWindow::inactivePress() const {
	return _inactivePress;
}

void MainWindow::onInactiveTimer() {
	inactivePress(false);
}

void MainWindow::onStateChanged(Qt::WindowState state) {
	stateChangedHook(state);

	psUserActionDone();

	updateIsActive((state == Qt::WindowMinimized) ? Global::OfflineBlurTimeout() : Global::OnlineFocusTimeout());

	psUpdateSysMenu(state);
	if (state == Qt::WindowMinimized && cWorkMode() == dbiwmTrayOnly) {
		App::wnd()->minimizeToTray();
	}
	psSavePosition(state);
}

void MainWindow::init() {
	psInitFrameless();
	setWindowIcon(wndIcon);

	Application::instance()->installEventFilter(this);
	connect(windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(onStateChanged(Qt::WindowState)));
	connect(windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWindowActiveChanged()), Qt::QueuedConnection);

	QPalette p(palette());
	p.setColor(QPalette::Window, st::windowBg->c);
	setPalette(p);

	title = new TitleWidget(this);

    psInitSize();
}

void MainWindow::onWindowActiveChanged() {
	checkHistoryActivation();
	QTimer::singleShot(1, this, SLOT(updateTrayMenu()));
}

void MainWindow::firstShow() {
#ifdef Q_OS_WIN
	trayIconMenu = new PopupMenu();
	trayIconMenu->deleteOnHide(false);
#else // Q_OS_WIN
	trayIconMenu = new QMenu(this);
#endif // else for Q_OS_WIN

	auto isLinux = (cPlatform() == dbipLinux32 || cPlatform() == dbipLinux64);
	auto notificationActionText = lang(Global::DesktopNotify()
		? lng_disable_notifications_from_tray
		: lng_enable_notifications_from_tray);

	if (isLinux) {
		trayIconMenu->addAction(lang(lng_open_from_tray), this, SLOT(showFromTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_minimize_to_tray), this, SLOT(minimizeToTray()))->setEnabled(true);
		trayIconMenu->addAction(notificationActionText, this, SLOT(toggleDisplayNotifyFromTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_quit_from_tray), this, SLOT(quitFromTray()))->setEnabled(true);
	} else {
		trayIconMenu->addAction(lang(lng_minimize_to_tray), this, SLOT(minimizeToTray()))->setEnabled(true);
		trayIconMenu->addAction(notificationActionText, this, SLOT(toggleDisplayNotifyFromTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_quit_from_tray), this, SLOT(quitFromTray()))->setEnabled(true);
	}
	psUpdateWorkmode();
	psFirstShow();
	updateTrayMenu();

	_mediaView = new MediaView();
}

QWidget *MainWindow::filedialogParent() {
	return (_mediaView && _mediaView->isVisible()) ? (QWidget*)_mediaView : (QWidget*)this;
}

void MainWindow::clearWidgets() {
	Ui::hideLayer(true);
	if (_passcode) {
		_passcode->hide();
		_passcode->deleteLater();
		_passcode = 0;
	}
	if (main) {
		delete main;
		main = nullptr;
	}
	if (intro) {
		intro->stop_show();
		intro->hide();
		intro->deleteLater();
		intro->rpcClear();
		intro = 0;
	}
	if (_mediaView) {
		hideMediaview();
		_mediaView->rpcClear();
	}
	title->updateControlsVisibility();
	updateGlobalMenu();
}

QPixmap MainWindow::grabInner() {
	QPixmap result;
	if (settings) {
		result = myGrab(settings);
	} else if (intro) {
		result = myGrab(intro);
	} else if (main) {
		result = myGrab(main);
	} else if (_passcode) {
		result = myGrab(_passcode);
	}
	return result;
}

void MainWindow::clearPasscode() {
	if (!_passcode) return;

	QPixmap bg = grabInner();

	_passcode->stop_show();
	_passcode->hide();
	_passcode->deleteLater();
	_passcode = 0;
	if (intro) {
		intro->animShow(bg, true);
	} else {
		main->animShow(bg, true);
	}
	notifyUpdateAll();
	title->updateControlsVisibility();
	updateGlobalMenu();

	if (auto main = App::main()) {
		main->checkStartUrl();
	}
}

void MainWindow::setupPasscode(bool anim) {
	QPixmap bg = grabInner();

	if (_passcode) {
		_passcode->stop_show();
		_passcode->hide();
		_passcode->deleteLater();
	}
	_passcode = new PasscodeWidget(this);
	_passcode->move(0, st::titleHeight);
	if (main) main->hide();
	if (settings) {
		settings->deleteLater();
	}
	if (intro) intro->hide();
	if (anim) {
		_passcode->animShow(bg);
	} else {
		setInnerFocus();
	}
	_shouldLockAt = 0;
	notifyUpdateAll();
	title->updateControlsVisibility();
	updateGlobalMenu();
}

void MainWindow::checkAutoLockIn(int msec) {
	if (_autoLockTimer.isActive()) {
		int remain = _autoLockTimer.remainingTime();
		if (remain > 0 && remain <= msec) return;
	}
	_autoLockTimer.start(msec);
}

void MainWindow::checkAutoLock() {
	if (!Global::LocalPasscode() || App::passcoded()) return;

	App::app()->checkLocalTime();
	uint64 ms = getms(true), idle = psIdleTime(), should = Global::AutoLock() * 1000ULL;
	if (idle >= should || (_shouldLockAt > 0 && ms > _shouldLockAt + 3000ULL)) {
		setupPasscode(true);
	} else {
		_shouldLockAt = ms + (should - idle);
		_autoLockTimer.start(should - idle);
	}
}

void MainWindow::setupIntro(bool anim) {
	cSetContactsReceived(false);
	cSetDialogsReceived(false);
	if (intro && !intro->isHidden() && !main) return;

	if (_mediaView) {
		_mediaView->clearData();
	}
	Ui::hideSettingsAndLayer(true);

	QPixmap bg = anim ? grabInner() : QPixmap();

	clearWidgets();
	intro = new IntroWidget(this);
	intro->move(0, st::titleHeight);
	if (anim) {
		intro->animShow(bg);
	}

	fixOrder();

	updateConnectingStatus();

	_delayedServiceMsgs.clear();
	if (_serviceHistoryRequest) {
		MTP::cancel(_serviceHistoryRequest);
		_serviceHistoryRequest = 0;
	}
}

void MainWindow::serviceNotification(const QString &msg, const MTPMessageMedia &media, bool force) {
	History *h = (main && App::userLoaded(ServiceUserId)) ? App::history(ServiceUserId) : 0;
	if (!h || (!force && h->isEmpty())) {
		_delayedServiceMsgs.push_back(DelayedServiceMsg(msg, media));
		return sendServiceHistoryRequest();
	}

	main->serviceNotification(msg, media);
}

void MainWindow::showDelayedServiceMsgs() {
	QVector<DelayedServiceMsg> toAdd = _delayedServiceMsgs;
	_delayedServiceMsgs.clear();
	for (QVector<DelayedServiceMsg>::const_iterator i = toAdd.cbegin(), e = toAdd.cend(); i != e; ++i) {
		serviceNotification(i->first, i->second, true);
	}
}

void MainWindow::sendServiceHistoryRequest() {
	if (!main || !main->started() || _delayedServiceMsgs.isEmpty() || _serviceHistoryRequest) return;

	UserData *user = App::userLoaded(ServiceUserId);
	if (!user) {
		MTPDuser::Flags userFlags = MTPDuser::Flag::f_first_name | MTPDuser::Flag::f_phone | MTPDuser::Flag::f_status | MTPDuser::Flag::f_verified;
		user = App::feedUsers(MTP_vector<MTPUser>(1, MTP_user(MTP_flags(userFlags), MTP_int(ServiceUserId), MTPlong(), MTP_string("Telegram"), MTPstring(), MTPstring(), MTP_string("42777"), MTP_userProfilePhotoEmpty(), MTP_userStatusRecently(), MTPint(), MTPstring(), MTPstring())));
	}
	_serviceHistoryRequest = MTP::send(MTPmessages_GetHistory(user->input, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(1), MTP_int(0), MTP_int(0)), main->rpcDone(&MainWidget::serviceHistoryDone), main->rpcFail(&MainWidget::serviceHistoryFail));
}

void MainWindow::setupMain(bool anim, const MTPUser *self) {
	QPixmap bg = anim ? grabInner() : QPixmap();
	clearWidgets();
	main = new MainWidget(this);
	main->move(0, st::titleHeight);
	if (anim) {
		main->animShow(bg);
	} else {
		main->activate();
	}
	if (self) {
		main->start(*self);
	} else {
		MTP::send(MTPusers_GetUsers(MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())), main->rpcDone(&MainWidget::startFull));
	}
	title->updateControlsVisibility();

	fixOrder();

	updateConnectingStatus();
}

void MainWindow::updateUnreadCounter() {
	if (!Global::started() || App::quitting()) return;

	title->updateCounter();
	psUpdateCounter();
}

void MainWindow::showSettings() {
	if (_passcode) return;

	if (isHidden()) showFromTray();

	if (settings) {
		Ui::hideSettingsAndLayer();
		return;
	}

	if (!layerBg) {
		layerBg = new LayerStackWidget(this);
	}
	settings = new Settings::Widget();
	connect(settings, SIGNAL(destroyed(QObject*)), this, SLOT(onSettingsDestroyed(QObject*)));
	layerBg->showSpecialLayer(settings);
}

void MainWindow::ui_hideSettingsAndLayer(ShowLayerOptions options) {
	if (layerBg) {
		layerBg->onClose();
	}
}

void MainWindow::mtpStateChanged(int32 dc, int32 state) {
	if (dc == MTP::maindc()) {
		updateConnectingStatus();
		Global::RefConnectionTypeChanged().notify();
	}
}

void MainWindow::updateConnectingStatus() {
	int32 state = MTP::dcstate();
	if (state == MTP::ConnectingState || state == MTP::DisconnectedState || (state < 0 && state > -600)) {
		if (main || getms() > 5000 || _connecting) {
			showConnecting(lang(lng_connecting));
		}
	} else if (state < 0) {
		showConnecting(lng_reconnecting(lt_count, ((-state) / 1000) + 1), lang(lng_reconnecting_try_now));
		QTimer::singleShot((-state) % 1000, this, SLOT(updateConnectingStatus()));
	} else {
		hideConnecting();
	}
}

IntroWidget *MainWindow::introWidget() {
	return intro;
}

MainWidget *MainWindow::mainWidget() {
	return main;
}

PasscodeWidget *MainWindow::passcodeWidget() {
	return _passcode;
}

void MainWindow::showPhoto(const PhotoOpenClickHandler *lnk, HistoryItem *item) {
	return (!item && lnk->peer()) ? showPhoto(lnk->photo(), lnk->peer()) : showPhoto(lnk->photo(), item);
}

void MainWindow::showPhoto(PhotoData *photo, HistoryItem *item) {
	if (_mediaView->isHidden()) Ui::hideLayer(true);
	_mediaView->showPhoto(photo, item);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void MainWindow::showPhoto(PhotoData *photo, PeerData *peer) {
	if (_mediaView->isHidden()) Ui::hideLayer(true);
	_mediaView->showPhoto(photo, peer);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void MainWindow::showDocument(DocumentData *doc, HistoryItem *item) {
	if (_mediaView->isHidden()) Ui::hideLayer(true);
	_mediaView->showDocument(doc, item);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void MainWindow::ui_showLayer(LayerWidget *box, ShowLayerOptions options) {
	if (box) {
		if (!layerBg) {
			layerBg = new LayerStackWidget(this);
		}
		if (options.testFlag(KeepOtherLayers)) {
			if (options.testFlag(ShowAfterOtherLayers)) {
				layerBg->prependLayer(box);
			} else {
				layerBg->appendLayer(box);
			}
		} else {
			layerBg->showLayer(box);
		}
		if (options.testFlag(ForceFastShowLayer)) {
			layerBg->showFast();
		}
	} else {
		if (layerBg) {
			if (settings) {
				layerBg->onCloseLayers();
			} else {
				layerBg->onClose();
				if (options.testFlag(ForceFastShowLayer)) {
					layerBg->hide();
					layerBg->deleteLater();
					layerBg = nullptr;
				}
			}
		}
		hideMediaview();
	}
}

bool MainWindow::ui_isLayerShown() {
	return !!layerBg;
}

bool MainWindow::ui_isMediaViewShown() {
	return _mediaView && !_mediaView->isHidden();
}

void MainWindow::ui_showMediaPreview(DocumentData *document) {
	if (!document || ((!document->isAnimation() || !document->loaded()) && !document->sticker())) return;
	if (!_mediaPreview) {
		_mediaPreview = std_::make_unique<MediaPreviewWidget>(this);
		updateControlsGeometry();
	}
	if (_mediaPreview->isHidden()) {
		fixOrder();
	}
	_mediaPreview->showPreview(document);
}

void MainWindow::ui_showMediaPreview(PhotoData *photo) {
	if (!photo) return;
	if (!_mediaPreview) {
		_mediaPreview = std_::make_unique<MediaPreviewWidget>(this);
		updateControlsGeometry();
	}
	if (_mediaPreview->isHidden()) {
		fixOrder();
	}
	_mediaPreview->showPreview(photo);
}

void MainWindow::ui_hideMediaPreview() {
	if (!_mediaPreview) return;
	_mediaPreview->hidePreview();
}

PeerData *MainWindow::ui_getPeerForMouseAction() {
	if (_mediaView && !_mediaView->isHidden()) {
		return _mediaView->ui_getPeerForMouseAction();
	} else if (main) {
		return main->ui_getPeerForMouseAction();
	}
	return nullptr;
}

void MainWindow::showConnecting(const QString &text, const QString &reconnect) {
	if (_connecting) {
		_connecting->set(text, reconnect);
	} else {
		_connecting.create(this, text, reconnect);
		_connecting->show();
		updateControlsGeometry();
		fixOrder();
	}
}

void MainWindow::hideConnecting() {
	if (_connecting) {
		_connecting.destroyDelayed();
	}
}

bool MainWindow::doWeReadServerHistory() const {
	return isActive(false) && main && !Ui::isLayerShown() && main->doWeReadServerHistory();
}

void MainWindow::checkHistoryActivation() {
	if (main && MTP::authedId() && doWeReadServerHistory()) {
		main->markActiveHistoryAsRead();
	}
}

void MainWindow::layerHidden() {
	if (layerBg) {
		layerBg->hide();
		layerBg->deleteLater();
	}
	layerBg = nullptr;
	hideMediaview();
	setInnerFocus();
}

void MainWindow::onReActivate() {
	if (auto w = App::wnd()) {
		if (auto f = QApplication::focusWidget()) {
			f->clearFocus();
		}
		w->windowHandle()->requestActivate();
		w->activate();
		if (auto f = QApplication::focusWidget()) {
			f->clearFocus();
		}
		w->setInnerFocus();
    }
}

void MainWindow::hideMediaview() {
    if (_mediaView && !_mediaView->isHidden()) {
        _mediaView->hide();
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
		onReActivate();
		QTimer::singleShot(200, this, SLOT(onReActivate()));
#endif
    }
}

bool MainWindow::contentOverlapped(const QRect &globalRect) {
	if (main && main->contentOverlapped(globalRect)) return true;
	if (layerBg && layerBg->contentOverlapped(globalRect)) return true;
	return false;
}

void MainWindow::setInnerFocus() {
	if (layerBg && layerBg->canSetFocus()) {
		layerBg->setInnerFocus();
	} else if (_passcode) {
		_passcode->setInnerFocus();
	} else if (settings) {
		settings->setInnerFocus();
	} else if (main) {
		main->setInnerFocus();
	}
}

QRect MainWindow::clientRect() const {
	return QRect(0, st::titleHeight, width(), height() - st::titleHeight);
}

QRect MainWindow::photoRect() const {
	if (settings) {
		return settings->geometry();
	} else if (main) {
		QRect r(main->historyRect());
		r.moveLeft(r.left() + main->x());
		r.moveTop(r.top() + main->y());
		return r;
	}
	return QRect(0, 0, 0, 0);
}

void MainWindow::wStartDrag(QMouseEvent *e) {
	dragStart = e->globalPos() - frameGeometry().topLeft();
	dragging = true;
}

void MainWindow::paintEvent(QPaintEvent *e) {
}

HitTestType MainWindow::hitTest(const QPoint &p) const {
	int x(p.x()), y(p.y()), w(width()), h(height());

	const int32 raw = psResizeRowWidth();
	if (!windowState().testFlag(Qt::WindowMaximized)) {
		if (y < raw) {
			if (x < raw) {
				return HitTestType::TopLeft;
			} else if (x > w - raw - 1) {
				return HitTestType::TopRight;
			}
			return HitTestType::Top;
		} else if (y > h - raw - 1) {
			if (x < raw) {
				return HitTestType::BottomLeft;
			} else if (x > w - raw - 1) {
				return HitTestType::BottomRight;
			}
			return HitTestType::Bottom;
		} else if (x < raw) {
			return HitTestType::Left;
		} else if (x > w - raw - 1) {
			return HitTestType::Right;
		}
	}
	auto titleTest = title->hitTest(p - title->geometry().topLeft());
	if (titleTest != HitTestType::None) {
		return titleTest;
	} else if (x >= 0 && y >= 0 && x < w && y < h) {
		return HitTestType::Client;
	}
	return HitTestType::None;
}

QRect MainWindow::iconRect() const {
	return title->iconRect();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e) {
	switch (e->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::KeyPress:
	case QEvent::TouchBegin:
	case QEvent::Wheel:
		psUserActionDone();
		break;

	case QEvent::MouseMove:
		if (main && main->isIdle()) {
			psUserActionDone();
			main->checkIdleFinish();
		}
		break;

	case QEvent::MouseButtonRelease:
		Ui::hideMediaPreview();
		break;

	case QEvent::ShortcutOverride: // handle shortcuts ourselves
		return true;

	case QEvent::Shortcut:
		DEBUG_LOG(("Shortcut event catched: %1").arg(static_cast<QShortcutEvent*>(e)->key().toString()));
		if (Shortcuts::launch(static_cast<QShortcutEvent*>(e)->shortcutId())) {
			return true;
		}
		break;

	case QEvent::ApplicationActivate:
		if (obj == Application::instance()) {
			psUserActionDone();
			QTimer::singleShot(1, this, SLOT(onWindowActiveChanged()));
		}
		break;

	case QEvent::FileOpen:
		if (obj == Application::instance()) {
			QString url = static_cast<QFileOpenEvent*>(e)->url().toEncoded().trimmed();
			if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
				cSetStartUrl(url.mid(0, 8192));
				if (auto main = App::main()) {
					main->checkStartUrl();
				}
			}
			activate();
		}
		break;

	case QEvent::WindowStateChange:
		if (obj == this) {
			Qt::WindowState state = (windowState() & Qt::WindowMinimized) ? Qt::WindowMinimized : ((windowState() & Qt::WindowMaximized) ? Qt::WindowMaximized : ((windowState() & Qt::WindowFullScreen) ? Qt::WindowFullScreen : Qt::WindowNoState));
			onStateChanged(state);
		}
		break;

	case QEvent::Move:
	case QEvent::Resize:
		if (obj == this) {
			psUpdatedPosition();
		}
		break;
	}

	return Platform::MainWindow::eventFilter(obj, e);
}

void MainWindow::mouseMoveEvent(QMouseEvent *e) {
	if (e->buttons() & Qt::LeftButton) {
		if (dragging) {
			if (windowState().testFlag(Qt::WindowMaximized)) {
				setWindowState(windowState() & ~Qt::WindowMaximized);

				dragStart = e->globalPos() - frameGeometry().topLeft();
			} else {
				move(e->globalPos() - dragStart);
			}
		}
	} else if (dragging) {
		dragging = false;
	}
}

void MainWindow::mouseReleaseEvent(QMouseEvent *e) {
	dragging = false;
}

bool MainWindow::minimizeToTray() {
    if (App::quitting() || !psHasTrayIcon()) return false;

	closeWithoutDestroy();
    if (cPlatform() == dbipWindows && trayIcon && !cSeenTrayTooltip()) {
		trayIcon->showMessage(str_const_toString(AppName), lang(lng_tray_icon_text), QSystemTrayIcon::Information, 10000);
		cSetSeenTrayTooltip(true);
		Local::writeSettings();
	}
	updateIsActive(Global::OfflineBlurTimeout());
	updateTrayMenu();
	updateGlobalMenu();
	return true;
}

void MainWindow::updateTrayMenu(bool force) {
    if (!trayIconMenu || (cPlatform() == dbipWindows && !force)) return;

	auto iconMenu = trayIconMenu;
	auto actions = iconMenu->actions();
	auto isLinux = (cPlatform() == dbipLinux32 || cPlatform() == dbipLinux64);
	if (isLinux) {
		auto minimizeAction = actions.at(1);
		minimizeAction->setDisabled(!isVisible());
	} else {
		auto active = isActive(false);
		auto toggleAction = actions.at(0);
		disconnect(toggleAction, SIGNAL(triggered(bool)), this, SLOT(minimizeToTray()));
		disconnect(toggleAction, SIGNAL(triggered(bool)), this, SLOT(showFromTray()));
		connect(toggleAction, SIGNAL(triggered(bool)), this, active ? SLOT(minimizeToTray()) : SLOT(showFromTray()));
		toggleAction->setText(lang(active ? lng_minimize_to_tray : lng_open_from_tray));

		// On macOS just remove trayIcon menu if the window is not active.
		// So we will activate the window on click instead of showing the menu.
		if (!active && (cPlatform() == dbipMac || cPlatform() == dbipMacOld)) {
			iconMenu = nullptr;
		}
	}
	auto notificationAction = actions.at(isLinux ? 2 : 1);
	auto notificationActionText = lang(Global::DesktopNotify()
		? lng_disable_notifications_from_tray
		: lng_enable_notifications_from_tray);
	notificationAction->setText(notificationActionText);

#ifndef Q_OS_WIN
    if (trayIcon && trayIcon->contextMenu() != iconMenu) {
		trayIcon->setContextMenu(iconMenu);
    }
#endif // !Q_OS_WIN

    psTrayMenuUpdated();
}

void MainWindow::onShowAddContact() {
	if (isHidden()) showFromTray();

	if (main) main->showAddContact();
}

void MainWindow::onShowNewGroup() {
	if (isHidden()) showFromTray();

	if (main) Ui::showLayer(new GroupInfoBox(CreatingGroupGroup, false), KeepOtherLayers);
}

void MainWindow::onShowNewChannel() {
	if (isHidden()) showFromTray();

	if (main) Ui::showLayer(new GroupInfoBox(CreatingGroupChannel, false), KeepOtherLayers);
}

void MainWindow::onLogout() {
	if (isHidden()) showFromTray();

	ConfirmBox *box = new ConfirmBox(lang(lng_sure_logout), lang(lng_settings_logout), st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onLogoutSure()));
	Ui::showLayer(box);
}

void MainWindow::onLogoutSure() {
	App::logOut();
}

void MainWindow::updateGlobalMenu() {
#ifdef Q_OS_MAC
	if (App::wnd()) {
		psMacUpdateMenu();
	}
#endif
}

void MainWindow::quitFromTray() {
	App::quit();
}

void MainWindow::activate() {
	bool wasHidden = !isVisible();
	setWindowState(windowState() & ~Qt::WindowMinimized);
	setVisible(true);
	psActivateProcess();
	activateWindow();
	updateIsActive(Global::OnlineFocusTimeout());
	if (wasHidden) {
		if (main) {
			main->windowShown();
		}
	}
}

void MainWindow::noIntro(IntroWidget *was) {
	if (was == intro) {
		intro = nullptr;
	}
}

void MainWindow::onSettingsDestroyed(QObject *was) {
	if (was == settings) {
		settings = nullptr;
	}
	checkHistoryActivation();
}

void MainWindow::noMain(MainWidget *was) {
	if (was == main) {
		main = 0;
	}
}

void MainWindow::noLayerStack(LayerStackWidget *was) {
	if (was == layerBg) {
		layerBg = nullptr;
	}
}

void MainWindow::layerFinishedHide(LayerStackWidget *was) {
	if (was == layerBg) {
		QTimer::singleShot(0, this, SLOT(layerHidden()));
	}
}

void MainWindow::fixOrder() {
	title->raise();
	if (layerBg) layerBg->raise();
	if (_mediaPreview) _mediaPreview->raise();
	if (_connecting) _connecting->raise();
}

void MainWindow::showFromTray(QSystemTrayIcon::ActivationReason reason) {
	if (reason != QSystemTrayIcon::Context) {
        QTimer::singleShot(1, this, SLOT(updateTrayMenu()));
        QTimer::singleShot(1, this, SLOT(updateGlobalMenu()));
        activate();
		Notify::unreadCounterUpdated();
	}
}

void MainWindow::toggleTray(QSystemTrayIcon::ActivationReason reason) {
	if ((cPlatform() == dbipMac || cPlatform() == dbipMacOld) && isActive(false)) return;
	if (reason == QSystemTrayIcon::Context) {
		updateTrayMenu(true);
		QTimer::singleShot(1, this, SLOT(psShowTrayMenu()));
	} else {
		if (isActive(false)) {
			minimizeToTray();
		} else {
			showFromTray(reason);
		}
	}
}

void MainWindow::toggleDisplayNotifyFromTray() {
	if (App::passcoded()) {
		if (!isActive()) showFromTray();
		Ui::showLayer(new InformBox(lang(lng_passcode_need_unblock)));
		return;
	}

	bool soundNotifyChanged = false;
	Global::SetDesktopNotify(!Global::DesktopNotify());
	if (Global::DesktopNotify()) {
		if (Global::RestoreSoundNotifyFromTray() && !Global::SoundNotify()) {
			Global::SetSoundNotify(true);
			Global::SetRestoreSoundNotifyFromTray(false);
			soundNotifyChanged = true;
		}
	} else {
		if (Global::SoundNotify()) {
			Global::SetSoundNotify(false);
			Global::SetRestoreSoundNotifyFromTray(true);
			soundNotifyChanged = true;
		} else {
			Global::SetRestoreSoundNotifyFromTray(false);
		}
	}
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::DesktopEnabled);
	if (soundNotifyChanged) {
		Global::RefNotifySettingsChanged().notify(Notify::ChangeType::SoundEnabled);
	}
}

void MainWindow::closeEvent(QCloseEvent *e) {
	if (Sandbox::isSavingSession()) {
		e->accept();
		App::quit();
	} else {
		e->ignore();
		if (!MTP::authedId() || !Ui::hideWindowNoQuit()) {
			App::quit();
		}
	}
}

TitleWidget *MainWindow::getTitle() {
	return title;
}

void MainWindow::resizeEvent(QResizeEvent *e) {
	if (!title) return;

	Adaptive::Layout layout = Adaptive::OneColumnLayout;
	if (width() > st::adaptiveWideWidth) {
		layout = Adaptive::WideLayout;
	} else if (width() >= st::adaptiveNormalWidth) {
		layout = Adaptive::NormalLayout;
	}
	if (layout != Global::AdaptiveLayout()) {
		Global::SetAdaptiveLayout(layout);
		Adaptive::Changed().notify(true);
	}
	updateControlsGeometry();
	emit resized(QSize(width(), height() - st::titleHeight));
}

void MainWindow::updateControlsGeometry() {
	title->setGeometry(0, 0, width(), st::titleHeight);
	if (layerBg) layerBg->resize(width(), height());
	if (_mediaPreview) _mediaPreview->setGeometry(0, title->height(), width(), height() - title->height());
	if (_connecting) _connecting->setGeometry(0, height() - _connecting->height(), _connecting->width(), _connecting->height());
}

MainWindow::TempDirState MainWindow::tempDirState() {
	if (_clearManager && _clearManager->hasTask(Local::ClearManagerDownloads)) {
		return TempDirRemoving;
	}
	return QDir(cTempDir()).exists() ? TempDirExists : TempDirEmpty;
}

MainWindow::TempDirState MainWindow::localStorageState() {
	if (_clearManager && _clearManager->hasTask(Local::ClearManagerStorage)) {
		return TempDirRemoving;
	}
	return (Local::hasImages() || Local::hasStickers() || Local::hasWebFiles() || Local::hasAudios()) ? TempDirExists : TempDirEmpty;
}

void MainWindow::tempDirDelete(int task) {
	if (_clearManager) {
		if (_clearManager->addTask(task)) {
			return;
		} else {
			_clearManager->stop();
			_clearManager = nullptr;
		}
	}
	_clearManager = new Local::ClearManager();
	_clearManager->addTask(task);
	connect(_clearManager, SIGNAL(succeed(int,void*)), this, SLOT(onClearFinished(int,void*)));
	connect(_clearManager, SIGNAL(failed(int,void*)), this, SLOT(onClearFailed(int,void*)));
	_clearManager->start();
}

void MainWindow::onClearFinished(int task, void *manager) {
	if (manager && manager == _clearManager) {
		_clearManager->stop();
		_clearManager = nullptr;
	}
	emit tempDirCleared(task);
}

void MainWindow::onClearFailed(int task, void *manager) {
	if (manager && manager == _clearManager) {
		_clearManager->stop();
		_clearManager = nullptr;
	}
	emit tempDirClearFailed(task);
}

void MainWindow::notifySchedule(History *history, HistoryItem *item) {
	if (App::quitting() || !history->currentNotification() || !App::api()) return;

	PeerData *notifyByFrom = (!history->peer->isUser() && item->mentionsMe()) ? item->from() : 0;

	if (item->isSilent()) {
		history->popNotification(item);
		return;
	}

	bool haveSetting = (history->peer->notify != UnknownNotifySettings);
	if (haveSetting) {
		if (history->peer->notify != EmptyNotifySettings && history->peer->notify->mute > unixtime()) {
			if (notifyByFrom) {
				haveSetting = (item->from()->notify != UnknownNotifySettings);
				if (haveSetting) {
					if (notifyByFrom->notify != EmptyNotifySettings && notifyByFrom->notify->mute > unixtime()) {
						history->popNotification(item);
						return;
					}
				} else {
					App::api()->requestNotifySetting(notifyByFrom);
				}
			} else {
				history->popNotification(item);
				return;
			}
		}
	} else {
		if (notifyByFrom && notifyByFrom->notify == UnknownNotifySettings) {
			App::api()->requestNotifySetting(notifyByFrom);
		}
		App::api()->requestNotifySetting(history->peer);
	}
	if (!item->notificationReady()) {
		haveSetting = false;
	}

	int delay = item->Has<HistoryMessageForwarded>() ? 500 : 100, t = unixtime();
	uint64 ms = getms(true);
	bool isOnline = main->lastWasOnline(), otherNotOld = ((cOtherOnline() * uint64(1000)) + Global::OnlineCloudTimeout() > t * uint64(1000));
	bool otherLaterThanMe = (cOtherOnline() * uint64(1000) + (ms - main->lastSetOnline()) > t * uint64(1000));
	if (!isOnline && otherNotOld && otherLaterThanMe) {
		delay = Global::NotifyCloudDelay();
	} else if (cOtherOnline() >= t) {
		delay = Global::NotifyDefaultDelay();
	}

	uint64 when = ms + delay;
	_notifyWhenAlerts[history].insert(when, notifyByFrom);
	if (Global::DesktopNotify() && !Platform::Notifications::skipToast()) {
		NotifyWhenMaps::iterator i = _notifyWhenMaps.find(history);
		if (i == _notifyWhenMaps.end()) {
			i = _notifyWhenMaps.insert(history, NotifyWhenMap());
		}
		if (i.value().constFind(item->id) == i.value().cend()) {
			i.value().insert(item->id, when);
		}
		NotifyWaiters *addTo = haveSetting ? &_notifyWaiters : &_notifySettingWaiters;
		NotifyWaiters::const_iterator it = addTo->constFind(history);
		if (it == addTo->cend() || it->when > when) {
			addTo->insert(history, NotifyWaiter(item->id, when, notifyByFrom));
		}
	}
	if (haveSetting) {
		if (!_notifyWaitTimer.isActive() || _notifyWaitTimer.remainingTime() > delay) {
			_notifyWaitTimer.start(delay);
		}
	}
}

void MainWindow::notifyClear(History *history) {
	if (!history) {
		Window::Notifications::manager()->clearAll();

		for (auto i = _notifyWhenMaps.cbegin(), e = _notifyWhenMaps.cend(); i != e; ++i) {
			i.key()->clearNotifications();
		}
		_notifyWhenMaps.clear();
		_notifyWhenAlerts.clear();
		_notifyWaiters.clear();
		_notifySettingWaiters.clear();
		return;
	}

	Window::Notifications::manager()->clearFromHistory(history);

	history->clearNotifications();
	_notifyWhenMaps.remove(history);
	_notifyWhenAlerts.remove(history);
	_notifyWaiters.remove(history);
	_notifySettingWaiters.remove(history);

	_notifyWaitTimer.stop();
	notifyShowNext();
}

void MainWindow::notifyClearFast() {
	Window::Notifications::manager()->clearAllFast();

	_notifyWhenMaps.clear();
	_notifyWhenAlerts.clear();
	_notifyWaiters.clear();
	_notifySettingWaiters.clear();
}

void MainWindow::notifySettingGot() {
	int32 t = unixtime();
	for (NotifyWaiters::iterator i = _notifySettingWaiters.begin(); i != _notifySettingWaiters.end();) {
		History *history = i.key();
		bool loaded = false, muted = false;
		if (history->peer->notify != UnknownNotifySettings) {
			if (history->peer->notify == EmptyNotifySettings || history->peer->notify->mute <= t) {
				loaded = true;
			} else if (PeerData *from = i.value().notifyByFrom) {
				if (from->notify != UnknownNotifySettings) {
					if (from->notify == EmptyNotifySettings || from->notify->mute <= t) {
						loaded = true;
					} else {
						loaded = muted = true;
					}
				}
			} else {
				loaded = muted = true;
			}
		}
		if (loaded) {
			if (HistoryItem *item = App::histItemById(history->channelId(), i.value().msg)) {
				if (!item->notificationReady()) {
					loaded = false;
				}
			} else {
				muted = true;
			}
		}
		if (loaded) {
			if (!muted) {
				_notifyWaiters.insert(i.key(), i.value());
			}
			i = _notifySettingWaiters.erase(i);
		} else {
			++i;
		}
	}
	_notifyWaitTimer.stop();
	notifyShowNext();
}

void MainWindow::notifyShowNext() {
	if (App::quitting()) return;

	uint64 ms = getms(true), nextAlert = 0;
	bool alert = false;
	int32 now = unixtime();
	for (NotifyWhenAlerts::iterator i = _notifyWhenAlerts.begin(); i != _notifyWhenAlerts.end();) {
		while (!i.value().isEmpty() && i.value().begin().key() <= ms) {
			NotifySettingsPtr n = i.key()->peer->notify, f = i.value().begin().value() ? i.value().begin().value()->notify : UnknownNotifySettings;
			while (!i.value().isEmpty() && i.value().begin().key() <= ms + 500) { // not more than one sound in 500ms from one peer - grouping
				i.value().erase(i.value().begin());
			}
			if (n == EmptyNotifySettings || (n != UnknownNotifySettings && n->mute <= now)) {
				alert = true;
			} else if (f == EmptyNotifySettings || (f != UnknownNotifySettings && f->mute <= now)) { // notify by from()
				alert = true;
			}
		}
		if (i.value().isEmpty()) {
			i = _notifyWhenAlerts.erase(i);
		} else {
			if (!nextAlert || nextAlert > i.value().begin().key()) {
				nextAlert = i.value().begin().key();
			}
			++i;
		}
	}
	if (alert) {
		psFlash();
		App::playSound();
	}

	if (_notifyWaiters.isEmpty() || !Global::DesktopNotify() || Platform::Notifications::skipToast()) {
		if (nextAlert) {
			_notifyWaitTimer.start(nextAlert - ms);
		}
		return;
	}

	while (true) {
		uint64 next = 0;
		HistoryItem *notifyItem = 0;
		History *notifyHistory = 0;
		for (NotifyWaiters::iterator i = _notifyWaiters.begin(); i != _notifyWaiters.end();) {
			History *history = i.key();
			if (history->currentNotification() && history->currentNotification()->id != i.value().msg) {
				NotifyWhenMaps::iterator j = _notifyWhenMaps.find(history);
				if (j == _notifyWhenMaps.end()) {
					history->clearNotifications();
					i = _notifyWaiters.erase(i);
					continue;
				}
				do {
					NotifyWhenMap::const_iterator k = j.value().constFind(history->currentNotification()->id);
					if (k != j.value().cend()) {
						i.value().msg = k.key();
						i.value().when = k.value();
						break;
					}
					history->skipNotification();
				} while (history->currentNotification());
			}
			if (!history->currentNotification()) {
				_notifyWhenMaps.remove(history);
				i = _notifyWaiters.erase(i);
				continue;
			}
			uint64 when = i.value().when;
			if (!notifyItem || next > when) {
				next = when;
				notifyItem = history->currentNotification();
				notifyHistory = history;
			}
			++i;
		}
		if (notifyItem) {
			if (next > ms) {
				if (nextAlert && nextAlert < next) {
					next = nextAlert;
					nextAlert = 0;
				}
				_notifyWaitTimer.start(next - ms);
				break;
			} else {
				HistoryItem *fwd = notifyItem->Has<HistoryMessageForwarded>() ? notifyItem : nullptr; // forwarded notify grouping
				int32 fwdCount = 1;

				uint64 ms = getms(true);
				History *history = notifyItem->history();
				NotifyWhenMaps::iterator j = _notifyWhenMaps.find(history);
				if (j == _notifyWhenMaps.cend()) {
					history->clearNotifications();
				} else {
					HistoryItem *nextNotify = 0;
					do {
						history->skipNotification();
						if (!history->hasNotification()) {
							break;
						}

						j.value().remove((fwd ? fwd : notifyItem)->id);
						do {
							NotifyWhenMap::const_iterator k = j.value().constFind(history->currentNotification()->id);
							if (k != j.value().cend()) {
								nextNotify = history->currentNotification();
								_notifyWaiters.insert(notifyHistory, NotifyWaiter(k.key(), k.value(), 0));
								break;
							}
							history->skipNotification();
						} while (history->hasNotification());
						if (nextNotify) {
							if (fwd) {
								HistoryItem *nextFwd = nextNotify->Has<HistoryMessageForwarded>() ? nextNotify : nullptr;
								if (nextFwd && fwd->author() == nextFwd->author() && qAbs(int64(nextFwd->date.toTime_t()) - int64(fwd->date.toTime_t())) < 2) {
									fwd = nextFwd;
									++fwdCount;
								} else {
									nextNotify = 0;
								}
							} else {
								nextNotify = 0;
							}
						}
					} while (nextNotify);
				}

				Window::Notifications::manager()->showNotification(notifyItem, fwdCount);

				if (!history->hasNotification()) {
					_notifyWaiters.remove(history);
					_notifyWhenMaps.remove(history);
					continue;
				}
			}
		} else {
			break;
		}
	}
	if (nextAlert) {
		_notifyWaitTimer.start(nextAlert - ms);
	}
}

void MainWindow::app_activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button) {
	handler->onClick(button);
}

void MainWindow::notifyUpdateAll() {
	Window::Notifications::manager()->updateAll();
}

QImage MainWindow::iconLarge() const {
	return iconbig256;
}

void MainWindow::placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) {
	QPainter p(&img);

	QString cnt = (count < 100) ? QString("%1").arg(count) : QString("..%1").arg(count % 10, 1, 10, QChar('0'));
	int32 cntSize = cnt.size();

	p.setBrush(bg->b);
	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::Antialiasing);
	int32 fontSize;
	if (size == 16) {
		fontSize = 8;
	} else if (size == 32) {
		fontSize = (cntSize < 2) ? 12 : 12;
	} else {
		fontSize = (cntSize < 2) ? 22 : 22;
	}
	style::font f = { fontSize, 0, 0 };
	int32 w = f->width(cnt), d, r;
	if (size == 16) {
		d = (cntSize < 2) ? 2 : 1;
		r = (cntSize < 2) ? 4 : 3;
	} else if (size == 32) {
		d = (cntSize < 2) ? 5 : 2;
		r = (cntSize < 2) ? 8 : 7;
	} else {
		d = (cntSize < 2) ? 9 : 4;
		r = (cntSize < 2) ? 16 : 14;
	}
	p.drawRoundedRect(QRect(shift.x() + size - w - d * 2, shift.y() + size - f->height, w + d * 2, f->height), r, r);
	p.setFont(f->f);

	p.setPen(color->p);

	p.drawText(shift.x() + size - w - d, shift.y() + size - f->height + f->ascent, cnt);

}

QImage MainWindow::iconWithCounter(int size, int count, style::color bg, bool smallIcon) {
	bool layer = false;
	if (size < 0) {
		size = -size;
		layer = true;
	}
	if (layer) {
		if (size != 16 && size != 20 && size != 24) size = 32;

		QString cnt = (count < 1000) ? QString("%1").arg(count) : QString("..%1").arg(count % 100, 2, 10, QChar('0'));
		QImage result(size, size, QImage::Format_ARGB32);
		int32 cntSize = cnt.size();
		result.fill(st::transparent->c);
		{
			QPainter p(&result);
			p.setBrush(bg->b);
			p.setPen(Qt::NoPen);
			p.setRenderHint(QPainter::Antialiasing);
			int32 fontSize;
			if (size == 16) {
				fontSize = (cntSize < 2) ? 11 : ((cntSize < 3) ? 11 : 8);
			} else if (size == 20) {
				fontSize = (cntSize < 2) ? 14 : ((cntSize < 3) ? 13 : 10);
			} else if (size == 24) {
				fontSize = (cntSize < 2) ? 17 : ((cntSize < 3) ? 16 : 12);
			} else {
				fontSize = (cntSize < 2) ? 22 : ((cntSize < 3) ? 20 : 16);
			}
			style::font f = { fontSize, 0, 0 };
			int32 w = f->width(cnt), d, r;
			if (size == 16) {
				d = (cntSize < 2) ? 5 : ((cntSize < 3) ? 2 : 1);
				r = (cntSize < 2) ? 8 : ((cntSize < 3) ? 7 : 3);
			} else if (size == 20) {
				d = (cntSize < 2) ? 6 : ((cntSize < 3) ? 2 : 1);
				r = (cntSize < 2) ? 10 : ((cntSize < 3) ? 9 : 5);
			} else if (size == 24) {
				d = (cntSize < 2) ? 7 : ((cntSize < 3) ? 3 : 1);
				r = (cntSize < 2) ? 12 : ((cntSize < 3) ? 11 : 6);
			} else {
				d = (cntSize < 2) ? 9 : ((cntSize < 3) ? 4 : 2);
				r = (cntSize < 2) ? 16 : ((cntSize < 3) ? 14 : 8);
			}
			p.drawRoundedRect(QRect(size - w - d * 2, size - f->height, w + d * 2, f->height), r, r);
			p.setFont(f->f);

			p.setPen(st::counterColor->p);

			p.drawText(size - w - d, size - f->height + f->ascent, cnt);
		}
		return result;
	} else {
		if (size != 16 && size != 32) size = 64;
	}

	QImage img(smallIcon ? ((size == 16) ? iconbig16 : (size == 32 ? iconbig32 : iconbig64)) : ((size == 16) ? icon16 : (size == 32 ? icon32 : icon64)));
	if (!count) return img;

	if (smallIcon) {
		placeSmallCounter(img, size, count, bg, QPoint(), st::counterColor);
	} else {
		QPainter p(&img);
		p.drawPixmap(size / 2, size / 2, App::pixmapFromImageInPlace(iconWithCounter(-size / 2, count, bg, false)));
	}
	return img;
}

void MainWindow::sendPaths() {
	if (App::passcoded()) return;
	hideMediaview();
	Ui::hideSettingsAndLayer();
	if (main) {
		main->activate();
	}
}

void MainWindow::mediaOverviewUpdated(PeerData *peer, MediaOverviewType type) {
	if (main) main->mediaOverviewUpdated(peer, type);
	if (_mediaView && !_mediaView->isHidden()) {
		_mediaView->mediaOverviewUpdated(peer, type);
	}
	if (type != OverviewCount) {
		Notify::PeerUpdate update(peer);
		update.flags |= Notify::PeerUpdate::Flag::SharedMediaChanged;
		update.mediaTypesMask |= (1 << type);
		Notify::peerUpdatedDelayed(update);
	}
}

void MainWindow::documentUpdated(DocumentData *doc) {
	if (!_mediaView || _mediaView->isHidden()) return;
	_mediaView->documentUpdated(doc);
}

void MainWindow::changingMsgId(HistoryItem *row, MsgId newId) {
	if (main) main->changingMsgId(row, newId);
	if (!_mediaView || _mediaView->isHidden()) return;
	_mediaView->changingMsgId(row, newId);
}

bool MainWindow::isActive(bool cached) const {
	if (cached) return _isActive;
	return isActiveWindow() && isVisible() && !(windowState() & Qt::WindowMinimized);
}

void MainWindow::updateIsActive(int timeout) {
	if (timeout) return _isActiveTimer.start(timeout);
	_isActive = isActive(false);
	if (main) main->updateOnline();
}

MainWindow::~MainWindow() {
    notifyClearFast();
	if (_clearManager) {
		_clearManager->stop();
		_clearManager = nullptr;
	}
	delete _connecting;
	delete _mediaView;
	delete trayIcon;
	delete trayIconMenu;
	delete intro;
	delete main;
	delete settings;
}

PreLaunchWindow *PreLaunchWindowInstance = 0;

PreLaunchWindow::PreLaunchWindow(QString title) : TWidget(0) {
	Fonts::start();

	QIcon icon(App::pixmapFromImageInPlace(QImage(cPlatform() == dbipMac ? qsl(":/gui/art/iconbig256.png") : qsl(":/gui/art/icon256.png"))));
	if (cPlatform() == dbipLinux32 || cPlatform() == dbipLinux64) {
		icon = QIcon::fromTheme("telegram", icon);
	}
	setWindowIcon(icon);
	setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

	setWindowTitle(title.isEmpty() ? qsl("Telegram") : title);

	QPalette p(palette());
	p.setColor(QPalette::Background, QColor(255, 255, 255));
	setPalette(p);

	QLabel tmp(this);
	tmp.setText(qsl("Tmp"));
	_size = tmp.sizeHint().height();

	int paddingVertical = (_size / 2);
	int paddingHorizontal = _size;
	int borderRadius = (_size / 5);
	setStyleSheet(qsl("QPushButton { padding: %1px %2px; background-color: #ffffff; border-radius: %3px; }\nQPushButton#confirm:hover, QPushButton#cancel:hover { background-color: #edf7ff; color: #2f9fea; }\nQPushButton#confirm { color: #2f9fea; }\nQPushButton#cancel { color: #aeaeae; }\nQLineEdit { border: 1px solid #e0e0e0; padding: 5px; }\nQLineEdit:focus { border: 2px solid #62c0f7; padding: 4px; }").arg(paddingVertical).arg(paddingHorizontal).arg(borderRadius));
	if (!PreLaunchWindowInstance) {
		PreLaunchWindowInstance = this;
	}
}

void PreLaunchWindow::activate() {
	setWindowState(windowState() & ~Qt::WindowMinimized);
	setVisible(true);
	psActivateProcess();
	activateWindow();
}

PreLaunchWindow *PreLaunchWindow::instance() {
	return PreLaunchWindowInstance;
}

PreLaunchWindow::~PreLaunchWindow() {
	if (PreLaunchWindowInstance == this) {
		PreLaunchWindowInstance = 0;
	}
}

PreLaunchLabel::PreLaunchLabel(QWidget *parent) : QLabel(parent) {
	QFont labelFont(font());
	labelFont.setFamily(qsl("Open Sans Semibold"));
	labelFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(labelFont);

	QPalette p(palette());
	p.setColor(QPalette::Foreground, QColor(0, 0, 0));
	setPalette(p);
	show();
};

void PreLaunchLabel::setText(const QString &text) {
	QLabel::setText(text);
	updateGeometry();
	resize(sizeHint());
}

PreLaunchInput::PreLaunchInput(QWidget *parent, bool password) : QLineEdit(parent) {
	QFont logFont(font());
	logFont.setFamily(qsl("Open Sans"));
	logFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(logFont);

	QPalette p(palette());
	p.setColor(QPalette::Foreground, QColor(0, 0, 0));
	setPalette(p);

	QLineEdit::setTextMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);
	if (password) {
		setEchoMode(QLineEdit::Password);
	}
	show();
};

PreLaunchLog::PreLaunchLog(QWidget *parent) : QTextEdit(parent) {
	QFont logFont(font());
	logFont.setFamily(qsl("Open Sans"));
	logFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(logFont);

	QPalette p(palette());
	p.setColor(QPalette::Foreground, QColor(96, 96, 96));
	setPalette(p);

	setReadOnly(true);
	setFrameStyle(QFrame::NoFrame | QFrame::Plain);
	viewport()->setAutoFillBackground(false);
	setContentsMargins(0, 0, 0, 0);
	document()->setDocumentMargin(0);
	show();
};

PreLaunchButton::PreLaunchButton(QWidget *parent, bool confirm) : QPushButton(parent) {
	setFlat(true);

	setObjectName(confirm ? "confirm" : "cancel");

	QFont closeFont(font());
	closeFont.setFamily(qsl("Open Sans Semibold"));
	closeFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(closeFont);

	setCursor(Qt::PointingHandCursor);
	show();
};

void PreLaunchButton::setText(const QString &text) {
	QPushButton::setText(text);
	updateGeometry();
	resize(sizeHint());
}

PreLaunchCheckbox::PreLaunchCheckbox(QWidget *parent) : QCheckBox(parent) {
	setTristate(false);
	setCheckState(Qt::Checked);

	QFont closeFont(font());
	closeFont.setFamily(qsl("Open Sans Semibold"));
	closeFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(closeFont);

	setCursor(Qt::PointingHandCursor);
	show();
};

void PreLaunchCheckbox::setText(const QString &text) {
	QCheckBox::setText(text);
	updateGeometry();
	resize(sizeHint());
}

NotStartedWindow::NotStartedWindow()
: _label(this)
, _log(this)
, _close(this) {
	_label.setText(qsl("Could not start Telegram Desktop!\nYou can see complete log below:"));

	_log.setPlainText(Logs::full());

	connect(&_close, SIGNAL(clicked()), this, SLOT(close()));
	_close.setText(qsl("CLOSE"));

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	move(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6));
	updateControls();
	show();
}

void NotStartedWindow::updateControls() {
	_label.show();
	_log.show();
	_close.show();

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	QSize s(scr.width() / 2, scr.height() / 2);
	if (s == size()) {
		resizeEvent(0);
	} else {
		resize(s);
	}
}

void NotStartedWindow::closeEvent(QCloseEvent *e) {
	deleteLater();
}

void NotStartedWindow::resizeEvent(QResizeEvent *e) {
	int padding = _size;
	_label.setGeometry(padding, padding, width() - 2 * padding, _label.sizeHint().height());
	_log.setGeometry(padding, padding * 2 + _label.sizeHint().height(), width() - 2 * padding, height() - 4 * padding - _label.height() - _close.height());
	_close.setGeometry(width() - padding - _close.width(), height() - padding - _close.height(), _close.width(), _close.height());
}

LastCrashedWindow::LastCrashedWindow()
: _port(80)
, _label(this)
, _pleaseSendReport(this)
, _yourReportName(this)
, _minidump(this)
, _report(this)
, _send(this)
, _sendSkip(this, false)
, _networkSettings(this)
, _continue(this)
, _showReport(this)
, _saveReport(this)
, _getApp(this)
, _includeUsername(this)
, _reportText(QString::fromUtf8(Sandbox::LastCrashDump()))
, _reportShown(false)
, _reportSaved(false)
, _sendingState(Sandbox::LastCrashDump().isEmpty() ? SendingNoReport : SendingUpdateCheck)
, _updating(this)
, _sendingProgress(0)
, _sendingTotal(0)
, _checkReply(0)
, _sendReply(0)
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
, _updatingCheck(this)
, _updatingSkip(this, false)
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
{
	excludeReportUsername();

	if (!cAlphaVersion() && !cBetaVersion()) { // currently accept crash reports only from testers
		_sendingState = SendingNoReport;
	}
	if (_sendingState != SendingNoReport) {
		qint64 dumpsize = 0;
		QString dumpspath = cWorkingDir() + qsl("tdata/dumps");
#if defined Q_OS_MAC && !defined MAC_USE_BREAKPAD
		dumpspath += qsl("/completed");
#endif
		QString possibleDump = getReportField(qstr("minidump"), qstr("Minidump:"));
		if (!possibleDump.isEmpty()) {
			if (!possibleDump.startsWith('/')) {
				possibleDump = dumpspath + '/' + possibleDump;
			}
			if (!possibleDump.endsWith(qstr(".dmp"))) {
				possibleDump += qsl(".dmp");
			}
			QFileInfo possibleInfo(possibleDump);
			if (possibleInfo.exists()) {
				_minidumpName = possibleInfo.fileName();
				_minidumpFull = possibleInfo.absoluteFilePath();
				dumpsize = possibleInfo.size();
			}
		}
		if (_minidumpFull.isEmpty()) {
			QString maxDump, maxDumpFull;
            QDateTime maxDumpModified, workingModified = QFileInfo(cWorkingDir() + qsl("tdata/working")).lastModified();
			QFileInfoList list = QDir(dumpspath).entryInfoList();
            for (int32 i = 0, l = list.size(); i < l; ++i) {
                QString name = list.at(i).fileName();
                if (name.endsWith(qstr(".dmp"))) {
                    QDateTime modified = list.at(i).lastModified();
                    if (maxDump.isEmpty() || qAbs(workingModified.secsTo(modified)) < qAbs(workingModified.secsTo(maxDumpModified))) {
                        maxDump = name;
                        maxDumpModified = modified;
                        maxDumpFull = list.at(i).absoluteFilePath();
                        dumpsize = list.at(i).size();
                    }
                }
            }
            if (!maxDump.isEmpty() && qAbs(workingModified.secsTo(maxDumpModified)) < 10) {
                _minidumpName = maxDump;
                _minidumpFull = maxDumpFull;
            }
        }
		if (_minidumpName.isEmpty()) { // currently don't accept crash reports without dumps from google libraries
			_sendingState = SendingNoReport;
		} else {
			_minidump.setText(qsl("+ %1 (%2 KB)").arg(_minidumpName).arg(dumpsize / 1024));
		}
	}
	if (_sendingState != SendingNoReport) {
		QString version = getReportField(qstr("version"), qstr("Version:"));
		QString current = cBetaVersion() ? qsl("-%1").arg(cBetaVersion()) : QString::number(AppVersion);
		if (version != current) { // currently don't accept crash reports from not current app version
			_sendingState = SendingNoReport;
		}
	}

	_networkSettings.setText(qsl("NETWORK SETTINGS"));
	connect(&_networkSettings, SIGNAL(clicked()), this, SLOT(onNetworkSettings()));

	if (_sendingState == SendingNoReport) {
		_label.setText(qsl("Last time Telegram Desktop was not closed properly."));
	} else {
		_label.setText(qsl("Last time Telegram Desktop crashed :("));
	}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	_updatingCheck.setText(qsl("TRY AGAIN"));
	connect(&_updatingCheck, SIGNAL(clicked()), this, SLOT(onUpdateRetry()));
	_updatingSkip.setText(qsl("SKIP"));
	connect(&_updatingSkip, SIGNAL(clicked()), this, SLOT(onUpdateSkip()));

	Sandbox::connect(SIGNAL(updateChecking()), this, SLOT(onUpdateChecking()));
	Sandbox::connect(SIGNAL(updateLatest()), this, SLOT(onUpdateLatest()));
	Sandbox::connect(SIGNAL(updateProgress(qint64,qint64)), this, SLOT(onUpdateDownloading(qint64,qint64)));
	Sandbox::connect(SIGNAL(updateFailed()), this, SLOT(onUpdateFailed()));
	Sandbox::connect(SIGNAL(updateReady()), this, SLOT(onUpdateReady()));

	switch (Sandbox::updatingState()) {
	case Application::UpdatingDownload:
		setUpdatingState(UpdatingDownload, true);
		setDownloadProgress(Sandbox::updatingReady(), Sandbox::updatingSize());
	break;
	case Application::UpdatingReady: setUpdatingState(UpdatingReady, true); break;
	default: setUpdatingState(UpdatingCheck, true); break;
	}

	cSetLastUpdateCheck(0);
	Sandbox::startUpdateCheck();
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	_updating.setText(qsl("Please check if there is a new version available."));
	if (_sendingState != SendingNoReport) {
		_sendingState = SendingNone;
	}
#endif // else for !TDESKTOP_DISABLE_AUTOUPDATE

	_pleaseSendReport.setText(qsl("Please send us a crash report."));
	_yourReportName.setText(qsl("Your Report Tag: %1\nYour User Tag: %2").arg(QString(_minidumpName).replace(".dmp", "")).arg(Sandbox::UserTag(), 0, 16));
	_yourReportName.setCursor(style::cur_text);
	_yourReportName.setTextInteractionFlags(Qt::TextSelectableByMouse);

	_includeUsername.setText(qsl("Include username @%1 as your contact info").arg(_reportUsername));

	_report.setPlainText(_reportTextNoUsername);

	_showReport.setText(qsl("VIEW REPORT"));
	connect(&_showReport, SIGNAL(clicked()), this, SLOT(onViewReport()));
	_saveReport.setText(qsl("SAVE TO FILE"));
	connect(&_saveReport, SIGNAL(clicked()), this, SLOT(onSaveReport()));
	_getApp.setText(qsl("GET THE LATEST OFFICIAL VERSION OF TELEGRAM DESKTOP"));
	connect(&_getApp, SIGNAL(clicked()), this, SLOT(onGetApp()));

	_send.setText(qsl("SEND CRASH REPORT"));
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSendReport()));

	_sendSkip.setText(qsl("SKIP"));
	connect(&_sendSkip, SIGNAL(clicked()), this, SLOT(onContinue()));
	_continue.setText(qsl("CONTINUE"));
	connect(&_continue, SIGNAL(clicked()), this, SLOT(onContinue()));

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	move(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6));
	updateControls();
	show();
}

void LastCrashedWindow::onViewReport() {
	_reportShown = !_reportShown;
	updateControls();
}

void LastCrashedWindow::onSaveReport() {
	QString to = QFileDialog::getSaveFileName(0, qsl("Telegram Crash Report"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + qsl("/report.telegramcrash"), qsl("Telegram crash report (*.telegramcrash)"));
	if (!to.isEmpty()) {
		QFile file(to);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(getCrashReportRaw());
			_reportSaved = true;
			updateControls();
		}
	}
}

QByteArray LastCrashedWindow::getCrashReportRaw() const {
	QByteArray result(Sandbox::LastCrashDump());
	if (!_reportUsername.isEmpty() && _includeUsername.checkState() != Qt::Checked) {
		result.replace((qsl("Username: ") + _reportUsername).toUtf8(), "Username: _not_included_");
	}
	return result;
}

void LastCrashedWindow::onGetApp() {
	QDesktopServices::openUrl(qsl("https://desktop.telegram.org"));
}

void LastCrashedWindow::excludeReportUsername() {
	QString prefix = qstr("Username:");
	QStringList lines = _reportText.split('\n');
	for (int32 i = 0, l = lines.size(); i < l; ++i) {
		if (lines.at(i).trimmed().startsWith(prefix)) {
			_reportUsername = lines.at(i).trimmed().mid(prefix.size()).trimmed();
			lines.removeAt(i);
			break;
		}
	}
	_reportTextNoUsername = _reportUsername.isEmpty() ? _reportText : lines.join('\n');
}

QString LastCrashedWindow::getReportField(const QLatin1String &name, const QLatin1String &prefix) {
	QStringList lines = _reportText.split('\n');
	for (int32 i = 0, l = lines.size(); i < l; ++i) {
		if (lines.at(i).trimmed().startsWith(prefix)) {
			QString data = lines.at(i).trimmed().mid(prefix.size()).trimmed();

			if (name == qstr("version")) {
				if (data.endsWith(qstr(" beta"))) {
					data = QString::number(-data.replace(QRegularExpression(qsl("[^\\d]")), "").toLongLong());
				} else {
					data = QString::number(data.replace(QRegularExpression(qsl("[^\\d]")), "").toLongLong());
				}
			}

			return data;
		}
	}
	return QString();
}

void LastCrashedWindow::addReportFieldPart(const QLatin1String &name, const QLatin1String &prefix, QHttpMultiPart *multipart) {
	QString data = getReportField(name, prefix);
	if (!data.isEmpty()) {
		QHttpPart reportPart;
		reportPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(qsl("form-data; name=\"%1\"").arg(name)));
		reportPart.setBody(data.toUtf8());
		multipart->append(reportPart);
	}
}

void LastCrashedWindow::onSendReport() {
	if (_checkReply) {
		_checkReply->deleteLater();
		_checkReply = 0;
	}
	if (_sendReply) {
		_sendReply->deleteLater();
		_sendReply = 0;
	}
	App::setProxySettings(_sendManager);

	QString apiid = getReportField(qstr("apiid"), qstr("ApiId:")), version = getReportField(qstr("version"), qstr("Version:"));
	_checkReply = _sendManager.get(QNetworkRequest(qsl("https://tdesktop.com/crash.php?act=query_report&apiid=%1&version=%2&dmp=%3&platform=%4").arg(apiid).arg(version).arg(minidumpFileName().isEmpty() ? 0 : 1).arg(cPlatformString())));

	connect(_checkReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onSendingError(QNetworkReply::NetworkError)));
	connect(_checkReply, SIGNAL(finished()), this, SLOT(onCheckingFinished()));

	_pleaseSendReport.setText(qsl("Sending crash report..."));
	_sendingState = SendingProgress;
	_reportShown = false;
	updateControls();
}

namespace {
	struct zByteArray {
		zByteArray() : pos(0), err(0) {
		}
		uLong pos;
		int err;
		QByteArray data;
	};

	voidpf zByteArrayOpenFile(voidpf opaque, const char* filename, int mode) {
		zByteArray *ba = (zByteArray*)opaque;
		if (mode & ZLIB_FILEFUNC_MODE_WRITE) {
			if (mode & ZLIB_FILEFUNC_MODE_CREATE) {
				ba->data.clear();
			}
			ba->pos = ba->data.size();
			ba->data.reserve(2 * 1024 * 1024);
		} else if (mode & ZLIB_FILEFUNC_MODE_READ) {
			ba->pos = 0;
		}
		ba->err = 0;
		return opaque;
	}

	uLong zByteArrayReadFile(voidpf opaque, voidpf stream, void* buf, uLong size) {
		zByteArray *ba = (zByteArray*)opaque;
		uLong toRead = 0;
		if (!ba->err) {
			if (ba->data.size() > int(ba->pos)) {
				toRead = qMin(size, uLong(ba->data.size() - ba->pos));
				memcpy(buf, ba->data.constData() + ba->pos, toRead);
				ba->pos += toRead;
			}
			if (toRead < size) {
				ba->err = -1;
			}
		}
		return toRead;
	}

	uLong zByteArrayWriteFile(voidpf opaque, voidpf stream, const void* buf, uLong size) {
		zByteArray *ba = (zByteArray*)opaque;
		if (ba->data.size() < int(ba->pos + size)) {
			ba->data.resize(ba->pos + size);
		}
		memcpy(ba->data.data() + ba->pos, buf, size);
		ba->pos += size;
		return size;
	}

	int zByteArrayCloseFile(voidpf opaque, voidpf stream) {
		zByteArray *ba = (zByteArray*)opaque;
		int result = ba->err;
		ba->pos = 0;
		ba->err = 0;
		return result;
	}

	int zByteArrayErrorFile(voidpf opaque, voidpf stream) {
		zByteArray *ba = (zByteArray*)opaque;
		return ba->err;
	}

	long zByteArrayTellFile(voidpf opaque, voidpf stream) {
		zByteArray *ba = (zByteArray*)opaque;
		return ba->pos;
	}

	long zByteArraySeekFile(voidpf opaque, voidpf stream, uLong offset, int origin) {
		zByteArray *ba = (zByteArray*)opaque;
		if (!ba->err) {
			switch (origin) {
			case ZLIB_FILEFUNC_SEEK_SET: ba->pos = offset; break;
			case ZLIB_FILEFUNC_SEEK_CUR: ba->pos += offset; break;
			case ZLIB_FILEFUNC_SEEK_END: ba->pos = ba->data.size() + offset; break;
			}
			if (int(ba->pos) > ba->data.size()) {
				ba->err = -1;
			}
		}
		return ba->err;
	}

}

QString LastCrashedWindow::minidumpFileName() {
	QFileInfo dmpFile(_minidumpFull);
	if (dmpFile.exists() && dmpFile.size() > 0 && dmpFile.size() < 20 * 1024 * 1024 &&
		QRegularExpression(qsl("^[a-zA-Z0-9\\-]{1,64}\\.dmp$")).match(dmpFile.fileName()).hasMatch()) {
		return dmpFile.fileName();
	}
	return QString();
}

void LastCrashedWindow::onCheckingFinished() {
	if (!_checkReply || _sendReply) return;

	QByteArray result = _checkReply->readAll().trimmed();
	_checkReply->deleteLater();
	_checkReply = 0;

	LOG(("Crash report check for sending done, result: %1").arg(QString::fromUtf8(result)));

	if (result == "Old") {
		_pleaseSendReport.setText(qsl("This report is about some old version of Telegram Desktop."));
		_sendingState = SendingTooOld;
		updateControls();
		return;
	} else if (result == "Unofficial") {
		_pleaseSendReport.setText(qsl("You use some custom version of Telegram Desktop."));
		_sendingState = SendingUnofficial;
		updateControls();
		return;
	} else if (result != "Report") {
		_pleaseSendReport.setText(qsl("Thank you for your report!"));
		_sendingState = SendingDone;
		updateControls();

		SignalHandlers::restart();
		return;
	}

	QHttpMultiPart *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

	addReportFieldPart(qstr("platform"), qstr("Platform:"), multipart);
	addReportFieldPart(qstr("version"), qstr("Version:"), multipart);

	QHttpPart reportPart;
	reportPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
	reportPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"report\"; filename=\"report.telegramcrash\""));
	reportPart.setBody(getCrashReportRaw());
	multipart->append(reportPart);

	QString dmpName = minidumpFileName();
	if (!dmpName.isEmpty()) {
		QFile file(_minidumpFull);
		if (file.open(QIODevice::ReadOnly)) {
			QByteArray minidump = file.readAll();
			file.close();

			QString zipName = QString(dmpName).replace(qstr(".dmp"), qstr(".zip"));
			zByteArray minidumpZip;

			bool failed = false;
			zlib_filefunc_def zfuncs;
			zfuncs.opaque = &minidumpZip;
			zfuncs.zopen_file = zByteArrayOpenFile;
			zfuncs.zerror_file = zByteArrayErrorFile;
			zfuncs.zread_file = zByteArrayReadFile;
			zfuncs.zwrite_file = zByteArrayWriteFile;
			zfuncs.zclose_file = zByteArrayCloseFile;
			zfuncs.zseek_file = zByteArraySeekFile;
			zfuncs.ztell_file = zByteArrayTellFile;

			if (zipFile zf = zipOpen2(0, APPEND_STATUS_CREATE, 0, &zfuncs)) {
				zip_fileinfo zfi = { { 0, 0, 0, 0, 0, 0 }, 0, 0, 0 };
				QByteArray dmpNameUtf = dmpName.toUtf8();
				if (zipOpenNewFileInZip(zf, dmpNameUtf.constData(), &zfi, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION) != ZIP_OK) {
					failed = true;
				} else if (zipWriteInFileInZip(zf, minidump.constData(), minidump.size()) != 0) {
					failed = true;
				} else if (zipCloseFileInZip(zf) != 0) {
					failed = true;
				}
				if (zipClose(zf, NULL) != 0) {
					failed = true;
				}
				if (failed) {
					minidumpZip.err = -1;
				}
			}

			if (!minidumpZip.err) {
				QHttpPart dumpPart;
				dumpPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
				dumpPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(qsl("form-data; name=\"dump\"; filename=\"%1\"").arg(zipName)));
				dumpPart.setBody(minidumpZip.data);
				multipart->append(dumpPart);

				_minidump.setText(qsl("+ %1 (%2 KB)").arg(zipName).arg(minidumpZip.data.size() / 1024));
			}
		}
	}

	_sendReply = _sendManager.post(QNetworkRequest(qsl("https://tdesktop.com/crash.php?act=report")), multipart);
	multipart->setParent(_sendReply);

	connect(_sendReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onSendingError(QNetworkReply::NetworkError)));
	connect(_sendReply, SIGNAL(finished()), this, SLOT(onSendingFinished()));
	connect(_sendReply, SIGNAL(uploadProgress(qint64,qint64)), this, SLOT(onSendingProgress(qint64,qint64)));

	updateControls();
}

void LastCrashedWindow::updateControls() {
	int padding = _size, h = padding + _networkSettings.height() + padding;

	_label.show();
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	h += _networkSettings.height() + padding;
	if (_updatingState == UpdatingFail && (_sendingState == SendingNoReport || _sendingState == SendingUpdateCheck)) {
		_networkSettings.show();
		_updatingCheck.show();
		_updatingSkip.show();
		_send.hide();
		_sendSkip.hide();
		_continue.hide();
		_pleaseSendReport.hide();
		_yourReportName.hide();
		_includeUsername.hide();
		_getApp.hide();
		_showReport.hide();
		_report.hide();
		_minidump.hide();
		_saveReport.hide();
		h += padding + _updatingCheck.height() + padding;
	} else {
		if (_updatingState == UpdatingCheck || _sendingState == SendingFail || _sendingState == SendingProgress) {
			_networkSettings.show();
		} else {
			_networkSettings.hide();
		}
		if (_updatingState == UpdatingNone || _updatingState == UpdatingLatest || _updatingState == UpdatingFail) {
			h += padding + _updatingCheck.height() + padding;
			if (_sendingState == SendingNoReport) {
				_pleaseSendReport.hide();
				_yourReportName.hide();
				_includeUsername.hide();
				_getApp.hide();
				_showReport.hide();
				_report.hide();
				_minidump.hide();
				_saveReport.hide();
				_send.hide();
				_sendSkip.hide();
				_continue.show();
			} else {
				h += _showReport.height() + padding + _yourReportName.height() + padding;
				_pleaseSendReport.show();
				_yourReportName.show();
				if (_reportUsername.isEmpty()) {
					_includeUsername.hide();
				} else {
					h += _includeUsername.height() + padding;
					_includeUsername.show();
				}
				if (_sendingState == SendingTooOld || _sendingState == SendingUnofficial) {
					QString verStr = getReportField(qstr("version"), qstr("Version:"));
					qint64 ver = verStr.isEmpty() ? 0 : verStr.toLongLong();
					if (!ver || (ver == AppVersion) || (ver < 0 && (-ver / 1000) == AppVersion)) {
						h += _getApp.height() + padding;
						_getApp.show();
						h -= _yourReportName.height() + padding; // hide report name
						_yourReportName.hide();
						if (!_reportUsername.isEmpty()) {
							h -= _includeUsername.height() + padding;
							_includeUsername.hide();
						}
					} else {
						_getApp.hide();
					}
					_showReport.hide();
					_report.hide();
					_minidump.hide();
					_saveReport.hide();
					_send.hide();
					_sendSkip.hide();
					_continue.show();
				} else {
					_getApp.hide();
					if (_reportShown) {
						h += (_pleaseSendReport.height() * 12.5) + padding + (_minidumpName.isEmpty() ? 0 : (_minidump.height() + padding));
						_report.show();
						if (_minidumpName.isEmpty()) {
							_minidump.hide();
						} else {
							_minidump.show();
						}
						if (_reportSaved || _sendingState == SendingFail || _sendingState == SendingProgress || _sendingState == SendingUploading) {
							_saveReport.hide();
						} else {
							_saveReport.show();
						}
						_showReport.hide();
					} else {
						_report.hide();
						_minidump.hide();
						_saveReport.hide();
						if (_sendingState == SendingFail || _sendingState == SendingProgress || _sendingState == SendingUploading) {
							_showReport.hide();
						} else {
							_showReport.show();
						}
					}
					if (_sendingState == SendingTooMany || _sendingState == SendingDone) {
						_send.hide();
						_sendSkip.hide();
						_continue.show();
					} else {
						if (_sendingState == SendingProgress || _sendingState == SendingUploading) {
							_send.hide();
						} else {
							_send.show();
						}
						_sendSkip.show();
						_continue.hide();
					}
				}
			}
		} else {
			_getApp.hide();
			_pleaseSendReport.hide();
			_yourReportName.hide();
			_includeUsername.hide();
			_showReport.hide();
			_report.hide();
			_minidump.hide();
			_saveReport.hide();
			_send.hide();
			_sendSkip.hide();
			_continue.hide();
		}
		_updatingCheck.hide();
		if (_updatingState == UpdatingCheck || _updatingState == UpdatingDownload) {
			h += padding + _updatingSkip.height() + padding;
			_updatingSkip.show();
		} else {
			_updatingSkip.hide();
		}
	}
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	h += _networkSettings.height() + padding;
	h += padding + _send.height() + padding;
	if (_sendingState == SendingNoReport) {
		_pleaseSendReport.hide();
		_yourReportName.hide();
		_includeUsername.hide();
		_showReport.hide();
		_report.hide();
		_minidump.hide();
		_saveReport.hide();
		_send.hide();
		_sendSkip.hide();
		_continue.show();
		_networkSettings.hide();
	} else {
		h += _showReport.height() + padding + _yourReportName.height() + padding;
		_pleaseSendReport.show();
		_yourReportName.show();
		if (_reportUsername.isEmpty()) {
			_includeUsername.hide();
		} else {
			h += _includeUsername.height() + padding;
			_includeUsername.show();
		}
		if (_reportShown) {
			h += (_pleaseSendReport.height() * 12.5) + padding + (_minidumpName.isEmpty() ? 0 : (_minidump.height() + padding));
			_report.show();
			if (_minidumpName.isEmpty()) {
				_minidump.hide();
			} else {
				_minidump.show();
			}
			_showReport.hide();
			if (_reportSaved || _sendingState == SendingFail || _sendingState == SendingProgress || _sendingState == SendingUploading) {
				_saveReport.hide();
			} else {
				_saveReport.show();
			}
		} else {
			_report.hide();
			_minidump.hide();
			_saveReport.hide();
			if (_sendingState == SendingFail || _sendingState == SendingProgress || _sendingState == SendingUploading) {
				_showReport.hide();
			} else {
				_showReport.show();
			}
		}
		if (_sendingState == SendingDone) {
			_send.hide();
			_sendSkip.hide();
			_continue.show();
			_networkSettings.hide();
		} else {
			if (_sendingState == SendingProgress || _sendingState == SendingUploading) {
				_send.hide();
			} else {
				_send.show();
			}
			_sendSkip.show();
			if (_sendingState == SendingFail) {
				_networkSettings.show();
			} else {
				_networkSettings.hide();
			}
			_continue.hide();
		}
	}

	_getApp.show();
	h += _networkSettings.height() + padding;
#endif // else for !TDESKTOP_DISABLE_AUTOUPDATE

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	QSize s(2 * padding + QFontMetrics(_label.font()).width(qsl("Last time Telegram Desktop was not closed properly.")) + padding + _networkSettings.width(), h);
	if (s == size()) {
		resizeEvent(0);
	} else {
		resize(s);
	}
}

void LastCrashedWindow::onNetworkSettings() {
	auto &p = Sandbox::PreLaunchProxy();
	NetworkSettingsWindow *box = new NetworkSettingsWindow(this, p.host, p.port ? p.port : 80, p.user, p.password);
	connect(box, SIGNAL(saved(QString, quint32, QString, QString)), this, SLOT(onNetworkSettingsSaved(QString, quint32, QString, QString)));
	box->show();
}

void LastCrashedWindow::onNetworkSettingsSaved(QString host, quint32 port, QString username, QString password) {
	Sandbox::RefPreLaunchProxy().host = host;
	Sandbox::RefPreLaunchProxy().port = port ? port : 80;
	Sandbox::RefPreLaunchProxy().user = username;
	Sandbox::RefPreLaunchProxy().password = password;
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if ((_updatingState == UpdatingFail && (_sendingState == SendingNoReport || _sendingState == SendingUpdateCheck)) || (_updatingState == UpdatingCheck)) {
		Sandbox::stopUpdate();
		cSetLastUpdateCheck(0);
		Sandbox::startUpdateCheck();
	} else
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	if (_sendingState == SendingFail || _sendingState == SendingProgress) {
		onSendReport();
	}
	activate();
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void LastCrashedWindow::setUpdatingState(UpdatingState state, bool force) {
	if (_updatingState != state || force) {
		_updatingState = state;
		switch (state) {
		case UpdatingLatest:
			_updating.setText(qsl("Latest version is installed."));
			if (_sendingState == SendingNoReport) {
				QTimer::singleShot(0, this, SLOT(onContinue()));
			} else {
				_sendingState = SendingNone;
			}
		break;
		case UpdatingReady:
			if (checkReadyUpdate()) {
				cSetRestartingUpdate(true);
				App::quit();
				return;
			} else {
				setUpdatingState(UpdatingFail);
				return;
			}
		break;
		case UpdatingCheck:
			_updating.setText(qsl("Checking for updates..."));
		break;
		case UpdatingFail:
			_updating.setText(qsl("Update check failed :("));
		break;
		}
		updateControls();
	}
}

void LastCrashedWindow::setDownloadProgress(qint64 ready, qint64 total) {
	qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
	QString readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
	QString totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
	QString res = qsl("Downloading update {ready} / {total} MB..").replace(qstr("{ready}"), readyStr).replace(qstr("{total}"), totalStr);
	if (_newVersionDownload != res) {
		_newVersionDownload = res;
		_updating.setText(_newVersionDownload);
		updateControls();
	}
}

void LastCrashedWindow::onUpdateRetry() {
	cSetLastUpdateCheck(0);
	Sandbox::startUpdateCheck();
}

void LastCrashedWindow::onUpdateSkip() {
	if (_sendingState == SendingNoReport) {
		onContinue();
	} else {
		if (_updatingState == UpdatingCheck || _updatingState == UpdatingDownload) {
			Sandbox::stopUpdate();
			setUpdatingState(UpdatingFail);
		}
		_sendingState = SendingNone;
		updateControls();
	}
}

void LastCrashedWindow::onUpdateChecking() {
	setUpdatingState(UpdatingCheck);
}

void LastCrashedWindow::onUpdateLatest() {
	setUpdatingState(UpdatingLatest);
}

void LastCrashedWindow::onUpdateDownloading(qint64 ready, qint64 total) {
	setUpdatingState(UpdatingDownload);
	setDownloadProgress(ready, total);
}

void LastCrashedWindow::onUpdateReady() {
	setUpdatingState(UpdatingReady);
}

void LastCrashedWindow::onUpdateFailed() {
	setUpdatingState(UpdatingFail);
}
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

void LastCrashedWindow::onContinue() {
	if (SignalHandlers::restart() == SignalHandlers::CantOpen) {
		new NotStartedWindow();
	} else if (!Global::started()) {
		Sandbox::launch();
	}
	close();
}

void LastCrashedWindow::onSendingError(QNetworkReply::NetworkError e) {
	LOG(("Crash report sending error: %1").arg(e));

	_pleaseSendReport.setText(qsl("Sending crash report failed :("));
	_sendingState = SendingFail;
	if (_checkReply) {
		_checkReply->deleteLater();
		_checkReply = 0;
	}
	if (_sendReply) {
		_sendReply->deleteLater();
		_sendReply = 0;
	}
	updateControls();
}

void LastCrashedWindow::onSendingFinished() {
	if (_sendReply) {
		QByteArray result = _sendReply->readAll();
		LOG(("Crash report sending done, result: %1").arg(QString::fromUtf8(result)));

		_sendReply->deleteLater();
		_sendReply = 0;
		_pleaseSendReport.setText(qsl("Thank you for your report!"));
		_sendingState = SendingDone;
		updateControls();

		SignalHandlers::restart();
	}
}

void LastCrashedWindow::onSendingProgress(qint64 uploaded, qint64 total) {
	if (_sendingState != SendingProgress && _sendingState != SendingUploading) return;
	_sendingState = SendingUploading;

	if (total < 0) {
		_pleaseSendReport.setText(qsl("Sending crash report %1 KB...").arg(uploaded / 1024));
	} else {
		_pleaseSendReport.setText(qsl("Sending crash report %1 / %2 KB...").arg(uploaded / 1024).arg(total / 1024));
	}
	updateControls();
}

void LastCrashedWindow::closeEvent(QCloseEvent *e) {
	deleteLater();
}

void LastCrashedWindow::resizeEvent(QResizeEvent *e) {
	int padding = _size;
	_label.move(padding, padding + (_networkSettings.height() - _label.height()) / 2);

	_send.move(width() - padding - _send.width(), height() - padding - _send.height());
	if (_sendingState == SendingProgress || _sendingState == SendingUploading) {
		_sendSkip.move(width() - padding - _sendSkip.width(), height() - padding - _sendSkip.height());
	} else {
		_sendSkip.move(width() - padding - _send.width() - padding - _sendSkip.width(), height() - padding - _sendSkip.height());
	}

	_updating.move(padding, padding * 2 + _networkSettings.height() + (_networkSettings.height() - _updating.height()) / 2);

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	_pleaseSendReport.move(padding, padding * 2 + _networkSettings.height() + _networkSettings.height() + padding + (_showReport.height() - _pleaseSendReport.height()) / 2);
	_showReport.move(padding * 2 + _pleaseSendReport.width(), padding * 2 + _networkSettings.height() + _networkSettings.height() + padding);
	_yourReportName.move(padding, _showReport.y() + _showReport.height() + padding);
	_includeUsername.move(padding, _yourReportName.y() + _yourReportName.height() + padding);
	_getApp.move((width() - _getApp.width()) / 2, _showReport.y() + _showReport.height() + padding);

	if (_sendingState == SendingFail || _sendingState == SendingProgress) {
		_networkSettings.move(padding * 2 + _pleaseSendReport.width(), padding * 2 + _networkSettings.height() + _networkSettings.height() + padding);
	} else {
		_networkSettings.move(padding * 2 + _updating.width(), padding * 2 + _networkSettings.height());
	}

	if (_updatingState == UpdatingCheck || _updatingState == UpdatingDownload) {
		_updatingCheck.move(width() - padding - _updatingCheck.width(), height() - padding - _updatingCheck.height());
		_updatingSkip.move(width() - padding - _updatingSkip.width(), height() - padding - _updatingSkip.height());
	} else {
		_updatingCheck.move(width() - padding - _updatingCheck.width(), height() - padding - _updatingCheck.height());
		_updatingSkip.move(width() - padding - _updatingCheck.width() - padding - _updatingSkip.width(), height() - padding - _updatingSkip.height());
	}
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	_getApp.move((width() - _getApp.width()) / 2, _updating.y() + _updating.height() + padding);

	_pleaseSendReport.move(padding, padding * 2 + _networkSettings.height() + _networkSettings.height() + padding + _getApp.height() + padding + (_showReport.height() - _pleaseSendReport.height()) / 2);
	_showReport.move(padding * 2 + _pleaseSendReport.width(), padding * 2 + _networkSettings.height() + _networkSettings.height() + padding + _getApp.height() + padding);
	_yourReportName.move(padding, _showReport.y() + _showReport.height() + padding);
	_includeUsername.move(padding, _yourReportName.y() + _yourReportName.height() + padding);

	_networkSettings.move(padding * 2 + _pleaseSendReport.width(), padding * 2 + _networkSettings.height() + _networkSettings.height() + padding + _getApp.height() + padding);
#endif // else for !TDESKTOP_DISABLE_AUTOUPDATE
	if (_reportUsername.isEmpty()) {
		_report.setGeometry(padding, _yourReportName.y() + _yourReportName.height() + padding, width() - 2 * padding, _pleaseSendReport.height() * 12.5);
	} else {
		_report.setGeometry(padding, _includeUsername.y() + _includeUsername.height() + padding, width() - 2 * padding, _pleaseSendReport.height() * 12.5);
	}
	_minidump.move(padding, _report.y() + _report.height() + padding);
	_saveReport.move(_showReport.x(), _showReport.y());

	_continue.move(width() - padding - _continue.width(), height() - padding - _continue.height());
}

NetworkSettingsWindow::NetworkSettingsWindow(QWidget *parent, QString host, quint32 port, QString username, QString password)
: PreLaunchWindow(qsl("HTTP Proxy Settings"))
, _hostLabel(this)
, _portLabel(this)
, _usernameLabel(this)
, _passwordLabel(this)
, _hostInput(this)
, _portInput(this)
, _usernameInput(this)
, _passwordInput(this, true)
, _save(this)
, _cancel(this, false)
, _parent(parent) {
	setWindowModality(Qt::ApplicationModal);

	_hostLabel.setText(qsl("Hostname"));
	_portLabel.setText(qsl("Port"));
	_usernameLabel.setText(qsl("Username"));
	_passwordLabel.setText(qsl("Password"));

	_save.setText(qsl("SAVE"));
	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	_cancel.setText(qsl("CANCEL"));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(close()));

	_hostInput.setText(host);
	_portInput.setText(QString::number(port));
	_usernameInput.setText(username);
	_passwordInput.setText(password);

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	move(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6));
	updateControls();
	show();

	_hostInput.setFocus();
	_hostInput.setCursorPosition(_hostInput.text().size());
}

void NetworkSettingsWindow::resizeEvent(QResizeEvent *e) {
	int padding = _size;
	_hostLabel.move(padding, padding);
	_hostInput.setGeometry(_hostLabel.x(), _hostLabel.y() + _hostLabel.height(), 2 * _hostLabel.width(), _hostInput.height());
	_portLabel.move(padding + _hostInput.width() + padding, padding);
	_portInput.setGeometry(_portLabel.x(), _portLabel.y() + _portLabel.height(), width() - padding - _portLabel.x(), _portInput.height());
	_usernameLabel.move(padding, _hostInput.y() + _hostInput.height() + padding);
	_usernameInput.setGeometry(_usernameLabel.x(), _usernameLabel.y() + _usernameLabel.height(), (width() - 3 * padding) / 2, _usernameInput.height());
	_passwordLabel.move(padding + _usernameInput.width() + padding, _usernameLabel.y());
	_passwordInput.setGeometry(_passwordLabel.x(), _passwordLabel.y() + _passwordLabel.height(), width() - padding - _passwordLabel.x(), _passwordInput.height());

	_save.move(width() - padding - _save.width(), height() - padding - _save.height());
	_cancel.move(_save.x() - padding - _cancel.width(), _save.y());
}

void NetworkSettingsWindow::onSave() {
	QString host = _hostInput.text().trimmed(), port = _portInput.text().trimmed(), username = _usernameInput.text().trimmed(), password = _passwordInput.text().trimmed();
	if (!port.isEmpty() && !port.toUInt()) {
		_portInput.setFocus();
		return;
	} else if (!host.isEmpty() && port.isEmpty()) {
		_portInput.setFocus();
		return;
	}
	emit saved(host, port.toUInt(), username, password);
	close();
}

void NetworkSettingsWindow::closeEvent(QCloseEvent *e) {
}

void NetworkSettingsWindow::updateControls() {
	_hostInput.updateGeometry();
	_hostInput.resize(_hostInput.sizeHint());
	_portInput.updateGeometry();
	_portInput.resize(_portInput.sizeHint());
	_usernameInput.updateGeometry();
	_usernameInput.resize(_usernameInput.sizeHint());
	_passwordInput.updateGeometry();
	_passwordInput.resize(_passwordInput.sizeHint());

	int padding = _size;
	int w = 2 * padding + _hostLabel.width() * 2 + padding + _portLabel.width() * 2 + padding;
	int h = padding + _hostLabel.height() + _hostInput.height() + padding + _usernameLabel.height() + _usernameInput.height() + padding + _save.height() + padding;
	if (w == width() && h == height()) {
		resizeEvent(0);
	} else {
		setGeometry(_parent->x() + (_parent->width() - w) / 2, _parent->y() + (_parent->height() - h) / 2, w, h);
	}
}

ShowCrashReportWindow::ShowCrashReportWindow(const QString &text)
: _log(this) {
	_log.setPlainText(text);

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	setGeometry(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6), scr.width() / 2, scr.height() / 2);
	show();
}

void ShowCrashReportWindow::resizeEvent(QResizeEvent *e) {
	_log.setGeometry(rect().marginsRemoved(QMargins(basicSize(), basicSize(), basicSize(), basicSize())));
}

void ShowCrashReportWindow::closeEvent(QCloseEvent *e) {
    deleteLater();
}

#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
int showCrashReportWindow(const QString &crashdump) {
	QString text;

	QFile dump(crashdump);
	if (dump.open(QIODevice::ReadOnly)) {
		text = qsl("Crash dump file '%1':\n\n").arg(QFileInfo(crashdump).absoluteFilePath());
		text += psPrepareCrashDump(dump.readAll(), crashdump);
	} else {
		text = qsl("ERROR: could not read crash dump file '%1'").arg(QFileInfo(crashdump).absoluteFilePath());
	}

	if (Global::started()) {
		ShowCrashReportWindow *wnd = new ShowCrashReportWindow(text);
		return 0;
	}

	QByteArray args[] = { QFile::encodeName(QDir::toNativeSeparators(cExeDir() + cExeName())) };
	int a_argc = 1;
	char *a_argv[1] = { args[0].data() };
	QApplication app(a_argc, a_argv);

	ShowCrashReportWindow *wnd = new ShowCrashReportWindow(text);
	return app.exec();
}
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
