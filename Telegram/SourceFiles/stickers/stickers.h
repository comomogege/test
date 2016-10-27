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

namespace Stickers {

void applyArchivedResult(const MTPDmessages_stickerSetInstallResultArchive &d);
void installLocally(uint64 setId);
void undoInstallLocally(uint64 setId);
void markFeaturedAsRead(uint64 setId);

namespace internal {

class FeaturedReader : public QObject {
	Q_OBJECT

public:
	FeaturedReader(QObject *parent);
	void scheduleRead(uint64 setId);

private slots:
	void onReadSets();

private:
	QTimer *_timer;
	OrderedSet<uint64> _setIds;

};

} // namespace internal
} // namespace Stickers
