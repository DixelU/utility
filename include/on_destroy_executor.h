#ifndef DIXELU_ON_DESTROY_EXECUTOR_H
#define DIXELU_ON_DESTROY_EXECUTOR_H

#include <type_traits>
#include <utility>

namespace dixelu
{

template<typename Func>
class on_destroy_executor final
{
public:
	on_destroy_executor(Func&& func) : _f(std::move(func)) {}
	on_destroy_executor(on_destroy_executor&& ode) noexcept(std::is_nothrow_move_constructible_v<Func>) :
		_f(std::move(ode._f)),
		_active(std::exchange(ode._active, false))
	{}
	on_destroy_executor& operator=(on_destroy_executor&& ode) noexcept(std::is_nothrow_move_assignable_v<Func>)
	{
		if (this == &ode)
			return *this;
		if (_active)
			safeFunctorExecutor(nullptr);
		_f = std::move(ode._f);
		_active = std::exchange(ode._active, false);
		return *this;
	}

	~on_destroy_executor()
	{
		if (_active)
			safeFunctorExecutor(nullptr);
	}

	void dismiss() noexcept { _active = false; }

private:

	template<typename F = Func>
	void safeFunctorExecutor(
		typename std::enable_if<std::is_same<decltype(static_cast<bool>(std::declval<F>())), bool>::value, void*>::type _ = nullptr)
	{
		if(_f) _f();
	}

	template<typename = Func>
	void safeFunctorExecutor(...)
	{
		_f();
	}

	Func _f;
	bool _active = true;
};

template<typename Func>
on_destroy_executor<Func> make_on_destroy_executor(Func&& func)
{
	return on_destroy_executor<Func>(std::move(func));
}

}

#endif //DIXELU_ON_DESTROY_EXECUTOR_H
