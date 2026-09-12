// K2 High FPS Fix - d3m0
// Windows x86 GOG Aspyr: SHA-256 777BEE235A9E8BDD9863F6741BC3AC54BB6A113B62B1D2E4D12BBE6DB963A914
// Algorithm reference: K1HighFPSFixes 1.0.0, commit 422a367e.
// This module does not change the engine's global delta or impose an FPS cap.
#if !defined(__i386__) || !defined(__clang__)
#error Build with Clang targeting i686-w64-windows-gnu.
#endif
using u32 = unsigned int;
using i32 = int;
static_assert(sizeof(void*) == 4 && sizeof(u32) == 4);
static_assert(__LDBL_MANT_DIG__ == 64);

// Both inspected KPM 0.6.3 and 0.7.0 wrappers put this record in EBX.
// The saved ESP is NOT the original game ESP in 0.7.0. All stack accesses
// below deliberately use the game's preserved EBP instead.
struct SavedRegisters {
    u32 eflags, edi, esi, ebp, esp, ebx, edx, ecx, eax;
};
static_assert(sizeof(SavedRegisters) == 36);
static_assert(__builtin_offsetof(SavedRegisters, ebp) == 12);
static_assert(__builtin_offsetof(SavedRegisters, eax) == 32);

template<class T>
__attribute__((always_inline)) static inline volatile T& mem(u32 address) {
    return *reinterpret_cast<volatile T*>(address);
}
static constexpr u32 frameAddress = 0x00A14C64;
static constexpr u32 deltaAddress = 0x009F6C40;
static constexpr float referenceDelta = 0.01666666753590106964111328125f;

static inline bool positiveFinite(float value) {
    // Integer inspection avoids NaN comparisons and does not depend on MXCSR.
    union { float f; u32 u; } v = {value};
    return v.u > 0 && v.u < 0x7F800000u;
}

namespace Letterbox {
struct State {
    u32 owner;
    float openingRemainder;
    float closingRemainder;
};
// GUI instances are few; no game object padding, heap allocation or dangling
// references are used. Begin-transition hooks reset an address reused by a GUI.
static State states[32] = {};
static u32 nextVictim = 0;

static State& stateFor(u32 owner) {
    for (u32 i = 0; i < 32; ++i) {
        if (states[i].owner == owner) return states[i];
    }
    for (u32 i = 0; i < 32; ++i) {
        if (states[i].owner == 0) {
            states[i].owner = owner;
            states[i].openingRemainder = 0;
            states[i].closingRemainder = 0;
            return states[i];
        }
    }
    State& state = states[nextVictim];
    nextVictim = (nextVictim + 1) & 31u;
    state.owner = owner;
    state.openingRemainder = 0;
    state.closingRemainder = 0;
    return state;
}

static void step(SavedRegisters* saved, bool closing) {
    const u32 owner = mem<u32>(saved->ebp - 4);
    const u32 deltaPointer = mem<u32>(saved->ebp + 8);
    const float dt = mem<float>(deltaPointer);
    if (!positiveFinite(dt)) {
        saved->eax = 0;
        return; // A paused/invalid frame must not discard pending motion.
    }
    const i32 extent = closing
        ? mem<i32>(owner + 0x6C) - mem<i32>(owner + 0x7C)
        : mem<i32>(owner + 0x74) - mem<i32>(owner + 0x84);
    if (dt >= referenceDelta) {
        // Preserve native <=60-FPS rounding. Most importantly, do not clear
        // the high-FPS remainder when moving back through the threshold.
        const long double distance = static_cast<long double>(extent) * 2.0L * dt;
        saved->eax = static_cast<u32>(static_cast<i32>(distance));
        return;
    }
    State& state = stateFor(owner);
    float& remainder = closing ? state.closingRemainder : state.openingRemainder;
    // Native 60-FPS integer pixel step, then fractional carry at higher FPS.
    // Signed integer division deliberately rounds toward zero, as in K1.
    const i32 referenceStep = extent / 30;
    volatile float total = static_cast<float>(
        static_cast<long double>(referenceStep) * dt * 60.0L + remainder);
    const i32 whole = static_cast<i32>(total);
    remainder = static_cast<float>(static_cast<long double>(total) - whole);
    saved->eax = static_cast<u32>(whole);
}
}

extern "C" __attribute__((noinline)) void ResetLetterboxState(SavedRegisters* saved) {
    Letterbox::State& state = Letterbox::stateFor(saved->ecx);
    state.openingRemainder = 0;
    state.closingRemainder = 0;
}
extern "C" __attribute__((noinline)) void OpeningStep(SavedRegisters* saved) {
    Letterbox::step(saved, false);
}
extern "C" __attribute__((noinline)) void ClosingStep(SavedRegisters* saved) {
    Letterbox::step(saved, true);
}

namespace Jitter {
static u32 lastFrame = 0;
static bool initialized = false;
static bool allow = true;
// One initial tick matches the current K1 helper's startup phase. There is
// no 60-FPS mode switch, so 59/61-FPS changes cannot reset this accumulator.
static double pendingTicks = 1.0;
static void update() {
    const u32 frame = mem<u32>(frameAddress);
    if (initialized && frame == lastFrame) return;
    initialized = true;
    lastFrame = frame;
    const float dt = mem<float>(deltaAddress);
    allow = false;
    if (!positiveFinite(dt)) return;
    pendingTicks += static_cast<double>(dt) * 60.0;
    if (pendingTicks >= 1.0) {
        pendingTicks -= 1.0;
        if (pendingTicks > 1.0) pendingTicks = 1.0;
        allow = true;
    }
}
}
extern "C" __attribute__((noinline)) void JitterGate(SavedRegisters* saved) {
    Jitter::update();
    const u32 model = mem<u32>(saved->ebp - 8);
    if (!Jitter::allow && mem<u32>(model + 0x148) != 0) {
        // Gate only the random evolution, not the vertex loop: the separate
        // U/V replacements keep the base UV coordinates advancing every frame.
        // This retains elapsed scrolling time for mixed scrolling/jitter assets.
        mem<i32>(saved->ebp - 0x20) = 0;
    }
}

namespace Water {
static u32 lastFrame = 0;
static bool initialized = false;
static bool highFPS = false;
static bool allow = true;
static volatile float pendingSeconds = 0;
static constexpr float threshold = 0.015625f; // 64 FPS, matching current K1.
static void update() {
    const u32 frame = mem<u32>(frameAddress);
    if (initialized && frame == lastFrame) return;
    initialized = true;
    lastFrame = frame;
    const float raw = mem<float>(deltaAddress);
    const float dt = positiveFinite(raw) ? raw : 0.0f;
    if (dt >= threshold) {
        highFPS = false;
        allow = true;
        // Native frames account for their own delta. Retain, rather than
        // zeroing or replaying, the unfinished high-FPS virtual frame.
        return;
    }
    highFPS = true;
    pendingSeconds = dt + pendingSeconds;
    allow = false;
    if (pendingSeconds >= referenceDelta) {
        pendingSeconds = pendingSeconds - referenceDelta;
        if (pendingSeconds > referenceDelta) pendingSeconds = referenceDelta;
        allow = true;
    }
}
}
extern "C" __attribute__((noinline)) void WaterGate(SavedRegisters* saved) {
    Water::update();
    if (Water::highFPS && !Water::allow) {
        const u32 controller = mem<u32>(saved->ebp - 0xC8);
        mem<u32>(controller + 8) = mem<u32>(frameAddress);
    }
}
extern "C" __attribute__((noinline)) void WaterDelta(SavedRegisters* saved) {
    if (Water::highFPS) {
        // Replace the LOCAL unscaled delta only. Native K2 code then applies
        // its own scale and controller speed, in its original x87 order.
        mem<float>(saved->ebp - 0xCC) = referenceDelta;
    }
}

// Preserve x87/XMM/MXCSR explicitly, including live x87 stack operands. KPM
// 0.7.0 also saves these, but 0.6.3 does not. FNINIT supplies an empty x87 stack
// for our C++ without damaging the game's pending operands or control word.
// EBX remains the KPM record pointer throughout each call.
#define EXPORT_HOOK(exportName, handler) \
extern "C" __declspec(dllexport) __attribute__((naked)) void exportName() { \
    __asm__ volatile( \
        "pushl %ebp\n\t" \
        "movl %esp, %ebp\n\t" \
        "andl $-16, %esp\n\t" \
        "subl $512, %esp\n\t" \
        "fxsave (%esp)\n\t" \
        "fninit\n\t" \
        "subl $12, %esp\n\t" \
        "pushl %ebx\n\t" \
        "calll _" #handler "\n\t" \
        "addl $16, %esp\n\t" \
        "fxrstor (%esp)\n\t" \
        "movl %ebp, %esp\n\t" \
        "popl %ebp\n\t" \
        "retl\n\t"); \
}
EXPORT_HOOK(ResetLetterbox, ResetLetterboxState)
EXPORT_HOOK(LetterboxOpeningStep, OpeningStep)
EXPORT_HOOK(LetterboxClosingStep, ClosingStep)
EXPORT_HOOK(GateTextureJitter, JitterGate)
EXPORT_HOOK(GateWaterControllerFrame, WaterGate)
EXPORT_HOOK(FixWaterVirtualFrameDelta, WaterDelta)
