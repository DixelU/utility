#pragma once

#include <type_traits>
#include <utility>

namespace dixelu
{

template<typename F>
class scope_exit final
{
	F function_;
	bool active_ = true;

public:
	explicit constexpr scope_exit(F function) noexcept(std::is_nothrow_move_constructible_v<F>) :
		function_(std::move(function))
	{}

	constexpr scope_exit(scope_exit&& other) noexcept(std::is_nothrow_move_constructible_v<F>) :
		function_(std::move(other.function_)),
		active_(std::exchange(other.active_, false))
	{}

	scope_exit(const scope_exit&) = delete;
	scope_exit& operator=(const scope_exit&) = delete;
	scope_exit& operator=(scope_exit&&) = delete;

	constexpr ~scope_exit() noexcept
	{
		if (active_)
			function_();
	}

	constexpr void release() noexcept { active_ = false; }
	[[nodiscard]] constexpr bool active() const noexcept { return active_; }
};

template<typename F>
[[nodiscard]] constexpr auto make_scope_exit(F&& function)
{
	return scope_exit<std::decay_t<F>>(std::forward<F>(function));
}

} // namespace dixelu
