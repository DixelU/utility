//
// Created by stranger on 27.09.23.
//

#ifndef SAFMTQ_BASICTHREADPOOL_H
#define SAFMTQ_BASICTHREADPOOL_H

#include <Logger.h>

#include <deque>
#include <atomic>
#include <thread>
#include <condition_variable>

template <typename Function, bool withElementSkip = true>
class BasicThreadPool
{
private:
	static constexpr size_t suspiciousTaskQueueSize = 50;
	std::shared_ptr<Logger> _logger;
	const std::string _poolName;

	std::deque<Function> _tasksQueue;
	std::deque<std::thread> _threads;
	std::mutex _mutex;
	std::condition_variable _condition;
	std::atomic_bool _running;

public:
	BasicThreadPool(
		std::shared_ptr<Logger> logger,
		size_t poolSize = 1,
		std::string poolName = ""):
		_logger(logger),
		_poolName(makeName(poolName)),
		_running(true)
	{
		for (std::size_t i = 0; i < poolSize; ++i)
			_threads.emplace_back(std::bind(&BasicThreadPool::poolMain, this, i));
	}

	void reserveThreads(std::size_t newPoolSize)
	{
		LOG_FUNCTION_ENTRY();

		if(!_running)
			return;

		std::unique_lock<std::mutex> lock(_mutex);
		if(_threads.size() >= newPoolSize)
			return;

		auto preReserveThreadsCount = _threads.size();
		for (std::size_t i = 0; i < newPoolSize - preReserveThreadsCount; ++i)
			_threads.emplace_back(std::bind(&BasicThreadPool::poolMain, this, _threads.size()));
	}

	void truncateThreads(std::size_t newPoolSize)
	{
		LOG_BEGIN_OF_FUNCTION();

		if(!_running)
			return;

		std::unique_lock<std::mutex> lock(_mutex);
		if(_threads.size() < newPoolSize)
		{
			lock.unlock();
			return reserveThreads(newPoolSize);
		}

		{
			std::unique_lock<std::mutex> shutdownLock(_mutex);
			_running = false;
			_condition.notify_all();
		}

		for (auto& t : _threads)
			if (t.joinable())
				t.join();

		_running = true;
		reserveThreads(newPoolSize);

		LOG_END_OF_FUNCTION();
	}

	~BasicThreadPool()
	{
		LOG_BEGIN_OF_FUNCTION();

		{
			std::unique_lock<std::mutex> lock(_mutex);
			_running = false;
			_condition.notify_all();
		}

		for (auto& t : _threads)
			if (t.joinable())
				t.join();

		LOG_END_OF_FUNCTION();
	}

	void runFunctor(Function&& functor)
	{
		Function unconditionallyMovedFunctor = std::move(functor);
		size_t currentTaskQueueSize = 0;

		{
			std::unique_lock<std::mutex> lock(_mutex);
			currentTaskQueueSize = _tasksQueue.size();
			if(!withElementSkip || currentTaskQueueSize <= suspiciousTaskQueueSize)
			{
				_tasksQueue.push_back(std::move(unconditionallyMovedFunctor));
				_condition.notify_one();
			}
		}

		if(!withElementSkip && currentTaskQueueSize > suspiciousTaskQueueSize)
			log("Possible " + _poolName + " overflow. Current size: " +
				std::to_string(currentTaskQueueSize), boost::log::trivial::warning);
	}

private:
	void poolMain(size_t threadNumber)
	{
		auto poolInstanceData =
			makeInstanceData(_poolName + "#" + std::to_string(threadNumber));

		while (_running)
		{
			std::unique_lock<std::mutex> lock(_mutex);
			while (_tasksQueue.empty() && _running)
			{
				auto sleepingMarker = makeSleepingMarker(poolInstanceData);
				_condition.wait(lock);
			}

			if (!_running)
				break;

			{
				Function task = _tasksQueue.front();
				_tasksQueue.pop_front();

				lock.unlock();

				task();
			}

			lock.lock();
		}
	}

	DEFINE_SELFLOG_FUNC(QUOTE(BasicThreadPool))

	static std::string makeName(std::string poolName)
	{
		poolName = (poolName.empty()) ? "Unspecified" : poolName;
		poolName = poolName + "@" + getPrettyClassName<Function>();
		return poolName;
	}

	template<typename T>
	inline static std::string getPrettyClassName()
	{
		auto className = boost::typeindex::type_id<T>().pretty_name();
		removeAll(className, "(anonymous namespace)");
		removeAll(className, "{anonymous}");
		return className;
	}
};

#endif //SAFMTQ_BASICTHREADPOOL_H
