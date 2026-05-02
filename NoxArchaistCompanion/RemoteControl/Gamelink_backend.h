#pragma once

// Private platform backend interface for Gamelink. Each platform provides one
// translation unit that implements every function below; only one is compiled
// into the binary.

#include <cstddef>
#include <cstdint>

namespace GameLink::backend
{
	bool create_mutex(const char* name);
	void destroy_mutex();

	bool create_shared_memory(const char* name, std::size_t size, void** out_ptr);
	void destroy_shared_memory(std::size_t size);

	bool lock(uint32_t timeout_ms);
	void unlock();
}
