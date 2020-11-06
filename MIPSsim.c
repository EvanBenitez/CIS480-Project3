#include <stdio.h>
#include <stdlib.h>

//Program counter
int PC = 508;

//file array
int memory[256];

//create data structures for registers
int reg[32];

struct {
  int memout;
  int ALUOut;
  int RegRd;
  int ctrl;
} MEM_WB;

struct {
  int btgt;
  char zero;
  int ALUOut;
  int rd2;
  int RegRd;
  int ctrl;
} EX_MEM;

struct {
  int pc4;
  int rd1;
  int rd2;
  int extend;
  int rt;
  int rd;
  int ctrl;
} ID_EX;

struct {
  int pc4;
  int inst;
} IF_ID;

//extract instruction and convert to small endian for compatibility
int instruction(int raw){
  int inst = 0; //final instruction (little endian order for intel compatibility)

  //reverse bytes
  inst += (raw & 0xff000000) >> 24;
  inst += (raw & 0x00ff0000) >> 8;
  inst += (raw & 0x0000ff00) << 8;
  inst += (raw & 0x000000ff) << 24;

  return inst;
}

//initialize registers and load file
void init(char *fileName){
  //Extract instructions
  FILE *file;

  //Check file
  if((file = fopen(fileName, "r")) == NULL){
    perror("No such file.");
    exit(0);
  }

  int raw; //for raw file data
  for(int i = 0; i < 256 && fread(&raw,4,1,file); i++){
    memory[i] = instruction(raw); //convert to little endian order
  }

  //Initialize registers with zeros
  MEM_WB.memout = 0;
  MEM_WB.ALUOut = 0;
  MEM_WB.RegRd = 0;
  MEM_WB.ctrl = 0;

  EX_MEM.btgt = 0;
  EX_MEM.zero = 0;
  EX_MEM.ALUOut = 0;
  EX_MEM.rd2 = 0;
  EX_MEM.RegRd = 0;
  EX_MEM.ctrl = 0;

  ID_EX.pc4 = 0;
  ID_EX.rd1 = 0;
  ID_EX.rd2 = 0;
  ID_EX.extend = 0;
  ID_EX.rt = 0;
  ID_EX.rd = 0;
  ID_EX.ctrl = 0;

  IF_ID.pc4 = 0;
  IF_ID.inst = 0;

  for(int i = 0; i<31; i++){
    reg[i] = 0;
  }

  //load file into memory[]
}

//print register and memory status
void print_result(){
  //print PC
  printf("PC = %x \n", PC);

  //print first 16 words of .data segment
  printf("DM        %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d \n", memory[0], memory[1], memory[2], memory[3],
                                                                                          memory[4], memory[5], memory[6], memory[7],
                                                                                          memory[8], memory[9], memory[10], memory[11],
                                                                                          memory[12], memory[13], memory[14], memory[15]);

  //print first 16 registers
  printf("RegFile   %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d %4d \n", reg[0], reg[1], reg[2], reg[3],
                                                                                          reg[4], reg[5], reg[6], reg[7],
                                                                                          reg[8], reg[9], reg[10], reg[11],
                                                                                          reg[12], reg[13], reg[14], reg[15]);

  //print pipeline registers
  printf("IF/ID (pc4, inst)                               %-4x %08x\n", IF_ID.pc4, IF_ID.inst);
  printf("ID/EX (pc4, rd1, rd2, extend, rt, rd, ctrl)     %-4x %-4d %-4d %-4d %-4d %-4d %03x\n", ID_EX.pc4, ID_EX.rd1, ID_EX.rd2, ID_EX.extend, ID_EX.rt, ID_EX.rd, ID_EX.ctrl);
  printf("EX/MEM (btgt, zero, ALUOut, rd2, RegRd, ctrl)   %-4x %-4d %-4d %-4d %-4d %03x\n", EX_MEM.btgt, EX_MEM.zero, EX_MEM.ALUOut, EX_MEM.rd2, EX_MEM.RegRd, EX_MEM.ctrl);
  printf("MEM/WB (memout, ALUOut, RegRd, ctrl)           %-4d %-4d %-4d %03x\n", MEM_WB.memout, MEM_WB.ALUOut, MEM_WB.RegRd, MEM_WB.ctrl);

  printf("-----------------------------------------------------------------------------------------------\n");
}

//get the control signal from the instruction
int control_signal(int inst){
  int opicode = (inst & 0xfc000000) >> 26;
  switch(opicode){
    case 0 : //R-format
      return 0x588;
    case 8 : //ADDI
      return 0x380;
    case 35 : //LW
      return 0x2c0;
    case 43 : //SW
      return 0x220;
    case 4 : //BEQ
      return 0x014;
    case 5 : //BNE
      return 0x006;
    default :
        return 0;
  }

}

//Instruction Fetch tasks
void IF_Stage(){
  PC += 4; //Update program counter
  IF_ID.inst = memory[PC/4]; //Put instruction code in pipleilne register
  IF_ID.pc4 = PC + 4; //Put PC in pipeline register
}

//Instruction Decode tasks
void ID_Stage(){
  ID_EX.pc4 = IF_ID.pc4; // move PC down the pipeline

  //get register data 1 and 2
  int reg1 = (IF_ID.inst & 0x03e00000) >> 21;
  int reg2 = (IF_ID.inst & 0x001f0000) >> 16;
  ID_EX.rd1 = reg[reg1];
  ID_EX.rd2 = reg[reg2];
  ID_EX.extend = (IF_ID.inst & 0x0000ffff);

  //check sign bit and extend
  if((ID_EX.extend & 0x00008000) == 0x00008000){
    ID_EX.extend += 0xffff0000; //extend negitive
  }

  //Set ID/EX rt, and rd
  ID_EX.rt = reg2;
  ID_EX.rd = (IF_ID.inst & 0x0000f800) >> 11;

  //get control signal
  ID_EX.ctrl = control_signal(IF_ID.inst);
}

//Execution tasks
void EX_Stage(){
  //calculate branch address
  EX_MEM.btgt = ID_EX.pc4 + (ID_EX.extend << 2);

  //get ALUSrc value
  int ALUSrc = ((ID_EX.ctrl >> 9) & 1) == 1 ? ID_EX.extend : ID_EX.rd2;

  //get func
  int func = ID_EX.extend & 0x0000003f;

  //Preform appropreate ALU operation
  //Check ALUOp and func code
  //Add
  if( ( ((ID_EX.ctrl >> 2) & 3) == 0) || (( (ID_EX.ctrl >> 3) & 1) && func == 32) ){
    EX_MEM.ALUOut = ID_EX.rd1 + ALUSrc;
    printf("Option 0: %d\n", ALUSrc);
  }
  //Subtract
  else if( (((ID_EX.ctrl >> 2) & 3) == 1) || (( (ID_EX.ctrl >> 3) & 1) && func == 34) ){
    EX_MEM.ALUOut = ID_EX.rd1 - ALUSrc;
    printf("Option 1: %d\n", ALUSrc);
  }
  //Set less than
  else if(func == 42){
    EX_MEM.ALUOut = ID_EX.rd1 - ALUSrc < 0 ? 1 : 0;
    printf("Option 2: %d\n", ALUSrc);
  }

  //Set zero
  EX_MEM.zero = EX_MEM.ALUOut == 0 ? 1 : 0;

  //move rd2 down the pipeline
  EX_MEM.rd2 = ID_EX.rd2;

  //get RegRd
  EX_MEM.RegRd = ((ID_EX.ctrl >> 10) & 1) == 0 ? ID_EX.rt : ID_EX.rd;

  //Move Ctrl down the pipeline
  EX_MEM.ctrl = ID_EX.ctrl;
}

//Memory Tasks
void MEM_Stage(){

}

//Write Back tasks
void WB_Stage(){

}

int main(int argc, char *argv[]){
  if (argc != 2){
    printf("Must include input file only.");
    exit(0);
  }

  //initialize registers and memory
  init(argv[1]);

  int to_halt = 15; //place holder for better implementation
  while(to_halt-- > 0){
    reg[4] = 6;
    reg[31] = 45;
    IF_Stage();
    ID_Stage();
    EX_Stage();
    print_result();
  }
}
