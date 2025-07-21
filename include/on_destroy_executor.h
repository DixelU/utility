#ifndef DIXELU_ON_DESTROY_EXECUTOR_H
#define DIXELU_ON_DESTROY_EXECUTOR_H

#include <algorithm>

namespace dixelu
{

template<typename Func>
class on_destroy_executor final
{
public:
	on_destroy_executor(Func&& func) : _f(std::move(func)) {}
	on_destroy_executor(on_destroy_executor&& ode) noexcept : _f(std::move(ode._f)) {}
	on_destroy_executor& operator=(on_destroy_executor&& ode) noexcept
	{
		_f = std::move(ode._f);
		return *this;
	}

	~on_destroy_executor() { safeFunctorExecutor(nullptr); }

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
};

template<typename Func>
on_destroy_executor<Func> make_on_destroy_executor(Func&& func)
{
	return on_destroy_executor<Func>(std::move(func));
}

}

#endif //DIXELU_ON_DESTROY_EXECUTOR_H
