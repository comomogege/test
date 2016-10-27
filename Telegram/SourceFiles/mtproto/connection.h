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

#include "mtproto/core_types.h"
#include "mtproto/auth_key.h"
#include "mtproto/connection_abstract.h"
#include "core/single_timer.h"

namespace MTP {
namespace internal {

class ConnectionPrivate;
class SessionData;

class Thread : public QThread {
	Q_OBJECT

public:
	Thread();
	uint32 getThreadId() const;
	~Thread();

private:
	uint32 _threadId;

};

class Connection {
public:

	enum ConnectionType {
		TcpConnection,
		HttpConnection
	};

	Connection();

	int32 prepare(SessionData *data, int32 dc = 0); // return dc
	void start();

	void kill();
	void waitTillFinish();
	~Connection();

	static const int UpdateAlways = 666;

	int32 state() const;
	QString transport() const;

private:

	QThread *thread;
	ConnectionPrivate *data;

};

class ConnectionPrivate : public QObject {
	Q_OBJECT

public:

	ConnectionPrivate(QThread *thread, Connection *owner, SessionData *data, uint32 dc);
	~ConnectionPrivate();

	void stop();

	int32 getDC() const;

	int32 getState() const;
	QString transport() const;

signals:

	void needToReceive();
	void needToRestart();
	void stateChanged(qint32 newState);
	void sessionResetDone();

	void needToSendAsync();
	void sendAnythingAsync(quint64 msWait);
	void sendHttpWaitAsync();
	void sendPongAsync(quint64 msgId, quint64 pingId);
	void sendMsgsStateInfoAsync(quint64 msgId, QByteArray data);
	void resendAsync(quint64 msgId, quint64 msCanWait, bool forceContainer, bool sendMsgStateInfo);
	void resendManyAsync(QVector<quint64> msgIds, quint64 msCanWait, bool forceContainer, bool sendMsgStateInfo);
	void resendAllAsync();

	void finished(Connection *connection);

public slots:

	void retryByTimer();
	void restartNow();
	void restart(bool mayBeBadKey = false);

	void onPingSender();
	void onPingSendForce();

	void onWaitConnectedFailed();
	void onWaitReceivedFailed();
	void onWaitIPv4Failed();

	void onOldConnection();
	void onSentSome(uint64 size);
	void onReceivedSome();

	void onReadyData();
	void socketStart(bool afterConfig = false);

	void onConnected4();
	void onConnected6();
	void onDisconnected4();
	void onDisconnected6();
	void onError4(bool mayBeBadKey = false);
	void onError6(bool mayBeBadKey = false);

	void doFinish();

	// Auth key creation packet receive slots
	void pqAnswered();
	void dhParamsAnswered();
	void dhClientParamsAnswered();

	// General packet receive slot, connected to conn->receivedData signal
	void handleReceived();

	// Sessions signals, when we need to send something
	void tryToSend();

	void updateAuthKey();

	void onConfigLoaded();

private:

	void doDisconnect();

	void createConn(bool createIPv4, bool createIPv6);
	void destroyConn(AbstractConnection **conn = 0); // 0 - destory all

	mtpMsgId placeToContainer(mtpRequest &toSendRequest, mtpMsgId &bigMsgId, mtpMsgId *&haveSentArr, mtpRequest &req);
	mtpMsgId prepareToSend(mtpRequest &request, mtpMsgId currentLastId);
	mtpMsgId replaceMsgId(mtpRequest &request, mtpMsgId newId);

	bool sendRequest(mtpRequest &request, bool needAnyResponse, QReadLocker &lockFinished);
	mtpRequestId wasSent(mtpMsgId msgId) const;

	int32 handleOneReceived(const mtpPrime *from, const mtpPrime *end, uint64 msgId, int32 serverTime, uint64 serverSalt, bool badTime);
	mtpBuffer ungzip(const mtpPrime *from, const mtpPrime *end) const;
	void handleMsgsStates(const QVector<MTPlong> &ids, const std::string &states, QVector<MTPlong> &acked);

	void clearMessages();

	bool setState(int32 state, int32 ifState = Connection::UpdateAlways);
	mutable QReadWriteLock stateConnMutex;
	int32 _state;

	bool _needSessionReset;
	void resetSession();

	ShiftedDcId dc;
	Connection *_owner;
	AbstractConnection *_conn, *_conn4, *_conn6;

	SingleTimer retryTimer; // exp retry timer
	int retryTimeout;
	quint64 retryWillFinish;

	SingleTimer oldConnectionTimer;
	bool oldConnection;

	SingleTimer _waitForConnectedTimer, _waitForReceivedTimer, _waitForIPv4Timer;
	uint32 _waitForReceived, _waitForConnected;
	int64 firstSentAt;

	QVector<MTPlong> ackRequestData, resendRequestData;

	// if badTime received - search for ids in sessionData->haveSent and sessionData->wereAcked and sync time/salt, return true if found
	bool requestsFixTimeSalt(const QVector<MTPlong> &ids, int32 serverTime, uint64 serverSalt);

	// remove msgs with such ids from sessionData->haveSent, add to sessionData->wereAcked
	void requestsAcked(const QVector<MTPlong> &ids, bool byResponse = false);

	mtpPingId _pingId, _pingIdToSend;
	uint64 _pingSendAt;
	mtpMsgId _pingMsgId;
	SingleTimer _pingSender;

	void resend(quint64 msgId, quint64 msCanWait = 0, bool forceContainer = false, bool sendMsgStateInfo = false);
	void resendMany(QVector<quint64> msgIds, quint64 msCanWait = 0, bool forceContainer = false, bool sendMsgStateInfo = false);

	template <typename TRequest>
	void sendRequestNotSecure(const TRequest &request);

	template <typename TResponse>
	bool readResponseNotSecure(TResponse &response);

	bool restarted, _finished;

	uint64 keyId;
	QReadWriteLock sessionDataMutex;
	SessionData *sessionData;

	bool myKeyLock;
	void lockKey();
	void unlockKey();

	// Auth key creation fields and methods
	struct AuthKeyCreateData {
		AuthKeyCreateData()
		: new_nonce(*(MTPint256*)((uchar*)new_nonce_buf))
		, auth_key_aux_hash(*(MTPlong*)((uchar*)new_nonce_buf + 33))
		, retries(0)
		, g(0)
		, req_num(0)
		, msgs_sent(0) {
			memset(new_nonce_buf, 0, sizeof(new_nonce_buf));
			memset(aesKey, 0, sizeof(aesKey));
			memset(aesIV, 0, sizeof(aesIV));
			memset(auth_key, 0, sizeof(auth_key));
		}
		MTPint128 nonce, server_nonce;
		uchar new_nonce_buf[41]; // 32 bytes new_nonce + 1 check byte + 8 bytes of auth_key_aux_hash
		MTPint256 &new_nonce;
		MTPlong &auth_key_aux_hash;

		uint32 retries;
		MTPlong retry_id;

		int32 g;

		uchar aesKey[32], aesIV[32];
		uint32 auth_key[64];
		MTPlong auth_key_hash;

		uint32 req_num; // sent not encrypted request number
		uint32 msgs_sent;
	};
	struct AuthKeyCreateStrings {
		QByteArray dh_prime;
		QByteArray g_a;
	};
	AuthKeyCreateData *authKeyData;
	AuthKeyCreateStrings *authKeyStrings;

	void dhClientParamsSend();
	void authKeyCreated();
	void clearAuthKeyData();

};

} // namespace internal
} // namespace MTP
