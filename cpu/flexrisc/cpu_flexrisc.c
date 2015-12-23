#include "../../bccx.h"
#include "cpu_flexrisc.h"

extern FILE* g_af;

// register definitions
//
REGREC g_gpregs[CPU_NUM_REGS] =
{
	{
		rACC,	"A",
		"%a1",	"%a",	NULL,	NULL
	},
	{
		rGPR,	"B",
		"%b1",	"%b",	NULL,	NULL
	},
#if CPU_NUM_REGS > 2
	{
		rGPR,	"C",
		"%c1",	"%c",	NULL,	NULL
	},
#endif
#if CPU_NUM_REGS > 3
	{
		rGPR,	"D",
		"%d1",	"%d",	NULL,	NULL
	},
#endif
};

REGREC g_bpreg =
	{
		rSP,	"BP",
		NULL,	"%d",	NULL,	NULL
	};

REGREC g_spreg =
	{
		rSP,	"SP",
		NULL,	"sp",	NULL,	NULL
	};

static int	g_localloc;
static int	g_tmpalloc;
static int	g_tmplevel;

static int	g_argn;

//***********************************************************************
PREGREC CODEgetRegister(int index)
{
	if(index < 0 || index >= CPU_NUM_REGS)
	{
		return NULL;
	}
	return &g_gpregs[index];
}

//***********************************************************************
int CODEgenProlog(PCCTX px, int first)
{
	// first means "pre output"
	if(! first)
	{
		g_localloc		  = 0;
		g_tmpalloc		  = 0;
		g_tmplevel		  = 0;
		g_argn 			  = 0;
	}
	return GENERICgenProlog(px, first);
}

//***********************************************************************
OPCODE CPUopCode(OPTYPE op, PSYMREF psymr)
{
	switch(op)
	{
	case opBOOLOR:
		return iOR;
	case opBOOLAND:
		return iAND;
	case opBITOR:
		return iOR;
	case opBITXOR:
		return iXOR;
	case opBITAND:
		return iAND;
	case opPLUS:
		return iADD;
	case opMINUS:
		return iSBC;
	case opMUL:
		return iMUL;
	case opPREINC:
		return iINC;
	case opPREDEC:
		return iDEC;
	case opDEREF:
		return iLDAT;
	default:
		Log(logError, 0, "No opcode for op %s\n", OPname(op));
		return iUNDEF;
	}
}

//***********************************************************************
const char* CODEopStr(OPCODE code, int size)
{
	static char rb[32];
	char* cn;
	
	switch(code)
	{
	case iLD:		cn = "ld";		break;
	case iLDAT:		cn = "ld";		break;
	case iST:		cn = "st";		break;
	case iSTAT:		cn = "st";		break;
	case iXCHG:		cn = "xchg";	break;
	case iADD:		cn = "add";		break;
	case iSUB:		cn = "sub";		break;
	case iOR:		cn = "or";		break;
	case iXOR:		cn = "xor";		break;
	case iAND:		cn = "and";		break;
	case iROL:		cn = "rol";		break;
	case iROR:		cn = "ror";		break;
	case iCMP:		cn = "cmp";		break;
	case iBRC:		cn = "brc";		break;
	case iBRS:		cn = "brs";		break;
	case iINC:		cn = "inc";		break;
	case iDEC:		cn = "dec";		break;
	case iMUL:		cn = "imul";	break;
	case iPUSH:		cn = "push";	break;
	case iPOP:		cn = "pop";		break;
	case iTSTOR:	cn = "st";		break;
	case iTLOAD:	cn = "ld";		break;
	case iJMP:		cn = "jmp";		break;
	case iJSR:		cn = "jsr";		break;
	case iRTS:		cn = "rts";		break;
	case iUNDEF:	cn = "!undef!";	break;
	default:		cn = "bad!";	break;
	}
	return cn;
}

//***********************************************************************
const char* CPUjmpCode(CODETYPE code, OPTYPE op)
{
	char* opname = NULL;
	
	if(code == codeBEQ)
	{
		// jump on compare == 0 (false)
		//
		switch(op)
		{
		case opBOOLEQ:	opname	= "jne";	break;
		case opBOOLNEQ:	opname	= "je";		break;
		case opBOOLLT:	opname	= "jge";	break;
		case opBOOLGT:	opname	= "jle";	break;
		case opBOOLLTEQ:opname	= "jg";		break;
		case opBOOLGTEQ:opname	= "jl";		break;
		}
	}
	else if(code == codeBNE)
	{
		// jump on compare == 1 (true)
		//
		switch(op)
		{
		case opBOOLEQ:	opname	= "jeq";	break;
		case opBOOLNEQ:	opname	= "jne";	break;
		case opBOOLLT:	opname	= "jl";		break;
		case opBOOLGT:	opname	= "jg";		break;
		case opBOOLLTEQ:opname	= "jle";	break;
		case opBOOLGTEQ:opname	= "jge";	break;
		}
	}
	else
	{
		Log(logError, 2, "Internal - bad code for jmp type\n");
	}
	if(! opname) opname = "bad";
	
	return opname;
}

//***********************************************************************
const char* CPUregCode(PREGREC reg, int bytesize)
{
	switch(bytesize)
	{
	case 1:
		return reg->name8;
	case 2:
		return reg->name16;
	default:
		Log(logError, 2, "Internal - bad reg size\n");
		return "bad";
	}
}

//***********************************************************************
const char* CPUsymName(PSYMREF psymr, char* nb)
{
	if(! psymr || ! nb)
	{
		return "badoperand";
	}
	if(psymr->desc.isauto)
	{
		int symoff = psymr->psym->offset;

		// its a local or argument
		//
		if(symoff < 0)
		{
			// its an argument of this function, so find it above the stack frame,
			// not below. add 8 for/ the ebp and return address on the stack, but
			// only really for since arg0 has offset -1
			//
			symoff = g_localloc + 4 + (((CPU_ARG_ALIGN + 7) / 8) * -symoff);
		}
		snprintf(nb, 128, "%d(%s)", symoff - g_localloc, CPUregCode(&g_bpreg, 2));
		return nb;
	}
	else
	{
		if(psymr->desc.islit && ! psymr->desc.isdim)
		{
			// a literal, prepend a $ on it
			//
			snprintf(nb, 128, "#%s", psymr->name);
			return nb;
		}
		else
		{
			// regular symbol name, just refer to its name
			//
			if(! psymr->desc.istatic && CPU_PREPEND_UNDERSCORE)
			{
				snprintf(nb, 128, "_%s", psymr->name);
				return nb;
			}
			else
			{
				return psymr->name;
			}
		}
	}
}

//***********************************************************************
PASM CODEgenerate(
				PCCTX		px,
				PFUNCTION	pf,
				CODETYPE	type,
				OPTYPE		op,
				PREGREC		pra,	PREGREC	prb,
				PSYMREF		psa,	PSYMREF	psb
				)
{
	PASM pasm;

	// generate an aseembly instruction, or sequence of instructions for the
	// "C" operation op, or psuedo (alloc) operation op.  Mostly this just
	// substitues the appropriate opcode for the op and the real formatting is
	// done in the output stage
	//
	Log(logDebug, 9, "GEN type=%s %s\n", FUNCcodename(type),
			type == codeINSN ? OPname(op) : (psa ? psa->name : psb ? psb->name : ""));

	pasm = PASMcreate(type, iUNDEF, pra, prb, psa, psb);
	
	switch(type)
	{
	case codeINSN:		// an actual operation
		switch(op)
		{
		// these we ignore, delete the pasm
		case opSTATEMENT:
			PASMdestroy(pasm);
			return NULL;

		// these should never get here since func/opstack expand or eval them
		case opCOMMA:
		case opEQUAL:
		case opPOSTINC:
		case opPOSTDEC:
		case opCAST:
		case opSIZEOF:
		// these don't make it past function stack processing 
		// and are converted to loadat/storeat/loada
		case opINDIRECT:
		case opADDROF:
			Log(logError, 0, "Unexpected op %s in code gen\n", OPname(op));
			PASMdestroy(pasm);
			return NULL;
		
		case opINDEX:
			pasm->opcode = iADD;
			break;
			
		// bool or and and need to be special since 2 && 1 == 1 (for example)
		case opBOOLOR:
		case opBOOLAND:
			pasm->opcode = CPUopCode(op, pasm->psa);
			break;

		// sets condition codes (compares) and saves result in register
		case opBOOLEQ:
		case opBOOLNEQ:
		case opBOOLLT:
		case opBOOLGT:
		case opBOOLLTEQ:
		case opBOOLGTEQ:
			// emit the compare
			pasm->opcode = iCMP;
			// store the result of the compare in the dest reg
			pasm->next = PASMcreate(codeINSN, CPUopCode(op, pasm->psa), pasm->pra, NULL, pasm->psa, NULL);
			break;
		
		// normal math
		case opBITOR:
		case opBITXOR:
		case opBITAND:
		case opPLUS:
		case opMINUS:
		// inc/dec (opstack added a separate == for them)
		case opPREINC:
		case opPREDEC:
		// pointer deref
		case opDEREF:
			pasm->opcode = CPUopCode(op, pasm->psa);
			break;
			
		// math that needs help
		case opMUL:
		case opSHIFTL:
		case opSHIFTR:
		case opDIV:
		case opMOD:
		case opBOOLNOT:
		case opBITINVERT:
		case opNEGATE:
			break;
			
		// return just means make sure result is in return register before jump to eof
		case opRETURN:
			if(pasm->pra)
			{
				PREGREC prr = &g_gpregs[CPU_FUNCTION_RET_REGISTER];

				if(pasm->pra != prr)
				{
					pasm->opcode = iMOV;
					pasm->prb    = pasm->pra;
					pasm->pra    = prr;
					pasm->psb	 = pasm->psa;
					pasm->psa	 = NULL;
				}
				else
				{
					// return reg is ok, no nead for this emit entry
					PASMdestroy(pasm);
					return NULL;
				}
			}
			else
			{
				Log(logError, 0, "No operand to return\n");
			}
			break;

		case opCALL:
			pasm->opcode = iCALL;
			// get rid of reg to force use of label
			pasm->pra = NULL;
			
			// if there is more than 2 args, restore stack
			if(g_argn > 2)
			{
				PSYMREF psb;
				char argbytes[32];
				
				snprintf(argbytes, 32, "%d", (g_argn - 2) * ((CPU_ARG_ALIGN + 7) / 8));
				g_argn = 0;

				psb = SYMREFcreate(GetIntTypeForSize(CPU_INT_SIZE));
				SYMREFrename(psb, argbytes);
				psb->desc.islit = 1;
				psb->type = psb->psym;
				pasm->next = PASMcreate(codeINSN, iADD, &g_spreg, NULL, NULL, psb);
			}
			break;
			
		// compare the reg result to zero / nonzero setting condition codes
		case opTEST:
			pasm->opcode = CPUopCode(opTEST, psa);
			break;
			
		case opSDEREF:
		case opPDEREF:
			break;
			
		case opPROMOTE2UNSIGNED:
			break;
		case opPROMOTEUCHAR2UINT:
		case opPROMOTECHAR2INT:
		case opPROMOTEUSHORT2UINT:
		case opPROMOTESHORT2INT:
			// shouldn't get here, but its a noop anyway
			PASMdestroy(pasm);
			break;
			
		case opPROMOTEINT2LONG:
		case opPROMOTEUINT2ULONG:
			// make a byte into a long.  since the flexrisc uses
			// the UPPER byte as the byte part, shift it down 8 bits
			// and fill the upper byte with 0xff (signed and bit 7 set)
			// or with 0x00 (unsigned or bit7 clear)  operand has to
			// be in the accumulator
			//
			if(! pasm->pra || pasm->pra->type != rACC)
			{
				Log(logError, 0, "Promotee not in accumulator\n");
				PASMdestroy(pasm);
				return NULL;
			}
			// write out program, first, change this to test bit 7
			pasm->opcode = iTSTB;
			
			// fill in code from "promote"
			PASMprogram(pasm, promote, pasm);
			break;
			
		case opPROMOTELONG2LONGLONG:
		case opPROMOTEULONG2ULONGLONG:
		case opPROMOTEFLT2DBL:
			// unhandled promotions, there is no long-long type
			Log(logError, 0, "Unhandled promotion %s\n", OPname(op));
			PASMdestroy(pasm);
			return NULL;

		default:
			Log(logError, 0, "Internal - unhandled C op %s\n", OPname(op));
		}
		break;
		
	case codeMOVE:		// ra <- rb
	case codeSTORE:		// psa or ra <- rb
	case codeLOAD:		// ra <- psa
	case codeSTOREAT:	// (ra) <- rb
		pasm->opcode = iMOV;
		break;
		
	case codeARG:		// make an argument from ra/psa
		pasm->type   = codeINSN;
		pasm->opcode = iPUSH;
		g_argn++;
		break;

	case codeTSTORE:	// push ra
		pasm->opcode = iTSTOR;
		break;

	case codeTLOAD:		// pop ra
		pasm->opcode = iTLOAD;
		break;

	case codeLOADADDR:	// ra <- &psa
		break;

	case codeSWAP:		// ra <-> rb
		break;

	case codeBR:		// goto psa
		pasm->opcode = iJMP;
		break;

	case codeBEQ:		// goto psa if condition == 0
		pasm->opcode = iJMP;
		break;

	case codeBNE:		// goto psa if condition != 0
		pasm->opcode = iJMP;
		break;
		
	// these following are generic to the asm format, not the cpu
	// so just defer to the generic code generator
	//
	case codeLOCAL:		// allocate stack space
		if(op == opEQUAL)
			g_tmpalloc = pasm->psa->psym->offset;
		else
			g_localloc = pasm->psa->psym->offset;
		// FALL into generic handler
	case codeLABEL:		// label:
	case codePUBLIC:	// public
	case codeGLOBAL:	// allocate for var
	case codeEXTERN:	// extern
	case codeEOF:		// end of function 
	case codeCOMMENT:	// comment in psa
	case codeDEBUG:		// debug info in psa
		
		return GENERICgenerate(px, pf, pasm);
		break;
		
	default:
		
		Log(logError, 0, "Internal - unhandled code\n");
		break;
	}
	return pasm;
}

//***********************************************************************
int CODEoutput(PCCTX px, PFUNCTION pf)
{
	PASM pasm;
	const char* pand1, *pand2;
	char nbufa[128], nbufb[128];
	int  opsize, dstsize, rv;
	
	pasm = pf->pemit;
	
	g_argn     = 0;
	
	// stack-temp vars start below locals on the frame
	g_tmplevel = g_localloc;
	
	while(pasm)
	{
		Log(logDebug, 9, "OUT type=%s %s\n", FUNCcodename(pasm->type),
				pasm->type == codeINSN ?
					CODEopStr(pasm->opcode, -1)
				:	(pasm->psa ? pasm->psa->name : pasm->psb ? pasm->psb->name : ""));
	
		switch(pasm->opcode)
		{
		// binary math operations ra = ra OP rb
		case iADD:
		case iSUB:
		case iOR:
		case iXOR:
		case iAND:
		case iSAL:
		case iSAR:
		case iSLL:
		case iSLR:
		case iCMP:
		case iMUL:
		case iDIV:
			opsize = SYMREFgetSize(pasm->psa ? pasm->psa : pasm->psb);
			if(pasm->pra)
			{
				pand1 = CPUregCode(pasm->pra, opsize);
			}
			else if(pasm->psa)
			{
				pand1 = CPUsymName(pasm->psa, nbufa);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			if(pasm->prb)
			{
				pand2 = CPUregCode(pasm->prb, opsize);
			}
			else if(pasm->psb)
			{
				pand2 = CPUsymName(pasm->psb, nbufb);
			}
			else
			{
				Log(logError, 0, "Missing operand B\n");
				return 3;
			}
			fprintf(g_af, "\t%-6s\t%s, %s\n", CODEopStr(pasm->opcode, opsize), pand2, pand1);
			break;
			
		// unary math operations
		case iINC:
		case iDEC:
		case iNOT:
		case iNEG:
		case iCALL:
		// push and pop
		case iPUSH:
		case iPOP:
		// branch
		case iJMP:
		case iJEQ:
		case iJNE:
			if(pasm->opcode == iPUSH || pasm->opcode == iPOP)
				opsize = CPU_LONG_SIZE >> 3; // stack is int aligned
			else
				opsize = SYMREFgetSize(pasm->psa);
			if(pasm->pra)
			{
				pand1 = CPUregCode(pasm->pra, opsize);
			}
			else if(pasm->psa)
			{
				pand1 = CPUsymName(pasm->psa, nbufa);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			fprintf(g_af, "\t%-6s\t%s\n", CODEopStr(pasm->opcode, opsize), pand1);
			break;

		// set from condition
		case iSETE:
		case iSETNE:
		case iSETLT:
		case iSETGT:
		case iSETLE:
		case iSETGE:
			if(pasm->pra)
			{
				pand1 = CPUregCode(pasm->pra, 1);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			fprintf(g_af, "\t%-6s\t%s\n", CODEopStr(pasm->opcode, opsize), pand1);
			break;

		// return
		case iRET:
			fprintf(g_af, "\t%-6s\n", CODEopStr(pasm->opcode, opsize));
			break;

		// test byte
		case iTSTB:
			pand1 = pasm->pra ? CPUregCode(pasm->pra, 1) : NULL;
			if(! pand1)
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			fprintf(g_af, "\t%-6s\t%s, %s\n", CODEopStr(pasm->opcode, opsize), pand1, pand1);
			break;

		// load address (always loads cpu address size
		case iLEA:
		// moving
		case iMOV:
		// swapping
		case iXCHG:
			if(pasm->opcode == iLEA)
			{
				opsize = dstsize = (CPU_ADDRESS_SIZE >> 3);
			}
			else if(pasm->type == codeSTOREAT)
			{
				dstsize = CPU_ADDRESS_SIZE >> 3;
				
				if(pasm->psa->desc.isptr == 1)
					opsize = SYMgetSize(pasm->psa ? pasm->psa->type : 0);
				else
					opsize = dstsize;
			}
			else
			{
				opsize = dstsize = SYMREFgetSize(pasm->psa ? pasm->psa : pasm->psb);
			}
			if(pasm->pra)
			{
				pand1 = CPUregCode(pasm->pra, dstsize);
			}
			else if(pasm->psa)
			{
				pand1 = CPUsymName(pasm->psa, nbufa);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			if(pasm->prb)
			{
				pand2 = CPUregCode(pasm->prb, opsize);
			}
			else if(pasm->psb)
			{
				pand2 = CPUsymName(pasm->psb, nbufb);
			}
			else
			{
				Log(logError, 0, "Missing operand B\n");
				return 3;
			}
			if(pasm->type == codeSTOREAT)
			{
				fprintf(g_af, "\t%-6s\t%s, (%s)\n", CODEopStr(pasm->opcode, opsize), pand2, pand1);
			}
			else
			{
				fprintf(g_af, "\t%-6s\t%s, %s\n", CODEopStr(pasm->opcode, opsize), pand2, pand1);
			}
			break;

		// tmpstore (push)
		case iTSTOR:
			opsize = dstsize = (CPU_ADDRESS_SIZE >> 3);
			// stores reg prb at psa, psa is an offset in frame
			if(! pasm->prb || ! pasm->psa)
			{
				Log(logError, 0, "Missing operand A TSTORE\n");
				return 2;
			}
			g_tmplevel += (CPU_ADDRESS_SIZE >> 3);
			fprintf(g_af, "\t%-6s\t%s, %d(%%ebp)\n", CODEopStr(pasm->opcode, opsize),
					CPUregCode(pasm->prb, opsize), -g_tmplevel);
			break;
			
		// tmpload (pop)
		case iTLOAD:
			opsize = dstsize = (CPU_ADDRESS_SIZE >> 3);
			// stores psb into pra, psb is an offset in frame
			if(! pasm->pra || ! pasm->psb)
			{
				Log(logError, 0, "Missing operand A TSTORE\n");
				return 2;
			}
			fprintf(g_af, "\t%-6s\t%d(%%ebp), %s\n", CODEopStr(pasm->opcode, opsize),
					-g_tmplevel, CPUregCode(pasm->pra, opsize));
			g_tmplevel -= (CPU_ADDRESS_SIZE >> 3);
			break;
			
		// dereferencing
		case iMOVAT:
			if(pasm->pra && pasm->psa)
			{
				if(pasm->psa->desc.isptr)
				{
					if(pasm->psa->desc.isptr > 1)
					{
						opsize = CPU_ADDRESS_SIZE >> 3;
					}
					else
					{
						opsize = SYMgetSize(pasm->psa->type);
					}
				}
				else
				{
					Log(logError, 0, "Attempt to dereference non-pointer type\n");
					return 1;
				}
				pand2 = CPUregCode(pasm->pra, (CPU_ADDRESS_SIZE >> 3));
				pand1 = CPUregCode(pasm->pra, opsize);
				fprintf(g_af, "\t%-6s\t(%s), %s\n", CODEopStr(pasm->opcode, opsize), pand2, pand1);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			break;
			
		// promotions
		case iMOVSBL:
		case iMOVSWL:
		case iMOVZBL:
		case iMOVZWL:
			opsize = (pasm->opcode == iMOVSBL || pasm->opcode == iMOVZBL) ? 1 : 2;
			if(pasm->pra)
			{
				pand2 = CPUregCode(pasm->pra, opsize);
				pand1 = CPUregCode(pasm->pra, 4);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			fprintf(g_af, "\t%-6s\t%s, %s\n", CODEopStr(pasm->opcode, opsize), pand2, pand1);
			break;
			
		// psudeo ops
		case iUNDEF:			
			switch(pasm->type)
			{
			// these following are generic to the asm format, not the cpu
			// so just defer to the generic code output
			//
			case codeLABEL:		// label:
			case codePUBLIC:	// public
			case codeGLOBAL:	// allocate for var
			case codeEXTERN:	// extern
			case codeLOCAL:		// allocate stack space
			case codeCOMMENT:	// comment in psa
			case codeDEBUG:		// debug info in psa			
				rv = GENERICoutput(px, pf, pasm);
				if(rv) return rv;
				break;

			case codeARG:
				g_argn++;
				break;

			// call generic to handle stack restore and add return
			case codeEOF:
				rv = GENERICoutput(px, pf, pasm);
				if(rv) return rv;
				fprintf(g_af, "\t%-6s\n", CODEopStr(iRET, 4));
				break;

			default:
				Log(logError, 0, "Internal - unhandled code in output\n");
				return 1;
			}
			break;
			
		default:
			Log(logError, 0, "Internal - unhandled CPU opcode\n", CODEopStr(pasm->opcode, -1));
			return 1;
		}
		if(pasm)
		{
			pasm = pasm->next;
		}
	}
	return 0;
}

//***********************************************************************
extern int CODEgenEpilog(PCCTX px, int last)
{
	return GENERICgenEpilog(px, last);
}


