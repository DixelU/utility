#pragma once

#ifndef SAF_SMRP2
#define SAF_SMRP2

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <syncstream>
#include <unordered_set>
#include <vector>

#ifdef _MSC_VER
#define FORCEDINLINE __forceinline
#else
#define FORCEDINLINE __attribute__((always_inline))
#endif

struct logger_base
{
	virtual ~logger_base() = default;
	virtual void operator<<(std::string&& message) {};
	virtual std::string getLast() const { return ""; };

	virtual void report(std::vector<std::ptrdiff_t> data, std::string&& message)
	{
		std::string accumulator = "{";

		for (size_t i = 0; i < data.size(); ++i)
		{
			accumulator += std::to_string(data[i]);
			if (i + 1 < data.size())
				accumulator += ", ";
		}
		accumulator += "}";

		operator<<(std::move(accumulator) + ": " + std::move(message));
	};
};

struct logger :
	public logger_base
{
protected:
	std::vector<std::string> messages;
	mutable std::mutex mtx;
public:
	~logger() override {}
	void operator<<(std::string&& message) override
	{
		std::lock_guard locker(mtx);
		messages.push_back(std::move(message));
	}
	std::string getLast() const override
	{
		std::lock_guard locker(mtx);
		return (messages.size()) ? messages.back() : "";
	}
	std::vector<std::string> getAll() const
	{
		std::lock_guard locker(mtx);
		return messages;
	}
}; 

struct singleline_logger :
	public logger
{
protected:
	std::string message;
	mutable std::mutex mtx;
public:
	~singleline_logger() override {}
	void operator<<(std::string&& message) override
	{
		std::lock_guard locker(mtx);
		this->message = std::move(message);
	}
	std::string getLast() const override
	{
		std::lock_guard locker(mtx);
		return message;
	}
};

struct printing_logger :
	public singleline_logger
{
public:
	std::string color;
	printing_logger( std::string color = "37" ) : color(std::move(color)) {}
	~printing_logger() override {}
	void operator<<(std::string&& message) override
	{
		std::lock_guard locker(mtx);
		this->message = std::move(message);
		std::osyncstream(std::cout) << "\x1b[0;" + color + "m " << this->message << "\x1b[0m" << std::endl;
	}
};

struct nonrepeating_logger :
	public printing_logger
{
public:
	~nonrepeating_logger() override {}
	nonrepeating_logger(std::string color = "37")
		:printing_logger(color) {}
	void operator<<(std::string&& message) override
	{
		if (message == "flush")
			return used_messages.clear();

		auto pos = message.find_last_of('}');
		if (pos == std::string::npos)
			pos = 0;

		auto [it, success] = used_messages.insert(message.substr(pos));
		if (success)
			printing_logger::operator<<(std::move(message));
	}
private:
	std::unordered_set<std::string> used_messages;
};

// Prints to console AND collects all messages for getAll()
struct collecting_printing_logger :
	public logger
{
public:
	std::string color;
	collecting_printing_logger(std::string color = "37") : color(std::move(color)) {}
	~collecting_printing_logger() override {}
	void operator<<(std::string&& message) override
	{
		std::lock_guard locker(mtx);
		messages.push_back(message);
		std::osyncstream(std::cout) << "\x1b[0;" + color + "m " << message << "\x1b[0m" << std::endl;
	}
	std::string getLast() const override
	{
		std::lock_guard locker(mtx);
		return messages.empty() ? "" : messages.back();
	}
};

// Deduplicates, prints, AND collects all messages for getAll()
struct nonrepeating_collecting_logger :
	public collecting_printing_logger
{
public:
	nonrepeating_collecting_logger(std::string color = "37") : collecting_printing_logger(color) {}
	~nonrepeating_collecting_logger() override {}
	void operator<<(std::string&& message) override
	{
		if (message == "flush")
			return used_messages.clear();

		auto pos = message.find_last_of('}');
		if (pos == std::string::npos)
			pos = 0;

		auto [it, success] = used_messages.insert(message.substr(pos));
		if (success)
			collecting_printing_logger::operator<<(std::move(message));
	}
private:
	std::unordered_set<std::string> used_messages;
};

struct single_midi_processor_2
{
	inline static constexpr std::uint32_t MTrk_header = 1297379947;
	inline static constexpr std::uint32_t MThd_header = 1297377380;
	inline static constexpr std::uint64_t disable_tick = (~0ull);

	using tick_type = std::uint64_t;
	using base_type = std::uint8_t;
	using metasize_type = std::uint32_t;
	using sgsize_type = std::int32_t;
	using sgtick_type = std::int64_t;
	using ppq_type = std::uint16_t;

	inline static constexpr std::size_t tick_position = 0;
	inline static constexpr std::size_t event_type = 8;
	inline static constexpr std::size_t event_param1 = 9;
	inline static constexpr std::size_t event_param2 = 10;
	inline static constexpr std::size_t event_param3 = 11;
	inline static constexpr std::size_t event_param4 = 12;
	inline static constexpr std::size_t event_param5 = 13;

	// meta:   8 tick  1 type  1 metatype  1 vlv size  4 size  ...<raw meta>~vlv+data
	inline static constexpr std::size_t event_meta_raw = 8 + 1 + 1 + 1 + 4;
	inline constexpr static std::size_t get_meta_param_index(std::size_t vlv_size, base_type parameter)
	{
		return event_meta_raw + vlv_size + parameter;
	}

	using data_iterator = std::vector<base_type>::iterator;

	struct message_buffers
	{
		using logger_t = logger;

		std::shared_ptr<logger_t> trace;
		std::shared_ptr<logger_t> log;
		std::shared_ptr<logger_t> warning;
		std::shared_ptr<logger_t> borderline_error;
		std::shared_ptr<logger_t> error;

		std::atomic_uint64_t last_input_position;
		std::atomic_bool processing;
		std::atomic_bool finished;

		bool is_console_oriented;

		message_buffers(bool is_console_oriented = false):
			trace(is_console_oriented ?
				std::shared_ptr<logger_t>(std::make_shared<nonrepeating_collecting_logger>("37")) :
				std::shared_ptr<logger_t>(std::make_shared<singleline_logger>())),
			log(is_console_oriented ?
				std::shared_ptr<logger_t>(std::make_shared<collecting_printing_logger>("32")) :
				std::shared_ptr<logger_t>(std::make_shared<singleline_logger>())),
			warning(is_console_oriented ?
				std::shared_ptr<logger_t>(std::make_shared<collecting_printing_logger>("33")) :
				std::shared_ptr<logger_t>(std::make_shared<singleline_logger>())),
			borderline_error(is_console_oriented ?
				std::shared_ptr<logger_t>(std::make_shared<collecting_printing_logger>("95")) :
				std::shared_ptr<logger_t>(std::make_shared<singleline_logger>())),
			error(is_console_oriented ?
				std::shared_ptr<logger_t>(std::make_shared<collecting_printing_logger>("31")) :
				std::shared_ptr<logger_t>(std::make_shared<singleline_logger>())),
			last_input_position(0),
			processing(false),
			finished(false),
			is_console_oriented(is_console_oriented)
		{
		}
	};

	struct processing_data
	{
		std::wstring filename;
		
		std::atomic_uint64_t tracks_count;
	};

	FORCEDINLINE static void ostream_write(
		std::vector<base_type>& vec,
		const std::vector<base_type>::iterator& beg, 
		const std::vector<base_type>::iterator& end, 
		std::ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}

	FORCEDINLINE static void ostream_write(std::vector<base_type>& vec, std::ostream& out)
	{
		out.write(((char*)vec.data()), vec.size());;
	}

	FORCEDINLINE static uint8_t push_vlv(uint32_t value, std::vector<base_type>& vec)
	{
		return push_vlv_s(value, vec);
	};

	FORCEDINLINE static uint8_t push_vlv_s(tick_type s_value, std::vector<base_type>& vec)
	{
		constexpr uint8_t $7byte_mask = 0x7F, max_size = 11, $7byte_mask_size = 7;
		constexpr uint8_t $adjacent7byte_mask = ~$7byte_mask;
		uint64_t value = s_value;
		uint8_t __stack[max_size]{};
		uint8_t* const stack_end = __stack + max_size;
		uint8_t* stack = stack_end;
		do
		{
			stack--;
			*stack = (value & $7byte_mask);
			value >>= $7byte_mask_size;
			if (stack_end - stack != 1)
				*stack |= $adjacent7byte_mask;
		} while (value);

		auto stack_size = size_t(stack_end - stack);

		copy_back_traits::copy_back(
			vec,
			copy_back_traits::raw_storage{ stack, stack_size }
		);

		return stack_size;
	}

	FORCEDINLINE static uint64_t get_vlv(bbb_ffr& file_input)
	{
		uint64_t value = 0;
		base_type single_byte;
		do
		{
			single_byte = file_input.get();
			value = value << 7 | single_byte & 0x7F;
		} while (single_byte & 0x80 && !file_input.eof());
		return value;
	}

	template<typename V>
	FORCEDINLINE static void __hack_resize(V& v, size_t newSize)
	{
		//hack beyond hacks
		struct vt { typename V::value_type v; vt() {} };
		static_assert(sizeof(vt[10]) == sizeof(typename V::value_type[10]), "alignment error");
		typedef std::vector<vt, typename std::allocator_traits<typename V::allocator_type>::template rebind_alloc<vt>> V2;
		reinterpret_cast<V2&>(v).resize(newSize);
	}

#define SAFC_S2B_PUSH_BACK
#define SAFC_S2B_HACK_RESIZE
	template<typename T>
	FORCEDINLINE static size_t push_back(std::vector<base_type>& vec, const T& value)
	{
		constexpr auto type_size = sizeof(T);
		auto current_index = vec.size();
#ifdef SAFC_S2B_PUSH_BACK
#ifdef SAFC_S2B_HACK_RESIZE
		__hack_resize(vec, current_index + type_size);
#else
		vec.resize(current_index + type_size);
#endif
		auto& value_ref = *(T*)(&vec[current_index]);
		value_ref = value;
#else
		auto begin = (const base_type*)&value;
		vec.insert(vec.end(), begin, begin + type_size);
#endif
		return type_size;
	}

	struct copy_back_traits
	{
		struct raw_storage
		{
			void* ptr;
			size_t size;
		};

		template<typename T>
		static void __copy_T_to_array(
			base_type* storage, 
			const typename std::enable_if<std::is_same<T, raw_storage>::value, T>::type& value)
		{
			std::memcpy(storage, value.ptr, value.size);
		}

		template<typename T>
		static void __copy_T_to_array(
			base_type* storage, 
			const typename std::enable_if<(!std::is_same<T, raw_storage>::value), T>::type& value)
		{
			*(T*)(storage) = value;
		}

		template<typename T>
		constexpr static typename std::enable_if<(!std::is_same<T, raw_storage>::value), size_t>::type
			size_of_contained_data(const T& /*data*/)
		{
			return sizeof(T);
		}

		template<typename T>
		constexpr static typename std::enable_if<std::is_same<T, raw_storage>::value, size_t>::type
			size_of_contained_data(const T& data)
		{
			return data.size;
		}

		template<typename _head>
		static size_t __total_size(const _head& head)
		{
			return size_of_contained_data(head);
		}

		template<typename _head, typename... _tail>
		static size_t __total_size(const _head& head, const _tail&... tail)
		{
			return
				size_of_contained_data(head) +
				__total_size(tail...);
		}

		template<typename _head, typename... _tail>
		FORCEDINLINE void static __copy_to_array(base_type* storage, _head head_value, _tail... tail_values)
		{
			__copy_T_to_array<_head>(storage, head_value);

			__copy_to_array(storage + sizeof(_head), tail_values...);
		}

		template<typename _head>
		FORCEDINLINE void static __copy_to_array(base_type* storage, _head head_value)
		{
			__copy_T_to_array<_head>(storage, head_value);
		}

		template<typename... _variadic_types>
		FORCEDINLINE void static copy_back(
			std::vector<base_type>& vec,
			_variadic_types... types)
		{
			auto total_size = __total_size(types...);
			auto current_index = vec.size();
			__hack_resize(vec, current_index + total_size);
			base_type* new_storage = &vec[current_index];
			__copy_to_array(new_storage, types...);
			//std::cout << "##SCE##" << std::endl;
		}
	};

	FORCEDINLINE static void copy_back(
		std::vector<base_type>& vec,
		const std::vector<base_type>::iterator& begin,
		const std::vector<base_type>::iterator& end, 
		const size_t expected_size)
	{
#ifdef SAFC_S2B_HACK_RESIZE
		//damn slow memcpy
		switch (expected_size)
		{
			case 1: { copy_back_traits::copy_back(vec, get_value<uint8_t>(begin, 0)); break; }
			case 2: { copy_back_traits::copy_back(vec, get_value<uint16_t>(begin, 0)); break; }
			case 3: { copy_back_traits::copy_back(vec, get_value<uint16_t>(begin, 0), get_value<uint8_t>(begin, 2)); break; }
			case 4: { copy_back_traits::copy_back(vec, get_value<uint32_t>(begin, 0)); break; }
			case 5: { copy_back_traits::copy_back(vec, get_value<uint32_t>(begin, 0), get_value<uint8_t>(begin, 4)); break; }
			case 6: { copy_back_traits::copy_back(vec, get_value<uint32_t>(begin, 0), get_value<uint16_t>(begin, 2)); break; }
			case 7: { copy_back_traits::copy_back(vec, get_value<uint32_t>(begin, 0), get_value<uint16_t>(begin, 2), get_value<uint8_t>(begin, 6)); break; }
			case 8: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0)); break; }

			//case 9: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint8_t>(begin, 0 + 8)); break; }
			//case 10: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint16_t>(begin, 0 + 8)); break; }
			//case 11: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint16_t>(begin, 0 + 8), get_value<uint8_t>(begin, 2 + 8)); break; }
			//case 12: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint32_t>(begin, 0 + 8)); break; }
			//case 13: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint32_t>(begin, 0 + 8), get_value<uint8_t>(begin, 4 + 8)); break; }
			//case 14: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint32_t>(begin, 0 + 8), get_value<uint16_t>(begin, 2 + 8)); break; }
			//case 15: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint32_t>(begin, 0 + 8), get_value<uint16_t>(begin, 2 + 8), get_value<uint8_t>(begin, 6 + 8)); break; }
			//case 16: { copy_back_traits::copy_back(vec, get_value<uint64_t>(begin, 0), get_value<uint64_t>(begin, 0 + 8)); break; }
			default:
			{
				//vec.insert(vec.end(), begin, end);
				std::size_t size = end - begin;
				copy_back_traits::copy_back(vec, copy_back_traits::raw_storage{ &*begin, size });
				break;
			}
		}
#else
		vec.insert(vec.end(), begin, end);
#endif
	}

	template<typename T>
	FORCEDINLINE static T& get_value(std::vector<base_type>& vec, size_t index)
	{
		return *(T*)&vec[index];
	}

	template<typename T>
	FORCEDINLINE static T& get_value(const std::vector<base_type>::iterator& vec, size_t index)
	{
		return *(T*)&vec[index];
	}

	template<typename T>
	FORCEDINLINE static bool is_valid_index(std::vector<base_type>& vec, size_t index)
	{
		return vec.size() <= index + sizeof(T);
	}

	struct data_buffers
	{
		std::vector<base_type> meta_buffer;
	};

	inline static bool perform_single_run_analysis(
		bbb_ffr& file_input,
		data_buffers& data_buffers,
		message_buffers& buffers,
		std::vector<std::vector<tick_type>>& current_polyphony
	)
	{
		auto& meta_buffer = data_buffers.meta_buffer;

		tick_type current_tick = 0;

		std::uint32_t header = 0;

		std::uint32_t rsb = 0;

		bool previous_event_was_meta = false;
		bool had_possible_untouched_rsb_optimisation_site = false;
		bool had_encountered_unexpected_rsb_region = false;

		std::uint32_t track_expected_size = 0;
		size_t noteoff_misses = 0;

		bool is_good = true;
		bool is_going = true;

		while (header != MTrk_header && file_input.good()) //MTrk = 1297379947
			header = (header << 8) | file_input.get();

		for (auto& single_polyphony : current_polyphony)
			single_polyphony.clear();

		for (int i = 0; i < 4 && file_input.good(); i++)
			track_expected_size = (track_expected_size << 8) | file_input.get();

		auto begin_of_track_data = file_input.tellg();

		if (file_input.eof())
		{
			buffers.log->report({ begin_of_track_data }, "End of file");
			return false;
		}

		while (file_input.good() && is_good && is_going)
		{
			constexpr uint32_t deltatime_standard_limit = (1 << (7 * 4)) - 1;

			base_type command = 0;
			base_type param_buffer = 0;

			auto deltatime_position = file_input.tellg();

			auto delta_time = get_vlv(file_input);
			current_tick += delta_time;

			if (delta_time >= 0x7FFFFFFF) [[unlikely]]
				buffers.trace->report({ deltatime_position, int64_t(delta_time), 0x7FFFFFFF }, "Delta time too big");
			else if (delta_time >= deltatime_standard_limit) [[unlikely]]
				buffers.trace->report({ deltatime_position, int64_t(delta_time), deltatime_standard_limit }, "Delta time too big");

			command = file_input.get();
			if (command < 0x80) [[unlikely]]
			{
				if (had_possible_untouched_rsb_optimisation_site && !had_encountered_unexpected_rsb_region)
				{
					buffers.warning->report({ file_input.tellg() - 1, command , rsb }, "Sudden RSB region");
					had_encountered_unexpected_rsb_region = true;
				}
				else
					buffers.trace->report({ file_input.tellg() - 1, command , rsb }, "RSB optimisation detected");

				param_buffer = command;
				command = rsb;
			}
			else
			{
				if (command < 0xF0)
				{
					had_possible_untouched_rsb_optimisation_site |=
						((rsb & 0xF0) == (command & 0xF0));
					rsb = command;
				}

				if (command < 0xF0 || command == 0xFF)
					param_buffer = file_input.get();
				else
					param_buffer = 0xFF;
			}

			if (command < 0x80)
			{
				is_good = false;
				buffers.error->report({ file_input.tellg(), command }, "Unknown event header or mishandled RSB");
				break;
			}

			auto current_file_position = file_input.tellg();

			switch (command >> 4)
			{
			case 0x8: case 0x9: {
				base_type com = command;
				base_type key = param_buffer;
				base_type vel = file_input.get();
				tick_type reference = disable_tick;
				previous_event_was_meta = false;

				if (key & 0x80)
					buffers.warning->report({ file_input.tellg() - 2, key }, "Key overflow");
				if (vel & 0x80)
					buffers.warning->report({ file_input.tellg() - 1, vel }, "Vel overflow");

				auto comman_xor_modifier = ((!bool(vel) & bool(com & 0x10)) << 4);
				com ^= comman_xor_modifier;

				std::uint16_t key_polyindex = (com & 0xF) | (((std::uint16_t)key) << 4);
				bool polyphony_error = false;
				auto& current_polyphony_object = current_polyphony[key_polyindex];
				if (com & 0x10)
					current_polyphony_object.push_back(current_file_position); // hot smh
				else if (current_polyphony_object.size())
				{
					reference = current_polyphony_object.back();
					current_polyphony_object.pop_back(); // hot smh
					auto other_note_reference = reference + event_param3;
				}
				else
				{
					polyphony_error = true;
					++noteoff_misses;

					buffers.trace->report({ file_input.tellg() - 2 }, "OFF of nonON Note");
				}

				break;
			}
			case 0xA: case 0xB: case 0xE: {
				base_type com = command;
				base_type p1 = param_buffer;
				base_type p2 = file_input.get();
				previous_event_was_meta = false;

				if (p1 & 0x80)
					buffers.warning->report({ file_input.tellg() - 2, com, p1 }, "Double key event p1 overflow");
				if (p2 & 0x80)
					buffers.warning->report({ file_input.tellg() - 1, com, p2 }, "Double key event p2 overflow");

				break;
			}
			case 0xC: case 0xD: {
				base_type com = command;
				base_type p1 = param_buffer;
				previous_event_was_meta = false;

				if (p1 & 0x80)
					buffers.warning->report({ file_input.tellg() - 1, com, p1 }, "Single key event p overflow");

				break;
			}
			case 0xF: {
				base_type com = command;
				base_type type = param_buffer;

				is_going &= !(type == 0x2F && com == 0xFF);
				previous_event_was_meta = true;

				if (!is_going)
				{
					// ignore the end of track event;
					for (auto it = current_polyphony.begin(); it != current_polyphony.end(); ++it)
					{
						if (it->size())
							buffers.trace->report({ file_input.tellg() - 2, (it - current_polyphony.begin()), static_cast<int64_t>(it->size()) }, "Nonempty polyphony at the end of track");
					}

					continue;
				}

				//if(com == 0xFF) // damn sysex broke it all >:C
					//push_back<base_type>(data_buffer, type);

				// 8 tick  1 type  1 metatype (except when sysex)  1 vlv size  4 size  ...<raw meta>~vlv+data

				metasize_type length = get_vlv(file_input);
				auto encoded_length = push_vlv_s(length, meta_buffer);

				for (std::size_t i = 0; i < length; ++i)
					meta_buffer.push_back(file_input.get());
				length += encoded_length;

				if (com == 0xFF) [[likely]]
				{
					/*copy_back_traits::copy_back(
						data_buffer,
						current_tick,
						com,
						type,
						encoded_length,
						length,
						copy_back_traits::raw_storage{
							meta_buffer.data(), meta_buffer.size() });*/
				}
				else
				{
					/*copy_back_traits::copy_back(
						data_buffer,
						current_tick,
						com,
						encoded_length,
						length,
						copy_back_traits::raw_storage{
							meta_buffer.data(), meta_buffer.size() });*/
				}

				meta_buffer.clear();

				break;
			}
			default: {
				buffers.error->report({ file_input.tellg(), command }, "Unknown event type");
				break;
			}
			}
		}

		auto end_of_track_data = file_input.tellg();

		for (auto& single_polyphony : current_polyphony)
			if (single_polyphony.size())
			{
				buffers.warning->report({ file_input.tellg() }, "Nonzero polyphony at the end of track");
				break;
			}

		if (end_of_track_data - begin_of_track_data + 1 != track_expected_size)
			buffers.borderline_error->report({ file_input.tellg(), track_expected_size, end_of_track_data - begin_of_track_data + 1 },
				"Track size mismatch");

		if (noteoff_misses)
			buffers.warning->report({ file_input.tellg(), static_cast<std::ptrdiff_t>(noteoff_misses)}, "Encountered several instances of negative polyphony");

		return true;
	}

	template<bool ready_for_write = false>
	FORCEDINLINE constexpr static std::ptrdiff_t expected_size(base_type v)
	{
		switch (v >> 4)
		{
			//MH_CASE(0xA0) : MH_CASE(0xB0) : MH_CASE(0xE0) :
		case 0xA: case 0xB: case 0xE:
			return 8ll + 3ll;
			//MH_CASE(0x80) : MH_CASE(0x90) :
		case 0x8: case 0x9:
			return 8ll + 3ll + (8ll * !ready_for_write);
			//MH_CASE(0xC0) : MH_CASE(0xD0) :
		case 0xC: case 0xD:
			return 8ll + 2ll;
		default:
			return -1ll;
		}
	}

	inline static constexpr int gapped_size_legnth = 2 * (1) + 1;

	template<bool ready_for_write = false>
	FORCEDINLINE static std::enable_if<!ready_for_write, std::ptrdiff_t>::type expected_size(const data_iterator& cur)
	{
		const auto& type = cur[event_type];
		auto size = expected_size<ready_for_write>(type);
		if (size > 0)
			return size;

		// 8 tick  1 type  1 metatype (when not sysex)  _1 vlv size_  4 size  ...<raw meta>~vlv+data	
		bool is_sysex = type != 0xFF;
		const auto& meta_size = get_value<metasize_type>(cur, event_param3 - is_sysex);

		return std::ptrdiff_t(event_meta_raw) + std::ptrdiff_t(meta_size) - is_sysex;
	}

	template<bool ready_for_write = false>
	FORCEDINLINE static std::enable_if<ready_for_write, std::array<std::ptrdiff_t, gapped_size_legnth>>::type
		expected_size(const data_iterator& cur)
	{
		const auto& type = cur[event_type];
		auto size = expected_size<ready_for_write>(type);
		if (size > 0)
			return { size, 0, 0 };

		// 8 tick  1 type  1 metatype (when not sysex)  _1 vlv size_  4 size  ...<raw meta>~vlv+data	
		bool is_sysex = type != 0xFF;
		const auto& meta_size = get_value<metasize_type>(cur, event_param3 - is_sysex);

		return 
		{ 
			std::ptrdiff_t(event_param2 - is_sysex),
			std::ptrdiff_t(event_meta_raw - is_sysex),
			std::ptrdiff_t(event_meta_raw - is_sysex + meta_size)
		};
	}

	static void sync_processing(processing_data& data, message_buffers& loggers)
	{
		loggers.processing = true;

		std::vector<std::vector<tick_type>> polyphony_stacks(4096);
		data_buffers track_buffers;

		for (auto& el : polyphony_stacks)
			el.reserve(1000);

		track_buffers.meta_buffer.reserve(1ull << 10);

		bbb_ffr file_input(data.filename.c_str());

		// todo: verify midi header
		for (int i = 0; i < 10 && file_input.good(); i++)
			(file_input.get());

		uint16_t header_tracks_count;
		header_tracks_count = ((std::uint16_t)file_input.get()) << 8;
		header_tracks_count |= ((std::uint16_t)file_input.get());

		uint16_t ppq;
		ppq = ((std::uint16_t)file_input.get()) << 8;
		ppq |= ((std::uint16_t)file_input.get());

		if (ppq == 0)
			loggers.error->report({ file_input.tellg() - 2 }, "Zero PPQ");
		else if (ppq > 0x7FFF)
			loggers.warning->report({ file_input.tellg() - 2, ppq }, "PPQ greater than 32767 might not be supported by some players");

		std::ptrdiff_t track_counter = 0;

		while (file_input.good())
		{
			bool is_readable =
				perform_single_run_analysis(file_input, track_buffers, loggers, polyphony_stacks);

			if (!is_readable)
				break;

			track_counter += 1;

			loggers.last_input_position = file_input.tellg();

			loggers.log->report({ static_cast<ptrdiff_t>(loggers.last_input_position.load()), track_counter}, "Track processed");

			*loggers.trace << "flush";
		}

		file_input.close();

		data.tracks_count = track_counter;

		if (header_tracks_count != track_counter)
			loggers.borderline_error->report({ static_cast<ptrdiff_t>(loggers.last_input_position.load()), header_tracks_count, track_counter}, "Incorrect header tracks count");
			 
		loggers.processing = false;
		loggers.finished = true;
	}
};

#endif SAF_SMRP2
