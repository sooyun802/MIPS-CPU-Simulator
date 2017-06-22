#include "cpu.h"
#include "stages.h"
#include "instruction.h"

#include <stdio.h>

int num_branches;
int num_mispredictions;
int *BHT;
// the three operands are encoded inside a single uint16_t. This method cracks them open
static void inline decode_ops(uint16_t input, byte *dest, byte *src1, byte *src2)
{
	*dest = input & 0x001F;
	*src1 = (input & 0x03E0) >> 5;
	*src2 = (input & 0x7C00) >> 10;
}

static void inline jump_to(uint32_t* pc, uint32_t immediate) {
  if(immediate < 0x00400000)
    *pc = 0x00400000 + immediate; // load the target into the PC.
  else
    *pc = immediate;
}

/**
   The execute implementations. These actually perform the action of the stage.
 **/

// IF Stage ---------------------------------------------------------------
// Instructions are fetched from memory in this stage, and passed into the CPU's ID latch
void InstructionFetchStage::Execute()
{
  // Input of the program
  int type_branch_predictor = core->type_branch_predictor; // Type of branch predictor (1: 1-bit, 2: 2-bit)
  int num_bht_entries = core->num_bht_entries; // The number of BHT entries
  //---------------------
  
	right.opcode = core->mem->get<byte>(core->PC);
	uint16_t operands = core->mem->get<uint16_t>(core->PC + 1);
	decode_ops(operands, &right.Rdest, &right.Rsrc1, &right.Rsrc2);
	right.immediate = core->mem->get<uint32_t>(core->PC + 3);
	core->PC += 8;
	right.PC = core->PC;

  // Branch prediction ----------------------------------------------------
  // Static branch prediction (Always Not Taken)
	//right.predict_taken = false;
	if(right.opcode>=2 && right.opcode<=4) {
		if(type_branch_predictor==1) {
			switch(BHT[(right.PC>>2)%num_bht_entries]) {
				case 0:
					right.predict_taken=false;
					break;
				case 1:
					right.predict_taken=true;
					break;
			}
		}
		else if(type_branch_predictor==2) {
			switch(BHT[(right.PC>>2)%num_bht_entries]) {
				case 0:
				case 1:
					right.predict_taken=false;
					break;
				case 2:
				case 3:
					right.predict_taken=true;
					break;
			}
		}
	}
  // ----------------------------------------------------------------------
}


// ID Stage ---------------------------------------------------------------
void InstructionDecodeStage::Execute()
{
  // Input of the program
  int type_branch_predictor = core->type_branch_predictor; // Type of branch predictor (1: 1-bit, 2: 2-bit)
  int num_bht_entries = core->num_bht_entries; // The number of BHT entries
  //---------------------
  
	core->registers[0] = 0; // wire register 0 to zero for all register reads
	right.Rsrc1Val = *(int32_t *)&core->registers[left.Rsrc1];
	right.Rsrc2Val = *(int32_t *)&core->registers[left.Rsrc2];
	right.immediate = left.immediate;
	right.Rsrc1 = left.Rsrc1;
	right.Rsrc2 = left.Rsrc2;
	right.Rdest = left.Rdest;
	right.PC = left.PC;
	right.opcode = left.opcode;
	right.predict_taken = left.predict_taken;
}


// EXE Stage --------------------------------------------------------------
void ExecuteStage::Execute()
{
  // Inputs ---------------------------------------------------------------
  // Values transmitted from left
  // * left.opcode
  // * left.Rsrc1Val
  // * left.Rsrc2Val
  // * left.immediate
  // * left.PC
  // * left.predict_taken

  // Decoded instruction (decoded_inst)
  const instruction* decoded_inst;
  decoded_inst = left.control();

  // Source value (svalue)
	int32_t svalue = left.Rsrc1Val;

  // Source parameter (sparam)
  uint32_t param;
	switch (decoded_inst->alu_source) {
	case 0: // source from register
		param = left.Rsrc2Val;
		break;

	case 1: // immediate add/sub
		param = left.immediate;
		break;

	case 2: // address calculation
		param = left.immediate + *(uint32_t *)&left.Rsrc1Val;
		break;
	}
	int32_t sparam = *(int32_t *)&param;

  // Input of the program
  int type_branch_predictor = core->type_branch_predictor; // Type of branch predictor (1: 1-bit, 2: 2-bit)
  int num_bht_entries = core->num_bht_entries; // The number of BHT entries
  //---------------------
  
  // ----------------------------------------------------------------------
  
  // Result (result)
	int32_t result = sparam;

  // ALU Execution --------------------------------------------------------
	switch (decoded_inst->alu_operation) {
	case 0:
		// do nothing, this operation does not require an alu op (copy forward)
		break;

	case 1:
		// do a signed add of reg1 to param
		result = svalue + sparam;
		break;

	case 2:
		// do a signed subtract of param from reg1
		result = svalue - sparam;
		break;
	}
  // ----------------------------------------------------------------------

  // Branch Execution -----------------------------------------------------
  bool taken = 0;
	if (decoded_inst->branch) {
		num_branches++;
		bool taken = (left.opcode == 2 && left.Rsrc1Val == 0) ||
		             (left.opcode == 3 && left.Rsrc1Val >= left.Rsrc2Val) ||
		             (left.opcode == 4 && left.Rsrc1Val != left.Rsrc2Val);
		// if mispredict, nop out IF and ID. (mispredict == prediction and taken differ)
    if(core->verbose) {
      if(taken == 1) {
        printf("*** Branch taken: %d %d\n", left.Rsrc1Val, left.Rsrc2Val);
      } else {
        printf("*** Branch not taken: %d %d\n", left.Rsrc1Val, left.Rsrc2Val);
      }
    }

    if (left.predict_taken != taken) {
      if(core->verbose) {    
        printf("*** MISPREDICT!\n");
      }
    }
    if (taken) {
      core->ifs.make_nop();
      core->ids.make_nop();
      if(type_branch_predictor==1) {
      	if(BHT[(left.PC>>2)%num_bht_entries]==0) num_mispredictions++;
      	BHT[(left.PC>>2)%num_bht_entries]=1;
      }
      else if(type_branch_predictor==2) {
      	if(BHT[(left.PC>>2)%num_bht_entries]<=1) num_mispredictions++;
      	if(BHT[(left.PC>>2)%num_bht_entries]<3) BHT[(left.PC>>2)%num_bht_entries]+=1;
      }
      jump_to(&core->PC, left.immediate);
    }
    else {
    	 if(type_branch_predictor==1) {
    	 	if(BHT[(left.PC>>2)%num_bht_entries]==1) num_mispredictions++;
    	 	BHT[(left.PC>>2)%num_bht_entries]=0;
    	 }
    	 else if(type_branch_predictor==2) {
    	 	if(BHT[(left.PC>>2)%num_bht_entries]>=2) num_mispredictions++;
    	 	if(BHT[(left.PC>>2)%num_bht_entries]>0) BHT[(left.PC>>2)%num_bht_entries]-=1;
    	 }
    }
  }
  // ----------------------------------------------------------------------

  right.aluresult = *(uint32_t *)&result;

  // Copy forward from previous latch -------------------------------------
  right.opcode = left.opcode;
  right.Rsrc1 = left.Rsrc1;
  right.Rsrc2 = left.Rsrc2;
  right.Rsrc1Val = left.Rsrc1Val;
  right.Rsrc2Val = left.Rsrc2Val;
  right.Rdest = left.Rdest;
  // ----------------------------------------------------------------------
}

// MEM Stage --------------------------------------------------------------

void MemoryStage::Execute()
{
	const instruction *control = left.control();

	right.aluresult = left.aluresult;
	right.mem_data = 0;
	if (control->mem_read) {
		if (control->mem_read == 1) {
			right.mem_data = core->mem->get<byte>(left.aluresult);
		}
		else if (control->mem_read == 4) {
			right.mem_data = core->mem->get<uint32_t>(left.aluresult);
		}
	}
	else if (control->mem_write) {
		if (control->mem_write == 1) {
			core->mem->set<byte>(left.aluresult, left.Rsrc2Val);
		}
		else if (control->mem_write == 4) {
			core->mem->set<uint32_t>(left.aluresult, left.Rsrc2Val);
		}
	}
	right.opcode = left.opcode;
	right.Rsrc1Val = left.Rsrc1Val;
	right.Rsrc2Val = left.Rsrc2Val;
	right.Rdest = left.Rdest;
}


// WB Stage ---------------------------------------------------------------
void WriteBackStage::Execute()
{
	const instruction *control = left.control();

	if (control->special_case != NULL) {
		control->special_case(core);
	}
	if (control->register_write) {
		if (control->mem_to_register) {
			// write the mem_data into register Rdest
			core->registers[left.Rdest] = left.mem_data;
		}
		else {
			// write the alu result into register Rdest
			core->registers[left.Rdest] = left.aluresult;
		}
		core->registers[0] = 0; // wire back to zero
	}
}


void InstructionDecodeStage::Shift()
{
	left = core->ifs.right;
}


void ExecuteStage::Shift()
{
	left = core->ids.right;
}


void MemoryStage::Shift()
{
	left = core->exs.right;
}


void WriteBackStage::Shift()
{
	left = core->mys.right;
}


void InstructionDecodeStage::Resolve()
{
	// Check for a data stall. If found, back up the machine one cycle. -----
	if (core->ids.right.control()->mem_read &&
	    (core->ids.right.Rdest == core->ifs.right.Rsrc1 || 
       core->ids.right.Rdest == core->ifs.right.Rsrc2)) {
		// Bubble the pipe!
		// nop the two stalled instructions, back up the pc by 2 instructions
    if(core->verbose) {
      printf("*** BUBBLE\n");
    }
		core->ifs.make_nop();
		core->PC -= 8;
	}
  // ----------------------------------------------------------------------
}


void ExecuteStage::Resolve()
{
	// If the previous instruction is attempting a READ of the same register the instruction
	// in this stage is supposed to WRITE, then here, update the previous stage's right latch
	// with the value coming out of the ALU. (also, not reading zero reg)
	if (core->exs.right.Rdest != 0 && core->exs.right.control()->register_write) {
		if (core->exs.right.Rdest == core->ids.right.Rsrc1) {
			core->ids.right.Rsrc1Val = core->exs.right.aluresult;
      if(core->verbose) {
        printf("*** FORWARDex1:  %08x going to ID/EX's Rsrc1Val \n",
            core->exs.right.aluresult);
      }
		}
		if (core->exs.right.Rdest == core->ids.right.Rsrc2) {
			core->ids.right.Rsrc2Val = core->exs.right.aluresult;
      if(core->verbose) {
        printf("*** FORWARDex2:  %08x going to ID/EX's Rsrc2Val \n",
            core->exs.right.aluresult);
      }
		}
	}
}


void MemoryStage::Resolve()
{
	// If the next to previous instruction (IDS) is attempting a READ of the same register the instruction
	// in this stage is supposed to WRITE, then here, update the next-to-previous stage's right latch
  // with the value coming out of the this stage. (also, the read is not on zero)
  if (core->mys.right.Rdest != 0 && core->mys.right.control()->register_write) {
    if (core->exs.right.Rdest != core->ids.right.Rsrc1 &&
        core->mys.right.Rdest == core->ids.right.Rsrc1) {
      core->ids.right.Rsrc1Val =
        core->mys.right.control()->mem_read ? core->mys.right.mem_data : core->mys.right.aluresult;
      if(core->verbose) {
        printf("*** FORWARDmem1: %08x going to ID/EX's Rsrc1Val \n",
            core->ids.right.Rsrc1Val);
      }
    }
    if (core->exs.right.Rdest != core->ids.right.Rsrc2 &&
        core->mys.right.Rdest == core->ids.right.Rsrc2) {
      core->ids.right.Rsrc2Val =
        core->mys.right.control()->mem_read ? core->mys.right.mem_data : core->mys.right.aluresult;
      if(core->verbose) {
        printf("*** FORWARDmem2: %08x going to ID/EX's Rsrc2Val \n",
            core->ids.right.Rsrc2Val);
      }
    }
  }
}
