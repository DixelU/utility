#include <tdv/utils/UuidUtils.h>

#include <random>
#include <chrono>
#include <mutex>
#include <thread>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace
{

std::uint64_t timeSeed()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace ;

namespace tdv
{
namespace utils
{

std::string generateUUID()
{
	using RandomEngine = std::mt19937_64;

	static std::mutex mtx;
	std::lock_guard<std::mutex> locker(mtx); // this is needed because of odd mingw implementation of thread_local-s

	// do not put thread_local here!!!
	static std::random_device rnd;
	static RandomEngine mtEngine(rnd() ^ timeSeed() ^ std::hash<std::thread::id>()(std::this_thread::get_id()));
	static boost::uuids::basic_random_generator<RandomEngine> generator(mtEngine);

	return boost::uuids::to_string(generator());
}

} // namespace utils;
} // namespace tdv;

