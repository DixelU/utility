#ifndef __SAFMTQ__METASDK_UTILS_H__
#define __SAFMTQ__METASDK_UTILS_H__

#include <mctx.h>

#include <string>
#include <utility>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <list>

#include <functional>

#include <ContextPath.h>

template<typename T>
struct PromisedContextValue:
	protected dixelu::mctx
{
private:
	using CtxRef = std::reference_wrapper<dixelu::mctx>;
	using RefContainer = std::list<CtxRef>;

	struct ControlBlock
	{
		std::mutex _mtx;
		std::unique_ptr<T> _actualData;
		RefContainer _copyReferences;

		RefContainer::iterator addRef(CtxRef ref)
		{
			std::lock_guard<std::mutex> locker(_mtx);
			_copyReferences.push_back(ref);
			auto endIt = _copyReferences.end();
			--endIt;
			return endIt;
		}

		RefContainer::iterator removeRef(RefContainer::iterator refIt) noexcept
		{
			std::lock_guard<std::mutex> locker(_mtx);
			auto refEndIt = _copyReferences.end();
			if(refIt != refEndIt)
				_copyReferences.erase(refIt);
			return refEndIt;
		}

		void assign(T data)
		{
			std::lock_guard<std::mutex> locker(_mtx);
			_actualData.reset(new T(std::move(data)));
			for(auto& el: _copyReferences)
				el.get() = *_actualData;
		}

		ControlBlock() = default;
		~ControlBlock() = default;
	};

	CtxRef getSelfReference()
	{
		return std::ref<dixelu::mctx>(*this);
	}

	CtxRef getSelfReference() const
	{
		return std::ref<dixelu::mctx>(*const_cast<PromisedContextValue*>(this));
	}

	void __prepareToDestory()
	{
		if(_controlBlock)
			_thisContextDataIterator = _controlBlock->removeRef(_thisContextDataIterator);
	}

public:
	PromisedContextValue():
		_controlBlock(std::make_shared<ControlBlock>())
	{
		dixelu::mctx::operator=(nullptr);
		_thisContextDataIterator = _controlBlock->addRef(getSelfReference());
	}


	~PromisedContextValue() override
	{
		__prepareToDestory();
	}

	PromisedContextValue(PromisedContextValue&& rhs):
		dixelu::mctx(std::move(static_cast<dixelu::mctx&>(rhs))),
		_controlBlock(std::move(rhs._controlBlock))
	{
		rhs._thisContextDataIterator = _controlBlock->removeRef(rhs._thisContextDataIterator);
		_thisContextDataIterator = _controlBlock->addRef(getSelfReference());
	}

	PromisedContextValue(const PromisedContextValue& rhs):
		dixelu::mctx((static_cast<const dixelu::mctx&>(rhs))),
		_controlBlock(rhs._controlBlock),
		_thisContextDataIterator(_controlBlock->addRef(getSelfReference()))
	{

	}

	PromisedContextValue& operator=(const PromisedContextValue& rhs)
	{
		if(&rhs == this)
			return *this;

		__prepareToDestory();
		dixelu::mctx::operator=(static_cast<const dixelu::mctx&>(rhs));
		_controlBlock = rhs._controlBlock;
		_thisContextDataIterator = _controlBlock->addRef(getSelfReference());
		return *this;
	}

	PromisedContextValue& operator=(PromisedContextValue&& rhs)
	{
		if(&rhs == this)
			return *this;

		__prepareToDestory();
		dixelu::mctx::operator=(std::move(static_cast<dixelu::mctx&&>(rhs)));
		_controlBlock = std::move(rhs._controlBlock);
		rhs._thisContextDataIterator = _controlBlock->removeRef(rhs._thisContextDataIterator);
		_thisContextDataIterator = _controlBlock->addRef(getSelfReference());
		return *this;
	}

	void assign(T&& data) { _controlBlock->assign(std::move(data)); }

	inline bool operator==(const PromisedContextValue<T>& rhs) const
	{
		return _controlBlock == rhs._controlBlock;
	}

protected:
	std::shared_ptr<ControlBlock> _controlBlock;
	mutable RefContainer::iterator _thisContextDataIterator;
};

dixelu::mctx getDifference(const dixelu::mctx& _old, const dixelu::mctx& _new);
dixelu::mctx mergeSettingsContext(const dixelu::mctx& target, const dixelu::mctx& sourse);

std::string deterministicUUID(const std::string& string);

template<typename T>
struct LazyElement:
	dixelu::mctx
{
	static_assert((!std::is_same_v<T, void>), "LazyElement \"get\" functor cannot return void");

	LazyElement(std::function<T()> getFunctor):
		_controlBlock(std::make_shared<ControlBlock>()),
		_f(std::move(getFunctor)) {}
	LazyElement(const LazyElement&) = default;
	LazyElement(LazyElement&&) = default;
	LazyElement& operator=(const LazyElement&) = default;
	LazyElement& operator=(LazyElement&&) = default;
	~LazyElement() override = default;
	operator T&() { return __privateGet(); }
	operator const T&() const { return __privateGet(); }
private:
	T& __privateGet() const
	{
		std::unique_lock<std::mutex> locker(_controlBlock->_mtx);
		auto& cachedData = _controlBlock->_cachedData;

		if(!cachedData)
			cachedData.reset(new T(_f()));

		return *cachedData;
	}

	struct ControlBlock
	{
		mutable std::unique_ptr<T> _cachedData;
		std::mutex _mtx;
	};

	const std::shared_ptr<ControlBlock> _controlBlock;
	const std::function<T()> _f;
};

template<typename T>
T& extractConditionalLazy(dixelu::mctx& ctx)
{
	if(ctx.is<LazyElement<T>>())
	{
		T& lazyElementRef = ctx.as<LazyElement<T>>();
		T unconditionallyMovedFromElement = std::move(lazyElementRef);
		ctx = std::move(unconditionallyMovedFromElement);
		return ctx.as<T>();
	}

	if(ctx.is<T>())
		return ctx.as<T>();

	throw std::runtime_error(
		"No lazy elements of type " +
		std::string(typeid(T).name()));
}

std::string demangle(const char* name);

#endif // __SAFMTQ__METASDK_UTILS_H__
