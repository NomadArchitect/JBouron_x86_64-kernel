// C++ entry point of the kernel.

#include <util/util.hpp>
#include <logging/log.hpp>
#include <memory/segmentation.hpp>
#include <selftests/selftests.hpp>
#include <cpu/cpu.hpp>
#include <interrupts/interrupts.hpp>
#include <bootstruct.hpp>
#include <framealloc/framealloc.hpp>
#include <paging/paging.hpp>
#include <util/result.hpp>
#include <util/err.hpp>
#include <datastruct/datastruct.hpp>
#include <memory/malloc.hpp>
#include <util/assert.hpp>
#include <util/subrange.hpp>
#include <acpi/acpi.hpp>
#include <timers/timers.hpp>
#include <timers/lapictimer.hpp>
#include <interrupts/vectormap.hpp>
#include <smp/smp.hpp>
#include <memory/stack.hpp>
#include <concurrency/tests.hpp>
#include <smp/percpu.hpp>
#include <interrupts/ipi.hpp>
#include <smp/remotecall.hpp>
#include <util/ptr.hpp>

#include "interrupts/ioapic.hpp"

#include <util/panic.hpp>

// Wake up all Application Processor and put them in a while(true) { halt() }
// loop.
static void wakeAps() {
    // Target for the APs.
    auto const apTarget([]() {
        Log::info("CPU {} online", Smp::id());
        while (true) {
            asm("sti");
            asm("hlt");
        }
    });

    u32 const numCpus(Smp::ncpus());
    Log::info("Waking all {} AP(s) in the system", numCpus-1);
    for (Smp::Id id(1); id < numCpus; ++id) {
        Smp::startupApplicationProcessor(id, apTarget);
    }
}

static void runSelfTests() {
    Log::info("Running self-tests:");
    SelfTests::TestRunner runner;

    Cpu::Test(runner);
    Memory::Segmentation::Test(runner);
    Interrupts::Test(runner);
    Paging::Test(runner);
    FrameAlloc::Test(runner);
    Result::Test(runner);
    ErrType::Test(runner);
    DataStruct::Test(runner);
    HeapAlloc::Test(runner);
    Timer::Test(runner);
    Smp::Test(runner);

    if (Smp::ncpus() > 1) {
        Log::info("Running multi-cpus tests:");
        wakeAps();

        Interrupts::Ipi::Test(runner);
        Smp::RemoteCall::Test(runner);
        Concurrency::Test(runner);
        SmartPtr::Test(runner);
    } else {
        Log::warn("Skipping multi-cpus tests due to having a single core");
    }

    runner.printSummary();
}

// Log the bootstruct's content.
// @param bootStruct: The bootstruct to dump into the logs.
static void dumpBootStruct(BootStruct const& bootStruct) {
    Log::info("Physical memory map:");
    for (u64 i(0); i < bootStruct.memoryMapSize; ++i) {
        BootStruct::MemMapEntry const& entry(bootStruct.memoryMap[i]);
        char const * const type(entry.isAvailable() ? "Available" : "Reserved");
        u64 const end(entry.base + entry.length);
        Log::info("  {x} - {x}  {}", entry.base, end, type);
    }

    Log::debug("Physical frame free-list:");
    BootStruct::PhyFrameFreeListNode const * curr(
        bootStruct.phyFrameFreeListHead);
    while (!!curr) {
        Log::debug("  {} frames starting @{x}", curr->numFrames, curr->base);
        curr = curr->next;
    }
}

// Initialize the kernel.
static void initKernel(BootStruct const * const bootStruct) {
    Memory::Segmentation::Init();
    FrameAlloc::Init(*bootStruct);
    Paging::Init(*bootStruct);
    // Interrupts must be initialized _after_ paging since the APIC
    // initialization requires Paging::map().
    // FIXME: We really need a way to enforce initialization order. It is hard
    // to track down issues due to initialization being performed in the wrong
    // order.
    // Now that paging and the direct map have been initialized, we can switch
    // to the proper frame allocator.
    FrameAlloc::directMapInitialized();
    HeapAlloc::Init();
    Smp::PerCpu::Init();
    Interrupts::Init();
    Smp::RemoteCall::Init();
}

// Target code after the BSP switches to the new higher-half stack. This
// function does not return.
static void stackSwitchTarget() {
    runSelfTests();

    while (true) {
        asm("sti");
        asm("hlt");
    }
}

// C++ entry point of the kernel. Called by the assembly entry point
// `kernelEntry` after calling all global constructors.
// @param bootStruct: Pointer to the BootStruct as given by the bootloader when
// jumping to the kernel's assembly entry point.
extern "C" void kernelMain(BootStruct const * const bootStruct) {
    Log::info("=== Kernel C++ Entry point ===");

    Log::debug("Bootstruct is @{x}", reinterpret_cast<u64>(bootStruct));
    dumpBootStruct(*bootStruct);

    initKernel(bootStruct);

    // Now that the kernel has been initialized, we can switch to a proper stack
    // instead of staying on the minuscule one that was used throughout the
    // bootloader.
    Res<VirAddr> const stackAllocRes(Stack::allocate());
    if (!stackAllocRes) {
        PANIC("Cannot allocate a stack for the BSP: {}", stackAllocRes.error());
    }
    Stack::switchToStack(stackAllocRes.value(), stackSwitchTarget);

    UNREACHABLE
}
