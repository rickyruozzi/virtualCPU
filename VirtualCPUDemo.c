#include "VirtualCPU.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static void waitForDemo(uint32_t milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000u);
#endif
}

int main(void) {
    static const uint8_t program[] = {
        VCPU_LI, 0, 'H', 'E',
        VCPU_STA, 0, (uint8_t)VCPU_REFRESH_BUFFER_START,
            (uint8_t)(VCPU_REFRESH_BUFFER_START >> 8),
        VCPU_LI, 0, 'L', 'L',
        VCPU_STA, 0, (uint8_t)(VCPU_REFRESH_BUFFER_START + 2),
            (uint8_t)((VCPU_REFRESH_BUFFER_START + 2) >> 8),
        VCPU_LI, 0, 'O', 0,
        VCPU_STA, 0, (uint8_t)(VCPU_REFRESH_BUFFER_START + 4),
            (uint8_t)((VCPU_REFRESH_BUFFER_START + 4) >> 8),
        VCPU_LI, 0, 'A', 'B',
        VCPU_STA, 0, (uint8_t)VCPU_PERIPHERAL_BUFFER_START,
            (uint8_t)(VCPU_PERIPHERAL_BUFFER_START >> 8),
        VCPU_HALT
    };
    VirtualCPU vcpu;

    initVirtualCPU(&vcpu, true, true);
    initRefreshHandler(&vcpu, refreshVideoBufferToConsole);
    initPeripheralOutputHandler(&vcpu, sendPeripheralBufferToConsole);
    loadProgram(&vcpu, program, sizeof(program));

    if (!startRefreshThread(&vcpu, 50)) {
        fprintf(stderr, "Unable to start refresh thread\n");
        return EXIT_FAILURE;
    }

    if (runVirtualCPU(&vcpu, 0) != VCPU_HALTED) {
        fprintf(stderr, "Virtual program did not halt correctly\n");
        stopRefreshThread(&vcpu);
        return EXIT_FAILURE;
    }

    waitForDemo(1200);
    stopRefreshThread(&vcpu);
    return EXIT_SUCCESS;
}
