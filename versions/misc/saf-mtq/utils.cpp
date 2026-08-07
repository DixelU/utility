#include "utils.h"

#include <cmath>
#include <time.h>

#include <chrono>
#include <iostream>
#include <iomanip>
#include <deque>
#include <fstream>

#ifdef WITH_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#endif

std::chrono::steady_clock::time_point STATIC_INIT_TIMESTAMP = std::chrono::steady_clock::now();

uint64_t millisecondsFrom(std::chrono::steady_clock::time_point begin)
{
	auto end = std::chrono::steady_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
}

uint64_t nowMcsec()
{
	auto end = std::chrono::steady_clock::now();
	return std::chrono::duration_cast<std::chrono::microseconds>(end - STATIC_INIT_TIMESTAMP).count();
}

std::string formatException(const std::exception& e, int level)
{
	std::string res = std::string(level, ' ') + "exception: " + e.what() + "\n";
	try
	{
		std::rethrow_if_nested(e);
	}
	catch (const std::exception& nestedException)
	{
		return res + formatException(nestedException, level + 1);
	}
	catch (...)
	{
		return res + "%UNKNOWN%\n";
	}
	return res;
}

auto bitspread_u8(std::uint8_t input) -> std::uint16_t
{
	std::uint16_t result = 0;

	result |= static_cast<std::uint16_t>(input & 0b00000001);
	result |= static_cast<std::uint16_t>(input & 0b00000010) << 1;
	result |= static_cast<std::uint16_t>(input & 0b00000100) << 2;
	result |= static_cast<std::uint16_t>(input & 0b00001000) << 3;
	result |= static_cast<std::uint16_t>(input & 0b00010000) << 4;
	result |= static_cast<std::uint16_t>(input & 0b00100000) << 5;
	result |= static_cast<std::uint16_t>(input & 0b01000000) << 6;
	result |= static_cast<std::uint16_t>(input & 0b10000000) << 7;

	return result;
}

auto bitspread_u32(std::uint32_t input) -> std::uint64_t
{
	return
		static_cast<uint64_t>(bitspread_u8(input & 0xFF)) |
		(static_cast<uint64_t>(bitspread_u8((input >> 8) & 0xFF)) << 16) |
		(static_cast<uint64_t>(bitspread_u8((input >> 16) & 0xFF)) << 32) |
		(static_cast<uint64_t>(bitspread_u8((input >> 24) & 0xFF)) << 48);
}

dixelu::mctx queryStringToContext(const std::string& queryString)
{
	dixelu::mctx queryObject;
	auto beginIt = queryString.begin();
	auto endIt = beginIt;
	while(beginIt != queryString.end())
	{
		endIt = std::find(beginIt, queryString.end(), '&');

		std::string singleQueryParameter(beginIt, endIt);
		auto queryParameterValueDelimIt =
			std::find(singleQueryParameter.begin(), singleQueryParameter.end(), '=');
		std::string queryParameterName =
			std::string(singleQueryParameter.begin(), queryParameterValueDelimIt);
		std::string queryParameterValue =
			queryParameterValueDelimIt != singleQueryParameter.end() ?
			std::string(queryParameterValueDelimIt + 1, singleQueryParameter.end()) :
			"";

		queryObject[queryParameterName] = queryParameterValue;

		if(endIt != queryString.end())
			++endIt;
		beginIt = endIt;
	}
	return queryObject;
}

std::size_t replaceAll(std::string& inout, const std::string &what, const std::string &with)
{
	std::size_t count = 0;
	for (std::string::size_type pos{};
		 inout.npos != (pos = inout.find(what.data(), pos, what.length()));
		 pos += with.length(), ++count) {
		inout.replace(pos, what.length(), with.data(), with.length());
	}
	return count;
}

std::size_t removeAll(std::string& inout, const std::string &what)
{
	return replaceAll(inout, what, "");
}

void ActiveBlockData::switchState(bool state)
{
	auto prevState = _isRunning;
	if(prevState == state)
		return;

	_isRunning = state;

	auto currentTimestamp =
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - STATIC_INIT_TIMESTAMP).count();
	auto newSwitchInterval = currentTimestamp - _lastSwitchTimestamp;
	if(!_lastSwitchTimestamp)
		newSwitchInterval = 0;

	if(prevState)
	{
		_totalRunningTime += newSwitchInterval;
		_shortTermRunningTime += newSwitchInterval;
	}
	else
	{
		_totalSleepingTime += newSwitchInterval;
		_shortTermSleepingTime += newSwitchInterval;
	}

	constexpr uint64_t shortTermSpan = (1 << 20); // 1kk mcsecs ~ 1 second

	if(_shortTermRunningTime > shortTermSpan || _shortTermSleepingTime > shortTermSpan)
	{
		_shortTermRunningTime >>= 4; // divide by 16
		_shortTermSleepingTime >>= 4; // division will preserve the runtime ratio
	}

	auto shortTermRunningRatio = double(_shortTermRunningTime) /
		double(_shortTermSleepingTime + _shortTermRunningTime + 1);
	auto averageRunningRatio = double(_totalRunningTime) /
		double(_totalRunningTime + _totalSleepingTime + 1);

	auto newFPS = 1000000.f / ((_lastSwitchInterval + newSwitchInterval));
	if(!std::isnormal(newFPS))
		newFPS = 0;

	_averageBlockRunTime = static_cast<float>(averageRunningRatio);
		//((rollingCoef - 1) * _averageBlockRunTime + ) / (rollingCoef);
	_shortTermAverageBlockRunTime = static_cast<float>(shortTermRunningRatio);
		//((shortTermRollingCoef - 1) * _shortTermAverageBlockRunTime + shortTermRunningRatio) / shortTermRollingCoef;

	auto fpsDifference = _shortTermAverageFPS - newFPS;
	auto fpsAbsoluteDifference = std::abs(fpsDifference);
	constexpr float fpsRelativeDifferenceThreshold = 50;

	if(fpsAbsoluteDifference/(_shortTermAverageFPS + 1) <= fpsRelativeDifferenceThreshold)
		_averageFPS =
			((rollingCoef - 1) * _averageFPS + newFPS) / (rollingCoef);

	_shortTermAverageFPS =
		((momentalRollingCoef - 1) * _shortTermAverageFPS + newFPS) / momentalRollingCoef;

	_lastSwitchTimestamp = currentTimestamp;
	_lastSwitchInterval = newSwitchInterval;
}

SingleInstanceData::SingleInstanceData(std::string&& name, std::shared_ptr<ActiveBlockData>& ptr):
	_name(std::move(name)),
	_activeBlockWPtr(ptr)
{}

LifetimeSleepingMarker SingleInstanceData::makeSleepingMarker()
{
	return LifetimeSleepingMarker(shared_from_this());
}

LifetimeRunningMarker SingleInstanceData::makeRunningMarker()
{
	return LifetimeRunningMarker(shared_from_this());
}

LifetimeSleepingMarker::~LifetimeSleepingMarker()
{
	if(auto ptr = _ptr->_activeBlockWPtr.lock())
		ptr->switchState(true);
}

LifetimeSleepingMarker::LifetimeSleepingMarker(decltype(_ptr)&& ptr):
	_ptr(std::move(ptr))
{
	if(auto ptr = _ptr->_activeBlockWPtr.lock())
		ptr->switchState(false);
}

LifetimeRunningMarker::~LifetimeRunningMarker()
{
	if(auto ptr = _ptr->_activeBlockWPtr.lock())
		ptr->switchState(false);
}

LifetimeRunningMarker::LifetimeRunningMarker(decltype(_ptr)&& ptr):
	_ptr(std::move(ptr))
{
	if(auto ptr = _ptr->_activeBlockWPtr.lock())
		ptr->switchState(true);
}


std::shared_ptr<InstancesLogger::InstanceData> InstancesLogger::makeInstanceData(std::string&& instanceName)
{
	auto instanceData = std::make_shared<InstanceData>();

	instanceData->_2 = std::make_shared<ActiveBlockData>();
	instanceData->_1 = std::make_shared<SingleInstanceData>(
		std::move(instanceName), instanceData->_2);

	{
		std::lock_guard<std::mutex> locker(_listMutex);
		_instances.push_back(instanceData->_1);
	}

	// comment out following line to disable dead instances purge from the instances list.
	purgeDeadInstances();

	return instanceData;
}

#ifdef WITH_OPENCV
cv::Mat InstancesLogger::makePreview(size_t width, size_t height) const
{
	auto instances = getInstances();

	cv::Mat mat(width, height, CV_8UC3, 0);
	__drawPreview(mat, instances);
	return mat;
}

void InstancesLogger::drawPreview(cv::Mat& mat) const
{
	auto instances = getInstances();
	__drawPreview(mat, instances);
}
#endif

void InstancesLogger::purgeDeadInstances()
{
	{
		decltype(_instances) aliveInstances;
		std::lock_guard<std::mutex> locker(_listMutex);
		for(auto& el: _instances)
			if(auto ptr = el->_activeBlockWPtr.lock())
				aliveInstances.push_back(el);
		aliveInstances.swap(_instances);
	}
}

#ifdef WITH_OPENCV
void InstancesLogger::__drawPreview(
	cv::Mat& targetImage,
	std::deque<std::shared_ptr<SingleInstanceData>>& instancesList)
{
	size_t width = targetImage.cols;
	size_t height = targetImage.rows;

	targetImage.setTo(0);

	size_t squaresSideCount = std::ceil(std::sqrt(float(instancesList.size())));

	const size_t
		singleCellWidth = width / squaresSideCount,
		singleCellHeight = height / squaresSideCount;

	for(size_t i = 0; i < squaresSideCount; i++)
	{
		for(size_t j = 0; j < squaresSideCount; j++)
		{
			size_t instancesListIndex = i + squaresSideCount * j;
			if(instancesListIndex >= instancesList.size())
				continue;

			auto& instanceData = instancesList[instancesListIndex];

			cv::Rect roi(
				singleCellWidth * i,
				singleCellHeight * j,
				singleCellWidth,
				singleCellHeight);

			auto roiMat = targetImage(roi);
			auto activeBlock = instanceData->_activeBlockWPtr.lock();

			cv::Scalar fillScalar;
			std::string name = instanceData->_name;;
			double load;
			double averageLoad;
			double fps;
			double averageFPS;

			if(!activeBlock)
			{
				fillScalar = cv::Scalar(0, 0, 0);
				load = 0;
				averageLoad = 0;
				fps = 0;
				averageFPS = 0;
			}
			else
			{
				bool currentState = activeBlock->_isRunning;
				fillScalar = cv::Scalar((1. - currentState) * 0.5, currentState, currentState) * 255;
				load = activeBlock->_shortTermAverageBlockRunTime;
				averageLoad = activeBlock->_averageBlockRunTime;
				fps = activeBlock->_shortTermAverageFPS;
				averageFPS = activeBlock->_averageFPS;
			}

			roiMat.setTo(fillScalar);

			auto textColor = std::round(1. - fillScalar.val[1]) * 255;
			cv::Scalar textColorScalar(textColor, textColor, textColor);

			auto callPutText =
				[&](const std::string &str, float y)
				{
					cv::putText(roiMat,
								str,
								{5, (int)y},
								cv::FONT_HERSHEY_SIMPLEX,
								0.33,
								textColorScalar,
								1,
								cv::LINE_AA,
								false);
				};

			callPutText(name, 10);

			auto loadString = "Load: " + std::to_string(load * 100) + "%";
			callPutText(loadString, 20);

			auto fpsString = "FPS: " + std::to_string(fps);
			callPutText(fpsString, 30);

			auto avgLoadString = "Avg load: " + std::to_string(averageLoad * 100) + "%";
			callPutText(avgLoadString, 40);

			auto avgFPS = "Avg FPS: " + std::to_string(averageFPS);
			callPutText(avgFPS, 50);
		}
	}
}
#endif

dixelu::mctx InstancesLogger::getState()
{
	dixelu::mctx result = dixelu::mctx::make_array();
	auto instances = getInstances();

	for(size_t index = 0; index < instances.size(); ++index)
	{
		dixelu::mctx singleInstanceData;

		auto instanceRawData = instances[index];
		singleInstanceData["name"] = instanceRawData->_name;
		if(auto activeInstanceBlockPtr = instanceRawData->_activeBlockWPtr.lock())
		{
			size_t lastSwitchTimestamp = activeInstanceBlockPtr->_lastSwitchTimestamp;
			bool isRunning = activeInstanceBlockPtr->_isRunning;

			auto currentTimestamp =
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - STATIC_INIT_TIMESTAMP).count();
			auto timeSinceLastSwitch = currentTimestamp - lastSwitchTimestamp;

			singleInstanceData[ isRunning ? "running_for_mcsec" : "sleeping_for_mcsec" ] =
				timeSinceLastSwitch;
			singleInstanceData["state"] =
				(isRunning) ? "running" : "sleeping";
			singleInstanceData["average_load_percentage"] =
				activeInstanceBlockPtr->_averageBlockRunTime * 100;
			singleInstanceData["short_term_load_percentage"] =
				activeInstanceBlockPtr->_shortTermAverageBlockRunTime * 100;
			singleInstanceData["average_fps"] =
				(float)activeInstanceBlockPtr->_averageFPS;
			singleInstanceData["short_term_fps"] =
				(float)activeInstanceBlockPtr->_shortTermAverageFPS;
		}
		else
			singleInstanceData["state"] = "dead";

		result.push_back(std::move(singleInstanceData));
	}

	return result;
}

std::deque<std::shared_ptr<SingleInstanceData>> InstancesLogger::getInstances() const
{
	decltype(_instances) instances;

	{
		std::lock_guard<std::mutex> locker(_listMutex);
		instances = _instances;
	}

	return instances;
}

LifetimeSleepingMarker makeSleepingMarker(
	const std::shared_ptr<InstancesLogger::InstanceData>& data)
{
	return data->_1->makeSleepingMarker();
}

LifetimeRunningMarker makeRunningMarker(
	const std::shared_ptr<InstancesLogger::InstanceData>& data)
{
	return data->_1->makeRunningMarker();
}

InstancesLogger::InstanceDataPtr makeInstanceData(std::string&& instanceName)
{
	return InstancesLogger::getInstance()->makeInstanceData(std::move(instanceName));
}
