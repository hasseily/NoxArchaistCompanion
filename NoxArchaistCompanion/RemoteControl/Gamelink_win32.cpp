#if defined(_WIN32)

#include "RemoteControl/Gamelink_backend.h"

#include <Windows.h>

namespace
{
	HANDLE g_mutex_handle = nullptr;
	HANDLE g_mmap_handle = nullptr;
	void* g_mapped_view = nullptr;
}

namespace GameLink::backend
{
	bool create_mutex(const char* name)
	{
		if (g_mutex_handle)
			return true;

		// If the mutex already exists another instance is running.
		HANDLE existing = OpenMutexA(SYNCHRONIZE, FALSE, name);
		if (existing)
		{
			CloseHandle(existing);
			return false;
		}

		g_mutex_handle = CreateMutexA(nullptr, FALSE, name);
		return g_mutex_handle != nullptr;
	}

	void destroy_mutex()
	{
		if (g_mutex_handle)
		{
			CloseHandle(g_mutex_handle);
			g_mutex_handle = nullptr;
		}
	}

	bool create_shared_memory(const char* name, std::size_t size, void** out_ptr)
	{
		g_mmap_handle = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
		                                   PAGE_READWRITE, 0,
		                                   static_cast<DWORD>(size), name);
		if (!g_mmap_handle)
			return false;

		g_mapped_view = MapViewOfFile(g_mmap_handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if (!g_mapped_view)
		{
			CloseHandle(g_mmap_handle);
			g_mmap_handle = nullptr;
			return false;
		}

		*out_ptr = g_mapped_view;
		return true;
	}

	void destroy_shared_memory(std::size_t /*size*/)
	{
		if (g_mapped_view)
		{
			UnmapViewOfFile(g_mapped_view);
			g_mapped_view = nullptr;
		}
		if (g_mmap_handle)
		{
			CloseHandle(g_mmap_handle);
			g_mmap_handle = nullptr;
		}
	}

	bool lock(uint32_t timeout_ms)
	{
		if (!g_mutex_handle)
			return false;
		return WaitForSingleObject(g_mutex_handle, timeout_ms) == WAIT_OBJECT_0;
	}

	void unlock()
	{
		if (g_mutex_handle)
			ReleaseMutex(g_mutex_handle);
	}
}

#endif	// _WIN32
