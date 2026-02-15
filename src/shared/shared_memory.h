// ============================================================================
//  Shared Memory IPC — Proxy (in-process) <-> External Tool
//
//  The version.dll proxy runs INSIDE the game process and has full memory
//  access. tvk.sys strips PROCESS_VM_READ from external handles via
//  ObRegisterCallbacks, but code running in-process can read freely.
//
//  Two channels:
//    1. SharedGameInfo — proxy writes game base, PID, and key pointers
//    2. MemoryRelay — external tool requests reads, proxy fulfills them
//
//  Named section: "Local\\DaDResearchShm"
// ============================================================================
#pragma once

#include <cstdint>

// Shared memory section name — must match on both sides
#define DAD_SHM_NAME "Local\\DaDResearchShm"

// Magic value to verify shared memory is initialized
#define DAD_SHM_MAGIC 0x44414452  // "DADR"

// Protocol version — bump on layout changes
#define DAD_SHM_VERSION 1

// Memory relay: max bytes per read request
#define DAD_RELAY_MAX_SIZE 0x4000  // 16 KB per request (enough for actor arrays)

// ============================================================================
//  Memory relay request/response
//  External tool writes a request, proxy fulfills it in-place
// ============================================================================
#pragma pack(push, 1)

struct MemoryRelaySlot {
    volatile uint32_t state;       // 0=idle, 1=request pending, 2=response ready, 3=error
    uint64_t address;              // Address to read from (in game process)
    uint32_t size;                 // Bytes to read (max DAD_RELAY_MAX_SIZE)
    uint8_t  data[DAD_RELAY_MAX_SIZE]; // Response data written by proxy
};

// Multiple relay slots for concurrent reads (reduces round-trip latency)
#define DAD_RELAY_SLOTS 4

struct SharedGameData {
    // ── Header ──
    uint32_t magic;                // Must be DAD_SHM_MAGIC
    uint32_t version;              // Must be DAD_SHM_VERSION
    uint32_t pid;                  // Game process PID
    volatile uint32_t heartbeat;   // Proxy increments this each tick (detect stale)

    // ── Module info (set once at startup) ──
    uint64_t gameBase;             // Base address of main game executable module
    uint64_t gameSize;             // Size of main game executable module (SizeOfImage)

    // ── Memory relay slots ──
    MemoryRelaySlot relay[DAD_RELAY_SLOTS];
};

#pragma pack(pop)

// Total shared memory size
#define DAD_SHM_SIZE sizeof(SharedGameData)

// Relay slot states
#define DAD_RELAY_IDLE     0
#define DAD_RELAY_REQUEST  1
#define DAD_RELAY_READY    2
#define DAD_RELAY_ERROR    3
