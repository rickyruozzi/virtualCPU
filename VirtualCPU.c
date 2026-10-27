#include "VirtualCPU.h"
#include <string.h>

static bool instructionFits(const VirtualCPU *vcpu, size_t length) {
    return (size_t)vcpu->cpu.pc <= vcpu->memory.size &&
           length <= vcpu->memory.size - (size_t)vcpu->cpu.pc;
}

static bool validRegister(uint8_t reg) {
    return reg < VCPU_REGISTER_COUNT;
}

static bool validRegisterOperands(const uint8_t *operands, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!validRegister(operands[i])) {
            return false;
        }
    }
    return true;
}

static bool wordFitsDataMemory(uint16_t address) {
    return (size_t)address + sizeof(uint16_t) <= VCPU_STACK_BASE;
}

static uint16_t readWord(const VirtualCPU *vcpu, uint16_t address) {
    if (vcpu->cpu.littleEndian) {
        return vcpu->memory.data[address] |
            (uint16_t)(vcpu->memory.data[address + 1] << 8);
    }
    return (uint16_t)(vcpu->memory.data[address] << 8) |
        vcpu->memory.data[address + 1];
}

static void writeWord(VirtualCPU *vcpu, uint16_t address, uint16_t value) {
    if (vcpu->cpu.littleEndian) {
        vcpu->memory.data[address] = (uint8_t)value;
        vcpu->memory.data[address + 1] = (uint8_t)(value >> 8);
    } else {
        vcpu->memory.data[address] = (uint8_t)(value >> 8);
        vcpu->memory.data[address + 1] = (uint8_t)value;
    }
}

static VirtualCPUStatus evaluateError(VirtualCPU *vcpu,
                                      VirtualCPUStatus status,
                                      ErrorType error) {
    vcpu->cpu.error = error;
    return status;
}

void initVirtualCPU(VirtualCPU *vcpu, bool littleEndian, bool sequential) {
    vcpu->cpu.littleEndian = littleEndian;
    vcpu->cpu.sequential = sequential;
    vcpu->cpu.halted = false;
    for (int i = 0; i < VCPU_REGISTER_COUNT; i++) {
        vcpu->cpu.r[i] = 0;
    }
    vcpu->cpu.pc = 0;
    vcpu->cpu.sp = VCPU_STACK_START;
    vcpu->cpu.flags = 0;
    vcpu->cpu.error = ERROR_NONE;
    vcpu->memory.size = 0;
    vcpu->timer = 0;
    memset(&vcpu->kb, 0, sizeof(vcpu->kb));
    memset(&vcpu->output, 0, sizeof(vcpu->output));
    for (int i = 0; i < VCPU_INTERRUPT_COUNT; i++) {
        vcpu->interruptHandlers[i].handler = NULL;
    }
}

void getAddressFromRegisters(VirtualCPU *vcpu, uint8_t reg1, uint8_t reg2, uint16_t *address) {
    if (vcpu->cpu.littleEndian) {
        *address = vcpu->cpu.r[reg1] | (uint16_t)(vcpu->cpu.r[reg2] << 8);
    } else {
        *address = (uint16_t)(vcpu->cpu.r[reg1] << 8) | vcpu->cpu.r[reg2];
    }
}

static bool getAbsoluteAddress(const VirtualCPU *vcpu, uint16_t pc,
                               uint16_t *address) {
    if (vcpu->cpu.littleEndian) {
        *address = vcpu->memory.data[pc] |
            (uint16_t)(vcpu->memory.data[pc + 1] << 8);
    } else {
        *address = (uint16_t)(vcpu->memory.data[pc] << 8) |
            vcpu->memory.data[pc + 1];
    }
    return wordFitsDataMemory(*address);
}

static uint16_t readInstructionWord(const VirtualCPU *vcpu, uint16_t address) {
    if (vcpu->cpu.littleEndian) {
        return vcpu->memory.data[address] |
            (uint16_t)(vcpu->memory.data[address + 1] << 8);
    }
    return (uint16_t)(vcpu->memory.data[address] << 8) |
        vcpu->memory.data[address + 1];
}

int findFreeInterruptHandlerSlot(VirtualCPU *vcpu) {
    for (int i = 0; i < VCPU_INTERRUPT_COUNT; i++) {
        if (vcpu->interruptHandlers[i].handler == NULL) {
            return i;
        }
    }
    return -1; // No free slot found
}

void setInterruptHandler(VirtualCPU *vcpu, uint8_t interruptNumber, void (*handler)(VirtualCPU *vcpu)) {
    if (interruptNumber < VCPU_INTERRUPT_COUNT) {
        vcpu->interruptHandlers[interruptNumber].handler = handler;
    } else {
        fprintf(stderr, "Invalid interrupt number: %u\n", interruptNumber);
        exit(EXIT_FAILURE);
    }
}

void initInterruptHandler(VirtualCPU *vcpu, uint8_t interruptNumber, void (*handler)(VirtualCPU *vcpu)) {
    if(interruptNumber >= VCPU_INTERRUPT_COUNT) {
        fprintf(stderr, "Invalid interrupt number: %u\n", interruptNumber);
        exit(EXIT_FAILURE);
    }
    vcpu->interruptHandlers[interruptNumber].handler = handler;
}

void loadProgram(VirtualCPU *vcpu, const uint8_t *program, size_t programSize) {
    if (programSize > VCPU_STACK_BASE) {
        fprintf(stderr, "Program size exceeds data memory area\n");
        exit(EXIT_FAILURE);
    }
    if (program == NULL) {
        fprintf(stderr, "Program is NULL\n");
        exit(EXIT_FAILURE);
    }
    memcpy(vcpu->memory.data, program, programSize);
    vcpu->memory.size = programSize;
    vcpu->cpu.pc = 0;
    vcpu->cpu.sp = VCPU_STACK_START;
    vcpu->cpu.halted = false;
    vcpu->cpu.error = ERROR_NONE;
    vcpu->timer = 0;
    memset(&vcpu->kb, 0, sizeof(vcpu->kb));
    memset(&vcpu->output, 0, sizeof(vcpu->output));
}

void timerTick(VirtualCPU *vcpu) {
    vcpu->timer++;
}

void keyboardInput(VirtualCPU *vcpu, const char* input, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (vcpu->kb.count < VCPU_KEYBOARD_BUFFER_SIZE) {
            vcpu->kb.buffer[vcpu->kb.writeIndex] = input[i];
            vcpu->kb.writeIndex = (vcpu->kb.writeIndex + 1) % VCPU_KEYBOARD_BUFFER_SIZE;
            vcpu->kb.count++;
        } else {
            vcpu->kb.overflow = true;
            break; 
        }
    }
}

void consoleOutput(VirtualCPU *vcpu) {
    while (vcpu->output.count > 0) {
        char c = vcpu->output.buffer[vcpu->output.readIndex];
        putchar(c);
        vcpu->output.readIndex =
            (vcpu->output.readIndex + 1) % VCPU_CONSOLE_BUFFER_SIZE;
        vcpu->output.count--;
    }
}

VirtualCPUStatus stepVirtualCPU(VirtualCPU *vcpu) {
    uint8_t opcode;

    vcpu->cpu.error = ERROR_NONE;
    if (vcpu->cpu.halted) {
        return VCPU_HALTED;
    }
    if (!instructionFits(vcpu, 1)) {
        return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
    }

    opcode = vcpu->memory.data[vcpu->cpu.pc];
    switch (opcode) {
    case VCPU_NOP:
        vcpu->cpu.pc++;
        return VCPU_OK;

    case VCPU_HALT:
        vcpu->cpu.halted = true;
        return VCPU_HALTED;

    case VCPU_MOV: {
        uint8_t registers[2];
        if (!instructionFits(vcpu, 3)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        if (!validRegisterOperands(registers, 2)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        vcpu->cpu.r[registers[0]] = vcpu->cpu.r[registers[1]];
        if (vcpu->cpu.r[registers[0]] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[registers[0]] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 3;
        return VCPU_OK;
    }

    case VCPU_LI: {
        uint8_t reg;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        if (vcpu->cpu.littleEndian) {
            vcpu->cpu.r[reg] = vcpu->memory.data[vcpu->cpu.pc + 2] |
                (uint16_t)(vcpu->memory.data[vcpu->cpu.pc + 3] << 8);
        } else {
            vcpu->cpu.r[reg] = (uint16_t)(vcpu->memory.data[vcpu->cpu.pc + 2] << 8) |
                vcpu->memory.data[vcpu->cpu.pc + 3];
        }
        if (vcpu->cpu.r[reg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[reg] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_ADD: {
        uint8_t registers[3];
        uint32_t sum;
        uint16_t result;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc + 3];
        if (!validRegisterOperands(registers, 3)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        sum = (uint32_t)vcpu->cpu.r[registers[1]] +
            vcpu->cpu.r[registers[2]];
        result = (uint16_t)sum;
        vcpu->cpu.r[registers[0]] = result;
        if (sum > UINT16_MAX) {
            vcpu->cpu.flags |= VCPU_FLAG_CARRY;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        }
        if (result == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((result & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_SUB: {
        uint8_t registers[3];
        uint16_t left;
        uint16_t right;
        uint16_t result;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc + 3];
        if (!validRegisterOperands(registers, 3)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        left = vcpu->cpu.r[registers[1]];
        right = vcpu->cpu.r[registers[2]];
        result = left - right;
        vcpu->cpu.r[registers[0]] = result;
        if (left < right) {
            vcpu->cpu.flags |= VCPU_FLAG_CARRY;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        }
        if (result == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((result & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_AND: {
        uint8_t registers[3];
        uint16_t result;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc + 3];
        if (!validRegisterOperands(registers, 3)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        result = vcpu->cpu.r[registers[1]] & vcpu->cpu.r[registers[2]];
        vcpu->cpu.r[registers[0]] = result;
        vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        if (result == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((result & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_OR: {
        uint8_t registers[3];
        uint16_t result;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc + 3];
        if (!validRegisterOperands(registers, 3)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        result = vcpu->cpu.r[registers[1]] | vcpu->cpu.r[registers[2]];
        vcpu->cpu.r[registers[0]] = result;
        vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        if (result == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((result & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_XOR: {
        uint8_t registers[3];
        uint16_t result;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc + 3];
        if (!validRegisterOperands(registers, 3)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        result = vcpu->cpu.r[registers[1]] ^ vcpu->cpu.r[registers[2]];
        vcpu->cpu.r[registers[0]] = result;
        vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        if (result == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((result & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_LD: {
        uint8_t registers[3];
        uint16_t address;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc + 3];
        if (!validRegisterOperands(registers, 3)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        getAddressFromRegisters(vcpu, registers[1], registers[2], &address);
        if (!wordFitsDataMemory(address)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        vcpu->cpu.r[registers[0]] = readWord(vcpu, address);
        if (vcpu->cpu.r[registers[0]] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[registers[0]] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_ST: {
        uint8_t registers[3];
        uint16_t address;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc + 3];
        if (!validRegisterOperands(registers, 3)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        getAddressFromRegisters(vcpu, registers[1], registers[2], &address);
        if (!wordFitsDataMemory(address)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        writeWord(vcpu, address, vcpu->cpu.r[registers[0]]);
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_LDA: {
        uint8_t reg;
        uint16_t address;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        if (!getAbsoluteAddress(vcpu, vcpu->cpu.pc + 2, &address)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        vcpu->cpu.r[reg] = readWord(vcpu, address);
        if (vcpu->cpu.r[reg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[reg] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_STA: {
        uint8_t reg;
        uint16_t address;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        if (!getAbsoluteAddress(vcpu, vcpu->cpu.pc + 2, &address)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        writeWord(vcpu, address, vcpu->cpu.r[reg]);
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_JMP: {
        uint16_t address;
        if (!instructionFits(vcpu, 3)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if (vcpu->cpu.littleEndian) {
            address = vcpu->memory.data[vcpu->cpu.pc + 1] |
                (uint16_t)(vcpu->memory.data[vcpu->cpu.pc + 2] << 8);
        } else {
            address = (uint16_t)(vcpu->memory.data[vcpu->cpu.pc + 1] << 8) |
                vcpu->memory.data[vcpu->cpu.pc + 2];
        }
        if ((size_t)address >= vcpu->memory.size) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        vcpu->cpu.pc = address;
        return VCPU_OK;
    }

    case VCPU_JZ: {
        uint8_t reg;
        uint16_t address;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        if (vcpu->cpu.littleEndian) {
            address = vcpu->memory.data[vcpu->cpu.pc + 2] |
                (uint16_t)(vcpu->memory.data[vcpu->cpu.pc + 3] << 8);
        } else {
            address = (uint16_t)(vcpu->memory.data[vcpu->cpu.pc + 2] << 8) |
                vcpu->memory.data[vcpu->cpu.pc + 3];
        }
        if ((size_t)address >= vcpu->memory.size) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if (vcpu->cpu.r[reg] == 0) {
            vcpu->cpu.pc = address;
        } else {
            vcpu->cpu.pc += 4;
        }
        return VCPU_OK;
    }

    case VCPU_CMP: {
        uint8_t registers[2];
        uint16_t left;
        uint16_t right;
        uint16_t result;
        if (!instructionFits(vcpu, 3)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        if (!validRegisterOperands(registers, 2)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        left = vcpu->cpu.r[registers[0]];
        right = vcpu->cpu.r[registers[1]];
        result = left - right;
        if (left < right) {
            vcpu->cpu.flags |= VCPU_FLAG_CARRY;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        }
        if (result == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((result & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 3;
        return VCPU_OK;
    }

    case VCPU_PUSH: {
        uint8_t reg;
        if (!instructionFits(vcpu, 2)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        if (vcpu->cpu.sp < VCPU_STACK_BASE + sizeof(uint16_t)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_STACK_OVERFLOW);
        }
        vcpu->cpu.sp -= sizeof(uint16_t);
        writeWord(vcpu, vcpu->cpu.sp, vcpu->cpu.r[reg]);
        vcpu->cpu.pc += 2;
        return VCPU_OK;
    }

    case VCPU_POP: {
        uint8_t reg;
        if (!instructionFits(vcpu, 2)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        if (vcpu->cpu.sp >= VCPU_STACK_START) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_STACK_UNDERFLOW);
        }
        vcpu->cpu.r[reg] = readWord(vcpu, vcpu->cpu.sp);
        if (vcpu->cpu.r[reg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[reg] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.sp += sizeof(uint16_t);
        vcpu->cpu.pc += 2;
        return VCPU_OK;
    }

    case VCPU_INC: 
    {
        uint8_t reg; 
        uint16_t oldValue;
        if (!instructionFits(vcpu, 2)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc +1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        oldValue = vcpu->cpu.r[reg];
        vcpu->cpu.r[reg]++;
        if (oldValue == UINT16_MAX) {
            vcpu->cpu.flags |= VCPU_FLAG_CARRY;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        }
        if (vcpu->cpu.r[reg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[reg] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 2;
        return VCPU_OK;
    }

    case VCPU_DEC: 
    {
        uint8_t reg; 
        uint16_t oldValue;
        if (!instructionFits(vcpu, 2)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc +1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        oldValue = vcpu->cpu.r[reg];
        vcpu->cpu.r[reg]--;
        if (oldValue == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_CARRY;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        }
        if (vcpu->cpu.r[reg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[reg] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 2;
        return VCPU_OK;
    }

    case VCPU_SHL: //SHL   rd, rs, amount
    {
        uint8_t DestinationReg;
        uint8_t sourceReg;
        uint8_t amount;
        uint16_t sourceValue;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        DestinationReg = vcpu->memory.data[vcpu->cpu.pc +1];
        if (!validRegister(DestinationReg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        sourceReg = vcpu->memory.data[vcpu->cpu.pc +2];
        if (!validRegister(sourceReg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        amount = vcpu->memory.data[vcpu->cpu.pc + 3];
        sourceValue = vcpu->cpu.r[sourceReg];
        if (amount == 0) {
            vcpu->cpu.r[DestinationReg] = sourceValue;
        } else if (amount < 16) {
            vcpu->cpu.r[DestinationReg] = (uint16_t)(sourceValue << amount);
            if ((sourceValue & (uint16_t)(1u << (16 - amount))) != 0) {
                vcpu->cpu.flags |= VCPU_FLAG_CARRY;
            } else {
                vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
            }
        } else {
            vcpu->cpu.r[DestinationReg] = 0;
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        }
        if (vcpu->cpu.r[DestinationReg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[DestinationReg] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_SHR: //SHR   rd, rs, amount
    {
        uint8_t DestinationReg;
        uint8_t sourceReg;
        uint8_t amount;
        uint16_t sourceValue;
        if (!instructionFits(vcpu, 4)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        DestinationReg = vcpu->memory.data[vcpu->cpu.pc +1];
        if (!validRegister(DestinationReg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        sourceReg = vcpu->memory.data[vcpu->cpu.pc +2];
        if (!validRegister(sourceReg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        amount = vcpu->memory.data[vcpu->cpu.pc + 3];
        sourceValue = vcpu->cpu.r[sourceReg];
        if (amount == 0) {
            vcpu->cpu.r[DestinationReg] = sourceValue;
        } else if (amount < 16) {
            vcpu->cpu.r[DestinationReg] = (uint16_t)(sourceValue >> amount);
            if ((sourceValue & (uint16_t)(1u << (amount - 1))) != 0) {
                vcpu->cpu.flags |= VCPU_FLAG_CARRY;
            } else {
                vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
            }
        } else {
            vcpu->cpu.r[DestinationReg] = 0;
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        }
        if (vcpu->cpu.r[DestinationReg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[DestinationReg] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_NEG: //NEG   rd
    {
        uint8_t reg;
        uint16_t value;
        if (!instructionFits(vcpu, 2)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc +1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        value = vcpu->cpu.r[reg];
        vcpu->cpu.r[reg] = (uint16_t)(-value);
        if (value != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_CARRY;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        }
        if (vcpu->cpu.r[reg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[reg] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 2;
        return VCPU_OK;
    }

    case VCPU_JNZ:{ //JNZ   reg, address
        uint8_t reg;
        uint16_t address; 
        if( instructionFits(vcpu, 4) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if(!validRegister(reg)){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        address = readInstructionWord(vcpu, vcpu->cpu.pc + 2);
        if((size_t)address >= vcpu->memory.size){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if(vcpu->cpu.r[reg] != 0){
            vcpu->cpu.pc = address;
        } else {
            vcpu->cpu.pc += 4;
        }
        return VCPU_OK;
    }

    case VCPU_JC:{
        uint8_t reg; 
        uint16_t address;
        if( instructionFits(vcpu, 4) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if(!validRegister(reg)){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        address = readInstructionWord(vcpu, vcpu->cpu.pc + 2);
        if((size_t)address >=  vcpu->memory.size){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if((vcpu->cpu.flags & VCPU_FLAG_CARRY) != 0){
            vcpu->cpu.pc = address;
        } else {
            vcpu->cpu.pc += 4;
        }
        return VCPU_OK;
    }

    case VCPU_JNC:{ //JNC reg, address
        uint8_t reg;
        uint16_t address; 
        if( instructionFits(vcpu, 4) == false){ 
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if(!validRegister(reg)){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        address = readInstructionWord(vcpu, vcpu->cpu.pc + 2);
        if((size_t)address >= vcpu->memory.size){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if((vcpu->cpu.flags & VCPU_FLAG_CARRY) == 0){
            vcpu->cpu.pc = address;
        } else {
            vcpu->cpu.pc += 4;
        }
        return VCPU_OK;
    } 

    case VCPU_JN: { //JN reg, address
        uint8_t reg; 
        uint16_t address; 
        if(instructionFits(vcpu, 4) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if(!validRegister(reg)){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        address = readInstructionWord(vcpu, vcpu->cpu.pc + 2);
        if((size_t)address >= vcpu->memory.size){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if((vcpu->cpu.flags & VCPU_FLAG_NEGATIVE) != 0){
            vcpu->cpu.pc = address;
        } else {
            vcpu->cpu.pc += 4;
        }
        return VCPU_OK;
    }

    case VCPU_JNN:{
        uint8_t reg; 
        uint16_t address; 
        if(instructionFits(vcpu, 4) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if(!validRegister(reg)){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        address = readInstructionWord(vcpu, vcpu->cpu.pc + 2);
        if((size_t)address >= vcpu->memory.size){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if((vcpu->cpu.flags & VCPU_FLAG_NEGATIVE) == 0){
            vcpu->cpu.pc = address;
        } else {
            vcpu->cpu.pc += 4;
        }
        return VCPU_OK;
    }

    case VCPU_JP:{
        uint8_t reg; 
        uint16_t address; 
        if(instructionFits(vcpu, 4) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc + 1];
        if(!validRegister(reg)){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        address = readInstructionWord(vcpu, vcpu->cpu.pc + 2);
        if((size_t)address >= vcpu->memory.size){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if((vcpu->cpu.flags & VCPU_FLAG_ZERO) == 0){
            vcpu->cpu.pc = address;
        } else {
            vcpu->cpu.pc += 4;
        }
        return VCPU_OK;
    }

    case VCPU_CALL: { // call address
        uint16_t address;
        if (!instructionFits(vcpu, 3)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        address = readInstructionWord(vcpu, vcpu->cpu.pc + 1);
        if((size_t)address >= vcpu->memory.size) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if (vcpu->cpu.sp < VCPU_STACK_BASE + sizeof(uint16_t)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_STACK_OVERFLOW);
        }
        vcpu->cpu.sp -= sizeof(uint16_t); //dobbiamo salvare il valore del pc + 3 nello stack, in modo da poter tornare al punto giusto dopo la chiamata
        writeWord(vcpu, vcpu->cpu.sp, vcpu->cpu.pc + 3);
        vcpu->cpu.pc = address;
        return VCPU_OK;
    }

    case VCPU_RET:{
        if(instructionFits(vcpu, 1) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if(vcpu->cpu.sp >= VCPU_STACK_START){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_STACK_UNDERFLOW);
        }
        vcpu->cpu.pc = readWord(vcpu, vcpu->cpu.sp);
        vcpu->cpu.sp += sizeof(uint16_t);
        return VCPU_OK;
    }

    case VCPU_MUL:{
        uint8_t registers[3];
        uint32_t product; 
        if(instructionFits(vcpu, 4) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc+1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc+2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc+3];
        if(!validRegisterOperands(registers, 3)){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        product = (uint32_t)vcpu->cpu.r[registers[1]] * (uint32_t)vcpu->cpu.r[registers[2]];
        vcpu->cpu.r[registers[0]] = (uint16_t)(product & 0xFFFFu);
        if(product > UINT16_MAX){
            vcpu->cpu.flags |= VCPU_FLAG_CARRY;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_CARRY;
        }
        if (vcpu->cpu.r[registers[0]] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((vcpu->cpu.r[registers[0]] & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_DIV:{
        uint8_t registers[3];
        uint16_t divisor;
        uint16_t result;
        if(instructionFits(vcpu, 4) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc+1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc+2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc+3];
        if(!validRegisterOperands(registers, 3)){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        divisor = vcpu->cpu.r[registers[2]];
        if(divisor == 0){
            return evaluateError(vcpu, VCPU_DIVIDE_BY_ZERO, ERROR_DIVIDE_BY_ZERO);
        }
        result = vcpu->cpu.r[registers[1]] / divisor;
        vcpu->cpu.r[registers[0]] = result;
        if (result == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((result & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_MOD: {
        uint8_t registers[3];
        uint16_t divisor;
        uint16_t result;
        if(instructionFits(vcpu, 4) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc+1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc+2];
        registers[2] = vcpu->memory.data[vcpu->cpu.pc+3];
        if(!validRegisterOperands(registers, 3)){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        divisor = vcpu->cpu.r[registers[2]];
        if(divisor == 0){
            return evaluateError(vcpu, VCPU_DIVIDE_BY_ZERO, ERROR_DIVIDE_BY_ZERO);
        }
        result = vcpu->cpu.r[registers[1]] % divisor;
        vcpu->cpu.r[registers[0]] = result;
        if (result == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        if ((result & 0x8000u) != 0) {
            vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_INT:{
        uint8_t interruptNumber; 
        if(instructionFits(vcpu, 2) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        interruptNumber = vcpu->memory.data[vcpu->cpu.pc+1];
        if(interruptNumber >= VCPU_INTERRUPT_COUNT){
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        if(vcpu->interruptHandlers[interruptNumber].handler != NULL){
            vcpu->interruptHandlers[interruptNumber].handler(vcpu);
        } else {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        return VCPU_OK;
    }

    case VCPU_IRET:{ //IRET
        if(instructionFits(vcpu, 1) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        if(vcpu->cpu.sp >= VCPU_STACK_START){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_STACK_UNDERFLOW);
        }
        vcpu->cpu.pc = readWord(vcpu, vcpu->cpu.sp);
        vcpu->cpu.sp += sizeof(uint16_t);
        return VCPU_OK;
    }

    case VCPU_STI:{ //STI
        if(instructionFits(vcpu, 1) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        vcpu->cpu.flags |= VCPU_FLAG_INTERRUPT;
        vcpu->cpu.pc += 1;
        return VCPU_OK;
    }

    case VCPU_CLI:{ //CLI
        if(instructionFits(vcpu, 1) == false){
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_INTERRUPT;
        vcpu->cpu.pc += 1;
        return VCPU_OK;
    }

    default:
        return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
    }
}

VirtualCPUStatus runVirtualCPU(VirtualCPU *vcpu, size_t maxSteps) {
    size_t steps = 0;
    VirtualCPUStatus status = VCPU_OK;

    while (status == VCPU_OK &&
           (maxSteps == 0 || steps < maxSteps)) {
        status = stepVirtualCPU(vcpu);
        steps++;
    }
    if (status == VCPU_OK && maxSteps != 0) {
        return VCPU_STEP_LIMIT;
    }
    return status;
}

void executeProgram(VirtualCPU *vcpu) {
    (void)runVirtualCPU(vcpu, 0);
}

void resetVirtualCPU(VirtualCPU *vcpu) {
    vcpu->cpu.pc = 0;
    vcpu->cpu.sp = VCPU_STACK_START;
    vcpu->cpu.flags = 0;
    vcpu->cpu.halted = false;
    vcpu->cpu.error = ERROR_NONE;
    memset(&vcpu->kb, 0, sizeof(vcpu->kb));
    memset(&vcpu->output, 0, sizeof(vcpu->output));
    for (int i = 0; i < VCPU_REGISTER_COUNT; i++) {
        vcpu->cpu.r[i] = 0;
    }
}