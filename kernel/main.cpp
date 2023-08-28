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

#include "interrupts/ioapic.hpp"

static void runSelfTests() {
    Log::info("Running self-tests:");
    SelfTests::TestRunner runner;

    Cpu::Test(runner);
    Interrupts::Test(runner);
    Paging::Test(runner);
    FrameAlloc::Test(runner);
    Result::Test(runner);
    ErrType::Test(runner);
    DataStruct::Test(runner);
    HeapAlloc::Test(runner);

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

// C++ entry point of the kernel. Called by the assembly entry point
// `kernelEntry` after calling all global constructors.
// @param bootStruct: Pointer to the BootStruct as given by the bootloader when
// jumping to the kernel's assembly entry point.
extern "C" void kernelMain(BootStruct const * const bootStruct) {
    Log::info("=== Kernel C++ Entry point ===");

    Log::debug("Bootstruct is @{x}", reinterpret_cast<u64>(bootStruct));
    dumpBootStruct(*bootStruct);

    Memory::Segmentation::Init();
    FrameAlloc::Init(*bootStruct);
    Paging::Init(*bootStruct);
    // Interrupts must be initialized _after_ paging since the APIC
    // initialization requires Paging::map().
    // FIXME: We really need a way to enforce initialization order. It is hard
    // to track down issues due to initialization being performed in the wrong
    // order.
    Interrupts::Init();
    // Now that paging and the direct map have been initialized, we can switch
    // to the proper frame allocator.
    FrameAlloc::directMapInitialized();
    HeapAlloc::Init();

    runSelfTests();

    Acpi::Info const& acpi(Acpi::parseTables());
    Log::info("{} processor(s) in the system", acpi.processorDescSize);

    // Quick and dirty test for I/O APIC support.
    // FIXME: Should be removed + add proper I/O APIC testing.
    PhyAddr const ioApicBase(acpi.ioApicDesc[0].address);
    Interrupts::IoApic ioApic(ioApicBase);
    ioApic.redirectInterrupt(Interrupts::IoApic::InputPin(2),
                             Interrupts::IoApic::OutVector(32),
                             Interrupts::IoApic::DeliveryMode::Fixed,
                             Interrupts::IoApic::DestinationMode::Physical,
                             Interrupts::IoApic::InputPinPolarity::ActiveHigh,
                             Interrupts::IoApic::TriggerMode::Edge,
                             0);
    Log::info("Created entry");

    u64 intCount(0);
    while (true) {
        asm("sti");
        asm("hlt");
        intCount++;
        if (intCount > 15) {
            ioApic.setInterruptSourceMask(Interrupts::IoApic::InputPin(2),true);
            Log::debug("Maked timer input pin on I/O apic");
        }
    }
}
