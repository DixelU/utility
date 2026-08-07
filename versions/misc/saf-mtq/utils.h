#ifndef __SAFMTQ__UTILS_H__
#define __SAFMTQ__UTILS_H__

#include <string>
#include <sstream>
#include <chrono>
#include <atomic>
#include <deque>
#include <memory>
#include <condition_variable>
#include <mutex>

#ifdef WITH_OPENCV
#include <opencv2/core.hpp>
#endif

#include <mctx.h>

#include <boost/type_index.hpp>

extern std::chrono::steady_clock::time_point STATIC_INIT_TIMESTAMP;

struct CtxKvIt
{
	dixelu::mctx& ref;
	CtxKvIt(dixelu::mctx& ref) : ref(ref) {}
	CtxKvIt(const dixelu::mctx& ref) : ref(const_cast<dixelu::mctx&>(ref)) {}
	[[nodiscard]] auto begin() const -> decltype(ref.kvbegin()) { return ref.kvbegin(); }
	[[nodiscard]] auto end() const -> decltype(ref.kvend()) { return ref.kvend(); }
	[[nodiscard]] auto cbegin() const -> decltype(ref.kvbegin()) { return ref.kvbegin(); }
	[[nodiscard]] auto cend() const -> decltype(ref.kvend()) { return ref.kvend(); }
};

uint64_t millisecondsFrom(std::chrono::steady_clock::time_point a);
uint64_t nowMcsec();

std::size_t replaceAll(std::string& inout, const std::string &what, const std::string &with);
std::size_t removeAll(std::string& inout, const std::string &what);

std::string templateNameToMethodRecognizerFilename(const std::string& name);
std::string templateNameToRecognizerMethodName(const std::string& name);
std::string recognizerMethodNameToTemplateName(const std::string& name);

dixelu::mctx queryStringToContext(const std::string& queryString);

void openFileExperiment(std::string& filename);

// prints the explanatory string of an exception. If the exception is nested,
// recurses to print the explanatory of the exception it holds
// https://en.cppreference.com/w/cpp/error/nested_exception
std::string formatException(const std::exception& e, int level =  0);

template<typename F>
std::int64_t measureFunctor(F&& f)
{
	auto begin = std::chrono::steady_clock::now();
	f();
	auto end = std::chrono::steady_clock::now();
	return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
}

// SIMD friendly even for -O2 xd
auto bitspread_u8(std::uint8_t input) -> std::uint16_t;

// this one struggles to properly unroll into SIMD instructions, force __attribute__((noinline))
auto bitspread_u32(std::uint32_t input) -> std::uint64_t;

template<typename OnDestroyFunctor>
class OnDestroyExecutor
{
public:
	OnDestroyExecutor(OnDestroyFunctor&& func) : _f(std::move(func)) {}
	OnDestroyExecutor(OnDestroyExecutor&& ode) : _f(std::move(ode._f)) {}
	OnDestroyExecutor& operator=(OnDestroyExecutor&& ode)
	{
		_f = std::move(ode._f);
		return *this;
	}
	virtual ~OnDestroyExecutor() { safeFunctorExecutor(nullptr); }
private:

	template<typename F = OnDestroyFunctor>
	void safeFunctorExecutor(
		typename std::enable_if<std::is_same<decltype(bool(std::declval<F>())), bool>::value, void*>::type _ = nullptr)
	{
		if(_f) _f();
	}

	template<typename = OnDestroyFunctor>
	void safeFunctorExecutor(...)
	{
		_f();
	}

	OnDestroyFunctor _f;
};

template<typename OnDestroyFunctor>
OnDestroyExecutor<OnDestroyFunctor> makeOnDestroyExecutor(OnDestroyFunctor&& func)
{
	return OnDestroyExecutor<OnDestroyFunctor>(std::move(func));
}

template<typename Class>
struct RestrictedAccessContainer
{
	using Ptr = std::shared_ptr<RestrictedAccessContainer<Class>>;

	RestrictedAccessContainer():
		_block(std::make_shared<ControlBlock>())
	{}
	RestrictedAccessContainer(Class&& _classElement):
		_block(std::make_shared<ControlBlock>())
	{
		_block->_ptr.reset(new Class(std::move(_classElement)));
	}

	RestrictedAccessContainer(const RestrictedAccessContainer& other) = default;
	RestrictedAccessContainer& operator=(const RestrictedAccessContainer& other) = default;

	~RestrictedAccessContainer()
	{
		forceCloseAllAccesses();
	}

	inline void forceCloseAllAccesses()
	{
		auto controlBlockCopy = _block;
		std::unique_lock<std::mutex> locker(controlBlockCopy->_mutex);
		controlBlockCopy->_stop = true;
		controlBlockCopy->_ptr.reset();
		controlBlockCopy->_nonnullCondVar.notify_all();
	}

	inline operator bool() const
	{
		auto controlBlockCopy = _block;
		return controlBlockCopy->_ptr.get();
	}

	inline void reset()
	{
		auto controlBlockCopy = _block;
		std::unique_lock<std::mutex> locker(controlBlockCopy->_mutex);
		controlBlockCopy->_ptr.reset();
		//controlBlockCopy->_stop = false;
	}

	inline void operator=(Class&& _classElement)
	{
		auto controlBlockCopy = _block;
		std::unique_lock<std::mutex> locker(controlBlockCopy->_mutex);
		if(controlBlockCopy->_stop)
			return;
		controlBlockCopy->_ptr.reset(new Class(std::move(_classElement)));
		//controlBlockCopy->_stop = false;
		controlBlockCopy->_nonnullCondVar.notify_all();
	}

	template<typename F>
	inline void access(F func)
	{
		auto controlBlockCopy = _block;
		std::unique_lock<std::mutex> locker(controlBlockCopy->_mutex);
		if(!controlBlockCopy->_ptr && !controlBlockCopy->_stop)
		{
			controlBlockCopy->_nonnullCondVar.wait(locker, [&](){
				return controlBlockCopy->_ptr || controlBlockCopy->_stop;
			});
		}

		if(!controlBlockCopy->_stop)
			func(*controlBlockCopy->_ptr);
	}

	template<typename F>
	inline void no_wait_access(F func)
	{
		auto controlBlockCopy = _block;
		std::unique_lock<std::mutex> locker(controlBlockCopy->_mutex);
		if(!controlBlockCopy->_stop && controlBlockCopy->_ptr)
			func(*controlBlockCopy->_ptr);
	}

private:

	struct ControlBlock
	{
		std::atomic_bool _stop;
		std::mutex _mutex;
		std::condition_variable _nonnullCondVar;
		std::unique_ptr<Class> _ptr;

		ControlBlock(): _stop(false) {}
	};
	std::shared_ptr<ControlBlock> _block;
};


class SingleInstanceData;
class LifetimeSleepingMarker;
class LifetimeRunningMarker;
struct InstancesLogger;

class ActiveBlockData
{
	static constexpr size_t momentalRollingCoef = 1;
	static constexpr size_t shortTermRollingCoef = 5;
	static constexpr size_t rollingCoef = 12;

	volatile bool _isRunning {false}; // 0 = sleeping
	volatile uint64_t _lastSwitchTimestamp{0};
	volatile uint64_t _lastSwitchInterval{0};
	uint64_t _totalRunningTime{0};
	uint64_t _totalSleepingTime{0};
	uint64_t _shortTermRunningTime{0};
	uint64_t _shortTermSleepingTime{0};

	volatile float _shortTermAverageBlockRunTime{0};
	volatile float _averageBlockRunTime{0};
	volatile float _shortTermAverageFPS{0};
	volatile float _averageFPS{0};

	void switchState(bool state);

	friend class LifetimeRunningMarker;
	friend class LifetimeSleepingMarker;
	friend struct InstancesLogger;
};

class SingleInstanceData:
	public std::enable_shared_from_this<SingleInstanceData>
{
public:
	SingleInstanceData(std::string&& name, std::shared_ptr<ActiveBlockData>& ptr);
private:
	const std::string _name;
	std::weak_ptr<ActiveBlockData> _activeBlockWPtr;
	friend class LifetimeRunningMarker;
	friend class LifetimeSleepingMarker;
	friend struct InstancesLogger;
public:
	LifetimeSleepingMarker makeSleepingMarker();
	LifetimeRunningMarker makeRunningMarker();
};

class LifetimeSleepingMarker
{
	std::shared_ptr<SingleInstanceData> _ptr;
	friend class SingleInstanceData;
	LifetimeSleepingMarker(decltype(_ptr)&& ptr);
public:
	~LifetimeSleepingMarker();
};

class LifetimeRunningMarker
{
	std::shared_ptr<SingleInstanceData> _ptr;
	friend class SingleInstanceData;
	LifetimeRunningMarker(decltype(_ptr)&& ptr);
public:
	~LifetimeRunningMarker();
};


struct InstancesLogger
{
	struct InstanceData
	{
		std::shared_ptr<SingleInstanceData> _1;
		std::shared_ptr<ActiveBlockData> _2;
	};

	static std::shared_ptr<InstancesLogger> getInstance()
	{
		static std::shared_ptr<InstancesLogger> _instance{std::make_shared<InstancesLogger>()};
		return _instance;
	}

	std::shared_ptr<InstanceData> makeInstanceData(std::string&& instanceName);

#ifdef WITH_OPENCV
	cv::Mat makePreview(size_t width, size_t height) const;
	void drawPreview(cv::Mat& mat) const;
#endif

	dixelu::mctx getState();

	using InstanceDataPtr = std::shared_ptr<InstanceData>;
private:

#ifdef WITH_OPENCV
	static void __drawPreview(
		cv::Mat& targetImage, std::deque<std::shared_ptr<SingleInstanceData>>& instancesList);
#endif

	std::deque<std::shared_ptr<SingleInstanceData>> getInstances() const;

	void purgeDeadInstances();

	mutable std::mutex _listMutex;
	std::deque<std::shared_ptr<SingleInstanceData>> _instances;
};


InstancesLogger::InstanceDataPtr makeInstanceData(
	std::string&& instanceName);

LifetimeSleepingMarker makeSleepingMarker(
	const std::shared_ptr<InstancesLogger::InstanceData>& data);

LifetimeRunningMarker makeRunningMarker(
	const std::shared_ptr<InstancesLogger::InstanceData>& data);

template<typename T>
inline static std::string getPrettyClassName()
{
	auto className = boost::typeindex::type_id<T>().pretty_name();

	std::size_t pos = className.rfind("::");

	if (pos != std::string::npos) {
		className = className.substr(pos + 2);  // +2 - длина "::"
	}
//	removeAll(className, "(anonymous namespace)");
//	removeAll(className, "{anonymous}");

//	const std::string doubleColon = "::";

//	while (className.find(doubleColon) == 0) {
//		className.erase(0, doubleColon.length());
//	}

//	std::string::size_type pos = 0;
//	while ((pos = className.find(doubleColon, pos)) != std::string::npos) {
//		if (pos + doubleColon.length() < className.length() && className.substr(pos + doubleColon.length(), doubleColon.length()) == doubleColon) {
//			className.erase(pos, doubleColon.length());
//		} else {
//			pos += doubleColon.length();
//		}
//	}


	return className;
}

#endif // __SAFMTQ__UTILS_H__
