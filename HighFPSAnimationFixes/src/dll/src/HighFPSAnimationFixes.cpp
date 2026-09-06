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

// HighFPSAnimationFixes
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

extern "C" __declspec(dllexport) __attribute__((naked)) void UpdateJitterTick() {
    __asm__ volatile("pushl %ebx\n\tcalll _AnimationUpdate\n\taddl $4, %esp\n\tretl");
}

extern "C" __declspec(dllexport) __attribute__((naked)) void GateJitterBranch1() {
    __asm__ volatile("pushl %ebx\n\tcalll _AnimationGate1\n\taddl $4, %esp\n\tretl");
}

extern "C" __declspec(dllexport) __attribute__((naked)) void GateJitterBranch2() {
    __asm__ volatile("pushl %ebx\n\tcalll _AnimationGate2\n\taddl $4, %esp\n\tretl");
}

extern "C" __declspec(dllexport) __attribute__((naked)) void GateJitterBranch3() {
    __asm__ volatile("pushl %ebx\n\tcalll _AnimationGate3\n\taddl $4, %esp\n\tretl");
}
