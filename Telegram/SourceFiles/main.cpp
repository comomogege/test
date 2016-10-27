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
#include "application.h"
#include "pspecific.h"

#include "localstorage.h"

int main(int argc, char *argv[]) {
#ifndef Q_OS_MAC // Retina display support is working fine, others are not.
	QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);
#endif // Q_OS_MAC
	QCoreApplication::setApplicationName(qsl("TelegramDesktop"));

	settingsParseArgs(argc, argv);
	if (cLaunchMode() == LaunchModeFixPrevious) {
		return psFixPrevious();
	} else if (cLaunchMode() == LaunchModeCleanup) {
		return psCleanup();
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	} else if (cLaunchMode() == LaunchModeShowCrash) {
		return showCrashReportWindow(QFileInfo(cStartUrl()).absoluteFilePath());
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
	}

	// both are finished in Application::closeApplication
	Logs::start(); // must be started before Platform is started
	Platform::start(); // must be started before QApplication is created

	int result = 0;
	{
		Application app(argc, argv);
		result = app.exec();
	}

	DEBUG_LOG(("Telegram finished, result: %1").arg(result));

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (cRestartingUpdate()) {
		DEBUG_LOG(("Application Info: executing updater to install update..."));
		psExecUpdater();
	} else
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	if (cRestarting()) {
		DEBUG_LOG(("Application Info: executing Telegram, because of restart..."));
		psExecTelegram();
	}

	SignalHandlers::finish();
	Platform::finish();
	Logs::finish();

	return result;
}
