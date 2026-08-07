#ifndef SAFMTQ_BASE_GLOBALMAP_H
#define SAFMTQ_BASE_GLOBALMAP_H

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>

struct CellsTile;

constexpr int CELL_SIDE_SIZE = 256;
constexpr int CELL_MIDDLE = CELL_SIDE_SIZE / 2;

enum class BuildingTypeID : uint16_t
{
	NONE = 0,

	INVALID = std::numeric_limits<uint16_t>::max()
};

enum class BuildingID : uint64_t
{
	NONE = 0,
};

struct BaseBuilding :
	std::enable_shared_from_this<BaseBuilding>
{
	BuildingTypeID type{};
	BuildingID id{};
	bool removed = false;

	virtual ~BaseBuilding() = default;

	[[nodiscard]] virtual BuildingTypeID get_type_id() const;
	[[nodiscard]] virtual BuildingID get_id() const;
};

template<typename T, typename U>
struct CellIndex
{
	using base_index_t = T;
	using compound_index_t = U;

	static_assert(
		sizeof(compound_index_t) == 2 * sizeof(base_index_t) &&
		std::is_integral_v<T> && std::is_integral_v<U>);

	constexpr static compound_index_t MASK = std::numeric_limits<base_index_t>::max();
	constexpr static compound_index_t SHIFT = std::numeric_limits<base_index_t>::digits;

	const base_index_t x;
	const base_index_t y;

	CellIndex(base_index_t x, base_index_t y) : x(x), y(y) {}
	CellIndex(compound_index_t index) : x(index & MASK), y(index >> SHIFT) {}

	operator compound_index_t() const
	{
		return x | (static_cast<compound_index_t>(y) << SHIFT);
	}

	std::strong_ordering operator<=>(const CellIndex& value) const
	{
		return static_cast<compound_index_t>(*this) <=> static_cast<compound_index_t>(value);
	}

	CellIndex operator+(const CellIndex& other) const { return {x + other.x, y + other.y}; }
	CellIndex operator-(const CellIndex& other) const { return {x - other.x, y - other.y}; }

	CellIndex operator*(base_index_t multiplier) const { return {x * multiplier, y * multiplier}; }
	CellIndex operator/(base_index_t divisor) const { return {x / divisor, y / divisor}; }

	template<typename W, typename Z>
	CellIndex(CellIndex<W, Z> other): x(other.x), y(other.y) {}
};

using cid8_t = CellIndex<std::uint8_t, std::uint16_t>;
using cid16_t = CellIndex<std::uint16_t, std::uint32_t>;
using cid32_t = CellIndex<std::uint32_t, std::uint64_t>;

struct BaseBuildingReference final : BaseBuilding
{
	std::weak_ptr<CellsTile> tile;
	cid8_t index;

	BaseBuildingReference(std::weak_ptr<CellsTile> tile, cid8_t index);

	~BaseBuildingReference() override = default;

	[[nodiscard]] auto get() const -> std::shared_ptr<BaseBuilding>;
};

struct CellsTile
{
	std::array<std::atomic<std::shared_ptr<BaseBuilding>>, CELL_SIDE_SIZE * CELL_SIDE_SIZE> container;
	cid32_t index;
};

struct GlobalMap
{
	std::map<cid32_t, CellsTile> tiles;
};

struct ModularBuilding final : BaseBuilding
{
	
};

#endif //SAFMTQ_BASE_GLOBALMAP_H