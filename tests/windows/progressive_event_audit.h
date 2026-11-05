// Process-local diagnostics for a separately linked Classic test executable.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>

constexpr uint32_t EventAuditSlots = 65536, EventAuditModules = 4096;
struct EventAuditEntry {
    uint64_t handle, caller, sequence;
    uint32_t thread, kind, moduleSequence, reserved;
};
struct EventAuditModule {
    uint64_t base, size;
    uint32_t sequence, reason, ready, reserved;
    wchar_t name[80];
};
struct EventAuditData {
    uint32_t magic, version, bytes, entryBytes, entryCapacity, moduleBytes, moduleCapacity;
    volatile LONG enabled;
    volatile LONG64 recorded, closed, collisions, unidentifiedDuplicates, moduleOverflow;
    volatile LONG moduleUsed, error;
    EventAuditEntry entries[EventAuditSlots];
    EventAuditModule modules[EventAuditModules];
};
static_assert(sizeof(void*) == 8 && sizeof(EventAuditEntry) == 40);
static_assert(sizeof(EventAuditModule) == 192 && offsetof(EventAuditData, entries) == 80);
extern "C" __declspec(dllexport) EventAuditData FoxicMPEventAuditData;

// Optional first-eight DWM creation stacks, separate from the version-1 buffer.
// No stack walking occurs unless FOXICMP_EVENT_AUDIT_STACKS=1 is also set.
struct EventAuditStack {
    uint64_t handle, frames[24];
    uint32_t thread, count, moduleSequence;
    volatile LONG ready;
};
struct EventAuditStacks {
    uint32_t magic, version, bytes, rowBytes, capacity;
    volatile LONG used, enabled;
    uint32_t reserved;
    EventAuditStack rows[8];
};
static_assert(sizeof(EventAuditStack)==216 && offsetof(EventAuditStacks,rows)==32);
extern "C" __declspec(dllexport) EventAuditStacks FoxicMPEventAuditStacks;

struct EventAuditHook {
    uint64_t hook, caller, callback, threadCreated;
    uint32_t thread, eventMin, eventMax, process, targetThread, flags, kind, result;
    volatile LONG ready;
    uint32_t reserved;
};
struct EventAuditHooks {
    uint32_t magic, version, bytes, rowBytes, capacity;
    volatile LONG used;
    uint32_t reserved[2];
    EventAuditHook rows[32];
};
static_assert(sizeof(EventAuditHook)==72 && offsetof(EventAuditHooks,rows)==32);
extern "C" __declspec(dllexport) EventAuditHooks FoxicMPEventAuditHooks;
