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
#include "localstorage.h"

#include <openssl/evp.h>

#include "serialize/serialize_document.h"
#include "serialize/serialize_common.h"
#include "data/data_drafts.h"
#include "window/chat_background.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "lang.h"
#include "media/media_audio.h"
#include "application.h"
#include "apiwrap.h"

namespace Local {
namespace {

using FileKey = quint64;

static const char tdfMagic[] = { 'T', 'D', 'F', '$' };
static const int32 tdfMagicLen = sizeof(tdfMagic);

QString toFilePart(FileKey val) {
	QString result;
	result.reserve(0x10);
	for (int32 i = 0; i < 0x10; ++i) {
		uchar v = (val & 0x0F);
		result.push_back((v < 0x0A) ? ('0' + v) : ('A' + (v - 0x0A)));
		val >>= 4;
	}
	return result;
}

QString _basePath, _userBasePath;

bool _started = false;
internal::Manager *_manager = nullptr;
TaskQueue *_localLoader = nullptr;

bool _working() {
	return _manager && !_basePath.isEmpty();
}

bool _userWorking() {
	return _manager && !_basePath.isEmpty() && !_userBasePath.isEmpty();
}

enum FileOptions {
	UserPath = 0x01,
	SafePath = 0x02,
};

bool keyAlreadyUsed(QString &name, int options = UserPath | SafePath) {
	name += '0';
	if (QFileInfo(name).exists()) return true;
	if (options & SafePath) {
		name[name.size() - 1] = '1';
		return QFileInfo(name).exists();
	}
	return false;
}

FileKey genKey(int options = UserPath | SafePath) {
	if (options & UserPath) {
		if (!_userWorking()) return 0;
	} else {
		if (!_working()) return 0;
	}

	FileKey result;
	QString base = (options & UserPath) ? _userBasePath : _basePath, path;
	path.reserve(base.size() + 0x11);
	path += base;
	do {
		result = rand_value<FileKey>();
		path.resize(base.size());
		path += toFilePart(result);
	} while (!result || keyAlreadyUsed(path, options));

	return result;
}

void clearKey(const FileKey &key, int options = UserPath | SafePath) {
	if (options & UserPath) {
		if (!_userWorking()) return;
	} else {
		if (!_working()) return;
	}

	QString base = (options & UserPath) ? _userBasePath : _basePath, name;
	name.reserve(base.size() + 0x11);
	name.append(base).append(toFilePart(key)).append('0');
	QFile::remove(name);
	if (options & SafePath) {
		name[name.size() - 1] = '1';
		QFile::remove(name);
	}
}

bool _checkStreamStatus(QDataStream &stream) {
	if (stream.status() != QDataStream::Ok) {
		LOG(("Bad data stream status: %1").arg(stream.status()));
		return false;
	}
	return true;
}

QByteArray _settingsSalt, _passKeySalt, _passKeyEncrypted;

MTP::AuthKey _oldKey, _settingsKey, _passKey, _localKey;
void createLocalKey(const QByteArray &pass, QByteArray *salt, MTP::AuthKey *result) {
	uchar key[LocalEncryptKeySize] = { 0 };
	int32 iterCount = pass.size() ? LocalEncryptIterCount : LocalEncryptNoPwdIterCount; // dont slow down for no password
	QByteArray newSalt;
	if (!salt) {
		newSalt.resize(LocalEncryptSaltSize);
		memset_rand(newSalt.data(), newSalt.size());
		salt = &newSalt;

		cSetLocalSalt(newSalt);
	}

	PKCS5_PBKDF2_HMAC_SHA1(pass.constData(), pass.size(), (uchar*)salt->data(), salt->size(), iterCount, LocalEncryptKeySize, key);

	result->setKey(key);
}

struct FileReadDescriptor {
	FileReadDescriptor() : version(0) {
	}
	int32 version;
	QByteArray data;
	QBuffer buffer;
	QDataStream stream;
	~FileReadDescriptor() {
		if (version) {
			stream.setDevice(0);
			if (buffer.isOpen()) buffer.close();
			buffer.setBuffer(0);
		}
	}
};

struct EncryptedDescriptor {
	EncryptedDescriptor() {
	}
	EncryptedDescriptor(uint32 size) {
		uint32 fullSize = sizeof(uint32) + size;
		if (fullSize & 0x0F) fullSize += 0x10 - (fullSize & 0x0F);
		data.reserve(fullSize);

		data.resize(sizeof(uint32));
		buffer.setBuffer(&data);
		buffer.open(QIODevice::WriteOnly);
		buffer.seek(sizeof(uint32));
		stream.setDevice(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);
	}
	QByteArray data;
	QBuffer buffer;
	QDataStream stream;
	void finish() {
		if (stream.device()) stream.setDevice(0);
		if (buffer.isOpen()) buffer.close();
		buffer.setBuffer(0);
	}
	~EncryptedDescriptor() {
		finish();
	}
};

struct FileWriteDescriptor {
	FileWriteDescriptor(const FileKey &key, int options = UserPath | SafePath) : dataSize(0) {
		init(toFilePart(key), options);
	}
	FileWriteDescriptor(const QString &name, int options = UserPath | SafePath) : dataSize(0) {
		init(name, options);
	}
	void init(const QString &name, int options) {
		if (options & UserPath) {
			if (!_userWorking()) return;
		} else {
			if (!_working()) return;
		}

		// detect order of read attempts and file version
		QString toTry[2];
		toTry[0] = ((options & UserPath) ? _userBasePath : _basePath) + name + '0';
		if (options & SafePath) {
			toTry[1] = ((options & UserPath) ? _userBasePath : _basePath) + name + '1';
			QFileInfo toTry0(toTry[0]);
			QFileInfo toTry1(toTry[1]);
			if (toTry0.exists()) {
				if (toTry1.exists()) {
					QDateTime mod0 = toTry0.lastModified(), mod1 = toTry1.lastModified();
					if (mod0 > mod1) {
						qSwap(toTry[0], toTry[1]);
					}
				} else {
					qSwap(toTry[0], toTry[1]);
				}
				toDelete = toTry[1];
			} else if (toTry1.exists()) {
				toDelete = toTry[1];
			}
		}

		file.setFileName(toTry[0]);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(tdfMagic, tdfMagicLen);
			qint32 version = AppVersion;
			file.write((const char*)&version, sizeof(version));

			stream.setDevice(&file);
			stream.setVersion(QDataStream::Qt_5_1);
		}
	}
	bool writeData(const QByteArray &data) {
		if (!file.isOpen()) return false;

		stream << data;
		quint32 len = data.isNull() ? 0xffffffff : data.size();
		if (QSysInfo::ByteOrder != QSysInfo::BigEndian) {
			len = qbswap(len);
		}
		md5.feed(&len, sizeof(len));
		md5.feed(data.constData(), data.size());
		dataSize += sizeof(len) + data.size();

		return true;
	}
	static QByteArray prepareEncrypted(EncryptedDescriptor &data, const MTP::AuthKey &key = _localKey) {
		data.finish();
		QByteArray &toEncrypt(data.data);

		// prepare for encryption
		uint32 size = toEncrypt.size(), fullSize = size;
		if (fullSize & 0x0F) {
			fullSize += 0x10 - (fullSize & 0x0F);
			toEncrypt.resize(fullSize);
			memset_rand(toEncrypt.data() + size, fullSize - size);
		}
		*(uint32*)toEncrypt.data() = size;
		QByteArray encrypted(0x10 + fullSize, Qt::Uninitialized); // 128bit of sha1 - key128, sizeof(data), data
		hashSha1(toEncrypt.constData(), toEncrypt.size(), encrypted.data());
		MTP::aesEncryptLocal(toEncrypt.constData(), encrypted.data() + 0x10, fullSize, &key, encrypted.constData());

		return encrypted;
	}
	bool writeEncrypted(EncryptedDescriptor &data, const MTP::AuthKey &key = _localKey) {
		return writeData(prepareEncrypted(data, key));
	}
	void finish() {
		if (!file.isOpen()) return;

		stream.setDevice(0);

		md5.feed(&dataSize, sizeof(dataSize));
		qint32 version = AppVersion;
		md5.feed(&version, sizeof(version));
		md5.feed(tdfMagic, tdfMagicLen);
		file.write((const char*)md5.result(), 0x10);
		file.close();

		if (!toDelete.isEmpty()) {
			QFile::remove(toDelete);
		}
	}
	QFile file;
	QDataStream stream;

	QString toDelete;

	HashMd5 md5;
	int32 dataSize;

	~FileWriteDescriptor() {
		finish();
	}
};

bool readFile(FileReadDescriptor &result, const QString &name, int options = UserPath | SafePath) {
	if (options & UserPath) {
		if (!_userWorking()) return false;
	} else {
		if (!_working()) return false;
	}

	// detect order of read attempts
	QString toTry[2];
	toTry[0] = ((options & UserPath) ? _userBasePath : _basePath) + name + '0';
	if (options & SafePath) {
		QFileInfo toTry0(toTry[0]);
		if (toTry0.exists()) {
			toTry[1] = ((options & UserPath) ? _userBasePath : _basePath) + name + '1';
			QFileInfo toTry1(toTry[1]);
			if (toTry1.exists()) {
				QDateTime mod0 = toTry0.lastModified(), mod1 = toTry1.lastModified();
				if (mod0 < mod1) {
					qSwap(toTry[0], toTry[1]);
				}
			} else {
				toTry[1] = QString();
			}
		} else {
			toTry[0][toTry[0].size() - 1] = '1';
		}
	}
	for (int32 i = 0; i < 2; ++i) {
		QString fname(toTry[i]);
		if (fname.isEmpty()) break;

		QFile f(fname);
		if (!f.open(QIODevice::ReadOnly)) {
			DEBUG_LOG(("App Info: failed to open '%1' for reading").arg(name));
			continue;
		}

		// check magic
		char magic[tdfMagicLen];
		if (f.read(magic, tdfMagicLen) != tdfMagicLen) {
			DEBUG_LOG(("App Info: failed to read magic from '%1'").arg(name));
			continue;
		}
		if (memcmp(magic, tdfMagic, tdfMagicLen)) {
			DEBUG_LOG(("App Info: bad magic %1 in '%2'").arg(Logs::mb(magic, tdfMagicLen).str()).arg(name));
			continue;
		}

		// read app version
		qint32 version;
		if (f.read((char*)&version, sizeof(version)) != sizeof(version)) {
			DEBUG_LOG(("App Info: failed to read version from '%1'").arg(name));
			continue;
		}
		if (version > AppVersion) {
			DEBUG_LOG(("App Info: version too big %1 for '%2', my version %3").arg(version).arg(name).arg(AppVersion));
			continue;
		}

		// read data
		QByteArray bytes = f.read(f.size());
		int32 dataSize = bytes.size() - 16;
		if (dataSize < 0) {
			DEBUG_LOG(("App Info: bad file '%1', could not read sign part").arg(name));
			continue;
		}

		// check signature
		HashMd5 md5;
		md5.feed(bytes.constData(), dataSize);
		md5.feed(&dataSize, sizeof(dataSize));
		md5.feed(&version, sizeof(version));
		md5.feed(magic, tdfMagicLen);
		if (memcmp(md5.result(), bytes.constData() + dataSize, 16)) {
			DEBUG_LOG(("App Info: bad file '%1', signature did not match").arg(name));
			continue;
		}

		bytes.resize(dataSize);
		result.data = bytes;
		bytes = QByteArray();

		result.version = version;
		result.buffer.setBuffer(&result.data);
		result.buffer.open(QIODevice::ReadOnly);
		result.stream.setDevice(&result.buffer);
		result.stream.setVersion(QDataStream::Qt_5_1);

		if ((i == 0 && !toTry[1].isEmpty()) || i == 1) {
			QFile::remove(toTry[1 - i]);
		}

		return true;
	}
	return false;
}

bool decryptLocal(EncryptedDescriptor &result, const QByteArray &encrypted, const MTP::AuthKey &key = _localKey) {
	if (encrypted.size() <= 16 || (encrypted.size() & 0x0F)) {
		LOG(("App Error: bad encrypted part size: %1").arg(encrypted.size()));
		return false;
	}
	uint32 fullLen = encrypted.size() - 16;

	QByteArray decrypted;
	decrypted.resize(fullLen);
	const char *encryptedKey = encrypted.constData(), *encryptedData = encrypted.constData() + 16;
	aesDecryptLocal(encryptedData, decrypted.data(), fullLen, &key, encryptedKey);
	uchar sha1Buffer[20];
	if (memcmp(hashSha1(decrypted.constData(), decrypted.size(), sha1Buffer), encryptedKey, 16)) {
		LOG(("App Info: bad decrypt key, data not decrypted - incorrect password?"));
		return false;
	}

	uint32 dataLen = *(const uint32*)decrypted.constData();
	if (dataLen > uint32(decrypted.size()) || dataLen <= fullLen - 16 || dataLen < sizeof(uint32)) {
		LOG(("App Error: bad decrypted part size: %1, fullLen: %2, decrypted size: %3").arg(dataLen).arg(fullLen).arg(decrypted.size()));
		return false;
	}

	decrypted.resize(dataLen);
	result.data = decrypted;
	decrypted = QByteArray();

	result.buffer.setBuffer(&result.data);
	result.buffer.open(QIODevice::ReadOnly);
	result.buffer.seek(sizeof(uint32)); // skip len
	result.stream.setDevice(&result.buffer);
	result.stream.setVersion(QDataStream::Qt_5_1);

	return true;
}

bool readEncryptedFile(FileReadDescriptor &result, const QString &name, int options = UserPath | SafePath, const MTP::AuthKey &key = _localKey) {
	if (!readFile(result, name, options)) {
		return false;
	}
	QByteArray encrypted;
	result.stream >> encrypted;

	EncryptedDescriptor data;
	if (!decryptLocal(data, encrypted, key)) {
		result.stream.setDevice(0);
		if (result.buffer.isOpen()) result.buffer.close();
		result.buffer.setBuffer(0);
		result.data = QByteArray();
		result.version = 0;
		return false;
	}

	result.stream.setDevice(0);
	if (result.buffer.isOpen()) result.buffer.close();
	result.buffer.setBuffer(0);
	result.data = data.data;
	result.buffer.setBuffer(&result.data);
	result.buffer.open(QIODevice::ReadOnly);
	result.buffer.seek(data.buffer.pos());
	result.stream.setDevice(&result.buffer);
	result.stream.setVersion(QDataStream::Qt_5_1);

	return true;
}

bool readEncryptedFile(FileReadDescriptor &result, const FileKey &fkey, int options = UserPath | SafePath, const MTP::AuthKey &key = _localKey) {
	return readEncryptedFile(result, toFilePart(fkey), options, key);
}

FileKey _dataNameKey = 0;

enum { // Local Storage Keys
	lskUserMap = 0x00,
	lskDraft = 0x01, // data: PeerId peer
	lskDraftPosition = 0x02, // data: PeerId peer
	lskImages = 0x03, // data: StorageKey location
	lskLocations = 0x04, // no data
	lskStickerImages = 0x05, // data: StorageKey location
	lskAudios = 0x06, // data: StorageKey location
	lskRecentStickersOld = 0x07, // no data
	lskBackground = 0x08, // no data
	lskUserSettings = 0x09, // no data
	lskRecentHashtagsAndBots = 0x0a, // no data
	lskStickersOld = 0x0b, // no data
	lskSavedPeers = 0x0c, // no data
	lskReportSpamStatuses = 0x0d, // no data
	lskSavedGifsOld = 0x0e, // no data
	lskSavedGifs = 0x0f, // no data
	lskStickersKeys = 0x10, // no data
	lskTrustedBots = 0x11, // no data
};

enum {
	dbiKey = 0x00,
	dbiUser = 0x01,
	dbiDcOptionOld = 0x02,
	dbiChatSizeMax = 0x03,
	dbiMutePeer = 0x04,
	dbiSendKey = 0x05,
	dbiAutoStart = 0x06,
	dbiStartMinimized = 0x07,
	dbiSoundNotify = 0x08,
	dbiWorkMode = 0x09,
	dbiSeenTrayTooltip = 0x0a,
	dbiDesktopNotify = 0x0b,
	dbiAutoUpdate = 0x0c,
	dbiLastUpdateCheck = 0x0d,
	dbiWindowPosition = 0x0e,
	dbiConnectionType = 0x0f,
	// 0x10 reserved
	dbiDefaultAttach = 0x11,
	dbiCatsAndDogs = 0x12,
	dbiReplaceEmojis = 0x13,
	dbiAskDownloadPath = 0x14,
	dbiDownloadPathOld = 0x15,
	dbiScale = 0x16,
	dbiEmojiTabOld = 0x17,
	dbiRecentEmojisOld = 0x18,
	dbiLoggedPhoneNumber = 0x19,
	dbiMutedPeers = 0x1a,
	// 0x1b reserved
	dbiNotifyView = 0x1c,
	dbiSendToMenu = 0x1d,
	dbiCompressPastedImage = 0x1e,
	dbiLang = 0x1f,
	dbiLangFile = 0x20,
	dbiTileBackground = 0x21,
	dbiAutoLock = 0x22,
	dbiDialogLastPath = 0x23,
	dbiRecentEmojis = 0x24,
	dbiEmojiVariants = 0x25,
	dbiRecentStickers = 0x26,
	dbiDcOption = 0x27,
	dbiTryIPv6 = 0x28,
	dbiSongVolume = 0x29,
	dbiWindowsNotificationsOld = 0x30,
	dbiIncludeMuted = 0x31,
	dbiMegagroupSizeMax = 0x32,
	dbiDownloadPath = 0x33,
	dbiAutoDownload = 0x34,
	dbiSavedGifsLimit = 0x35,
	dbiShowingSavedGifs = 0x36,
	dbiAutoPlay = 0x37,
	dbiAdaptiveForWide = 0x38,
	dbiHiddenPinnedMessages = 0x39,
	dbiDialogsMode = 0x40,
	dbiModerateMode = 0x41,
	dbiVideoVolume = 0x42,
	dbiStickersRecentLimit = 0x43,
	dbiNativeNotifications = 0x44,
	dbiNotificationsCount  = 0x45,
	dbiNotificationsCorner = 0x46,

	dbiEncryptedWithSalt = 333,
	dbiEncrypted = 444,

	// 500-600 reserved

	dbiVersion = 666,
};


typedef QMap<PeerId, FileKey> DraftsMap;
DraftsMap _draftsMap, _draftCursorsMap;
typedef QMap<PeerId, bool> DraftsNotReadMap;
DraftsNotReadMap _draftsNotReadMap;

typedef QPair<FileKey, qint32> FileDesc; // file, size

typedef QMultiMap<MediaKey, FileLocation> FileLocations;
FileLocations _fileLocations;
typedef QPair<MediaKey, FileLocation> FileLocationPair;
typedef QMap<QString, FileLocationPair> FileLocationPairs;
FileLocationPairs _fileLocationPairs;
typedef QMap<MediaKey, MediaKey> FileLocationAliases;
FileLocationAliases _fileLocationAliases;
typedef QMap<QString, FileDesc> WebFilesMap;
WebFilesMap _webFilesMap;
uint64 _storageWebFilesSize = 0;
FileKey _locationsKey = 0, _reportSpamStatusesKey = 0, _trustedBotsKey = 0;

using TrustedBots = OrderedSet<uint64>;
TrustedBots _trustedBots;
bool _trustedBotsRead = false;

FileKey _recentStickersKeyOld = 0;
FileKey _installedStickersKey = 0, _featuredStickersKey = 0, _recentStickersKey = 0, _archivedStickersKey = 0;
FileKey _savedGifsKey = 0;

FileKey _backgroundKey = 0;
bool _backgroundWasRead = false;

bool _readingUserSettings = false;
FileKey _userSettingsKey = 0;
FileKey _recentHashtagsAndBotsKey = 0;
bool _recentHashtagsAndBotsWereRead = false;

FileKey _savedPeersKey = 0;

typedef QMap<StorageKey, FileDesc> StorageMap;
StorageMap _imagesMap, _stickerImagesMap, _audiosMap;
int32 _storageImagesSize = 0, _storageStickersSize = 0, _storageAudiosSize = 0;

bool _mapChanged = false;
int32 _oldMapVersion = 0, _oldSettingsVersion = 0;

enum WriteMapWhen {
	WriteMapNow,
	WriteMapFast,
	WriteMapSoon,
};

void _writeMap(WriteMapWhen when = WriteMapSoon);

void _writeLocations(WriteMapWhen when = WriteMapSoon) {
	if (when != WriteMapNow) {
		_manager->writeLocations(when == WriteMapFast);
		return;
	}
	if (!_working()) return;

	_manager->writingLocations();
	if (_fileLocations.isEmpty() && _webFilesMap.isEmpty()) {
		if (_locationsKey) {
			clearKey(_locationsKey);
			_locationsKey = 0;
			_mapChanged = true;
			_writeMap();
		}
	} else {
		if (!_locationsKey) {
			_locationsKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapFast);
		}
		quint32 size = 0;
		for (FileLocations::const_iterator i = _fileLocations.cbegin(), e = _fileLocations.cend(); i != e; ++i) {
			// location + type + namelen + name
			size += sizeof(quint64) * 2 + sizeof(quint32) + Serialize::stringSize(i.value().name());
			if (AppVersion > 9013) {
				// bookmark
				size += Serialize::bytearraySize(i.value().bookmark());
			}
			// date + size
			size += Serialize::dateTimeSize() + sizeof(quint32);
		}

		//end mark
		size += sizeof(quint64) * 2 + sizeof(quint32) + Serialize::stringSize(QString());
		if (AppVersion > 9013) {
			size += Serialize::bytearraySize(QByteArray());
		}
		size += Serialize::dateTimeSize() + sizeof(quint32);

		size += sizeof(quint32); // aliases count
		for (FileLocationAliases::const_iterator i = _fileLocationAliases.cbegin(), e = _fileLocationAliases.cend(); i != e; ++i) {
			// alias + location
			size += sizeof(quint64) * 2 + sizeof(quint64) * 2;
		}

		size += sizeof(quint32); // web files count
		for (WebFilesMap::const_iterator i = _webFilesMap.cbegin(), e = _webFilesMap.cend(); i != e; ++i) {
			// url + filekey + size
			size += Serialize::stringSize(i.key()) + sizeof(quint64) + sizeof(qint32);
		}

		EncryptedDescriptor data(size);
		for (FileLocations::const_iterator i = _fileLocations.cbegin(); i != _fileLocations.cend(); ++i) {
			data.stream << quint64(i.key().first) << quint64(i.key().second) << quint32(i.value().type) << i.value().name();
			if (AppVersion > 9013) {
				data.stream << i.value().bookmark();
			}
			data.stream << i.value().modified << quint32(i.value().size);
		}

		data.stream << quint64(0) << quint64(0) << quint32(0) << QString();
		if (AppVersion > 9013) {
			data.stream << QByteArray();
		}
		data.stream << QDateTime::currentDateTime() << quint32(0);

		data.stream << quint32(_fileLocationAliases.size());
		for (FileLocationAliases::const_iterator i = _fileLocationAliases.cbegin(), e = _fileLocationAliases.cend(); i != e; ++i) {
			data.stream << quint64(i.key().first) << quint64(i.key().second) << quint64(i.value().first) << quint64(i.value().second);
		}

		data.stream << quint32(_webFilesMap.size());
		for (WebFilesMap::const_iterator i = _webFilesMap.cbegin(), e = _webFilesMap.cend(); i != e; ++i) {
			data.stream << i.key() << quint64(i.value().first) << qint32(i.value().second);
		}

		FileWriteDescriptor file(_locationsKey);
		file.writeEncrypted(data);
	}
}

void _readLocations() {
	FileReadDescriptor locations;
	if (!readEncryptedFile(locations, _locationsKey)) {
		clearKey(_locationsKey);
		_locationsKey = 0;
		_writeMap();
		return;
	}

	bool endMarkFound = false;
	while (!locations.stream.atEnd()) {
		quint64 first, second;
		QByteArray bookmark;
		FileLocation loc;
		quint32 type;
		locations.stream >> first >> second >> type >> loc.fname;
		if (locations.version > 9013) {
			locations.stream >> bookmark;
		}
		locations.stream >> loc.modified >> loc.size;
		loc.setBookmark(bookmark);

		if (!first && !second && !type && loc.fname.isEmpty() && !loc.size) { // end mark
			endMarkFound = true;
			break;
		}

		MediaKey key(first, second);
		loc.type = StorageFileType(type);

		_fileLocations.insert(key, loc);
		_fileLocationPairs.insert(loc.fname, FileLocationPair(key, loc));
	}

	if (endMarkFound) {
		quint32 cnt;
		locations.stream >> cnt;
		for (quint32 i = 0; i < cnt; ++i) {
			quint64 kfirst, ksecond, vfirst, vsecond;
			locations.stream >> kfirst >> ksecond >> vfirst >> vsecond;
			_fileLocationAliases.insert(MediaKey(kfirst, ksecond), MediaKey(vfirst, vsecond));
		}

		if (!locations.stream.atEnd()) {
			_storageWebFilesSize = 0;
			_webFilesMap.clear();

			quint32 webLocationsCount;
			locations.stream >> webLocationsCount;
			for (quint32 i = 0; i < webLocationsCount; ++i) {
				QString url;
				quint64 key;
				qint32 size;
				locations.stream >> url >> key >> size;
				_webFilesMap.insert(url, FileDesc(key, size));
				_storageWebFilesSize += size;
			}
		}
	}
}

void _writeReportSpamStatuses() {
	if (!_working()) return;

	if (cReportSpamStatuses().isEmpty()) {
		if (_reportSpamStatusesKey) {
			clearKey(_reportSpamStatusesKey);
			_reportSpamStatusesKey = 0;
			_mapChanged = true;
			_writeMap();
		}
	} else {
		if (!_reportSpamStatusesKey) {
			_reportSpamStatusesKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapFast);
		}
		const ReportSpamStatuses &statuses(cReportSpamStatuses());

		quint32 size = sizeof(qint32);
		for (ReportSpamStatuses::const_iterator i = statuses.cbegin(), e = statuses.cend(); i != e; ++i) {
			// peer + status
			size += sizeof(quint64) + sizeof(qint32);
		}

		EncryptedDescriptor data(size);
		data.stream << qint32(statuses.size());
		for (ReportSpamStatuses::const_iterator i = statuses.cbegin(), e = statuses.cend(); i != e; ++i) {
			data.stream << quint64(i.key()) << qint32(i.value());
		}

		FileWriteDescriptor file(_reportSpamStatusesKey);
		file.writeEncrypted(data);
	}
}

void _readReportSpamStatuses() {
	FileReadDescriptor statuses;
	if (!readEncryptedFile(statuses, _reportSpamStatusesKey)) {
		clearKey(_reportSpamStatusesKey);
		_reportSpamStatusesKey = 0;
		_writeMap();
		return;
	}

	ReportSpamStatuses &map(cRefReportSpamStatuses());
	map.clear();

	qint32 size = 0;
	statuses.stream >> size;
	for (int32 i = 0; i < size; ++i) {
		quint64 peer = 0;
		qint32 status = 0;
		statuses.stream >> peer >> status;
		map.insert(peer, DBIPeerReportSpamStatus(status));
	}
}

MTP::DcOptions *_dcOpts = 0;
bool _readSetting(quint32 blockId, QDataStream &stream, int version) {
	switch (blockId) {
	case dbiDcOptionOld: {
		quint32 dcId, port;
		QString host, ip;
		stream >> dcId >> host >> ip >> port;
		if (!_checkStreamStatus(stream)) return false;

		if (_dcOpts) _dcOpts->insert(dcId, MTP::DcOption(dcId, 0, ip.toUtf8().constData(), port));
	} break;

	case dbiDcOption: {
		quint32 dcIdWithShift, port;
		qint32 flags;
		QString ip;
		stream >> dcIdWithShift >> flags >> ip >> port;
		if (!_checkStreamStatus(stream)) return false;

		if (_dcOpts) _dcOpts->insert(dcIdWithShift, MTP::DcOption(MTP::bareDcId(dcIdWithShift), MTPDdcOption::Flags(flags), ip.toUtf8().constData(), port));
	} break;

	case dbiChatSizeMax: {
		qint32 maxSize;
		stream >> maxSize;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetChatSizeMax(maxSize);
	} break;

	case dbiSavedGifsLimit: {
		qint32 limit;
		stream >> limit;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetSavedGifsLimit(limit);
	} break;

	case dbiStickersRecentLimit: {
		qint32 limit;
		stream >> limit;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetStickersRecentLimit(limit);
	} break;

	case dbiMegagroupSizeMax: {
		qint32 maxSize;
		stream >> maxSize;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetMegagroupSizeMax(maxSize);
	} break;

	case dbiUser: {
		quint32 dcId;
		qint32 uid;
		stream >> uid >> dcId;
		if (!_checkStreamStatus(stream)) return false;

		DEBUG_LOG(("MTP Info: user found, dc %1, uid %2").arg(dcId).arg(uid));
		MTP::configure(dcId, uid);
	} break;

	case dbiKey: {
		qint32 dcId;
		quint32 key[64];
		stream >> dcId;
		stream.readRawData((char*)key, 256);
		if (!_checkStreamStatus(stream)) return false;

		DEBUG_LOG(("MTP Info: key found, dc %1, key: %2").arg(dcId).arg(Logs::mb(key, 256).str()));
		dcId = MTP::bareDcId(dcId);
		MTP::AuthKeyPtr keyPtr(new MTP::AuthKey());
		keyPtr->setKey(key);
		keyPtr->setDC(dcId);

		MTP::setKey(dcId, keyPtr);
	} break;

	case dbiAutoStart: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetAutoStart(v == 1);
	} break;

	case dbiStartMinimized: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetStartMinimized(v == 1);
	} break;

	case dbiSendToMenu: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetSendToMenu(v == 1);
	} break;

	case dbiSoundNotify: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetSoundNotify(v == 1);
	} break;

	case dbiAutoDownload: {
		qint32 photo, audio, gif;
		stream >> photo >> audio >> gif;
		if (!_checkStreamStatus(stream)) return false;

		cSetAutoDownloadPhoto(photo);
		cSetAutoDownloadAudio(audio);
		cSetAutoDownloadGif(gif);
	} break;

	case dbiAutoPlay: {
		qint32 gif;
		stream >> gif;
		if (!_checkStreamStatus(stream)) return false;

		cSetAutoPlayGif(gif == 1);
	} break;

	case dbiDialogsMode: {
		qint32 enabled, modeInt;
		stream >> enabled >> modeInt;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetDialogsModeEnabled(enabled == 1);
		Dialogs::Mode mode = Dialogs::Mode::All;
		if (enabled) {
			mode = static_cast<Dialogs::Mode>(modeInt);
			if (mode != Dialogs::Mode::All && mode != Dialogs::Mode::Important) {
				mode = Dialogs::Mode::All;
			}
		}
		Global::SetDialogsMode(mode);
	} break;

	case dbiModerateMode: {
		qint32 enabled;
		stream >> enabled;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetModerateModeEnabled(enabled == 1);
	} break;

	case dbiIncludeMuted: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetIncludeMuted(v == 1);
	} break;

	case dbiShowingSavedGifs: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetShowingSavedGifs(v == 1);
	} break;

	case dbiDesktopNotify: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetDesktopNotify(v == 1);
		if (App::wnd()) App::wnd()->updateTrayMenu();
	} break;

	case dbiWindowsNotificationsOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiNativeNotifications: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetNativeNotifications(v == 1);
	} break;

	case dbiNotificationsCount: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetNotificationsCount((v > 0 ? v : 3));
	} break;

	case dbiNotificationsCorner: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetNotificationsCorner(static_cast<Notify::ScreenCorner>((v >= 0 && v < 4) ? v : 2));
	} break;

	case dbiWorkMode: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		switch (v) {
		case dbiwmTrayOnly: cSetWorkMode(dbiwmTrayOnly); break;
		case dbiwmWindowOnly: cSetWorkMode(dbiwmWindowOnly); break;
		default: cSetWorkMode(dbiwmWindowAndTray); break;
		};
	} break;

	case dbiConnectionType: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		switch (v) {
		case dbictHttpProxy:
		case dbictTcpProxy: {
			ProxyData p;
			qint32 port;
			stream >> p.host >> port >> p.user >> p.password;
			if (!_checkStreamStatus(stream)) return false;

			p.port = uint32(port);
			Global::SetConnectionProxy(p);
			Global::SetConnectionType(DBIConnectionType(v));
		} break;
		case dbictHttpAuto:
		default: Global::SetConnectionType(dbictAuto); break;
		};
	} break;

	case dbiTryIPv6: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetTryIPv6(v == 1);
	} break;

	case dbiSeenTrayTooltip: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetSeenTrayTooltip(v == 1);
	} break;

	case dbiAutoUpdate: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetAutoUpdate(v == 1);
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
		if (!cAutoUpdate()) {
			Sandbox::stopUpdate();
		}
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	} break;

	case dbiLastUpdateCheck: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetLastUpdateCheck(v);
	} break;

	case dbiScale: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		DBIScale s = cRealScale();
		switch (v) {
		case dbisAuto: s = dbisAuto; break;
		case dbisOne: s = dbisOne; break;
		case dbisOneAndQuarter: s = dbisOneAndQuarter; break;
		case dbisOneAndHalf: s = dbisOneAndHalf; break;
		case dbisTwo: s = dbisTwo; break;
		}
		if (cRetina()) s = dbisOne;
		cSetConfigScale(s);
		cSetRealScale(s);
	} break;

	case dbiLang: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		if (v == languageTest || (v >= 0 && v < languageCount)) {
			cSetLang(v);
		}
	} break;

	case dbiLangFile: {
		QString v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetLangFile(v);
	} break;

	case dbiWindowPosition: {
		TWindowPos pos;
		stream >> pos.x >> pos.y >> pos.w >> pos.h >> pos.moncrc >> pos.maximized;
		if (!_checkStreamStatus(stream)) return false;

		cSetWindowPos(pos);
	} break;

	case dbiLoggedPhoneNumber: {
		QString v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetLoggedPhoneNumber(v);
	} break;

	case dbiMutePeer: { // deprecated
		quint64 peerId;
		stream >> peerId;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiMutedPeers: { // deprecated
		quint32 count;
		stream >> count;
		if (!_checkStreamStatus(stream)) return false;

		for (uint32 i = 0; i < count; ++i) {
			quint64 peerId;
			stream >> peerId;
		}
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiSendKey: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetCtrlEnter(v == dbiskCtrlEnter);
		if (App::main()) App::main()->ctrlEnterSubmitUpdated();
	} break;

	case dbiCatsAndDogs: { // deprecated
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiTileBackground: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		bool tile = (version < 8005 && !_backgroundKey) ? false : (v == 1);
		Window::chatBackground()->setTile(tile);
	} break;

	case dbiAdaptiveForWide: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetAdaptiveForWide(v == 1);
	} break;

	case dbiAutoLock: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetAutoLock(v);
		Global::RefLocalPasscodeChanged().notify();
	} break;

	case dbiReplaceEmojis: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetReplaceEmojis(v == 1);
	} break;

	case dbiDefaultAttach: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		switch (v) {
		case dbidaPhoto: cSetDefaultAttach(dbidaPhoto); break;
		default: cSetDefaultAttach(dbidaDocument); break;
		}
	} break;

	case dbiNotifyView: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		switch (v) {
		case dbinvShowNothing: Global::SetNotifyView(dbinvShowNothing); break;
		case dbinvShowName: Global::SetNotifyView(dbinvShowName); break;
		default: Global::SetNotifyView(dbinvShowPreview); break;
		}
	} break;

	case dbiAskDownloadPath: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetAskDownloadPath(v == 1);
	} break;

	case dbiDownloadPathOld: {
		QString v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		if (!v.isEmpty() && v != qstr("tmp") && !v.endsWith('/')) v += '/';
		Global::SetDownloadPath(v);
		Global::SetDownloadPathBookmark(QByteArray());
		Global::RefDownloadPathChanged().notify();
	} break;

	case dbiDownloadPath: {
		QString v;
		QByteArray bookmark;
		stream >> v >> bookmark;
		if (!_checkStreamStatus(stream)) return false;

		if (!v.isEmpty() && v != qstr("tmp") && !v.endsWith('/')) v += '/';
		Global::SetDownloadPath(v);
		Global::SetDownloadPathBookmark(bookmark);
		psDownloadPathEnableAccess();
		Global::RefDownloadPathChanged().notify();
	} break;

	case dbiCompressPastedImage: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetCompressPastedImage(v == 1);
	} break;

	case dbiEmojiTabOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		// deprecated
	} break;

	case dbiRecentEmojisOld: {
		RecentEmojisPreloadOld v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		if (!v.isEmpty()) {
			RecentEmojisPreload p;
			p.reserve(v.size());
			for (int i = 0; i < v.size(); ++i) {
				uint64 e(v.at(i).first);
				switch (e) {
				case 0xD83CDDEFLLU: e = 0xD83CDDEFD83CDDF5LLU; break;
				case 0xD83CDDF0LLU: e = 0xD83CDDF0D83CDDF7LLU; break;
				case 0xD83CDDE9LLU: e = 0xD83CDDE9D83CDDEALLU; break;
				case 0xD83CDDE8LLU: e = 0xD83CDDE8D83CDDF3LLU; break;
				case 0xD83CDDFALLU: e = 0xD83CDDFAD83CDDF8LLU; break;
				case 0xD83CDDEBLLU: e = 0xD83CDDEBD83CDDF7LLU; break;
				case 0xD83CDDEALLU: e = 0xD83CDDEAD83CDDF8LLU; break;
				case 0xD83CDDEELLU: e = 0xD83CDDEED83CDDF9LLU; break;
				case 0xD83CDDF7LLU: e = 0xD83CDDF7D83CDDFALLU; break;
				case 0xD83CDDECLLU: e = 0xD83CDDECD83CDDE7LLU; break;
				}
				p.push_back(qMakePair(e, v.at(i).second));
			}
			cSetRecentEmojisPreload(p);
		}
	} break;

	case dbiRecentEmojis: {
		RecentEmojisPreload v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetRecentEmojisPreload(v);
	} break;

	case dbiRecentStickers: {
		RecentStickerPreload v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetRecentStickersPreload(v);
	} break;

	case dbiEmojiVariants: {
		EmojiColorVariants v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetEmojiVariants(v);
	} break;


	case dbiHiddenPinnedMessages: {
		Global::HiddenPinnedMessagesMap v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetHiddenPinnedMessages(v);
	} break;

	case dbiDialogLastPath: {
		QString path;
		stream >> path;
		if (!_checkStreamStatus(stream)) return false;

		cSetDialogLastPath(path);
	} break;

	case dbiSongVolume: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetSongVolume(snap(v / 1e6, 0., 1.));
	} break;

	case dbiVideoVolume: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetVideoVolume(snap(v / 1e6, 0., 1.));
	} break;

	default:
	LOG(("App Error: unknown blockId in _readSetting: %1").arg(blockId));
	return false;
	}

	return true;
}

bool _readOldSettings(bool remove = true) {
	bool result = false;
	QFile file(cWorkingDir() + qsl("tdata/config"));
	if (file.open(QIODevice::ReadOnly)) {
		LOG(("App Info: reading old config..."));
		QDataStream stream(&file);
		stream.setVersion(QDataStream::Qt_5_1);

		qint32 version = 0;
		while (!stream.atEnd()) {
			quint32 blockId;
			stream >> blockId;
			if (!_checkStreamStatus(stream)) break;

			if (blockId == dbiVersion) {
				stream >> version;
				if (!_checkStreamStatus(stream)) break;

				if (version > AppVersion) break;
			} else if (!_readSetting(blockId, stream, version)) {
				break;
			}
		}
		file.close();
		result = true;
	}
	if (remove) file.remove();
	return result;
}

void _readOldUserSettingsFields(QIODevice *device, qint32 &version) {
	QDataStream stream(device);
	stream.setVersion(QDataStream::Qt_5_1);

	while (!stream.atEnd()) {
		quint32 blockId;
		stream >> blockId;
		if (!_checkStreamStatus(stream)) {
			break;
		}

		if (blockId == dbiVersion) {
			stream >> version;
			if (!_checkStreamStatus(stream)) {
				break;
			}

			if (version > AppVersion) return;
		} else if (blockId == dbiEncryptedWithSalt) {
			QByteArray salt, data, decrypted;
			stream >> salt >> data;
			if (!_checkStreamStatus(stream)) {
				break;
			}

			if (salt.size() != 32) {
				LOG(("App Error: bad salt in old user config encrypted part, size: %1").arg(salt.size()));
				continue;
			}

			createLocalKey(QByteArray(), &salt, &_oldKey);

			if (data.size() <= 16 || (data.size() & 0x0F)) {
				LOG(("App Error: bad encrypted part size in old user config: %1").arg(data.size()));
				continue;
			}
			uint32 fullDataLen = data.size() - 16;
			decrypted.resize(fullDataLen);
			const char *dataKey = data.constData(), *encrypted = data.constData() + 16;
			aesDecryptLocal(encrypted, decrypted.data(), fullDataLen, &_oldKey, dataKey);
			uchar sha1Buffer[20];
			if (memcmp(hashSha1(decrypted.constData(), decrypted.size(), sha1Buffer), dataKey, 16)) {
				LOG(("App Error: bad decrypt key, data from old user config not decrypted"));
				continue;
			}
			uint32 dataLen = *(const uint32*)decrypted.constData();
			if (dataLen > uint32(decrypted.size()) || dataLen <= fullDataLen - 16 || dataLen < 4) {
				LOG(("App Error: bad decrypted part size in old user config: %1, fullDataLen: %2, decrypted size: %3").arg(dataLen).arg(fullDataLen).arg(decrypted.size()));
				continue;
			}
			decrypted.resize(dataLen);
			QBuffer decryptedStream(&decrypted);
			decryptedStream.open(QIODevice::ReadOnly);
			decryptedStream.seek(4); // skip size
			LOG(("App Info: reading encrypted old user config..."));

			_readOldUserSettingsFields(&decryptedStream, version);
		} else if (!_readSetting(blockId, stream, version)) {
			return;
		}
	}
}

bool _readOldUserSettings(bool remove = true) {
	bool result = false;
	QFile file(cWorkingDir() + cDataFile() + (cTestMode() ? qsl("_test") : QString()) + qsl("_config"));
	if (file.open(QIODevice::ReadOnly)) {
		LOG(("App Info: reading old user config..."));
		qint32 version = 0;

		MTP::DcOptions dcOpts;
		{
			QReadLocker lock(MTP::dcOptionsMutex());
			dcOpts = Global::DcOptions();
		}
		_dcOpts = &dcOpts;
		_readOldUserSettingsFields(&file, version);
		{
			QWriteLocker lock(MTP::dcOptionsMutex());
			Global::SetDcOptions(dcOpts);
		}

		file.close();
		result = true;
	}
	if (remove) file.remove();
	return result;
}

void _readOldMtpDataFields(QIODevice *device, qint32 &version) {
	QDataStream stream(device);
	stream.setVersion(QDataStream::Qt_5_1);

	while (!stream.atEnd()) {
		quint32 blockId;
		stream >> blockId;
		if (!_checkStreamStatus(stream)) {
			break;
		}

		if (blockId == dbiVersion) {
			stream >> version;
			if (!_checkStreamStatus(stream)) {
				break;
			}

			if (version > AppVersion) return;
		} else if (blockId == dbiEncrypted) {
			QByteArray data, decrypted;
			stream >> data;
			if (!_checkStreamStatus(stream)) {
				break;
			}

			if (!_oldKey.created()) {
				LOG(("MTP Error: reading old encrypted keys without old key!"));
				continue;
			}

			if (data.size() <= 16 || (data.size() & 0x0F)) {
				LOG(("MTP Error: bad encrypted part size in old keys: %1").arg(data.size()));
				continue;
			}
			uint32 fullDataLen = data.size() - 16;
			decrypted.resize(fullDataLen);
			const char *dataKey = data.constData(), *encrypted = data.constData() + 16;
			aesDecryptLocal(encrypted, decrypted.data(), fullDataLen, &_oldKey, dataKey);
			uchar sha1Buffer[20];
			if (memcmp(hashSha1(decrypted.constData(), decrypted.size(), sha1Buffer), dataKey, 16)) {
				LOG(("MTP Error: bad decrypt key, data from old keys not decrypted"));
				continue;
			}
			uint32 dataLen = *(const uint32*)decrypted.constData();
			if (dataLen > uint32(decrypted.size()) || dataLen <= fullDataLen - 16 || dataLen < 4) {
				LOG(("MTP Error: bad decrypted part size in old keys: %1, fullDataLen: %2, decrypted size: %3").arg(dataLen).arg(fullDataLen).arg(decrypted.size()));
				continue;
			}
			decrypted.resize(dataLen);
			QBuffer decryptedStream(&decrypted);
			decryptedStream.open(QIODevice::ReadOnly);
			decryptedStream.seek(4); // skip size
			LOG(("App Info: reading encrypted old keys..."));

			_readOldMtpDataFields(&decryptedStream, version);
		} else if (!_readSetting(blockId, stream, version)) {
			return;
		}
	}
}

bool _readOldMtpData(bool remove = true) {
	bool result = false;
	QFile file(cWorkingDir() + cDataFile() + (cTestMode() ? qsl("_test") : QString()));
	if (file.open(QIODevice::ReadOnly)) {
		LOG(("App Info: reading old keys..."));
		qint32 version = 0;

		MTP::DcOptions dcOpts;
		{
			QReadLocker lock(MTP::dcOptionsMutex());
			dcOpts = Global::DcOptions();
		}
		_dcOpts = &dcOpts;
		_readOldMtpDataFields(&file, version);
		{
			QWriteLocker lock(MTP::dcOptionsMutex());
			Global::SetDcOptions(dcOpts);
		}

		file.close();
		result = true;
	}
	if (remove) file.remove();
	return result;
}

void _writeUserSettings() {
	if (_readingUserSettings) {
		LOG(("App Error: attempt to write settings while reading them!"));
		return;
	}
	LOG(("App Info: writing encrypted user settings..."));

	if (!_userSettingsKey) {
		_userSettingsKey = genKey();
		_mapChanged = true;
		_writeMap(WriteMapFast);
	}

	uint32 size = 20 * (sizeof(quint32) + sizeof(qint32));
	size += sizeof(quint32) + Serialize::stringSize(Global::AskDownloadPath() ? QString() : Global::DownloadPath()) + Serialize::bytearraySize(Global::AskDownloadPath() ? QByteArray() : Global::DownloadPathBookmark());
	size += sizeof(quint32) + sizeof(qint32) + (cRecentEmojisPreload().isEmpty() ? cGetRecentEmojis().size() : cRecentEmojisPreload().size()) * (sizeof(uint64) + sizeof(ushort));
	size += sizeof(quint32) + sizeof(qint32) + cEmojiVariants().size() * (sizeof(uint32) + sizeof(uint64));
	size += sizeof(quint32) + sizeof(qint32) + (cRecentStickersPreload().isEmpty() ? cGetRecentStickers().size() : cRecentStickersPreload().size()) * (sizeof(uint64) + sizeof(ushort));
	size += sizeof(quint32) + Serialize::stringSize(cDialogLastPath());
	size += sizeof(quint32) + 3 * sizeof(qint32);
	size += sizeof(quint32) + 2 * sizeof(qint32);
	if (!Global::HiddenPinnedMessages().isEmpty()) {
		size += sizeof(quint32) + sizeof(qint32) + Global::HiddenPinnedMessages().size() * (sizeof(PeerId) + sizeof(MsgId));
	}

	EncryptedDescriptor data(size);
	data.stream << quint32(dbiSendKey) << qint32(cCtrlEnter() ? dbiskCtrlEnter : dbiskEnter);
	data.stream << quint32(dbiTileBackground) << qint32(Window::chatBackground()->tile() ? 1 : 0);
	data.stream << quint32(dbiAdaptiveForWide) << qint32(Global::AdaptiveForWide() ? 1 : 0);
	data.stream << quint32(dbiAutoLock) << qint32(Global::AutoLock());
	data.stream << quint32(dbiReplaceEmojis) << qint32(cReplaceEmojis() ? 1 : 0);
	data.stream << quint32(dbiDefaultAttach) << qint32(cDefaultAttach());
	data.stream << quint32(dbiSoundNotify) << qint32(Global::SoundNotify());
	data.stream << quint32(dbiIncludeMuted) << qint32(Global::IncludeMuted());
	data.stream << quint32(dbiShowingSavedGifs) << qint32(cShowingSavedGifs());
	data.stream << quint32(dbiDesktopNotify) << qint32(Global::DesktopNotify());
	data.stream << quint32(dbiNotifyView) << qint32(Global::NotifyView());
	data.stream << quint32(dbiNativeNotifications) << qint32(Global::NativeNotifications());
	data.stream << quint32(dbiNotificationsCount) << qint32(Global::NotificationsCount());
	data.stream << quint32(dbiNotificationsCorner) << qint32(Global::NotificationsCorner());
	data.stream << quint32(dbiAskDownloadPath) << qint32(Global::AskDownloadPath());
	data.stream << quint32(dbiDownloadPath) << (Global::AskDownloadPath() ? QString() : Global::DownloadPath()) << (Global::AskDownloadPath() ? QByteArray() : Global::DownloadPathBookmark());
	data.stream << quint32(dbiCompressPastedImage) << qint32(cCompressPastedImage());
	data.stream << quint32(dbiDialogLastPath) << cDialogLastPath();
	data.stream << quint32(dbiSongVolume) << qint32(qRound(Global::SongVolume() * 1e6));
	data.stream << quint32(dbiVideoVolume) << qint32(qRound(Global::VideoVolume() * 1e6));
	data.stream << quint32(dbiAutoDownload) << qint32(cAutoDownloadPhoto()) << qint32(cAutoDownloadAudio()) << qint32(cAutoDownloadGif());
	data.stream << quint32(dbiDialogsMode) << qint32(Global::DialogsModeEnabled() ? 1 : 0) << static_cast<qint32>(Global::DialogsMode());
	data.stream << quint32(dbiModerateMode) << qint32(Global::ModerateModeEnabled() ? 1 : 0);
	data.stream << quint32(dbiAutoPlay) << qint32(cAutoPlayGif() ? 1 : 0);

	{
		RecentEmojisPreload v(cRecentEmojisPreload());
		if (v.isEmpty()) {
			v.reserve(cGetRecentEmojis().size());
			for (RecentEmojiPack::const_iterator i = cGetRecentEmojis().cbegin(), e = cGetRecentEmojis().cend(); i != e; ++i) {
				v.push_back(qMakePair(emojiKey(i->first), i->second));
			}
		}
		data.stream << quint32(dbiRecentEmojis) << v;
	}
	data.stream << quint32(dbiEmojiVariants) << cEmojiVariants();
	{
		RecentStickerPreload v(cRecentStickersPreload());
		if (v.isEmpty()) {
			v.reserve(cGetRecentStickers().size());
			for (RecentStickerPack::const_iterator i = cGetRecentStickers().cbegin(), e = cGetRecentStickers().cend(); i != e; ++i) {
				v.push_back(qMakePair(i->first->id, i->second));
			}
		}
		data.stream << quint32(dbiRecentStickers) << v;
	}
	if (!Global::HiddenPinnedMessages().isEmpty()) {
		data.stream << quint32(dbiHiddenPinnedMessages) << Global::HiddenPinnedMessages();
	}

	FileWriteDescriptor file(_userSettingsKey);
	file.writeEncrypted(data);
}

void _readUserSettings() {
	FileReadDescriptor userSettings;
	if (!readEncryptedFile(userSettings, _userSettingsKey)) {
		LOG(("App Info: could not read encrypted user settings..."));
		_readOldUserSettings();
		return _writeUserSettings();
	}

	LOG(("App Info: reading encrypted user settings..."));
	_readingUserSettings = true;
	while (!userSettings.stream.atEnd()) {
		quint32 blockId;
		userSettings.stream >> blockId;
		if (!_checkStreamStatus(userSettings.stream)) {
			_readingUserSettings = false;
			return _writeUserSettings();
		}

		if (!_readSetting(blockId, userSettings.stream, userSettings.version)) {
			_readingUserSettings = false;
			return _writeUserSettings();
		}
	}
	_readingUserSettings = false;
	LOG(("App Info: encrypted user settings read."));
}

void _writeMtpData() {
	FileWriteDescriptor mtp(toFilePart(_dataNameKey), SafePath);
	if (!_localKey.created()) {
		LOG(("App Error: localkey not created in _writeMtpData()"));
		return;
	}

	MTP::AuthKeysMap keys = MTP::getKeys();

	quint32 size = sizeof(quint32) + sizeof(qint32) + sizeof(quint32);
	size += keys.size() * (sizeof(quint32) + sizeof(quint32) + 256);

	EncryptedDescriptor data(size);
	data.stream << quint32(dbiUser) << qint32(MTP::authedId()) << quint32(MTP::maindc());
	for_const (const MTP::AuthKeyPtr &key, keys) {
		data.stream << quint32(dbiKey) << quint32(key->getDC());
		key->write(data.stream);
	}

	mtp.writeEncrypted(data, _localKey);
}

void _readMtpData() {
	FileReadDescriptor mtp;
	if (!readEncryptedFile(mtp, toFilePart(_dataNameKey), SafePath)) {
		if (_localKey.created()) {
			_readOldMtpData();
			_writeMtpData();
		}
		return;
	}

	LOG(("App Info: reading encrypted mtp data..."));
	while (!mtp.stream.atEnd()) {
		quint32 blockId;
		mtp.stream >> blockId;
		if (!_checkStreamStatus(mtp.stream)) {
			return _writeMtpData();
		}

		if (!_readSetting(blockId, mtp.stream, mtp.version)) {
			return _writeMtpData();
		}
	}
}

ReadMapState _readMap(const QByteArray &pass) {
	uint64 ms = getms();
	QByteArray dataNameUtf8 = (cDataFile() + (cTestMode() ? qsl(":/test/") : QString())).toUtf8();
	FileKey dataNameHash[2];
	hashMd5(dataNameUtf8.constData(), dataNameUtf8.size(), dataNameHash);
	_dataNameKey = dataNameHash[0];
	_userBasePath = _basePath + toFilePart(_dataNameKey) + QChar('/');

	FileReadDescriptor mapData;
	if (!readFile(mapData, qsl("map"))) {
		return ReadMapFailed;
	}
	LOG(("App Info: reading map..."));

	QByteArray salt, keyEncrypted, mapEncrypted;
	mapData.stream >> salt >> keyEncrypted >> mapEncrypted;
	if (!_checkStreamStatus(mapData.stream)) {
		return ReadMapFailed;
	}

	if (salt.size() != LocalEncryptSaltSize) {
		LOG(("App Error: bad salt in map file, size: %1").arg(salt.size()));
		return ReadMapFailed;
	}
	createLocalKey(pass, &salt, &_passKey);

	EncryptedDescriptor keyData, map;
	if (!decryptLocal(keyData, keyEncrypted, _passKey)) {
		LOG(("App Info: could not decrypt pass-protected key from map file, maybe bad password..."));
		return ReadMapPassNeeded;
	}
	uchar key[LocalEncryptKeySize] = { 0 };
	if (keyData.stream.readRawData((char*)key, LocalEncryptKeySize) != LocalEncryptKeySize || !keyData.stream.atEnd()) {
		LOG(("App Error: could not read pass-protected key from map file"));
		return ReadMapFailed;
	}
	_localKey.setKey(key);

	_passKeyEncrypted = keyEncrypted;
	_passKeySalt = salt;

	if (!decryptLocal(map, mapEncrypted)) {
		LOG(("App Error: could not decrypt map."));
		return ReadMapFailed;
	}
	LOG(("App Info: reading encrypted map..."));

	DraftsMap draftsMap, draftCursorsMap;
	DraftsNotReadMap draftsNotReadMap;
	StorageMap imagesMap, stickerImagesMap, audiosMap;
	qint64 storageImagesSize = 0, storageStickersSize = 0, storageAudiosSize = 0;
	quint64 locationsKey = 0, reportSpamStatusesKey = 0, trustedBotsKey = 0;
	quint64 recentStickersKeyOld = 0;
	quint64 installedStickersKey = 0, featuredStickersKey = 0, recentStickersKey = 0, archivedStickersKey = 0;
	quint64 savedGifsKey = 0;
	quint64 backgroundKey = 0, userSettingsKey = 0, recentHashtagsAndBotsKey = 0, savedPeersKey = 0;
	while (!map.stream.atEnd()) {
		quint32 keyType;
		map.stream >> keyType;
		switch (keyType) {
		case lskDraft: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 p;
				map.stream >> key >> p;
				draftsMap.insert(p, key);
				draftsNotReadMap.insert(p, true);
			}
		} break;
		case lskDraftPosition: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 p;
				map.stream >> key >> p;
				draftCursorsMap.insert(p, key);
			}
		} break;
		case lskImages: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 first, second;
				qint32 size;
				map.stream >> key >> first >> second >> size;
				imagesMap.insert(StorageKey(first, second), FileDesc(key, size));
				storageImagesSize += size;
			}
		} break;
		case lskStickerImages: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 first, second;
				qint32 size;
				map.stream >> key >> first >> second >> size;
				stickerImagesMap.insert(StorageKey(first, second), FileDesc(key, size));
				storageStickersSize += size;
			}
		} break;
		case lskAudios: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 first, second;
				qint32 size;
				map.stream >> key >> first >> second >> size;
				audiosMap.insert(StorageKey(first, second), FileDesc(key, size));
				storageAudiosSize += size;
			}
		} break;
		case lskLocations: {
			map.stream >> locationsKey;
		} break;
		case lskReportSpamStatuses: {
			map.stream >> reportSpamStatusesKey;
		} break;
		case lskTrustedBots: {
			map.stream >> trustedBotsKey;
		} break;
		case lskRecentStickersOld: {
			map.stream >> recentStickersKeyOld;
		} break;
		case lskBackground: {
			map.stream >> backgroundKey;
		} break;
		case lskUserSettings: {
			map.stream >> userSettingsKey;
		} break;
		case lskRecentHashtagsAndBots: {
			map.stream >> recentHashtagsAndBotsKey;
		} break;
		case lskStickersOld: {
			map.stream >> installedStickersKey;
		} break;
		case lskStickersKeys: {
			map.stream >> installedStickersKey >> featuredStickersKey >> recentStickersKey >> archivedStickersKey;
		} break;
		case lskSavedGifsOld: {
			quint64 key;
			map.stream >> key;
		} break;
		case lskSavedGifs: {
			map.stream >> savedGifsKey;
		} break;
		case lskSavedPeers: {
			map.stream >> savedPeersKey;
		} break;
		default:
		LOG(("App Error: unknown key type in encrypted map: %1").arg(keyType));
		return ReadMapFailed;
		}
		if (!_checkStreamStatus(map.stream)) {
			return ReadMapFailed;
		}
	}

	_draftsMap = draftsMap;
	_draftCursorsMap = draftCursorsMap;
	_draftsNotReadMap = draftsNotReadMap;

	_imagesMap = imagesMap;
	_storageImagesSize = storageImagesSize;
	_stickerImagesMap = stickerImagesMap;
	_storageStickersSize = storageStickersSize;
	_audiosMap = audiosMap;
	_storageAudiosSize = storageAudiosSize;

	_locationsKey = locationsKey;
	_reportSpamStatusesKey = reportSpamStatusesKey;
	_trustedBotsKey = trustedBotsKey;
	_recentStickersKeyOld = recentStickersKeyOld;
	_installedStickersKey = installedStickersKey;
	_featuredStickersKey = featuredStickersKey;
	_recentStickersKey = recentStickersKey;
	_archivedStickersKey = archivedStickersKey;
	_savedGifsKey = savedGifsKey;
	_savedPeersKey = savedPeersKey;
	_backgroundKey = backgroundKey;
	_userSettingsKey = userSettingsKey;
	_recentHashtagsAndBotsKey = recentHashtagsAndBotsKey;
	_oldMapVersion = mapData.version;
	if (_oldMapVersion < AppVersion) {
		_mapChanged = true;
		_writeMap();
	} else {
		_mapChanged = false;
	}

	if (_locationsKey) {
		_readLocations();
	}
	if (_reportSpamStatusesKey) {
		_readReportSpamStatuses();
	}

	_readUserSettings();
	_readMtpData();

	LOG(("Map read time: %1").arg(getms() - ms));
	if (_oldSettingsVersion < AppVersion) {
		writeSettings();
	}
	return ReadMapDone;
}

void _writeMap(WriteMapWhen when) {
	if (when != WriteMapNow) {
		_manager->writeMap(when == WriteMapFast);
		return;
	}
	_manager->writingMap();
	if (!_mapChanged) return;
	if (_userBasePath.isEmpty()) {
		LOG(("App Error: _userBasePath is empty in writeMap()"));
		return;
	}

	if (!QDir().exists(_userBasePath)) QDir().mkpath(_userBasePath);

	FileWriteDescriptor map(qsl("map"));
	if (_passKeySalt.isEmpty() || _passKeyEncrypted.isEmpty()) {
		uchar local5Key[LocalEncryptKeySize] = { 0 };
		QByteArray pass(LocalEncryptKeySize, Qt::Uninitialized), salt(LocalEncryptSaltSize, Qt::Uninitialized);
		memset_rand(pass.data(), pass.size());
		memset_rand(salt.data(), salt.size());
		createLocalKey(pass, &salt, &_localKey);

		_passKeySalt.resize(LocalEncryptSaltSize);
		memset_rand(_passKeySalt.data(), _passKeySalt.size());
		createLocalKey(QByteArray(), &_passKeySalt, &_passKey);

		EncryptedDescriptor passKeyData(LocalEncryptKeySize);
		_localKey.write(passKeyData.stream);
		_passKeyEncrypted = FileWriteDescriptor::prepareEncrypted(passKeyData, _passKey);
	}
	map.writeData(_passKeySalt);
	map.writeData(_passKeyEncrypted);

	uint32 mapSize = 0;
	if (!_draftsMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _draftsMap.size() * sizeof(quint64) * 2;
	if (!_draftCursorsMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _draftCursorsMap.size() * sizeof(quint64) * 2;
	if (!_imagesMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _imagesMap.size() * (sizeof(quint64) * 3 + sizeof(qint32));
	if (!_stickerImagesMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _stickerImagesMap.size() * (sizeof(quint64) * 3 + sizeof(qint32));
	if (!_audiosMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _audiosMap.size() * (sizeof(quint64) * 3 + sizeof(qint32));
	if (_locationsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_reportSpamStatusesKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_trustedBotsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_recentStickersKeyOld) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_installedStickersKey || _featuredStickersKey || _recentStickersKey || _archivedStickersKey) {
		mapSize += sizeof(quint32) + 4 * sizeof(quint64);
	}
	if (_savedGifsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_savedPeersKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_backgroundKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_userSettingsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_recentHashtagsAndBotsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	EncryptedDescriptor mapData(mapSize);
	if (!_draftsMap.isEmpty()) {
		mapData.stream << quint32(lskDraft) << quint32(_draftsMap.size());
		for (DraftsMap::const_iterator i = _draftsMap.cbegin(), e = _draftsMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value()) << quint64(i.key());
		}
	}
	if (!_draftCursorsMap.isEmpty()) {
		mapData.stream << quint32(lskDraftPosition) << quint32(_draftCursorsMap.size());
		for (DraftsMap::const_iterator i = _draftCursorsMap.cbegin(), e = _draftCursorsMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value()) << quint64(i.key());
		}
	}
	if (!_imagesMap.isEmpty()) {
		mapData.stream << quint32(lskImages) << quint32(_imagesMap.size());
		for (StorageMap::const_iterator i = _imagesMap.cbegin(), e = _imagesMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value().first) << quint64(i.key().first) << quint64(i.key().second) << qint32(i.value().second);
		}
	}
	if (!_stickerImagesMap.isEmpty()) {
		mapData.stream << quint32(lskStickerImages) << quint32(_stickerImagesMap.size());
		for (StorageMap::const_iterator i = _stickerImagesMap.cbegin(), e = _stickerImagesMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value().first) << quint64(i.key().first) << quint64(i.key().second) << qint32(i.value().second);
		}
	}
	if (!_audiosMap.isEmpty()) {
		mapData.stream << quint32(lskAudios) << quint32(_audiosMap.size());
		for (StorageMap::const_iterator i = _audiosMap.cbegin(), e = _audiosMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value().first) << quint64(i.key().first) << quint64(i.key().second) << qint32(i.value().second);
		}
	}
	if (_locationsKey) {
		mapData.stream << quint32(lskLocations) << quint64(_locationsKey);
	}
	if (_reportSpamStatusesKey) {
		mapData.stream << quint32(lskReportSpamStatuses) << quint64(_reportSpamStatusesKey);
	}
	if (_trustedBotsKey) {
		mapData.stream << quint32(lskTrustedBots) << quint64(_trustedBotsKey);
	}
	if (_recentStickersKeyOld) {
		mapData.stream << quint32(lskRecentStickersOld) << quint64(_recentStickersKeyOld);
	}
	if (_installedStickersKey || _featuredStickersKey || _recentStickersKey || _archivedStickersKey) {
		mapData.stream << quint32(lskStickersKeys);
		mapData.stream << quint64(_installedStickersKey) << quint64(_featuredStickersKey) << quint64(_recentStickersKey) << quint64(_archivedStickersKey);
	}
	if (_savedGifsKey) {
		mapData.stream << quint32(lskSavedGifs) << quint64(_savedGifsKey);
	}
	if (_savedPeersKey) {
		mapData.stream << quint32(lskSavedPeers) << quint64(_savedPeersKey);
	}
	if (_backgroundKey) {
		mapData.stream << quint32(lskBackground) << quint64(_backgroundKey);
	}
	if (_userSettingsKey) {
		mapData.stream << quint32(lskUserSettings) << quint64(_userSettingsKey);
	}
	if (_recentHashtagsAndBotsKey) {
		mapData.stream << quint32(lskRecentHashtagsAndBots) << quint64(_recentHashtagsAndBotsKey);
	}
	map.writeEncrypted(mapData);

	_mapChanged = false;
}

} // namespace

void finish() {
	if (_manager) {
		_writeMap(WriteMapNow);
		_manager->finish();
		_manager->deleteLater();
		_manager = 0;
		delete _localLoader;
		_localLoader = 0;
	}
}

void start() {
	t_assert(_manager == 0);

	_manager = new internal::Manager();
	_localLoader = new TaskQueue(0, FileLoaderQueueStopTimeout);

	_basePath = cWorkingDir() + qsl("tdata/");
	if (!QDir().exists(_basePath)) QDir().mkpath(_basePath);

	FileReadDescriptor settingsData;
	if (!readFile(settingsData, cTestMode() ? qsl("settings_test") : qsl("settings"), SafePath)) {
		_readOldSettings();
		_readOldUserSettings(false); // needed further in _readUserSettings
		_readOldMtpData(false); // needed further in _readMtpData
		return writeSettings();
	}
	LOG(("App Info: reading settings..."));

	QByteArray salt, settingsEncrypted;
	settingsData.stream >> salt >> settingsEncrypted;
	if (!_checkStreamStatus(settingsData.stream)) {
		return writeSettings();
	}

	if (salt.size() != LocalEncryptSaltSize) {
		LOG(("App Error: bad salt in settings file, size: %1").arg(salt.size()));
		return writeSettings();
	}
	createLocalKey(QByteArray(), &salt, &_settingsKey);

	EncryptedDescriptor settings;
	if (!decryptLocal(settings, settingsEncrypted, _settingsKey)) {
		LOG(("App Error: could not decrypt settings from settings file, maybe bad passcode..."));
		return writeSettings();
	}
	MTP::DcOptions dcOpts;
	{
		QReadLocker lock(MTP::dcOptionsMutex());
		dcOpts = Global::DcOptions();
	}
	_dcOpts = &dcOpts;
	LOG(("App Info: reading encrypted settings..."));
	while (!settings.stream.atEnd()) {
		quint32 blockId;
		settings.stream >> blockId;
		if (!_checkStreamStatus(settings.stream)) {
			return writeSettings();
		}

		if (!_readSetting(blockId, settings.stream, settingsData.version)) {
			return writeSettings();
		}
	}
	if (dcOpts.isEmpty()) {
		const BuiltInDc *bdcs = builtInDcs();
		for (int i = 0, l = builtInDcsCount(); i < l; ++i) {
			MTPDdcOption::Flags flags = 0;
			MTP::ShiftedDcId idWithShift = MTP::shiftDcId(bdcs[i].id, flags);
			dcOpts.insert(idWithShift, MTP::DcOption(bdcs[i].id, flags, bdcs[i].ip, bdcs[i].port));
			DEBUG_LOG(("MTP Info: adding built in DC %1 connect option: %2:%3").arg(bdcs[i].id).arg(bdcs[i].ip).arg(bdcs[i].port));
		}

		const BuiltInDc *bdcsipv6 = builtInDcsIPv6();
		for (int i = 0, l = builtInDcsCountIPv6(); i < l; ++i) {
			MTPDdcOption::Flags flags = MTPDdcOption::Flag::f_ipv6;
			MTP::ShiftedDcId idWithShift = MTP::shiftDcId(bdcsipv6[i].id, flags);
			dcOpts.insert(idWithShift, MTP::DcOption(bdcsipv6[i].id, flags, bdcsipv6[i].ip, bdcsipv6[i].port));
			DEBUG_LOG(("MTP Info: adding built in DC %1 IPv6 connect option: %2:%3").arg(bdcsipv6[i].id).arg(bdcsipv6[i].ip).arg(bdcsipv6[i].port));
		}
	}
	{
		QWriteLocker lock(MTP::dcOptionsMutex());
		Global::SetDcOptions(dcOpts);
	}

	_oldSettingsVersion = settingsData.version;
	_settingsSalt = salt;
}

void writeSettings() {
	if (_basePath.isEmpty()) {
		LOG(("App Error: _basePath is empty in writeSettings()"));
		return;
	}

	if (!QDir().exists(_basePath)) QDir().mkpath(_basePath);

	FileWriteDescriptor settings(cTestMode() ? qsl("settings_test") : qsl("settings"), SafePath);
	if (_settingsSalt.isEmpty() || !_settingsKey.created()) {
		_settingsSalt.resize(LocalEncryptSaltSize);
		memset_rand(_settingsSalt.data(), _settingsSalt.size());
		createLocalKey(QByteArray(), &_settingsSalt, &_settingsKey);
	}
	settings.writeData(_settingsSalt);

	MTP::DcOptions dcOpts;
	{
		QReadLocker lock(MTP::dcOptionsMutex());
		dcOpts = Global::DcOptions();
	}
	if (dcOpts.isEmpty()) {
		const BuiltInDc *bdcs = builtInDcs();
		for (int i = 0, l = builtInDcsCount(); i < l; ++i) {
			MTPDdcOption::Flags flags = 0;
			MTP::ShiftedDcId idWithShift = MTP::shiftDcId(bdcs[i].id, flags);
			dcOpts.insert(idWithShift, MTP::DcOption(bdcs[i].id, flags, bdcs[i].ip, bdcs[i].port));
			DEBUG_LOG(("MTP Info: adding built in DC %1 connect option: %2:%3").arg(bdcs[i].id).arg(bdcs[i].ip).arg(bdcs[i].port));
		}

		const BuiltInDc *bdcsipv6 = builtInDcsIPv6();
		for (int i = 0, l = builtInDcsCountIPv6(); i < l; ++i) {
			MTPDdcOption::Flags flags = MTPDdcOption::Flag::f_ipv6;
			MTP::ShiftedDcId idWithShift = MTP::shiftDcId(bdcsipv6[i].id, flags);
			dcOpts.insert(idWithShift, MTP::DcOption(bdcsipv6[i].id, flags, bdcsipv6[i].ip, bdcsipv6[i].port));
			DEBUG_LOG(("MTP Info: adding built in DC %1 IPv6 connect option: %2:%3").arg(bdcsipv6[i].id).arg(bdcsipv6[i].ip).arg(bdcsipv6[i].port));
		}

		QWriteLocker lock(MTP::dcOptionsMutex());
		Global::SetDcOptions(dcOpts);
	}

	quint32 size = 12 * (sizeof(quint32) + sizeof(qint32));
	for (auto i = dcOpts.cbegin(), e = dcOpts.cend(); i != e; ++i) {
		size += sizeof(quint32) + sizeof(quint32) + sizeof(quint32);
		size += sizeof(quint32) + Serialize::stringSize(QString::fromUtf8(i->ip.data(), i->ip.size()));
	}
	size += sizeof(quint32) + Serialize::stringSize(cLangFile());

	size += sizeof(quint32) + sizeof(qint32);
	if (Global::ConnectionType() == dbictHttpProxy || Global::ConnectionType() == dbictTcpProxy) {
		auto &proxy = Global::ConnectionProxy();
		size += Serialize::stringSize(proxy.host) + sizeof(qint32) + Serialize::stringSize(proxy.user) + Serialize::stringSize(proxy.password);
	}

	size += sizeof(quint32) + sizeof(qint32) * 7;

	EncryptedDescriptor data(size);
	data.stream << quint32(dbiChatSizeMax) << qint32(Global::ChatSizeMax());
	data.stream << quint32(dbiMegagroupSizeMax) << qint32(Global::MegagroupSizeMax());
	data.stream << quint32(dbiSavedGifsLimit) << qint32(Global::SavedGifsLimit());
	data.stream << quint32(dbiStickersRecentLimit) << qint32(Global::StickersRecentLimit());
	data.stream << quint32(dbiAutoStart) << qint32(cAutoStart());
	data.stream << quint32(dbiStartMinimized) << qint32(cStartMinimized());
	data.stream << quint32(dbiSendToMenu) << qint32(cSendToMenu());
	data.stream << quint32(dbiWorkMode) << qint32(cWorkMode());
	data.stream << quint32(dbiSeenTrayTooltip) << qint32(cSeenTrayTooltip());
	data.stream << quint32(dbiAutoUpdate) << qint32(cAutoUpdate());
	data.stream << quint32(dbiLastUpdateCheck) << qint32(cLastUpdateCheck());
	data.stream << quint32(dbiScale) << qint32(cConfigScale());
	data.stream << quint32(dbiLang) << qint32(cLang());
	for (auto i = dcOpts.cbegin(), e = dcOpts.cend(); i != e; ++i) {
		data.stream << quint32(dbiDcOption) << quint32(i.key());
		data.stream << qint32(i->flags) << QString::fromUtf8(i->ip.data(), i->ip.size());
		data.stream << quint32(i->port);
	}
	data.stream << quint32(dbiLangFile) << cLangFile();

	data.stream << quint32(dbiConnectionType) << qint32(Global::ConnectionType());
	if (Global::ConnectionType() == dbictHttpProxy || Global::ConnectionType() == dbictTcpProxy) {
		auto &proxy = Global::ConnectionProxy();
		data.stream << proxy.host << qint32(proxy.port) << proxy.user << proxy.password;
	}
	data.stream << quint32(dbiTryIPv6) << qint32(Global::TryIPv6());

	TWindowPos pos(cWindowPos());
	data.stream << quint32(dbiWindowPosition) << qint32(pos.x) << qint32(pos.y) << qint32(pos.w) << qint32(pos.h) << qint32(pos.moncrc) << qint32(pos.maximized);

	settings.writeEncrypted(data, _settingsKey);
}

void writeUserSettings() {
	_writeUserSettings();
}

void writeMtpData() {
	_writeMtpData();
}

void reset() {
	if (_localLoader) {
		_localLoader->stop();
	}

	_passKeySalt.clear(); // reset passcode, local key
	_draftsMap.clear();
	_draftCursorsMap.clear();
	_fileLocations.clear();
	_fileLocationPairs.clear();
	_fileLocationAliases.clear();
	_imagesMap.clear();
	_draftsNotReadMap.clear();
	_stickerImagesMap.clear();
	_audiosMap.clear();
	_storageImagesSize = _storageStickersSize = _storageAudiosSize = 0;
	_webFilesMap.clear();
	_storageWebFilesSize = 0;
	_locationsKey = _reportSpamStatusesKey = _trustedBotsKey = 0;
	_recentStickersKeyOld = 0;
	_installedStickersKey = _featuredStickersKey = _recentStickersKey = _archivedStickersKey = 0;
	_savedGifsKey = 0;
	_backgroundKey = _userSettingsKey = _recentHashtagsAndBotsKey = _savedPeersKey = 0;
	_oldMapVersion = _oldSettingsVersion = 0;
	_mapChanged = true;
	_writeMap(WriteMapNow);

	_writeMtpData();
}

bool checkPasscode(const QByteArray &passcode) {
	MTP::AuthKey tmp;
	createLocalKey(passcode, &_passKeySalt, &tmp);
	return (tmp == _passKey);
}

void setPasscode(const QByteArray &passcode) {
	createLocalKey(passcode, &_passKeySalt, &_passKey);

	EncryptedDescriptor passKeyData(LocalEncryptKeySize);
	_localKey.write(passKeyData.stream);
	_passKeyEncrypted = FileWriteDescriptor::prepareEncrypted(passKeyData, _passKey);

	_mapChanged = true;
	_writeMap(WriteMapNow);

	Global::SetLocalPasscode(!passcode.isEmpty());
	Global::RefLocalPasscodeChanged().notify();
}

ReadMapState readMap(const QByteArray &pass) {
	ReadMapState result = _readMap(pass);
	if (result == ReadMapFailed) {
		_mapChanged = true;
		_writeMap(WriteMapNow);
	}
	return result;
}

int32 oldMapVersion() {
	return _oldMapVersion;
}

int32 oldSettingsVersion() {
	return _oldSettingsVersion;
}

void writeDrafts(const PeerId &peer, const MessageDraft &localDraft, const MessageDraft &editDraft) {
	if (!_working()) return;

	if (localDraft.msgId <= 0 && localDraft.textWithTags.text.isEmpty() && editDraft.msgId <= 0) {
		auto i = _draftsMap.find(peer);
		if (i != _draftsMap.cend()) {
			clearKey(i.value());
			_draftsMap.erase(i);
			_mapChanged = true;
			_writeMap();
		}

		_draftsNotReadMap.remove(peer);
	} else {
		auto i = _draftsMap.constFind(peer);
		if (i == _draftsMap.cend()) {
			i = _draftsMap.insert(peer, genKey());
			_mapChanged = true;
			_writeMap(WriteMapFast);
		}

		auto msgTags = FlatTextarea::serializeTagsList(localDraft.textWithTags.tags);
		auto editTags = FlatTextarea::serializeTagsList(editDraft.textWithTags.tags);

		int size = sizeof(quint64);
		size += Serialize::stringSize(localDraft.textWithTags.text) + Serialize::bytearraySize(msgTags) + 2 * sizeof(qint32);
		size += Serialize::stringSize(editDraft.textWithTags.text) + Serialize::bytearraySize(editTags) + 2 * sizeof(qint32);

		EncryptedDescriptor data(size);
		data.stream << quint64(peer);
		data.stream << localDraft.textWithTags.text << msgTags;
		data.stream << qint32(localDraft.msgId) << qint32(localDraft.previewCancelled ? 1 : 0);
		data.stream << editDraft.textWithTags.text << editTags;
		data.stream << qint32(editDraft.msgId) << qint32(editDraft.previewCancelled ? 1 : 0);

		FileWriteDescriptor file(i.value());
		file.writeEncrypted(data);

		_draftsNotReadMap.remove(peer);
	}
}

void clearDraftCursors(const PeerId &peer) {
	DraftsMap::iterator i = _draftCursorsMap.find(peer);
	if (i != _draftCursorsMap.cend()) {
		clearKey(i.value());
		_draftCursorsMap.erase(i);
		_mapChanged = true;
		_writeMap();
	}
}

void _readDraftCursors(const PeerId &peer, MessageCursor &localCursor, MessageCursor &editCursor) {
	DraftsMap::iterator j = _draftCursorsMap.find(peer);
	if (j == _draftCursorsMap.cend()) {
		return;
	}

	FileReadDescriptor draft;
	if (!readEncryptedFile(draft, j.value())) {
		clearDraftCursors(peer);
		return;
	}
	quint64 draftPeer;
	qint32 localPosition = 0, localAnchor = 0, localScroll = QFIXED_MAX;
	qint32 editPosition = 0, editAnchor = 0, editScroll = QFIXED_MAX;
	draft.stream >> draftPeer >> localPosition >> localAnchor >> localScroll;
	if (!draft.stream.atEnd()) {
		draft.stream >> editPosition >> editAnchor >> editScroll;
	}

	if (draftPeer != peer) {
		clearDraftCursors(peer);
		return;
	}

	localCursor = MessageCursor(localPosition, localAnchor, localScroll);
	editCursor = MessageCursor(editPosition, editAnchor, editScroll);
}

void readDraftsWithCursors(History *h) {
	PeerId peer = h->peer->id;
	if (!_draftsNotReadMap.remove(peer)) {
		clearDraftCursors(peer);
		return;
	}

	DraftsMap::iterator j = _draftsMap.find(peer);
	if (j == _draftsMap.cend()) {
		clearDraftCursors(peer);
		return;
	}
	FileReadDescriptor draft;
	if (!readEncryptedFile(draft, j.value())) {
		clearKey(j.value());
		_draftsMap.erase(j);
		clearDraftCursors(peer);
		return;
	}

	quint64 draftPeer = 0;
	TextWithTags msgData, editData;
	QByteArray msgTagsSerialized, editTagsSerialized;
	qint32 msgReplyTo = 0, msgPreviewCancelled = 0, editMsgId = 0, editPreviewCancelled = 0;
	draft.stream >> draftPeer >> msgData.text;
	if (draft.version >= 9048) {
		draft.stream >> msgTagsSerialized;
	}
	if (draft.version >= 7021) {
		draft.stream >> msgReplyTo;
		if (draft.version >= 8001) {
			draft.stream >> msgPreviewCancelled;
			if (!draft.stream.atEnd()) {
				draft.stream >> editData.text;
				if (draft.version >= 9048) {
					draft.stream >> editTagsSerialized;
				}
				draft.stream >> editMsgId >> editPreviewCancelled;
			}
		}
	}
	if (draftPeer != peer) {
		clearKey(j.value());
		_draftsMap.erase(j);
		clearDraftCursors(peer);
		return;
	}

	msgData.tags = FlatTextarea::deserializeTagsList(msgTagsSerialized, msgData.text.size());
	editData.tags = FlatTextarea::deserializeTagsList(editTagsSerialized, editData.text.size());

	MessageCursor msgCursor, editCursor;
	_readDraftCursors(peer, msgCursor, editCursor);

	if (!h->localDraft()) {
		if (msgData.text.isEmpty() && !msgReplyTo) {
			h->clearLocalDraft();
		} else {
			h->setLocalDraft(std_::make_unique<Data::Draft>(msgData, msgReplyTo, msgCursor, msgPreviewCancelled));
		}
	}
	if (!editMsgId) {
		h->clearEditDraft();
	} else {
		h->setEditDraft(std_::make_unique<Data::Draft>(editData, editMsgId, editCursor, editPreviewCancelled));
	}
}

void writeDraftCursors(const PeerId &peer, const MessageCursor &msgCursor, const MessageCursor &editCursor) {
	if (!_working()) return;

	if (msgCursor == MessageCursor() && editCursor == MessageCursor()) {
		clearDraftCursors(peer);
	} else {
		DraftsMap::const_iterator i = _draftCursorsMap.constFind(peer);
		if (i == _draftCursorsMap.cend()) {
			i = _draftCursorsMap.insert(peer, genKey());
			_mapChanged = true;
			_writeMap(WriteMapFast);
		}

		EncryptedDescriptor data(sizeof(quint64) + sizeof(qint32) * 3);
		data.stream << quint64(peer) << qint32(msgCursor.position) << qint32(msgCursor.anchor) << qint32(msgCursor.scroll);
		data.stream << qint32(editCursor.position) << qint32(editCursor.anchor) << qint32(editCursor.scroll);

		FileWriteDescriptor file(i.value());
		file.writeEncrypted(data);
	}
}

bool hasDraftCursors(const PeerId &peer) {
	return _draftCursorsMap.contains(peer);
}

bool hasDraft(const PeerId &peer) {
	return _draftsMap.contains(peer);
}

void writeFileLocation(MediaKey location, const FileLocation &local) {
	if (local.fname.isEmpty()) return;

	FileLocationAliases::const_iterator aliasIt = _fileLocationAliases.constFind(location);
	if (aliasIt != _fileLocationAliases.cend()) {
		location = aliasIt.value();
	}

	FileLocationPairs::iterator i = _fileLocationPairs.find(local.fname);
	if (i != _fileLocationPairs.cend()) {
		if (i.value().second == local) {
			if (i.value().first != location) {
				_fileLocationAliases.insert(location, i.value().first);
				_writeLocations(WriteMapFast);
			}
			return;
		}
		if (i.value().first != location) {
			for (FileLocations::iterator j = _fileLocations.find(i.value().first), e = _fileLocations.end(); (j != e) && (j.key() == i.value().first);) {
				if (j.value() == i.value().second) {
					_fileLocations.erase(j);
					break;
				}
			}
			_fileLocationPairs.erase(i);
		}
	}
	_fileLocations.insert(location, local);
	_fileLocationPairs.insert(local.fname, FileLocationPair(location, local));
	_writeLocations(WriteMapFast);
}

FileLocation readFileLocation(MediaKey location, bool check) {
	FileLocationAliases::const_iterator aliasIt = _fileLocationAliases.constFind(location);
	if (aliasIt != _fileLocationAliases.cend()) {
		location = aliasIt.value();
	}

	FileLocations::iterator i = _fileLocations.find(location);
	for (FileLocations::iterator i = _fileLocations.find(location); (i != _fileLocations.end()) && (i.key() == location);) {
		if (check) {
			if (!i.value().check()) {
				_fileLocationPairs.remove(i.value().fname);
				i = _fileLocations.erase(i);
				_writeLocations();
				continue;
			}
		}
		return i.value();
	}
	return FileLocation();
}

qint32 _storageImageSize(qint32 rawlen) {
	// fulllen + storagekey + type + len + data
	qint32 result = sizeof(uint32) + sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + rawlen;
	if (result & 0x0F) result += 0x10 - (result & 0x0F);
	result += tdfMagicLen + sizeof(qint32) + sizeof(quint32) + 0x10 + 0x10; // magic + version + len of encrypted + part of sha1 + md5
	return result;
}

qint32 _storageStickerSize(qint32 rawlen) {
	// fulllen + storagekey + len + data
	qint32 result = sizeof(uint32) + sizeof(quint64) * 2 + sizeof(quint32) + rawlen;
	if (result & 0x0F) result += 0x10 - (result & 0x0F);
	result += tdfMagicLen + sizeof(qint32) + sizeof(quint32) + 0x10 + 0x10; // magic + version + len of encrypted + part of sha1 + md5
	return result;
}

qint32 _storageAudioSize(qint32 rawlen) {
	// fulllen + storagekey + len + data
	qint32 result = sizeof(uint32) + sizeof(quint64) * 2 + sizeof(quint32) + rawlen;
	if (result & 0x0F) result += 0x10 - (result & 0x0F);
	result += tdfMagicLen + sizeof(qint32) + sizeof(quint32) + 0x10 + 0x10; // magic + version + len of encrypted + part of sha1 + md5
	return result;
}

void writeImage(const StorageKey &location, const ImagePtr &image) {
	if (image->isNull() || !image->loaded()) return;
	if (_imagesMap.constFind(location) != _imagesMap.cend()) return;

	QByteArray fmt = image->savedFormat();
	StorageFileType format = StorageFileUnknown;
	if (fmt == "JPG") {
		format = StorageFileJpeg;
	} else if (fmt == "PNG") {
		format = StorageFilePng;
	} else if (fmt == "GIF") {
		format = StorageFileGif;
	}
	if (format) {
		image->forget();
		writeImage(location, StorageImageSaved(format, image->savedData()), false);
	}
}

void writeImage(const StorageKey &location, const StorageImageSaved &image, bool overwrite) {
	if (!_working()) return;

	qint32 size = _storageImageSize(image.data.size());
	StorageMap::const_iterator i = _imagesMap.constFind(location);
	if (i == _imagesMap.cend()) {
		i = _imagesMap.insert(location, FileDesc(genKey(UserPath), size));
		_storageImagesSize += size;
		_mapChanged = true;
		_writeMap();
	} else if (!overwrite) {
		return;
	}
	EncryptedDescriptor data(sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + image.data.size());
	data.stream << quint64(location.first) << quint64(location.second) << quint32(image.type) << image.data;
	FileWriteDescriptor file(i.value().first, UserPath);
	file.writeEncrypted(data);
	if (i.value().second != size) {
		_storageImagesSize += size;
		_storageImagesSize -= i.value().second;
		_imagesMap[location].second = size;
	}
}

class AbstractCachedLoadTask : public Task {
public:

	AbstractCachedLoadTask(const FileKey &key, const StorageKey &location, bool readImageFlag, mtpFileLoader *loader) :
		_key(key), _location(location), _readImageFlag(readImageFlag), _loader(loader), _result(0) {
	}
	void process() {
		FileReadDescriptor image;
		if (!readEncryptedFile(image, _key, UserPath)) {
			return;
		}

		QByteArray imageData;
		quint64 locFirst, locSecond;
		quint32 imageType;
		readFromStream(image.stream, locFirst, locSecond, imageType, imageData);

		// we're saving files now before we have actual location
		//if (locFirst != _location.first || locSecond != _location.second) {
		//	return;
		//}

		_result = new Result(StorageFileType(imageType), imageData, _readImageFlag);
	}
	void finish() {
		if (_result) {
			_loader->localLoaded(_result->image, _result->format, _result->pixmap);
		} else {
			clearInMap();
			_loader->localLoaded(StorageImageSaved());
		}
	}
	virtual void readFromStream(QDataStream &stream, quint64 &first, quint64 &second, quint32 &type, QByteArray &data) = 0;
	virtual void clearInMap() = 0;
	virtual ~AbstractCachedLoadTask() {
		delete base::take(_result);
	}

protected:
	FileKey _key;
	StorageKey _location;
	bool _readImageFlag;
	struct Result {
		Result(StorageFileType type, const QByteArray &data, bool readImageFlag) : image(type, data) {
			if (readImageFlag) {
				QByteArray guessFormat;
				switch (type) {
					case StorageFileGif: guessFormat = "GIF"; break;
					case StorageFileJpeg: guessFormat = "JPG"; break;
					case StorageFilePng: guessFormat = "PNG"; break;
					case StorageFileWebp: guessFormat = "WEBP"; break;
					default: guessFormat = QByteArray(); break;
				}
				pixmap = App::pixmapFromImageInPlace(App::readImage(data, &guessFormat, false));
				if (!pixmap.isNull()) {
					format = guessFormat;
				}
			}
		}
		StorageImageSaved image;
		QByteArray format;
		QPixmap pixmap;
	};
	mtpFileLoader *_loader;
	Result *_result;

};

class ImageLoadTask : public AbstractCachedLoadTask {
public:
	ImageLoadTask(const FileKey &key, const StorageKey &location, mtpFileLoader *loader) :
	AbstractCachedLoadTask(key, location, true, loader) {
	}
	void readFromStream(QDataStream &stream, quint64 &first, quint64 &second, quint32 &type, QByteArray &data) {
		stream >> first >> second >> type >> data;
	}
	void clearInMap() {
		StorageMap::iterator j = _imagesMap.find(_location);
		if (j != _imagesMap.cend() && j->first == _key) {
			clearKey(_key, UserPath);
			_storageImagesSize -= j->second;
			_imagesMap.erase(j);
		}
	}
};

TaskId startImageLoad(const StorageKey &location, mtpFileLoader *loader) {
	StorageMap::const_iterator j = _imagesMap.constFind(location);
	if (j == _imagesMap.cend() || !_localLoader) {
		return 0;
	}
	return _localLoader->addTask(new ImageLoadTask(j->first, location, loader));
}

int32 hasImages() {
	return _imagesMap.size();
}

qint64 storageImagesSize() {
	return _storageImagesSize;
}

void writeStickerImage(const StorageKey &location, const QByteArray &sticker, bool overwrite) {
	if (!_working()) return;

	qint32 size = _storageStickerSize(sticker.size());
	StorageMap::const_iterator i = _stickerImagesMap.constFind(location);
	if (i == _stickerImagesMap.cend()) {
		i = _stickerImagesMap.insert(location, FileDesc(genKey(UserPath), size));
		_storageStickersSize += size;
		_mapChanged = true;
		_writeMap();
	} else if (!overwrite) {
		return;
	}
	EncryptedDescriptor data(sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + sticker.size());
	data.stream << quint64(location.first) << quint64(location.second) << sticker;
	FileWriteDescriptor file(i.value().first, UserPath);
	file.writeEncrypted(data);
	if (i.value().second != size) {
		_storageStickersSize += size;
		_storageStickersSize -= i.value().second;
		_stickerImagesMap[location].second = size;
	}
}

class StickerImageLoadTask : public AbstractCachedLoadTask {
public:
	StickerImageLoadTask(const FileKey &key, const StorageKey &location, mtpFileLoader *loader) :
	AbstractCachedLoadTask(key, location, true, loader) {
	}
	void readFromStream(QDataStream &stream, quint64 &first, quint64 &second, quint32 &type, QByteArray &data) {
		stream >> first >> second >> data;
		type = StorageFilePartial;
	}
	void clearInMap() {
		auto j = _stickerImagesMap.find(_location);
		if (j != _stickerImagesMap.cend() && j->first == _key) {
			clearKey(j.value().first, UserPath);
			_storageStickersSize -= j.value().second;
			_stickerImagesMap.erase(j);
		}
	}
};

TaskId startStickerImageLoad(const StorageKey &location, mtpFileLoader *loader) {
	auto j = _stickerImagesMap.constFind(location);
	if (j == _stickerImagesMap.cend() || !_localLoader) {
		return 0;
	}
	return _localLoader->addTask(new StickerImageLoadTask(j->first, location, loader));
}

bool willStickerImageLoad(const StorageKey &location) {
	return _stickerImagesMap.constFind(location) != _stickerImagesMap.cend();
}

bool copyStickerImage(const StorageKey &oldLocation, const StorageKey &newLocation) {
	auto i = _stickerImagesMap.constFind(oldLocation);
	if (i == _stickerImagesMap.cend()) {
		return false;
	}
	_stickerImagesMap.insert(newLocation, i.value());
	_mapChanged = true;
	_writeMap();
	return true;
}

int32 hasStickers() {
	return _stickerImagesMap.size();
}

qint64 storageStickersSize() {
	return _storageStickersSize;
}

void writeAudio(const StorageKey &location, const QByteArray &audio, bool overwrite) {
	if (!_working()) return;

	qint32 size = _storageAudioSize(audio.size());
	StorageMap::const_iterator i = _audiosMap.constFind(location);
	if (i == _audiosMap.cend()) {
		i = _audiosMap.insert(location, FileDesc(genKey(UserPath), size));
		_storageAudiosSize += size;
		_mapChanged = true;
		_writeMap();
	} else if (!overwrite) {
		return;
	}
	EncryptedDescriptor data(sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + audio.size());
	data.stream << quint64(location.first) << quint64(location.second) << audio;
	FileWriteDescriptor file(i.value().first, UserPath);
	file.writeEncrypted(data);
	if (i.value().second != size) {
		_storageAudiosSize += size;
		_storageAudiosSize -= i.value().second;
		_audiosMap[location].second = size;
	}
}

class AudioLoadTask : public AbstractCachedLoadTask {
public:
	AudioLoadTask(const FileKey &key, const StorageKey &location, mtpFileLoader *loader) :
	AbstractCachedLoadTask(key, location, false, loader) {
	}
	void readFromStream(QDataStream &stream, quint64 &first, quint64 &second, quint32 &type, QByteArray &data) {
		stream >> first >> second >> data;
		type = StorageFilePartial;
	}
	void clearInMap() {
		auto j = _audiosMap.find(_location);
		if (j != _audiosMap.cend() && j->first == _key) {
			clearKey(j.value().first, UserPath);
			_storageAudiosSize -= j.value().second;
			_audiosMap.erase(j);
		}
	}
};

TaskId startAudioLoad(const StorageKey &location, mtpFileLoader *loader) {
	auto j = _audiosMap.constFind(location);
	if (j == _audiosMap.cend() || !_localLoader) {
		return 0;
	}
	return _localLoader->addTask(new AudioLoadTask(j->first, location, loader));
}

bool copyAudio(const StorageKey &oldLocation, const StorageKey &newLocation) {
	auto i = _audiosMap.constFind(oldLocation);
	if (i == _audiosMap.cend()) {
		return false;
	}
	_audiosMap.insert(newLocation, i.value());
	_mapChanged = true;
	_writeMap();
	return true;
}

int32 hasAudios() {
	return _audiosMap.size();
}

qint64 storageAudiosSize() {
	return _storageAudiosSize;
}

qint32 _storageWebFileSize(const QString &url, qint32 rawlen) {
	// fulllen + url + len + data
	qint32 result = sizeof(uint32) + Serialize::stringSize(url) + sizeof(quint32) + rawlen;
	if (result & 0x0F) result += 0x10 - (result & 0x0F);
	result += tdfMagicLen + sizeof(qint32) + sizeof(quint32) + 0x10 + 0x10; // magic + version + len of encrypted + part of sha1 + md5
	return result;
}

void writeWebFile(const QString &url, const QByteArray &content, bool overwrite) {
	if (!_working()) return;

	qint32 size = _storageWebFileSize(url, content.size());
	WebFilesMap::const_iterator i = _webFilesMap.constFind(url);
	if (i == _webFilesMap.cend()) {
		i = _webFilesMap.insert(url, FileDesc(genKey(UserPath), size));
		_storageWebFilesSize += size;
		_writeLocations();
	} else if (!overwrite) {
		return;
	}
	EncryptedDescriptor data(Serialize::stringSize(url) + sizeof(quint32) + sizeof(quint32) + content.size());
	data.stream << url << content;
	FileWriteDescriptor file(i.value().first, UserPath);
	file.writeEncrypted(data);
	if (i.value().second != size) {
		_storageWebFilesSize += size;
		_storageWebFilesSize -= i.value().second;
		_webFilesMap[url].second = size;
	}
}

class WebFileLoadTask : public Task {
public:
	WebFileLoadTask(const FileKey &key, const QString &url, webFileLoader *loader)
		: _key(key)
		, _url(url)
		, _loader(loader)
		, _result(0) {
	}
	void process() {
		FileReadDescriptor image;
		if (!readEncryptedFile(image, _key, UserPath)) {
			return;
		}

		QByteArray imageData;
		QString url;
		image.stream >> url >> imageData;

		_result = new Result(StorageFilePartial, imageData);
	}
	void finish() {
		if (_result) {
			_loader->localLoaded(_result->image, _result->format, _result->pixmap);
		} else {
			WebFilesMap::iterator j = _webFilesMap.find(_url);
			if (j != _webFilesMap.cend() && j->first == _key) {
				clearKey(j.value().first, UserPath);
				_storageWebFilesSize -= j.value().second;
				_webFilesMap.erase(j);
			}
			_loader->localLoaded(StorageImageSaved());
		}
	}
	virtual ~WebFileLoadTask() {
		delete base::take(_result);
	}

protected:
	FileKey _key;
	QString _url;
	struct Result {
		Result(StorageFileType type, const QByteArray &data) : image(type, data) {
			QByteArray guessFormat;
			pixmap = App::pixmapFromImageInPlace(App::readImage(data, &guessFormat, false));
			if (!pixmap.isNull()) {
				format = guessFormat;
			}
		}
		StorageImageSaved image;
		QByteArray format;
		QPixmap pixmap;
	};
	webFileLoader *_loader;
	Result *_result;

};

TaskId startWebFileLoad(const QString &url, webFileLoader *loader) {
	WebFilesMap::const_iterator j = _webFilesMap.constFind(url);
	if (j == _webFilesMap.cend() || !_localLoader) {
		return 0;
	}
	return _localLoader->addTask(new WebFileLoadTask(j->first, url, loader));
}

int32 hasWebFiles() {
	return _webFilesMap.size();
}

qint64 storageWebFilesSize() {
	return _storageWebFilesSize;
}

class CountWaveformTask : public Task {
public:
	CountWaveformTask(DocumentData *doc)
		: _doc(doc)
		, _loc(doc->location(true))
		, _data(doc->data())
		, _wavemax(0) {
		if (_data.isEmpty() && !_loc.accessEnable()) {
			_doc = 0;
		}
	}
	void process() {
		if (!_doc) return;

		_waveform = audioCountWaveform(_loc, _data);
		uchar wavemax = 0;
		for (int32 i = 0, l = _waveform.size(); i < l; ++i) {
			uchar waveat = _waveform.at(i);
			if (wavemax < waveat) wavemax = waveat;
		}
		_wavemax = wavemax;
	}
	void finish() {
		if (VoiceData *voice = _doc ? _doc->voice() : 0) {
			if (!_waveform.isEmpty()) {
				voice->waveform = _waveform;
				voice->wavemax = _wavemax;
			}
			if (voice->waveform.isEmpty()) {
				voice->waveform.resize(1);
				voice->waveform[0] = -2;
				voice->wavemax = 0;
			} else if (voice->waveform[0] < 0) {
				voice->waveform[0] = -2;
				voice->wavemax = 0;
			}
			auto &items = App::documentItems();
			auto i = items.constFind(_doc);
			if (i != items.cend()) {
				for_const (auto item, i.value()) {
					Ui::repaintHistoryItem(item);
				}
			}
		}
	}
	virtual ~CountWaveformTask() {
		if (_data.isEmpty() && _doc) {
			_loc.accessDisable();
		}
	}

protected:
	DocumentData *_doc;
	FileLocation _loc;
	QByteArray _data;
	VoiceWaveform _waveform;
	char _wavemax;

};

void countVoiceWaveform(DocumentData *document) {
	if (VoiceData *voice = document->voice()) {
		if (_localLoader) {
			voice->waveform.resize(1 + sizeof(TaskId));
			voice->waveform[0] = -1; // counting
			TaskId taskId = _localLoader->addTask(new CountWaveformTask(document));
			memcpy(voice->waveform.data() + 1, &taskId, sizeof(taskId));
		}
	}
}

void cancelTask(TaskId id) {
	if (_localLoader) {
		_localLoader->cancelTask(id);
	}
}

void _writeStickerSet(QDataStream &stream, const Stickers::Set &set) {
	bool notLoaded = (set.flags & MTPDstickerSet_ClientFlag::f_not_loaded);
	if (notLoaded) {
		stream << quint64(set.id) << quint64(set.access) << set.title << set.shortName << qint32(-set.count) << qint32(set.hash) << qint32(set.flags);
		return;
	} else {
		if (set.stickers.isEmpty()) return;
	}

	stream << quint64(set.id) << quint64(set.access) << set.title << set.shortName << qint32(set.stickers.size()) << qint32(set.hash) << qint32(set.flags);
	for (StickerPack::const_iterator j = set.stickers.cbegin(), e = set.stickers.cend(); j != e; ++j) {
		Serialize::Document::writeToStream(stream, *j);
	}

	if (AppVersion > 9018) {
		stream << qint32(set.emoji.size());
		for (StickersByEmojiMap::const_iterator j = set.emoji.cbegin(), e = set.emoji.cend(); j != e; ++j) {
			stream << emojiString(j.key()) << qint32(j->size());
			for (int32 k = 0, l = j->size(); k < l; ++k) {
				stream << quint64(j->at(k)->id);
			}
		}
	}
}

// In generic method _writeStickerSets() we look through all the sets and call a
// callback on each set to see, if we write it, skip it or abort the whole write.
enum class StickerSetCheckResult {
	Write,
	Skip,
	Abort,
};

// CheckSet is a functor on Stickers::Set, which returns a StickerSetCheckResult.
template <typename CheckSet>
void _writeStickerSets(FileKey &stickersKey, CheckSet checkSet, const Stickers::Order &order) {
	if (!_working()) return;

	auto &sets = Global::StickerSets();
	if (sets.isEmpty()) {
		if (stickersKey) {
			clearKey(stickersKey);
			stickersKey = 0;
			_mapChanged = true;
		}
		_writeMap();
		return;
	}
	int32 setsCount = 0;
	QByteArray hashToWrite;
	quint32 size = sizeof(quint32) + Serialize::bytearraySize(hashToWrite);
	for_const (auto &set, sets) {
		auto result = checkSet(set);
		if (result == StickerSetCheckResult::Abort) {
			return;
		} else if (result == StickerSetCheckResult::Skip) {
			continue;
		}

		// id + access + title + shortName + stickersCount + hash + flags
		size += sizeof(quint64) * 2 + Serialize::stringSize(set.title) + Serialize::stringSize(set.shortName) + sizeof(quint32) + sizeof(qint32) * 2;
		for_const (auto &sticker, set.stickers) {
			size += Serialize::Document::sizeInStream(sticker);
		}

		size += sizeof(qint32); // emojiCount
		for (auto j = set.emoji.cbegin(), e = set.emoji.cend(); j != e; ++j) {
			size += Serialize::stringSize(emojiString(j.key())) + sizeof(qint32) + (j->size() * sizeof(quint64));
		}

		++setsCount;
	}
	if (!setsCount && order.isEmpty()) {
		if (stickersKey) {
			clearKey(stickersKey);
			stickersKey = 0;
			_mapChanged = true;
		}
		_writeMap();
		return;
	}
	size += sizeof(qint32) + (order.size() * sizeof(quint64));

	if (!stickersKey) {
		stickersKey = genKey();
		_mapChanged = true;
		_writeMap(WriteMapFast);
	}
	EncryptedDescriptor data(size);
	data.stream << quint32(setsCount) << hashToWrite;
	for_const (auto &set, sets) {
		auto result = checkSet(set);
		if (result == StickerSetCheckResult::Abort) {
			return;
		} else if (result == StickerSetCheckResult::Skip) {
			continue;
		}
		_writeStickerSet(data.stream, set);
	}
	data.stream << order;

	FileWriteDescriptor file(stickersKey);
	file.writeEncrypted(data);
}

void _readStickerSets(FileKey &stickersKey, Stickers::Order *outOrder = nullptr, MTPDstickerSet::Flags readingFlags = 0) {
	FileReadDescriptor stickers;
	if (!readEncryptedFile(stickers, stickersKey)) {
		clearKey(stickersKey);
		stickersKey = 0;
		_writeMap();
		return;
	}

	bool readingInstalled = (readingFlags == qFlags(MTPDstickerSet::Flag::f_installed));

	auto &sets = Global::RefStickerSets();
	if (outOrder) outOrder->clear();

	quint32 cnt;
	QByteArray hash;
	stickers.stream >> cnt >> hash; // ignore hash, it is counted
	if (readingInstalled && stickers.version < 8019) { // bad data in old caches
		cnt += 2; // try to read at least something
	}
	for (uint32 i = 0; i < cnt; ++i) {
		quint64 setId = 0, setAccess = 0;
		QString setTitle, setShortName;
		qint32 scnt = 0;
		stickers.stream >> setId >> setAccess >> setTitle >> setShortName >> scnt;

		qint32 setHash = 0, setFlags = 0;
		if (stickers.version > 8033) {
			stickers.stream >> setHash >> setFlags;
			if (setFlags & qFlags(MTPDstickerSet_ClientFlag::f_not_loaded__old)) {
				setFlags &= ~qFlags(MTPDstickerSet_ClientFlag::f_not_loaded__old);
				setFlags |= qFlags(MTPDstickerSet_ClientFlag::f_not_loaded);
			}
		}
		if (readingInstalled && stickers.version < 9061) {
			setFlags |= qFlags(MTPDstickerSet::Flag::f_installed);
		}

		if (setId == Stickers::DefaultSetId) {
			setTitle = lang(lng_stickers_default_set);
			setFlags |= qFlags(MTPDstickerSet::Flag::f_official | MTPDstickerSet_ClientFlag::f_special);
			if (readingInstalled && outOrder && stickers.version < 9061) {
				outOrder->push_front(setId);
			}
		} else if (setId == Stickers::CustomSetId) {
			setTitle = lang(lng_custom_stickers);
			setFlags |= qFlags(MTPDstickerSet_ClientFlag::f_special);
		} else if (setId == Stickers::CloudRecentSetId) {
			setTitle = lang(lng_recent_stickers);
			setFlags |= qFlags(MTPDstickerSet_ClientFlag::f_special);
		} else if (setId) {
			if (readingInstalled && outOrder && stickers.version < 9061) {
				outOrder->push_back(setId);
			}
		} else {
			continue;
		}

		auto it = sets.find(setId);
		if (it == sets.cend()) {
			// We will set this flags from order lists when reading those stickers.
			setFlags &= ~(MTPDstickerSet::Flag::f_installed | MTPDstickerSet_ClientFlag::f_featured);
			it = sets.insert(setId, Stickers::Set(setId, setAccess, setTitle, setShortName, 0, setHash, MTPDstickerSet::Flags(setFlags)));
		}
		auto &set = it.value();
		auto inputSet = MTP_inputStickerSetID(MTP_long(set.id), MTP_long(set.access));

		if (scnt < 0) { // disabled not loaded set
			if (!set.count || set.stickers.isEmpty()) {
				set.count = -scnt;
			}
			continue;
		}

		bool fillStickers = set.stickers.isEmpty();
		if (fillStickers) {
			set.stickers.reserve(scnt);
			set.count = 0;
		}

		Serialize::Document::StickerSetInfo info(setId, setAccess, setShortName);
		OrderedSet<DocumentId> read;
		for (int32 j = 0; j < scnt; ++j) {
			auto document = Serialize::Document::readStickerFromStream(stickers.version, stickers.stream, info);
			if (!document || !document->sticker()) continue;

			if (read.contains(document->id)) continue;
			read.insert(document->id);

			if (fillStickers) {
				set.stickers.push_back(document);
				if (!(set.flags & MTPDstickerSet_ClientFlag::f_special)) {
					if (document->sticker()->set.type() != mtpc_inputStickerSetID) {
						document->sticker()->set = inputSet;
					}
				}
				++set.count;
			}
		}

		if (stickers.version > 9018) {
			qint32 emojiCount;
			stickers.stream >> emojiCount;
			for (int32 j = 0; j < emojiCount; ++j) {
				QString emojiString;
				qint32 stickersCount;
				stickers.stream >> emojiString >> stickersCount;
				StickerPack pack;
				pack.reserve(stickersCount);
				for (int32 k = 0; k < stickersCount; ++k) {
					quint64 id;
					stickers.stream >> id;
					DocumentData *doc = App::document(id);
					if (!doc || !doc->sticker()) continue;

					pack.push_back(doc);
				}
				if (fillStickers) {
					if (auto e = emojiGetNoColor(emojiFromText(emojiString))) {
						set.emoji.insert(e, pack);
					}
				}
			}
		}
	}

	// Read orders of installed and featured stickers.
	if (outOrder && stickers.version >= 9061) {
		stickers.stream >> *outOrder;
	}

	// Set flags that we dropped above from the order.
	if (readingFlags && outOrder) {
		for_const (auto setId, *outOrder) {
			auto it = sets.find(setId);
			if (it != sets.cend()) {
				it->flags |= readingFlags;
			}
		}
	}
}

void writeInstalledStickers() {
	if (!Global::started()) return;

	_writeStickerSets(_installedStickersKey, [](const Stickers::Set &set) {
		if (set.id == Stickers::CloudRecentSetId) { // separate file for recent
			return StickerSetCheckResult::Skip;
		} else if (set.flags & MTPDstickerSet_ClientFlag::f_special) {
			if (set.stickers.isEmpty()) { // all other special are "installed"
				return StickerSetCheckResult::Skip;
			}
		} else if (!(set.flags & MTPDstickerSet::Flag::f_installed) || (set.flags & MTPDstickerSet::Flag::f_archived)) {
			return StickerSetCheckResult::Skip;
		} else if (set.flags & MTPDstickerSet_ClientFlag::f_not_loaded) { // waiting to receive
			return StickerSetCheckResult::Abort;
		} else if (set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Global::StickerSetsOrder());
}

void writeFeaturedStickers() {
	if (!Global::started()) return;

	_writeStickerSets(_featuredStickersKey, [](const Stickers::Set &set) {
		if (set.id == Stickers::CloudRecentSetId) { // separate file for recent
			return StickerSetCheckResult::Skip;
		} else if (set.flags & MTPDstickerSet_ClientFlag::f_special) {
			return StickerSetCheckResult::Skip;
		} else if (!(set.flags & MTPDstickerSet_ClientFlag::f_featured)) {
			return StickerSetCheckResult::Skip;
		} else if (set.flags & MTPDstickerSet_ClientFlag::f_not_loaded) { // waiting to receive
			return StickerSetCheckResult::Abort;
		} else if (set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Global::FeaturedStickerSetsOrder());
}

void writeRecentStickers() {
	if (!Global::started()) return;

	_writeStickerSets(_recentStickersKey, [](const Stickers::Set &set) {
		if (set.id != Stickers::CloudRecentSetId || set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Stickers::Order());
}

void writeArchivedStickers() {
	if (!Global::started()) return;

	_writeStickerSets(_archivedStickersKey, [](const Stickers::Set &set) {
		if (!(set.flags & MTPDstickerSet::Flag::f_archived) || set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Global::ArchivedStickerSetsOrder());
}

void importOldRecentStickers() {
	if (!_recentStickersKeyOld) return;

	FileReadDescriptor stickers;
	if (!readEncryptedFile(stickers, _recentStickersKeyOld)) {
		clearKey(_recentStickersKeyOld);
		_recentStickersKeyOld = 0;
		_writeMap();
		return;
	}

	auto &sets = Global::RefStickerSets();
	sets.clear();

	auto &order = Global::RefStickerSetsOrder();
	order.clear();

	auto &recent = cRefRecentStickers();
	recent.clear();

	auto &def = sets.insert(Stickers::DefaultSetId, Stickers::Set(Stickers::DefaultSetId, 0, lang(lng_stickers_default_set), QString(), 0, 0, MTPDstickerSet::Flag::f_official | MTPDstickerSet::Flag::f_installed | MTPDstickerSet_ClientFlag::f_special)).value();
	auto &custom = sets.insert(Stickers::CustomSetId, Stickers::Set(Stickers::CustomSetId, 0, lang(lng_custom_stickers), QString(), 0, 0, MTPDstickerSet::Flag::f_installed | MTPDstickerSet_ClientFlag::f_special)).value();

	QMap<uint64, bool> read;
	while (!stickers.stream.atEnd()) {
		quint64 id, access;
		QString name, mime, alt;
		qint32 date, dc, size, width, height, type;
		qint16 value;
		stickers.stream >> id >> value >> access >> date >> name >> mime >> dc >> size >> width >> height >> type;
		if (stickers.version >= 7021) {
			stickers.stream >> alt;
		}
		if (!value || read.contains(id)) continue;
		read.insert(id, true);

		QVector<MTPDocumentAttribute> attributes;
		if (!name.isEmpty()) attributes.push_back(MTP_documentAttributeFilename(MTP_string(name)));
		if (type == AnimatedDocument) {
			attributes.push_back(MTP_documentAttributeAnimated());
		} else if (type == StickerDocument) {
			MTPDdocumentAttributeSticker::Flags stickerFlags = 0;
			attributes.push_back(MTP_documentAttributeSticker(MTP_flags(stickerFlags), MTP_string(alt), MTP_inputStickerSetEmpty(), MTPMaskCoords()));
		}
		if (width > 0 && height > 0) {
			attributes.push_back(MTP_documentAttributeImageSize(MTP_int(width), MTP_int(height)));
		}

		DocumentData *doc = App::documentSet(id, 0, access, 0, date, attributes, mime, ImagePtr(), dc, size, StorageImageLocation());
		if (!doc->sticker()) continue;

		if (value > 0) {
			def.stickers.push_back(doc);
			++def.count;
		} else {
			custom.stickers.push_back(doc);
			++custom.count;
		}
		if (recent.size() < Global::StickersRecentLimit() && qAbs(value) > 1) {
			recent.push_back(qMakePair(doc, qAbs(value)));
		}
	}
	if (def.stickers.isEmpty()) {
		sets.remove(Stickers::DefaultSetId);
	} else {
		order.push_front(Stickers::DefaultSetId);
	}
	if (custom.stickers.isEmpty()) sets.remove(Stickers::CustomSetId);

	writeInstalledStickers();
	writeUserSettings();

	clearKey(_recentStickersKeyOld);
	_recentStickersKeyOld = 0;
	_writeMap();
}

void readInstalledStickers() {
	if (!_installedStickersKey) {
		return importOldRecentStickers();
	}

	Global::RefStickerSets().clear();
	_readStickerSets(_installedStickersKey, &Global::RefStickerSetsOrder(), qFlags(MTPDstickerSet::Flag::f_installed));
}

void readFeaturedStickers() {
	_readStickerSets(_featuredStickersKey, &Global::RefFeaturedStickerSetsOrder(), qFlags(MTPDstickerSet_ClientFlag::f_featured));

	auto &sets = Global::StickerSets();
	int unreadCount = 0;
	for_const (auto setId, Global::FeaturedStickerSetsOrder()) {
		auto it = sets.constFind(setId);
		if (it != sets.cend() && (it->flags & MTPDstickerSet_ClientFlag::f_unread)) {
			++unreadCount;
		}
	}
	Global::SetFeaturedStickerSetsUnreadCount(unreadCount);
}

void readRecentStickers() {
	_readStickerSets(_recentStickersKey);
}

void readArchivedStickers() {
	static bool archivedStickersRead = false;
	if (!archivedStickersRead) {
		_readStickerSets(_archivedStickersKey, &Global::RefArchivedStickerSetsOrder());
		archivedStickersRead = true;
	}
}

int32 countStickersHash(bool checkOutdatedInfo) {
	uint32 acc = 0;
	bool foundOutdated = false;
	auto &sets = Global::StickerSets();
	auto &order = Global::StickerSetsOrder();
	for (auto i = order.cbegin(), e = order.cend(); i != e; ++i) {
		auto j = sets.constFind(*i);
		if (j != sets.cend()) {
			if (j->id == Stickers::DefaultSetId) {
				foundOutdated = true;
			} else if (!(j->flags & MTPDstickerSet_ClientFlag::f_special)
				&& !(j->flags & MTPDstickerSet::Flag::f_archived)) {
				acc = (acc * 20261) + j->hash;
			}
		}
	}
	return (!checkOutdatedInfo || !foundOutdated) ? int32(acc & 0x7FFFFFFF) : 0;
}

int32 countRecentStickersHash() {
	uint32 acc = 0;
	auto &sets = Global::StickerSets();
	auto it = sets.constFind(Stickers::CloudRecentSetId);
	if (it != sets.cend()) {
		for_const (auto doc, it->stickers) {
			auto docId = doc->id;
			acc = (acc * 20261) + uint32(docId >> 32);
			acc = (acc * 20261) + uint32(docId & 0xFFFFFFFF);
		}
	}
	return int32(acc & 0x7FFFFFFF);
}

int32 countFeaturedStickersHash() {
	uint32 acc = 0;
	auto &sets = Global::StickerSets();
	auto &featured = Global::FeaturedStickerSetsOrder();
	for_const (auto setId, featured) {
		acc = (acc * 20261) + uint32(setId >> 32);
		acc = (acc * 20261) + uint32(setId & 0xFFFFFFFF);

		auto it = sets.constFind(setId);
		if (it != sets.cend() && (it->flags & MTPDstickerSet_ClientFlag::f_unread)) {
			acc = (acc * 20261) + 1U;
		}
	}
	return int32(acc & 0x7FFFFFFF);
}

int32 countSavedGifsHash() {
	uint32 acc = 0;
	auto &saved = cSavedGifs();
	for_const (auto doc, saved) {
		auto docId = doc->id;
		acc = (acc * 20261) + uint32(docId >> 32);
		acc = (acc * 20261) + uint32(docId & 0xFFFFFFFF);
	}
	return int32(acc & 0x7FFFFFFF);
}

void writeSavedGifs() {
	if (!_working()) return;

	const SavedGifs &saved(cSavedGifs());
	if (saved.isEmpty()) {
		if (_savedGifsKey) {
			clearKey(_savedGifsKey);
			_savedGifsKey = 0;
			_mapChanged = true;
		}
		_writeMap();
	} else {
		quint32 size = sizeof(quint32); // count
		for_const (auto gif, saved) {
			size += Serialize::Document::sizeInStream(gif);
		}

		if (!_savedGifsKey) {
			_savedGifsKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapFast);
		}
		EncryptedDescriptor data(size);
		data.stream << quint32(saved.size());
		for_const (auto gif, saved) {
			Serialize::Document::writeToStream(data.stream, gif);
		}
		FileWriteDescriptor file(_savedGifsKey);
		file.writeEncrypted(data);
	}
}

void readSavedGifs() {
	if (!_savedGifsKey) return;

	FileReadDescriptor gifs;
	if (!readEncryptedFile(gifs, _savedGifsKey)) {
		clearKey(_savedGifsKey);
		_savedGifsKey = 0;
		_writeMap();
		return;
	}

	SavedGifs &saved(cRefSavedGifs());
	saved.clear();

	quint32 cnt;
	gifs.stream >> cnt;
	saved.reserve(cnt);
	OrderedSet<DocumentId> read;
	for (uint32 i = 0; i < cnt; ++i) {
		DocumentData *document = Serialize::Document::readFromStream(gifs.version, gifs.stream);
		if (!document || !document->isAnimation()) continue;

		if (read.contains(document->id)) continue;
		read.insert(document->id);

		saved.push_back(document);
	}
}

void writeBackground(int32 id, const QImage &img) {
	if (!_working()) return;

	QByteArray png;
	if (!img.isNull()) {
		QBuffer buf(&png);
		if (!img.save(&buf, "BMP")) return;
	}
	if (!_backgroundKey) {
		_backgroundKey = genKey();
		_mapChanged = true;
		_writeMap(WriteMapFast);
	}
	quint32 size = sizeof(qint32) + sizeof(quint32) + (png.isEmpty() ? 0 : (sizeof(quint32) + png.size()));
	EncryptedDescriptor data(size);
	data.stream << qint32(id);
	if (!png.isEmpty()) data.stream << png;

	FileWriteDescriptor file(_backgroundKey);
	file.writeEncrypted(data);
}

bool readBackground() {
	if (_backgroundWasRead) return false;
	_backgroundWasRead = true;

	FileReadDescriptor bg;
	if (!readEncryptedFile(bg, _backgroundKey)) {
		clearKey(_backgroundKey);
		_backgroundKey = 0;
		_writeMap();
		return false;
	}

	QByteArray pngData;
	qint32 id;
	bg.stream >> id;
	if (!id || id == DefaultChatBackground) {
		if (bg.version < 8005) {
			App::initBackground(DefaultChatBackground, QImage(), true);
			if (!id) Window::chatBackground()->setTile(!DefaultChatBackground);
		} else {
			App::initBackground(id, QImage(), true);
		}
		return true;
	}
	bg.stream >> pngData;

	QImage img;
	QBuffer buf(&pngData);
	QImageReader reader(&buf);
#ifndef OS_MAC_OLD
	reader.setAutoTransform(true);
#endif // OS_MAC_OLD
	if (reader.read(&img)) {
		App::initBackground(id, img, true);
		return true;
	}
	return false;
}

uint32 _peerSize(PeerData *peer) {
	uint32 result = sizeof(quint64) + sizeof(quint64) + Serialize::storageImageLocationSize();
	if (peer->isUser()) {
		UserData *user = peer->asUser();

		// first + last + phone + username + access
		result += Serialize::stringSize(user->firstName) + Serialize::stringSize(user->lastName) + Serialize::stringSize(user->phone()) + Serialize::stringSize(user->username) + sizeof(quint64);

		// flags
		if (AppVersion >= 9012) {
			result += sizeof(qint32);
		}

		// onlineTill + contact + botInfoVersion
		result += sizeof(qint32) + sizeof(qint32) + sizeof(qint32);
	} else if (peer->isChat()) {
		ChatData *chat = peer->asChat();

		// name + count + date + version + admin + forbidden + left + inviteLink
		result += Serialize::stringSize(chat->name) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + Serialize::stringSize(chat->inviteLink());
	} else if (peer->isChannel()) {
		ChannelData *channel = peer->asChannel();

		// name + access + date + version + forbidden + flags + inviteLink
		result += Serialize::stringSize(channel->name) + sizeof(quint64) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + Serialize::stringSize(channel->inviteLink());
	}
	return result;
}

void _writePeer(QDataStream &stream, PeerData *peer) {
	stream << quint64(peer->id) << quint64(peer->photoId);
	Serialize::writeStorageImageLocation(stream, peer->photoLoc);
	if (peer->isUser()) {
		UserData *user = peer->asUser();

		stream << user->firstName << user->lastName << user->phone() << user->username << quint64(user->access);
		if (AppVersion >= 9012) {
			stream << qint32(user->flags);
		}
		if (AppVersion >= 9016) {
			stream << (user->botInfo ? user->botInfo->inlinePlaceholder : QString());
		}
		stream << qint32(user->onlineTill) << qint32(user->contact) << qint32(user->botInfo ? user->botInfo->version : -1);
	} else if (peer->isChat()) {
		ChatData *chat = peer->asChat();

		qint32 flagsData = (AppVersion >= 9012) ? chat->flags : (chat->haveLeft() ? 1 : 0);

		stream << chat->name << qint32(chat->count) << qint32(chat->date) << qint32(chat->version) << qint32(chat->creator);
		stream << qint32(chat->isForbidden ? 1 : 0) << qint32(flagsData) << chat->inviteLink();
	} else if (peer->isChannel()) {
		ChannelData *channel = peer->asChannel();

		stream << channel->name << quint64(channel->access) << qint32(channel->date) << qint32(channel->version);
		stream << qint32(channel->isForbidden ? 1 : 0) << qint32(channel->flags) << channel->inviteLink();
	}
}

PeerData *_readPeer(FileReadDescriptor &from, int32 fileVersion = 0) {
	quint64 peerId = 0, photoId = 0;
	from.stream >> peerId >> photoId;

	StorageImageLocation photoLoc(Serialize::readStorageImageLocation(from.stream));

	PeerData *result = App::peerLoaded(peerId);
	bool wasLoaded = (result != nullptr);
	if (!wasLoaded) {
		result = App::peer(peerId);
		result->loadedStatus = PeerData::FullLoaded;
	}
	if (result->isUser()) {
		UserData *user = result->asUser();

		QString first, last, phone, username, inlinePlaceholder;
		quint64 access;
		qint32 flags = 0, onlineTill, contact, botInfoVersion;
		from.stream >> first >> last >> phone >> username >> access;
		if (from.version >= 9012) {
			from.stream >> flags;
		}
		if (from.version >= 9016 || fileVersion >= 9016) {
			from.stream >> inlinePlaceholder;
		}
		from.stream >> onlineTill >> contact >> botInfoVersion;

		bool showPhone = !isServiceUser(user->id) && (peerToUser(user->id) != MTP::authedId()) && (contact <= 0);
		QString pname = (showPhone && !phone.isEmpty()) ? App::formatPhone(phone) : QString();

		if (!wasLoaded) {
			user->setPhone(phone);
			user->setName(first, last, pname, username);

			user->access = access;
			user->flags = MTPDuser::Flags(flags);
			user->onlineTill = onlineTill;
			user->contact = contact;
			user->setBotInfoVersion(botInfoVersion);
			if (!inlinePlaceholder.isEmpty() && user->botInfo) {
				user->botInfo->inlinePlaceholder = inlinePlaceholder;
			}

			if (peerToUser(user->id) == MTP::authedId()) {
				user->input = MTP_inputPeerSelf();
				user->inputUser = MTP_inputUserSelf();
			} else {
				user->input = MTP_inputPeerUser(MTP_int(peerToUser(user->id)), MTP_long((user->access == UserNoAccess) ? 0 : user->access));
				user->inputUser = MTP_inputUser(MTP_int(peerToUser(user->id)), MTP_long((user->access == UserNoAccess) ? 0 : user->access));
			}

			user->setUserpic(photoLoc.isNull() ? ImagePtr(userDefPhoto(user->colorIndex)) : ImagePtr(photoLoc));
		}
	} else if (result->isChat()) {
		ChatData *chat = result->asChat();

		QString name, inviteLink;
		qint32 count, date, version, creator, forbidden, flagsData, flags;
		from.stream >> name >> count >> date >> version >> creator >> forbidden >> flagsData >> inviteLink;

		if (from.version >= 9012) {
			flags = flagsData;
		} else {
			// flagsData was haveLeft
			flags = (flagsData == 1) ? MTPDchat::Flags(MTPDchat::Flag::f_left) : MTPDchat::Flags(0);
		}
		if (!wasLoaded) {
			chat->setName(name);
			chat->count = count;
			chat->date = date;
			chat->version = version;
			chat->creator = creator;
			chat->isForbidden = (forbidden == 1);
			chat->flags = MTPDchat::Flags(flags);
			chat->setInviteLink(inviteLink);

			chat->input = MTP_inputPeerChat(MTP_int(peerToChat(chat->id)));
			chat->inputChat = MTP_int(peerToChat(chat->id));

			chat->setUserpic(photoLoc.isNull() ? ImagePtr(chatDefPhoto(chat->colorIndex)) : ImagePtr(photoLoc));
		}
	} else if (result->isChannel()) {
		ChannelData *channel = result->asChannel();

		QString name, inviteLink;
		quint64 access;
		qint32 date, version, forbidden, flags;
		from.stream >> name >> access >> date >> version >> forbidden >> flags >> inviteLink;

		if (!wasLoaded) {
			channel->setName(name, QString());
			channel->access = access;
			channel->date = date;
			channel->version = version;
			channel->isForbidden = (forbidden == 1);
			channel->flags = MTPDchannel::Flags(flags);
			channel->setInviteLink(inviteLink);

			channel->input = MTP_inputPeerChannel(MTP_int(peerToChannel(channel->id)), MTP_long(access));
			channel->inputChannel = MTP_inputChannel(MTP_int(peerToChannel(channel->id)), MTP_long(access));

			channel->setUserpic(photoLoc.isNull() ? ImagePtr((channel->isMegagroup() ? chatDefPhoto(channel->colorIndex) : channelDefPhoto(channel->colorIndex))) : ImagePtr(photoLoc));
		}
	}
	if (!wasLoaded) {
		App::markPeerUpdated(result);
		emit App::main()->peerPhotoChanged(result);
	}
	return result;
}

void writeRecentHashtagsAndBots() {
	if (!_working()) return;

	const RecentHashtagPack &write(cRecentWriteHashtags()), &search(cRecentSearchHashtags());
	const RecentInlineBots &bots(cRecentInlineBots());
	if (write.isEmpty() && search.isEmpty() && bots.isEmpty()) readRecentHashtagsAndBots();
	if (write.isEmpty() && search.isEmpty() && bots.isEmpty()) {
		if (_recentHashtagsAndBotsKey) {
			clearKey(_recentHashtagsAndBotsKey);
			_recentHashtagsAndBotsKey = 0;
			_mapChanged = true;
		}
		_writeMap();
	} else {
		if (!_recentHashtagsAndBotsKey) {
			_recentHashtagsAndBotsKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapFast);
		}
		quint32 size = sizeof(quint32) * 3, writeCnt = 0, searchCnt = 0, botsCnt = cRecentInlineBots().size();
		for (RecentHashtagPack::const_iterator i = write.cbegin(), e = write.cend(); i != e;  ++i) {
			if (!i->first.isEmpty()) {
				size += Serialize::stringSize(i->first) + sizeof(quint16);
				++writeCnt;
			}
		}
		for (RecentHashtagPack::const_iterator i = search.cbegin(), e = search.cend(); i != e; ++i) {
			if (!i->first.isEmpty()) {
				size += Serialize::stringSize(i->first) + sizeof(quint16);
				++searchCnt;
			}
		}
		for (RecentInlineBots::const_iterator i = bots.cbegin(), e = bots.cend(); i != e; ++i) {
			size += _peerSize(*i);
		}

		EncryptedDescriptor data(size);
		data.stream << quint32(writeCnt) << quint32(searchCnt);
		for (RecentHashtagPack::const_iterator i = write.cbegin(), e = write.cend(); i != e; ++i) {
			if (!i->first.isEmpty()) data.stream << i->first << quint16(i->second);
		}
		for (RecentHashtagPack::const_iterator i = search.cbegin(), e = search.cend(); i != e; ++i) {
			if (!i->first.isEmpty()) data.stream << i->first << quint16(i->second);
		}
		data.stream << quint32(botsCnt);
		for (RecentInlineBots::const_iterator i = bots.cbegin(), e = bots.cend(); i != e; ++i) {
			_writePeer(data.stream, *i);
		}
		FileWriteDescriptor file(_recentHashtagsAndBotsKey);
		file.writeEncrypted(data);
	}
}

void readRecentHashtagsAndBots() {
	if (_recentHashtagsAndBotsWereRead) return;
	_recentHashtagsAndBotsWereRead = true;

	if (!_recentHashtagsAndBotsKey) return;

	FileReadDescriptor hashtags;
	if (!readEncryptedFile(hashtags, _recentHashtagsAndBotsKey)) {
		clearKey(_recentHashtagsAndBotsKey);
		_recentHashtagsAndBotsKey = 0;
		_writeMap();
		return;
	}

	quint32 writeCount = 0, searchCount = 0, botsCount = 0;
	hashtags.stream >> writeCount >> searchCount;

	QString tag;
	quint16 count;

	RecentHashtagPack write, search;
	RecentInlineBots bots;
	if (writeCount) {
		write.reserve(writeCount);
		for (uint32 i = 0; i < writeCount; ++i) {
			hashtags.stream >> tag >> count;
			write.push_back(qMakePair(tag.trimmed(), count));
		}
	}
	if (searchCount) {
		search.reserve(searchCount);
		for (uint32 i = 0; i < searchCount; ++i) {
			hashtags.stream >> tag >> count;
			search.push_back(qMakePair(tag.trimmed(), count));
		}
	}
	cSetRecentWriteHashtags(write);
	cSetRecentSearchHashtags(search);

	if (!hashtags.stream.atEnd()) {
		hashtags.stream >> botsCount;
		if (botsCount) {
			bots.reserve(botsCount);
			for (uint32 i = 0; i < botsCount; ++i) {
				PeerData *peer = _readPeer(hashtags, 9016);
				if (peer && peer->isUser() && peer->asUser()->botInfo && !peer->asUser()->botInfo->inlinePlaceholder.isEmpty() && !peer->asUser()->username.isEmpty()) {
					bots.push_back(peer->asUser());
				}
			}
		}
		cSetRecentInlineBots(bots);
	}
}

void writeSavedPeers() {
	if (!_working()) return;

	const SavedPeers &saved(cSavedPeers());
	if (saved.isEmpty()) {
		if (_savedPeersKey) {
			clearKey(_savedPeersKey);
			_savedPeersKey = 0;
			_mapChanged = true;
		}
		_writeMap();
	} else {
		if (!_savedPeersKey) {
			_savedPeersKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapFast);
		}
		quint32 size = sizeof(quint32);
		for (SavedPeers::const_iterator i = saved.cbegin(); i != saved.cend(); ++i) {
			size += _peerSize(i.key()) + Serialize::dateTimeSize();
		}

		EncryptedDescriptor data(size);
		data.stream << quint32(saved.size());
		for (SavedPeers::const_iterator i = saved.cbegin(); i != saved.cend(); ++i) {
			_writePeer(data.stream, i.key());
			data.stream << i.value();
		}

		FileWriteDescriptor file(_savedPeersKey);
		file.writeEncrypted(data);
	}
}

void readSavedPeers() {
	if (!_savedPeersKey) return;

	FileReadDescriptor saved;
	if (!readEncryptedFile(saved, _savedPeersKey)) {
		clearKey(_savedPeersKey);
		_savedPeersKey = 0;
		_writeMap();
		return;
	}
	if (saved.version == 9011) { // broken dev version
		clearKey(_savedPeersKey);
		_savedPeersKey = 0;
		_writeMap();
		return;
	}

	quint32 count = 0;
	saved.stream >> count;
	cRefSavedPeers().clear();
	cRefSavedPeersByTime().clear();
	QList<PeerData*> peers;
	peers.reserve(count);
	for (uint32 i = 0; i < count; ++i) {
		PeerData *peer = _readPeer(saved);
		if (!peer) break;

		QDateTime t;
		saved.stream >> t;

		cRefSavedPeers().insert(peer, t);
		cRefSavedPeersByTime().insert(t, peer);
		peers.push_back(peer);
	}

	if (App::api()) App::api()->requestPeers(peers);
}

void addSavedPeer(PeerData *peer, const QDateTime &position) {
	auto &savedPeers = cRefSavedPeers();
	auto i = savedPeers.find(peer);
	if (i == savedPeers.cend()) {
		savedPeers.insert(peer, position);
	} else if (i.value() != position) {
		cRefSavedPeersByTime().remove(i.value(), peer);
		i.value() = position;
		cRefSavedPeersByTime().insert(i.value(), peer);
	}
	writeSavedPeers();
}

void removeSavedPeer(PeerData *peer) {
	auto &savedPeers = cRefSavedPeers();
	if (savedPeers.isEmpty()) return;

	auto i = savedPeers.find(peer);
	if (i != savedPeers.cend()) {
		cRefSavedPeersByTime().remove(i.value(), peer);
		savedPeers.erase(i);

		writeSavedPeers();
	}
}

void writeReportSpamStatuses() {
	_writeReportSpamStatuses();
}

void writeTrustedBots() {
	if (!_working()) return;

	if (_trustedBots.isEmpty()) {
		if (_trustedBotsKey) {
			clearKey(_trustedBotsKey);
			_trustedBotsKey = 0;
			_mapChanged = true;
			_writeMap();
		}
	} else {
		if (!_trustedBotsKey) {
			_trustedBotsKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapFast);
		}
		quint32 size = sizeof(qint32) + _trustedBots.size() * sizeof(quint64);
		EncryptedDescriptor data(size);
		data.stream << qint32(_trustedBots.size());
		for_const (auto botId, _trustedBots) {
			data.stream << quint64(botId);
		}

		FileWriteDescriptor file(_trustedBotsKey);
		file.writeEncrypted(data);
	}
}

void readTrustedBots() {
	if (!_trustedBotsKey) return;

	FileReadDescriptor trusted;
	if (!readEncryptedFile(trusted, _trustedBotsKey)) {
		clearKey(_trustedBotsKey);
		_trustedBotsKey = 0;
		_writeMap();
		return;
	}

	qint32 size = 0;
	trusted.stream >> size;
	for (int i = 0; i < size; ++i) {
		quint64 botId = 0;
		trusted.stream >> botId;
		_trustedBots.insert(botId);
	}
}

void makeBotTrusted(UserData *bot) {
	if (!isBotTrusted(bot)) {
		_trustedBots.insert(bot->id);
		writeTrustedBots();
	}
}

bool isBotTrusted(UserData *bot) {
	if (!_trustedBotsRead) {
		readTrustedBots();
		_trustedBotsRead = true;
	}
	return _trustedBots.contains(bot->id);
}

bool encrypt(const void *src, void *dst, uint32 len, const void *key128) {
	if (!_localKey.created()) {
		return false;
	}
	MTP::aesEncryptLocal(src, dst, len, &_localKey, key128);
	return true;
}

bool decrypt(const void *src, void *dst, uint32 len, const void *key128) {
	if (!_localKey.created()) {
		return false;
	}
	MTP::aesDecryptLocal(src, dst, len, &_localKey, key128);
	return true;
}

struct ClearManagerData {
	QThread *thread;
	StorageMap images, stickers, audios;
	WebFilesMap webFiles;
	QMutex mutex;
	QList<int> tasks;
	bool working;
};

ClearManager::ClearManager() : data(new ClearManagerData()) {
	data->thread = new QThread();
	data->working = true;
}

bool ClearManager::addTask(int task) {
	QMutexLocker lock(&data->mutex);
	if (!data->working) return false;

	if (!data->tasks.isEmpty() && (data->tasks.at(0) == ClearManagerAll)) return true;
	if (task == ClearManagerAll) {
		data->tasks.clear();
		if (!_imagesMap.isEmpty()) {
			_imagesMap.clear();
			_storageImagesSize = 0;
			_mapChanged = true;
		}
		if (!_stickerImagesMap.isEmpty()) {
			_stickerImagesMap.clear();
			_storageStickersSize = 0;
			_mapChanged = true;
		}
		if (!_audiosMap.isEmpty()) {
			_audiosMap.clear();
			_storageAudiosSize = 0;
			_mapChanged = true;
		}
		if (!_draftsMap.isEmpty()) {
			_draftsMap.clear();
			_mapChanged = true;
		}
		if (!_draftCursorsMap.isEmpty()) {
			_draftCursorsMap.clear();
			_mapChanged = true;
		}
		if (_locationsKey) {
			_locationsKey = 0;
			_mapChanged = true;
		}
		if (_reportSpamStatusesKey) {
			_reportSpamStatusesKey = 0;
			_mapChanged = true;
		}
		if (_trustedBotsKey) {
			_trustedBotsKey = 0;
			_mapChanged = true;
		}
		if (_recentStickersKeyOld) {
			_recentStickersKeyOld = 0;
			_mapChanged = true;
		}
		if (_installedStickersKey || _featuredStickersKey || _recentStickersKey || _archivedStickersKey) {
			_installedStickersKey = _featuredStickersKey = _recentStickersKey = _archivedStickersKey = 0;
			_mapChanged = true;
		}
		if (_recentHashtagsAndBotsKey) {
			_recentHashtagsAndBotsKey = 0;
			_mapChanged = true;
		}
		if (_savedPeersKey) {
			_savedPeersKey = 0;
			_mapChanged = true;
		}
		_writeMap();
	} else {
		if (task & ClearManagerStorage) {
			if (data->images.isEmpty()) {
				data->images = _imagesMap;
			} else {
				for (StorageMap::const_iterator i = _imagesMap.cbegin(), e = _imagesMap.cend(); i != e; ++i) {
					StorageKey k = i.key();
					while (data->images.constFind(k) != data->images.cend()) {
						++k.second;
					}
					data->images.insert(k, i.value());
				}
			}
			if (!_imagesMap.isEmpty()) {
				_imagesMap.clear();
				_storageImagesSize = 0;
				_mapChanged = true;
			}
			if (data->stickers.isEmpty()) {
				data->stickers = _stickerImagesMap;
			} else {
				for (StorageMap::const_iterator i = _stickerImagesMap.cbegin(), e = _stickerImagesMap.cend(); i != e; ++i) {
					StorageKey k = i.key();
					while (data->stickers.constFind(k) != data->stickers.cend()) {
						++k.second;
					}
					data->stickers.insert(k, i.value());
				}
			}
			if (!_stickerImagesMap.isEmpty()) {
				_stickerImagesMap.clear();
				_storageStickersSize = 0;
				_mapChanged = true;
			}
			if (data->webFiles.isEmpty()) {
				data->webFiles = _webFilesMap;
			} else {
				for (WebFilesMap::const_iterator i = _webFilesMap.cbegin(), e = _webFilesMap.cend(); i != e; ++i) {
					QString k = i.key();
					while (data->webFiles.constFind(k) != data->webFiles.cend()) {
						k += '#';
					}
					data->webFiles.insert(k, i.value());
				}
			}
			if (!_webFilesMap.isEmpty()) {
				_webFilesMap.clear();
				_storageWebFilesSize = 0;
				_writeLocations();
			}
			if (data->audios.isEmpty()) {
				data->audios = _audiosMap;
			} else {
				for (StorageMap::const_iterator i = _audiosMap.cbegin(), e = _audiosMap.cend(); i != e; ++i) {
					StorageKey k = i.key();
					while (data->audios.constFind(k) != data->audios.cend()) {
						++k.second;
					}
					data->audios.insert(k, i.value());
				}
			}
			if (!_audiosMap.isEmpty()) {
				_audiosMap.clear();
				_storageAudiosSize = 0;
				_mapChanged = true;
			}
			_writeMap();
		}
		for (int32 i = 0, l = data->tasks.size(); i < l; ++i) {
			if (data->tasks.at(i) == task) return true;
		}
	}
	data->tasks.push_back(task);
	return true;
}

bool ClearManager::hasTask(ClearManagerTask task) {
	QMutexLocker lock(&data->mutex);
	if (data->tasks.isEmpty()) return false;
	if (data->tasks.at(0) == ClearManagerAll) return true;
	for (int32 i = 0, l = data->tasks.size(); i < l; ++i) {
		if (data->tasks.at(i) == task) return true;
	}
	return false;
}

void ClearManager::start() {
	moveToThread(data->thread);
	connect(data->thread, SIGNAL(started()), this, SLOT(onStart()));
	connect(data->thread, SIGNAL(finished()), data->thread, SLOT(deleteLater()));
	connect(data->thread, SIGNAL(finished()), this, SLOT(deleteLater()));
	data->thread->start();
}

void ClearManager::stop() {
	{
		QMutexLocker lock(&data->mutex);
		data->tasks.clear();
	}
	auto thread = data->thread;
	thread->quit();
	thread->wait();
}

ClearManager::~ClearManager() {
	delete data;
}

void ClearManager::onStart() {
	while (true) {
		int task = 0;
		bool result = false;
		StorageMap images, stickers, audios;
		WebFilesMap webFiles;
		{
			QMutexLocker lock(&data->mutex);
			if (data->tasks.isEmpty()) {
				data->working = false;
				break;
			}
			task = data->tasks.at(0);
			images = data->images;
			stickers = data->stickers;
			audios = data->audios;
			webFiles = data->webFiles;
		}
		switch (task) {
		case ClearManagerAll: {
			result = QDir(cTempDir()).removeRecursively();
			QDirIterator di(_userBasePath, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
			while (di.hasNext()) {
				di.next();
				const QFileInfo& fi = di.fileInfo();
				if (fi.isDir() && !fi.isSymLink()) {
					if (!QDir(di.filePath()).removeRecursively()) result = false;
				} else {
					QString path = di.filePath();
					if (!path.endsWith(qstr("map0")) && !path.endsWith(qstr("map1"))) {
						if (!QFile::remove(di.filePath())) result = false;
					}
				}
			}
		} break;
		case ClearManagerDownloads:
			result = QDir(cTempDir()).removeRecursively();
		break;
		case ClearManagerStorage:
			for (StorageMap::const_iterator i = images.cbegin(), e = images.cend(); i != e; ++i) {
				clearKey(i.value().first, UserPath);
			}
			for (StorageMap::const_iterator i = stickers.cbegin(), e = stickers.cend(); i != e; ++i) {
				clearKey(i.value().first, UserPath);
			}
			for (StorageMap::const_iterator i = audios.cbegin(), e = audios.cend(); i != e; ++i) {
				clearKey(i.value().first, UserPath);
			}
			for (WebFilesMap::const_iterator i = webFiles.cbegin(), e = webFiles.cend(); i != e; ++i) {
				clearKey(i.value().first, UserPath);
			}
			result = true;
		break;
		}
		{
			QMutexLocker lock(&data->mutex);
			if (!data->tasks.isEmpty() && data->tasks.at(0) == task) {
				data->tasks.pop_front();
			}
			if (data->tasks.isEmpty()) {
				data->working = false;
			}
			if (result) {
				emit succeed(task, data->working ? 0 : this);
			} else {
				emit failed(task, data->working ? 0 : this);
			}
			if (!data->working) break;
		}
	}
}

namespace internal {

Manager::Manager() {
	_mapWriteTimer.setSingleShot(true);
	connect(&_mapWriteTimer, SIGNAL(timeout()), this, SLOT(mapWriteTimeout()));
	_locationsWriteTimer.setSingleShot(true);
	connect(&_locationsWriteTimer, SIGNAL(timeout()), this, SLOT(locationsWriteTimeout()));
}

void Manager::writeMap(bool fast) {
	if (!_mapWriteTimer.isActive() || fast) {
		_mapWriteTimer.start(fast ? 1 : WriteMapTimeout);
	} else if (_mapWriteTimer.remainingTime() <= 0) {
		mapWriteTimeout();
	}
}

void Manager::writingMap() {
	_mapWriteTimer.stop();
}

void Manager::writeLocations(bool fast) {
	if (!_locationsWriteTimer.isActive() || fast) {
		_locationsWriteTimer.start(fast ? 1 : WriteMapTimeout);
	} else if (_locationsWriteTimer.remainingTime() <= 0) {
		locationsWriteTimeout();
	}
}

void Manager::writingLocations() {
	_locationsWriteTimer.stop();
}

void Manager::mapWriteTimeout() {
	_writeMap(WriteMapNow);
}

void Manager::locationsWriteTimeout() {
	_writeLocations(WriteMapNow);
}

void Manager::finish() {
	if (_mapWriteTimer.isActive()) {
		mapWriteTimeout();
	}
	if (_locationsWriteTimer.isActive()) {
		locationsWriteTimeout();
	}
}

} // namespace internal
} // namespace Local
