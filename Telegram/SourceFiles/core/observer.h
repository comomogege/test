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

#include "core/vector_of_moveable.h"
#include "core/type_traits.h"

namespace base {
namespace internal {

using ObservableCallHandlers = base::lambda_unique<void()>;
void RegisterPendingObservable(ObservableCallHandlers *handlers);
void UnregisterActiveObservable(ObservableCallHandlers *handlers);
void UnregisterObservable(ObservableCallHandlers *handlers);

template <typename EventType>
using EventParamType = typename base::type_traits<EventType>::parameter_type;

template <typename EventType>
struct SubscriptionHandlerHelper {
	using type = base::lambda_unique<void(EventParamType<EventType>)>;
};

template <>
struct SubscriptionHandlerHelper<void> {
	using type = base::lambda_unique<void()>;
};

template <typename EventType>
using SubscriptionHandler = typename SubscriptionHandlerHelper<EventType>::type;

// Required because QShared/WeakPointer can't point to void.
class BaseObservableData {
};

template <typename EventType, typename Handler>
class CommonObservableData;

template <typename EventType, typename Handler>
class ObservableData;

} // namespace internal

class Subscription {
public:
	Subscription() = default;
	Subscription(const Subscription &) = delete;
	Subscription &operator=(const Subscription &) = delete;
	Subscription(Subscription &&other) : _node(base::take(other._node)), _removeMethod(other._removeMethod) {
	}
	Subscription &operator=(Subscription &&other) {
		qSwap(_node, other._node);
		qSwap(_removeMethod, other._removeMethod);
		return *this;
	}
	explicit operator bool() const {
		return (_node != nullptr);
	}
	void destroy() {
		if (_node) {
			(*_removeMethod)(_node);
			delete _node;
			_node = nullptr;
		}
	}
	~Subscription() {
		destroy();
	}

private:
	struct Node {
		Node(const QSharedPointer<internal::BaseObservableData> &observable) : observable(observable) {
		}
		Node *next = nullptr;
		Node *prev = nullptr;
		QWeakPointer<internal::BaseObservableData> observable;
	};
	using RemoveMethod = void(*)(Node*);
	Subscription(Node *node, RemoveMethod removeMethod) : _node(node), _removeMethod(removeMethod) {
	}

	Node *_node = nullptr;
	RemoveMethod _removeMethod;

	template <typename EventType, typename Handler>
	friend class internal::CommonObservableData;

	template <typename EventType, typename Handler>
	friend class internal::ObservableData;

};

namespace internal {

template <typename EventType, typename Handler, bool EventTypeIsSimple>
class BaseObservable;

template <typename EventType, typename Handler>
class CommonObservable {
public:
	Subscription add_subscription(Handler &&handler) {
		if (!_data) {
			_data = MakeShared<ObservableData<EventType, Handler>>(this);
		}
		return _data->append(std_::forward<Handler>(handler));
	}

private:
	QSharedPointer<ObservableData<EventType, Handler>> _data;

	friend class CommonObservableData<EventType, Handler>;
	friend class BaseObservable<EventType, Handler, base::type_traits<EventType>::is_fast_copy_type::value>;

};

template <typename EventType, typename Handler>
class BaseObservable<EventType, Handler, true> : public internal::CommonObservable<EventType, Handler> {
public:
	void notify(EventType event, bool sync = false) {
		if (this->_data) {
			this->_data->notify(std_::move(event), sync);
		}
	}

};

template <typename EventType, typename Handler>
class BaseObservable<EventType, Handler, false> : public internal::CommonObservable<EventType, Handler> {
public:
	void notify(EventType &&event, bool sync = false) {
		if (this->_data) {
			this->_data->notify(std_::move(event), sync);
		}
	}
	void notify(const EventType &event, bool sync = false) {
		if (this->_data) {
			this->_data->notify(EventType(event), sync);
		}
	}

};

} // namespace internal

namespace internal {

template <typename EventType, typename Handler>
class CommonObservableData : public BaseObservableData {
public:
	CommonObservableData(CommonObservable<EventType, Handler> *observable) : _observable(observable) {
	}

	Subscription append(Handler &&handler) {
		auto node = new Node(_observable->_data, std_::forward<Handler>(handler));
		if (_begin) {
			_end->next = node;
			node->prev = _end;
			_end = node;
		} else {
			_begin = _end = node;
		}
		return { _end, &CommonObservableData::destroyNode };
	}

	bool empty() const {
		return !_begin;
	}

private:
	struct Node : public Subscription::Node {
		Node(const QSharedPointer<BaseObservableData> &observer, Handler &&handler) : Subscription::Node(observer), handler(std_::move(handler)) {
		}
		Handler handler;
	};

	void remove(Subscription::Node *node) {
		if (node->prev) {
			node->prev->next = node->next;
		}
		if (node->next) {
			node->next->prev = node->prev;
		}
		if (_begin == node) {
			_begin = static_cast<Node*>(node->next);
		}
		if (_end == node) {
			_end = static_cast<Node*>(node->prev);
		}
		if (_current == node) {
			_current = static_cast<Node*>(node->prev);
		} else if (!_begin) {
			_observable->_data.reset();
		}
	}

	static void destroyNode(Subscription::Node *node) {
		if (auto that = node->observable.toStrongRef()) {
			static_cast<CommonObservableData*>(that.data())->remove(node);
		}
	}

	template <typename CallCurrent>
	void notifyEnumerate(CallCurrent callCurrent) {
		_current = _begin;
		do {
			callCurrent();
			if (_current) {
				_current = static_cast<Node*>(_current->next);
			} else if (_begin) {
				_current = _begin;
			} else {
				break;
			}
		} while (_current);

		if (empty()) {
			_observable->_data.reset();
		}
	}

	CommonObservable<EventType, Handler> *_observable = nullptr;
	Node *_begin = nullptr;
	Node *_current = nullptr;
	Node *_end = nullptr;
	ObservableCallHandlers _callHandlers;

	friend class ObservableData<EventType, Handler>;

};

template <typename EventType, typename Handler>
class ObservableData : public CommonObservableData<EventType, Handler> {
public:
	using CommonObservableData<EventType, Handler>::CommonObservableData;

	void notify(EventType &&event, bool sync) {
		if (_handling) {
			sync = false;
		}
		if (sync) {
			_events.push_back(std_::move(event));
			callHandlers();
		} else {
			if (!this->_callHandlers) {
				this->_callHandlers = [this]() {
					callHandlers();
				};
			}
			if (_events.empty()) {
				RegisterPendingObservable(&this->_callHandlers);
			}
			_events.push_back(std_::move(event));
		}
	}

	~ObservableData() {
		UnregisterObservable(&this->_callHandlers);
	}

private:
	void callHandlers() {
		_handling = true;
		auto events = base::take(_events);
		for (auto &event : events) {
			this->notifyEnumerate([this, &event]() {
				this->_current->handler(event);
			});
		}
		_handling = false;
		UnregisterActiveObservable(&this->_callHandlers);
	}

	std_::vector_of_moveable<EventType> _events;
	bool _handling = false;

};

template <class Handler>
class ObservableData<void, Handler> : public CommonObservableData<void, Handler> {
public:
	using CommonObservableData<void, Handler>::CommonObservableData;

	void notify(bool sync) {
		if (_handling) {
			sync = false;
		}
		if (sync) {
			++_eventsCount;
			callHandlers();
		} else {
			if (!this->_callHandlers) {
				this->_callHandlers = [this]() {
					callHandlers();
				};
			}
			if (!_eventsCount) {
				RegisterPendingObservable(&this->_callHandlers);
			}
			++_eventsCount;
		}
	}

	~ObservableData() {
		UnregisterObservable(&this->_callHandlers);
	}

private:
	void callHandlers() {
		_handling = true;
		auto eventsCount = base::take(_eventsCount);
		for (int i = 0; i != eventsCount; ++i) {
			this->notifyEnumerate([this]() {
				this->_current->handler();
			});
		}
		_handling = false;
		UnregisterActiveObservable(&this->_callHandlers);
	}

	int _eventsCount = 0;
	bool _handling = false;

};

template <typename Handler>
class BaseObservable<void, Handler, base::type_traits<void>::is_fast_copy_type::value> : public internal::CommonObservable<void, Handler> {
public:
	void notify(bool sync = false) {
		if (this->_data) {
			this->_data->notify(sync);
		}
	}

};

} // namespace internal

template <typename EventType, typename Handler = internal::SubscriptionHandler<EventType>>
class Observable : public internal::BaseObservable<EventType, Handler, base::type_traits<EventType>::is_fast_copy_type::value> {
};

class Subscriber {
protected:
	template <typename EventType, typename Handler, typename Lambda>
	int subscribe(base::Observable<EventType, Handler> &observable, Lambda &&handler) {
		_subscriptions.push_back(observable.add_subscription(std_::forward<Lambda>(handler)));
		return _subscriptions.size();
	}

	template <typename EventType, typename Handler, typename Lambda>
	int subscribe(base::Observable<EventType, Handler> *observable, Lambda &&handler) {
		return subscribe(*observable, std_::forward<Lambda>(handler));
	}

	void unsubscribe(int index) {
		if (!index) return;
		t_assert(index > 0 && index <= _subscriptions.size());
		_subscriptions[index - 1].destroy();
		if (index == _subscriptions.size()) {
			while (index > 0 && !_subscriptions[--index]) {
				_subscriptions.pop_back();
			}
		}
	}

	~Subscriber() {
		auto subscriptions = base::take(_subscriptions);
		for (auto &subscription : subscriptions) {
			subscription.destroy();
		}
	}

private:
	std_::vector_of_moveable<base::Subscription> _subscriptions;

};

void HandleObservables();

} // namespace base
