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
#include "platform/mac/notifications_manager_mac.h"

#include "pspecific.h"
#include "platform/mac/mac_utilities.h"
#include "styles/style_window.h"

#include <Cocoa/Cocoa.h>

namespace {

NeverFreedPointer<Platform::Notifications::Manager> ManagerInstance;

} // namespace

NSImage *qt_mac_create_nsimage(const QPixmap &pm);

@interface NotificationDelegate : NSObject<NSUserNotificationCenterDelegate> {
}

- (id) init;
- (void)userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification;
- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification;

@end

@implementation NotificationDelegate {
}

- (id) init {
	if (self = [super init]) {
	}
	return self;
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification {
	auto manager = ManagerInstance.data();
	if (!manager) {
		return;
	}

	NSDictionary *notificationUserInfo = [notification userInfo];
	NSNumber *launchIdObject = [notificationUserInfo objectForKey:@"launch"];
	auto notificationLaunchId = launchIdObject ? [launchIdObject unsignedLongLongValue] : 0ULL;
	DEBUG_LOG(("Received notification with instance %1").arg(notificationLaunchId));
	if (notificationLaunchId != Global::LaunchId()) { // other app instance notification
		return;
	}

	NSNumber *peerObject = [notificationUserInfo objectForKey:@"peer"];
	auto notificationPeerId = peerObject ? [peerObject unsignedLongLongValue] : 0ULL;
	if (!notificationPeerId) {
		return;
	}

	NSNumber *msgObject = [notificationUserInfo objectForKey:@"msgid"];
	auto notificationMsgId = msgObject ? [msgObject intValue] : 0;
	if (notification.activationType == NSUserNotificationActivationTypeReplied) {
		auto notificationReply = QString::fromUtf8([[[notification response] string] UTF8String]);
		manager->notificationReplied(notificationPeerId, notificationMsgId, notificationReply);
	} else if (notification.activationType == NSUserNotificationActivationTypeContentsClicked) {
		manager->notificationActivated(notificationPeerId, notificationMsgId);
	}

	[center removeDeliveredNotification: notification];
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification {
	return YES;
}

@end

namespace Platform {
namespace Notifications {

void start() {
	if (cPlatform() != dbipMacOld) {
		ManagerInstance.createIfNull();
	}
}

Manager *manager() {
	return ManagerInstance.data();
}

void finish() {
	ManagerInstance.clear();
}

void defaultNotificationShown(QWidget *widget) {
	widget->hide();
	objc_holdOnTop(widget->winId());
	widget->show();
	psShowOverAll(widget, false);
}

class Manager::Impl {
public:
	Impl();
	void showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton);
	void clearAll();
	void clearFromHistory(History *history);
	void updateDelegate();

	~Impl();

private:
	NotificationDelegate *_delegate;

};

Manager::Impl::Impl() : _delegate([[NotificationDelegate alloc] init]) {
}

void Manager::Impl::showNotification(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, const QString &msg, bool hideNameAndPhoto, bool hideReplyButton) {
	@autoreleasepool {

	NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
	if ([notification respondsToSelector:@selector(setIdentifier:)]) {
		auto identifier = QString::number(Global::LaunchId()) + '_' + QString::number(peer->id) + '_' + QString::number(msgId);
		auto identifierValue = Q2NSString(identifier);
		[notification setIdentifier:identifierValue];
	}
	[notification setUserInfo:[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithUnsignedLongLong:peer->id],@"peer",[NSNumber numberWithInt:msgId],@"msgid",[NSNumber numberWithUnsignedLongLong:Global::LaunchId()],@"launch",nil]];

	[notification setTitle:Q2NSString(title)];
	[notification setSubtitle:Q2NSString(subtitle)];
	[notification setInformativeText:Q2NSString(msg)];
	if (!hideNameAndPhoto && [notification respondsToSelector:@selector(setContentImage:)]) {
		auto userpic = peer->genUserpic(st::notifyMacPhotoSize);
		NSImage *img = [qt_mac_create_nsimage(userpic) autorelease];
		[notification setContentImage:img];
	}

	if (!hideReplyButton && [notification respondsToSelector:@selector(setHasReplyButton:)]) {
		[notification setHasReplyButton:YES];
	}

	[notification setSoundName:nil];

	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center deliverNotification:notification];

	}
}

void Manager::Impl::clearAll() {
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	NSArray *notificationsList = [center deliveredNotifications];
	for (id notification in notificationsList) {
		NSDictionary *notificationUserInfo = [notification userInfo];
		NSNumber *launchIdObject = [notificationUserInfo objectForKey:@"launch"];
		auto notificationLaunchId = launchIdObject ? [launchIdObject unsignedLongLongValue] : 0ULL;
		if (notificationLaunchId == Global::LaunchId()) {
			[center removeDeliveredNotification:notification];
		}
	}
	[center removeAllDeliveredNotifications];
}

void Manager::Impl::clearFromHistory(History *history) {
	unsigned long long peerId = history->peer->id;

	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	NSArray *notificationsList = [center deliveredNotifications];
	for (id notification in notificationsList) {
		NSDictionary *notificationUserInfo = [notification userInfo];
		NSNumber *launchIdObject = [notificationUserInfo objectForKey:@"launch"];
		NSNumber *peerObject = [notificationUserInfo objectForKey:@"peer"];
		auto notificationLaunchId = launchIdObject ? [launchIdObject unsignedLongLongValue] : 0ULL;
		auto notificationPeerId = peerObject ? [peerObject unsignedLongLongValue] : 0ULL;
		if (notificationPeerId == peerId && notificationLaunchId == Global::LaunchId()) {
			[center removeDeliveredNotification:notification];
		}
	}
}

void Manager::Impl::updateDelegate() {
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	[center setDelegate:_delegate];
}

Manager::Impl::~Impl() {
	[_delegate release];
}

Manager::Manager() : _impl(std_::make_unique<Impl>()) {
}

void Manager::updateDelegate() {
	_impl->updateDelegate();
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
