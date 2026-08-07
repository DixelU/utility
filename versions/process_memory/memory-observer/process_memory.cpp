// process_memory.cpp
#include "process_memory.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>

namespace mo {

std::vector<ProcessEntry> EnumerateProcesses() {
    std::vector<ProcessEntry> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcessEntry e;
            e.pid = pe.th32ProcessID;
            e.name = pe.szExeFile;
            out.push_back(std::move(e));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    std::sort(out.begin(), out.end(), [](const ProcessEntry& a, const ProcessEntry& b) {
        int c = _wcsicmp(a.name.c_str(), b.name.c_str());
        if (c != 0) return c < 0;
        return a.pid < b.pid;
    });
    return out;
}

bool EnableDebugPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;

    LUID luid{};
    bool ok = false;
    if (LookupPrivilegeValueA(nullptr, SE_DEBUG_NAME, &luid)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        ok = (GetLastError() == ERROR_SUCCESS);
    }
    CloseHandle(token);
    return ok;
}

ProcessMemory::~ProcessMemory() { Detach(); }

bool ProcessMemory::Attach(uint32_t pid) {
    Detach();
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) {
        // Fall back to the limited query right (works for more processes).
        h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                        FALSE, pid);
    }
    if (!h)
        return false;
    handle_ = h;
    pid_ = pid;
    RegionFilter all;
    RefreshRegions(all);
    return true;
}

void ProcessMemory::Detach() {
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    pid_ = 0;
    regions_.clear();
    total_ = 0;
}

static bool ProtectReadable(DWORD protect) {
    if (protect & PAGE_GUARD) return false;
    DWORD base = protect & 0xFF;
    switch (base) {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;  // PAGE_NOACCESS and friends
    }
}

void ProcessMemory::RefreshRegions(const RegionFilter& filter) {
    regions_.clear();
    total_ = 0;
    if (!handle_)
        return;

    HANDLE h = static_cast<HANDLE>(handle_);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint64_t addr = reinterpret_cast<uint64_t>(si.lpMinimumApplicationAddress);
    const uint64_t maxAddr =
        reinterpret_cast<uint64_t>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi{};
    while (addr <= maxAddr) {
        SIZE_T got = VirtualQueryEx(h, reinterpret_cast<LPCVOID>(addr),
                                    &mbi, sizeof(mbi));
        if (got == 0)
            break;

        uint64_t regBase = reinterpret_cast<uint64_t>(mbi.BaseAddress);
        uint64_t regSize = static_cast<uint64_t>(mbi.RegionSize);

        bool include = mbi.State == MEM_COMMIT && ProtectReadable(mbi.Protect);
        if (include) {
            switch (mbi.Type) {
                case MEM_IMAGE:   include = filter.image; break;
                case MEM_MAPPED:  include = filter.mapped; break;
                case MEM_PRIVATE: include = filter.priv; break;
                default:          include = false; break;
            }
        }
        if (include) {
            Region r;
            r.base = regBase;
            r.size = regSize;
            r.linStart = total_;
            r.protect = mbi.Protect;
            r.type = mbi.Type;
            regions_.push_back(r);
            total_ += regSize;
        }

        uint64_t next = regBase + regSize;
        if (next <= addr)  // guard against no-progress / overflow
            break;
        addr = next;
    }
}

size_t ProcessMemory::FindRegion(uint64_t offset) const {
    if (regions_.empty() || offset >= total_)
        return regions_.size();
    // Greatest linStart <= offset.
    size_t lo = 0, hi = regions_.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (regions_[mid].linStart <= offset)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

size_t ProcessMemory::ReadLinear(uint64_t offset, void* dst, size_t count) {
    auto* out = static_cast<uint8_t*>(dst);
    std::fill(out, out + count, 0);
    if (!handle_ || count == 0 || offset >= total_)
        return 0;

    HANDLE h = static_cast<HANDLE>(handle_);
    size_t okBytes = 0;
    uint64_t cur = offset;
    size_t remaining = count;

    while (remaining > 0 && cur < total_) {
        size_t ri = FindRegion(cur);
        if (ri >= regions_.size())
            break;
        const Region& r = regions_[ri];
        uint64_t inReg = cur - r.linStart;          // offset within region
        uint64_t avail = r.size - inReg;            // bytes left in region
        size_t chunk = static_cast<size_t>(
            std::min<uint64_t>(avail, remaining));

        SIZE_T read = 0;
        if (ReadProcessMemory(h,
                              reinterpret_cast<LPCVOID>(r.base + inReg),
                              out, chunk, &read)) {
            okBytes += read;
        }
        // Whether or not the read succeeded, advance by the whole chunk so the
        // linear layout stays consistent (failed bytes remain zero).
        out += chunk;
        cur += chunk;
        remaining -= chunk;
    }
    return okBytes;
}

bool ProcessMemory::AddressToLinear(uint64_t address, uint64_t& outOffset) const {
    // Regions are sorted by base address (VirtualQueryEx walks ascending).
    for (const Region& r : regions_) {
        if (address >= r.base && address < r.base + r.size) {
            outOffset = r.linStart + (address - r.base);
            return true;
        }
    }
    return false;
}

AddressInfo ProcessMemory::LinearToAddress(uint64_t offset) const {
    AddressInfo info;
    size_t ri = FindRegion(offset);
    if (ri >= regions_.size())
        return info;
    const Region& r = regions_[ri];
    info.valid = true;
    info.address = r.base + (offset - r.linStart);
    info.regionIndex = ri;
    info.protect = r.protect;
    info.type = r.type;
    return info;
}

} // namespace mo
