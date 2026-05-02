#if !defined(_WIN32) && !defined(__APPLE__)

// Linux implementation. Grid Cartographer's protocol uses ASCII names; the
// Linux build maps them to "/<name>" for shm_open / sem_open per POSIX.

#include "RemoteControl/Gamelink_backend.h"

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <string>

namespace
{
	std::string g_shm_name;
	std::string g_sem_name;

	int g_shm_fd = -1;
	void* g_mapped_view = nullptr;
	std::size_t g_mapped_size = 0;

	sem_t* g_sem = SEM_FAILED;

	std::string posix_name(const char* name)
	{
		std::string s = "/";
		s += name;
		return s;
	}
}

namespace GameLink::backend
{
	bool create_mutex(const char* name)
	{
		if (g_sem != SEM_FAILED)
			return true;

		g_sem_name = posix_name(name);

		// Refuse to start if another instance owns the semaphore. O_EXCL means
		// "fail if it already exists"; if so, peek with O_CREAT-less open and
		// release it so we don't leak an instance from a crashed prior run.
		g_sem = sem_open(g_sem_name.c_str(), O_CREAT | O_EXCL, 0600, 1);
		if (g_sem == SEM_FAILED && errno == EEXIST)
		{
			sem_t* existing = sem_open(g_sem_name.c_str(), 0);
			if (existing != SEM_FAILED)
			{
				sem_close(existing);
				return false;
			}
		}
		return g_sem != SEM_FAILED;
	}

	void destroy_mutex()
	{
		if (g_sem != SEM_FAILED)
		{
			sem_close(g_sem);
			sem_unlink(g_sem_name.c_str());
			g_sem = SEM_FAILED;
			g_sem_name.clear();
		}
	}

	bool create_shared_memory(const char* name, std::size_t size, void** out_ptr)
	{
		g_shm_name = posix_name(name);
		g_shm_fd = shm_open(g_shm_name.c_str(), O_CREAT | O_RDWR, 0600);
		if (g_shm_fd < 0)
			return false;

		if (ftruncate(g_shm_fd, static_cast<off_t>(size)) != 0)
		{
			close(g_shm_fd);
			shm_unlink(g_shm_name.c_str());
			g_shm_fd = -1;
			return false;
		}

		g_mapped_view = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
		if (g_mapped_view == MAP_FAILED)
		{
			close(g_shm_fd);
			shm_unlink(g_shm_name.c_str());
			g_shm_fd = -1;
			g_mapped_view = nullptr;
			return false;
		}

		g_mapped_size = size;
		*out_ptr = g_mapped_view;
		return true;
	}

	void destroy_shared_memory(std::size_t size)
	{
		if (g_mapped_view)
		{
			munmap(g_mapped_view, size ? size : g_mapped_size);
			g_mapped_view = nullptr;
			g_mapped_size = 0;
		}
		if (g_shm_fd >= 0)
		{
			close(g_shm_fd);
			shm_unlink(g_shm_name.c_str());
			g_shm_fd = -1;
		}
		g_shm_name.clear();
	}

	bool lock(uint32_t timeout_ms)
	{
		if (g_sem == SEM_FAILED)
			return false;

		timespec ts{};
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec  += timeout_ms / 1000;
		ts.tv_nsec += (timeout_ms % 1000) * 1000000;
		if (ts.tv_nsec >= 1000000000)
		{
			ts.tv_sec  += 1;
			ts.tv_nsec -= 1000000000;
		}

		while (sem_timedwait(g_sem, &ts) != 0)
		{
			if (errno == EINTR)
				continue;
			return false;
		}
		return true;
	}

	void unlock()
	{
		if (g_sem != SEM_FAILED)
			sem_post(g_sem);
	}
}

#endif
