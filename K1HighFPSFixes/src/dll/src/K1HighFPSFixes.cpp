#if !defined(__i386__) || !defined(__clang__)
#error Build with Clang targeting i686-w64-windows-gnu.
#endif

using u8 = unsigned char;
using u32 = unsigned int;
using i32 = int;

static_assert(sizeof(void*) == 4);
static_assert(sizeof(u32) == 4);
static_assert(sizeof(i32) == 4);
static_assert(__LDBL_MANT_DIG__ == 64);

struct SavedRegisters {
    u32 eflags;
    u32 edi;
    u32 esi;
    u32 ebp;
    u32 esp;
    u32 ebx;
    u32 edx;
    u32 ecx;
    u32 eax;
};

static_assert(sizeof(SavedRegisters) == 36);
static_assert(__builtin_offsetof(SavedRegisters, edi) == 0x04);
static_assert(__builtin_offsetof(SavedRegisters, esp) == 0x10);
static_assert(__builtin_offsetof(SavedRegisters, ecx) == 0x1C);
static_assert(__builtin_offsetof(SavedRegisters, eax) == 0x20);

template<typename T>
__attribute__((always_inline)) static inline volatile T& memory(u32 address) {
    return *reinterpret_cast<volatile T*>(address);
}

namespace AnimationFix {

static u32 accumulatedTicks = 50000;
static u8 allowJitter = 1;

extern "C" __attribute__((noinline)) void AnimationUpdate(SavedRegisters* saved) {
    const u32 elapsedTicks = saved->eax;
    if (static_cast<i32>(elapsedTicks) <= 0) {
        allowJitter = 0;
        return;
    }

    accumulatedTicks += elapsedTicks * 3u;
    allowJitter = 0;
    if (static_cast<i32>(accumulatedTicks) >= 50000) {
        allowJitter = 1;
        accumulatedTicks -= 50000u;
        if (static_cast<i32>(accumulatedTicks) > 50000) {
            accumulatedTicks = 50000;
        }
    }
}

static void Gate(SavedRegisters* saved, u32 stackOffset) {
    if (allowJitter == 0 && memory<u32>(saved->edi + 0x148) != 0) {
        memory<u32>(saved->esp + stackOffset) = 0;
    }
}

extern "C" __attribute__((noinline)) void AnimationGate1(SavedRegisters* saved) {
    Gate(saved, 0x1C);
}

extern "C" __attribute__((noinline)) void AnimationGate2(SavedRegisters* saved) {
    Gate(saved, 0x20);
}

extern "C" __attribute__((noinline)) void AnimationGate3(SavedRegisters* saved) {
    Gate(saved, 0x20);
}

}

namespace WaterFix {

static u32 lastFrame = 0xFFFFFFFFu;
static i32 allowUpdate = 1;
static i32 highFPS = 0;
static volatile float accumulatedSeconds = 0.0f;
static constexpr float highFPSThreshold = 0.015625f;
static constexpr float virtualFrameSeconds = 0.01666666753590106964111328125f;

__attribute__((noinline, optnone)) static void UpdateFrame() {
    const u32 frame = memory<u32>(0x007A46F4);
    if (frame == lastFrame) {
        return;
    }
    lastFrame = frame;

    volatile float frameSeconds = memory<float>(0x0078E574);
    if (frameSeconds < 0.0f) {
        frameSeconds = 0.0f;
    }

    if (frameSeconds >= highFPSThreshold) {
        highFPS = 0;
        allowUpdate = 1;
        return;
    }

    highFPS = 1;
    accumulatedSeconds = frameSeconds + accumulatedSeconds;
    if (accumulatedSeconds >= virtualFrameSeconds) {
        accumulatedSeconds = accumulatedSeconds - virtualFrameSeconds;
        if (accumulatedSeconds > virtualFrameSeconds) {
            accumulatedSeconds = virtualFrameSeconds;
        }
        allowUpdate = 1;
    } else {
        allowUpdate = 0;
    }
}

extern "C" __attribute__((noinline)) void WaterGate(SavedRegisters* saved) {
    const u32 controller = saved->ecx;
    UpdateFrame();
    if (highFPS != 0 && allowUpdate == 0) {
        memory<u32>(controller + 0x08) = memory<u32>(0x007A46F4);
    }
}

extern "C" __attribute__((noinline)) void WaterDelta(SavedRegisters* saved) {
    if (highFPS != 0) {
        long double delta = static_cast<long double>(virtualFrameSeconds);
        delta = delta * static_cast<long double>(memory<float>(saved->edi + 0x18));
        delta = delta * static_cast<long double>(memory<float>(0x00741BF8));
        memory<float>(saved->esp + 0x18) = static_cast<float>(delta);
    }
}

}

template<class T> __attribute__((always_inline)) static inline volatile T& mem(u32 address) {
    return memory<T>(address);
}
static constexpr u32 deltaAddress = 0x0078E574;
static constexpr float referenceDelta = 0.01666666753590106964111328125f;
static inline bool positiveFinite(float value) {
    union { float f; u32 u; } bits = {value};
    return bits.u > 0 && bits.u < 0x7F800000u;
}
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
static void resetParticle(u32 owner) {
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
extern "C" __attribute__((noinline)) void ResetParticleFromEbp(SavedRegisters* saved) {
    resetParticle(saved->ebp);
}
extern "C" __attribute__((noinline)) void ResetParticleFromEsi(SavedRegisters* saved) {
    resetParticle(saved->esi);
}
extern "C" __attribute__((noinline)) void FountainAccumulate(SavedRegisters* saved) {
    const u32 owner = mem<u32>(saved->esp + 0x10);
    const float dt = mem<float>(saved->esp + 0xA0);
    const float rate = mem<float>(saved->esp + 0x14);
    saved->ecx = owner;
    saved->eflags &= ~0x40u;
    if (!positiveFinite(dt) || !positiveFinite(rate)) return;
    const long double total = positive(mem<float>(owner + 0xCC)) + dt;
    const float stored = static_cast<float>(total > 3.0e38L ? 3.0e38L : total);
    mem<float>(owner + 0xCC) = stored;
    if (static_cast<long double>(stored) * rate >= 1.0L)
        saved->eflags |= 0x40u;
}
extern "C" __attribute__((noinline)) void FountainBudget(SavedRegisters* saved) {
    const u32 owner = mem<u32>(saved->esp + 0x10);
    const long double seconds = positive(mem<float>(owner + 0xCC));
    const float rate = mem<float>(saved->esp + 0x14);
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
    mem<i32>(saved->esp + 0x18) = budget;
    saved->esi = owner;
    saved->ebp = 0;
    saved->edx = static_cast<u32>(budget);
}

extern "C" __attribute__((noinline)) void CycleUpdate(SavedRegisters* saved) {
    const u32 owner = saved->esi;
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
    const i32 height = reinterpret_cast<Dimension>(mem<u32>(table + 0x58))(texture);
    if (width <= 0 || height <= 0 || width > 0x7FFFFFFF / height) return;
    const u32 frames = static_cast<u32>(width * height);
    const i32 current = mem<i32>(owner + 0x3C);
    const u32 start = current < 0 ? 0u : static_cast<u32>(current) % frames;
    const u32 advance = static_cast<u32>(remainder(whole, frames));
    const u32 next = (start + advance) % frames;
    mem<float>(owner + 0x40) = storeRemainder(fraction / fps, 1.0L / fps);
    mem<u32>(owner + 0x3C) = next;
    reinterpret_cast<SelectFrame>(mem<u32>(table + 0xA8))(texture, next);
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
    const u32 owner = saved->esi;
    if (mem<i32>(owner + 0xDC) <= 0) {
        shakeTimers.reset(owner);
        saved->eax = 0;
        return;
    }
    saved->eax = milliseconds(shakeTimers.get(owner), mem<float>(saved->esp + 0x28));
}
extern "C" __attribute__((noinline)) void ResetShakeFromEsi(SavedRegisters* saved) {
    shakeTimers.reset(saved->esi);
}
extern "C" __attribute__((noinline)) void ResetShakeFromEcx(SavedRegisters* saved) {
    shakeTimers.reset(saved->ecx);
}
extern "C" __attribute__((noinline)) void PreviewMilliseconds(SavedRegisters* saved) {
    const u32 owner = saved->edi;
    TimerState& state = previewTimers.get(owner);
    const u32 elapsed = mem<u32>(owner + 0x390);
    if (elapsed != state.expected) state.fraction = 0;
    u32 amount = milliseconds(state, mem<float>(saved->ebp + 8));
    if (amount > 0xFFFFFFFFu - elapsed) amount = 0xFFFFFFFFu - elapsed;
    state.expected = elapsed + amount;
    saved->eax = amount;
}
extern "C" __attribute__((noinline)) void ResetPreviewFromEdi(SavedRegisters* saved) {
    previewTimers.reset(saved->edi);
}
extern "C" __attribute__((noinline)) void ResetPreviewFromEsi(SavedRegisters* saved) {
    previewTimers.reset(saved->esi);
}

}

static bool hasFpWrapper(u32 returnAddress) {
    const volatile u8* p = reinterpret_cast<const volatile u8*>(returnAddress);
    static const u8 signature[] = {
        0x89,0xDC,0x50,0x8D,0x83,0x24,0x00,0x00,0x00,
        0x83,0xC0,0x0F,0x83,0xE0,0xF0,0x0F,0xAE,0x08,0x58,0x9D,0x61
    };
    for (u32 i=0; i<sizeof(signature); ++i) if (p[i] != signature[i]) return false;
    return true;
}
using Handler = void (*)(SavedRegisters*);
extern "C" __attribute__((noinline)) void DispatchK1(SavedRegisters* saved, u32 returnAddress, Handler handler) {
    const u32 originalSavedESP = saved->esp;
    if (hasFpWrapper(returnAddress)) saved->esp += 528u;
    handler(saved);
    saved->esp = originalSavedESP;
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
        "subl $4, %esp\n\t" \
        "pushl $_" #handler "\n\t" \
        "pushl 4(%ebp)\n\t" \
        "pushl %ebx\n\t" \
        "calll _DispatchK1\n\t" \
        "addl $16, %esp\n\t" \
        "fxrstor (%esp)\n\t" \
        "movl %ebp, %esp\n\t" \
        "popl %ebp\n\t" \
        "retl\n\t"); \
}
EXPORT_HOOK(UpdateJitterTick, AnimationUpdate)
EXPORT_HOOK(GateJitterBranch1, AnimationGate1)
EXPORT_HOOK(GateJitterBranch2, AnimationGate2)
EXPORT_HOOK(GateJitterBranch3, AnimationGate3)
EXPORT_HOOK(GateWaterControllerFrame, WaterGate)
EXPORT_HOOK(FixWaterVirtualFrameDelta, WaterDelta)
EXPORT_HOOK(AccumulateParticleTime, FountainAccumulate)
EXPORT_HOOK(CalculateParticleBudget, FountainBudget)
EXPORT_HOOK(ResetParticleInit, ResetParticleFromEbp)
EXPORT_HOOK(ResetParticleDestroy, ResetParticleFromEsi)
EXPORT_HOOK(UpdateCycleTexture, CycleUpdate)
EXPORT_HOOK(AccumulateShakeMilliseconds, ShakeMilliseconds)
EXPORT_HOOK(ResetShakeNewEffect, ResetShakeFromEcx)
EXPORT_HOOK(ResetShakeConstructor, ResetShakeFromEsi)
EXPORT_HOOK(AccumulatePreviewMilliseconds, PreviewMilliseconds)
EXPORT_HOOK(ResetPreviewIdle, ResetPreviewFromEdi)
EXPORT_HOOK(ResetPreviewObject, ResetPreviewFromEsi)
