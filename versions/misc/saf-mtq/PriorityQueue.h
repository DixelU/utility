#ifndef TDV_DATA_PRIORITYQUEUE_H_
#define TDV_DATA_PRIORITYQUEUE_H_

#include <condition_variable>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>
#include <future>
#include <mutex>
#include <set>

namespace tdv
{
namespace data
{

class QueueEmpty: public std::runtime_error
{
public:
	QueueEmpty();
};

class ThreadReleased: public std::runtime_error
{
public:
	ThreadReleased();
};

template<typename T>
class PriorityQueue
{
public:
	using Ptr = std::shared_ptr<PriorityQueue<T>>;

	PriorityQueue(uint32_t fixedSize = 64);
	~PriorityQueue();

	void releaseThread(std::thread::id threadID) const;

	void put(const T &data, int priority);

	T get(bool nowait = false);

	uint32_t size() const;

private:
	using TimePoint = std::chrono::time_point<std::chrono::system_clock>;

	struct QueueItem
	{
		T data; // must be trivially copyable
		int priority;
		TimePoint tp;

		bool operator<(const QueueItem& item) const;
	};

	void unsafePut(const T &data, int priority);

	const uint32_t _maxCapacity;
	mutable std::condition_variable _cv;
	mutable std::mutex _mtx;
	/* we can use google's btree_multiset instead of std::mulstiset
	 * -75% of runtime overhead
	 * -25% memory consumption
	 * + memory locality (at least expected)
	 * + fast traversal
	 */
	std::multiset<QueueItem> _container;

	// todo: more elegant way of breaking the waiting;
	std::shared_ptr<std::atomic_bool> _shutdownFlag;

	//mutable std::set<std::thread::id> _releasedThreads;
	TimePoint _smallestCurrentTimepoint;
};

template<typename T>
uint32_t PriorityQueue<T>::size() const
{
	return _container.size();
}

template<typename T>
bool PriorityQueue<T>::QueueItem::operator<(const QueueItem& item) const
{
	return (priority < item.priority) || (priority == item.priority && ((tp - item.tp).count() < 0));
}

template<typename T>
PriorityQueue<T>::~PriorityQueue()
{
	*_shutdownFlag = true;
	_cv.notify_all();
}

template<typename T>
PriorityQueue<T>::PriorityQueue(uint32_t fixedSize):
	_maxCapacity(fixedSize),
	_shutdownFlag(std::make_shared<std::atomic_bool>(false)),
	_smallestCurrentTimepoint(std::chrono::system_clock::now())
{
}

template<typename T>
void PriorityQueue<T>::unsafePut(const T &data, int priority)
{
	if(_container.size() == _maxCapacity)
	{
		_container.erase(_container.begin());
	}
	_container.insert({data, priority, std::chrono::system_clock::now()});
}

template<typename T>
void PriorityQueue<T>::releaseThread(std::thread::id /*threadID*/) const
{
	std::unique_lock<std::mutex> lock(_mtx);

	//_releasedThreads.insert(threadID);
	*_shutdownFlag = true;
	_cv.notify_all();
}

template<typename T>
void PriorityQueue<T>::put(const T &data, int priority)
{
	std::lock_guard<std::mutex> locker(_mtx);
	unsafePut(data, priority);
	_cv.notify_one();
}

template<typename T>
T PriorityQueue<T>::get(bool nowait)
{
	auto& shutdownFlag = *_shutdownFlag;
	auto tryToRelease = [&shutdownFlag]()
	{
		if (shutdownFlag)
			throw ThreadReleased();
	};

	while (true)
	{
		std::unique_lock<std::mutex> lock(_mtx);

		tryToRelease();

		if (!_container.empty())
		{
			auto maxPriority = (--_container.end())->priority;
			auto it = _container.lower_bound({T(), maxPriority, _smallestCurrentTimepoint});
			QueueItem data = std::move(*it);
			_container.erase(it);

			return data.data;
		}
		else
			nowait ? throw QueueEmpty() : _cv.wait(lock);
	}
}


} // namespace data
} // namespace tdv

#endif //TDV_DATA_PRIORITYQUEUE_H_
