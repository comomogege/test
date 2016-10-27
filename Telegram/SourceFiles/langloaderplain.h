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

#include "lang.h"

class LangLoaderRequest : public QMap <LangKey, bool> {
public:
	LangLoaderRequest() {
	}
	LangLoaderRequest(LangKey key1) {
		insert(key1, true);
	}
	LangLoaderRequest(LangKey key1, LangKey key2) {
		insert(key1, true);
		insert(key2, true);
	}
	LangLoaderRequest(LangKey key1, LangKey key2, LangKey key3) {
		insert(key1, true);
		insert(key2, true);
		insert(key3, true);
	}
};

using LangLoaderResult = QMap<LangKey, LangString>;
class LangLoaderPlain : public LangLoader {
public:
	LangLoaderPlain(const QString &file, const LangLoaderRequest &request = LangLoaderRequest());

	LangLoaderResult found() const {
		return result;
	}

protected:
	QString file;
	LangLoaderRequest request;

	bool readKeyValue(const char *&from, const char *end);

	bool readingAll;
	LangLoaderResult result;

};
