#ifndef DIXELU_SPOILABLE_FUTURE_H
#define DIXELU_SPOILABLE_FUTURE_H

#include <stdexcept>
#include <iostream>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <mutex>

namespace dixelu
{

namespace spoilable_future
{

namespace details
{

enum class status
{
	yet_empty = 0,
	spoiled,
	ready,
	moved_from
};

template<typename T>
struct future_state
{
	using status = status;
	std::mutex _locker;
	std::condition_variable _cond;
	std::unique_ptr<T> _data;
	std::atomic_size_t _promisesCount;
	std::atomic<status> _status;

	future_state():
		_data(),
		_promisesCount(0),
		_status(status::yet_empty)
	{

	}
};

template<bool value>
struct enable_default_copyable
{
	template<typename T>
	static void implement_copy_trait(
		typename std::remove_cv<T>::type& target,
		const typename std::remove_cv<T>::type& source) { target = source; }
};

template<>
struct enable_default_copyable<false>
{
	template<typename T>
	static void implement_copy_trait(
		typename std::remove_cv<T>::type& target,
		const typename std::remove_cv<T>::type& source) = delete;
};

struct copyable_movable_mutex_base
{
	std::mutex _mtx;
	copyable_movable_mutex_base() noexcept : _mtx() {}
	copyable_movable_mutex_base(const copyable_movable_mutex_base&) noexcept : _mtx() {}
	copyable_movable_mutex_base(copyable_movable_mutex_base&&) noexcept : _mtx() {}
	copyable_movable_mutex_base& operator=(const copyable_movable_mutex_base&) noexcept { return *this; }
	copyable_movable_mutex_base& operator=(copyable_movable_mutex_base&&) noexcept { return *this; }
	~copyable_movable_mutex_base() = default;
};

} // namespace details

template<typename T, bool _shared, bool _waitless, bool _reusable>
class promise;

template<typename T, bool _shared, bool _waitless, bool _reusable>
class future:
	details::enable_default_copyable<_shared>
{
	friend class promise<T, _shared, _waitless, _reusable>;
	using state = details::future_state<T>;
	using cv_removed_T_t = typename std::remove_cv<T>::type;

	std::shared_ptr<state> _state;

	explicit future(std::shared_ptr<state>&& promiseState):
		_state(std::move(promiseState))
	{}

	template<bool is_shared>
	typename std::enable_if<is_shared, const cv_removed_T_t&>::type
		__unsafe_get_from_state()
	{
		return *_state->_data;
	}

	template<bool is_shared>
	typename std::enable_if<(!is_shared), cv_removed_T_t>::type
		__unsafe_get_from_state()
	{
		_state->_status = state::status::moved_from;
		cv_removed_T_t movedIntoValue = std::move(*_state->_data);
		return movedIntoValue; // use RVO
	}

public:
	using state_status = typename state::status;

	future() = default;
	future(future&& rhs) noexcept:
		_state(std::move(rhs._state)) {}

	future& operator=(future&& rhs) noexcept
	{
		_state = std::move(rhs._state);
		return *this;
	}

	future(const future& source)
	{
		details::enable_default_copyable<_shared>
		    ::template implement_copy_trait<decltype(_state)>(_state, source._state);
	}

	future& operator=(const future& source)
	{
		details::enable_default_copyable<_shared>
		    ::template implement_copy_trait<decltype(_state)>(_state, source._state);
		return *this;
	}

	typename std::conditional<_shared, const cv_removed_T_t&, cv_removed_T_t>::type
		get()
	{
		if(!_state)
			throw std::runtime_error("No future state");

		std::unique_lock<std::mutex> locker(_state->_locker);

		if(_waitless && _state->_status != state::status::ready)
			throw std::runtime_error("Waitless future is not yet ready");

		if(!_waitless)
		{
			_state->_cond.wait(locker, [this]() -> bool {
				auto current_status = _state->_status.load();
				return current_status != state::status::yet_empty && (
					!_reusable || current_status != state::status::moved_from);
			});

			if(_state->_status != state::status::ready)
			{
				auto serialisedValue = std::to_string((int)_state->_status.load());
				throw std::runtime_error("Future is not ready " + serialisedValue);
			}
		}

		return __unsafe_get_from_state<_shared>();
	}

	bool valid() const
	{
		return static_cast<bool>(_state.get());
	}

	template<bool waitless = _waitless>
	typename std::enable_if<(!waitless), typename state::status>::type wait() const
	{
		if(!_state)
			throw std::runtime_error("No future state");

		std::unique_lock<std::mutex> locker(_state->_locker);

		_state->_cond.wait(locker, [this]() -> bool {
			auto current_status = _state->_status.load();
			return current_status != state::status::yet_empty && (
				!_reusable || current_status != state::status::moved_from);
		});

		return _state->_status.load();
	}

	template< class Rep, class Period, bool waitless = _waitless>
	typename std::enable_if<(!waitless), typename state::status>::type
		wait_for(const std::chrono::duration<Rep, Period>& rel_time) const
	{
		if(!_state)
			throw std::runtime_error("No future state");

		std::unique_lock<std::mutex> locker(_state->_locker);
		_state->_cond.wait_for(locker, rel_time, [this]() -> bool {
			auto current_status = _state->_status.load();
			return current_status != state::status::yet_empty && (
				!_reusable || current_status != state::status::moved_from);
		});

		return _state->_status.load();
	}

	state_status get_state() const
	{
		if(!_state)
			throw std::runtime_error("No future state");

		return _state->_status.load();
	}
};

template<typename T, bool _shared, bool _waitless, bool _reusable>
class promise:
	details::copyable_movable_mutex_base
{
	using state = details::future_state<T>;
public:
	using associated_future_t = future<T, _shared, _waitless, _reusable>;
private:

	std::shared_ptr<state> _state{nullptr};

	std::shared_ptr<state> getStateWithConstruction()
	{
		if(!_state)
		{
			_state = std::make_shared<state>();
			_state->_promisesCount = 1;
		}
		return _state;
	}

	void unbindCurrentState() noexcept
	{
		if(!_state)
			return;

		std::unique_lock<std::mutex> locker(_state->_locker);

		--_state->_promisesCount;
		if(_state->_promisesCount == 0 && _state->_status == state::status::yet_empty)
			_state->_status = state::status::spoiled;
		if(!_waitless)
			_state->_cond.notify_all();

		locker.unlock();
		_state.reset();
	}

public:
	promise() = default;
	promise(promise&& rhs) noexcept:
		details::copyable_movable_mutex_base(),
		_state(std::move(rhs._state)) {}

	promise(const promise& lhs):
		details::copyable_movable_mutex_base(),
		_state(lhs._state)
	{
		if(_state)
			++_state->_promisesCount;
	}

	~promise()
	{
		std::unique_lock<std::mutex> stateLocker(_mtx);
		unbindCurrentState();
	}

	void reset()
	{
		std::unique_lock<std::mutex> stateLocker(_mtx);
		if(!_shared)
			return _state.reset();

		if(!_state)
			return;

		std::unique_lock<std::mutex> locker(_state->_locker);
		_state->_status = state::status::yet_empty;
		_state->_data.reset();
	}

	promise& operator=(const promise& lhs)
	{
		std::unique_lock<std::mutex> stateLocker(_mtx);

		if(lhs._state != _state)
			unbindCurrentState();
		_state = lhs._state;
		return *this;
	}
	promise& operator=(promise&& lhs) noexcept
	{
		std::unique_lock<std::mutex> stateLocker(_mtx);

		if(lhs._state != _state)
			unbindCurrentState();
		_state = std::move(lhs._state);
		return *this;
	}

	associated_future_t get_future()
	{
		std::unique_lock<std::mutex> stateLocker(_mtx);

		auto currentState = getStateWithConstruction();
		return associated_future_t(std::move(currentState));
	}

	void set_value(T&& value)
	{
		std::unique_lock<std::mutex> stateLocker(_mtx);

		auto currentState = getStateWithConstruction();
		std::unique_lock<std::mutex> locker(currentState->_locker);

		if(currentState->_data && !_reusable)
			throw std::runtime_error("set_value to already set non-reusable promise");
		
		currentState->_data.reset(new T(std::move(value)));
		currentState->_status = state::status::ready;

		if(!_waitless)
			currentState->_cond.notify_all();
	}

	void set_value(const T& value) { set_value((typename std::remove_cv<T>::type)value); }
};

} // spoilable future

template<typename T>
using promise = spoilable_future::promise<T, false, false, false>;
template<typename T>
using future = spoilable_future::future<T, false, false, false>;

using future_status = spoilable_future::details::status;

// <typename T, bool _shared, bool _waitless, bool _reusable>
template<typename T>
using waitless_promise = spoilable_future::promise<T, false, true, false>;
template<typename T>
using waitless_future = spoilable_future::future<T, false, true, false>;
template<typename T>
using shared_waitless_promise = spoilable_future::promise<T, true, true, false>;
template<typename T>
using shared_waitless_future = spoilable_future::future<T, true, true, false>;
template<typename T>
using shared_promise = spoilable_future::promise<T, true, false, false>;
template<typename T>
using shared_future = spoilable_future::future<T, true, false, false>;
template<typename T>
using shared_promise = spoilable_future::promise<T, true, false, false>;
template<typename T>
using shared_future = spoilable_future::future<T, true, false, false>;
template<typename T>
using reusable_promise = spoilable_future::promise<T, false, false, true>;
template<typename T>
using reusable_future = spoilable_future::future<T, false, false, true>;
template<typename T>
using shared_waitless_reusable_promise = spoilable_future::promise<T, true, true, true>;
template<typename T>
using shared_waitless_reusable_future = spoilable_future::future<T, true, true, true>;

} // namespace dixelu

#endif //DIXELU_SPOILABLE_FUTURE_H