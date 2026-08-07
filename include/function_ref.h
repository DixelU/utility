#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace dixelu
{

// A non-owning callable view. The referenced callable must outlive this object.
// Rvalue functors are rejected to make accidental dangling references harder.
template<typename>
class function_ref;

template<typename R, typename... Args>
class function_ref<R(Args...)>
{
	using function_pointer = R(*)(Args...);

	union storage
	{
		void* object;
		function_pointer function;

		constexpr storage() noexcept : object(nullptr) {}
		constexpr explicit storage(void* value) noexcept : object(value) {}
		constexpr explicit storage(function_pointer value) noexcept : function(value) {}
	};

	using invoker = R(*)(const storage&, Args&&...);

	storage target_{};
	invoker invoke_ = nullptr;

public:
	constexpr function_ref() noexcept = default;
	constexpr function_ref(std::nullptr_t) noexcept {}

	constexpr function_ref(function_pointer function) noexcept :
		target_(function),
		invoke_([](const storage& target, Args&&... args) -> R {
			return std::invoke(
				target.function,
				std::forward<Args>(args)...);
		})
	{}

	template<typename F>
		requires (
			!std::is_same_v<std::remove_cvref_t<F>, function_ref> &&
			!std::is_pointer_v<std::remove_reference_t<F>> &&
			std::is_invocable_r_v<R, F&, Args...>)
	constexpr function_ref(F& callable) noexcept :
		target_(const_cast<void*>(static_cast<const void*>(std::addressof(callable)))),
		invoke_([](const storage& target, Args&&... args) -> R {
			return std::invoke(
				*static_cast<F*>(target.object),
				std::forward<Args>(args)...);
		})
	{}

	template<typename F>
		requires (!std::is_lvalue_reference_v<F>)
	function_ref(F&&) = delete;

	[[nodiscard]] constexpr explicit operator bool() const noexcept
	{
		return invoke_ != nullptr;
	}

	R operator()(Args... args) const
	{
		return invoke_(target_, std::forward<Args>(args)...);
	}
};

} // namespace dixelu
