#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "VirtualCPU.h"
#include <signal.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

static volatile sig_atomic_t keepRunning = 1;

static void handleInterrupt(int signalNumber) {
    (void)signalNumber;
    keepRunning = 0;
}

static void waitForDemo(uint32_t milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    {
        struct timespec delay;
        delay.tv_sec = (time_t)(milliseconds / 1000u);
        delay.tv_nsec = (long)((milliseconds % 1000u) * 1000000u);
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
        }
    }
#endif
}

static void drawFrame(VirtualCPU *vcpu, size_t frame) {
    static const char *const frames[] = {
        "       O        "
        "      /|\\       "
        "      / \\       ",
        "        O       "
        "       /|\\      "
        "      /  \\      ",
        "       O        "
        "      /|\\       "
        "       /\\       "
    };
    const char *source = frames[frame % (sizeof(frames) / sizeof(frames[0]))];
    uint8_t *buffer = &vcpu->memory.data[VCPU_REFRESH_BUFFER_START];

    memset(buffer, ' ', VCPU_REFRESH_BUFFER_SIZE);
    for (size_t row = 0; row < 3; row++) {
        memcpy(buffer + (row + 6) * VCPU_VIDEO_WIDTH,
               source + row * VCPU_VIDEO_WIDTH,
               VCPU_VIDEO_WIDTH);
    }
    atomic_store(&vcpu->videoDirty, true);
}

int main(void) {
    static const uint8_t program[] = {
        VCPU_HALT
    };
    VirtualCPU vcpu;

    signal(SIGINT, handleInterrupt);
    initVirtualCPU(&vcpu, true, true);
    initRefreshHandler(&vcpu, refreshVideoBufferToConsole);
    loadProgram(&vcpu, program, sizeof(program));
    drawFrame(&vcpu, 0);

    if (!startRefreshThread(&vcpu, 10)) {
        fprintf(stderr, "Unable to start refresh thread\n");
        return EXIT_FAILURE;
    }

    if (runVirtualCPU(&vcpu, 0) != VCPU_HALTED) {
        fprintf(stderr, "Virtual program did not halt correctly\n");
        stopRefreshThread(&vcpu);
        return EXIT_FAILURE;
    }

    for (size_t tick = 0; keepRunning; tick++) {
        drawFrame(&vcpu, tick / 8);
        (void)stepVirtualCPU(&vcpu);
        waitForDemo(50);
    }
    stopRefreshThread(&vcpu);
    printf("\nStopped.\n");
    return EXIT_SUCCESS;
}
