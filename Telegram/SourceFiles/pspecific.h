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

#include <QtWidgets/QMainWindow>
#include <QtNetwork/QNetworkReply>
#include "sysbuttons.h"

#ifdef Q_OS_MAC
#include "pspecific_mac.h"
#elif defined Q_OS_LINUX // Q_OS_MAC
#include "pspecific_linux.h"
#elif defined Q_OS_WINRT // Q_OS_MAC || Q_OS_LINUX
#include "pspecific_winrt.h"
#elif defined Q_OS_WIN // Q_OS_MAC || Q_OS_LINUX || Q_OS_WINRT
#include "pspecific_win.h"
#endif // Q_OS_MAC || Q_OS_LINUX || Q_OS_WINRT || Q_OS_WIN

namespace Platform {

void start();
void finish();

void SetWatchingMediaKeys(bool watching);

namespace ThirdParty {

void start();
void finish();

} // namespace ThirdParty
} // namespace Platform
