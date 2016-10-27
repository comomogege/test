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

#include "core/basic_types.h"

namespace base {

template <typename T, size_t N>
inline constexpr size_t array_size(T(&)[N]) {
	return N;
}

template <typename T>
inline T take(T &source, T &&new_value = T()) {
	std_::swap_moveable(new_value, source);
	return std_::move(new_value);
}

} // namespace base

template <typename Enum>
inline QFlags<Enum> qFlags(Enum v) {
	return QFlags<Enum>(v);
}

static const int32 ScrollMax = INT_MAX;

extern uint64 _SharedMemoryLocation[];
template <typename T, unsigned int N>
T *SharedMemoryLocation() {
	static_assert(N < 4, "Only 4 shared memory locations!");
	return reinterpret_cast<T*>(_SharedMemoryLocation + N);
}

// see https://github.com/boostcon/cppnow_presentations_2012/blob/master/wed/schurr_cpp11_tools_for_class_authors.pdf
class str_const { // constexpr string
public:
	template<std::size_t N>
	constexpr str_const(const char(&a)[N]) : _str(a), _size(N - 1) {
	}
	constexpr char operator[](std::size_t n) const {
		return (n < _size) ? _str[n] :
#ifndef OS_MAC_OLD
			throw std::out_of_range("");
#else // OS_MAC_OLD
			throw std::exception();
#endif // OS_MAC_OLD
	}
	constexpr std::size_t size() const { return _size; }
	const char *c_str() const { return _str; }

private:
	const char* const _str;
	const std::size_t _size;

};

inline QString str_const_toString(const str_const &str) {
	return QString::fromUtf8(str.c_str(), str.size());
}

template <typename T>
inline void accumulate_max(T &a, const T &b) { if (a < b) a = b; }

template <typename T>
inline void accumulate_min(T &a, const T &b) { if (a > b) a = b; }

static volatile int *t_assert_nullptr = nullptr;
inline void t_noop() {}
inline void t_assert_fail(const char *message, const char *file, int32 line) {
	QString info(qsl("%1 %2:%3").arg(message).arg(file).arg(line));
	LOG(("Assertion Failed! %1 %2:%3").arg(info));
	SignalHandlers::setCrashAnnotation("Assertion", info);
	*t_assert_nullptr = 0;
}
#define t_assert_full(condition, message, file, line) ((!(condition)) ? t_assert_fail(message, file, line) : t_noop())
#define t_assert_c(condition, comment) t_assert_full(condition, "\"" #condition "\" (" comment ")", __FILE__, __LINE__)
#define t_assert(condition) t_assert_full(condition, "\"" #condition "\"", __FILE__, __LINE__)

class Exception : public std::exception {
public:

	Exception(const QString &msg, bool isFatal = true) : _fatal(isFatal), _msg(msg.toUtf8()) {
		LOG(("Exception: %1").arg(msg));
	}
	bool fatal() const {
		return _fatal;
	}

	virtual const char *what() const throw() {
		return _msg.constData();
	}
	virtual ~Exception() throw() {
	}

private:
	bool _fatal;
	QByteArray _msg;
};

class MTPint;
using TimeId = int32;
TimeId myunixtime();
void unixtimeInit();
void unixtimeSet(TimeId servertime, bool force = false);
TimeId unixtime();
TimeId fromServerTime(const MTPint &serverTime);
void toServerTime(const TimeId &clientTime, MTPint &outServerTime);
uint64 msgid();
int32 reqid();

inline QDateTime date(int32 time = -1) {
	QDateTime result;
	if (time >= 0) result.setTime_t(time);
	return result;
}

inline QDateTime dateFromServerTime(const MTPint &time) {
	return date(fromServerTime(time));
}

inline QDateTime date(const MTPint &time) {
	return dateFromServerTime(time);
}

QDateTime dateFromServerTime(TimeId time);

inline void mylocaltime(struct tm * _Tm, const time_t * _Time) {
#ifdef Q_OS_WIN
	localtime_s(_Tm, _Time);
#else
	localtime_r(_Time, _Tm);
#endif
}

namespace ThirdParty {

void start();
void finish();

}

bool checkms(); // returns true if time has changed
uint64 getms(bool checked = false);

const static uint32 _md5_block_size = 64;
class HashMd5 {
public:

	HashMd5(const void *input = 0, uint32 length = 0);
	void feed(const void *input, uint32 length);
	int32 *result();

private:

	void init();
	void finalize();
	void transform(const uchar *block);

	bool _finalized;
	uchar _buffer[_md5_block_size];
	uint32 _count[2];
	uint32 _state[4];
	uchar _digest[16];

};

int32 hashCrc32(const void *data, uint32 len);
int32 *hashSha1(const void *data, uint32 len, void *dest); // dest - ptr to 20 bytes, returns (int32*)dest
int32 *hashSha256(const void *data, uint32 len, void *dest); // dest - ptr to 32 bytes, returns (int32*)dest
int32 *hashMd5(const void *data, uint32 len, void *dest); // dest = ptr to 16 bytes, returns (int32*)dest
char *hashMd5Hex(const int32 *hashmd5, void *dest); // dest = ptr to 32 bytes, returns (char*)dest
inline char *hashMd5Hex(const void *data, uint32 len, void *dest) { // dest = ptr to 32 bytes, returns (char*)dest
	return hashMd5Hex(HashMd5(data, len).result(), dest);
}

// good random (using openssl implementation)
void memset_rand(void *data, uint32 len);
template <typename T>
T rand_value() {
	T result;
	memset_rand(&result, sizeof(result));
	return result;
}

inline void memset_rand_bad(void *data, uint32 len) {
	for (uchar *i = reinterpret_cast<uchar*>(data), *e = i + len; i != e; ++i) {
		*i = uchar(rand() & 0xFF);
	}
}

template <typename T>
inline void memsetrnd_bad(T &value) {
	memset_rand_bad(&value, sizeof(value));
}

class ReadLockerAttempt {
public:

	ReadLockerAttempt(QReadWriteLock *_lock) : success(_lock->tryLockForRead()), lock(_lock) {
	}
	~ReadLockerAttempt() {
		if (success) {
			lock->unlock();
		}
	}

	operator bool() const {
		return success;
	}

private:

	bool success;
	QReadWriteLock *lock;

};

inline QString fromUtf8Safe(const char *str, int32 size = -1) {
	if (!str || !size) return QString();
	if (size < 0) size = int32(strlen(str));
	QString result(QString::fromUtf8(str, size));
	QByteArray back = result.toUtf8();
	if (back.size() != size || memcmp(back.constData(), str, size)) return QString::fromLocal8Bit(str, size);
	return result;
}

inline QString fromUtf8Safe(const QByteArray &str) {
	return fromUtf8Safe(str.constData(), str.size());
}

static const QRegularExpression::PatternOptions reMultiline(QRegularExpression::DotMatchesEverythingOption | QRegularExpression::MultilineOption);

template <typename T>
inline T snap(const T &v, const T &_min, const T &_max) {
	return (v < _min) ? _min : ((v > _max) ? _max : v);
}

template <typename T>
class ManagedPtr {
public:
	ManagedPtr() : ptr(0) {
	}
	ManagedPtr(T *p) : ptr(p) {
	}
	T *operator->() const {
		return ptr;
	}
	T *v() const {
		return ptr;
	}

protected:

	T *ptr;
	typedef ManagedPtr<T> Parent;
};

QString translitRusEng(const QString &rus);
QString rusKeyboardLayoutSwitch(const QString &from);

enum DBISendKey {
	dbiskEnter = 0,
	dbiskCtrlEnter = 1,
};

enum DBINotifyView {
	dbinvShowPreview = 0,
	dbinvShowName = 1,
	dbinvShowNothing = 2,
};

enum DBIWorkMode {
	dbiwmWindowAndTray = 0,
	dbiwmTrayOnly = 1,
	dbiwmWindowOnly = 2,
};

enum DBIConnectionType {
	dbictAuto = 0,
	dbictHttpAuto = 1, // not used
	dbictHttpProxy = 2,
	dbictTcpProxy = 3,
};

enum DBIDefaultAttach {
	dbidaDocument = 0,
	dbidaPhoto = 1,
};

struct ProxyData {
	QString host;
	uint32 port = 0;
	QString user, password;
};

enum DBIScale {
	dbisAuto = 0,
	dbisOne = 1,
	dbisOneAndQuarter = 2,
	dbisOneAndHalf = 3,
	dbisTwo = 4,

	dbisScaleCount = 5,
};

static const int MatrixRowShift = 40000;

enum DBIEmojiTab {
	dbietRecent = -1,
	dbietPeople = 0,
	dbietNature = 1,
	dbietFood = 2,
	dbietActivity = 3,
	dbietTravel = 4,
	dbietObjects = 5,
	dbietSymbols = 6,
	dbietStickers = 666,
};
static const int emojiTabCount = 8;
inline DBIEmojiTab emojiTabAtIndex(int index) {
	return (index < 0 || index >= emojiTabCount) ? dbietRecent : DBIEmojiTab(index - 1);
}

enum DBIPlatform {
	dbipWindows = 0,
	dbipMac = 1,
	dbipLinux64 = 2,
	dbipLinux32 = 3,
	dbipMacOld = 4,
};

enum DBIPeerReportSpamStatus {
	dbiprsNoButton = 0, // hidden, but not in the cloud settings yet
	dbiprsUnknown = 1, // contacts not loaded yet
	dbiprsShowButton = 2, // show report spam button, each show peer request setting from cloud
	dbiprsReportSent = 3, // report sent, but the report spam panel is not hidden yet
	dbiprsHidden = 4, // hidden in the cloud or not needed (bots, contacts, etc), no more requests
	dbiprsRequesting = 5, // requesting the cloud setting right now
};

inline QString strMakeFromLetters(const uint32 *letters, int32 len) {
	QString result;
	result.reserve(len);
	for (int32 i = 0; i < len; ++i) {
		result.push_back(QChar((((letters[i] >> 16) & 0xFF) << 8) | (letters[i] & 0xFF)));
	}
	return result;
}

class MimeType {
public:

	enum TypeEnum {
		Unknown,
		WebP,
	};

	MimeType(const QMimeType &type) : _typeStruct(type), _type(Unknown) {
	}
	MimeType(TypeEnum type) : _type(type) {
	}
	QStringList globPatterns() const;
	QString filterString() const;
	QString name() const;

private:

	QMimeType _typeStruct;
	TypeEnum _type;

};

MimeType mimeTypeForName(const QString &mime);
MimeType mimeTypeForFile(const QFileInfo &file);
MimeType mimeTypeForData(const QByteArray &data);

#include <cmath>

inline int rowscount(int fullCount, int countPerRow) {
	return (fullCount + countPerRow - 1) / countPerRow;
}
inline int floorclamp(int value, int step, int lowest, int highest) {
	return qMin(qMax(value / step, lowest), highest);
}
inline int floorclamp(float64 value, int step, int lowest, int highest) {
	return qMin(qMax(static_cast<int>(std::floor(value / step)), lowest), highest);
}
inline int ceilclamp(int value, int step, int lowest, int highest) {
	return qMax(qMin((value + step - 1) / step, highest), lowest);
}
inline int ceilclamp(float64 value, int32 step, int32 lowest, int32 highest) {
	return qMax(qMin(static_cast<int>(std::ceil(value / step)), highest), lowest);
}

enum ForwardWhatMessages {
	ForwardSelectedMessages,
	ForwardContextMessage,
	ForwardPressedMessage,
	ForwardPressedLinkMessage
};

enum ShowLayerOption {
	CloseOtherLayers = 0x00,
	KeepOtherLayers = 0x01,
	ShowAfterOtherLayers = 0x03,

	AnimatedShowLayer = 0x00,
	ForceFastShowLayer = 0x04,
};
Q_DECLARE_FLAGS(ShowLayerOptions, ShowLayerOption);
Q_DECLARE_OPERATORS_FOR_FLAGS(ShowLayerOptions);

static int32 FullArcLength = 360 * 16;
static int32 QuarterArcLength = (FullArcLength / 4);
static int32 MinArcLength = (FullArcLength / 360);
static int32 AlmostFullArcLength = (FullArcLength - MinArcLength);

template <typename T, typename... Args>
inline QSharedPointer<T> MakeShared(Args&&... args) {
	return QSharedPointer<T>(new T(std_::forward<Args>(args)...));
}

// This pointer is used for global non-POD variables that are allocated
// on demand by createIfNull(lambda) and are never automatically freed.
template <typename T>
class NeverFreedPointer {
public:
	NeverFreedPointer() = default;
	NeverFreedPointer(const NeverFreedPointer<T> &other) = delete;
	NeverFreedPointer &operator=(const NeverFreedPointer<T> &other) = delete;

	template <typename... Args>
	void createIfNull(Args&&... args) {
		if (isNull()) {
			reset(new T(std_::forward<Args>(args)...));
		}
	};

	T *data() const {
		return _p;
	}
	T *release() {
		return base::take(_p);
	}
	void reset(T *p = nullptr) {
		delete _p;
		_p = p;
	}
	bool isNull() const {
		return data() == nullptr;
	}

	void clear() {
		reset();
	}
	T *operator->() const {
		return data();
	}
	T &operator*() const {
		t_assert(!isNull());
		return *data();
	}
	explicit operator bool() const {
		return !isNull();
	}

private:
	T *_p;

};

// This pointer is used for static non-POD variables that are allocated
// on first use by constructor and are never automatically freed.
template <typename T>
class StaticNeverFreedPointer {
public:
	explicit StaticNeverFreedPointer(T *p) : _p(p) {
	}
	StaticNeverFreedPointer(const StaticNeverFreedPointer<T> &other) = delete;
	StaticNeverFreedPointer &operator=(const StaticNeverFreedPointer<T> &other) = delete;

	T *data() const {
		return _p;
	}
	T *release() {
		return base::take(_p);
	}
	void reset(T *p = nullptr) {
		delete _p;
		_p = p;
	}
	bool isNull() const {
		return data() == nullptr;
	}

	void clear() {
		reset();
	}
	T *operator->() const {
		return data();
	}
	T &operator*() const {
		t_assert(!isNull());
		return *data();
	}
	explicit operator bool() const {
		return !isNull();
	}

private:
	T *_p = nullptr;

};
