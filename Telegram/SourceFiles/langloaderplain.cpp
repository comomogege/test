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
#include "langloaderplain.h"

namespace {

	bool skipWhitespaces(const char *&from, const char *end) {
		while (from < end && (*from == ' ' || *from == '\n' || *from == '\t' || *from == '\r')) {
			++from;
		}
		return (from < end);
	}

	bool skipComment(const char *&from, const char *end) {
		if (from >= end) return false;
		if (*from == '/') {
			if (from + 1 >= end) return true;
			if (*(from + 1) == '*') {
				from += 2;
				while (from + 1 < end && (*from != '*' || *(from + 1) != '/')) {
					++from;
				}
				from += 2;
				return (from < end);
			} else if (*(from + 1) == '/') {
				from += 2;
				while (from < end && *from != '\n' && *from != '\r') {
					++from;
				}
				return (from < end);
			} else {
				return true;
			}
		}
		return true;
	}

	bool skipJunk(const char *&from, const char *end) {
		const char *start;
		do {
			start = from;
			if (!skipWhitespaces(from, end)) return false;
            if (!skipComment(from, end)) throw Exception("Unexpected end of comment!");
		} while (start != from);
		return true;
	}
}

bool LangLoaderPlain::readKeyValue(const char *&from, const char *end) {
	if (!skipJunk(from, end)) return false;

	if (*from != '"') throw Exception(QString("Expected quote before key name!"));
	++from;
	const char *nameStart = from;
	while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9'))) {
		++from;
	}

	QByteArray varName = QByteArray(nameStart, from - nameStart);

	if (*from != '"') throw Exception(QString("Expected quote after key name '%1'!").arg(QLatin1String(varName)));
	++from;

	if (!skipJunk(from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (*from != '=') throw Exception(QString("'=' expected in key '%1'!").arg(QLatin1String(varName)));

	if (!skipJunk(++from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (*from != '"') throw Exception(QString("Expected string after '=' in key '%1'!").arg(QLatin1String(varName)));

	LangKey varKey = keyIndex(varName);
	bool feedingValue = request.isEmpty();
	if (feedingValue) {
		if (varKey == lngkeys_cnt) {
			warning(QString("Unknown key '%1'!").arg(QLatin1String(varName)));
		}
	} else if (!readingAll && !request.contains(varKey)) {
		varKey = lngkeys_cnt;
	}
	bool readingValue = (varKey != lngkeys_cnt);

	QByteArray varValue;
	QMap<ushort, bool> tagsUsed;
	const char *start = ++from;
	while (from < end && *from != '"') {
		if (*from == '\n') {
			throw Exception(QString("Unexpected end of string in key '%1'!").arg(QLatin1String(varName)));
		}
		if (*from == '\\') {
			if (from + 1 >= end) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
			if (*(from + 1) == '"' || *(from + 1) == '\\' || *(from + 1) == '{') {
				if (readingValue && from > start) varValue.append(start, from - start);
				start = ++from;
			} else if (*(from + 1) == 'n') {
				if (readingValue) {
					if (from > start) varValue.append(start, int(from - start));
					varValue.append('\n');
				}
				start = (++from) + 1;
			}
		} else if (readingValue && *from == '{') {
			if (from > start) varValue.append(start, int(from - start));

			const char *tagStart = ++from;
			while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9'))) {
				++from;
			}
			if (from == tagStart) {
				readingValue = false;
				warning(QString("Expected tag name in key '%1'!").arg(QLatin1String(varName)));
				continue;
			}
			QByteArray tagName = QByteArray(tagStart, int(from - tagStart));

			if (from == end || (*from != '}' && *from != ':')) throw Exception(QString("Expected '}' or ':' after tag name in key '%1'!").arg(QLatin1String(varName)));

			ushort index = tagIndex(tagName);
			if (index == lngtags_cnt) {
				readingValue = false;
				warning(QString("Tag '%1' not found in key '%2', not using value.").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
				continue;
			}

			if (!tagReplaced(varKey, index)) {
				readingValue = false;
				warning(QString("Unexpected tag '%1' in key '%2', not using value.").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
				continue;
			}
			if (tagsUsed.contains(index)) throw Exception(QString("Tag '%1' double used in key '%2'!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
			tagsUsed.insert(index, true);

			QString tagReplacer(4, TextCommand);
			tagReplacer[1] = TextCommandLangTag;
			tagReplacer[2] = QChar(0x0020 + index);
			varValue.append(tagReplacer.toUtf8());

			if (*from == ':') {
				start = ++from;

				QByteArray subvarValue;
				bool foundtag = false;
				int countedIndex = 0;
				while (from < end && *from != '"' && *from != '}') {
					if (*from == '|') {
						if (from > start) subvarValue.append(start, int(from - start));
						if (countedIndex >= lngtags_max_counted_values) throw Exception(QString("Too many values inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
						LangKey subkey = subkeyIndex(varKey, index, countedIndex++);
						if (subkey == lngkeys_cnt) {
							readingValue = false;
							warning(QString("Unexpected counted tag '%1' in key '%2', not using value.").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
							break;
						} else {
							if (feedingValue) {
								if (!feedKeyValue(subkey, QString::fromUtf8(subvarValue))) throw Exception(QString("Tag '%1' is not counted in key '%2'!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
							} else {
								foundKeyValue(subkey);
							}
						}
						subvarValue = QByteArray();
						foundtag = false;
						start = from + 1;
					}
					if (*from == '\n') {
						throw Exception(QString("Unexpected end of string inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
					}
					if (*from == '\\') {
						if (from + 1 >= end) throw Exception(QString("Unexpected end of file inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
						if (*(from + 1) == '"' || *(from + 1) == '\\' || *(from + 1) == '{' || *(from + 1) == '#') {
							if (from > start) subvarValue.append(start, int(from - start));
							start = ++from;
						} else if (*(from + 1) == 'n') {
							if (from > start) subvarValue.append(start, int(from - start));

							subvarValue.append('\n');

							start = (++from) + 1;
						}
					} else if (*from == '{') {
						throw Exception(QString("Unexpected tag inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
					} else if (*from == '#') {
						if (foundtag) throw Exception(QString("Replacement '#' double used inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
						foundtag = true;
						if (from > start) subvarValue.append(start, int(from - start));
						subvarValue.append(tagReplacer.toUtf8());
						start = from + 1;
					}
					++from;
				}
				if (!readingValue) continue;
				if (from >= end) throw Exception(QString("Unexpected end of file inside counted tag '%1' in '%2' key!").arg(QString::fromUtf8(tagName)).arg(QString::fromUtf8(varName)));
				if (*from == '"') throw Exception(QString("Unexpected end of string inside counted tag '%1' in '%2' key!").arg(QString::fromUtf8(tagName)).arg(QString::fromUtf8(varName)));

				if (from > start) subvarValue.append(start, int(from - start));
				if (countedIndex >= lngtags_max_counted_values) throw Exception(QString("Too many values inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));

				LangKey subkey = subkeyIndex(varKey, index, countedIndex++);
				if (subkey == lngkeys_cnt) {
					readingValue = false;
					warning(QString("Unexpected counted tag '%1' in key '%2', not using value.").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
					break;
				} else {
					if (feedingValue) {
						if (!feedKeyValue(subkey, QString::fromUtf8(subvarValue))) throw Exception(QString("Tag '%1' is not counted in key '%2'!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
					} else {
						foundKeyValue(subkey);
					}
				}
			}
			start = from + 1;
		}
		++from;
	}
	if (from >= end) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (readingValue && from > start) varValue.append(start, from - start);

	if (!skipJunk(++from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (*from != ';') throw Exception(QString("';' expected after \"value\" in key '%1'!").arg(QLatin1String(varName)));

	skipJunk(++from, end);

	if (readingValue) {
		if (feedingValue) {
			if (!feedKeyValue(varKey, QString::fromUtf8(varValue))) throw Exception(QString("Could not write value in key '%1'!").arg(QLatin1String(varName)));
		} else {
			foundKeyValue(varKey);
			result.insert(varKey, QString::fromUtf8(varValue));
		}
	}

	return true;
}

LangLoaderPlain::LangLoaderPlain(const QString &file, const LangLoaderRequest &request) : file(file), request(request), readingAll(request.contains(lngkeys_cnt)) {
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly)) {
		error(qsl("Could not open input file!"));
		return;
	}
	if (f.size() > 1024 * 1024) {
		error(qsl("Too big file: %1").arg(f.size()));
		return;
	}
	QByteArray checkCodec = f.read(3);
	if (checkCodec.size() < 3) {
		error(qsl("Bad lang input file: %1").arg(file));
		return;
	}
	f.seek(0);

	QByteArray data;
	int skip = 0;
	if ((checkCodec.at(0) == '\xFF' && checkCodec.at(1) == '\xFE') || (checkCodec.at(0) == '\xFE' && checkCodec.at(1) == '\xFF') || (checkCodec.at(1) == 0)) {
		QTextStream stream(&f);
		stream.setCodec("UTF-16");

		QString string = stream.readAll();
		if (stream.status() != QTextStream::Ok) {
			error(qsl("Could not read valid UTF-16 file: % 1").arg(file));
			return;
		}
		f.close();

		data = string.toUtf8();
	} else if (checkCodec.at(0) == 0) {
		QByteArray tmp = "\xFE\xFF" + f.readAll(); // add fake UTF-16 BOM
		f.close();

		QTextStream stream(&tmp);
		stream.setCodec("UTF-16");
		QString string = stream.readAll();
		if (stream.status() != QTextStream::Ok) {
			error(qsl("Could not read valid UTF-16 file: % 1").arg(file));
			return;
		}

		data = string.toUtf8();
	} else {
		data = f.readAll();
		if (checkCodec.at(0) == '\xEF' && checkCodec.at(1) == '\xBB' && checkCodec.at(2) == '\xBF') {
			skip = 3; // skip UTF-8 BOM
		}
	}

	const char *text = data.constData() + skip, *end = text + data.size() - skip;
	try {
		while (true) {
			if (!readKeyValue(text, end)) {
				break;
			}
		}
	} catch (std::exception &e) {
		error(QString::fromUtf8(e.what()));
		return;
	}
}
