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
#include "platform/linux/notifications_manager_linux.h"

#include "window/notifications_utilities.h"
#include "platform/linux/linux_libnotify.h"
#include "platform/linux/linux_libs.h"
#include "lang.h"

namespace Platform {
namespace Notifications {
namespace {

NeverFreedPointer<Manager> ManagerInstance;

bool LibNotifyLoaded() {
	return (Libs::notify_init != nullptr)
		&& (Libs::notify_uninit != nullptr)
		&& (Libs::notify_is_initted != nullptr)
//		&& (Libs::notify_get_app_name != nullptr)
//		&& (Libs::notify_set_app_name != nullptr)
		&& (Libs::notify_get_server_caps != nullptr)
		&& (Libs::notify_get_server_info != nullptr)
		&& (Libs::notify_notification_new != nullptr)
//		&& (Libs::notify_notification_update != nullptr)
		&& (Libs::notify_notification_show != nullptr)
//		&& (Libs::notify_notification_set_app_name != nullptr)
		&& (Libs::notify_notification_set_timeout != nullptr)
//		&& (Libs::notify_notification_set_category != nullptr)
//		&& (Libs::notify_notification_set_urgency != nullptr)
//		&& (Libs::notify_notification_set_icon_from_pixbuf != nullptr)
		&& (Libs::notify_notification_set_image_from_pixbuf != nullptr)
//		&& (Libs::notify_notification_set_hint != nullptr)
//		&& (Libs::notify_notification_set_hint_int32 != nullptr)
//		&& (Libs::notify_notification_set_hint_uint32 != nullptr)
//		&& (Libs::notify_notification_set_hint_double != nullptr)
		&& (Libs::notify_notification_set_hint_string != nullptr)
//		&& (Libs::notify_notification_set_hint_byte != nullptr)
//		&& (Libs::notify_notification_set_hint_byte_array != nullptr)
//		&& (Libs::notify_notification_clear_hints != nullptr)
		&& (Libs::notify_notification_add_action != nullptr)
		&& (Libs::notify_notification_clear_actions != nullptr)
		&& (Libs::notify_notification_close != nullptr)
		&& (Libs::notify_notification_get_closed_reason != nullptr)
		&& (Libs::g_object_ref_sink != nullptr)
		&& (Libs::g_object_unref != nullptr)
		&& (Libs::g_list_free_full != nullptr)
		&& (Libs::g_error_free != nullptr)
		&& (Libs::g_signal_connect_data != nullptr)
		&& (Libs::g_signal_handler_disconnect != nullptr)
//		&& (Libs::gdk_pixbuf_new_from_data != nullptr)
		&& (Libs::gdk_pixbuf_new_from_file != nullptr);
}

QString escapeHtml(const QString &text) {
	auto result = QString();
	auto copyFrom = 0, textSize = text.size();
	auto data = text.constData();
	for (auto i = 0; i != textSize; ++i) {
		auto ch = data[i];
		if (ch == '<' || ch == '>' || ch == '&') {
			if (!copyFrom) {
				result.reserve(textSize * 5);
			}
			if (i > copyFrom) {
				result.append(data + copyFrom, i - copyFrom);
			}
			switch (ch.unicode()) {
			case '<': result.append(qstr("&lt;")); break;
			case '>': result.append(qstr("&gt;")); break;
			case '&': result.append(qstr("&amp;")); break;
			}
			copyFrom = i + 1;
		}
	}
	if (copyFrom > 0) {
		result.append(data + copyFrom, textSize - copyFrom);
		return result;
	}
	return text;
}

class NotificationData {
public:
	NotificationData(const QString &title, const QString &body, const QStringList &capabilities, PeerId peerId, MsgId msgId)
	: _data(Libs::notify_notification_new(title.toUtf8().constData(), body.toUtf8().constData(), nullptr)) {
		if (valid()) {
			init(capabilities, peerId, msgId);
		}
	}
	bool valid() const {
		return (_data != nullptr);
	}
	NotificationData(const NotificationData &other) = delete;
	NotificationData &operator=(const NotificationData &other) = delete;
	NotificationData(NotificationData &&other) = delete;
	NotificationData &operator=(NotificationData &&other) = delete;

	void setImage(const QString &imagePath) {
		auto imagePathNative = QFile::encodeName(imagePath);
		if (auto pixbuf = Libs::gdk_pixbuf_new_from_file(imagePathNative.constData(), nullptr)) {
			Libs::notify_notification_set_image_from_pixbuf(_data, pixbuf);
			Libs::g_object_unref(Libs::g_object_cast(pixbuf));
		}
	}
	bool show() {
		if (valid()) {
			GError *error = nullptr;
			Libs::notify_notification_show(_data, &error);
			if (!error) {
				return true;
			}

			logError(error);
		}
		return false;
	}

	bool close() {
		if (valid()) {
			GError *error = nullptr;
			Libs::notify_notification_close(_data, &error);
			if (!error) {
				return true;
			}

			logError(error);
		}
		return false;
	}

	~NotificationData() {
		if (valid()) {
//			if (_handlerId > 0) {
//				Libs::g_signal_handler_disconnect(Libs::g_object_cast(_data), _handlerId);
//			}
//			Libs::notify_notification_clear_actions(_data);
			Libs::g_object_unref(Libs::g_object_cast(_data));
		}
	}

private:
	void init(const QStringList &capabilities, PeerId peerId, MsgId msgId) {
		if (capabilities.contains(qsl("append"))) {
			Libs::notify_notification_set_hint_string(_data, "append", "true");
		} else if (capabilities.contains(qsl("x-canonical-append"))) {
			Libs::notify_notification_set_hint_string(_data, "x-canonical-append", "true");
		}

		auto signalReceiver = Libs::g_object_cast(_data);
		auto signalHandler = G_CALLBACK(NotificationData::notificationClosed);
		auto signalName = "closed";
		auto signalDataFreeMethod = &NotificationData::notificationDataFreeClosure;
		auto signalData = new NotificationDataStruct(peerId, msgId);
		_handlerId = Libs::g_signal_connect_helper(signalReceiver, signalName, signalHandler, signalData, signalDataFreeMethod);

		Libs::notify_notification_set_timeout(_data, Libs::NOTIFY_EXPIRES_DEFAULT);

		if (auto manager = ManagerInstance.data()) {
			if (manager->hasActionsSupport()) {
				auto label = lang(lng_notification_reply).toUtf8();
				auto actionReceiver = _data;
				auto actionHandler = &NotificationData::notificationClicked;
				auto actionLabel = label.constData();
				auto actionName = "default";
				auto actionDataFreeMethod = &NotificationData::notificationDataFree;
				auto actionData = new NotificationDataStruct(peerId, msgId);
				Libs::notify_notification_add_action(actionReceiver, actionName, actionLabel, actionHandler, actionData, actionDataFreeMethod);
			}
		}
	}

	void logError(GError *error) {
		LOG(("LibNotify Error: domain %1, code %2, message '%3'").arg(error->domain).arg(error->code).arg(QString::fromUtf8(error->message)));
		Libs::g_error_free(error);
	}

	struct NotificationDataStruct {
		NotificationDataStruct(PeerId peerId, MsgId msgId) : peerId(peerId), msgId(msgId) {
		}

		PeerId peerId = 0;
		MsgId msgId = 0;
	};
	static void notificationDataFree(gpointer data) {
		auto notificationData = static_cast<NotificationDataStruct*>(data);
		delete notificationData;
	}
	static void notificationDataFreeClosure(gpointer data, GClosure *closure) {
		auto notificationData = static_cast<NotificationDataStruct*>(data);
		delete notificationData;
	}
	static void notificationClosed(Libs::NotifyNotification *notification, gpointer data) {
		auto closedReason = Libs::notify_notification_get_closed_reason(notification);
		auto notificationData = static_cast<NotificationDataStruct*>(data);
		if (auto manager = ManagerInstance.data()) {
			manager->clearNotification(notificationData->peerId, notificationData->msgId);
		}
	}
	static void notificationClicked(Libs::NotifyNotification *notification, char *action, gpointer data) {
		auto notificationData = static_cast<NotificationDataStruct*>(data);
		if (auto manager = ManagerInstance.data()) {
			manager->notificationActivated(notificationData->peerId, notificationData->msgId);
		}
	}

	Libs::NotifyNotification *_data = nullptr;
	gulong _handlerId = 0;

};

using Notification = QSharedPointer<NotificationData>;

} // namespace

void start() {
	if (LibNotifyLoaded()) {
		if (Libs::notify_is_initted() || Libs::notify_init("Telegram Desktop")) {
			ManagerInstance.createIfNull();
			if (!ManagerInstance->init()) {
				ManagerInstance.clear();
				LOG(("LibNotify Error: manager failed to init!"));
			}
		} else {
			LOG(("LibNotify Error: failed to init!"));
		}
	}
}

Manager *manager() {
	if (Global::started() && Global::NativeNotifications()) {
		return ManagerInstance.data();
	}
	return nullptr;
}

bool supported() {
	return ManagerInstance.data() != nullptr;
}

void finish() {
	if (manager()) {
		ManagerInstance.reset();
		Libs::notify_uninit();
	}
}

class Manager::Impl {
public:
	bool init();

	void showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton);
	void clearAll();
	void clearFromHistory(History *history);
	void clearNotification(PeerId peerId, MsgId msgId);

	bool hasPoorSupport() const {
		return _poorSupported;
	}
	bool hasActionsSupport() const {
		return _actionsSupported;
	}

private:
	QString escapeNotificationText(const QString &text) const;
	void showNextNotification();

	struct QueuedNotification {
		PeerData *peer = nullptr;
		MsgId msgId = 0;
		QString title;
		QString body;
		bool hideNameAndPhoto = false;
	};

	QString _serverName;
	QStringList _capabilities;

	using QueuedNotifications = QList<QueuedNotification>;
	QueuedNotifications _queuedNotifications;

	using Notifications = QMap<PeerId, QMap<MsgId, Notification>>;
	Notifications _notifications;

	Window::Notifications::CachedUserpics _cachedUserpics;
	bool _actionsSupported = false;
	bool _markupSupported = false;
	bool _poorSupported = false;

};

bool Manager::Impl::init() {
	if (auto capabilities = Libs::notify_get_server_caps()) {
		for (auto capability = capabilities; capability; capability = capability->next) {
			auto capabilityText = QString::fromUtf8(static_cast<const char*>(capability->data));
			_capabilities.push_back(capabilityText);
		}
		Libs::g_list_free_full(capabilities, g_free);

		LOG(("LibNotify capabilities: %1").arg(_capabilities.join(qstr(", "))));
		if (_capabilities.contains(qsl("actions"))) {
			_actionsSupported = true;
		} else if (_capabilities.contains(qsl("body-markup"))) {
			_markupSupported = true;
		}
	} else {
		LOG(("LibNotify Error: could not get capabilities!"));
	}

	// Unity and other Notify OSD users handle desktop notifications
	// extremely poor, even without the ability to close() them.
	gchar *name = nullptr;
	if (Libs::notify_get_server_info(&name, nullptr, nullptr, nullptr)) {
		if (name) {
			_serverName = QString::fromUtf8(static_cast<const char*>(name));
			Libs::g_free(name);

			LOG(("Notifications Server: %1").arg(_serverName));
			if (_serverName == qstr("notify-osd")) {
//				_poorSupported = true;
				_actionsSupported = false;
			}
		} else {
			LOG(("LibNotify Error: successfully got empty server name!"));
		}
	} else {
		LOG(("LibNotify Error: could not get server name!"));
	}

	return !_serverName.isEmpty();
}

QString Manager::Impl::escapeNotificationText(const QString &text) const {
	return _markupSupported ? escapeHtml(text) : text;
}

void Manager::Impl::showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton) {
	auto titleText = escapeNotificationText(title);
	auto subtitleText = escapeNotificationText(subtitle);
	auto msgText = escapeNotificationText(msg);
	if (_markupSupported && !subtitleText.isEmpty()) {
		subtitleText = qstr("<b>") + subtitleText + qstr("</b>");
	}
	auto bodyText = subtitleText.isEmpty() ? msgText : (subtitleText + '\n' + msgText);

	QueuedNotification notification;
	notification.peer = peer;
	notification.msgId = msgId;
	notification.title = titleText;
	notification.body = bodyText;
	notification.hideNameAndPhoto = hideNameAndPhoto;
	_queuedNotifications.push_back(notification);

	showNextNotification();
}

void Manager::Impl::showNextNotification() {
	// Show only one notification at a time in Unity / Notify OSD.
	if (_poorSupported) {
		for (auto b = _notifications.begin(); !_notifications.isEmpty() && b->isEmpty();) {
			_notifications.erase(b);
		}
		if (!_notifications.isEmpty()) {
			return;
		}
	}

	QueuedNotification data;
	while (!_queuedNotifications.isEmpty()) {
		data = _queuedNotifications.front();
		_queuedNotifications.pop_front();
		if (data.peer) {
			break;
		}
	}
	if (!data.peer) {
		return;
	}

	auto peerId = data.peer->id;
	auto msgId = data.msgId;
	auto notification = MakeShared<NotificationData>(data.title, data.body, _capabilities, peerId, msgId);
	if (!notification->valid()) {
		return;
	}

	StorageKey key;
	if (data.hideNameAndPhoto) {
		key = StorageKey(0, 0);
	} else {
		key = data.peer->userpicUniqueKey();
	}
	notification->setImage(_cachedUserpics.get(key, data.peer));

	auto i = _notifications.find(peerId);
	if (i != _notifications.cend()) {
		auto j = i->find(msgId);
		if (j != i->cend()) {
			auto oldNotification = j.value();
			i->erase(j);
			oldNotification->close();
			i = _notifications.find(peerId);
		}
	}
	if (i == _notifications.cend()) {
		i = _notifications.insert(peerId, QMap<MsgId, Notification>());
	}
	_notifications[peerId].insert(msgId, notification);
	if (!notification->show()) {
		i = _notifications.find(peerId);
		if (i != _notifications.cend()) {
			i->remove(msgId);
			if (i->isEmpty()) _notifications.erase(i);
		}
		showNextNotification();
	}
}

void Manager::Impl::clearAll() {
	_queuedNotifications.clear();

	auto temp = base::take(_notifications);
	for_const (auto &notifications, temp) {
		for_const (auto notification, notifications) {
			notification->close();
		}
	}
}

void Manager::Impl::clearFromHistory(History *history) {
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.end();) {
		if (i->peer == history->peer) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}

	auto i = _notifications.find(history->peer->id);
	if (i != _notifications.cend()) {
		auto temp = base::take(i.value());
		_notifications.erase(i);

		for_const (auto notification, temp) {
			notification->close();
		}
	}

	showNextNotification();
}

void Manager::Impl::clearNotification(PeerId peerId, MsgId msgId) {
	auto i = _notifications.find(peerId);
	if (i != _notifications.cend()) {
		i.value().remove(msgId);
		if (i.value().isEmpty()) {
			_notifications.erase(i);
		}
	}

	showNextNotification();
}

Manager::Manager() : _impl(std_::make_unique<Impl>()) {
}

bool Manager::init() {
	return _impl->init();
}

void Manager::clearNotification(PeerId peerId, MsgId msgId) {
	_impl->clearNotification(peerId, msgId);
}

bool Manager::hasPoorSupport() const {
	return _impl->hasPoorSupport();
}

bool Manager::hasActionsSupport() const {
	return _impl->hasActionsSupport();
}

Manager::~Manager() = default;

void Manager::doShowNativeNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton) {
	_impl->showNotification(peer, msgId, title, subtitle, msg, hideNameAndPhoto, hideReplyButton);
}

void Manager::doClearAllFast() {
	_impl->clearAll();
}

void Manager::doClearFromHistory(History *history) {
	_impl->clearFromHistory(history);
}

} // namespace Notifications
} // namespace Platform
