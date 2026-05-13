// Gamelink protocol layer.
//
// Gamelink is the shared-memory bridge between an emulator and Grid
// Cartographer (or another companion app conforming to the same protocol).
// The protocol defines a single byte-identical struct (sSharedMemoryMap_R4)
// laid out on top of an OS shared-memory region, plus a named mutex that
// serialises read/write access. NAC plays the server role: it allocates the
// region, fills the frame/audio/peek fields each tick, and reads input/
// terminal commands written by GC.
//
// This file is platform-neutral. The OS-specific shared-memory and mutex
// primitives live in Gamelink_win32.cpp / Gamelink_posix.cpp behind the
// GameLink::backend interface declared in Gamelink_backend.h.

#include "RemoteControl/Gamelink.h"
#include "RemoteControl/Gamelink_backend.h"

#include <algorithm>
#include <cstring>

// Names must masquerade as AppleWin so Grid Cartographer recognises us
#define SYSTEM_NAME				"AppleWin"
#define PROTOCOL_VER			4
#define GAMELINK_MUTEX_NAME		"DWD_GAMELINK_MUTEX_R4"
#define GAMELINK_MMAP_NAME		"DWD_GAMELINK_MMAP_R4"

using namespace GameLink;

namespace
{
	bool g_bEnableGamelink = true;
	bool g_bEnableTrackOnly = false;
	bool g_bPaused = false;
	bool g_use_native_format = false;

	uint32_t g_membase_size = 0;

	GameLink::sSharedMemoryMap_R4* g_p_shared_memory = nullptr;
	GameLink::sSharedMMapBuffer_R1* g_p_outbuf = nullptr;

	RemoteCommandHandler g_remote_command_handler = nullptr;

	constexpr std::size_t MEMORY_MAP_CORE_SIZE = sizeof(GameLink::sSharedMemoryMap_R4);

	void shared_memory_init()
	{
		g_p_shared_memory->version = PROTOCOL_VER;
		g_p_shared_memory->flags = 0;

		std::memset(g_p_shared_memory->system, 0, sizeof(g_p_shared_memory->system));
		std::strncpy(g_p_shared_memory->system, SYSTEM_NAME, sizeof(g_p_shared_memory->system) - 1);
		std::memset(g_p_shared_memory->program, 0, sizeof(g_p_shared_memory->program));

		for (int i = 0; i < 4; ++i)
			g_p_shared_memory->program_hash[i] = 0;

		g_p_shared_memory->input.mouse_dx = 0;
		g_p_shared_memory->input.mouse_dy = 0;
		g_p_shared_memory->input.mouse_btn = 0;
		for (int i = 0; i < 8; ++i)
			g_p_shared_memory->input.keyb_state[i] = 0;

		g_p_shared_memory->peek.addr_count = 0;
		std::memset(g_p_shared_memory->peek.addr, 0, sizeof(g_p_shared_memory->peek.addr));
		std::memset(g_p_shared_memory->peek.data, 0, sizeof(g_p_shared_memory->peek.data));

		g_p_shared_memory->frame.seq = 0;
		g_p_shared_memory->frame.image_fmt = 0;
		g_p_shared_memory->frame.width = 0;
		g_p_shared_memory->frame.height = 0;
		g_p_shared_memory->frame.par_x = 1;
		g_p_shared_memory->frame.par_y = 1;
		std::memset(g_p_shared_memory->frame.buffer, 0, sSharedMMapFrame_R1::MAX_PAYLOAD);

		g_p_shared_memory->audio.master_vol_l = 100;
		g_p_shared_memory->audio.master_vol_r = 100;

		g_p_shared_memory->ram_size = g_membase_size;
	}

	void proc_mech(GameLink::sSharedMMapBuffer_R1* cmd, uint16_t payload)
	{
		if (payload <= 1 || payload > 128)
			return;

		cmd->payload = 0;
		char* com = reinterpret_cast<char*>(cmd->data);
		com[payload] = 0;

		if (g_remote_command_handler == nullptr)
			return;

		if (std::strcmp(com, ":reset") == 0)
			g_remote_command_handler(RemoteCommand::Reset);
		else if (std::strcmp(com, ":pause") == 0)
			g_remote_command_handler(RemoteCommand::Pause);
		else if (std::strcmp(com, ":shutdown") == 0)
			g_remote_command_handler(RemoteCommand::Shutdown);
	}
}

void GameLink::SetRemoteCommandHandler(RemoteCommandHandler handler)
{
	g_remote_command_handler = handler;
}

bool GameLink::GetGameLinkEnabled()			{ return g_bEnableGamelink; }
void GameLink::SetGameLinkEnabled(bool e)	{ g_bEnableGamelink = e; }
bool GameLink::GetTrackOnlyEnabled()		{ return g_bEnableTrackOnly; }
void GameLink::SetTrackOnlyEnabled(bool e)	{ g_bEnableTrackOnly = e; }
void GameLink::SetPaused(bool paused)		{ g_bPaused = paused; }
bool GameLink::GetVideoNativeFormat()		{ return g_use_native_format; }

int GameLink::Init(bool trackonly_mode)
{
	g_bEnableTrackOnly = trackonly_mode;
	return backend::create_mutex(GAMELINK_MUTEX_NAME) ? 1 : 0;
}

uint8_t* GameLink::AllocRAM(uint32_t size)
{
	g_membase_size = size;

	const std::size_t memory_map_size = MEMORY_MAP_CORE_SIZE + g_membase_size;
	void* p = nullptr;
	if (!backend::create_shared_memory(GAMELINK_MMAP_NAME, memory_map_size, &p))
	{
		backend::destroy_mutex();
		return nullptr;
	}

	g_p_shared_memory = reinterpret_cast<GameLink::sSharedMemoryMap_R4*>(p);
	shared_memory_init();
	GameLink::InitTerminal();

	return reinterpret_cast<uint8_t*>(g_p_shared_memory) + MEMORY_MAP_CORE_SIZE;
}

void GameLink::Term()
{
	// Tell client we're going away (ignore failures).
	if (g_p_shared_memory)
		g_p_shared_memory->version = 0;

	backend::destroy_shared_memory(MEMORY_MAP_CORE_SIZE + g_membase_size);
	backend::destroy_mutex();

	g_p_shared_memory = nullptr;
	g_membase_size = 0;
}

// Grid Cartographer keys programs by an array of 4 uint32_ts. AppleWin's
// "page-crc" form is broken up by the caller and passed straight through.
void GameLink::SetProgramInfo(const std::string name, uint32_t i1, uint32_t i2, uint32_t i3, uint32_t i4)
{
	if (!g_p_shared_memory)
		return;

	const std::size_t maxlen = sizeof(g_p_shared_memory->program) - 1;
	const std::string truncated = name.substr(0, maxlen);
	std::memset(g_p_shared_memory->program, 0, sizeof(g_p_shared_memory->program));
	std::memcpy(g_p_shared_memory->program, truncated.data(), truncated.size());

	g_p_shared_memory->program_hash[0] = i1;
	g_p_shared_memory->program_hash[1] = i2;
	g_p_shared_memory->program_hash[2] = i3;
	g_p_shared_memory->program_hash[3] = i4;
}

int GameLink::In(GameLink::sSharedMMapInput_R2* p_input,
                 GameLink::sSharedMMapAudio_R1* p_audio)
{
	if (!g_p_shared_memory)
		return 0;

	if (g_bEnableTrackOnly)
	{
		std::memset(p_input, 0, sizeof(sSharedMMapInput_R2));
		return 0;
	}

	int ready = 0;
	if (g_p_shared_memory->input.ready)
	{
		std::memcpy(p_input, &g_p_shared_memory->input, sizeof(sSharedMMapInput_R2));
		// Clear remote delta to prevent double-counting.
		g_p_shared_memory->input.mouse_dx = 0;
		g_p_shared_memory->input.mouse_dy = 0;
		g_p_shared_memory->input.ready = 0;
		ready = 1;
	}

	if (g_p_shared_memory->audio.master_vol_l <= 100)
		p_audio->master_vol_l = g_p_shared_memory->audio.master_vol_l;
	if (g_p_shared_memory->audio.master_vol_r <= 100)
		p_audio->master_vol_r = g_p_shared_memory->audio.master_vol_r;

	return ready;
}

void GameLink::Out(const uint8_t* p_sysmem)
{
	Out(0, 0, 1.0, false, nullptr, p_sysmem);
}

void GameLink::Out(uint16_t frame_width,
                   uint16_t frame_height,
                   double source_ratio,
                   bool want_mouse,
                   const uint8_t* p_frame,
                   const uint8_t* p_sysmem)
{
	if (!g_p_shared_memory)
		return;

	uint16_t par_x, par_y;
	if (source_ratio >= 1.0)
	{
		par_x = 4096;
		par_y = static_cast<uint16_t>(source_ratio * 4096.0);
	}
	else
	{
		par_x = static_cast<uint16_t>(4096.0 / source_ratio);
		par_y = 4096;
	}

	uint8_t flags;
	if (g_bEnableTrackOnly)
	{
		flags = sSharedMemoryMap_R4::FLAG_NO_FRAME;
	}
	else
	{
		flags = sSharedMemoryMap_R4::FLAG_WANT_KEYB;
		if (want_mouse)
			flags |= sSharedMemoryMap_R4::FLAG_WANT_MOUSE;
	}

	if (g_bPaused)
		flags |= sSharedMemoryMap_R4::FLAG_PAUSED;

	sSharedMMapBuffer_R1 proc_mech_buffer;
	proc_mech_buffer.payload = 0;

	if (!backend::lock(3000))
		return;

	g_p_shared_memory->version = PROTOCOL_VER;
	g_p_shared_memory->flags = flags;

	if (!g_bEnableTrackOnly && p_frame)
	{
		++g_p_shared_memory->frame.seq;

		g_p_shared_memory->frame.image_fmt = 1;	// 32-bit RGBA
		g_p_shared_memory->frame.width = frame_width;
		g_p_shared_memory->frame.height = frame_height;
		g_p_shared_memory->frame.par_x = par_x;
		g_p_shared_memory->frame.par_y = par_y;

		if (frame_width <= sSharedMMapFrame_R1::MAX_WIDTH &&
		    frame_height <= sSharedMMapFrame_R1::MAX_HEIGHT)
		{
			const uint32_t payload = std::min(uint32_t(frame_width) * uint32_t(frame_height) * 4u,
				uint32_t(sSharedMMapFrame_R1::MAX_PAYLOAD));
			std::memcpy(g_p_shared_memory->frame.buffer, p_frame, payload);
		}
	}

	UpdatePeekInfo(&g_p_shared_memory->peek, p_sysmem);
	ExecTerminal(&g_p_shared_memory->buf_recv,
	             &g_p_shared_memory->buf_tohost,
	             &proc_mech_buffer);

	backend::unlock();

	// Mechanical commands run outside the lock so the handler is free to
	// re-enter Gamelink (e.g. to toggle pause).
	if (proc_mech_buffer.payload)
		ExecTerminalMech(&proc_mech_buffer);
}

void GameLink::UpdatePeekInfo(sSharedMMapPeek_R2* peek, const uint8_t* p_sysmem)
{
	const uint32_t count = std::min<uint32_t>(peek->addr_count, sSharedMMapPeek_R2::PEEK_LIMIT);
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t address = peek->addr[i];
		peek->data[i] = (address < g_membase_size) ? p_sysmem[address] : uint8_t(0);
	}
}

void GameLink::InitTerminal()
{
	g_p_outbuf = nullptr;
}

void GameLink::ExecTerminalMech(GameLink::sSharedMMapBuffer_R1* p_procbuf)
{
	proc_mech(p_procbuf, p_procbuf->payload);
}

void GameLink::ExecTerminal(GameLink::sSharedMMapBuffer_R1* p_inbuf,
                            GameLink::sSharedMMapBuffer_R1* p_outbuf,
                            GameLink::sSharedMMapBuffer_R1* p_procbuf)
{
	if (p_inbuf->payload == 0)
		return;
	if (p_outbuf->payload > 0)
		return;

	g_p_outbuf = p_outbuf;

	if (p_inbuf->data[0] == ':')
	{
		const uint16_t payload = p_inbuf->payload;
		p_inbuf->payload = 0;

		if (payload > sSharedMMapBuffer_R1::BUFFER_SIZE)
			return;
		std::memcpy(p_procbuf->data, p_inbuf->data, payload);
		p_procbuf->payload = payload;
	}
}
