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
#include "settings/settings_notifications_widget.h"

#include "styles/style_settings.h"
#include "lang.h"
#include "localstorage.h"
#include "ui/effects/widget_slide_wrap.h"
#include "ui/flatcheckbox.h"
#include "mainwindow.h"
#include "window/notifications_manager.h"
#include "boxes/notifications_box.h"

namespace Settings {

NotificationsWidget::NotificationsWidget(QWidget *parent, UserData *self) : BlockWidget(parent, self, lang(lng_settings_section_notify)) {
	createControls();

	subscribe(Global::RefNotifySettingsChanged(), [this](Notify::ChangeType type) {
		if (type == Notify::ChangeType::DesktopEnabled) {
			desktopEnabledUpdated();
		} else if (type == Notify::ChangeType::ViewParams) {
			viewParamUpdated();
		} else if (type == Notify::ChangeType::SoundEnabled) {
			_playSound->setChecked(Global::SoundNotify());
		}
	});
}

void NotificationsWidget::createControls() {
	style::margins margin(0, 0, 0, st::settingsSkip);
	style::margins slidedPadding(0, margin.bottom() / 2, 0, margin.bottom() - (margin.bottom() / 2));
	addChildRow(_desktopNotifications, margin, lang(lng_settings_desktop_notify), SLOT(onDesktopNotifications()), Global::DesktopNotify());
	addChildRow(_showSenderName, margin, slidedPadding, lang(lng_settings_show_name), SLOT(onShowSenderName()), Global::NotifyView() <= dbinvShowName);
	addChildRow(_showMessagePreview, margin, slidedPadding, lang(lng_settings_show_preview), SLOT(onShowMessagePreview()), Global::NotifyView() <= dbinvShowPreview);
	if (!_showSenderName->entity()->checked()) {
		_showMessagePreview->hideFast();
	}
	if (!_desktopNotifications->checked()) {
		_showSenderName->hideFast();
		_showMessagePreview->hideFast();
	}
	addChildRow(_playSound, margin, lang(lng_settings_sound_notify), SLOT(onPlaySound()), Global::SoundNotify());
	addChildRow(_includeMuted, margin, lang(lng_settings_include_muted), SLOT(onIncludeMuted()), Global::IncludeMuted());

	if (cPlatform() != dbipMac) {
		createNotificationsControls();
	}
}

void NotificationsWidget::createNotificationsControls() {
	style::margins margin(0, 0, 0, st::settingsSkip);
	style::margins slidedPadding(0, margin.bottom() / 2, 0, margin.bottom() - (margin.bottom() / 2));

	QString nativeNotificationsLabel;
#ifdef Q_OS_WIN
	if (App::wnd()->psHasNativeNotifications()) {
		nativeNotificationsLabel = lang(lng_settings_use_windows);
	}
#elif defined Q_OS_LINUX64 || defined Q_OS_LINUX32
	if (App::wnd()->psHasNativeNotifications()) {
		nativeNotificationsLabel = lang(lng_settings_use_native_notifications);
	}
#endif // Q_OS_WIN
	if (!nativeNotificationsLabel.isEmpty()) {
		addChildRow(_nativeNotifications, margin, nativeNotificationsLabel, SLOT(onNativeNotifications()), Global::NativeNotifications());
	}
	addChildRow(_advanced, margin, slidedPadding, lang(lng_settings_advanced_notifications), SLOT(onAdvanced()));
	if (!nativeNotificationsLabel.isEmpty() && Global::NativeNotifications()) {
		_advanced->hideFast();
	}
}

void NotificationsWidget::onDesktopNotifications() {
	if (Global::DesktopNotify() == _desktopNotifications->checked()) {
		return;
	}
	Global::SetDesktopNotify(_desktopNotifications->checked());
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::DesktopEnabled);
}

void NotificationsWidget::desktopEnabledUpdated() {
	_desktopNotifications->setChecked(Global::DesktopNotify());
	if (Global::DesktopNotify()) {
		_showSenderName->slideDown();
		if (_showSenderName->entity()->checked()) {
			_showMessagePreview->slideDown();
		}
	} else {
		_showSenderName->slideUp();
		_showMessagePreview->slideUp();
	}
}

void NotificationsWidget::onShowSenderName() {
	auto viewParam = ([this]() {
		if (!_showSenderName->entity()->checked()) {
			return dbinvShowNothing;
		} else if (!_showMessagePreview->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowPreview;
	})();
	if (viewParam == Global::NotifyView()) {
		return;
	}
	Global::SetNotifyView(viewParam);
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::ViewParams);
}

void NotificationsWidget::onShowMessagePreview() {
	auto viewParam = ([this]() {
		if (_showMessagePreview->entity()->checked()) {
			return dbinvShowPreview;
		} else if (_showSenderName->entity()->checked()) {
			return dbinvShowName;
		}
		return dbinvShowNothing;
	})();
	if (viewParam == Global::NotifyView()) {
		return;
	}

	Global::SetNotifyView(viewParam);
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::ViewParams);
}

void NotificationsWidget::viewParamUpdated() {
	if (_showSenderName->entity()->checked()) {
		_showMessagePreview->slideDown();
	} else {
		_showMessagePreview->slideUp();
	}
}

void NotificationsWidget::onNativeNotifications() {
	if (Global::NativeNotifications() == _nativeNotifications->checked()) {
		return;
	}

	Window::Notifications::manager()->clearAllFast();
	Global::SetNativeNotifications(_nativeNotifications->checked());
	Local::writeUserSettings();

	if (Global::NativeNotifications()) {
		_advanced->slideUp();
	} else {
		_advanced->slideDown();
	}
}

void NotificationsWidget::onAdvanced() {
	Ui::showLayer(new NotificationsBox());
}

void NotificationsWidget::onPlaySound() {
	if (_playSound->checked() == Global::SoundNotify()) {
		return;
	}

	Global::SetSoundNotify(_playSound->checked());
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::SoundEnabled);
}

void NotificationsWidget::onIncludeMuted() {
	Global::SetIncludeMuted(_includeMuted->checked());
	Local::writeUserSettings();
	Global::RefNotifySettingsChanged().notify(Notify::ChangeType::IncludeMuted);
}

} // namespace Settings
