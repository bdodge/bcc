
#include "bccx.h"

/*
 * The functions codelist is a sequence of "C" operations and
 * operands in RPN notation which can translate directly into assembler
 * or at least psuedo code.  The code here translates the codelist
 * into a sequence of actual assembly language constructs using an
 * accumulator register and depending on the cpu code generator to
 * create the proper asm sequence for each "C" sequence
 *
 * This code also maintains a "stack" since expressions might have
 * intermediate results which require temporary storage.
 *
 * The input to this, the codelist is in the format
 *
 * 1 operator
 *  (operand2)  // second operand for binary operations
 *   operand    // destination operand
 *   result
 *
 * operations load the accumulator (or leave it loaded) and operate
 * on the accumulator against the second operand.  If the second operand
 * must be in a register for that to work, the cpu specific code can use
 * the register alloc system (regalloc) or not to help, but this code
 * assumes only the primary operand is in a register and it is in the
 * accumulator register.
 *
 * the result is passed down so the code generator knows the exact
 * type of value that is in the accumulator.
 * 
 * the operator "statement" is used to indicate and end of expression
 * to be used to free register allocation, clean the stack, etc.
 *
 * 2 argand sym
 *
 * make sym an argument for a function call
 *
 * 3 alloc sym
 *
 * allocate space for sym in global context or
 * on stack in function prolog.  all allocs should
 * appear at the top of the list.  it should be
 * apparent from the symbols type what kind of alloc
 * it is.
 *
 * 4 label
 *
 * a branch target label
 */


static PANDITEM	g_andstack;
static ANDITEM  g_ands[MAX_EXPRESSION];
static PANDITEM g_freeands;
static PREG     g_svreg;

//***********************************************************************
PFUNCTION FUNCcreate(PSYM name)
{
	PFUNCTION pf;
	
	pf = (PFUNCTION)malloc(sizeof(FUNCTION));
	if(! pf)
	{
		Log(logError, 0, "Internal: No memory for function\n");
		return NULL;
	}
	pf->pfuncsym	= name;
	pf->psymtab		= NULL;
	pf->next  		= NULL;
	pf->pcode 		= NULL;
	pf->pendofcode 	= NULL;
	pf->pemit 		= NULL;
	pf->pendofemit 	= NULL;
	return pf;
}

//***********************************************************************
void FUNCdestroy(PFUNCTION pf)
{
	if(pf)
	{
		if(pf->pfuncsym)
			SYMdestroy(pf->pfuncsym);
		free(pf);
	}
}

//***********************************************************************
int FUNCaddop(PFUNCTION pfunc, POPENTRY pop)
{
	if(! pfunc)
	{
		Log(logError, 0, "Internal: no function for operation\n");
		OPdestroy(pop);
		return 0;
	}
	pop->next = NULL;
	if(! pfunc->pcode)
	{
		pfunc->pcode      = pop;
		pfunc->pendofcode = pop;
	}
	else
	{
		if(pfunc->pendofcode->next)
		{
			while(pfunc->pendofcode->next)
				pfunc->pendofcode = pfunc->pendofcode->next;
		}
		pfunc->pendofcode->next = pop;
		pfunc->pendofcode = pop;
	}
	return 0;
}

//***********************************************************************
int FUNCinsertop(PFUNCTION pfunc, POPENTRY pafter, POPENTRY pop)
{
	POPENTRY plast;
	
	for(plast = pop; plast->next;)
		plast = plast->next;
	
	if(pafter == NULL || ! pfunc->pcode)
	{
		plast->next = pfunc->pcode;
		pfunc->pcode = pop;
	}
	else
	{
		plast->next = pafter->next;
		pafter->next = pop;
	}
	return 0;
}

//***********************************************************************
int FUNCaddLabel(PFUNCTION pfunc, char* label)
{
	POPENTRY pop;
	PSYM	 psym;
	
	psym = SYMcreate(label);
	if(! psym)
	{
		return 2;
	}
	psym->desc.istatic = 1;
	pop = OPcreate(opLABEL, opNONE, psym);
	if(! pop)
	{
		SYMdestroy(psym);
		return 1;
	}
	return FUNCaddop(pfunc, pop);
}

//***********************************************************************
int FUNCaddGoto(PFUNCTION pfunc, OPERATOR op, char* label)
{
	POPENTRY pop;
	PSYM	 psym;
	
	psym = SYMcreate(label);
	psym->desc.istatic = 1;
	pop = OPcreate(opGOTO, op, psym);
	if(! pop)
	{
		SYMdestroy(psym);
		return 1;
	}
	return FUNCaddop(pfunc, pop);
}

typedef struct tag_walk_cookie
{
	PFUNCTION 		pfunc;
	int       		doglb;
	unsigned long	nbytes;
}
STORECOOKIE, *PSSWC;

//***********************************************************************
int FUNCenmemberAggrt(PSYM psym)
{
	PSYM pmemb, ptype;
	int  offset, size;

	if(! psym)
		return 1;
	
	Log(logDebug,9, "==== setup aggrt %s\n", psym->name);
	
	// only enmember base aggrt types
	ptype = psym;
	
	if(! ptype || ! ptype->members)
		return 2;
	
	offset = 0;
	
	for(pmemb = ptype->members; pmemb; pmemb = pmemb->members)
	{
		size = SYMgetSize(pmemb);
		if(! ptype->desc.ispack)
		{
			int align;
			
			// align members on at least the address for their size
			// up to cpu-long-word alignment
			//
			align = size;
			
			if(CPU_ARCHITECTURE_RISC)
			{
				// this aligns all members on long-word boundaries
				//
				if(align < (CPU_LONG_SIZE))
				{
					align = (CPU_LONG_SIZE);
				}
			}
			if(align > (CPU_LONG_SIZE))
			{
				align = (CPU_LONG_SIZE);
			}
			offset = ((offset + align - 1) / align) * align;
			
			pmemb->offset = offset;
			
			if(ptype->desc.isaggrt != AGGRT_UNION)
			{
				offset += size;
			}
		}
	}
	return 0;
}

//***********************************************************************
int FUNCstorage(PSYM psym, void* cookie)
{
	PSSWC psc = (PSSWC)cookie;
	POPENTRY pop;
	
	Log(logDebug, 5, "Emit Storage %s\n", psym ? psym->name : "<nil>");
	if(! psym) return 0; // cant happen
	
	if(! psym->desc.isfunc || psym->desc.isext)	// anything but local functions
	{
		if(psc->doglb && ! psym->desc.isauto)
		{
			if(! psym->desc.iscode)
			{
				Log(logDebug,9, "==== global %s\n", psym->name);
				pop = OPcreate(opALLOC, opNONE, psym);
				FUNCinsertop(psc->pfunc, NULL, pop);
				psym->desc.iscode = 1;
			}
		}
		else if(! psc->doglb)
		{
			if(psym->desc.isauto)
			{
				int nbytes;

				if(! psym->desc.iscode)
				{
					// local allocs just allocate stack offsets and get
					// assigned an offset in the function stack frame
					//
					Log(logDebug, 9, "==== auto %s %d bytes\n", psym->name, psc->nbytes);
					psym->offset = psc->nbytes;
					nbytes = SYMgetSizeBytes(psym);
					if(nbytes < (CPU_PACK_ALIGN + 7) / 8)
						nbytes = (CPU_PACK_ALIGN + 7) / 8;
					psc->nbytes += nbytes;
				}
			}
		}
	}
	return 0;
}

//***********************************************************************
int FUNCemitGlobals(PFUNCTION pfunc, PSYMTAB ptab)
{
	STORECOOKIE cookie;
	
	cookie.pfunc = pfunc;
	cookie.doglb = 1;
	cookie.nbytes = 0;
	
	SYMTABwalk(ptab->symbols, FUNCstorage, &cookie);
	return 0;
}

//***********************************************************************
int FUNCemitLocals(PFUNCTION pfunc)
{
	PSYMTAB		ptab;
	PSYM		psym;
	POPENTRY	pop, pf;
	STORECOOKIE cookie;
	int			argnum;
	
	cookie.pfunc  = pfunc;
	cookie.doglb  = 0;
	cookie.nbytes = 0;

	// assign fake (negative) oridinal offsets to function arguments
	//
	for(argnum = 1, psym = pfunc->pfuncsym->members; psym; psym = psym->members)
	{
		psym->offset = -argnum++;
	}
	// figure out local stack alloc for local variables
	//
	for(ptab = pfunc->psymtab; ptab; ptab = ptab->prev)
	{
		SYMTABwalk(ptab->symbols, FUNCstorage, &cookie);
	}
	pfunc->localloc = cookie.nbytes;
	
	// allocate for nbytes on the stack
	//
	psym = SYMcreate("automatic var alloc");
	psym->offset = cookie.nbytes;
	psym->desc.isauto = 1;
	
	// find the function label to insert the local stack alloc on
	//
	for(pf = pfunc->pcode; pf; pf = pf->next)
		if(pf->type == opLABEL && pf->psym && pf->psym->desc.isfunc)
			break;
	if(! pf)
	{
		Log(logError, 0, "Internal: No function for locals\n");
		return 1;
	}
	pop = OPcreate(opALLOC, opNONE, psym);
	FUNCinsertop(pfunc, pf, pop);

	return 0;
}

//***********************************************************************
void EMITdumpCode(POPENTRY pop, POPENTRY pm, int len)
{
	char* typestr;
	const char *namestr;
	
	while(pop && (len-- > 0))
	{
		switch(pop->type)
		{
		case opERATOR:		typestr = "operator";	break;
		case opDEXMUL:		typestr = "dexmul";		break;
		case opERAND:		typestr = "opand";		break;
		case opRESULT:		typestr = "result";		break;
		case opMARKER:		typestr = "(mark";		break;
		case opARGUMENT:	typestr = "argand";		break;
		case opLABEL:		typestr = "lable:";		break;
		case opGOTO:		typestr = "jump";		break;
		case opALLOC:		typestr = "alloc";		break;
		default:			typestr = "?????";		break;
		}
		if(pop->type == opERATOR)
			namestr = OPname(pop->op);
		else
			namestr = pop->psym ? pop->psym->name : "!!nosym!!";
		Log(logDebug, 5, "%p %s %-8s %4s %s\n",
				pop,
				((pop == pm) ? "*":" "),
				typestr,
				((pop->type == opGOTO) ? OPname(pop->op) : " "),
				namestr);
		pop = pop->next;
	}
}

//***********************************************************************
int EMITopcode(PCCTX px, PFUNCTION pfunc, CODETYPE type, OPCODE op, PREG ra, PREG rb, PSYMREF psa, PSYMREF psb)
{
	PASM		pemit, pdump;
	const char*	ops;

	// now that symrefs are made for each operand, convert all array types to
	// pointer (except for local/global storages)  we wouldn't be here if the
	// op wasn't legal, and the code generator needs to treat arrays as pointers.
	//
	if(type != codeLOCAL && type != codeGLOBAL)
	{
		if(psa)
		{
			psa->desc.isptr += psa->desc.isdim ? 1 : 0;
			psa->desc.isdim = 0;
		}
		if(psb)
		{
			psb->desc.isptr += psb->desc.isdim ? 1 : 0;
			psb->desc.isdim = 0;
		}
	}
	// pass the parameters off to the code generator (cpu specific)
	//
	pemit = CODEgenerate(px, pfunc, type, op, ra ? ra->pr : NULL, rb ? rb->pr : NULL, psa, psb);
			
	for(pdump = pemit; pdump; pdump = pdump->next)
	{
		if(pdump->type == codeINSN)
			ops = CODEopStr(pdump->opcode, -1);
		else
			ops = FUNCcodename(pdump->type);
	
		if(pdump->pra && pdump->prb)
		{
			Log(logDebug, 5, " --- emit +++ %s\t%s,%s\n", ops, pdump->pra->name, pdump->prb->name);
		}
		else if(pdump->pra)
		{
			if(pdump->psa)
			{
				Log(logDebug, 5, " --- emit +++ %s\t%s,%s\n", ops, pdump->pra->name, pdump->psa->name);
			}
			else
			{
				Log(logDebug, 5, " --- emit +++ %s\t%s\n", ops, pdump->pra->name);
			}
		}
		else if(pdump->psa)
		{
			Log(logDebug, 5, " --- emit +++ %s\t%s\n", ops, pdump->psa->name);
		}
		else
		{
			Log(logError, 0, " --- emit --- no operands!\n");
		}
	}
	if(pemit)
	{
		if(! pfunc->pemit)
			pfunc->pemit = pemit;
		else
			pfunc->pendofemit->next = pemit;
		while(pemit->next)
			pemit = pemit->next;
		pfunc->pendofemit = pemit;
	}
	return 0;
}

//***********************************************************************
int ANDpush(PREG preg, PSYM restype)
{
	PANDITEM pand;
	
	pand = g_freeands;
	if(! pand)
	{
		Log(logError, 0, "Internal: Expression too complex\n");
		return 1;
	}
	if(! preg)
	{
		Log(logError, 0, "Internal: Can't push non-register\n");
		return 2;
	}
	if(! preg->state == rsLOADED)
	{
		Log(logError, 1, "Internal: Can't push empty register\n");
		return 3;
	}
	if(preg->state != rsSAVED)
	{
		preg->state = rsSTACKED;
	}
	g_freeands = g_freeands->next;
	
	Log(logDebug, 5, " ------ ANDpush ---%s = %s\n", preg->pr->name, restype->name);
	
	pand->ploc  = NULL;
	pand->preg  = preg;
	if(preg->psymr)
	{
		SYMREFdestroy(preg->psymr);
	}
	pand->preg->psymr = SYMREFcreate(restype);
	pand->next  = g_andstack;
	g_andstack  = pand;
	return 0;
}


//***********************************************************************
PANDITEM ANDpop()
{
	PANDITEM pand;
	
	if(! g_andstack)
	{
		Log(logError, 0, "Internal: inter-operand stack underflow\n");
		return NULL;
	}
	pand = g_andstack;
	g_andstack = g_andstack->next;
	pand->next = g_freeands;
	g_freeands = pand;
	if(pand->preg)
	{
		// change reg state from stacked to just loaded
		pand->preg->state = rsLOADED;
	}
	return pand;
}

//***********************************************************************
PANDITEM ANDfind(PANDITEM plist, char* exp)
{
	while(plist)
	{
		if(plist->preg)
		{
			if(! strcmp(plist->preg->psymr->name, exp))
				return plist;
		}
		else if(plist->ploc)
		{
			if(! strcmp(plist->ploc->psymr->name, exp))
				return plist;
		}
		plist = plist->next;
	}
	return NULL;
}

//***********************************************************************
int ANDfree(PANDITEM pand)
{
	if(pand->preg)
	{
		REGfree(g_andstack, pand->preg);
		pand->preg = NULL;
	}
	if(pand->ploc)
	{
		REGfreeLocal(pand->ploc);
		pand->ploc = NULL;
	}
	return 0;
}

//***********************************************************************
void ANDregFree(PANDITEM pand, int forcefree)
{
	if(pand)
	{
		if(pand->preg)
		{
			// this is a register left on the and stack
			// so move the register to the loaded list
			// or, if volatile, to the free list
			//
			if(
					!EMIT_OPTIMIZE(1)
				||	forcefree
				|| (pand->preg->psymr && pand->preg->psymr->desc.isvol)
			)
			{
				REGfree(g_andstack, pand->preg);
			}
			else
			{
				REGfreeLoaded(g_andstack, pand->preg);
			}
			pand->preg = NULL;
		}
		if(pand->ploc)
		{
			// the register is "stacked" in memory still, so if "push" is
			// used for stacked temp storage, this would be the place to modify
			// the stack pointer to compensate
			//
			pand->ploc = NULL;
		}
	}
}

//***********************************************************************
int EMITendStatement(int forcefree)
{
	PANDITEM  pand;

	Log(logDebug, 7, " --- Emit EOS ---%s\n", forcefree ? "clean slate" : "");
	
	// clean up the stack for the next operation
	//
	while(g_andstack)
	{
		pand = ANDpop();
		ANDregFree(pand, forcefree);
		ANDfree(pand);
	}
	if(g_svreg)
	{
		g_svreg->state = rsFREE;
		g_svreg = NULL;
	}
	// free up loaded/tmp registers
	//
	REGclear(g_andstack, forcefree);
	return 0;
}

//***********************************************************************
int EMITisPromotion(POPENTRY pop)
{
	return pop ?
			(pop->op >= opPROMOTE2UNSIGNED && pop->op <= opPROMOTEFLT2DBL)
			:
			0;
}

//***********************************************************************
int EMITpromotions(PCCTX px, PFUNCTION pfunc, POPENTRY pop, PREG preg)
{
	while(EMITisPromotion(pop))
	{
		// update type of thing in preg, but it still points to original smaller sym
		memcpy(&preg->psymr->desc, &pop->psym->desc, sizeof(pop->psym->desc));
		
		EMITopcode(px, pfunc, codeINSN, pop->op, preg, NULL, preg->psymr, NULL);
		pop = pop->next;
	}
	return 0;
}

//***********************************************************************
int EMITdexmul(PCCTX px, PFUNCTION pfunc, POPENTRY pop, PREG preg)
{
	if(pop && pop->type == opDEXMUL)
	{
		EMITopcode(px, pfunc, codeINSN, pop->op, preg, NULL,
				NULL, SYMREFcreate(pop->psym));
	}
	return 0;
}

//***********************************************************************
int FUNCemitCode(PCCTX px, PFUNCTION pfunc)
{
	PANDITEM  pand;
	POPENTRY  pcode;
	POPENTRY  pand1;
	POPENTRY  pand2;
	POPENTRY  presult;
	POPENTRY  promotions1, promotions2;
	POPENTRY  dexmul1, dexmul2;
	POPENTRY  pop;
	PREG	  rand1;
	PREG	  rand2;
	PSYM      pjmpsym;
	
	static char exp[MAX_EXPRESSION_NAME];
	
	pcode = pfunc->pcode;

	while(pcode)
	{
		promotions1 = NULL;
		promotions2 = NULL;
		dexmul1 = dexmul2 = NULL;
		pand1 = pand2 = NULL;
		
		OPdump(" ___ cur code =", 5, pcode);
		
		switch(pcode->type)
		{
		case opERATOR:
		
			pop = pcode;
			if(pop->op == opSTATEMENT)
			{
				// this end-of-statement marker is operand-less
				//
				EMITendStatement(0);
				pcode = pcode->next;
				break;
			}
			pcode = pcode->next;
			pand2 = pcode;

			if(pop->op == opCOMMA)
			{
				// comma just means pop the last result off 
				//
				pand = ANDpop();
				if(pand)
				{
					ANDregFree(pand, 0);
					ANDfree(pand);
				}
				break;
			}
			if(pand2)
			{
				pcode = pcode->next;
				
				// this is the start of any self induced operators
				// on this operand, so skip over any of these to get
				// to the next operand / code entry
				//
				promotions2 = pcode;
				
				while(pcode && EMITisPromotion(pcode))
				{
					pcode = pcode->next;
				}
				// each operand can have 0 or 1 index multipliers
				if(pcode && (pcode->type == opDEXMUL))
				{
					dexmul2 = pcode;
					pcode = pcode->next;
				}
				else
				{
					dexmul2 = NULL;
				}
			}
			if(! pand2 || (pand2->type != opERAND))
			{
				Log(logError, 0, "No operand for operator %s\n", OPname(pop->op));
				return 1;
			}
			if(! OPisUnary(pop->op))
			{
				if(! pcode)
				{
					Log(logError, 0, "No operand for operator %s\n", OPname(pop->op));
					return 1;
				}
				pand1 = pcode;
				
				if(pand1)
				{				
					pcode = pcode->next;
					promotions1 = pcode;
				
					while(pcode && EMITisPromotion(pcode))
					{
						pcode = pcode->next;
					}
					// each operand can have 0 or 1 index multipliers
					if(pcode && (pcode->type == opDEXMUL))
					{
						dexmul1 = pcode;
						pcode = pcode->next;
					}
					else
					{
						dexmul1 = NULL;
					}
				}
				if(! pand1 || (pand1->type != opERAND))
				{
					Log(logError, 0, "No operand for operator %s\n", OPname(pop->op));
					return 1;
				}
				// there are a few binary operators that set, and/or use, a "saved" address
				// (=, +, -).  the +/- case will always be with literal "1" ( 1 predexmul)
				// the result of the +/- is sent back to *(saved address) so there is never
				// a need to promote any operand (if the cpu cant handle a=a+1 without
				// promoting 1, it has to handle that in the code generator)
				// this greatly simplifies processing, since the saved address can use
				// the secondary register for cpus that have only 2 registers
				//
				if(
						(OPisIncDec(pand1->op) &&
						(pop->op == opEQUAL ||pop->op == opADD || pop->op == opMINUS))
				)
				{
					// this is the increment operator, or store for it, no promotion
					// since its a lit and we want to conserve regs, but make sure 
					// there's no dexmul that hasn't been opted out
					//
					if(dexmul2 || dexmul1)
					{
						Log(logError, 0, "Internal - no dexmul allowed for inc/dec ops\n");
					}
					Log(logDebug, 7, " @@--- Skip promotions since canned inc/dec\n");
					promotions1 = NULL;
					promotions2 = NULL;
				}
			}
			presult = pcode;
			if(! presult || (presult->type != opRESULT))
			{
				Log(logError, 0, "No result for operator %s\n", OPname(pop->op));
				return 1;
			}
			pcode = pcode->next;

			// here we go, an operation
			//
			REGdumpScore(g_andstack);
	
			if(! OPisUnary(pop->op))
			{
				// binary operator
				//
				if(pop->op == opEQUAL)
				{
					// pand1 <= pand2
					//
					// if the second operand has to be promoted, it needs a register, and
					// most likely it needs the accumulator for ACC machines
					//
					if(promotions2 && EMITisPromotion(promotions2) || dexmul2)
					{
						PANDITEM pswapfrom = NULL;

						// need to promote (which means math) so just get the accumulator 
						// if the operand isn't loaded in a register, get a reg to swap
						// the operand with
						//
						// Its quite possible the operand is already IN the acc, since it
						// represents the top of the current op stack, so if its an intermediate
						// operand, the normal loadAcc will work properly, but if pand1 is
						// in a register, it will need Acc too, so still need to swapout pand2
						//
						rand2 = NULL;
						
						if(CPU_ARCHITECTURE_ACC)
						{
							if(
										g_andstack
									&&	g_andstack->preg
									&&	g_andstack->preg->pr->type == rACC
									&&  (pand2->psym->name[0] != '(' || pand1->psym->name[0] == '(')
							)
							{
								rand2 = REGalloc(px, pfunc, g_andstack);
								if(! rand2)
								{
									Log(logError, 0, "Internal - cant get swap register\n");
									return 1;
								}
								// if pand2 is already in acc (there is a high likelyhood, since
								// its the top of the andstack), no need to swap it
								//
								if(pand2->psym->name[0] != '(')
								{
									pswapfrom = g_andstack;
									rand1 = g_andstack->preg;
									g_andstack->preg = rand2;
		
									// put the acc into rand2 to free up acc, use rand1 as
									// reg a, since that is where size comes from in generator
									//
									EMITopcode(px, pfunc, codeSWAP, opEQUAL, rand2, rand1,
											rand1->psymr, NULL);
		
									// swap r2,r1 symbolically
									rand2->psymr = rand1->psymr;
									rand2->state = rsSTACKED;
									rand1->state = rsFREE;
									rand1->psymr = NULL;
								}
							}
							// loading the acc should not swap out anything since its freed
							// 
							rand1 = REGloadAcc(px, pfunc, NULL, pand2,
									(pand2->psym->desc.isdim && ! pand2->psym->desc.isptr));
							if(! rand1)
							{
								Log(logError, 0, "Internal - cant get acc for promotion\n");
								return 1;
							}
							// keep rand1 "stacked (inuse)" so next loadacc knows to swap if needed
							rand1->state = rsSTACKED;
						}
						else
						{
							rand1 = REGforOpand(px, pfunc, g_andstack, pand2, 
									(pand2->psym->desc.isdim && ! pand2->psym->desc.isptr));
						}
						// ------ actual promotions here ---------
						//
						EMITpromotions(px, pfunc, promotions2, rand1);
						if(dexmul2)
						{
							EMITdexmul(px, pfunc, dexmul2, rand1);
						}
						if(CPU_ARCHITECTURE_ACC)
						{
							// if the acc is needed back on the stack, move the promoted result
							// back to the alloced register and the register to the stack
							// 
							if(pswapfrom)
							{
								PANDITEM prp;
								PSYMREF psymr;
	
								// had to swap out acc from stack
								//
								if(! g_andstack || ! g_andstack->preg)
								{
									Log(logError, 0, "Internal - dest operand not in reg on stack\n");
									return 1;
								}
								// rand1, the accumulator, gets back whichever stack entry
								// it was stolen from (or freed if this entry happened to
								// have been it, which would have been a lot of waste)
								//
								psymr = rand1->psymr;
								rand1->psymr = rand2->psymr;
								rand1->state = rsSTACKED;
								
								for(prp = g_andstack; prp && prp != pswapfrom;)
									prp = prp->next;
								if(prp)
								{
									prp->preg = rand1;
									
									// emit code to swap the actual comtents
									EMITopcode(px, pfunc, codeSWAP, opEQUAL, rand1, rand2, rand1->psymr, rand2->psymr);
								
									// rand2, the allocated gpr, is now just loaded
									// with pand2 promoted properly but keep it indicated as stacked
									// since it is in use until after the op is complete
									//
									rand2->psymr = psymr;
									rand2->state = rsSTACKED;
								}
								else
								{
									// this can only happen if the acc is swapped with itself
									Log(logError, 0, "Internal - swapped out operand no longer stacked\n");
									return 1;
								}
							}
							else if(rand2)
							{
								// need to put result in second reg since pand1 needs acc
								//
								// emit code to move the actual comtents rand1 into rand2
								//
								EMITopcode(px, pfunc, codeSWAP, opEQUAL, rand2, rand1, NULL, rand1->psymr);
								
								rand2->psymr = rand1->psymr;
								rand2->state = rsSTACKED;	// r2 still in use
								rand1->psymr = NULL;
								rand1->state = rsFREE;		// acc in now free
							}								
							else
							{
								// do not need the acc for pand1 so just keep pand2 in acc
								// and let loadacc of pand1 swap it if needed
								//
								rand2 = rand1;
							}
						}
						else
						{
							// not an ACC machine, can promote in any reg, so all set
							//
							rand2 = rand1;
						}
						// regardless of code above, rand1 is not valid here
						//
						rand1 = NULL;
					}
					else
					{
						rand1 = NULL;

						rand2 = REGforOpand(px, pfunc, g_andstack, pand2,
								(pand2->psym->desc.isdim && ! pand2->psym->desc.isptr));
						if(! rand2)
						{
							Log(logError, 0, "Internal - cant get register\n");
							return 1;
						}
						// keep rand2 in use
						rand2->state = rsSTACKED;
					}
					if(rand2)
					{
						if(! pand2->psym->desc.isuns && pand1->psym->desc.isuns)
						{
							Log(logDebug, 7, "  func promoting operand2 to unsigned\n");
							rand2->psymr->desc.isuns = 1;
						}
					}
					// the destination doesn't have to be in a register, but if the destination
					// is a calculation, then it will already be in one which can be used to deref
					// and if the destination is going to be stored AT then load it also
					//
					if(pand1->psym->name[0] == '(')
					{
						// going to store at the address thats an intermediate result
						// so make sure that's in a register since it'll be indirect
						//
						// if rand2 has the acc, load the dest operand into a gpr
						// and swap it with the acc
						//
						if(CPU_ARCHITECTURE_ACC && rand2 && rand2->pr->type == rACC)
						{
							PSYMREF psymr2 = rand2->psymr;
							PREG    randx;

							// if this destination is "use addr" then this is the
							// store for an inc/dec operation.  the saved address
							// can now be trashed. if its a post-dec operation then
							// swap the acc with what is at the saved addr reg
							// if the cpu has only 2 registers, this means getting
							// a temp to swap through.
							//
							if(OPisIncDec(pand1->op))
							{
								rand1 = g_svreg;
								rand1->state = rsSTACKED;
								g_svreg = NULL;
							}
							else
							{
								rand1 = REGforOpand(px, pfunc, g_andstack, pand1, 0);
							}
							if(! rand1)
							{
								Log(logError, 0, "Internal - cant get swap register\n");
								return 1;
							}

							EMITopcode(px, pfunc, codeSWAP, opEQUAL, rand1, rand2,
									rand1->psymr, NULL);
	
							// swap r2,r1 symbolically
							rand2->psymr = rand1->psymr;
							rand2->state = rsSTACKED;
							rand1->psymr = psymr2;
							rand1->state = rsSTACKED;
							// and swap for use too
							randx = rand2;
							rand2 = rand1;
							rand1 = randx;
						}
						else
						{
							if(OPisIncDec(pand1->op))
							{
								rand1 = g_svreg;
								rand1->state = rsSTACKED;
								g_svreg = NULL;
							}
							else
							{
								rand1 = REGloadAcc(px, pfunc, g_andstack, pand1, 0);
							}
						}
					}
					else
					{
						rand1 = NULL;
					}
					// for assignments in post inc/dec cases, need to pre-load
					// the dest before the store, so get a new 3rd reg to exchange
					// the operation through.  Since 2 regs may be in use already
					// (src and dst) this means allocating for the first load
					//
					if(pand1->op == opPOSTINC || pand1->op == opPOSTDEC)
					{
						PREG preg;

						// need to do ACC=a, then (a = a + 1) not using ACC
						//
						// alloc a register to load the source in.  this will
						// always fail in 2 register cpus since the 2 are in use
						// so I just use the stack.  It should always work in 3
						// (or more) reg cpus
						//
						if(CPU_NUM_REGS > 2 || ! rand1)
						{
							PSYMREF pand1symr;

							pand1symr = SYMREFcreate(pand1->psym);

							preg = REGalloc(px, pfunc, g_andstack);
							if(! preg)
							{
								// no registers avail, the acc should have been
								// pushed off, so cant continue
								//
								Log(logError, 0, "No register for swap with saved addr\n");
								return 1;
							}
							// load the original value into the register,
							// note this load should work regardless if the
							// source is indirect in a register or just a symbol name
							//
							EMITopcode(px, pfunc, codeLOAD, opNONE, preg, rand1,
									NULL, pand1symr);

							preg->psymr = pand1symr;
							preg->state = rsLOADED;

							// store the result (in acc usually) at the address
							// again, should work regardless of indirect or symb.
							//
							EMITopcode(px, pfunc, codeSTORE, opNONE, rand1, rand2,
									pand1symr, rand2->psymr);

							if(rand1)
							{
								// no longer need the dest addr
								rand1->psymr = NULL;
								rand1->state = rsFREE;
							}
							// the result reg is set to the alloced original value
							// rand2 is left alone and marked loaded below
							//
							rand1 = preg;
						}
						else
						{
							PSLOCAL ploc;

							// crummy 2 reg swap.  allocate a stack slot to swap through
							//
							ploc = LOCALalloc(pfunc, rand1->psymr);
							if(! ploc)
							{
								Log(logError, 0, "No location for swap with saved addr\n");
								return 1;
							}
							/*
							 * *ploc = (*rand1)
							 * (*rand1) = rand2
							 * rand1 = *ploc
							 */
							// store the original contents of svaddr at the temp location
							//
							EMITopcode(px, pfunc, codeTSTORE, opNONE, NULL, rand1,
									ploc->psymr, NULL);

							// store the result (of the inc/dec in rand2) at the saved address
							//
							EMITopcode(px, pfunc, codeSTORE, opNONE, rand1, rand2,
									rand1->psymr, rand2->psymr);

							// load the original contents from temp into rand1
							//
							EMITopcode(px, pfunc, codeTLOAD, opNONE, rand1, NULL,
									NULL, ploc->psymr);

							// rand1 is what it should be (the original op, before inc/dec)
							// (which was in rand2)
							//
							if(rand1->psymr)
							{
								SYMREFdestroy(rand1->psymr);
							}
							rand1->psymr = rand2->psymr;
							rand2->psymr = NULL;
							rand1->state = rsLOADED;
							ploc->psymr = NULL;

							LOCALfree(ploc);
						}
						// the original value of the operand just inc/dec stored should
						// be in rand1 here, and all set.  free rand2 to make sure the
						// optimization of setting rand1=rand2 isn't done
						//
						if(rand2)
						{
							REGfree(g_andstack, rand2);
							rand2 = NULL;
						}
					}
					else
					{
						// emit the store operation
						//
						EMITopcode(
									px, pfunc,
									codeSTORE,
									opNONE,
									rand1, rand2,
									SYMREFcreate(pand1->psym), rand2->psymr
								);

						// for preinc/predec, its the source that we want to keep 
						// as the result, not the dest, so hand set rand1 here
						// and free rand2 to avoild the optimization below
						//
						if(pand1->op == opPREINC || pand1->op == opPREDEC)
						{
							if(rand1)
							{
								REGfree(g_andstack, rand1);
							}
							rand1 = rand2;
							rand2 = NULL;
						}
					}
					// rand2 is no longer stacked
					if(rand2)
					{
						rand2->state = rsLOADED;
					}
					// go through the registers and dirty any references
					// to pand1 expression [todo]
					//					
					if(! rand1 && rand2 && rand2->state == rsLOADED)
					{
						// reassign the source register to the lvalue here since it
						// (the lvalue) is more likely to be used in a subsequent expression
						// we don't need a MOVE since the contents are the same but we
						// DO need to change the symref to the dest operand, since the 
						// register is taking over for the dest
						//
						rand1 = rand2;
						rand2 = NULL;

						if(rand1->psymr)
						{
							SYMREFdestroy(rand1->psymr);
						}
						rand1->psymr = SYMREFcreate(pand1->psym);
					}
					// note rand1 and result are pushed back on andstack below
				}
				else
				{
					int optout;
					
					optout = 0;
#if 0
					if(EMIT_OPTIMIZE(2))
					{
						// look for the completed expression in the and stack
						// and if there, and in a register, just push the already
						// computed result on the and stack again.
						//
						// This is my tip o' the hat to common subexpression removal.
						//
						pand = ANDfind(g_andstack, presult->psym->name);
						if(pand && pand->preg)
						{
							Log(logDebug, 5, " @@---- ANDreferto --- %s %s\n",
									pand->preg ? pand->preg->name :
										(pand->ploc ? pand->ploc->psymr->name : "??"));

							pand1  = pand;
							rand1  = pand->preg;
							optout = 1;
						}
					}
#endif
					if(! optout)
					{
						PANDITEM pswapfrom = NULL;		
						int regswapped;

						// registers have to be allocated in the order they are on the stack
						// (i.e. pand2,pand1)
						//
						// if the second operand has to be promoted, it needs a register, and
						// most likely it needs the accumulator.
						//
						// this is very similar to the opEQUAL code above but with enough
						// subtle differences to make it not a function.  With the binary
						// op case, it is often likely that the operation is commutative
						// so pand2 can be promoted in the ACC and kept there, and pand1
						// can load into a secondary register unless it also needs promoting
						// so swapping can be defered to pand1 load
						//
						if(CPU_ARCHITECTURE_ACC)
						{
							if(promotions2 && EMITisPromotion(promotions2) || dexmul2)
							{
								// need to promote (which means math) so just get the accumulator 
								// if the operand isn't loaded in a register, get a reg to swap
								// the operand with
								//
								// Its quite possible the operand is already IN the acc, since it
								// represents the top of the current op stack, so if its an intermediate
								// operand, the normal loadAcc will work properly.
								//
								// if the ACC is in use, it will be the top of the stack, which means
								// it has pand2 in it already, so only need to check for acc in use
								// if pand2 is not an intermediate result
								//
								rand2 = NULL;
								
								if(
										g_andstack
									&&	g_andstack->preg
									&&	g_andstack->preg->pr->type == rACC
									&&	pand2->psym->name[0] != '(' )
								{
									// pand2 needs the acc and the acc is in use
									//
									rand2 = REGalloc(px, pfunc, g_andstack);
									if(! rand2)
									{
										Log(logError, 0, "Internal - cant get swap register\n");
										return 1;
									}
									pswapfrom = g_andstack;
									rand1 = g_andstack->preg;
									g_andstack->preg = rand2;
		
									// put the acc into rand2 to free up acc, use rand1 as
									// dest reg, since that is where size comes from in generator
									//
									EMITopcode(px, pfunc, codeSWAP, opEQUAL, rand2, rand1,
											rand1->psymr, NULL);
		
									// swap r2,r1 symbolically
									rand2->psymr = rand1->psymr;
									rand2->state = rsSTACKED;
									rand1->state = rsFREE;
									rand1->psymr = NULL;
								}
								// loading the acc should not swap out anything now
								// 
								rand1 = REGloadAcc(px, pfunc, NULL, pand2,
										(pand2->psym->desc.isdim && ! pand2->psym->desc.isptr));
								if(! rand1)
								{
									Log(logError, 0, "Internal - cant get acc for promotion\n");
									return 1;
								}
								// keep rand1 "stacked (inuse)" so next loadacc knows to swap if needed
								rand1->state = rsSTACKED;
							}
							else
							{
								// promote in any register
								//
								rand1 = REGforOpand(px, pfunc, g_andstack, pand2, 
										(pand2->psym->desc.isdim && ! pand2->psym->desc.isptr));
							
							}
							EMITpromotions(px, pfunc, promotions2, rand1);
							if(dexmul2)
							{
								EMITdexmul(px, pfunc, dexmul2, rand1);
							}
							if(CPU_ARCHITECTURE_ACC)
							{
								// if the acc is needed back on the stack, move the promoted result
								// back to the alloced register and the register to the stack
								// 
								if(pswapfrom)
								{
									PANDITEM prp;
									PSYMREF psymr;
		
									// had to swap out acc from stack
									//
									if(! g_andstack || ! g_andstack->preg)
									{
										Log(logError, 0, "Internal - dest operand not in reg on stack\n");
										return 1;
									}
									// rand1, the accumulator, gets back whichever stack entry
									// it was stolen from (or freed if this entry happened to
									// have been it, which would have been a lot of waste
									//
									psymr = rand1->psymr;
									rand1->psymr = rand2->psymr;
									rand1->state = rsSTACKED;
									
									for(prp = g_andstack; prp && prp != pswapfrom;)
										prp = prp->next;
									if(prp)
									{
										prp->preg = rand1;
										
										// emit code to swap the actual comtents
										EMITopcode(px, pfunc, codeSWAP, opEQUAL, rand1, rand2, rand1->psymr, rand2->psymr);
									
										// rand2, the allocated gpr, is now just loaded
										// with pand2 promoted properly but keep it indicated as stacked
										// since it is in use until after the op is complete
										//
										rand2->psymr = psymr;
										rand2->state = rsSTACKED;
									}
									else
									{
										// this can only happen if the acc is swapped with itself
										Log(logError, 0, "Internal - swapped out operand no longer stacked\n");
										return 1;
									}
								}
								else
								{
									// if swapping didn't happen, the promoted pand2 is now
									// in the accumulator, so keep it there and let pand1
									// processing see if it needs to move it
									//
									rand2 = rand1;
								}
							}
							else
							{
								// non-acc machine
								//
								rand2 = rand1;
							}
							rand1 = NULL;
						}
						else if(
								// its an intermediate result, is already in a reg OR
									pand2->psym->name[0] == '('
								// a risc CPU which needs everything in a reg OR
								||	CPU_ARCHITECTURE_RISC
								// an operand that needs to be used indirectly
								||	(pand2->psym->name[1] == '*')
						)
						{
							//  intermediate result that doesn't need promotions or dexmul
							//	so its on the stack so should already be in a reg (or the acc)
							//
							rand2 = REGforOpand(px, pfunc, g_andstack, pand2, 
									(pand2->psym->desc.isdim && ! pand2->psym->desc.isptr));
						}
						else
						{
							// assume code generation can handle a symbolic source operand
							//
							rand2 = NULL;
						}
						if(rand2)
						{
							if(! pand2->psym->desc.isuns && pand1->psym->desc.isuns)
							{
								Log(logDebug, 7, "  func promoting operand2 to unsigned\n");
								rand2->psymr->desc.isuns = 1;
							}
						}
						if(CPU_ARCHITECTURE_ACC)
						{
							// if the second operand is in the accumulator, get a gpr for 
							// it and move it there so primary operand gets the accumulator
							// I have to do this since the general concept here is
							//
							// pand1 = pand1 OPER pand2
							//
							// so the result wants to be in the accumulator and some ops cant
							// be done backwards, i.e subtraction might always be ACC - GPR,
							// so pand1 - pand2 cant be done unless pand1 is in the ACC
							//
							// Note that if OPER is commutative, and pand1 doesn't need promotions
							// (or promotions can happen in a gpr) then there's no need to
							// swap the acc and gpr
							//
							if(rand2 && rand2->pr->type == rACC)
							{
								if(
										(promotions1 && EMITisPromotion(promotions1))
									||	dexmul1
									||	! OPisCommutative(pop->op)
								)
								{
									// get a register to put pand2 in to clear the way for pand1 in acc
									//
									rand1 = REGalloc(px, pfunc, g_andstack);
									if(! rand1)
									{
										Log(logError, 0, "Internal - cant get gpr for pand2\n");
										return 1;
									}
									// put the acc into rand1 (fresh register) to free up acc
									EMITopcode(px, pfunc, codeMOVE, opEQUAL, rand1, rand2, NULL, rand2->psymr);
	
									// symoblic update of rand1
									rand1->psymr = rand2->psymr;
									rand1->state = rsSTACKED;
									// acc is now clear
									rand2->state = rsFREE;
									rand2->psymr = NULL;
	
									// what was is in rand2 is now in rand1, so point back to it
									rand2 = rand1;
	
									// loading the acc should not swap out anything since its free
									//
									rand1 = REGloadAcc(px, pfunc, g_andstack, pand1,
											(pand1->psym->desc.isdim && ! pand1->psym->desc.isptr));
									regswapped = 0;
								}
								else
								{
									// commutative op and no promotions, just do it backwards
									//
									Log(logDebug, 8, " @-- using commutative shortcut \n");
									rand1 = rand2; // point rand1 at rand2, which is the acc
									rand2 = REGforOpand(px, pfunc, g_andstack, pand1,
											(pand1->psym->desc.isdim && ! pand1->psym->desc.isptr));
									regswapped = 1;
								}
							}
							else
							{
								// rand2 isn't the acc, so use acc for rand1
								//
								rand1 = REGloadAcc(px, pfunc, g_andstack, pand1,
										(pand1->psym->desc.isdim && ! pand1->psym->desc.isptr));
								regswapped = 0;
							}
						}
						else
						{
							// any reg works for operand, but use ACC if its free (no stack)
							// or if the code needs the result in the acc (like for
							// a ternary operation)
							//
							if(g_andstack && (pand1->psym->name[0] != '(' || g_andstack->preg->psymr->psym != pand1->psym))
							{	
								// not the top of the stack, but there is a stack (which might be in acc)
								rand1 = REGforOpand(px, pfunc, g_andstack, pand1,
										(pand1->psym->desc.isdim && ! pand1->psym->desc.isptr));
							}
							else
							{
								rand1 = REGloadAcc(px, pfunc, g_andstack, pand1,
										(pand1->psym->desc.isdim && ! pand1->psym->desc.isptr));
							}
							regswapped = 0;
						}
						if(rand1 && pand2->psym->desc.isuns && ! pand1->psym->desc.isuns)
						{
							Log(logDebug, 7, "  func promoting operand1 to unsigned\n");
							rand1->psymr->desc.isuns = 1;
						}
						// ---- promotions happen here ------
						//
						EMITpromotions(px, pfunc, promotions1, rand1);
						if(dexmul1)
						{
							EMITdexmul(px, pfunc, dexmul1, rand1);
						}
						if(rand1)
						{
							// if the dest operand has an address that needs saving, save it
							// now before its modified but also while acc/reg are loaded
							//
							if(OPisIncDec(pand1->op))
							{
								if(regswapped)
								{
									g_svreg = REGsaveAddr(px, pfunc, g_andstack, pand1, rand2);
								}
								else
								{
									g_svreg = REGsaveAddr(px, pfunc, g_andstack, pand1, rand1);
								}
							}
							EMITopcode(px, pfunc, codeINSN, pop->op, rand1, rand2, rand1->psymr,
									rand2 ? rand2->psymr : SYMREFcreate(pand2->psym));

							// rand2 is now just loaded, not stacked
							if(rand2)
							{
								rand2->state = rsLOADED;
							}
						}
						else
						{
							Log(logError, 0, "Internal: operand memory\n");
							return 1;
						}
					}
				}
				// push back the result on the and stack
				//
				ANDpush(rand1, presult->psym);

				// if there is a second reg and it has no other references
				// set it up as loaded in case it can be re-used without reloading
				//
				if(rand2)
				{						
					for(pand = g_andstack; pand; pand = pand->next)
					{
						if(pand->preg == rand2)
							break; // other reference on stack
					}
					if(! pand)
					{
						REGfreeLoaded(g_andstack, rand2);
					}
				}
			}
			else		/* unary -------------------------------------------------- */
			{
				int loadaddress = 0;
				
				if(pop->op == opADDROF || (pand2->psym->desc.isdim && ! pand2->psym->desc.isptr))
				{
					loadaddress = 1;
				}
				if(pop->op != opCALL || pand2->psym->name[0] == '(')
				{
					if(CPU_ARCHITECTURE_ACC || pop->op == opTERNARY)
					{
						rand1 = REGloadAcc(px, pfunc, g_andstack, pand2, loadaddress);
					}
					else
					{
						rand1 = REGforOpand(px, pfunc, g_andstack, pand2, loadaddress);
					}
					if(rand1)
					{
						EMITpromotions(px, pfunc, promotions2, rand1);

						if(pop->op != opTERNARY)
						{
							EMITopcode(px, pfunc, codeINSN, pop->op, rand1, NULL, rand1->psymr, NULL);
						}
						else
						{
							// this is an intermediate result for an ?: operation,						
							// set reg to saved to make sure it keeps across label
							//
							rand1->state = rsSAVED;
						}
						// push back the intermediate result on the opand stack
						//
						ANDpush(rand1, presult->psym);
					}
					else
					{
						Log(logError, 0, "Internal: operand memory\n");
						return 1;
					}
				}
				if(pop->op == opCALL)
				{
					if(pand2->psym->name[0] != '(')
					{
						// calling a label, need to emit since skipped above
						EMITopcode(px, pfunc, codeINSN, pop->op, NULL, NULL, SYMREFcreate(pand2->psym), NULL);
					}
					// get the function return register.  Whatever was in it is now the result
					// of the function call.
					//
					rand1 = REGgetSpecific(CPU_FUNCTION_RET_REGISTER);
					if(rand1->state == rsSAVED || rand1->state == rsSTACKED)
					{
						Log(logError, 0, "Return reg was in use\n");
					}
					rand1->state = rsLOADED;
					ANDpush(rand1, presult->psym);
				}
				// if this was a return from a function
				// then quickly insert a jump to end-of-function
				//
				if(pop->op == opRETURN)
				{					
					// pcode at this point HAS to be at the end-of-statement
					// so just emit a goto and move on
					//
					sprintf(exp, "__ef_%s", pfunc->pfuncsym->name);
					pjmpsym	= SYMcreate(exp);
					pjmpsym->desc.istatic = 1;
					EMITopcode(px, pfunc, codeBR, opNONE, NULL, NULL, SYMREFcreate(pjmpsym), NULL);
					SYMdestroy(pjmpsym);
				}
			}
			break;
			
		case opERAND:
			
			Log(logError, 0, "Missing operator for operand %s\n", pcode->psym ?
					pcode->psym->name : "<nil>");
			pcode = pcode->next;
			break;
			
		case opARGUMENT: // an operand that is gonna be passed
			
			pand1 = pcode;
			pcode = pcode->next;
			rand1 = REGloadAcc(px, pfunc, g_andstack, pand1,
						(pand1->psym->desc.isdim && ! pand1->psym->desc.isptr));
			if(rand1)
			{
				// handle promotions of args here
				EMITpromotions(px, pfunc, pcode, rand1);
				while(pcode && OPisPromotion(pcode->op))
					pcode = pcode->next;
				
				// the opcode is the really the argument number
				EMITopcode(px, pfunc, codeARG, pand1->op, rand1, NULL, rand1->psymr, NULL);

				// free the reg, no need to keep it on the and stack !!!!! really?
				REGfree(g_andstack, rand1);
			}
			else
			{
				Log(logError, 0, "Internal: operand memory\n");
				return 1;
			}
			break;
			
		case opLABEL:
			
			// if the code can be branched-to, then there cant be any assumptions
			// about whats in the register so do a clean up of loaded registers
			//
			// if the top of the and stack is a ternary expression sub-result, pull
			// it out and save and push it back 
			//
			{
				PSYMREF ternsymr;
				PREG    ternreg;

				if(g_andstack && g_andstack->preg && g_andstack->preg->state == rsSAVED)
				{
					ternreg = g_andstack->preg;
					ternsymr = ternreg->psymr;
					ternreg->psymr = NULL;
					g_andstack->preg = NULL;
				}
				else
				{
					ternreg = NULL;
				}
				EMITendStatement(1);

				EMITopcode(px, pfunc, codeLABEL, opNONE, NULL, NULL, SYMREFcreate(pcode->psym), NULL);

				if(ternreg)
				{
					g_andstack = g_freeands;
					g_freeands = g_freeands->next;

					g_andstack->preg = ternreg;
					g_andstack->preg->psymr = ternsymr;
					g_andstack->ploc = NULL;
					g_andstack->next = NULL;
				}
			}
			pcode = pcode->next;
			break;
			
		case opGOTO:
			
			{
				CODETYPE codet;
					
				switch(pcode->op)
				{
				case opBOOLEQ:		codet = codeBEQ; break;
				case opBOOLNEQ:		codet = codeBNE; break;
				case opNONE:		codet = codeBR;  break;
				default:
					codet = codeCOMMENT;
					Log(logError, 0, "Internal: not supported goto condition\n");
					break;
				}
				EMITopcode(px, pfunc, codet, opNONE, NULL, NULL, SYMREFcreate(pcode->psym), NULL);
			}
			pcode = pcode->next;
			break;
			
		case opALLOC:
		
			if(pcode->psym->desc.isauto)	// local
			{
				EMITopcode(px, pfunc, codeLOCAL, pcode->op, NULL, NULL, SYMREFcreate(pcode->psym), NULL);
			}
			else	// global
			{
				// global variables can be external, public-global or static-global
				// functions dont need an alloc, so only ext/pub is emitted for them
				if(pcode->psym->desc.isext)
				{
					// extern declared
					EMITopcode(px, pfunc, codeEXTERN, opNONE, NULL, NULL, SYMREFcreate(pcode->psym), NULL);
				}
				else if(! pcode->psym->desc.istatic)
				{
					// public implys global for variables
					EMITopcode(px, pfunc, codePUBLIC, opNONE, NULL, NULL, SYMREFcreate(pcode->psym), NULL);
				}
				else if(! pcode->psym->desc.isfunc)
				{
					// regular static global
					EMITopcode(px, pfunc, codeGLOBAL, opNONE, NULL, NULL, SYMREFcreate(pcode->psym), NULL);
				}
			}
			pcode = pcode->next;
			break;
			
		default:
			
			Log(logError, 0, "Internal: Bad type for code list\n");
			pcode = pcode->next;
			// cant happen
			break;
		}
	}
	// check that there is a return as the last statement if non void func
	//
	// [todo]

	// add the end-of-function label
	sprintf(exp, "__ef_%s", pfunc->pfuncsym->name);
	pjmpsym	= SYMcreate(exp);
	pjmpsym->desc.istatic = 1;
	EMITopcode(px, pfunc, codeLABEL, opNONE, NULL, NULL, SYMREFcreate(pjmpsym), NULL);
	SYMdestroy(pjmpsym);

	// and finally and end-of-function marker
	EMITopcode(px, pfunc, codeEOF, opNONE, NULL, NULL, SYMREFcreate(pfunc->pfuncsym), NULL);
	return 0;
}

//***********************************************************************
int FUNCemitProlog(PCCTX px, PFUNCTION pfunc, PSYMTAB ptypes)
{
	PANDITEM pand;
	int r;
	
	// init code generation
	//
	r = CODEgenProlog(px, 0);
	
	// init register scores
	//
	REGprolog();
	
	// clear the andstack
	//
	while(g_andstack)
	{
		pand = g_andstack->next;
		ANDfree(g_andstack);
		g_andstack = pand;
	}
	// reint pand microallocator
	// (todo - this should really only need to be done once ever)
	//
	for(r = 0; r < MAX_EXPRESSION; r++)
	{
		g_ands[r].ploc = NULL;
		g_ands[r].preg = NULL;
		g_ands[r].next = &g_ands[r + 1];
	}
	g_ands[r-1].next = NULL;
	g_freeands = g_ands;

	g_svreg = NULL;

	Log(logDebug, 3, "Function ----- %-15s >>>>>>>>>>>\n",
			pfunc->pfuncsym->name);

	// add the function label and publ decl (but wait for a while for anything else)
	//
	if(pfunc->pfuncsym)
	{
		POPENTRY pop;
		
		pop = OPcreate(opALLOC, opNONE, pfunc->pfuncsym);
		FUNCaddop(pfunc, pop);
		pop = OPcreate(opLABEL, opNONE, pfunc->pfuncsym);
		FUNCaddop(pfunc, pop);
	}
	pfunc->localloc = 0;
	pfunc->tmpalloc = 0;
	pfunc->maxargs  = 0;
	
	return 0;
}

//***********************************************************************
int FUNCemitEpilog(PCCTX px, PFUNCTION pfunc)
{
	if(pfunc->pfuncsym)
	{
		PASM pfa, pla;

		Log(logDebug, 3, "Function ----- %-15s <<<<<<<<<<<\n",
				pfunc->pfuncsym->name);
		
		// find the function label to insert the sva alloc on
		//
		for(pfa = pfunc->pemit; pfa; pfa = pfa->next)
			if(pfa->type == codeLABEL && pfa->psa && pfa->psa->desc.isfunc)
				break;
				
		if(! pfa)
		{
			Log(logError, 0, "Internal: No function for locals\n");
			return 1;
		}
		if(pfunc->localloc)
		{
			for(pla = pfa; pla; pla = pla->next)
				if(pla->type == codeLOCAL)
					break;
			if(! pla || pla->type != codeLOCAL)
			{
				Log(logError, 0, "No local alloc record and need it\n");
			}
			else
			{
				pfa = pla;
			}
		}
		if(pfunc->tmpalloc)
		{
			PASM pemit;
			PSYM psym;
			
			// resist the urge to fold tmpalloc into localalloc here, the
			// asm generation might need to know both separately.  Note
			// the actual offset for temp store/load are never passed down
			// to the code generator, since they are always in order (go up/down)
			// of the use (i.e. tstore/tload are always nested
			//
			psym = SYMcreate("temp var alloc");
			psym->offset = pfunc->tmpalloc;
			psym->desc.isauto = 1;
			
			pemit = CODEgenerate(px, pfunc, codeLOCAL, opEQUAL, NULL, NULL, SYMREFcreate(psym), NULL);
			if(pemit)
			{
				pemit->next = pfa->next;
				pfa->next = pemit;
			}
		}
	}
	return CODEgenEpilog(px, 0);
}

//***********************************************************************
int FUNCdestroyCode(POPENTRY pcode)
{
	POPENTRY px;
	
	while(pcode)
	{
		px = pcode;
		pcode = pcode->next;
		OPdestroy(px);
	}
	return 0;
}

//***********************************************************************
const char* FUNCcodename(CODETYPE type)
{
	const char* ops;
	
	switch(type)
	{	
	case codeINSN:		ops = "INSN";		break;
	case codeMOVE:		ops = "MOVE";		break;
	case codeARG:		ops = "ARG";		break;
	case codeSTORE:		ops = "STORE";		break;
	case codeTSTORE:	ops = "PUSH";		break;
	case codeLOAD:		ops = "LOAD";		break;
	case codeLOADADDR:	ops = "LOADA";		break;
	case codeTLOAD:		ops = "POP";		break;
	case codeSWAP:		ops = "SWAP";		break;
	case codeBR:		ops = "BR";			break;
	case codeBEQ:		ops = "BRE";		break;
	case codeBNE:		ops = "BNE";		break;
	case codeLABEL:		ops = "LABLE";		break;
	case codePUBLIC:	ops = "PUBL";		break;
	case codeEXTERN:	ops = "EXTN";		break;
	case codeLOCAL:		ops = "LOCL";		break;
	case codeGLOBAL:	ops = "GLBL";		break;
	case codeEOF:		ops = "EOF";		break;
	case codeCOMMENT:	ops = "COMM";		break;
	case codeDEBUG:		ops = "DBG";		break;
	default:			ops = "?????";		break;
	}
	return ops;
}

//***********************************************************************
int AssembleCode(PCCTX px)
{
	char cmd[MAX_PATH + 128];
	int rv;

	snprintf(cmd, sizeof(cmd), "gcc %s\n", px->asmf);

	rv = system(cmd);

	return rv;
}
