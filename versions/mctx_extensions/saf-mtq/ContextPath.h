#ifndef CONTEXTPATH_H
#define CONTEXTPATH_H

#include <memory>
#include <stack>
#include <mctx.h>

//#include <boost/container/small_vector.hpp>
#include <utility>

struct LinkedContextWrapper;

template<typename F, typename Ret, typename Arg>
struct IsCallableWithSpecifiedSignature
{
private:
	// Use SFINAE to check if F can be called with Arg and returns Ret
	template <typename U>
	static auto test(U* ptr) -> decltype(
		std::is_same<decltype((*ptr)(std::declval<Arg>())), Ret>::value, std::true_type()
	);

	// Fallback if F can't be called with Arg or return type doesn't match Ret
	template <typename>
	static std::false_type test(...);

public:
	// Result will be true if F is callable with Arg and returns Ret
	static constexpr bool value = decltype(test<F>(nullptr))::value;
};

class ContextPath
{
public:
	template<typename T>
	using SmallVector = std::vector<T>;//boost::container::small_vector<T, 2>;
private:

	enum class EntryType
	{
		NONE, INT, STR, ROOT, PREVIOUS, CONDITIONAL, SINGLE_EQUALITY_QUERY, MULTIQUERY, VARIABLE
	};

	struct Entry
	{
		virtual ~Entry() = default;
		virtual dixelu::mctx& apply(dixelu::mctx& element) const = 0;
		[[nodiscard]] virtual const dixelu::mctx& apply(const dixelu::mctx& element) const = 0;
		[[nodiscard]] virtual dixelu::mctx* try_apply(dixelu::mctx& element) const = 0;
		[[nodiscard]] virtual const dixelu::mctx* try_apply(const dixelu::mctx& element) const = 0;
		[[nodiscard]] virtual SmallVector<const dixelu::mctx*> all(const dixelu::mctx& element) const = 0;
		[[nodiscard]] virtual EntryType type() const { return EntryType::NONE; }
		[[nodiscard]] virtual bool exists(const dixelu::mctx& element) const = 0;
		[[nodiscard]] virtual Entry* make_copy() const = 0;
		[[nodiscard]] virtual std::string stringify() const = 0;
		virtual bool remove(dixelu::mctx& target) const = 0;
	};

	struct IntegerEntry : Entry
	{
		IntegerEntry(size_t index): _index(index) {}
		~IntegerEntry() override = default;
		[[nodiscard]] dixelu::mctx& apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx& apply(const dixelu::mctx& element) const override;
		[[nodiscard]] dixelu::mctx* try_apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx* try_apply(const dixelu::mctx& element) const override;
		[[nodiscard]] bool exists(const dixelu::mctx& element) const override;
		[[nodiscard]] SmallVector<const dixelu::mctx*> all(const dixelu::mctx& element) const override;
		[[nodiscard]] EntryType type() const override { return EntryType::INT; }
		[[nodiscard]] Entry* make_copy() const override { return new IntegerEntry(_index); };
		[[nodiscard]] std::string stringify() const override { return std::to_string(_index); }
		[[nodiscard]] bool remove(dixelu::mctx& element) const override;
		[[nodiscard]] const size_t& __get() const { return _index; }
	private:
		size_t _index;
	};

	struct StringEntry :
		Entry
	{
		StringEntry(const char* key): _key(key) {}
		StringEntry(std::string key): _key(std::move(key)) {}
		StringEntry(std::string&& key): _key(std::move(key)) {}
		~StringEntry() override = default;
		[[nodiscard]] dixelu::mctx& apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx& apply(const dixelu::mctx& element) const override;
		[[nodiscard]] dixelu::mctx* try_apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx* try_apply(const dixelu::mctx& element) const override;
		[[nodiscard]] SmallVector<const dixelu::mctx*> all(const dixelu::mctx& element) const override;
		[[nodiscard]] bool exists(const dixelu::mctx& element) const override;
		[[nodiscard]] EntryType type() const override { return EntryType::STR; }
		[[nodiscard]] Entry* make_copy() const override { return new StringEntry(_key); };
		[[nodiscard]] std::string stringify() const override { return "\"" + _key + "\""; }
		[[nodiscard]] bool remove(dixelu::mctx& element) const override;
		[[nodiscard]] bool keyIsMalformed() const;
		[[nodiscard]] const std::string& __get() const { return _key; }
	private:
		std::string _key;
	};

	struct __PreviousTag {};
	struct __RootTag {};

	struct RootEntry:
		Entry
	{
		RootEntry(__RootTag) {};
		~RootEntry() override = default;
		[[nodiscard]] dixelu::mctx& apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx& apply(const dixelu::mctx& element) const override;
		[[nodiscard]] dixelu::mctx* try_apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx* try_apply(const dixelu::mctx& element) const override;
		[[nodiscard]] SmallVector<const dixelu::mctx*> all(const dixelu::mctx& element) const override;
		[[nodiscard]] EntryType type() const override;
		[[nodiscard]] bool exists(const dixelu::mctx& element) const override;
		[[nodiscard]] Entry* make_copy() const override;
		[[nodiscard]] std::string stringify() const override { return "#"; }
		bool remove(dixelu::mctx& element) const override;

		void applyRootGetter(std::function<dixelu::mctx*()> f) { _rootGetter = f; }
	private:
		std::function<dixelu::mctx*()> _rootGetter;
	};

	struct PreviousEntry:
		Entry
	{
		explicit PreviousEntry(__PreviousTag) {}
		~PreviousEntry() override = default;

		[[nodiscard]] dixelu::mctx& apply(dixelu::mctx& /*element*/) const override;
		[[nodiscard]] const dixelu::mctx& apply(const dixelu::mctx& /*element*/) const override;
		[[nodiscard]] dixelu::mctx* try_apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx* try_apply(const dixelu::mctx& element) const override;
		[[nodiscard]] bool exists(const dixelu::mctx& /*element*/) const override;
		[[nodiscard]] Entry* make_copy() const override;;
		[[nodiscard]] SmallVector<const dixelu::mctx*> all(const dixelu::mctx& element) const override;
		[[nodiscard]] EntryType type() const override { return EntryType::PREVIOUS; }
		[[nodiscard]] std::string stringify() const override { return ".."; }
		[[nodiscard]] bool remove(dixelu::mctx& element) const override;
	};

	struct __UnidentifiedValue{};
	struct SetOfPathsSetToValues;

	template<typename F>
	struct ConditionalEntry:
		Entry
	{
		static_assert(
			IsCallableWithSpecifiedSignature<F, bool, const dixelu::mctx&>::value,
			"Cannot be used with specified condition");

		explicit ConditionalEntry(F&& f): f(std::forward<F>(f)) {}
		~ConditionalEntry() override = default;
		[[nodiscard]] dixelu::mctx& apply(dixelu::mctx& element) const override
		{
			auto result = lookupCondition(element);
			if(result == element.end())
				throw std::runtime_error("Could not find specified conditional value");
			return *result;
		}
		[[nodiscard]] const dixelu::mctx& apply(const dixelu::mctx& element) const override
		{
			auto result = lookupCondition(element);
			if(result == element.end())
				throw std::runtime_error("Could not find specified conditional value");
			return *result;
		}

		[[nodiscard]] dixelu::mctx* try_apply(dixelu::mctx& element) const override
		{
			auto result = lookupCondition(element);
			if(result == element.end())
				return nullptr;
			return &*result;
		}

		[[nodiscard]] const dixelu::mctx* try_apply(const dixelu::mctx& element) const override
		{
			auto result = lookupCondition(element);
			if(result == element.end())
				return nullptr;
			return &*result;
		}

		[[nodiscard]] bool exists(const dixelu::mctx& element) const override
		{
			return lookupCondition(element) != element.end();
		}

		[[nodiscard]] EntryType type() const override { return EntryType::CONDITIONAL; }
		[[nodiscard]] Entry* make_copy() const override { return new ConditionalEntry<F>(static_cast<F>(f)); }
		[[nodiscard]] std::string stringify() const override { return "*:{??F??}"; }

		[[nodiscard]] SmallVector<const dixelu::mctx*> all(const dixelu::mctx& element) const override
		{
			SmallVector<const dixelu::mctx*> result;
			auto iterators = batchLookupCondition(element);;
			for(auto& singleElement: iterators)
				result.push_back(&(*singleElement));
			return result;
		}

		[[nodiscard]] bool remove(dixelu::mctx& element) const override
		{
			const auto result = lookupCondition(element);
			if(result == element.end())
				return false;

			element.erase(result);
			return true;
		}

		bool removeAll(dixelu::mctx& element) const
		{
			const auto result = batchLookupCondition(element);
			for(auto& singleIt: result)
				element.erase(singleIt);

			return result.size();
		}

	protected:
		friend struct SetOfPathsSetToValues;

		[[nodiscard]] auto lookupCondition(dixelu::mctx& source) const ->
			decltype(source.end())
		{
			for(auto it = source.begin(); it != source.end(); ++it)
				if(f(*it))
					return it;
			return source.end();
		}

		[[nodiscard]] auto lookupCondition(const dixelu::mctx& source) const ->
			decltype(source.end())
		{
			for(auto it = source.begin(); it != source.end(); ++it)
				if(f(*it))
					return it;
			return source.end();
		}

		[[nodiscard]] auto batchLookupCondition(dixelu::mctx& source) const ->
			SmallVector<decltype(source.end())>
		{
			SmallVector<decltype(source.end())> allInstances;
			for(auto it = source.begin(); it != source.end(); ++it)
				if(f(*it))
					allInstances.emplace_back(it);
			return allInstances;
		}

		[[nodiscard]] auto batchLookupCondition(const dixelu::mctx& source) const ->
			SmallVector<decltype(source.end())>
		{
			SmallVector<decltype(source.end())> allInstances;
			for(auto it = source.begin(); it != source.end(); ++it)
				if(f(*it))
					allInstances.emplace_back(it);
			return allInstances;
		}

		F f;
	};

	struct UnidentifiedValue;

	struct SubpathIsSetToValueEntry:
		ConditionalEntry<std::function<bool(const dixelu::mctx&)>>
	{
		SubpathIsSetToValueEntry(ContextPath path, IntegerEntry entry):
			SubpathIsSetToValueEntry(std::move(path), &entry) {}
		SubpathIsSetToValueEntry(ContextPath path, StringEntry entry):
			SubpathIsSetToValueEntry(std::move(path), &entry) {}
		SubpathIsSetToValueEntry(ContextPath path, UnidentifiedValue entry):
			SubpathIsSetToValueEntry(std::move(path), &entry) {}

		SubpathIsSetToValueEntry(SubpathIsSetToValueEntry&& entry);
		SubpathIsSetToValueEntry(const SubpathIsSetToValueEntry& entry);
		~SubpathIsSetToValueEntry() override = default;

		[[nodiscard]] Entry* make_copy() const override;
		[[nodiscard]] std::string stringify() const override;
		[[nodiscard]] std::string stringifyInternals() const;
		[[nodiscard]] EntryType type() const override { return EntryType::SINGLE_EQUALITY_QUERY; }
	private:
		[[nodiscard]] bool __checkElementCompliance(const dixelu::mctx& element) const;

		friend struct SetOfPathsSetToValues;
		SubpathIsSetToValueEntry(ContextPath path, const Entry* entry);
		[[nodiscard]] const dixelu::mctx* __applyImpl(const dixelu::mctx& element) const;

		std::unique_ptr<ContextPath> _subpath;
		std::unique_ptr<Entry> _entry;
	};

	struct SetOfPathsSetToValues:
		Entry
	{
		SetOfPathsSetToValues() = default;

		SetOfPathsSetToValues(SubpathIsSetToValueEntry entry);

		~SetOfPathsSetToValues() override = default;
		[[nodiscard]] dixelu::mctx& apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx& apply(const dixelu::mctx& element) const override;
		[[nodiscard]] dixelu::mctx* try_apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx* try_apply(const dixelu::mctx& element) const override;
		[[nodiscard]] bool exists(const dixelu::mctx& element) const override;
		[[nodiscard]] Entry* make_copy() const override;
		[[nodiscard]] std::string stringify() const override;
		[[nodiscard]] bool remove(dixelu::mctx& element) const override;
		void operator|=(SubpathIsSetToValueEntry entry);
		[[nodiscard]] size_t size() const { return _values.size(); }
		[[nodiscard]] EntryType type() const override { return EntryType::MULTIQUERY; }
		[[nodiscard]] SmallVector<const dixelu::mctx*> all(const dixelu::mctx& element) const override;

	private:
		[[nodiscard]] bool __all(const dixelu::mctx& ctx) const;
		[[nodiscard]] bool __any(const dixelu::mctx& ctx) const;

		[[nodiscard]] auto lookupCondition(dixelu::mctx& source) const ->
			decltype(source.end());
		[[nodiscard]] auto batchLookupCondition(dixelu::mctx& source) const ->
			SmallVector< decltype(source.end())>;
		[[nodiscard]] auto lookupCondition(const dixelu::mctx& source) const ->
			decltype(source.end());
		[[nodiscard]] auto batchLookupCondition(const dixelu::mctx& source) const ->
			SmallVector< decltype(source.end())>;

		std::deque<SubpathIsSetToValueEntry> _values;
	};

	struct UnidentifiedValue:
		Entry
	{
		UnidentifiedValue(__UnidentifiedValue) {}
		~UnidentifiedValue() override = default;
		UnidentifiedValue(UnidentifiedValue&& rhs) noexcept;
		UnidentifiedValue& operator=(UnidentifiedValue&& rhs) = default;

		[[nodiscard]] dixelu::mctx& apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx& apply(const dixelu::mctx& element) const override;
		[[nodiscard]] dixelu::mctx* try_apply(dixelu::mctx& element) const override;
		[[nodiscard]] const dixelu::mctx* try_apply(const dixelu::mctx& element) const override;
		[[nodiscard]] bool exists(const dixelu::mctx& element) const override;
		[[nodiscard]] bool remove(dixelu::mctx& element) const override;

		[[nodiscard]] bool existsAny(
			const dixelu::mctx& element,
			const Entry* next,
			const ContextPath& path,
			size_t nextIndex) const;

		[[nodiscard]] bool existsAll(
			const dixelu::mctx& element,
			const Entry* next,
			const ContextPath& path,
			size_t nextIndex) const;

		[[nodiscard]] bool removeAny(
			dixelu::mctx& context,
			const Entry* next,
			const ContextPath& path,
			size_t nextIndex) const;

		[[nodiscard]] bool removeAll(
			dixelu::mctx& context,
			const Entry* next,
			const ContextPath& path,
			size_t nextIndex) const;

		void set(const StringEntry& entry) const { _appliedEntry = std::make_unique<StringEntry>(entry); }
		void set(const IntegerEntry& entry) const { _appliedEntry = std::make_unique<IntegerEntry>(entry); }
		void set(const Entry* entry) const { _appliedEntry.reset(entry->make_copy()); }

		EntryType type() const override { return EntryType::VARIABLE; }
		SmallVector<const dixelu::mctx*> all(const dixelu::mctx& element) const override;
		Entry* make_copy() const override;;
		std::string stringify() const override { return "*"; }
	private:
		mutable std::unique_ptr<Entry> _appliedEntry;
	};

	/*  -1 ~ last; any non-negative - index */
	std::pair<const dixelu::mctx*, int /* nestness level */> applyAndGetNthEntry(
		const dixelu::mctx& ctx,
		bool& putLastContextOnFailure,
		int applyCountReturnElement = -1) const;

public:
	using SetOfQueriesEntry = SetOfPathsSetToValues;

	ContextPath() = default;
	ContextPath(const RootEntry& entry) { _path.emplace_back(new RootEntry(entry)); }
	ContextPath(const StringEntry& entry) { _path.emplace_back(new StringEntry(entry)); }
	ContextPath(const IntegerEntry& entry) { _path.emplace_back(new IntegerEntry(entry)); }
	ContextPath(const SetOfQueriesEntry& entry) { _path.emplace_back(new SetOfQueriesEntry(entry)); }
	ContextPath(Entry* otherEntry) { _path.emplace_back(otherEntry); }
	ContextPath(const ContextPath& copySource);
	ContextPath(ContextPath&& copySource) = default;

	// copy-inducing operators
	ContextPath operator/(StringEntry stringEntry) const &;
	ContextPath operator/(IntegerEntry stringEntry) const &;
	ContextPath operator/(Entry* stringEntry) const &;
	ContextPath operator/(ContextPath&& subPath) const &;
	ContextPath operator/(__PreviousTag) const &;
	ContextPath operator/(__UnidentifiedValue) const &;
	ContextPath operator/(__RootTag) const&;

	// move-inducing operators
	ContextPath operator/(StringEntry stringEntry) &&;
	ContextPath operator/(IntegerEntry stringEntry) &&;
	ContextPath operator/(Entry* stringEntry) &&;
	ContextPath operator/(ContextPath&& subPath) &&;
	ContextPath operator/(__PreviousTag) &&;
	ContextPath operator/(__UnidentifiedValue) &&;
	ContextPath operator/(__RootTag) &&;

	~ContextPath() = default;
	ContextPath& operator=(const ContextPath& rhs);

	dixelu::mctx& operator[](dixelu::mctx& target) const;
	const dixelu::mctx& operator[](const dixelu::mctx& target) const;

	[[nodiscard]] bool exists(const dixelu::mctx& target) const;
	[[nodiscard]] bool existsAny(const dixelu::mctx& target) const;
	[[nodiscard]] bool existsAll(const dixelu::mctx& target) const;
	[[nodiscard]] ContextPath copy() const { return *this; }

	[[nodiscard]] std::string stringify() const;

	template<typename... Args>
	void setVariables(Args... args) { __setVariables(_path.begin(), args...); }

	[[nodiscard]] size_t size() const;

	bool remove(dixelu::mctx& target) const;

	template<typename F>
	static Entry* makeConditional(F&& f) { return new ConditionalEntry<F>(std::forward<F>(f)); }

	static SubpathIsSetToValueEntry makeSinglePathSetToValue(ContextPath&& path, IntegerEntry entry);
	static SubpathIsSetToValueEntry makeSinglePathSetToValue(ContextPath&& path, StringEntry entry);
	static SubpathIsSetToValueEntry makeSinglePathSetToValue(ContextPath&& path, UnidentifiedValue entry);

	[[nodiscard]] const dixelu::mctx* getConditional(const dixelu::mctx& element) const;

	template <template<class> class Vector = std::deque>
	Vector<const dixelu::mctx*> getAllMatching(const dixelu::mctx& element) const
	{
		using pathIterator = typename std::deque<std::unique_ptr<Entry>>::const_iterator;
		std::stack<std::pair<const dixelu::mctx*, pathIterator>> stack;
		Vector<const dixelu::mctx*> result;

		const dixelu::mctx* rawPtr = &element;
		auto it = _path.begin();
		for(;;)
		{
			auto& el = *it;
			auto current = el->all(*rawPtr);
			++it;

			if(it != _path.end())
				for(auto& singleCtx: current)
					stack.emplace(singleCtx, it);
			else
				for(auto& singleCtx: current)
					result.emplace_back(singleCtx);

			if(!stack.empty())
			{
				auto res = std::move(stack.top());
				stack.pop();
				rawPtr = res.first;
				it = res.second;
			}
			else
				return result;
		}
	}

	static constexpr __UnidentifiedValue variable{};
	static constexpr __PreviousTag previous{};
	static constexpr __RootTag $root{};

private:
	void __setRootContext(const std::shared_ptr<dixelu::mctx>& element) const;

	[[nodiscard]] bool existsAnyRecursive(const dixelu::mctx& target, size_t index) const;
	[[nodiscard]] bool existsAllRecursive(const dixelu::mctx& target, size_t index) const;

	using pathIt =  std::deque<std::unique_ptr<Entry>>::iterator;

	template<typename T, typename = void>
	auto setSingleVariable(
		UnidentifiedValue* /*targetValue*/,
		T /*entryValue*/) ->
		typename std::enable_if<std::is_base_of<Entry, T>::value, void>::type
	{
		throw std::runtime_error("Unknown type of variable set in setSingleVariable");
	}

	void __setVariables(const pathIt&) {}

	template<typename T, typename... Targs>
	void __setVariables(pathIt it, T value, Targs... Fargs)
	{
		while(it != _path.end())
		{
			if(auto ptr = dynamic_cast<UnidentifiedValue*>(it->get()))
			{
				ptr->set(value);
				break;
			}
			++it;
		}

		__setVariables(it, Fargs...);
	}

	bool removeRecursive(dixelu::mctx& context, size_t index) const;

	friend struct LinkedContextWrapper;

	std::deque<std::unique_ptr<Entry>> _path;
};

std::string serialiseContextTypes(const dixelu::mctx& source);

template<>
void ContextPath::setSingleVariable<ContextPath::StringEntry>(
	UnidentifiedValue* targetValue,
	StringEntry entryValue);

template<>
void ContextPath::setSingleVariable<ContextPath::IntegerEntry>(
	UnidentifiedValue* targetValue,
	IntegerEntry entryValue);

inline namespace literals
{

constexpr auto $root = ContextPath::$root;
ContextPath operator""_ctxpath(const char* str, size_t);

}

struct ContextPathSerializer
{
	static ContextPath deserialize(const std::string& string);
	static std::string serialize(const ContextPath& source) { return source.stringify(); }

private:
	enum class TokenType
	{
		ROOT, DELIM, ANY, WHICH, NEST_START, NEST_END, EQUALS, AND, STRING, NUMBER
	};

	struct Token
	{
		TokenType type;
		std::string value;
	};

	static std::deque<Token> tokenize(const std::string& string);

	// todo: implement using the railway sorting station algorithm?
	static ContextPath buildPath(
		std::deque<Token>::iterator tokensBegin,
		std::deque<Token>::iterator tokensEnd,
		bool forcePathExtensions = false,
		int nestness = 0);
};

struct LinkedContextWrapper
{
private:
	struct TrivialPseudoIterator;
	friend struct TrivialPseudoIterator;
protected:
	std::shared_ptr<dixelu::mctx> _rootContext;
	const dixelu::mctx* _currentNode;
public:

	explicit LinkedContextWrapper(std::shared_ptr<dixelu::mctx>&& rootContext);

	constexpr LinkedContextWrapper() noexcept:
		_currentNode(nullptr) {}

	/* also resets the current node */
	[[nodiscard]] LinkedContextWrapper deepClone() const;

	[[nodiscard]] bool isValid() const noexcept { return _currentNode != nullptr; }
	[[nodiscard]] bool empty() const noexcept { return _currentNode == nullptr || !_rootContext.get(); }
	[[nodiscard]] bool ownsSettingsInstance() const noexcept { return _rootContext.use_count() == 1; }

	LinkedContextWrapper operator[](std::string key) const;
	LinkedContextWrapper operator[](ContextPath path) const;

	template<typename T>
	T get_as(std::string key, T defaultValue) const
	{
		auto subfield = __getSubfield(std::move(key));
		if(subfield._currentNode)
			return subfield._currentNode->get<T>(defaultValue);
		return defaultValue;
	}

	[[nodiscard]] LinkedContextWrapper at(std::string key) const { return operator[](std::move(key)); }
	[[nodiscard]] LinkedContextWrapper at(ContextPath path) const { return operator[](std::move(path)); }

	template<typename T>
	T get_as(T defaultValue) const
	{
		if(_currentNode and _currentNode->is<T>())
			return _currentNode->get<T>();
		return defaultValue;
	}

	template<typename T>
	const T& as() const
	{
		if(_currentNode and _currentNode->is<T>())
			return _currentNode->as<T>();
		throw std::runtime_error("Unknown type or empty value");
	}

	template<typename T>
	const T& as(const T& substitute) const noexcept
	{
		if(_currentNode and _currentNode->is<T>())
			return _currentNode->as<T>();
		throw substitute;
	}

	const LinkedContextWrapper* operator->() const noexcept { return this; }

	[[nodiscard]] TrivialPseudoIterator begin() const;
	[[nodiscard]] TrivialPseudoIterator end() const;
	[[nodiscard]] TrivialPseudoIterator find(const std::string& key) const;

	[[nodiscard]] dixelu::mctx renderAsLinkedContext() const;

	void applyRenderedContextDiff(const dixelu::mctx& diff, bool forceNoDiffForwarding = false) const;

	void clear() { _currentNode = nullptr; _rootContext = nullptr; }
	[[nodiscard]] const dixelu::mctx& getUnderlyingContextRoot() const noexcept { return *_rootContext; }
	[[nodiscard]] const dixelu::mctx& getUnderlyingContextNode() const noexcept { return *_currentNode; }

private:

	struct TrivialPseudoIterator
	{
		using IteratorKind = decltype(std::declval<dixelu::mctx>().cbegin());
		TrivialPseudoIterator(std::shared_ptr<dixelu::mctx> rootContext, IteratorKind&& iter);

		TrivialPseudoIterator operator++();
		TrivialPseudoIterator operator++(int);
		TrivialPseudoIterator operator--();
		TrivialPseudoIterator operator--(int);

		bool operator==(const TrivialPseudoIterator& other) const;
		bool operator!=(const TrivialPseudoIterator& other) const;

		LinkedContextWrapper operator*() const;
		LinkedContextWrapper operator->() const;

	private:
		IteratorKind _it;
		decltype(_rootContext) _root;
	};

	void applyRenderedContextDiff__internal(
		const dixelu::mctx& diff,
		bool& dropCurrentField,
		bool forceNoDiffForwarding = false) const;

	void cloneAndCheckForLinks__recursive(
		dixelu::mctx& renderedContext,
		size_t linkageRecursivenessDepth = 0,
		bool clone = false) const;

	static void deserializeContainedPaths(dixelu::mctx& ctx);

	[[nodiscard]] LinkedContextWrapper makeShallowCopy() const;

	[[nodiscard]] ContextPath::SmallVector<LinkedContextWrapper>
		__getByPath(ContextPath& path) const;

	[[nodiscard]] LinkedContextWrapper __getSubfield(std::string&& key) const;

	const dixelu::mctx* getLinkedContext(const dixelu::mctx* nodeContext, bool& doesNotExist) const;

	[[nodiscard]] ContextPath::SmallVector<const dixelu::mctx*>
		getAllLinkedContexts(const dixelu::mctx* nodeContext, bool& doesNotExist) const;

	constexpr static auto REF_TAG = "$ref";
	constexpr static auto ARRAY_TAG = "$as_array";
};

#endif //CONTEXTPATH_H
