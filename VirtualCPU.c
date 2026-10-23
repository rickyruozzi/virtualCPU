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
    vcpu->cpu.sp = 0;
    vcpu->cpu.flags = 0;
    vcpu->cpu.error = ERROR_NONE;
    vcpu->memory.size = 0;
}

void loadProgram(VirtualCPU *vcpu, const uint8_t *program, size_t programSize) {
    if (programSize > sizeof(vcpu->memory.data)) {
        fprintf(stderr, "Program size exceeds memory size\n");
        exit(EXIT_FAILURE);
    }
    if (program == NULL) {
        fprintf(stderr, "Program is NULL\n");
        exit(EXIT_FAILURE);
    }
    memcpy(vcpu->memory.data, program, programSize);
    vcpu->memory.size = programSize;
    vcpu->cpu.pc = 0;
    vcpu->cpu.halted = false;
    vcpu->cpu.error = ERROR_NONE;
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
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_ADD: {
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
        result = vcpu->cpu.r[registers[1]] + vcpu->cpu.r[registers[2]];
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

    case VCPU_SUB: {
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
        result = vcpu->cpu.r[registers[1]] - vcpu->cpu.r[registers[2]];
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
        vcpu->cpu.r[reg] = vcpu->memory.data[address];
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_ST: {
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
        vcpu->memory.data[address] = (uint8_t)vcpu->cpu.r[reg];
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
        uint16_t result;
        if (!instructionFits(vcpu, 3)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        registers[0] = vcpu->memory.data[vcpu->cpu.pc + 1];
        registers[1] = vcpu->memory.data[vcpu->cpu.pc + 2];
        if (!validRegisterOperands(registers, 2)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        result = vcpu->cpu.r[registers[0]] - vcpu->cpu.r[registers[1]];
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
        if (vcpu->cpu.sp == 0 ||
            (size_t)(vcpu->cpu.sp - 1) >= sizeof(vcpu->memory.data)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_STACK_OVERFLOW);
        }
        vcpu->cpu.sp--;
        vcpu->memory.data[vcpu->cpu.sp] = (uint8_t)vcpu->cpu.r[reg];
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
        if ((size_t)vcpu->cpu.sp >= vcpu->memory.size ||
            (size_t)vcpu->cpu.sp >= sizeof(vcpu->memory.data)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_STACK_UNDERFLOW);
        }
        vcpu->cpu.r[reg] = vcpu->memory.data[vcpu->cpu.sp];
        vcpu->cpu.sp++;
        vcpu->cpu.pc += 2;
        return VCPU_OK;
    }

    case VCPU_INC: 
    {
        uint8_t reg; 
        if (!instructionFits(vcpu, 2)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc +1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        vcpu->cpu.r[reg]++;
        if (vcpu->cpu.r[reg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        vcpu->cpu.pc += 2;
        return VCPU_OK;
    }

    case VCPU_DEC: 
    {
        uint8_t reg; 
        if (!instructionFits(vcpu, 2)) {
            return evaluateError(vcpu, VCPU_MEMORY_ERROR, ERROR_MEMORY_ACCESS);
        }
        reg = vcpu->memory.data[vcpu->cpu.pc +1];
        if (!validRegister(reg)) {
            return evaluateError(vcpu, VCPU_INVALID_OPCODE, ERROR_INVALID_OPCODE);
        }
        vcpu->cpu.r[reg]--;
        if (vcpu->cpu.r[reg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        vcpu->cpu.pc += 2;
        return VCPU_OK;
    }

    case VCPU_SHL: //SHL   rd, rs, amount
    {
        uint8_t DestinationReg;
        uint8_t sourceReg;
        if (!instructionFits(vcpu, 2)) {
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
        vcpu->cpu.r[DestinationReg] = vcpu->cpu.r[sourceReg] << vcpu->memory.data[vcpu->cpu.pc +3];
        if (vcpu->cpu.r[DestinationReg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        vcpu->cpu.pc += 4;
        return VCPU_OK;
    }

    case VCPU_SHR: //SHR   rd, rs, amount
    {
        uint8_t DestinationReg;
        uint8_t sourceReg;
        if (!instructionFits(vcpu, 2)) {
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
        vcpu->cpu.r[DestinationReg] = vcpu->cpu.r[sourceReg] >> vcpu->memory.data[vcpu->cpu.pc +3];
        if (vcpu->cpu.r[DestinationReg] == 0) {
            vcpu->cpu.flags |= VCPU_FLAG_ZERO;
        } else {
            vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
        }
        vcpu->cpu.pc += 4;
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
    vcpu->cpu.sp = 0;
    vcpu->cpu.flags = 0;
    vcpu->cpu.halted = false;
    vcpu->cpu.error = ERROR_NONE;
    for (int i = 0; i < VCPU_REGISTER_COUNT; i++) {
        vcpu->cpu.r[i] = 0;
    }
}