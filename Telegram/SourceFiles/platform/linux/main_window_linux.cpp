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
#include "platform/linux/main_window_linux.h"

#include "platform/linux/linux_libs.h"
#include "platform/platform_notifications_manager.h"
#include "mainwindow.h"
#include "application.h"
#include "lang.h"
#include "localstorage.h"

namespace Platform {
namespace {

bool noQtTrayIcon = false, tryAppIndicator = false;
bool useGtkBase = false, useAppIndicator = false, useStatusIcon = false, trayIconChecked = false, useUnityCount = false;

AppIndicator *_trayIndicator = 0;
GtkStatusIcon *_trayIcon = 0;
GtkWidget *_trayMenu = 0;
GdkPixbuf *_trayPixbuf = 0;
QByteArray _trayPixbufData;
QList<QPair<GtkWidget*, QObject*> > _trayItems;

int32 _trayIconSize = 22;
bool _trayIconMuted = true;
int32 _trayIconCount = 0;
QImage _trayIconImageBack, _trayIconImage;

void _trayIconPopup(GtkStatusIcon *status_icon, guint button, guint32 activate_time, gpointer popup_menu) {
	Libs::gtk_menu_popup(Libs::gtk_menu_cast(popup_menu), NULL, NULL, Libs::gtk_status_icon_position_menu, status_icon, button, activate_time);
}

void _trayIconActivate(GtkStatusIcon *status_icon, gpointer popup_menu) {
	if (App::wnd()->isActiveWindow() && App::wnd()->isVisible()) {
		Libs::gtk_menu_popup(Libs::gtk_menu_cast(popup_menu), NULL, NULL, Libs::gtk_status_icon_position_menu, status_icon, 0, Libs::gtk_get_current_event_time());
	} else {
		App::wnd()->showFromTray();
	}
}

gboolean _trayIconResized(GtkStatusIcon *status_icon, gint size, gpointer popup_menu) {
	_trayIconSize = size;
	if (App::wnd()) App::wnd()->psUpdateCounter();
	return FALSE;
}

#define QT_RED 0
#define QT_GREEN 1
#define QT_BLUE 2
#define QT_ALPHA 3

#define GTK_RED 2
#define GTK_GREEN 1
#define GTK_BLUE 0
#define GTK_ALPHA 3

QImage _trayIconImageGen() {
	int32 counter = App::histories().unreadBadge(), counterSlice = (counter >= 1000) ? (1000 + (counter % 100)) : counter;
	bool muted = App::histories().unreadOnlyMuted();
	if (_trayIconImage.isNull() || _trayIconImage.width() != _trayIconSize || muted != _trayIconMuted || counterSlice != _trayIconCount) {
		if (_trayIconImageBack.isNull() || _trayIconImageBack.width() != _trayIconSize) {
			_trayIconImageBack = App::wnd()->iconLarge().scaled(_trayIconSize, _trayIconSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			_trayIconImageBack = _trayIconImageBack.convertToFormat(QImage::Format_ARGB32);
			int w = _trayIconImageBack.width(), h = _trayIconImageBack.height(), perline = _trayIconImageBack.bytesPerLine();
			uchar *bytes = _trayIconImageBack.bits();
			for (int32 y = 0; y < h; ++y) {
				for (int32 x = 0; x < w; ++x) {
					int32 srcoff = y * perline + x * 4;
					bytes[srcoff + QT_RED  ] = qMax(bytes[srcoff + QT_RED  ], uchar(224));
					bytes[srcoff + QT_GREEN] = qMax(bytes[srcoff + QT_GREEN], uchar(165));
					bytes[srcoff + QT_BLUE ] = qMax(bytes[srcoff + QT_BLUE ], uchar(44));
				}
			}
		}
		_trayIconImage = _trayIconImageBack;
		if (counter > 0) {
			QPainter p(&_trayIconImage);
			int32 layerSize = -16;
			if (_trayIconSize >= 48) {
				layerSize = -32;
			} else if (_trayIconSize >= 36) {
				layerSize = -24;
			} else if (_trayIconSize >= 32) {
				layerSize = -20;
			}
			QImage layer = App::wnd()->iconWithCounter(layerSize, counter, (muted ? st::counterMuteBG : st::counterBG), false);
			p.drawImage(_trayIconImage.width() - layer.width() - 1, _trayIconImage.height() - layer.height() - 1, layer);
		}
	}
	return _trayIconImage;
}

QString _trayIconImageFile() {
	int32 counter = App::histories().unreadBadge(), counterSlice = (counter >= 1000) ? (1000 + (counter % 100)) : counter;
	bool muted = App::histories().unreadOnlyMuted();

	QString name = cWorkingDir() + qsl("tdata/ticons/ico%1_%2_%3.png").arg(muted ? "mute" : "").arg(_trayIconSize).arg(counterSlice);
	QFileInfo info(name);
	if (info.exists()) return name;

	QImage img = _trayIconImageGen();
	if (img.save(name, "PNG")) return name;

	QDir dir(info.absoluteDir());
	if (!dir.exists()) {
		dir.mkpath(dir.absolutePath());
		if (img.save(name, "PNG")) return name;
	}

	return QString();
}

void loadPixbuf(QImage image) {
	int w = image.width(), h = image.height(), perline = image.bytesPerLine(), s = image.byteCount();
	_trayPixbufData.resize(w * h * 4);
	uchar *result = (uchar*)_trayPixbufData.data(), *bytes = image.bits();
	for (int32 y = 0; y < h; ++y) {
		for (int32 x = 0; x < w; ++x) {
			int32 offset = (y * w + x) * 4, srcoff = y * perline + x * 4;
			result[offset + GTK_RED  ] = bytes[srcoff + QT_RED  ];
			result[offset + GTK_GREEN] = bytes[srcoff + QT_GREEN];
			result[offset + GTK_BLUE ] = bytes[srcoff + QT_BLUE ];
			result[offset + GTK_ALPHA] = bytes[srcoff + QT_ALPHA];
		}
	}

	if (_trayPixbuf) Libs::g_object_unref(_trayPixbuf);
	_trayPixbuf = Libs::gdk_pixbuf_new_from_data(result, GDK_COLORSPACE_RGB, true, 8, w, h, w * 4, 0, 0);
}

void _trayMenuCallback(GtkMenu *menu, gpointer data) {
	for (int32 i = 0, l = _trayItems.size(); i < l; ++i) {
		if ((void*)_trayItems.at(i).first == (void*)menu) {
			QMetaObject::invokeMethod(_trayItems.at(i).second, "triggered");
		}
	}
}

static gboolean _trayIconCheck(gpointer/* pIn*/) {
	if (useStatusIcon && !trayIconChecked) {
		if (Libs::gtk_status_icon_is_embedded(_trayIcon)) {
			trayIconChecked = true;
			cSetSupportTray(true);
			if (App::wnd()) {
				App::wnd()->psUpdateWorkmode();
				App::wnd()->psUpdateCounter();
				App::wnd()->updateTrayMenu();
			}
		}
	}
	return FALSE;
}

#ifndef TDESKTOP_DISABLE_UNITY_INTEGRATION
UnityLauncherEntry *_psUnityLauncherEntry = nullptr;
#endif // !TDESKTOP_DISABLE_UNITY_INTEGRATION

} // namespace

MainWindow::MainWindow()
: icon256(qsl(":/gui/art/icon256.png"))
, iconbig256(icon256)
, wndIcon(QIcon::fromTheme("telegram", QIcon(QPixmap::fromImage(icon256, Qt::ColorOnly)))) {
	connect(&_psCheckStatusIconTimer, SIGNAL(timeout()), this, SLOT(psStatusIconCheck()));
	_psCheckStatusIconTimer.setSingleShot(false);

	connect(&_psUpdateIndicatorTimer, SIGNAL(timeout()), this, SLOT(psUpdateIndicator()));
	_psUpdateIndicatorTimer.setSingleShot(true);
}

bool MainWindow::psHasTrayIcon() const {
	return trayIcon || ((useAppIndicator || (useStatusIcon && trayIconChecked)) && (cWorkMode() != dbiwmWindowOnly));
}

void MainWindow::psStatusIconCheck() {
	_trayIconCheck(0);
	if (cSupportTray() || !--_psCheckStatusIconLeft) {
		_psCheckStatusIconTimer.stop();
		return;
	}
}

void MainWindow::psShowTrayMenu() {
}

void MainWindow::psRefreshTaskbarIcon() {
}

void MainWindow::psTrayMenuUpdated() {
	if (noQtTrayIcon && (useAppIndicator || useStatusIcon)) {
		const QList<QAction*> &actions = trayIconMenu->actions();
		if (_trayItems.isEmpty()) {
			DEBUG_LOG(("Creating tray menu!"));
			for (int32 i = 0, l = actions.size(); i != l; ++i) {
				GtkWidget *item = Libs::gtk_menu_item_new_with_label(actions.at(i)->text().toUtf8());
				Libs::gtk_menu_shell_append(Libs::gtk_menu_shell_cast(_trayMenu), item);
				Libs::g_signal_connect_helper(item, "activate", G_CALLBACK(_trayMenuCallback), this);
				Libs::gtk_widget_show(item);
				Libs::gtk_widget_set_sensitive(item, actions.at(i)->isEnabled());

				_trayItems.push_back(qMakePair(item, actions.at(i)));
			}
		} else {
			DEBUG_LOG(("Updating tray menu!"));
			for (int32 i = 0, l = actions.size(); i != l; ++i) {
				if (i < _trayItems.size()) {
					Libs::gtk_menu_item_set_label(reinterpret_cast<GtkMenuItem*>(_trayItems.at(i).first), actions.at(i)->text().toUtf8());
					Libs::gtk_widget_set_sensitive(_trayItems.at(i).first, actions.at(i)->isEnabled());
				}
			}
		}
	}
}

void MainWindow::psSetupTrayIcon() {
	if (noQtTrayIcon) {
		if (!cSupportTray()) return;
		psUpdateCounter();
	} else {
		LOG(("Using Qt tray icon."));
		if (!trayIcon) {
			trayIcon = new QSystemTrayIcon(this);
			QIcon icon;
			QFileInfo iconFile(_trayIconImageFile());
			if (iconFile.exists()) {
				QByteArray path = QFile::encodeName(iconFile.absoluteFilePath());
				icon = QIcon(path.constData());
			} else {
				icon = QIcon(QPixmap::fromImage(App::wnd()->iconLarge(), Qt::ColorOnly));
			}
			trayIcon->setIcon(icon);

			trayIcon->setToolTip(str_const_toString(AppName));
			connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(toggleTray(QSystemTrayIcon::ActivationReason)), Qt::UniqueConnection);

			// This is very important for native notifications via libnotify!
			// Some notification servers compose several notifications with a "Reply"
			// action into one and after that a click on "Reply" button does not call
			// the specified callback from any of the sent notification - libnotify
			// just ignores ibus messages, but Qt tray icon at least emits this signal.
			connect(trayIcon, SIGNAL(messageClicked()), this, SLOT(showFromTray()));

			App::wnd()->updateTrayMenu();
		}
		psUpdateCounter();

		trayIcon->show();
	}
}

void MainWindow::psUpdateWorkmode() {
	if (!cSupportTray()) return;

	if (cWorkMode() == dbiwmWindowOnly) {
		if (noQtTrayIcon) {
			if (useAppIndicator) {
				Libs::app_indicator_set_status(_trayIndicator, APP_INDICATOR_STATUS_PASSIVE);
			} else if (useStatusIcon) {
				Libs::gtk_status_icon_set_visible(_trayIcon, false);
			}
		} else {
			if (trayIcon) {
				trayIcon->setContextMenu(0);
				trayIcon->deleteLater();
			}
			trayIcon = 0;
		}
	} else {
		if (noQtTrayIcon) {
			if (useAppIndicator) {
				Libs::app_indicator_set_status(_trayIndicator, APP_INDICATOR_STATUS_ACTIVE);
			} else if (useStatusIcon) {
				Libs::gtk_status_icon_set_visible(_trayIcon, true);
			}
		} else {
			psSetupTrayIcon();
		}
	}
}

void MainWindow::psUpdateIndicator() {
	_psUpdateIndicatorTimer.stop();
	_psLastIndicatorUpdate = getms();
	QFileInfo iconFile(_trayIconImageFile());
	if (iconFile.exists()) {
		QByteArray path = QFile::encodeName(iconFile.absoluteFilePath()), name = QFile::encodeName(iconFile.fileName());
		name = name.mid(0, name.size() - 4);
		Libs::app_indicator_set_icon_full(_trayIndicator, path.constData(), name);
	} else {
		useAppIndicator = false;
	}
}

void MainWindow::psUpdateCounter() {
	setWindowIcon(wndIcon);

	int32 counter = App::histories().unreadBadge();

	setWindowTitle((counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram"));
#ifndef TDESKTOP_DISABLE_UNITY_INTEGRATION
	if (_psUnityLauncherEntry) {
		if (counter > 0) {
			Libs::unity_launcher_entry_set_count(_psUnityLauncherEntry, (counter > 9999) ? 9999 : counter);
			Libs::unity_launcher_entry_set_count_visible(_psUnityLauncherEntry, TRUE);
		} else {
			Libs::unity_launcher_entry_set_count_visible(_psUnityLauncherEntry, FALSE);
		}
	}
#endif // !TDESKTOP_DISABLE_UNITY_INTEGRATION

	if (noQtTrayIcon) {
		if (useAppIndicator) {
			if (getms() > _psLastIndicatorUpdate + 1000) {
				psUpdateIndicator();
			} else if (!_psUpdateIndicatorTimer.isActive()) {
				_psUpdateIndicatorTimer.start(100);
			}
		} else if (useStatusIcon && trayIconChecked) {
			QFileInfo iconFile(_trayIconImageFile());
			if (iconFile.exists()) {
				QByteArray path = QFile::encodeName(iconFile.absoluteFilePath());
				Libs::gtk_status_icon_set_from_file(_trayIcon, path.constData());
			} else {
				loadPixbuf(_trayIconImageGen());
				Libs::gtk_status_icon_set_from_pixbuf(_trayIcon, _trayPixbuf);
			}
		}
	} else if (trayIcon) {
		QIcon icon;
		QFileInfo iconFile(_trayIconImageFile());
		if (iconFile.exists()) {
			QByteArray path = QFile::encodeName(iconFile.absoluteFilePath());
			icon = QIcon(path.constData());
		} else {
			int32 counter = App::histories().unreadBadge();
			bool muted = App::histories().unreadOnlyMuted();

			style::color bg = muted ? st::counterMuteBG : st::counterBG;
			icon.addPixmap(App::pixmapFromImageInPlace(iconWithCounter(16, counter, bg, true)));
			icon.addPixmap(App::pixmapFromImageInPlace(iconWithCounter(32, counter, bg, true)));
		}
		trayIcon->setIcon(icon);
	}
}

bool MainWindow::psHasNativeNotifications() {
	return Notifications::supported();
}

void MainWindow::LibsLoaded() {
	QString cdesktop = QString(getenv("XDG_CURRENT_DESKTOP")).toLower();
	noQtTrayIcon = (cdesktop == qstr("pantheon")) || (cdesktop == qstr("gnome"));
	tryAppIndicator = (cdesktop == qstr("xfce"));

	if (noQtTrayIcon) cSetSupportTray(false);

	useGtkBase = (Libs::gtk_init_check != nullptr)
			&& (Libs::gtk_menu_new != nullptr)
			&& (Libs::gtk_menu_get_type != nullptr)
			&& (Libs::gtk_menu_item_new_with_label != nullptr)
			&& (Libs::gtk_menu_item_set_label != nullptr)
			&& (Libs::gtk_menu_shell_append != nullptr)
			&& (Libs::gtk_menu_shell_get_type != nullptr)
			&& (Libs::gtk_widget_show != nullptr)
			&& (Libs::gtk_widget_get_toplevel != nullptr)
			&& (Libs::gtk_widget_get_visible != nullptr)
			&& (Libs::gtk_widget_set_sensitive != nullptr)
			&& (Libs::g_type_check_instance_cast != nullptr)
			&& (Libs::g_signal_connect_data != nullptr)
			&& (Libs::g_object_ref_sink != nullptr)
			&& (Libs::g_object_unref != nullptr);

	useAppIndicator = useGtkBase
			&& (Libs::app_indicator_new != nullptr)
			&& (Libs::app_indicator_set_status != nullptr)
			&& (Libs::app_indicator_set_menu != nullptr)
			&& (Libs::app_indicator_set_icon_full != nullptr);

	if (tryAppIndicator && useGtkBase && useAppIndicator) {
		noQtTrayIcon = true;
		cSetSupportTray(false);
	}

	useStatusIcon = (Libs::gdk_init_check != nullptr)
			&& (Libs::gdk_pixbuf_new_from_data != nullptr)
			&& (Libs::gtk_status_icon_new_from_pixbuf != nullptr)
			&& (Libs::gtk_status_icon_set_from_pixbuf != nullptr)
			&& (Libs::gtk_status_icon_new_from_file != nullptr)
			&& (Libs::gtk_status_icon_set_from_file != nullptr)
			&& (Libs::gtk_status_icon_set_title != nullptr)
			&& (Libs::gtk_status_icon_set_tooltip_text != nullptr)
			&& (Libs::gtk_status_icon_set_visible != nullptr)
			&& (Libs::gtk_status_icon_is_embedded != nullptr)
			&& (Libs::gtk_status_icon_get_geometry != nullptr)
			&& (Libs::gtk_status_icon_position_menu != nullptr)
			&& (Libs::gtk_menu_popup != nullptr)
			&& (Libs::gtk_get_current_event_time != nullptr)
			&& (Libs::g_idle_add != nullptr);
	if (useStatusIcon) {
		DEBUG_LOG(("Status icon api loaded!"));
	}

#ifndef TDESKTOP_DISABLE_UNITY_INTEGRATION
	useUnityCount = (Libs::unity_launcher_entry_get_for_desktop_id != nullptr)
			&& (Libs::unity_launcher_entry_set_count != nullptr)
			&& (Libs::unity_launcher_entry_set_count_visible != nullptr);
	if (useUnityCount) {
		DEBUG_LOG(("Unity count api loaded!"));
	}
#endif // !TDESKTOP_DISABLE_UNITY_INTEGRATION
}

void MainWindow::psInitSize() {
	setMinimumWidth(st::wndMinWidth);
	setMinimumHeight(st::wndMinHeight);

	TWindowPos pos(cWindowPos());
	QRect avail(QDesktopWidget().availableGeometry());
	bool maximized = false;
	QRect geom(avail.x() + (avail.width() - st::wndDefWidth) / 2, avail.y() + (avail.height() - st::wndDefHeight) / 2, st::wndDefWidth, st::wndDefHeight);
	if (pos.w && pos.h) {
		QList<QScreen*> screens = Application::screens();
		for (QList<QScreen*>::const_iterator i = screens.cbegin(), e = screens.cend(); i != e; ++i) {
			QByteArray name = (*i)->name().toUtf8();
			if (pos.moncrc == hashCrc32(name.constData(), name.size())) {
				QRect screen((*i)->geometry());
				int32 w = screen.width(), h = screen.height();
				if (w >= st::wndMinWidth && h >= st::wndMinHeight) {
					if (pos.w > w) pos.w = w;
					if (pos.h > h) pos.h = h;
					pos.x += screen.x();
					pos.y += screen.y();
					if (pos.x < screen.x() + screen.width() - 10 && pos.y < screen.y() + screen.height() - 10) {
						geom = QRect(pos.x, pos.y, pos.w, pos.h);
					}
				}
				break;
			}
		}

		if (pos.y < 0) pos.y = 0;
		maximized = pos.maximized;
	}
	setGeometry(geom);
}

void MainWindow::psInitFrameless() {
	psUpdatedPositionTimer.setSingleShot(true);
	connect(&psUpdatedPositionTimer, SIGNAL(timeout()), this, SLOT(psSavePosition()));
}

void MainWindow::psSavePosition(Qt::WindowState state) {
	if (state == Qt::WindowActive) state = windowHandle()->windowState();
	if (state == Qt::WindowMinimized || !posInited) return;

	TWindowPos pos(cWindowPos()), curPos = pos;

	if (state == Qt::WindowMaximized) {
		curPos.maximized = 1;
	} else {
		QRect r(geometry());
		curPos.x = r.x();
		curPos.y = r.y();
		curPos.w = r.width();
		curPos.h = r.height();
		curPos.maximized = 0;
	}

	int px = curPos.x + curPos.w / 2, py = curPos.y + curPos.h / 2, d = 0;
	QScreen *chosen = 0;
	QList<QScreen*> screens = Application::screens();
	for (QList<QScreen*>::const_iterator i = screens.cbegin(), e = screens.cend(); i != e; ++i) {
		int dx = (*i)->geometry().x() + (*i)->geometry().width() / 2 - px; if (dx < 0) dx = -dx;
		int dy = (*i)->geometry().y() + (*i)->geometry().height() / 2 - py; if (dy < 0) dy = -dy;
		if (!chosen || dx + dy < d) {
			d = dx + dy;
			chosen = *i;
		}
	}
	if (chosen) {
		curPos.x -= chosen->geometry().x();
		curPos.y -= chosen->geometry().y();
		QByteArray name = chosen->name().toUtf8();
		curPos.moncrc = hashCrc32(name.constData(), name.size());
	}

	if (curPos.w >= st::wndMinWidth && curPos.h >= st::wndMinHeight) {
		if (curPos.x != pos.x || curPos.y != pos.y || curPos.w != pos.w || curPos.h != pos.h || curPos.moncrc != pos.moncrc || curPos.maximized != pos.maximized) {
			cSetWindowPos(curPos);
			Local::writeSettings();
		}
	}
}

void MainWindow::psUpdatedPosition() {
	psUpdatedPositionTimer.start(SaveWindowPositionTimeout);
}

void MainWindow::psCreateTrayIcon() {
	if (!noQtTrayIcon) {
		cSetSupportTray(QSystemTrayIcon::isSystemTrayAvailable());
		return;
	}

	if (useAppIndicator) {
		DEBUG_LOG(("Trying to create AppIndicator"));
		_trayMenu = Libs::gtk_menu_new();
		if (_trayMenu) {
			DEBUG_LOG(("Created gtk menu for appindicator!"));
			QFileInfo iconFile(_trayIconImageFile());
			if (iconFile.exists()) {
				QByteArray path = QFile::encodeName(iconFile.absoluteFilePath());
				_trayIndicator = Libs::app_indicator_new("Telegram Desktop", path.constData(), APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
				if (_trayIndicator) {
					LOG(("Using appindicator tray icon."));
				} else {
					DEBUG_LOG(("Failed to app_indicator_new()!"));
				}
			} else {
				useAppIndicator = false;
				DEBUG_LOG(("Failed to create image file!"));
			}
		} else {
			DEBUG_LOG(("Failed to gtk_menu_new()!"));
		}
		if (_trayMenu && _trayIndicator) {
			Libs::app_indicator_set_status(_trayIndicator, APP_INDICATOR_STATUS_ACTIVE);
			Libs::app_indicator_set_menu(_trayIndicator, Libs::gtk_menu_cast(_trayMenu));
			useStatusIcon = false;
		} else {
			DEBUG_LOG(("AppIndicator failed!"));
			useAppIndicator = false;
		}
	}
	if (useStatusIcon) {
		if (Libs::gdk_init_check(0, 0)) {
			if (!_trayMenu) _trayMenu = Libs::gtk_menu_new();
			if (_trayMenu) {
				QFileInfo iconFile(_trayIconImageFile());
				if (iconFile.exists()) {
					QByteArray path = QFile::encodeName(iconFile.absoluteFilePath());
					_trayIcon = Libs::gtk_status_icon_new_from_file(path.constData());
				} else {
					loadPixbuf(_trayIconImageGen());
					_trayIcon = Libs::gtk_status_icon_new_from_pixbuf(_trayPixbuf);
				}
				if (_trayIcon) {
					LOG(("Using GTK status tray icon."));

					Libs::g_signal_connect_helper(_trayIcon, "popup-menu", GCallback(_trayIconPopup), _trayMenu);
					Libs::g_signal_connect_helper(_trayIcon, "activate", GCallback(_trayIconActivate), _trayMenu);
					Libs::g_signal_connect_helper(_trayIcon, "size-changed", GCallback(_trayIconResized), _trayMenu);

					Libs::gtk_status_icon_set_title(_trayIcon, "Telegram Desktop");
					Libs::gtk_status_icon_set_tooltip_text(_trayIcon, "Telegram Desktop");
					Libs::gtk_status_icon_set_visible(_trayIcon, true);
				} else {
					useStatusIcon = false;
				}
			} else {
				useStatusIcon = false;
			}
		} else {
			useStatusIcon = false;
		}
	}
	if (!useStatusIcon && !useAppIndicator) {
		if (_trayMenu) {
			Libs::g_object_ref_sink(_trayMenu);
			Libs::g_object_unref(_trayMenu);
			_trayMenu = 0;
		}
	}
	cSetSupportTray(useAppIndicator);
	if (useStatusIcon) {
		Libs::g_idle_add((GSourceFunc)_trayIconCheck, 0);
		_psCheckStatusIconTimer.start(100);
	} else {
		psUpdateWorkmode();
	}
}

void MainWindow::psFirstShow() {
	psCreateTrayIcon();

#ifndef TDESKTOP_DISABLE_UNITY_INTEGRATION
	if (useUnityCount) {
		_psUnityLauncherEntry = Libs::unity_launcher_entry_get_for_desktop_id("telegramdesktop.desktop");
		if (_psUnityLauncherEntry) {
			LOG(("Found Unity Launcher entry telegramdesktop.desktop!"));
		} else {
			_psUnityLauncherEntry = Libs::unity_launcher_entry_get_for_desktop_id("Telegram.desktop");
			if (_psUnityLauncherEntry) {
				LOG(("Found Unity Launcher entry Telegram.desktop!"));
			} else {
				LOG(("Could not get Unity Launcher entry!"));
			}
		}
	} else {
		LOG(("Not using Unity Launcher count."));
	}
#endif // !TDESKTOP_DISABLE_UNITY_INTEGRATION

	psUpdateMargins();

	bool showShadows = true;

	show();
	//_private.enableShadow(winId());
	if (cWindowPos().maximized) {
		setWindowState(Qt::WindowMaximized);
	}

	if ((cLaunchMode() == LaunchModeAutoStart && cStartMinimized()) || cStartInTray()) {
		setWindowState(Qt::WindowMinimized);
		if (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray) {
			hide();
		} else {
			show();
		}
		showShadows = false;
	} else {
		show();
	}

	posInited = true;
}

bool MainWindow::psHandleTitle() {
	return false;
}

void MainWindow::psInitSysMenu() {
}

void MainWindow::psUpdateSysMenu(Qt::WindowState state) {
}

void MainWindow::psUpdateMargins() {
}

void MainWindow::psFlash() {
}

MainWindow::~MainWindow() {
	if (_trayIcon) {
		Libs::g_object_unref(_trayIcon);
		_trayIcon = nullptr;
	}
	if (_trayPixbuf) {
		Libs::g_object_unref(_trayPixbuf);
		_trayPixbuf = nullptr;
	}
	if (_trayMenu) {
		Libs::g_object_ref_sink(_trayMenu);
		Libs::g_object_unref(_trayMenu);
		_trayMenu = nullptr;
	}
}

} // namespace Platform
