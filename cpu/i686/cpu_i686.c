#include "../../bccx.h"
#include "cpu_i686.h"

// register definitions
//
#if (CPU_NUM_REGS > 4)||(CPU_NUM_REGS < 2)
	hey, somethings wrong
#endif

REGREC g_gpregs[] =
{
	{
		rACC,	"A",
		"%al",	"%ax",	"%eax",	NULL
	},
	{
		rGPR,	"B",
		"%bl",	"%bx",	"%ebx",	NULL
	},
	{
		rGPR,	"C",
		"%cl",	"%cx",	"%ecx",	NULL
	},
	{
		rGPR,	"D",
		"%dl",	"%dx",	"%edx",	NULL
	},
	{
		rFP,	"BP",
		NULL,	NULL,	"%ebp",	NULL
	},
	{
		rSP,	"SP",
		NULL,	NULL,	"%esp",	NULL
	}
};

#define GPR_A	&g_gpregs[0]
#define GPR_B	&g_gpregs[1]
#define GPR_C	&g_gpregs[2]
#define GPR_D	&g_gpregs[3]
#define GPR_BP	&g_gpregs[4]
#define GPR_SP	&g_gpregs[5]

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
	case opBOOLEQ:
		return iSETE;
	case opBOOLNEQ:
		return iSETNE;
	case opBOOLLT:
		return iSETLT;
	case opBOOLGT:
		return iSETGT;
	case opBOOLLTEQ:
		return iSETLT;
	case opBOOLGTEQ:
		return iSETGE;
	case opBITOR:
		return iOR;
	case opBITXOR:
		return iXOR;
	case opBITAND:
		return iAND;
	case opSHIFTL:
		return psymr->desc.isuns ? iSLL : iSAL;
	case opSHIFTR:
		return psymr->desc.isuns ? iSLL : iSAL;
	case opADD:
		return iADD;
	case opMINUS:
		return iSUB;
	case opMUL:
		return iMUL;
	case opDIV:
		return iDIV;
	case opMOD:
		return iDIV;
	case opBOOLNOT:
		return iNOT;
	case opBITINVERT:
		return iNOT;
	case opOFFSET:
		return iADDOFF;
	case opPREINC:
	case opPOSTINC:
		return iINC;
	case opPREDEC:
	case opPOSTDEC:
		return iDEC;
	case opNEGATE:
		return iNEG;
	case opDEREF:
		return iMOV;
	case opTEST:
		return iTSTB;
	default:
		Log(logError, 0, "No opcode for op %s\n", OPname(op));
		return iUNDEF;
	}
}

//***********************************************************************
const char* CODEopStr(OPCODE code, int size)
{
	static char rb[32];
	char* cn, zc;
	
	switch(size)
	{
	case -1:
		zc = '\0';
		break;
	case 1:
		zc = 'b';
		break;
	case 2:
		zc = 'w';
		break;
	case 4:
		zc = 'l';
		break;
	case 8:
		zc = '?';
		break;
	default:
		zc = '?';
		break;
	}
	switch(code)
	{
	case iMOV:		cn = "mov";		break;
	case iMOVAT:	cn = "mov";		break;
	case iXCHG:		cn = "xchg";	break;
	case iLEA:		cn = "lea";		break;
	case iMOVSBL:	cn = "movsbl";	zc = '\0'; break;
	case iMOVSWL:	cn = "movswl";	zc = '\0'; break;
	case iMOVZBL:	cn = "movzbl";	zc = '\0'; break;
	case iMOVZWL:	cn = "movzwl";	zc = '\0'; break;
	case iADD:		cn = "add";		break;
	case iADDOFF:	cn = "add";		break;
	case iSUB:		cn = "sub";		break;
	case iOR:		cn = "or";		break;
	case iXOR:		cn = "xor";		break;
	case iAND:		cn = "and";		break;
	case iNOT:		cn = "not";		break;
	case iNEG:		cn = "neg";		break;
	case iSAL:		cn = "sal";		break;
	case iSAR:		cn = "sar";		break;
	case iSLL:		cn = "sll";		break;
	case iSLR:		cn = "slr";		break;
	case iCMP:		cn = "cmp";		break;
	case iTSTB:		cn = "testb";	zc = '\0'; break;
	case iSETE:		cn = "sete";	zc = '\0'; break;
	case iSETNE:	cn = "setne";	zc = '\0'; break;
	case iSETLT:	cn = "setl";	zc = '\0'; break;
	case iSETGT:	cn = "setg";	zc = '\0'; break;
	case iSETLE:	cn = "setle";	zc = '\0'; break;
	case iSETGE:	cn = "setge";	zc = '\0'; break;
	case iINC:		cn = "inc";		break;
	case iDEC:		cn = "dec";		break;
	case iMUL:		cn = "imul";	break;
	case iDIV:		cn = "idiv";	break;
	case iPUSH:		cn = "push";	break;
	case iPOP:		cn = "pop";		break;
	case iTSTOR:	cn = "mov";		break;
	case iTLOAD:	cn = "mov";		break;
	case iJMP:		cn = "jmp";		zc = '\0'; break;
	case iJEQ:		cn = "je";		zc = '\0'; break;
	case iJNE:		cn = "jne";		zc = '\0'; break;
	case iCALL:		cn = "call";	zc = '\0'; break;
	case iRET:		cn = "ret";		break;
	case iUNDEF:	cn = "!undef!";	break;
	default:		cn = "bad!";	break;
	}
	snprintf(rb, sizeof(rb), "%s%c", cn, zc);
	return rb;
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
const char* CPUregCode(PREGREC reg, int bytesize, int indirect)
{
	static char rname[32];
	const char* rn;

	if(indirect)
	{
		snprintf(rname, sizeof(rname), "(%s)", CPUregCode(reg, (CPU_ADDRESS_SIZE >> 3), 0));
		return rname;
	}
	switch(bytesize)
	{
	case 1:
		rn = reg->name8;
		break;
	case 2:
		rn = reg->name16;
		break;
	case 4:
		rn = reg->name32;
		break;
	case 8:
		rn = reg->name64;
		break;
	default:
		Log(logError, 2, "Internal - bad reg size\n");
		return "bad";
	}
	return rn;
}

//***********************************************************************
const char* CPUsymName(PSYMREF psymr, char* nb, int underscore_globals)
{
	if(! psymr || ! nb)
	{
		return "badoperand";
	}
	if(psymr->desc.isauto && ! psymr->desc.islit)
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
		snprintf(nb, 128, "%d(%s)", symoff - g_localloc, "%ebp");
		return nb;
	}
	else
	{
		if(psymr->desc.islit && ! (psymr->desc.isdim || psymr->desc.isptr))
		{
			PSYM  pv;
			char* sname;

			if(psymr->desc.isenum)
			{
				// get to lowest init in chain (enums initied with enums)
				//
				for(pv = psymr->psym->init; pv && pv->init;)
					pv = pv->init;
				if(! pv)
					pv = psymr->psym;
				sname = pv->name;
			}
			else
			{
				sname = psymr->name;
			}
			// a literal, prepend a $ on it
			snprintf(nb, 128, "$%s", sname);
			return nb;
		}
		else
		{
			// regular symbol name, just refer to its name
			//
			if(! psymr->desc.istatic && underscore_globals)
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

int CPUopSize(PSYMREF opand, int indirect)
{
	if(! opand)
	{
		return 0;
	}
	if(indirect)
	{
		if((opand->desc.isptr + (opand->desc.isdim ? 1 : 0)) == 1)
		{
			return SYMgetSizeBytes(SYMbaseType(opand->type));
		}
	}
	return SYMREFgetSizeBytes(opand);
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
	PASM porig;

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
		case opSIZEOF:
		// bool or, and, not all need to be special since 2 && 1 == 1 (for example)
		// and C says you cant eval the right side if its not needed, so these are
		// handled in the scope and should never get here
		case opBOOLOR:
		case opBOOLAND:
		case opBOOLNOT:
			break;

			Log(logError, 0, "Unexpected op %s in code gen\n", OPname(op));
			PASMdestroy(pasm);
			return NULL;
		
		case opINDEX:
			pasm->opcode = iADD;
			break;
			
		case opOFFSET:
			pasm->opcode = iADDOFF;
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
			// store the result of the compare in the dest reg using set[]
			pasm->next =       PASMcreate(codeINSN, CPUopCode(op, pasm->psa), pasm->pra, NULL, pasm->psa, NULL);
			// promote to int at least
			pasm->next->next = PASMcreate(codeINSN, iMOVZBL, pasm->pra, NULL, pasm->psa, NULL);
			break;
		
		// normal math
		case opBITOR:
		case opBITXOR:
		case opBITAND:
		case opADD:
		case opMINUS:
		case opMUL:
		case opBITINVERT:
		// inc/dec (opstack added a separate == for them)
		case opPREINC:
		case opPREDEC:
		case opPOSTINC:
		case opPOSTDEC:
		case opNEGATE:
			pasm->opcode = CPUopCode(op, pasm->psa);
			break;

		// shifting needs C
		case opSHIFTL:
		case opSHIFTR:
			pasm->opcode = CPUopCode(op, pasm->psa);
			// if shifter is a lit, its OK
			if(pasm->psb && pasm->psb->desc.islit)
				break;
			// point to original opcode
			porig = pasm;
			if(porig->prb != GPR_C)
			{
#if CPU_NUM_REGS > 2
				if(! REGreserve(px, pf, GPR_C))
					return NULL;
#endif
				pasm = PASMcreate(codeINSN, iMOV, GPR_C, pasm->prb, NULL, pasm->psb);
				pasm->next = porig;
				porig->prb = GPR_C;
#if CPU_NUM_REGS > 2
			REGunreserve(px, pf, GPR_C);
#endif
			}
			break;

		// these guys are special, they use reserved register
		case opDIV:
		case opMOD:
			// point to original code
			porig = pasm;
			pasm->opcode = CPUopCode(op, pasm->psa);

#if CPU_NUM_REGS > 3
			// reserve register D, unless that's the source operand
			if(pasm->pra != GPR_D)
			{
				if(! REGreserve(px, pf, GPR_D))
					return NULL;
			}
#endif
			// if the source register isn't A, reserve it and move src to A
			if(pasm->pra != GPR_A)
			{
				if(! REGreserve(px, pf, GPR_A))
					return NULL;

				// move pra to pA
				pasm = PASMcreate(codeINSN, iMOV, GPR_A, porig->pra, NULL, porig->psa);
				if(! pasm) return NULL;

				// this is where the acutal divide goes
				pasm->next = porig;

				// move pA or pD (the result) back to pra
				if(op == opMOD)
				{
					// D has remainder, so only mov if D not source
					if(porig->pra != GPR_D)
						porig->next = PASMcreate(codeINSN, iMOV, porig->pra, GPR_D, porig->psa, NULL);
				}
				else
				{
					// we already know here orig pra is NOT A, so always move
					porig->next = PASMcreate(codeINSN, iMOV, porig->pra, GPR_A, porig->psa, NULL);
				}
				REGunreserve(px, pf, GPR_A);
			}
			else
			{
				// for mod,
				if(op == opMOD)
				{
					// D has remainder, so only mov if D not source
					if(porig->pra != GPR_D)
						porig->next = PASMcreate(codeINSN, iMOV, porig->pra, GPR_D, porig->psa, NULL);
				}
			}
#if CPU_NUM_REGS > 3
			if(pasm->pra != GPR_D)
			{
				// unreserve D
				REGunreserve(px, pf, GPR_D);
			}
#endif
			break;

		// pointer deref, pointers are not fully dereferenced until they are used
		// in an operation, since the operation itself provides the context.
		// but only one level of deref is allowed, so a deref on something that
		// is already needing derefing gets a move here
		//
		case opDEREF:
			if(! pasm->psa || pasm->psa->name[1] != '*')
			{
				PASMdestroy(pasm);
				return NULL;
			}
			pasm->opcode = CPUopCode(op, pasm->psa);
			pasm->prb = pasm->pra;
			pasm->psb = SYMREFcreateCopy(pasm->psa);
			pasm->psa->name[1] = '@'; // to avoid deref of dest
			break;

		// this is a noop now, since it already told the load to load-effective-address
		case opADDROF:
		// this is a noop now too, since it just modified the type in the reg
		case opCAST:
			PASMdestroy(pasm);
			return NULL;

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
			if(g_argn != 0)
			{
				PSYMREF psb;
				char argbytes[32];
				
				snprintf(argbytes, 32, "%d", g_argn * ((CPU_ARG_ALIGN + 7) / 8));
				g_argn = 0;

				psb = SYMREFcreate(GetIntTypeForSize(CPU_INT_SIZE));
				SYMREFrename(psb, argbytes);
				psb->desc.islit = 1;
				psb->type = psb->psym;
				pasm->next = PASMcreate(codeINSN, iADD, GPR_SP, NULL, NULL, psb);
			}
			break;
			
		// compare the reg result to zero / nonzero setting condition codes
		case opTEST:
			// if the opcode before this was not a compare, have to compare to zero
			// opTEST means "result = (operand ? 1 : 0)"
			pasm->opcode = CPUopCode(opTEST, psa);
			porig = pasm;
			pasm = PASMcreate(codeINSN, iCMP, pra, NULL, psa, SYMREFcreate(g_zero));
			if(pasm)
				pasm->next = PASMcreate(codeINSN, iSETNE, pra, NULL, psa, NULL);
			if(pasm && pasm->next)
				pasm->next->next = porig;
			break;
			
		case opSDEREF:
		case opPDEREF:
			break;
			
		case opPROMOTE2UNSIGNED:
			break;
		case opPROMOTEUCHAR2UINT:
		case opPROMOTECHAR2INT:
			pasm->opcode = pasm->psa->desc.isuns ? iMOVZBL : iMOVSBL;
			break;
		case opPROMOTEUSHORT2UINT:
		case opPROMOTESHORT2INT:
			pasm->opcode = pasm->psa->desc.isuns ? iMOVZWL : iMOVSWL;
			break;
		case opPROMOTEINT2LONG:
		case opPROMOTEUINT2ULONG:
			// shouldn't get here, but its a noop anyway
			PASMdestroy(pasm);
			return NULL;
			
		case opPROMOTELONG2LONGLONG:
		case opPROMOTEULONG2ULONGLONG:
		case opPROMOTEFLT2DBL:
			// unhandled promotions
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
		pasm->opcode = iLEA;
		break;

	case codeSWAP:		// ra <-> rb
		pasm->opcode = iXCHG;
		break;

	case codeBR:		// goto psa
		pasm->opcode = iJMP;
		break;

	case codeBEQ:		// goto psa if condition == 0
		pasm->opcode = iJEQ;
		break;

	case codeBNE:		// goto psa if condition != 0
		pasm->opcode = iJNE;
		break;
		
	// these following are generic to the asm format, not the cpu
	// so just defer to the generic code generator
	//
	case codeLOCAL:		// allocate stack space
		return GENERICgenerate(px, pf, pasm);

	case codeLABEL:		// label:
	case codePUBLIC:	// public
	case codeGLOBAL:	// allocate for var
	case codeEXTERN:	// extern
	case codeEOF:		// end of function 
	case codeCOMMENT:	// comment in psa
	case codeDEBUG:		// debug info in psa
		return GENERICgenerate(px, pf, pasm);
		
	default:
		
		Log(logError, 0, "Internal - unhandled code\n");
		break;
	}
	return pasm;
}

//***********************************************************************
int CODEupdateDeref(PASM pasm)
{
	PASM pder;

	for(pder = pasm->next; pder; pder = pder->next)
	{
		// see if this pasm uses the register for a source
		//
		if(pder->prb == pasm->pra)
		{
			// binary op source current pasms ra, it better match opands
			//
			if(pder->psb && pasm->psa)
			{
				if(strcmp(pder->psb->name, pasm->psa->name))
				{
					break;
					//Log(logError, 0, "mismatched parm in reg %s\n", pasm->pra->name);
				}
				else
				{
					pder->psb->name[1] = '@';
				}
			}
		}
		else if(! pder->prb && ! pder->psb && (pder->pra == pasm->pra))
		{
			// unary op, and matches ra
			//
			if(pder->psa && pasm->psa)
			{
				if(strcmp(pder->psa->name, pasm->psa->name))
				{
					break;
					//Log(logError, 0, "mismatched parm in reg %s\n", pasm->pra->name);
				}
				else
				{
					pder->psa->name[1] = '@';
				}
			}
		}
		else if(pder->pra == pasm->pra)
		{
			// this operation sets the register, so its no longer 
			// loaded here with what we think, no need to go on
			//
			break;
		}
	}
	return 0; 
}

//***********************************************************************
int CODEoutput(PCCTX px, PFUNCTION pf)
{
	PASM pasm;
	const char* pand1, *pand2;
	char nbufa[MAX_TOKEN + 32], nbufb[MAX_TOKEN + 32];
	int  opsize, dstsize, rv;
	int  inda, indb;
	
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
	
		inda = indb = 0;
		if(pasm->psa)
		{
			inda = pasm->psa->name[1] == '*';
		}
		if(pasm->psb)
		{
			indb = pasm->psb->name[1] == '*';
		}
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
			opsize = dstsize = pasm->psa ?
					CPUopSize(pasm->psa, inda) : CPUopSize(pasm->psb, indb);

			// if the destination register (inda) is indirect,
			// load it first since ra = (ra) + rb is what we
			// want, but add rb,(ra) means (ra) = (ra) + rb, so
			// i preload ra.  That also fixes the case where
			// rb is indirect, since only one memop per insn is ok
			//
			if(inda && pasm->pra)
			{
				fprintf(px->af, "\t%-6s\t%s, %s\n", CODEopStr(iMOV, opsize),
					CPUregCode(pasm->pra, opsize, inda), CPUregCode(pasm->pra, opsize, 0));
				// change the '*' in the regs symbol to indicate its no longer 
				// needing to be dereferenced
				CODEupdateDeref(pasm);
				inda = 0;
			}
			if(pasm->pra)
			{
				pand1 = CPUregCode(pasm->pra, opsize, inda);
			}
			else if(pasm->psa)
			{
				pand1 = CPUsymName(pasm->psa, nbufa, px->underscore_globals);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			if(pasm->prb)
			{
				if(pasm->opcode == iSAL || pasm->opcode == iSAR ||
						pasm->opcode == iSLL || pasm->opcode == iSLR)
					pand2 = CPUregCode(pasm->prb, 1, 0);
				else
					pand2 = CPUregCode(pasm->prb, opsize, indb);
			}
			else if(pasm->psb)
			{
				pand2 = CPUsymName(pasm->psb, nbufb, px->underscore_globals);
			}
			else
			{
				Log(logError, 0, "Missing operand B\n");
				return 3;
			}
			if(pasm->opcode == iDIV)
			{
				// for divide instructions add a clear of regD (it was reserved)
				// and pand1 is loaded in A
				fprintf(px->af, "\t%-6s\n", "cltd");
				fprintf(px->af, "\t%-6s\t%s\n", CODEopStr(pasm->opcode, opsize), pand2);
				break;
			}
			fprintf(px->af, "\t%-6s\t%s, %s\n", CODEopStr(pasm->opcode, opsize), pand2, pand1);
			break;
		
		case iADDOFF:

			if(pasm->psa && pasm->psb && pasm->psa->psym && pasm->psb->psym)
			{
				// this is really an offset, not an add, so just use the offset
				// directly from psb
				//
				fprintf(px->af, "\t%-6s\t$%d, %s\n", CODEopStr(pasm->opcode, opsize),
						(pasm->psb->psym->offset + 7) / 8, pand1);
				break;
			}
			else
			{
				Log(logError, 0, "Missing operand B\n");
				return 3;
			}
			break;

		// unary math operations with results
		case iINC:
		case iDEC:
		case iNOT:
		case iNEG:
			opsize = SYMREFgetSizeBytes(pasm->psa);

			// if the destination register (inda) is indirect,
			// load it first since ra = OP (ra) is what we
			// want, but OP (ra) means (ra) = OP (ra) so I preload ra
			//
			if(inda && pasm->pra)
			{
				fprintf(px->af, "\t%-6s\t%s, %s\n", CODEopStr(iMOV, opsize),
					CPUregCode(pasm->pra, opsize, inda), CPUregCode(pasm->pra, opsize, 0));
				// change the '*' in the regs symbol to indicate its no longer 
				// needing to be dereferenced
				CODEupdateDeref(pasm);
				inda = 0;
			}
			if(pasm->pra)
			{
				pand1 = CPUregCode(pasm->pra, opsize, inda);
			}
			else if(pasm->psa)
			{
				pand1 = CPUsymName(pasm->psa, nbufa, px->underscore_globals);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			fprintf(px->af, "\t%-6s\t%s\n", CODEopStr(pasm->opcode, opsize), pand1);
			break;

		// non result, misc ops
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
				opsize = SYMREFgetSizeBytes(pasm->psa);
			if(pasm->pra)
			{
				pand1 = CPUregCode(pasm->pra, opsize, inda);
			}
			else if(pasm->psa)
			{
				pand1 = CPUsymName(pasm->psa, nbufa, px->underscore_globals);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			fprintf(px->af, "\t%-6s\t%s\n", CODEopStr(pasm->opcode, opsize), pand1);
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
				pand1 = CPUregCode(pasm->pra, 1, inda);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			fprintf(px->af, "\t%-6s\t%s\n", CODEopStr(pasm->opcode, opsize), pand1);
			break;

		// return
		case iRET:
			fprintf(px->af, "\t%-6s\n", CODEopStr(pasm->opcode, -1));
			break;

		// test byte
		case iTSTB:
			pand1 = pasm->pra ? CPUregCode(pasm->pra, 1, inda) : NULL;
			if(! pand1)
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			fprintf(px->af, "\t%-6s\t%s, %s\n", CODEopStr(pasm->opcode, opsize), pand1, pand1);
			break;

		// load address (always loads cpu address size)
		case iLEA:
		// moving
		case iMOV:
		// swapping
		case iXCHG:
			if(pasm->type == codeMOVE || pasm->type == codeSWAP)
			{
				inda = indb = 0;
			}
			if(pasm->opcode == iLEA)
			{
				opsize = dstsize = (CPU_ADDRESS_SIZE >> 3);
			}
			else
			{
				opsize = dstsize = pasm->psa ?
					CPUopSize(pasm->psa, inda) : CPUopSize(pasm->psb, indb);
			}
			if(pasm->pra)
			{
				pand1 = CPUregCode(pasm->pra, dstsize, inda);
			}
			else if(pasm->psa)
			{
				pand1 = CPUsymName(pasm->psa, nbufa, px->underscore_globals);
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			if(pasm->prb)
			{
				pand2 = CPUregCode(pasm->prb, opsize, indb);
			}
			else if(pasm->psb)
			{
				pand2 = CPUsymName(pasm->psb, nbufb, px->underscore_globals);
			}
			else
			{
				Log(logError, 0, "Missing operand B\n");
				return 3;
			}
			fprintf(px->af, "\t%-6s\t%s, %s\n", CODEopStr(pasm->opcode, opsize), pand2, pand1);
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
			fprintf(px->af, "\t%-6s\t%s, %d(%%ebp)\n", CODEopStr(pasm->opcode, opsize),
					CPUregCode(pasm->prb, opsize, 0), -g_tmplevel);
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
			fprintf(px->af, "\t%-6s\t%d(%%ebp), %s\n", CODEopStr(pasm->opcode, opsize),
					-g_tmplevel, CPUregCode(pasm->pra, opsize, 0));
			g_tmplevel -= (CPU_ADDRESS_SIZE >> 3);
			break;
			
		// promotions
		case iMOVSBL:
		case iMOVSWL:
		case iMOVZBL:
		case iMOVZWL:
			opsize = (pasm->opcode == iMOVSBL || pasm->opcode == iMOVZBL) ? 1 : 2;
			if(pasm->pra)
			{
				pand2 = CPUregCode(pasm->pra, opsize, inda);
				pand1 = CPUregCode(pasm->pra, 4, 0);

				if(inda && pasm->pra)
				{
					// change the '*' in the regs symbol to indicate its no longer 
					// needing to be dereferenced
					CODEupdateDeref(pasm);
					inda = 0;
				}
			}
			else
			{
				Log(logError, 0, "Missing operand A\n");
				return 2;
			}
			fprintf(px->af, "\t%-6s\t%s, %s\n", CODEopStr(pasm->opcode, opsize), pand2, pand1);
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
			case codeCOMMENT:	// comment in psa
			case codeDEBUG:		// debug info in psa			
				rv = GENERICoutput(px, pf, pasm);
				if(rv) return rv;
				break;

			case codeLOCAL:		// allocate stack space
				rv = GENERICoutput(px, pf, pasm);
				if(rv) return rv;
				
				if(g_localloc == 0 && g_tmpalloc == 0)
				{
					PASM pn;

					// look ahead to end of function for all local allocs
					// to fold them into one
					for(pn = pasm; pn; pn = pn->next)
					{
						if(pn->type == codeLOCAL)
						{
							if(pn->opcode == opEQUAL)
							{
								g_tmpalloc = pn->psa->psym->offset;
							}
							else
							{
								g_localloc = pn->psa->psym->offset;
							}
						}
					}
				}
				if(g_localloc || g_tmpalloc)
				{
					// fold all local allocs into one stack alloc
					//
					fprintf(px->af, "\t%-6s  $%d, %s\n", "subl",
							g_localloc + g_tmpalloc, "%esp");
				}
				break;

			case codeARG:
				g_argn++;
				break;

			// call generic to handle stack restore and add return
			case codeEOF:
				rv = GENERICoutput(px, pf, pasm);
				if(rv) return rv;
				// restore the stack
				if(g_localloc != 0 || g_tmpalloc != 0)
				{
					fprintf(px->af, "\t%-6s  $%d, %s\n", "addl",
						g_localloc + g_tmpalloc, "%esp");
				}
				fprintf(px->af, "\t%-6s  %s\n", "popl", "%ebp");
				fprintf(px->af, "\t%-6s\n", CODEopStr(iRET, 4));
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


