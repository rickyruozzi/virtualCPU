#ifndef VIRTUALCPU_H
#define VIRTUALCPU_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define VCPU_REGISTER_COUNT 8
#define VCPU_MEMORY_SIZE 4096
#define VCPU_STACK_SIZE 1024
#define VCPU_STACK_BASE (VCPU_MEMORY_SIZE - VCPU_STACK_SIZE)
#define VCPU_STACK_START VCPU_MEMORY_SIZE

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
} VirtualCPUOpcode;

enum {
    VCPU_FLAG_ZERO = 1 << 0,
    VCPU_FLAG_CARRY = 1 << 1,
    VCPU_FLAG_NEGATIVE = 1 << 2
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

typedef struct Memory {
    uint8_t data[VCPU_MEMORY_SIZE];
    size_t size; /* Program size used for instruction fetches. */
} Memory;

typedef struct VirtualCPU {
    CPU cpu;
    Memory memory;
} VirtualCPU;

void initVirtualCPU(VirtualCPU *vcpu, bool littleEndian, bool sequential);
void getAddressFromRegisters(VirtualCPU *vcpu, uint8_t reg1, uint8_t reg2,
                             uint16_t *address);
void loadProgram(VirtualCPU *vcpu, const uint8_t *program, size_t programSize);
VirtualCPUStatus stepVirtualCPU(VirtualCPU *vcpu);
VirtualCPUStatus runVirtualCPU(VirtualCPU *vcpu, size_t maxSteps);
void executeProgram(VirtualCPU *vcpu);
void resetVirtualCPU(VirtualCPU *vcpu);

#endif // VIRTUALCPU_H