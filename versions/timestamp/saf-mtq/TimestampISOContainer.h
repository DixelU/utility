#ifndef TDV_UTILS_TIMESTAMP_TIMESTAMPISOCONTAINER_H
#define TDV_UTILS_TIMESTAMP_TIMESTAMPISOCONTAINER_H

#include <cstdint>
#include <string>

namespace tdv
{
namespace utils
{
namespace timestamp
{

std::string timestamp_to_iso_8601(uint64_t timestamp, bool useUtc = true);

struct TimestampISOContainer
{
private:
	std::string _convertedTimestamp;
	uint64_t _timestampUsedForConvertion;
	uint64_t _timestamp;
	bool _useUtc;
public:
	explicit TimestampISOContainer(uint64_t timestamp = ~0ULL);
	void useUTC(bool useUtc);
	uint64_t& access();
	const uint64_t& access() const;
	const std::string& toISO();
	bool isEmpty() const;
};

} // namespace timestamp;
} // namespace utils;
} // namespace tdv;

#endif
