// process_memory.h
// Attach to a Windows process and expose its committed/readable memory as a
// single contiguous "linear" address space that can be read by offset.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mo {

// A single committed, readable memory region of the target process.
struct Region {
    uint64_t base = 0;      // address inside the target process
    uint64_t size = 0;      // byte length
    uint64_t linStart = 0;  // start offset of this region in the linear space
    uint32_t protect = 0;   // PAGE_* protection flags
    uint32_t type = 0;      // MEM_IMAGE / MEM_MAPPED / MEM_PRIVATE
};

struct ProcessEntry {
    uint32_t pid = 0;
    std::wstring name;
};

// Filters controlling which region types are folded into the linear space.
struct RegionFilter {
    bool image = true;    // MEM_IMAGE (modules / executables)
    bool mapped = true;   // MEM_MAPPED (file mappings)
    bool priv = true;     // MEM_PRIVATE (heaps, stacks, VirtualAlloc)
};

// Reverse lookup result: which region a linear offset falls in.
struct AddressInfo {
    bool valid = false;
    uint64_t address = 0;   // process address
    size_t regionIndex = 0;
    uint32_t protect = 0;
    uint32_t type = 0;
};

// Enumerate running processes (best-effort; requires no special rights).
std::vector<ProcessEntry> EnumerateProcesses();

// Try to acquire SeDebugPrivilege so we can read protected processes.
bool EnableDebugPrivilege();

class ProcessMemory {
public:
    ProcessMemory() = default;
    ~ProcessMemory();
    ProcessMemory(const ProcessMemory&) = delete;
    ProcessMemory& operator=(const ProcessMemory&) = delete;

    bool Attach(uint32_t pid);
    void Detach();
    bool Attached() const { return handle_ != nullptr; }
    uint32_t Pid() const { return pid_; }

    // Re-walk the address space honouring the current filter.
    void RefreshRegions(const RegionFilter& filter);

    // Total number of bytes in the linear space (sum of all included regions).
    uint64_t TotalSize() const { return total_; }
    const std::vector<Region>& Regions() const { return regions_; }

    // Read `count` bytes starting at linear offset into dst. Bytes that fall in
    // gaps or that fail to read are zero-filled. Returns bytes actually read OK.
    size_t ReadLinear(uint64_t offset, void* dst, size_t count);

    // Map a linear offset back to a process address / region.
    AddressInfo LinearToAddress(uint64_t offset) const;

    // Map a process address to a linear offset. Returns false if the address is
    // not inside any included region.
    bool AddressToLinear(uint64_t address, uint64_t& outOffset) const;

private:
    size_t FindRegion(uint64_t offset) const;  // index, or regions_.size()

    void* handle_ = nullptr;  // HANDLE
    uint32_t pid_ = 0;
    std::vector<Region> regions_;
    uint64_t total_ = 0;
};

} // namespace mo
