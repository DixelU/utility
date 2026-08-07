#pragma once

#include <condition_variable>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

namespace dixelu
{

enum class background_worker_shutdown
{
	drain,
	cancel
};

enum class background_worker_submit_result
{
	accepted,
	empty_task,
	stopped
};

enum class background_worker_state
{
	running,
	stopping,
	stopped
};

struct background_worker_snapshot
{
	background_worker_state state = background_worker_state::running;
	std::uint64_t submitted = 0;
	std::uint64_t started = 0;
	std::uint64_t completed = 0;
	std::uint64_t failed = 0;
	std::uint64_t cancelled = 0;
	std::size_t pending = 0;
	bool task_running = false;
};

// A single-threaded FIFO executor with an explicit one-shot shutdown policy.
//
// drain:  reject new work, execute every accepted pending task, then stop.
// cancel: reject new work, discard pending tasks, and request cooperative stop
//         from the currently running task. An executing task is never killed.
//
// Destruction uses the policy selected at construction (drain by default) and
// waits for the worker. Destroying the object from one of its own tasks is not
// supported because the worker loop necessarily still refers to this object.
class background_worker
{
public:
	using task_type = std::move_only_function<void(std::stop_token)>;
	using exception_handler = std::function<void(std::exception_ptr)>;

	explicit background_worker(
		background_worker_shutdown destructor_policy = background_worker_shutdown::drain,
		exception_handler on_exception = {}) :
		destructor_policy_(destructor_policy),
		on_exception_(std::move(on_exception)),
		worker_([this](std::stop_token) { worker_loop(stop_source_.get_token()); })
	{}

	~background_worker()
	{
		shutdown(destructor_policy_);
	}

	background_worker(const background_worker&) = delete;
	background_worker& operator=(const background_worker&) = delete;
	background_worker(background_worker&&) = delete;
	background_worker& operator=(background_worker&&) = delete;

	background_worker_submit_result push(task_type task)
	{
		if (!task)
			return background_worker_submit_result::empty_task;

		{
			std::lock_guard lock(mutex_);
			if (shutdown_requested_)
				return background_worker_submit_result::stopped;

			queue_.push_back(std::move(task));
			++snapshot_.submitted;
			snapshot_.pending = queue_.size();
		}
		condition_.notify_one();
		return background_worker_submit_result::accepted;
	}

	template<typename Function>
		requires (
			!std::same_as<std::remove_cvref_t<Function>, task_type> &&
			(std::invocable<std::decay_t<Function>&, std::stop_token> ||
				std::invocable<std::decay_t<Function>&>))
	background_worker_submit_result push(Function&& function)
	{
		using stored_type = std::decay_t<Function>;
		if constexpr (std::invocable<stored_type&, std::stop_token>)
		{
			return push(task_type(std::forward<Function>(function)));
		}
		else
		{
			return push(task_type(
				[stored = stored_type(std::forward<Function>(function))](std::stop_token) mutable {
					std::invoke(stored);
				}));
		}
	}

	// Safe to call repeatedly and from a worker task. Calls made from another
	// thread block until the worker has stopped. A later cancel request may
	// escalate an earlier drain request; a drain request never reverses cancel.
	void shutdown(background_worker_shutdown policy)
	{
		bool called_from_worker = false;
		bool request_task_stop = false;
		{
			std::lock_guard lock(mutex_);
			called_from_worker = worker_id_ == std::this_thread::get_id();
			request_task_stop = request_shutdown_locked(policy);
		}
		if (request_task_stop)
			stop_source_.request_stop();
		condition_.notify_all();

		if (called_from_worker)
			return;

		std::lock_guard join_lock(join_mutex_);
		if (worker_.joinable())
			worker_.join();
	}

	[[nodiscard]] background_worker_snapshot snapshot() const
	{
		std::lock_guard lock(mutex_);
		return snapshot_;
	}

	[[nodiscard]] std::exception_ptr last_exception() const
	{
		std::lock_guard lock(mutex_);
		return last_exception_;
	}

private:
	[[nodiscard]] bool request_shutdown_locked(background_worker_shutdown policy)
	{
		if (snapshot_.state == background_worker_state::stopped)
			return false;
		shutdown_requested_ = true;
		snapshot_.state = background_worker_state::stopping;
		if (policy != background_worker_shutdown::cancel || cancel_requested_)
			return false;

		cancel_requested_ = true;
		snapshot_.cancelled += queue_.size();
		queue_.clear();
		snapshot_.pending = 0;
		return true;
	}

	void worker_loop(std::stop_token task_stop_token)
	{
		{
			std::lock_guard lock(mutex_);
			worker_id_ = std::this_thread::get_id();
		}

		while (true)
		{
			task_type task;
			{
				std::unique_lock lock(mutex_);
				condition_.wait(lock, [this] {
					return shutdown_requested_ || !queue_.empty();
				});

				if (cancel_requested_ || (shutdown_requested_ && queue_.empty()))
					break;

				task = std::move(queue_.front());
				queue_.pop_front();
				++snapshot_.started;
				snapshot_.pending = queue_.size();
				snapshot_.task_running = true;
			}

			std::exception_ptr failure;
			try
			{
				task(task_stop_token);
			}
			catch (...)
			{
				failure = std::current_exception();
			}

			{
				std::lock_guard lock(mutex_);
				snapshot_.task_running = false;
				if (failure)
				{
					++snapshot_.failed;
					last_exception_ = failure;
				}
				else
				{
					++snapshot_.completed;
				}
			}

			if (failure && on_exception_)
			{
				try
				{
					on_exception_(failure);
				}
				catch (...)
				{
					// Exception reporting must not terminate the worker either.
				}
			}
		}

		{
			std::lock_guard lock(mutex_);
			snapshot_.state = background_worker_state::stopped;
			snapshot_.task_running = false;
			worker_id_ = {};
		}
		condition_.notify_all();
	}

	const background_worker_shutdown destructor_policy_;
	const exception_handler on_exception_;
	mutable std::mutex mutex_;
	std::mutex join_mutex_;
	std::condition_variable condition_;
	std::deque<task_type> queue_;
	std::stop_source stop_source_;
	background_worker_snapshot snapshot_;
	std::exception_ptr last_exception_;
	std::thread::id worker_id_;
	bool shutdown_requested_ = false;
	bool cancel_requested_ = false;
	std::jthread worker_;
};

// Retains the old tag-based convenience without a global registry of
// references whose pointees can disappear before stop_all() is called.
template<typename Tag>
struct worker_singleton
{
	static background_worker& instance()
	{
		static background_worker worker;
		return worker;
	}
};

} // namespace dixelu
