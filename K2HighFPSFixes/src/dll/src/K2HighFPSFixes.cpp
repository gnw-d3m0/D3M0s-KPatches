#if !defined(__i386__) || !defined(__clang__)
#error Build with Clang targeting i686-w64-windows-gnu.
#endif
using u32 = unsigned int;
using i32 = int;
static_assert(sizeof(void*) == 4 && sizeof(u32) == 4);
static_assert(__LDBL_MANT_DIG__ == 64);

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
    union { float f; u32 u; } v = {value};
    return v.u > 0 && v.u < 0x7F800000u;
}

namespace Letterbox {
struct State {
    u32 owner;
    float openingRemainder;
    float closingRemainder;
};
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
        return;
    }
    const i32 extent = closing
        ? mem<i32>(owner + 0x6C) - mem<i32>(owner + 0x7C)
        : mem<i32>(owner + 0x74) - mem<i32>(owner + 0x84);
    if (dt >= referenceDelta) {
        const long double distance = static_cast<long double>(extent) * 2.0L * dt;
        saved->eax = static_cast<u32>(static_cast<i32>(distance));
        return;
    }
    State& state = stateFor(owner);
    float& remainder = closing ? state.closingRemainder : state.openingRemainder;
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
        mem<i32>(saved->ebp - 0x20) = 0;
    }
}

namespace Water {
static u32 lastFrame = 0;
static bool initialized = false;
static bool highFPS = false;
static bool allow = true;
static volatile float pendingSeconds = 0;
static constexpr float threshold = 0.015625f;
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
        mem<float>(saved->ebp - 0xCC) = referenceDelta;
    }
}

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

namespace Additional {

static long double remainder(long double value, long double divisor) {
    long double result;
    __asm__ volatile(
        "fldt %[d]\n\tfldt %[v]\n\t"
        "1: fprem\n\tfnstsw %%ax\n\ttestb $4, %%ah\n\tjnz 1b\n\t"
        "fstpt %[r]\n\tfstp %%st(0)\n\t"
        : [r] "=m" (result)
        : [v] "m" (value), [d] "m" (divisor)
        : "ax", "cc", "st", "st(1)");
    return result;
}
static long double positive(float value) {
    return positiveFinite(value) ? static_cast<long double>(value) : 0.0L;
}
static float storeRemainder(long double seconds, long double interval) {
    float value = static_cast<float>(seconds);
    if (static_cast<long double>(value) >= interval && value > 0) {
        union { float f; u32 u; } bits = {value};
        --bits.u;
        value = bits.f;
    }
    return value;
}

struct ParticleState { u32 owner; double fraction; };
static ParticleState particleStates[8192] = {};
static u32 particleVictim = 0;
static u32 particleHash(u32 owner) {
    u32 hash = owner >> 4;
    hash ^= hash >> 11;
    hash *= 0x9E3779B1u;
    return hash >> 19;
}
static ParticleState& particleState(u32 owner) {
    const u32 first = particleHash(owner);
    for (u32 n = 0; n < 8192; ++n) {
        ParticleState& state = particleStates[(first + n) & 8191u];
        if (state.owner == owner) return state;
        if (state.owner == 0) {
            state.owner = owner;
            state.fraction = 0;
            return state;
        }
    }
    ParticleState& state = particleStates[particleVictim];
    particleVictim = (particleVictim + 1) & 8191u;
    state.owner = owner;
    state.fraction = 0;
    return state;
}
extern "C" __attribute__((noinline)) void ResetParticleFromEax(SavedRegisters* saved) {
    const u32 owner = saved->eax;
    if (owner == 0) return;
    const u32 first = particleHash(owner);
    for (u32 n = 0; n < 8192; ++n) {
        u32 hole = (first + n) & 8191u;
        if (particleStates[hole].owner == 0) return;
        if (particleStates[hole].owner != owner) continue;
        particleStates[hole].owner = 0;
        u32 scan = (hole + 1) & 8191u;
        for (u32 moved = 0; moved < 8191 && particleStates[scan].owner; ++moved) {
            const u32 home = particleHash(particleStates[scan].owner);
            if (((scan - home) & 8191u) >= ((scan - hole) & 8191u)) {
                particleStates[hole] = particleStates[scan];
                particleStates[scan].owner = 0;
                hole = scan;
            }
            scan = (scan + 1) & 8191u;
        }
        particleStates[hole].fraction = 0;
        return;
    }
}
extern "C" __attribute__((noinline)) void FountainAccumulate(SavedRegisters* saved) {
    const u32 owner = mem<u32>(saved->ebp - 0x1D8);
    const float dt = mem<float>(saved->ebp + 8);
    const float rate = mem<float>(saved->ebp - 0x2C);
    saved->eflags |= 4u;
    if (!positiveFinite(dt) || !positiveFinite(rate)) return;
    const long double total = positive(mem<float>(owner + 0xCC)) + dt;
    const float stored = static_cast<float>(total > 3.0e38L ? 3.0e38L : total);
    mem<float>(owner + 0xCC) = stored;
    if (static_cast<long double>(stored) * rate >= 1.0L)
        saved->eflags &= ~4u;
}
extern "C" __attribute__((noinline)) void FountainBudget(SavedRegisters* saved) {
    const u32 owner = mem<u32>(saved->ebp - 0x1D8);
    const long double seconds = positive(mem<float>(owner + 0xCC));
    const float rate = mem<float>(saved->ebp - 0x2C);
    const float baseRate = mem<float>(owner + 0x58);
    i32 budget = 0;
    if (positiveFinite(baseRate)) {
        const long double baseProgress = seconds * baseRate;
        const long double pending = remainder(baseProgress, 1.0L);
        const long double opportunities = baseProgress - pending;
        mem<float>(owner + 0xCC) = storeRemainder(pending / baseRate, 1.0L / baseRate);
        ParticleState& state = particleState(owner);
        if (positiveFinite(rate)) {
            const long double credit = opportunities * rate / baseRate + state.fraction;
            const long double fraction = remainder(credit, 1.0L);
            const long double whole = credit - fraction;
            state.fraction = static_cast<double>(fraction);
            const i32 limit = rate >= 4095.0f ? 4096 : static_cast<i32>(rate) + 1;
            budget = whole >= limit ? limit : static_cast<i32>(whole);
        }
    }
    mem<i32>(saved->ebp - 0x5C) = budget;
}

extern "C" __attribute__((noinline)) void CycleUpdate(SavedRegisters* saved) {
    const u32 owner = mem<u32>(saved->ebp - 4);
    const float dt = mem<float>(deltaAddress);
    const float fps = mem<float>(owner + 0x38);
    if (!positiveFinite(dt) || !positiveFinite(fps)) return;
    const long double seconds = positive(mem<float>(owner + 0x40)) + dt;
    const long double progress = seconds * fps;
    const long double fraction = remainder(progress, 1.0L);
    const long double whole = progress - fraction;
    if (whole < 1.0L) {
        mem<float>(owner + 0x40) = storeRemainder(seconds, 1.0L / fps);
        return;
    }
    const u32 texture = mem<u32>(owner + 4);
    if (texture == 0) return;
    const u32 table = mem<u32>(texture);
    if (table == 0) return;
    using Dimension = i32 (__attribute__((thiscall)) *)(u32);
    using SelectFrame = void (__attribute__((thiscall)) *)(u32, i32);
    const i32 width = reinterpret_cast<Dimension>(mem<u32>(table + 0x5C))(texture);
    const i32 height = reinterpret_cast<Dimension>(mem<u32>(table + 0x60))(texture);
    if (width <= 0 || height <= 0 || width > 0x7FFFFFFF / height) return;
    const u32 frames = static_cast<u32>(width * height);
    const i32 current = mem<i32>(owner + 0x3C);
    const u32 start = current < 0 ? 0u : static_cast<u32>(current) % frames;
    const u32 advance = static_cast<u32>(remainder(whole, frames));
    const u32 next = (start + advance) % frames;
    mem<float>(owner + 0x40) = storeRemainder(fraction / fps, 1.0L / fps);
    mem<u32>(owner + 0x3C) = next;
    reinterpret_cast<SelectFrame>(mem<u32>(table + 0xAC))(texture, next);
}

struct TimerState { u32 owner; double fraction; u32 expected; };
template<u32 N> struct TimerTable {
    TimerState states[N] = {};
    u32 next = 0;
    void reset(u32 owner) {
        for (u32 i = 0; i < N; ++i) if (states[i].owner == owner) {
            states[i].owner = 0;
            states[i].fraction = 0;
            states[i].expected = 0;
        }
    }
    TimerState& get(u32 owner) {
        for (u32 i = 0; i < N; ++i) if (states[i].owner == owner) return states[i];
        for (u32 i = 0; i < N; ++i) if (states[i].owner == 0) {
            states[i].owner = owner;
            states[i].fraction = 0;
            states[i].expected = 0;
            return states[i];
        }
        TimerState& state = states[next];
        next = (next + 1) % N;
        state.owner = owner;
        state.fraction = 0;
        state.expected = 0;
        return state;
    }
};
static TimerTable<64> shakeTimers;
static TimerTable<64> previewTimers;
static u32 milliseconds(TimerState& state, float dt) {
    if (!positiveFinite(dt)) return 0;
    const long double total = static_cast<long double>(dt) * 1000.0L + state.fraction;
    const long double fraction = remainder(total, 1.0L);
    state.fraction = static_cast<double>(fraction);
    const long double whole = total - fraction;
    return whole >= 1073741823.0L ? 0x3FFFFFFFu : static_cast<u32>(whole);
}
extern "C" __attribute__((noinline)) void ShakeMilliseconds(SavedRegisters* saved) {
    const u32 owner = mem<u32>(saved->ebp - 0x38);
    if (mem<i32>(owner + 0xDC) <= 0) {
        shakeTimers.reset(owner);
        saved->eax = 0;
        return;
    }
    saved->eax = milliseconds(shakeTimers.get(owner), mem<float>(saved->ebp + 8));
}
extern "C" __attribute__((noinline)) void ResetShakeFromEdx(SavedRegisters* saved) {
    shakeTimers.reset(saved->edx);
}
extern "C" __attribute__((noinline)) void ResetShakeFromEcx(SavedRegisters* saved) {
    shakeTimers.reset(saved->ecx);
}
extern "C" __attribute__((noinline)) void PreviewMilliseconds(SavedRegisters* saved) {
    const u32 owner = mem<u32>(saved->ebp - 0x38);
    TimerState& state = previewTimers.get(owner);
    const u32 elapsed = mem<u32>(owner + 0x3A8);
    if (elapsed != state.expected) state.fraction = 0;
    u32 amount = milliseconds(state, mem<float>(saved->ebp + 8));
    if (amount > 0xFFFFFFFFu - elapsed) amount = 0xFFFFFFFFu - elapsed;
    state.expected = elapsed + amount;
    saved->eax = amount;
}
extern "C" __attribute__((noinline)) void ResetPreviewFromEcx(SavedRegisters* saved) {
    previewTimers.reset(saved->ecx);
}

}

EXPORT_HOOK(AccumulateParticleTime, FountainAccumulate)
EXPORT_HOOK(CalculateParticleBudget, FountainBudget)
EXPORT_HOOK(ResetParticleFraction, ResetParticleFromEax)
EXPORT_HOOK(UpdateCyclingTexture, CycleUpdate)
EXPORT_HOOK(FixShakeMilliseconds, ShakeMilliseconds)
EXPORT_HOOK(ResetShakeTimer, ResetShakeFromEdx)
EXPORT_HOOK(ResetShakeTimerOnCreate, ResetShakeFromEcx)
EXPORT_HOOK(FixPreviewMilliseconds, PreviewMilliseconds)
EXPORT_HOOK(ResetPreviewTimer, ResetPreviewFromEcx)
