#pragma once
#ifndef BBB_FFIO
#define BBB_FFIO

#include <stdio.h>
#include <fileapi.h>

#include <WinBase.h>

struct bbb_mmap
{
private:
	HANDLE hFile, hMapping;
	unsigned char* data;
	unsigned char* curr_pos;
	unsigned long long int size;

	void init(const char* filename)
	{
		hFile = CreateFileA(
			filename,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY,
			NULL
		);

		if (!hFile)
			return;

		LARGE_INTEGER fileSize;
		if (!GetFileSizeEx(hFile, &fileSize))
		{
			CloseHandle(hFile);
			hFile = nullptr;
			return;
		}

		size = fileSize.QuadPart;

		hMapping = CreateFileMappingW(
			hFile,
			NULL,
			PAGE_READONLY,
			fileSize.HighPart,
			fileSize.LowPart,
			NULL
		);

		if (!hMapping)
		{
			CloseHandle(hFile);
			hFile = nullptr;
			return;
		}

		void* ptr = MapViewOfFile(
			hMapping,
			FILE_MAP_READ,
			0,
			0,
			0
		);

		if (!ptr)
		{
			// Handle error
			CloseHandle(hMapping);
			CloseHandle(hFile);
			hMapping = nullptr;
			hFile = nullptr;
			return;
		}

		data = reinterpret_cast<unsigned char*>(ptr);
		curr_pos = data;
	}

	inline unsigned char __get()
	{
		size_t offset = curr_pos - data;
		if (offset < size) [[likely]]
			return *(curr_pos++);
		return 0;
	}

public:
	bbb_mmap():
		hFile(nullptr),
		hMapping(nullptr),
		data(nullptr),
		curr_pos(nullptr),
		size(0)
	{ }

	bbb_mmap(const char* filename):
		bbb_mmap()
	{
		init(filename);
	}

	~bbb_mmap()
	{
		close();
	}

	inline void reopen_next_file(const char* filename)
	{
		close();
		init(filename);
	}

	inline void seekg(unsigned long long int abs_pos)
	{
		curr_pos = data + abs_pos;
	}

	void close()
	{
		if (data)
			UnmapViewOfFile(data);

		if (hMapping)
			CloseHandle(hMapping);

		if (hFile)
			CloseHandle(hFile);

		size = 0;
		curr_pos = data = nullptr;
		hMapping = nullptr;
		hFile = nullptr;
	}

	inline unsigned char get()
	{
		return __get();
	}

	inline const unsigned char* begin() const
	{
		return data;
	}

	inline const unsigned char* ptr() const
	{
		return curr_pos;
	}

	inline signed long long int tellg() const
	{
		return curr_pos - data;
	}
	
	inline unsigned long long int length() const
	{
		return size;
	}

	inline bool good() const
	{
		return data && !eof();
	}
	inline bool eof() const
	{
		return tellg() >= size;
	}
};

#endif