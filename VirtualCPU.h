#ifndef VIRTUALCPU_H
#define VIRTUALCPU_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define VCPU_REGISTER_COUNT 8
#define VCPU_MEMORY_SIZE 4096

typedef enum VirtualCPUStatus {
    VCPU_OK = 0,
    VCPU_HALTED,
    VCPU_INVALID_OPCODE,
    VCPU_MEMORY_ERROR,
    VCPU_STEP_LIMIT
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
    VCPU_LD   = 0x30, /* LD rd, [addr16] */
    VCPU_ST   = 0x31, /* ST rs, [addr16] */
    VCPU_JMP  = 0x40, /* JMP addr16 */
    VCPU_JZ   = 0x41, /* JZ addr16 */
    VCPU_CMP  = 0x42, /* CMP rd, rs */
    VCPU_PUSH = 0x50, /* PUSH rs */
    VCPU_POP  = 0x51  /* POP rd */
} VirtualCPUOpcode;

enum {
    VCPU_FLAG_ZERO = 1 << 0,
    VCPU_FLAG_CARRY = 1 << 1,
    VCPU_FLAG_NEGATIVE = 1 << 2
};

typedef struct CPU {
    uint16_t r[VCPU_REGISTER_COUNT];
    uint16_t pc;
    uint16_t sp; 
    uint8_t flags;
    bool littleEndian;
    bool sequential;
    bool halted; 
} CPU;

typedef struct Memory {
    uint8_t data[VCPU_MEMORY_SIZE];
    size_t size;
} Memory;

typedef struct VirtualCPU {
    CPU cpu;
    Memory memory;
} VirtualCPU;

void initVirtualCPU(VirtualCPU *vcpu, bool littleEndian, bool sequential);
void loadProgram(VirtualCPU *vcpu, const uint8_t *program, size_t programSize);
VirtualCPUStatus stepVirtualCPU(VirtualCPU *vcpu);
VirtualCPUStatus runVirtualCPU(VirtualCPU *vcpu, size_t maxSteps);
void executeProgram(VirtualCPU *vcpu);

#endif // VIRTUALCPU_H