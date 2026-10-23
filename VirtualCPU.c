#include "VirtualCPU.h"


void initVirtualCPU(VirtualCPU *vcpu, bool littleEndian, bool sequential){
    vcpu->cpu.littleEndian = littleEndian;
    vcpu->cpu.sequential = sequential;
    for(int i = 0; i < 8; i++){
        vcpu->cpu.r[i] = 0;
    }
    vcpu->cpu.pc = 0;
    vcpu->cpu.sp = 0;
    vcpu->cpu.flags = 0;
}

void loadProgram(VirtualCPU *vcpu, const uint8_t *program, size_t programSize){
    if(programSize > sizeof(vcpu->memory.data)){
        fprintf(stderr, "Program size exceeds memory size\n");
        exit(EXIT_FAILURE);
    }
    if(program == NULL){
        fprintf(stderr, "Program is NULL\n");
        exit(EXIT_FAILURE);
    }
    memcpy(vcpu->memory.data, program, programSize);
    vcpu->memory.size = programSize;
}

void stepVirtualCPU(VirtualCPU * vcpu, virtualCPUStatus * status){
    if(vcpu->cpu.halted){
        *status = VCPU_HALTED;
        return;
    }
    uint8_t opcode = vcpu->memory.data[vcpu->cpu.pc];
    switch(opcode){
        case VCPU_NOP: 
            vcpu->cpu.pc++;
            *status = VCPU_OK;
            break;
        case VCPU_HALT:
            vcpu->halted = true;
            *status = VCPU_HALTED;
            break;
        
        case VCPU_MOV: /* MOV rd, rs */
            uint8_t dr = vcpu->memory.data[vcpu->cpu.pc + 1];
            uint8_t sr = vcpu->memory.data[vcpu->cpu.pc + 2];
            vcpu->cpu.r[dr] = vcpu->cpu.r[sr];
            vcpu->cpu.pc += 3;
            *status = VCPU_OK;
            break;
        
        case VCPU_LI: /* LI rd, imm16 */
            uint8_t dr = vcpu->memory.data[vcpu->cpu.pc+1];
            if(vcpu->cpu.littleEndian){
                vcpu->cpu.r[dr] = vcpu->memory.data[vcpu->cpu.pc+2] | (vcpu->memory.data[vcpu->cpu.pc+3] << 8);
            } else {
                vcpu->cpu.r[dr] = (vcpu->memory.data[vcpu->cpu.pc+2] << 8) | vcpu->memory.data[vcpu->cpu.pc+3];
            }
            vcpu->cpu.pc += 4;
            *status = VCPU_OK;
            break;

        case VCPU_ADD: /* ADD rd, rs1, rs2 */
            uint8_t dr = vcpu->memory.data[vcpu->cpu.pc+1];
            uint8_t sr1 = vcpu->memory.data[vcpu->cpu.pc+2];
            uint8_t sr2 = vcpu->memory.data[vcpu->cpu.pc+3];
            vcpu->cpu.r[dr] = vcpu->cpu.r[sr1] + vcpu->cpu.r[sr2];
            vcpu->cpu.pc += 4;
            *status = VCPU_OK;
            break;

        case VCPU_SUB: /* SUB rd, rs1, rs2 */
            uint8_t dr = vcpu->memory.data[vcpu->cpu.pc+1];
            uint8_t sr1 = vcpu->memory.data[vcpu->cpu.pc+2];
            uint8_t sr2 = vcpu->memory.data[vcpu->cpu.pc+3];
            uint16_t result = vcpu->cpu.r[sr1] - vcpu->cpu.r[sr2];
            vcpu->cpu.r[dr] = result;
            if(result == 0){
                vcpu->cpu.flags |= VCPU_FLAG_ZERO;
            } else {
                vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
            }
            if((result & 0x8000u) != 0){
                vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
            } else {
                vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
            }
            vcpu->cpu.pc += 4;
            *status = VCPU_OK;
            break;
        
        case VCPU_AND: /* AND rd, rs1, rs2 */
            uint8_t dr = vcpu->memory.data[vcpu->cpu.pc+1];
            uint8_t sr1 = vcpu->memory.data[vcpu->cpu.pc+2];
            uint8_t sr2 = vcpu->memory.data[vcpu->cpu.pc+3];
            vcpu->cpu.r[dr] = vcpu->cpu.r[sr1] & vcpu->cpu.r[sr2];
            vcpu->cpu.pc += 4;
            *status = VCPU_OK;
            break;

        case VCPU_OR: /* OR rd, rs1, rs2 */
            uint8_t dr = vcpu->memory.data[vcpu->cpu.pc+1];
            uint8_t sr1 = vcpu->memory.data[vcpu->cpu.pc+2];
            uint8_t sr2 = vcpu->memory.data[vcpu->cpu.pc+3];
            vcpu->cpu.r[dr] = vcpu->cpu.r[sr1] | vcpu->cpu.r[sr2];
            vcpu->cpu.pc += 4;
            *status = VCPU_OK;
            break;
        
        case VCPU_XOR: /* XOR rd, rs1, rs2 */
            uint8_t dr = vcpu->memory.data[vcpu->cpu.pc+1];
            uint8_t sr1 = vcpu->memory.data[vcpu->cpu.pc+2];
            uint8_t sr2 = vcpu->memory.data[vcpu->cpu.pc+3];
            vcpu->cpu.r[dr] = vcpu->cpu.r[sr1] ^ vcpu->cpu.r[sr2];
            vcpu->cpu.pc += 4;
            *status = VCPU_OK;
            break;
        
        case VCPU_LD: /* LD rd, [addr16] */
            uint8_t dr = vcpu->memory.data[vcpu->cpu.pc+1];
            uint16_t addr;
            if(vcpu->cpu.littleEndian){
                addr = vcpu->memory.data[vcpu->cpu.pc+2] | (vcpu->memory.data[vcpu->cpu.pc+3] << 8);
            } else {
                addr = (vcpu->memory.data[vcpu->cpu.pc+2] << 8) | vcpu->memory.data[vcpu->cpu.pc+3];
            }
            if(addr >= vcpu->memory.size){
                *status = VCPU_MEMORY_ERROR;
                return;
            }
            vcpu->cpu.r[dr] = vcpu->memory.data[addr];
            vcpu->cpu.pc += 4;
            *status = VCPU_OK;
            break;

        case VCPU_ST: /* ST rs, [addr16] */
            uint8_t sr = vcpu->memory.data[vcpu->cpu.pc+1];
            uint16_t addr;
            if(vcpu->cpu.littleEndian){
                addr = vcpu->memory.data[vcpu->cpu.pc+2] | (vcpu->memory.data[vcpu->cpu.pc+3] << 8);
            } else {
                addr = (vcpu->memory.data[vcpu->cpu.pc+2] << 8) | vcpu->memory.data[vcpu->cpu.pc+3];
            }
            if(addr >= vcpu->memory.size){
                *status = VCPU_MEMORY_ERROR;
                return;
            }
            vcpu->memory.data[addr] = vcpu->cpu.r[sr];
            vcpu->cpu.pc += 4;
            *status = VCPU_OK;
            break;
        
        case VCPU_JMP: /* JMP [addr16] */
            uint16_t addr;
            if(vcpu->cpu.littleEndian){
                addr = vcpu->memory.data[vcpu->cpu.pc+1] | (vcpu->memory.data[vcpu->cpu.pc+2] << 8);
            } else {
                addr = (vcpu->memory.data[vcpu->cpu.pc+1] << 8) | vcpu->memory.data[vcpu->cpu.pc+2];
            }
            if(addr >= vcpu->memory.size){
                *status = VCPU_MEMORY_ERROR;
                return;
            }
            vcpu->cpu.pc = addr;
            *status = VCPU_OK;
            break;

        case VCPU_JZ: /*JZ rd, [addr16]*/
            uint8_t rd = vcpu->memory.data[vcpu->cpu.r.pc+1];
            uint16_t addr; 
            if(vcpu->cpu.littleEndian){
                addr = vcpu->memory.data[vcpu->cpu.pc+2] | (vcpu->memory.data[vcpu->cpu.pc+3] << 8);
            } else {
                addr = (vcpu->memory.data[vcpu->cpu.pc+2] << 8) | vcpu->memory.data[vcpu->cpu.pc+3];
            }
            if(addr>= vcpu->memory.size){
                *status = VCPU_MEMORY_ERROR;
                return;
            }
            if(vcpu->cpu.r[rd] == 0){
                vcpu->cpu.pc = addr; 
            }
            else{
                vcpu->cpu.pc += 4; 
            }
            break;

        case VCPU_CMP: /* CMP rd, rs */
            uint8_t rd = vcpu->memory.data[vcpu->pc+1]; 
            uint8_t rs = vcpu->memory.data[vcpu->pc+2];
            uint16_t result = vcpu->cpu.r[rd] - vcpu->cpu.r[rs];
            if(result == 0){
                vcpu->cpu.flags |= VCPU_FLAG_ZERO;
            } else {
                vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_ZERO;
            }
            if((result & 0x8000u) != 0){ 
                vcpu->cpu.flags |= VCPU_FLAG_NEGATIVE;
            } else {
                vcpu->cpu.flags &= (uint8_t)~VCPU_FLAG_NEGATIVE;
            }
            vcpu->cpu.pc += 3;
            *status = VCPU_OK;
            break;

        case VCPU_PUSH: /* PUSH [reg] */
            uint8_t reg = vcpu-memory.data[vcpu->cpu.pc+1];
            if(vcpu->cpu.sp == 0){
                *status = VCPU_MEMORY_ERROR;
                return;
            }
            vcpu->cpu.sp--;
            vcpu->memory.data[vcpu->cpu.sp] = vcpu->cpu.r[reg];
            vcpu->cpu.pc += 2;
            *status = VCPU_OK; 
            break;

        case VCPU_POP: /* POP [reg] */
            uint8_t reg = vcpu->memory.data[vcpu->cpu.pc+1];
            if(vcpu->cpu.sp >= vcpu->memory.size){
                *status = VCPU_MEMORY_ERROR;
                return;
            }
            vcpu->cpu.r[reg] = vcpu->memory.data[vcpu->cpu.sp];
            vcpu->cpu.sp++;
            vcpu->cpu.pc += 2;
            *status = VCPU_OK;
            break;

        default:
            *status = VCPU_INVALID_OPCODE;
            break;
    }
}

void executeProgram(VirtualCPU *vcpu){
    virtualCPUStatus status;
    while(true){
        stepVirtualCPU(vcpu, &status);
        if(status != VCPU_OK){
            break;
        }
    }
}