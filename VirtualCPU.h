#ifndef VIRTUALCPU_H
#define VIRTUALCPU_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define VCPU_REGISTER_COUNT 8 // Number of general-purpose registers
#define VCPU_MEMORY_SIZE 0x10000u // 64KB of memory

#define VCPU_PROGRAM_START 0x0000u //inizio del programma
#define VCPU_RODATA_START  0x4000u //inizio della sezione di sola lettura
#define VCPU_DATA_START    0x5000u //inizio della sezione di dati
#define VCPU_BSS_START     0x6000u //inizio della sezione di memoria non inizializzata
#define VCPU_HEAP_START    0x7000u //inizio della sezione heap
#define VCPU_MMIO_START    0xE000u //inizio della sezione di memoria mappata su I/O
#define VCPU_MMIO_END      0xF000u //fine della sezione di memoria mappata su I/O
#define VCPU_REFRESH_BUFFER_START VCPU_MMIO_START
#define VCPU_REFRESH_BUFFER_SIZE 0x0100u
#define VCPU_VIDEO_WIDTH 16u
#define VCPU_VIDEO_HEIGHT \
    (VCPU_REFRESH_BUFFER_SIZE / VCPU_VIDEO_WIDTH)
#define VCPU_REFRESH_BUFFER_END \
    (VCPU_REFRESH_BUFFER_START + VCPU_REFRESH_BUFFER_SIZE)
#define VCPU_PERIPHERAL_BUFFER_START VCPU_REFRESH_BUFFER_END
#define VCPU_PERIPHERAL_BUFFER_SIZE 0x0100u
#define VCPU_PERIPHERAL_BUFFER_END \
    (VCPU_PERIPHERAL_BUFFER_START + VCPU_PERIPHERAL_BUFFER_SIZE)
#define VCPU_REFRESH_PERIOD 16u
#define VCPU_STACK_BASE    VCPU_MMIO_END //inizio della sezione di stack
#define VCPU_STACK_START   0xFFFEu //inizio dello stack (top of stack)
#define VCPU_STACK_SIZE    (VCPU_STACK_START - VCPU_STACK_BASE) //dimensione della sezione di stack
#define VCPU_HEAP_END      VCPU_MMIO_START //fine della sezione heap
#define VCPU_INTERRUPT_COUNT 16 //numero massimo di interrupt gestibili

typedef enum VirtualCPUStatus {
    VCPU_OK = 0,
    VCPU_HALTED,
    VCPU_INVALID_OPCODE,
    VCPU_MEMORY_ERROR,
    VCPU_STEP_LIMIT, 
    VCPU_DIVIDE_BY_ZERO
} VirtualCPUStatus;


typedef enum VirtualCPUOpcode {
    VCPU_NOP  = 0x00,
    VCPU_HALT = 0x01,
    VCPU_MOV  = 0x10, /* MOV rd, rs */
    VCPU_LI   = 0x11, /* LI rd, imm16 */
    VCPU_ADD  = 0x20, /* ADD rd, rs */
    VCPU_SUB  = 0x21, /* SUB rd, rs */
    VCPU_AND  = 0x22, /* AND rd, rs */
    VCPU_OR   = 0x23, /* OR rd, rs */
    VCPU_XOR  = 0x24, /* XOR rd, rs */
    VCPU_ADC  = 0x25, /* ADC rd, rs1, rs2 */
    VCPU_SBB  = 0x26, /* SBB rd, rs1, rs2 */
    VCPU_ANDI = 0x27, /* ANDI rd, rs, imm16 */
    VCPU_ORI  = 0x28, /* ORI rd, rs, imm16 */
    VCPU_XORI = 0x29, /* XORI rd, rs, imm16 */
    VCPU_LD   = 0x30, /* LD rd, word [r1:r2] */
    VCPU_ST   = 0x31, /* ST rs, word [r1:r2] */
    VCPU_LDA  = 0x32, /* LDA rd, word [addr16] */
    VCPU_STA  = 0x33, /* STA rs, word [addr16] */
    VCPU_JMP  = 0x40, /* JMP addr16 */
    VCPU_JZ   = 0x41, /* JZ addr16 */
    VCPU_CMP  = 0x42, /* CMP rd, rs */
    VCPU_PUSH = 0x50, /* PUSH word rs */
    VCPU_POP  = 0x51, /* POP word rd */
    VCPU_INC = 0x60, /* INC rd */
    VCPU_DEC = 0x61, /* DEC rd */
    VCPU_SHL = 0x70, /* SHL rd */
    VCPU_SHR = 0x71, /* SHR rd */
    VCPU_NEG = 0x80, /* NEG rd */
    VCPU_JNZ = 0x90, /* JNZ addr16 */
    VCPU_JC  = 0x91, /* JC addr16 */
    VCPU_JNC = 0x92, /* JNC addr16 */
    VCPU_JNN = 0x93, /* JNN addr16 */
    VCPU_JN= 0x94, /* JN addr16 */
    VCPU_JP = 0x95, /* JP addr16 */
    VCPU_CALL = 0xA0, /* CALL addr16 */
    VCPU_RET  = 0xA1, /* RET */
    VCPU_MUL  = 0xB0, /* MUL rd, rs */
    VCPU_DIV  = 0xB1, /* DIV rd, rs */
    VCPU_MOD  = 0xB2, /* MOD rd, rs */
    VCPU_INT  = 0xC0, /* INT imm8 */
    VCPU_IRET = 0xC1,  /* IRET */
    VCPU_CLI = 0xC2, /* CLI */
    VCPU_STI = 0xC3, /* STI */
    VCPU_ROL = 0xD0, /* ROL rd */
    VCPU_ROR = 0xD1, /* ROR rd */
    VCPU_NOT = 0xD2, /* NOT rd */
} VirtualCPUOpcode;

enum {
    VCPU_FLAG_ZERO = 1 << 0,
    VCPU_FLAG_CARRY = 1 << 1,
    VCPU_FLAG_NEGATIVE = 1 << 2,
    VCPU_FLAG_INTERRUPT = 1 << 3
};

typedef enum ErrorType {
    ERROR_NONE = 0,
    ERROR_INVALID_OPCODE,
    ERROR_MEMORY_ACCESS,
    ERROR_DIVIDE_BY_ZERO,
    ERROR_STACK_OVERFLOW,
    ERROR_STACK_UNDERFLOW
} ErrorType; 

typedef struct CPU {
    uint16_t r[VCPU_REGISTER_COUNT];
    uint16_t pc;
    uint16_t sp; 
    uint8_t flags;
    bool littleEndian;
    bool sequential;
    bool halted; 
    ErrorType error;
} CPU;

#define VCPU_KEYBOARD_BUFFER_SIZE 256
#define VCPU_CONSOLE_BUFFER_SIZE 256

typedef struct keyboard {
    char buffer[VCPU_KEYBOARD_BUFFER_SIZE];
    size_t readIndex;
    size_t writeIndex;
    size_t count; 
    bool overflow;
} keyboard;

typedef struct console {
    char buffer[VCPU_CONSOLE_BUFFER_SIZE];
    size_t readIndex;
    size_t writeIndex;
    size_t count;
    bool overflow;
} console;

typedef struct Memory {
    uint8_t data[VCPU_MEMORY_SIZE];
    size_t size; /* Program size used for instruction fetches. */
} Memory;

typedef struct VirtualCPU VirtualCPU;

typedef struct interruptHandler {
    void (*handler)(VirtualCPU *vcpu);
} interruptHandler;

typedef struct refreshHandler {
    void (*handler)(VirtualCPU *vcpu, const uint8_t *buffer, size_t size);
} refreshHandler;

typedef struct peripheralHandler {
    void (*handler)(VirtualCPU *vcpu, const uint8_t *buffer, size_t size);
} peripheralHandler;

struct VirtualCPU {
    CPU cpu;
    Memory memory;
    interruptHandler interruptHandlers[VCPU_INTERRUPT_COUNT];
    uint32_t timer;
    uint32_t refreshTimer;
    refreshHandler refresh;
    peripheralHandler peripheralOutput;
    atomic_bool peripheralOutputPending;
    void *refreshThread;
    void *refreshThreadLock;
    uint32_t refreshIntervalMs;
    atomic_bool refreshThreadRunning;
    keyboard kb; 
    console output;
};



void initVirtualCPU(VirtualCPU *vcpu, bool littleEndian, bool sequential);
void getAddressFromRegisters(VirtualCPU *vcpu, uint8_t reg1, uint8_t reg2,
                             uint16_t *address);
void loadProgram(VirtualCPU *vcpu, const uint8_t *program, size_t programSize);
VirtualCPUStatus stepVirtualCPU(VirtualCPU *vcpu);
VirtualCPUStatus runVirtualCPU(VirtualCPU *vcpu, size_t maxSteps);
void executeProgram(VirtualCPU *vcpu);
void resetVirtualCPU(VirtualCPU *vcpu);
void initInterruptHandler(VirtualCPU *vcpu, uint8_t interruptNumber, void (*handler)(VirtualCPU *vcpu));
void timerTick(VirtualCPU *vcpu);
void initRefreshHandler(
    VirtualCPU *vcpu,
    void (*handler)(VirtualCPU *vcpu, const uint8_t *buffer, size_t size));
bool startRefreshThread(VirtualCPU *vcpu, uint32_t intervalMs);
void stopRefreshThread(VirtualCPU *vcpu);
void refreshVideoBufferToConsole(
    VirtualCPU *vcpu, const uint8_t *buffer, size_t size);
void initPeripheralOutputHandler(
    VirtualCPU *vcpu,
    void (*handler)(VirtualCPU *vcpu, const uint8_t *buffer, size_t size));
void sendPeripheralBufferToConsole(
    VirtualCPU *vcpu, const uint8_t *buffer, size_t size);
void keyboardInput(VirtualCPU *vcpu, const char* input, size_t length);
void consoleOutput(VirtualCPU *vcpu);

#endif // VIRTUALCPU_H