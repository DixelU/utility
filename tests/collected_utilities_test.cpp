#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <span>
#include <sstream>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "buffered_object_pool.h"
#include "buffered_queue_spsc.h"
#include "buffered_file_reader.h"
#include "background_worker.h"
#include "base64.h"
#include "fixed_string.h"
#include "function_ref.h"
#include "ip_routing.h"
#include "mctx.h"
#include "mctx_json.h"
#include "memory_mapped_file_reader.h"
#include "on_destroy_executor.h"
#include "scope_exit.h"
#include "spoilable_future.h"
#include "unicode_literal.h"
#include "url.h"

namespace
{

void require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

template<typename F>
bool throws_invalid_argument(F&& function)
{
	try
	{
		std::forward<F>(function)();
		return false;
	}
	catch (const std::invalid_argument&)
	{
		return true;
	}
}

int double_value(int value)
{
	return value * 2;
}

struct counted_value
{
	inline static int live = 0;
	int value;

	explicit counted_value(int value) : value(value) { ++live; }
	~counted_value() { --live; }
};

std::filesystem::path test_file_path(const char* suffix)
{
	static std::atomic_uint counter = 0;
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	return std::filesystem::temp_directory_path() /
		("dixelu_utility_" + std::to_string(stamp) + "_" +
			std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) + suffix);
}

std::filesystem::path test_unicode_file_path(const char8_t* suffix)
{
	auto result = test_file_path("");
	result += std::filesystem::path(suffix);
	return result;
}

void write_test_file(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	require(output.is_open(), "test fixture file must open for writing");
	if (!bytes.empty())
	{
		output.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
	}
	require(output.good(), "test fixture bytes must be written");
}

void test_numeric_traits()
{
	static_assert(std::is_same_v<dixelu::details::try_unsigned<int>::type, unsigned int>);
	static_assert(std::is_same_v<dixelu::details::try_signed<unsigned int>::type, int>);
	static_assert(std::is_same_v<dixelu::details::opposite_sign_type<int>::type, unsigned int>);
	static_assert(std::is_same_v<dixelu::details::opposite_sign_type<unsigned int>::type, int>);
	static_assert(std::is_same_v<dixelu::details::try_signed<float>::type, float>);
}

void test_mctx_regressions()
{
	dixelu::mctx vector_value(std::vector<int>{1, 2, 3, 4});
	require(vector_value.is_array(), "mctx vector construction must create an array");
	require(vector_value.size() == 4, "mctx vector construction must preserve element count");
	require(vector_value[2].get<int>() == 3, "mctx vector construction must convert elements");

	auto first = vector_value.begin();
	++first;
	auto last = vector_value.end();
	--last;
	vector_value.erase(first, last);
	require(
		vector_value.size() == 2 && vector_value[0].get<int>() == 1 && vector_value[1].get<int>() == 4,
		"mctx range erase must erase the complete half-open range");

	dixelu::mctx const_iterator_value(std::vector<int>{1, 2, 3});
	const dixelu::mctx& const_view = const_iterator_value;
	auto const_first = const_view.begin();
	++const_first;
	const_iterator_value.erase(const_first, const_view.end());
	require(
		const_iterator_value.size() == 1 && const_iterator_value[0].get<int>() == 1,
		"mctx range erase must accept const iterators");

	require(dixelu::mctx("-42").get_as<int>(7) == -42, "mctx must parse integral strings");
	require(dixelu::mctx("2.5").get_as<double>(7.0) == 2.5, "mctx must parse floating strings");
	require(dixelu::mctx("2x").get_as<int>(7) == 7, "mctx must reject partial numeric strings");

	dixelu::mctx negative = -42;
	const std::string json = dixelu::mctx_json::serialize(negative);
	require(json == "-42", "mctx JSON must preserve signed integers");
	require(
		dixelu::mctx_json::deserialize(json).get<int>() == -42,
		"mctx JSON signed integers must round trip");
}

void test_function_ref()
{
	dixelu::function_ref<int(int)> free_function(double_value);
	require(free_function(6) == 12, "function_ref must call free functions");

	int offset = 7;
	auto lambda = [&](int value) { return value + offset; };
	dixelu::function_ref<int(int)> lambda_ref(lambda);
	require(lambda_ref(5) == 12, "function_ref must call mutable lvalue functors");

	const auto const_lambda = [](int value) { return value - 1; };
	dixelu::function_ref<int(int)> const_ref(const_lambda);
	require(const_ref(5) == 4, "function_ref must call const lvalue functors");
}

void test_fixed_string()
{
	dixelu::fixed_string<5> value("abcdefgh");
	require(value.view() == "abcde", "fixed_string must truncate at capacity");
	require(value.c_str()[value.size()] == '\0', "fixed_string must be null terminated");

	dixelu::fixed_string<5> same("abcde");
	require(value == same, "fixed_string equality must compare content");
	require(
		std::hash<dixelu::fixed_string<5>>{}(value) ==
			std::hash<dixelu::fixed_string<5>>{}(same),
		"equal fixed strings must hash equally");
}

void test_scope_exit()
{
	int calls = 0;
	{
		auto first = dixelu::make_scope_exit([&] { ++calls; });
		auto moved = std::move(first);
		require(moved.active(), "moved scope_exit must remain active");
	}
	require(calls == 1, "moving scope_exit must not execute twice");

	{
		auto released = dixelu::make_scope_exit([&] { ++calls; });
		released.release();
	}
	require(calls == 1, "released scope_exit must not execute");
}

void test_legacy_scope_and_future_regressions()
{
	int calls = 0;
	{
		auto first = dixelu::make_on_destroy_executor([&] { ++calls; });
		auto moved = std::move(first);
		(void)moved;
	}
	require(calls == 1, "moved on_destroy_executor must not execute twice");

	{
		auto dismissed = dixelu::make_on_destroy_executor([&] { ++calls; });
		dismissed.dismiss();
	}
	require(calls == 1, "dismissed on_destroy_executor must not execute");

	dixelu::promise<int> promise;
	auto future = promise.get_future();
	promise.set_value(42);
	require(future.get() == 42, "spoilable_future promise must deliver its value");
}

void test_object_pool()
{
	using pool_type = dixelu::buffered_object_pool<
		counted_value,
		2 * sizeof(counted_value)>;
	pool_type pool;

	counted_value* first = pool.create(1);
	counted_value* second = pool.create(2);
	const auto original_address = first;
	pool.destroy(first);
	first = pool.create(3);

	require(first == original_address, "object pool must recycle released slots");
	require(first->value == 3 && second->value == 2, "object pool must construct values");
	require(pool.get_statistics().live == 2, "object pool must count live objects");
	require(pool.get_statistics().high_watermark == 2, "object pool must keep high watermark");

	pool.destroy(first);
	pool.destroy(second);
	require(counted_value::live == 0, "object pool test must destroy every live object");
}

void test_spsc_queue()
{
	constexpr std::size_t count = 20000;
	dixelu::buffered_queue_spsc<std::size_t, 31, 3> queue;
	std::atomic_bool producer_done = false;

	std::thread producer([&] {
		for (std::size_t i = 0; i < count; ++i)
			queue.emplace(i);
		producer_done.store(true, std::memory_order_release);
	});

	std::size_t expected = 0;
	while (!producer_done.load(std::memory_order_acquire) || !queue.empty())
	{
		if (queue.empty())
		{
			std::this_thread::yield();
			continue;
		}
		require(queue.front() == expected, "SPSC queue must preserve FIFO order");
		queue.pop();
		++expected;
	}

	producer.join();
	require(expected == count, "SPSC queue must deliver every produced value");
	require(queue.approximate_size() == 0, "drained SPSC queue must report zero size");

	queue.push(41);
	queue.emplace(42ULL);
	require(queue.back() == 42, "SPSC queue back must return last produced value");
	queue.clear();
	require(queue.empty(), "SPSC queue clear must remove all elements");
}

void test_background_worker()
{
	using dixelu::background_worker_shutdown;
	using dixelu::background_worker_state;
	using dixelu::background_worker_submit_result;

	std::vector<int> order;
	dixelu::background_worker draining;
	require(
		draining.push(dixelu::background_worker::task_type{}) ==
			background_worker_submit_result::empty_task,
		"background worker must report an empty task without accepting it");
	for (int value = 0; value < 32; ++value)
	{
		auto owned = std::make_unique<int>(value);
		require(
			draining.push([owned = std::move(owned), &order] { order.push_back(*owned); }) ==
				background_worker_submit_result::accepted,
			"background worker must accept move-only tasks while running");
	}
	draining.shutdown(background_worker_shutdown::drain);
	draining.shutdown(background_worker_shutdown::drain);
	for (int value = 0; value < 32; ++value)
		require(order[static_cast<std::size_t>(value)] == value, "drain shutdown must preserve FIFO order");
	const auto drained = draining.snapshot();
	require(
		drained.state == background_worker_state::stopped && drained.submitted == 32 &&
			drained.completed == 32 && drained.failed == 0 && drained.cancelled == 0,
		"drain shutdown must complete every accepted task and remain stopped when repeated");
	require(
		draining.push([] {}) == background_worker_submit_result::stopped,
		"push after shutdown must report rejection");

	std::atomic_uint exception_reports = 0;
	std::atomic_uint continued_tasks = 0;
	dixelu::background_worker exception_worker(
		background_worker_shutdown::drain,
		[&](std::exception_ptr exception) {
			try
			{
				std::rethrow_exception(exception);
			}
			catch (const std::runtime_error&)
			{
				++exception_reports;
			}
			throw std::logic_error("handler failures must also be contained");
		});
	require(
		exception_worker.push([] { throw std::runtime_error("task failure"); }) ==
			background_worker_submit_result::accepted,
		"throwing task must still be accepted");
	exception_worker.push([&] { ++continued_tasks; });
	exception_worker.shutdown(background_worker_shutdown::drain);
	const auto exception_snapshot = exception_worker.snapshot();
	require(
		exception_reports == 1 && continued_tasks == 1 && exception_snapshot.failed == 1 &&
			exception_snapshot.completed == 1 && exception_worker.last_exception(),
		"task and handler exceptions must be reported without terminating the worker");

	std::promise<void> active_started;
	auto active_started_future = active_started.get_future();
	std::atomic_uint cancelled_task_runs = 0;
	std::atomic_uint stop_callback_runs = 0;
	dixelu::background_worker cancelling(background_worker_shutdown::cancel);
	cancelling.push([&](std::stop_token stop_token) {
		std::stop_callback callback(stop_token, [&] {
			++stop_callback_runs;
			(void)cancelling.snapshot();
		});
		active_started.set_value();
		while (!stop_token.stop_requested())
			std::this_thread::yield();
	});
	cancelling.push([&] { ++cancelled_task_runs; });
	cancelling.push([&] { ++cancelled_task_runs; });
	active_started_future.wait();
	cancelling.shutdown(background_worker_shutdown::cancel);
	const auto cancelled = cancelling.snapshot();
	require(
		cancelled_task_runs == 0 && stop_callback_runs == 1 && cancelled.started == 1 && cancelled.completed == 1 &&
			cancelled.cancelled == 2 && cancelled.state == background_worker_state::stopped,
		"cancel shutdown must stop cooperatively and count discarded pending tasks");

	std::promise<void> self_started;
	std::promise<void> allow_self_shutdown;
	std::promise<void> self_finished;
	auto self_started_future = self_started.get_future();
	auto allow_self_shutdown_future = allow_self_shutdown.get_future();
	auto self_finished_future = self_finished.get_future();
	std::atomic_uint self_cancelled_task_runs = 0;
	dixelu::background_worker self_stopping(background_worker_shutdown::cancel);
	self_stopping.push([&] {
		self_started.set_value();
		allow_self_shutdown_future.wait();
		self_stopping.shutdown(background_worker_shutdown::cancel);
		self_finished.set_value();
	});
	self_started_future.wait();
	require(
		self_stopping.push([&] { ++self_cancelled_task_runs; }) ==
			background_worker_submit_result::accepted,
		"worker must accept pending work before self-shutdown");
	allow_self_shutdown.set_value();
	self_finished_future.wait();
	self_stopping.shutdown(background_worker_shutdown::cancel);
	require(
		self_cancelled_task_runs == 0 && self_stopping.snapshot().cancelled == 1,
		"shutdown from a worker task must not deadlock and must cancel pending work");

	std::atomic_uint destructor_drained = 0;
	{
		dixelu::background_worker destructor_worker;
		destructor_worker.push([&] { ++destructor_drained; });
	}
	require(destructor_drained == 1, "default destruction must drain accepted work");
}

void test_buffered_file_reader()
{
#ifdef _WIN32
	static_assert(std::is_same_v<std::filesystem::path::value_type, wchar_t>);
#endif
	require(
		throws_invalid_argument([] { dixelu::buffered_file_reader invalid(0); }),
		"buffered reader must reject a zero-sized buffer");

	const auto exact_path = test_unicode_file_path(u8"_\u0444\u0430\u0439\u043b.bin");
	const auto empty_path = test_file_path("_empty.bin");
	const auto missing_path = test_file_path("_missing.bin");
	const std::array<std::byte, 8> bytes{
		std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
		std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}};
	write_test_file(exact_path, bytes);
	write_test_file(empty_path, {});
	auto cleanup = dixelu::make_scope_exit([&] {
		std::error_code ignored;
		std::filesystem::remove(exact_path, ignored);
		std::filesystem::remove(empty_path, ignored);
	});

	dixelu::buffered_file_reader reader(exact_path, 4);
	require(reader.is_open() && reader.size() == bytes.size(), "buffered reader must open a binary file");
	for (unsigned int expected = 0; expected < bytes.size(); ++expected)
	{
		const auto value = reader.get();
		require(
			value && std::to_integer<unsigned int>(*value) == expected,
			"buffered reader must preserve every byte, including zero");
	}
	require(reader.eof(), "an exact chunk-boundary read must reach EOF after the last byte");
	require(!reader.get(), "buffered reader must represent EOF separately from byte zero");

	require(reader.seek(3), "buffered reader must seek within the file");
	const auto sought_value = reader.get();
	require(
		sought_value && std::to_integer<unsigned int>(*sought_value) == 3,
		"buffered seek must set the next byte");
	require(!reader.seek(bytes.size() + 1), "buffered reader must reject seeks beyond EOF");
	require(reader.position() == 4, "a rejected buffered seek must preserve position");
	require(reader.seek(1), "buffered reader must seek before a bulk read");
	std::array<std::byte, 3> bulk{};
	require(reader.read(bulk) == bulk.size(), "buffered reader must fill a bulk destination");
	require(
		bulk[0] == std::byte{1} && bulk[2] == std::byte{3},
		"buffered bulk read must preserve byte order");
	require(reader.seek(4), "buffered reader must seek to a chunk boundary");
	std::ostringstream output;
	require(reader.copy_to(output) == 4, "buffered copy must report consumed bytes");
	require(output.str() == std::string("\x04\x05\x06\x07", 4), "buffered copy must preserve remaining data");
	require(reader.is_open() && reader.eof(), "buffered copy must not close its owned file");

	require(reader.reopen(empty_path), "buffered reader must reopen an empty file");
	require(reader.is_open() && reader.size() == 0 && reader.eof(), "empty file must be a valid open EOF state");
	require(!reader.reopen(missing_path), "buffered reader must report failed reopen");
	require(!reader.is_open() && reader.last_error(), "failed buffered reopen must leave a closed error state");
	require(reader.open(exact_path), "buffered reader must recover after a failed open");

	dixelu::buffered_file_reader moved(std::move(reader));
	require(!reader.is_open() && moved.is_open(), "buffered reader move must transfer file ownership");
	moved.close();
	moved.close();
	require(!moved.is_open(), "buffered reader close must be idempotent");
}

void test_memory_mapped_file_reader()
{
	const auto exact_path = test_unicode_file_path(u8"_\u043a\u0430\u0440\u0442\u0430.bin");
	const auto empty_path = test_file_path("_mapped_empty.bin");
	const auto missing_path = test_file_path("_mapped_missing.bin");
	const std::array<std::byte, 8> bytes{
		std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
		std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}};
	write_test_file(exact_path, bytes);
	write_test_file(empty_path, {});
	auto cleanup = dixelu::make_scope_exit([&] {
		std::error_code ignored;
		std::filesystem::remove(exact_path, ignored);
		std::filesystem::remove(empty_path, ignored);
	});

	dixelu::memory_mapped_file_reader reader(exact_path);
	require(reader.is_open() && reader.size() == bytes.size(), "mapped reader must open a binary file");
	require(reader.bytes().size() == bytes.size(), "mapped reader must expose the complete byte view");
	for (unsigned int expected = 0; expected < bytes.size(); ++expected)
	{
		const auto value = reader.get();
		require(
			value && std::to_integer<unsigned int>(*value) == expected,
			"mapped reader must preserve every byte, including zero");
	}
	require(reader.eof() && !reader.get(), "mapped reader must report EOF after the last byte");

	require(reader.seek(3), "mapped reader must seek within the file");
	const auto sought_value = reader.get();
	require(
		sought_value && std::to_integer<unsigned int>(*sought_value) == 3,
		"mapped seek must set the next byte");
	require(!reader.seek(bytes.size() + 1), "mapped reader must reject seeks beyond EOF");
	require(reader.position() == 4, "a rejected mapped seek must preserve position");
	require(reader.seek(1), "mapped reader must seek before a bulk read");
	std::array<std::byte, 3> bulk{};
	require(reader.read(bulk) == bulk.size(), "mapped reader must fill a bulk destination");
	require(
		bulk[0] == std::byte{1} && bulk[2] == std::byte{3},
		"mapped bulk read must preserve byte order");

	require(reader.reopen(empty_path), "mapped reader must reopen an empty file");
	require(
		reader.is_open() && reader.data() == nullptr && reader.bytes().empty() && reader.eof(),
		"empty mapping must be a valid open EOF state without a fake pointer");
	require(!reader.reopen(missing_path), "mapped reader must report failed reopen");
	require(!reader.is_open() && reader.last_error(), "failed mapped reopen must leave a closed error state");
	require(reader.open(exact_path), "mapped reader must recover after a failed open");

	dixelu::memory_mapped_file_reader moved(std::move(reader));
	require(!reader.is_open() && moved.is_open(), "mapped reader move must transfer native ownership");
	moved.close();
	moved.close();
	require(!moved.is_open(), "mapped reader close must be idempotent");
}

void test_native_literal()
{
	constexpr auto literal = dixelu::to_native_literal("utility");
	static_assert(literal.view().size() == 7);
	static_assert(literal.view()[0] == static_cast<dixelu::native_char>('u'));
	require(literal.str().size() == 7, "native literal must preserve its payload");
}

void test_base64_and_url()
{
	const auto man = dixelu::base64_decode("TWFu");
	require(
		std::string(man.begin(), man.end()) == "Man",
		"Base64 decoder must decode complete quartets");
	const auto ma = dixelu::base64_decode("T W E=\r\n");
	require(std::string(ma.begin(), ma.end()) == "Ma", "Base64 decoder must handle whitespace/padding");
	const auto m = dixelu::base64_decode("TQ==");
	require(std::string(m.begin(), m.end()) == "M", "Base64 decoder must handle double padding");
	require(
		throws_invalid_argument([] { (void)dixelu::base64_decode("TQ="); }),
		"Base64 decoder must reject truncated input");
	require(
		throws_invalid_argument([] { (void)dixelu::base64_decode("TR=="); }),
		"Base64 decoder must reject non-canonical padding bits");

	const auto secure = dixelu::parse_url("WSS://example.test?token=a#ignored");
	require(
		secure.scheme == "wss" && secure.host == "example.test" && secure.port == "443" &&
			secure.target == "/?token=a" && secure.tls,
		"URL parser must normalize schemes and query-only targets");
	const auto ipv6 = dixelu::parse_url("http://[2001:db8::1]:8080/path");
	require(
		ipv6.host == "2001:db8::1" && ipv6.port == "8080" && ipv6.target == "/path" && !ipv6.tls,
		"URL parser must handle bracketed IPv6 authorities");
	require(dixelu::url_encode("a b+c") == "a%20b%2Bc", "URL encoding must preserve only unreserved bytes");
	require(
		throws_invalid_argument([] { (void)dixelu::parse_url("https://2001:db8::1/"); }),
		"URL parser must reject unbracketed IPv6 authorities");
}

void test_ip_routing()
{
	using namespace dixelu::udp::routing;
	const auto ipv4 = parse_ipv4("192.168.1.7");
	require(ipv4 && (*ipv4)[0] == 192 && (*ipv4)[3] == 7, "IPv4 parser must decode octets");
	require(!parse_ipv4("256.1.1.1") && !parse_ipv4("1.2.3"), "IPv4 parser must reject invalid input");

	std::array<std::uint8_t, 20> packet4{};
	packet4[0] = 0x45;
	packet4[2] = 0;
	packet4[3] = 20;
	packet4[12] = 10;
	packet4[15] = 1;
	packet4[16] = 224;
	packet4[19] = 1;
	const auto info4 = inspect_ip_packet(packet4);
	require(info4 && has_usable_source(*info4), "IPv4 inspection must accept a valid source");
	require(has_group_destination(*info4), "IPv4 inspection must identify multicast destinations");

	std::array<std::uint8_t, 40> packet6{};
	packet6[0] = 0x60;
	packet6[23] = 1;
	packet6[24] = 0xFF;
	packet6[25] = 0x02;
	packet6[39] = 1;
	const auto info6 = inspect_ip_packet(packet6);
	require(info6 && has_usable_source(*info6), "IPv6 inspection must accept a valid source");
	require(has_group_destination(*info6), "IPv6 inspection must identify multicast destinations");

	const auto broadcast = subnet_broadcast_key("192.168.1.7", "255.255.255.0");
	const std::array<std::uint8_t, 4> expected_broadcast{192, 168, 1, 255};
	require(
		broadcast == address_key(4, expected_broadcast.data(), expected_broadcast.size()),
		"subnet broadcast calculation must apply the mask");

	route_bindings bindings;
	const auto address4 = source_key(*info4);
	require(bindings.learn(address4, "peer-a") == learn_result::learned, "route binding must learn a peer");
	require(bindings.learn(address4, "peer-a") == learn_result::unchanged, "same route binding must be idempotent");
	require(bindings.learn(address4, "peer-b") == learn_result::address_conflict, "route binding must detect address conflicts");
	require(bindings.peer_for(address4) == "peer-a", "route binding must resolve learned peers");
	bindings.erase_peer("peer-a");
	require(!bindings.peer_for(address4), "route binding erase must remove reverse mappings");
}

} // namespace

int main()
{
	test_numeric_traits();
	test_mctx_regressions();
	test_function_ref();
	test_fixed_string();
	test_scope_exit();
	test_legacy_scope_and_future_regressions();
	test_object_pool();
	test_spsc_queue();
	test_background_worker();
	test_buffered_file_reader();
	test_memory_mapped_file_reader();
	test_native_literal();
	test_base64_and_url();
	test_ip_routing();
}
