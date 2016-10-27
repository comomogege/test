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

class AudioPlayerLoader {
public:
	AudioPlayerLoader(const FileLocation &file, const QByteArray &data);
	virtual ~AudioPlayerLoader();

	virtual bool check(const FileLocation &file, const QByteArray &data);

	virtual bool open(qint64 &position) = 0;
	virtual int64 duration() = 0;
	virtual int32 frequency() = 0;
	virtual int32 format() = 0;

	enum class ReadResult {
		Error,
		NotYet,
		Ok,
		Wait,
		EndOfFile,
	};
	virtual ReadResult readMore(QByteArray &samples, int64 &samplesCount) = 0;

	void saveDecodedSamples(QByteArray *samples, int64 *samplesCount);
	void takeSavedDecodedSamples(QByteArray *samples, int64 *samplesCount);
	bool holdsSavedDecodedSamples() const;

protected:
	FileLocation file;
	bool access = false;
	QByteArray data;

	QFile f;
	int32 dataPos = 0;

	bool openFile();

private:
	QByteArray _savedSamples;
	int64 _savedSamplesCount = 0;
	bool _holdsSavedSamples = false;

};
