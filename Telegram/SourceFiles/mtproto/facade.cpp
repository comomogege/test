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

#include "mtproto/facade.h"

#include "localstorage.h"

namespace MTP {

namespace {

	typedef QMap<int32, internal::Session*> Sessions;
	Sessions sessions;
	internal::Session *mainSession;

	typedef QMap<mtpRequestId, int32> RequestsByDC; // holds dcWithShift for request to this dc or -dc for request to main dc
	RequestsByDC requestsByDC;
	QMutex requestByDCLock;

	typedef QMap<mtpRequestId, int32> AuthExportRequests; // holds target dcWithShift for auth export request
	AuthExportRequests authExportRequests;

	bool _started = false;

	uint32 layer;

	typedef QMap<mtpRequestId, RPCResponseHandler> ParserMap;
	ParserMap parserMap;
	QMutex parserMapLock;

	typedef QMap<mtpRequestId, mtpRequest> RequestMap;
	RequestMap requestMap;
	QReadWriteLock requestMapLock;

	typedef QPair<mtpRequestId, uint64> DelayedRequest;
	typedef QList<DelayedRequest> DelayedRequestsList;
	DelayedRequestsList delayedRequests;

	typedef QMap<mtpRequestId, int32> RequestsDelays;
	RequestsDelays requestsDelays;

	typedef QSet<mtpRequestId> BadGuestDCRequests;
	BadGuestDCRequests badGuestDCRequests;

	typedef QVector<mtpRequestId> DCAuthWaiters;
	typedef QMap<int32, DCAuthWaiters> AuthWaiters; // holds request ids waiting for auth import to specific dc
	AuthWaiters authWaiters;

	typedef OrderedSet<internal::Connection*> MTPQuittingConnections;
	MTPQuittingConnections quittingConnections;

	QMutex toClearLock;
	RPCCallbackClears toClear;

	RPCResponseHandler globalHandler;
	MTPStateChangedHandler stateChangedHandler = 0;
	MTPSessionResetHandler sessionResetHandler = 0;
	internal::GlobalSlotCarrier *_globalSlotCarrier = 0;

	void importDone(const MTPauth_Authorization &result, mtpRequestId req) {
		QMutexLocker locker1(&requestByDCLock);

		RequestsByDC::iterator i = requestsByDC.find(req);
		if (i == requestsByDC.end()) {
			LOG(("MTP Error: auth import request not found in requestsByDC, requestId: %1").arg(req));
			RPCError error(internal::rpcClientError("AUTH_IMPORT_FAIL", QString("did not find import request in requestsByDC, request %1").arg(req)));
			if (globalHandler.onFail && authedId()) (*globalHandler.onFail)(req, error); // auth failed in main dc
			return;
		}
		DcId newdc = bareDcId(i.value());

		DEBUG_LOG(("MTP Info: auth import to dc %1 succeeded").arg(newdc));

		DCAuthWaiters &waiters(authWaiters[newdc]);
		if (waiters.size()) {
			QReadLocker locker(&requestMapLock);
			for (DCAuthWaiters::iterator i = waiters.begin(), e = waiters.end(); i != e; ++i) {
				mtpRequestId requestId = *i;
				RequestMap::const_iterator j = requestMap.constFind(requestId);
				if (j == requestMap.cend()) {
					LOG(("MTP Error: could not find request %1 for resending").arg(requestId));
					continue;
				}
				ShiftedDcId dcWithShift = newdc;
				{
					RequestsByDC::iterator k = requestsByDC.find(requestId);
					if (k == requestsByDC.cend()) {
						LOG(("MTP Error: could not find request %1 by dc for resending").arg(requestId));
						continue;
					}
					if (k.value() < 0) {
						setdc(newdc);
						k.value() = -newdc;
					} else {
						dcWithShift = shiftDcId(newdc, getDcIdShift(k.value()));
						k.value() = dcWithShift;
					}
					DEBUG_LOG(("MTP Info: resending request %1 to dc %2 after import auth").arg(requestId).arg(k.value()));
				}
				if (internal::Session *session = internal::getSession(dcWithShift)) {
					session->sendPrepared(j.value());
				}
			}
			waiters.clear();
		}
	}

	bool importFail(const RPCError &error, mtpRequestId req) {
		if (isDefaultHandledError(error)) return false;

		if (globalHandler.onFail && authedId()) (*globalHandler.onFail)(req, error); // auth import failed
		return true;
	}

	void exportDone(const MTPauth_ExportedAuthorization &result, mtpRequestId req) {
		AuthExportRequests::const_iterator i = authExportRequests.constFind(req);
		if (i == authExportRequests.cend()) {
			LOG(("MTP Error: auth export request target dcWithShift not found, requestId: %1").arg(req));
			RPCError error(internal::rpcClientError("AUTH_IMPORT_FAIL", QString("did not find target dcWithShift, request %1").arg(req)));
			if (globalHandler.onFail && authedId()) (*globalHandler.onFail)(req, error); // auth failed in main dc
			return;
		}

		const auto &data(result.c_auth_exportedAuthorization());
		send(MTPauth_ImportAuthorization(data.vid, data.vbytes), rpcDone(importDone), rpcFail(importFail), i.value());
		authExportRequests.remove(req);
	}

	bool exportFail(const RPCError &error, mtpRequestId req) {
		if (isDefaultHandledError(error)) return false;

		AuthExportRequests::const_iterator i = authExportRequests.constFind(req);
		if (i != authExportRequests.cend()) {
			authWaiters[bareDcId(i.value())].clear();
		}
		if (globalHandler.onFail && authedId()) (*globalHandler.onFail)(req, error); // auth failed in main dc
		return true;
	}

	bool onErrorDefault(mtpRequestId requestId, const RPCError &error) {
		const QString &err(error.type());
		int32 code = error.code();
		if (!isFloodError(error) && err != qstr("AUTH_KEY_UNREGISTERED")) {
			int breakpoint = 0;
		}
		bool badGuestDC = (code == 400) && (err == qsl("FILE_ID_INVALID"));
        QRegularExpressionMatch m;
		if ((m = QRegularExpression("^(FILE|PHONE|NETWORK|USER)_MIGRATE_(\\d+)$").match(err)).hasMatch()) {
			if (!requestId) return false;

			ShiftedDcId dcWithShift = 0, newdcWithShift = m.captured(2).toInt();
			{
				QMutexLocker locker(&requestByDCLock);
				RequestsByDC::iterator i = requestsByDC.find(requestId);
				if (i == requestsByDC.end()) {
					LOG(("MTP Error: could not find request %1 for migrating to %2").arg(requestId).arg(newdcWithShift));
				} else {
					dcWithShift = i.value();
				}
			}
			if (!dcWithShift || !newdcWithShift) return false;

			DEBUG_LOG(("MTP Info: changing request %1 from dcWithShift%2 to dc%3").arg(requestId).arg(dcWithShift).arg(newdcWithShift));
			if (dcWithShift < 0) { // newdc shift = 0
				if (false && authedId() && !authExportRequests.contains(requestId)) { // migrate not supported at this moment
					DEBUG_LOG(("MTP Info: importing auth to dc %1").arg(newdcWithShift));
					DCAuthWaiters &waiters(authWaiters[newdcWithShift]);
					if (!waiters.size()) {
						authExportRequests.insert(send(MTPauth_ExportAuthorization(MTP_int(newdcWithShift)), rpcDone(exportDone), rpcFail(exportFail)), newdcWithShift);
					}
					waiters.push_back(requestId);
					return true;
				} else {
					MTP::setdc(newdcWithShift);
				}
			} else {
				newdcWithShift = shiftDcId(newdcWithShift, getDcIdShift(dcWithShift));
			}

			mtpRequest req;
			{
				QReadLocker locker(&requestMapLock);
				RequestMap::const_iterator i = requestMap.constFind(requestId);
				if (i == requestMap.cend()) {
					LOG(("MTP Error: could not find request %1").arg(requestId));
					return false;
				}
				req = i.value();
			}
			if (auto session = internal::getSession(newdcWithShift)) {
				internal::registerRequest(requestId, (dcWithShift < 0) ? -newdcWithShift : newdcWithShift);
				session->sendPrepared(req);
			}
			return true;
		} else if (code < 0 || code >= 500 || (m = QRegularExpression("^FLOOD_WAIT_(\\d+)$").match(err)).hasMatch()) {
			if (!requestId) return false;

			int32 secs = 1;
			if (code < 0 || code >= 500) {
				RequestsDelays::iterator i = requestsDelays.find(requestId);
				if (i != requestsDelays.cend()) {
					secs = (i.value() > 60) ? i.value() : (i.value() *= 2);
				} else {
					requestsDelays.insert(requestId, secs);
				}
			} else {
				secs = m.captured(1).toInt();
//				if (secs >= 60) return false;
			}
			uint64 sendAt = getms(true) + secs * 1000 + 10;
			DelayedRequestsList::iterator i = delayedRequests.begin(), e = delayedRequests.end();
			for (; i != e; ++i) {
				if (i->first == requestId) return true;
				if (i->second > sendAt) break;
			}
			delayedRequests.insert(i, DelayedRequest(requestId, sendAt));

			if (_globalSlotCarrier) _globalSlotCarrier->checkDelayed();

			return true;
		} else if (code == 401 || (badGuestDC && badGuestDCRequests.constFind(requestId) == badGuestDCRequests.cend())) {
			int32 dcWithShift = 0;
			{
				QMutexLocker locker(&requestByDCLock);
				RequestsByDC::iterator i = requestsByDC.find(requestId);
				if (i != requestsByDC.end()) {
					dcWithShift = i.value();
				} else {
					LOG(("MTP Error: unauthorized request without dc info, requestId %1").arg(requestId));
				}
			}
			int32 newdc = bareDcId(qAbs(dcWithShift));
			if (!newdc || newdc == internal::mainDC() || !authedId()) {
				if (!badGuestDC && globalHandler.onFail) (*globalHandler.onFail)(requestId, error); // auth failed in main dc
				return false;
			}

			DEBUG_LOG(("MTP Info: importing auth to dcWithShift %1").arg(dcWithShift));
			DCAuthWaiters &waiters(authWaiters[newdc]);
			if (!waiters.size()) {
				authExportRequests.insert(send(MTPauth_ExportAuthorization(MTP_int(newdc)), rpcDone(exportDone), rpcFail(exportFail)), abs(dcWithShift));
			}
			waiters.push_back(requestId);
			if (badGuestDC) badGuestDCRequests.insert(requestId);
			return true;
		} else if (err == qstr("CONNECTION_NOT_INITED") || err == qstr("CONNECTION_LAYER_INVALID")) {
			mtpRequest req;
			{
				QReadLocker locker(&requestMapLock);
				RequestMap::const_iterator i = requestMap.constFind(requestId);
				if (i == requestMap.cend()) {
					LOG(("MTP Error: could not find request %1").arg(requestId));
					return false;
				}
				req = i.value();
			}
			int32 dcWithShift = 0;
			{
				QMutexLocker locker(&requestByDCLock);
				RequestsByDC::iterator i = requestsByDC.find(requestId);
				if (i == requestsByDC.end()) {
					LOG(("MTP Error: could not find request %1 for resending with init connection").arg(requestId));
				} else {
					dcWithShift = i.value();
				}
			}
			if (!dcWithShift) return false;

			if (internal::Session *session = internal::getSession(qAbs(dcWithShift))) {
				req->needsLayer = true;
				session->sendPrepared(req);
			}
			return true;
		} else if (err == qstr("MSG_WAIT_FAILED")) {
			mtpRequest req;
			{
				QReadLocker locker(&requestMapLock);
				RequestMap::const_iterator i = requestMap.constFind(requestId);
				if (i == requestMap.cend()) {
					LOG(("MTP Error: could not find request %1").arg(requestId));
					return false;
				}
				req = i.value();
			}
			if (!req->after) {
				LOG(("MTP Error: wait failed for not dependent request %1").arg(requestId));
				return false;
			}
			int32 dcWithShift = 0;
			{
				QMutexLocker locker(&requestByDCLock);
				RequestsByDC::iterator i = requestsByDC.find(requestId), j = requestsByDC.find(req->after->requestId);
				if (i == requestsByDC.end()) {
					LOG(("MTP Error: could not find request %1 by dc").arg(requestId));
				} else if (j == requestsByDC.end()) {
					LOG(("MTP Error: could not find dependent request %1 by dc").arg(req->after->requestId));
				} else {
					dcWithShift = i.value();
					if (i.value() != j.value()) {
						req->after = mtpRequest();
					}
				}
			}
			if (!dcWithShift) return false;

			if (!req->after) {
				if (internal::Session *session = internal::getSession(qAbs(dcWithShift))) {
					req->needsLayer = true;
					session->sendPrepared(req);
				}
			} else {
				int32 newdc = bareDcId(qAbs(dcWithShift));
				DCAuthWaiters &waiters(authWaiters[newdc]);
				if (waiters.indexOf(req->after->requestId) >= 0) {
					if (waiters.indexOf(requestId) < 0) {
						waiters.push_back(requestId);
					}
					if (badGuestDCRequests.constFind(req->after->requestId) != badGuestDCRequests.cend()) {
						if (badGuestDCRequests.constFind(requestId) == badGuestDCRequests.cend()) {
							badGuestDCRequests.insert(requestId);
						}
					}
				} else {
					uint64 at = 0;
					DelayedRequestsList::iterator i = delayedRequests.begin(), e = delayedRequests.end();
					for (; i != e; ++i) {
						if (i->first == requestId) return true;
						if (i->first == req->after->requestId) break;
					}
					if (i != e) {
						delayedRequests.insert(i, DelayedRequest(requestId, i->second));
					}

					if (_globalSlotCarrier) _globalSlotCarrier->checkDelayed();
				}
			}
			return true;
		}
		if (badGuestDC) badGuestDCRequests.remove(requestId);
		return false;
	}

	bool _paused = false;

} // namespace

namespace internal {

Session *getSession(ShiftedDcId shiftedDcId) {
	if (!_started) return nullptr;
	if (!shiftedDcId) return mainSession;
	if (!bareDcId(shiftedDcId)) {
		shiftedDcId += bareDcId(mainSession->getDcWithShift());
	}

	Sessions::const_iterator i = sessions.constFind(shiftedDcId);
	if (i == sessions.cend()) {
		i = sessions.insert(shiftedDcId, new Session(shiftedDcId));
	}
	return i.value();
}

bool paused() {
	return _paused;
}

void registerRequest(mtpRequestId requestId, int32 dcWithShift) {
	{
		QMutexLocker locker(&requestByDCLock);
		requestsByDC.insert(requestId, dcWithShift);
	}
	internal::performDelayedClear(); // need to do it somewhere...
}

void unregisterRequest(mtpRequestId requestId) {
	requestsDelays.remove(requestId);

	{
		QWriteLocker locker(&requestMapLock);
		requestMap.remove(requestId);
	}

	QMutexLocker locker(&requestByDCLock);
	requestsByDC.remove(requestId);
}

mtpRequestId storeRequest(mtpRequest &request, const RPCResponseHandler &parser) {
	mtpRequestId res = reqid();
	request->requestId = res;
	if (parser.onDone || parser.onFail) {
		QMutexLocker locker(&parserMapLock);
		parserMap.insert(res, parser);
	}
	{
		QWriteLocker locker(&requestMapLock);
		requestMap.insert(res, request);
	}
	return res;
}

mtpRequest getRequest(mtpRequestId reqId) {
	static mtpRequest zero;
	mtpRequest req;
	{
		QReadLocker locker(&requestMapLock);
		RequestMap::const_iterator i = requestMap.constFind(reqId);
		req = (i == requestMap.cend()) ? zero : i.value();
	}
	return req;
}

void wrapInvokeAfter(mtpRequest &to, const mtpRequest &from, const mtpRequestMap &haveSent, int32 skipBeforeRequest) {
	mtpMsgId afterId(*(mtpMsgId*)(from->after->data() + 4));
	mtpRequestMap::const_iterator i = afterId ? haveSent.constFind(afterId) : haveSent.cend();
	int32 size = to->size(), lenInInts = (from.innerLength() >> 2), headlen = 4, fulllen = headlen + lenInInts;
	if (i == haveSent.constEnd()) { // no invoke after or such msg was not sent or was completed recently
		to->resize(size + fulllen + skipBeforeRequest);
		if (skipBeforeRequest) {
			memcpy(to->data() + size, from->constData() + 4, headlen * sizeof(mtpPrime));
			memcpy(to->data() + size + headlen + skipBeforeRequest, from->constData() + 4 + headlen, lenInInts * sizeof(mtpPrime));
		} else {
			memcpy(to->data() + size, from->constData() + 4, fulllen * sizeof(mtpPrime));
		}
	} else {
		to->resize(size + fulllen + skipBeforeRequest + 3);
		memcpy(to->data() + size, from->constData() + 4, headlen * sizeof(mtpPrime));
		(*to)[size + 3] += 3 * sizeof(mtpPrime);
		*((mtpTypeId*)&((*to)[size + headlen + skipBeforeRequest])) = mtpc_invokeAfterMsg;
		memcpy(to->data() + size + headlen + skipBeforeRequest + 1, &afterId, 2 * sizeof(mtpPrime));
		memcpy(to->data() + size + headlen + skipBeforeRequest + 3, from->constData() + 4 + headlen, lenInInts * sizeof(mtpPrime));
		if (size + 3 != 7) (*to)[7] += 3 * sizeof(mtpPrime);
	}
}

void clearCallbacks(mtpRequestId requestId, int32 errorCode) {
	RPCResponseHandler h;
	bool found = false;
	{
		QMutexLocker locker(&parserMapLock);
		ParserMap::iterator i = parserMap.find(requestId);
		if (i != parserMap.end()) {
			h = i.value();
			found = true;

			parserMap.erase(i);
		}
	}
	if (errorCode && found) {
		rpcErrorOccured(requestId, h, rpcClientError("CLEAR_CALLBACK", QString("did not handle request %1, error code %2").arg(requestId).arg(errorCode)));
	}
}

void clearCallbacksDelayed(const RPCCallbackClears &requestIds) {
	uint32 idsCount = requestIds.size();
	if (!idsCount) return;

	if (cDebug()) {
		QString idsStr = QString("%1").arg(requestIds[0].requestId);
		for (uint32 i = 1; i < idsCount; ++i) {
			idsStr += QString(", %1").arg(requestIds[i].requestId);
		}
		DEBUG_LOG(("RPC Info: clear callbacks delayed, msgIds: %1").arg(idsStr));
	}

	QMutexLocker lock(&toClearLock);
	uint32 toClearNow = toClear.size();
	if (toClearNow) {
		toClear.resize(toClearNow + idsCount);
		memcpy(toClear.data() + toClearNow, requestIds.constData(), idsCount * sizeof(RPCCallbackClear));
	} else {
		toClear = requestIds;
	}
}

void performDelayedClear() {
	QMutexLocker lock(&toClearLock);
	if (!toClear.isEmpty()) {
		for (RPCCallbackClears::iterator i = toClear.begin(), e = toClear.end(); i != e; ++i) {
			if (cDebug()) {
				QMutexLocker locker(&parserMapLock);
				if (parserMap.find(i->requestId) != parserMap.end()) {
					DEBUG_LOG(("RPC Info: clearing delayed callback %1, error code %2").arg(i->requestId).arg(i->errorCode));
				}
			}
			clearCallbacks(i->requestId, i->errorCode);
			internal::unregisterRequest(i->requestId);
		}
		toClear.clear();
	}
}

void execCallback(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) {
	RPCResponseHandler h;
	{
		QMutexLocker locker(&parserMapLock);
		ParserMap::iterator i = parserMap.find(requestId);
		if (i != parserMap.cend()) {
			h = i.value();
			parserMap.erase(i);

			DEBUG_LOG(("RPC Info: found parser for request %1, trying to parse response...").arg(requestId));
		}
	}
	if (h.onDone || h.onFail) {
		try {
			if (from >= end) throw mtpErrorInsufficient();

			if (*from == mtpc_rpc_error) {
				RPCError err(MTPRpcError(from, end));
				DEBUG_LOG(("RPC Info: error received, code %1, type %2, description: %3").arg(err.code()).arg(err.type()).arg(err.description()));
				if (!rpcErrorOccured(requestId, h, err)) {
					QMutexLocker locker(&parserMapLock);
					parserMap.insert(requestId, h);
					return;
				}
			} else {
				if (h.onDone) {
//						t_assert(App::app() != 0);
					(*h.onDone)(requestId, from, end);
				}
			}
		} catch (Exception &e) {
			if (!rpcErrorOccured(requestId, h, rpcClientError("RESPONSE_PARSE_FAILED", QString("exception text: ") + e.what()))) {
				QMutexLocker locker(&parserMapLock);
				parserMap.insert(requestId, h);
				return;
			}
		}
	} else {
		DEBUG_LOG(("RPC Info: parser not found for %1").arg(requestId));
	}
	unregisterRequest(requestId);
}

bool hasCallbacks(mtpRequestId requestId) {
	QMutexLocker locker(&parserMapLock);
	ParserMap::iterator i = parserMap.find(requestId);
	return (i != parserMap.cend());
}

void globalCallback(const mtpPrime *from, const mtpPrime *end) {
	if (globalHandler.onDone) (*globalHandler.onDone)(0, from, end); // some updates were received
}

void onStateChange(int32 dcWithShift, int32 state) {
	if (stateChangedHandler) stateChangedHandler(dcWithShift, state);
}

void onSessionReset(int32 dcWithShift) {
	if (sessionResetHandler) sessionResetHandler(dcWithShift);
}

bool rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err) { // return true if need to clean request data
	if (isDefaultHandledError(err)) {
		if (onFail && (*onFail)(requestId, err)) return true;
	}

	if (onErrorDefault(requestId, err)) {
		return false;
	}
	LOG(("RPC Error: request %1 got fail with code %2, error %3%4").arg(requestId).arg(err.code()).arg(err.type()).arg(err.description().isEmpty() ? QString() : QString(": %1").arg(err.description())));
	onFail && (*onFail)(requestId, err);
	return true;
}

GlobalSlotCarrier::GlobalSlotCarrier() {
	connect(&_timer, SIGNAL(timeout()), this, SLOT(checkDelayed()));
}

void GlobalSlotCarrier::checkDelayed() {
	uint64 now = getms(true);
	while (!delayedRequests.isEmpty() && now >= delayedRequests.front().second) {
		mtpRequestId requestId = delayedRequests.front().first;
		delayedRequests.pop_front();

		int32 dcWithShift = 0;
		{
			QMutexLocker locker(&requestByDCLock);
			RequestsByDC::const_iterator i = requestsByDC.constFind(requestId);
			if (i != requestsByDC.cend()) {
				dcWithShift = i.value();
			} else {
				LOG(("MTP Error: could not find request dc for delayed resend, requestId %1").arg(requestId));
				continue;
			}
		}

		mtpRequest req;
		{
			QReadLocker locker(&requestMapLock);
			RequestMap::const_iterator j = requestMap.constFind(requestId);
			if (j == requestMap.cend()) {
				DEBUG_LOG(("MTP Error: could not find request %1").arg(requestId));
				continue;
			}
			req = j.value();
		}
		if (Session *session = getSession(qAbs(dcWithShift))) {
			session->sendPrepared(req);
		}
	}

	if (!delayedRequests.isEmpty()) {
		_timer.start(delayedRequests.front().second - now);
	}
}

void GlobalSlotCarrier::connectionFinished(Connection *connection) {
	MTPQuittingConnections::iterator i = quittingConnections.find(connection);
	if (i != quittingConnections.cend()) {
		quittingConnections.erase(i);
	}

	connection->waitTillFinish();
	delete connection;
}

GlobalSlotCarrier *globalSlotCarrier() {
	return _globalSlotCarrier;
}

void queueQuittingConnection(Connection *connection) {
	quittingConnections.insert(connection);
}

} // namespace internal

void start() {
	if (started()) return;

    unixtimeInit();

	internal::DcenterMap &dcs(internal::DCMap());

	_globalSlotCarrier = new internal::GlobalSlotCarrier();

	mainSession = new internal::Session(internal::mainDC());
	sessions.insert(mainSession->getDcWithShift(), mainSession);

	_started = true;

	if (internal::configNeeded()) {
		internal::configLoader()->load();
	}
}

bool started() {
	return _started;
}

void restart() {
	if (!_started) return;

	for (auto i = sessions.cbegin(), e = sessions.cend(); i != e; ++i) {
		i.value()->restart();
	}
}

void restart(int32 dcMask) {
	if (!_started) return;

	dcMask = bareDcId(dcMask);
	for (Sessions::const_iterator i = sessions.cbegin(), e = sessions.cend(); i != e; ++i) {
		if (bareDcId(i.value()->getDcWithShift()) == dcMask) {
			i.value()->restart();
		}
	}
}

void pause() {
	if (!_started) return;
	_paused = true;
}

void unpause() {
	if (!_started) return;
	_paused = false;
	for (Sessions::const_iterator i = sessions.cbegin(), e = sessions.cend(); i != e; ++i) {
		i.value()->unpaused();
	}
}

void configure(int32 dc, int32 user) {
	if (_started) return;
	internal::setDC(dc);
	internal::authed(user);
}

void setdc(int32 dc, bool fromZeroOnly) {
	if (!dc || !_started) return;
	internal::setDC(dc, fromZeroOnly);
	int32 oldMainDc = mainSession->getDcWithShift();
	if (maindc() != oldMainDc) {
		killSession(oldMainDc);
	}
	Local::writeMtpData();
}

int32 maindc() {
	return internal::mainDC();
}

int32 dcstate(int32 dc) {
	if (!_started) return 0;

	if (!dc) return mainSession->getState();
	if (!bareDcId(dc)) {
		dc += bareDcId(mainSession->getDcWithShift());
	}

	Sessions::const_iterator i = sessions.constFind(dc);
	if (i != sessions.cend()) return i.value()->getState();

	return DisconnectedState;
}

QString dctransport(int32 dc) {
	if (!_started) return QString();

	if (!dc) return mainSession->transport();
	if (!bareDcId(dc)) {
		dc += bareDcId(mainSession->getDcWithShift());
	}

	Sessions::const_iterator i = sessions.constFind(dc);
	if (i != sessions.cend()) return i.value()->transport();

	return QString();
}

void ping() {
	if (internal::Session *session = internal::getSession(0)) {
		session->ping();
	}
}

void cancel(mtpRequestId requestId) {
	if (!_started) return;

	mtpMsgId msgId = 0;
	requestsDelays.remove(requestId);
	{
		QWriteLocker locker(&requestMapLock);
		RequestMap::iterator i = requestMap.find(requestId);
		if (i != requestMap.end()) {
			msgId = *(mtpMsgId*)(i.value()->constData() + 4);
			requestMap.erase(i);
		}
	}
	{
		QMutexLocker locker(&requestByDCLock);
		RequestsByDC::iterator i = requestsByDC.find(requestId);
		if (i != requestsByDC.end()) {
			if (internal::Session *session = internal::getSession(qAbs(i.value()))) {
				session->cancel(requestId, msgId);
			}
			requestsByDC.erase(i);
		}
	}
	internal::clearCallbacks(requestId);
}

void killSession(int32 dc) {
	Sessions::iterator i = sessions.find(dc);
	if (i != sessions.cend()) {
		bool wasMain = (i.value() == mainSession);

		i.value()->kill();
		i.value()->deleteLater();
		sessions.erase(i);

		if (wasMain) {
			mainSession = new internal::Session(internal::mainDC());
			int32 newdc = mainSession->getDcWithShift();
			i = sessions.find(newdc);
			if (i != sessions.cend()) {
				i.value()->kill();
				i.value()->deleteLater();
				sessions.erase(i);
			}
			sessions.insert(newdc, mainSession);
		}
	}
}

void stopSession(int32 dc) {
	Sessions::iterator i = sessions.find(dc);
	if (i != sessions.end()) {
		if (i.value() != mainSession) { // don't stop main session
			i.value()->stop();
		}
	}
}

int32 state(mtpRequestId requestId) {
	if (requestId > 0) {
		QMutexLocker locker(&requestByDCLock);
		RequestsByDC::iterator i = requestsByDC.find(requestId);
		if (i != requestsByDC.end()) {
			if (internal::Session *session = internal::getSession(qAbs(i.value()))) {
				return session->requestState(requestId);
			}
			return MTP::RequestConnecting;
		}
		return MTP::RequestSent;
	}
	if (internal::Session *session = internal::getSession(-requestId)) {
		return session->requestState(0);
	}
	return MTP::RequestConnecting;
}

void finish() {
	for (Sessions::iterator i = sessions.begin(), e = sessions.end(); i != e; ++i) {
		i.value()->kill();
		delete i.value();
	}
	sessions.clear();
	mainSession = nullptr;

	for_const (internal::Connection *connection, quittingConnections) {
		connection->waitTillFinish();
		delete connection;
	}
	quittingConnections.clear();

	delete _globalSlotCarrier;
	_globalSlotCarrier = nullptr;

	internal::destroyConfigLoader();

	_started = false;
}

void setAuthedId(int32 uid) {
	internal::authed(uid);
}

int32 authedId() {
	return internal::authed();
}

void logoutKeys(RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail) {
	mtpRequestId req = MTP::send(MTPauth_LogOut(), onDone, onFail);
	internal::logoutOtherDCs();
}

void setGlobalDoneHandler(RPCDoneHandlerPtr handler) {
	globalHandler.onDone = handler;
}

void setGlobalFailHandler(RPCFailHandlerPtr handler) {
	globalHandler.onFail = handler;
}

void setStateChangedHandler(MTPStateChangedHandler handler) {
	stateChangedHandler = handler;
}

void setSessionResetHandler(MTPSessionResetHandler handler) {
	sessionResetHandler = handler;
}

void clearGlobalHandlers() {
	setGlobalDoneHandler(RPCDoneHandlerPtr());
	setGlobalFailHandler(RPCFailHandlerPtr());
	setStateChangedHandler(0);
	setSessionResetHandler(0);
}

void updateDcOptions(const QVector<MTPDcOption> &options) {
	internal::updateDcOptions(options);
	Local::writeSettings();
}

AuthKeysMap getKeys() {
	return internal::getAuthKeys();
}

void setKey(int32 dc, AuthKeyPtr key) {
	return internal::setAuthKey(dc, key);
}

QReadWriteLock *dcOptionsMutex() {
	return internal::dcOptionsMutex();
}

} // namespace MTP
