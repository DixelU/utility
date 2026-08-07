#include <tdv/utils/timestamp/TimestampISOContainer.h>

#include <time.h>

#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace tdv
{
namespace utils
{
namespace timestamp
{

std::string timestamp_to_iso_8601(uint64_t timestamp, bool useUtc)
{
	auto epoch = std::chrono::time_point<std::chrono::system_clock>();
	auto since_epoch = std::chrono::milliseconds(timestamp);
	auto t = epoch + since_epoch;

	std::time_t time = std::chrono::system_clock::to_time_t(t);
	std::tm now_tm;

	static std::mutex timestampMutex;

	{
		std::lock_guard<std::mutex> locker(timestampMutex);
		// localtime and gmtime is not thread safe (bruh)
		now_tm =
			(useUtc) ?
				*gmtime(&time):
				*localtime(&time);
	}

	std::stringstream ss;

	constexpr int size = 200;
	char buf1[size];
	char buf2[size];
	std::strftime(buf1, size, "%Y-%m-%dT%H:%M:", &now_tm);
	std::strftime(buf2, size, "%z", &now_tm);

	ss << std::setfill('0')
	   << buf1
	   << std::setw(2) << (timestamp / 1000) % 60 << '.'
	   << std::setw(3) << timestamp % 1000 << "000";
	ss << buf2[0] << buf2[1] << buf2[2] << ':' << buf2[3] << buf2[4];
	return ss.str();
}

TimestampISOContainer::TimestampISOContainer(uint64_t timestamp):
	_convertedTimestamp("1970-01-01T00:00:00.000000+00:00"),
	_timestampUsedForConvertion(0),
	_timestamp(timestamp),
	_useUtc(true)
{
}

void TimestampISOContainer::useUTC(bool useUtc) { _useUtc = useUtc; }
uint64_t& TimestampISOContainer::access() { return _timestamp; }
const uint64_t& TimestampISOContainer::access() const { return _timestamp; }
const std::string& TimestampISOContainer::toISO()
{
	if(_timestamp != _timestampUsedForConvertion)
		(_convertedTimestamp =
			timestamp_to_iso_8601(_timestampUsedForConvertion = _timestamp, _useUtc));
	return _convertedTimestamp;
}
bool TimestampISOContainer::isEmpty() const { return !_timestamp; }

} // namespace timestamp;
} // namespace utils;
} // namespace tdv;
