
#include "bccx.h"

static REG		g_regs[CPU_NUM_REGS], *g_pacc;

static PSLOCAL	g_tmpstack;
static SLOCAL   g_tmps[MAX_EXPRESSION];
static PSLOCAL  g_freetmps;
static unsigned long g_tmpHighMark;


static struct tag_reserved
{
	REGSTATE	state;
	PSLOCAL		ploc;
}
g_resreg[CPU_NUM_REGS];


extern PANDITEM		ANDpop				(void);
extern void			ANDfree				(PANDITEM pand);

extern int			EMITopcode			(
											PCCTX px,
											PFUNCTION pfunc,
											CODETYPE type,
											OPCODE op,
											PREG ra, PREG rb,
											PSYMREF psa, PSYMREF psb
										);

//***********************************************************************
void REGdumpScore(PANDITEM pandstack)
{
	PANDITEM pand;
	PREG     preg;
	int      r;
	int      regused;
	int		 refcount;
	char     regloc[64];
	
	for(r = 0; r < CPU_NUM_REGS; r++)
	{
		preg = &g_regs[r];
		
		regloc[0] = '\0';
		
		switch(preg->state)
		{
		case rsFREE:

			strcpy(regloc, "free");
			regused = 0;
			break;
			
		case rsLOADED:

			strcpy(regloc, "loaded");
			regused = 1;
			break;
			
		case rsSAVED:

			strcpy(regloc, "saving");
			regused = 1;
			break;

		case rsSTACKED:
			
			for(pand = pandstack, refcount = 0; pand; pand = pand->next)
			{
				if(pand->preg == preg)
				{
					refcount++;
				}
			}
			if(refcount)
			{
				snprintf(regloc, sizeof(regloc), "inuse (%d)", refcount);
				regused = 1;
			}
			else
			{
				Log(logError, 1, "Internal - stacked reg with no reference on stack\n");
				strcpy(regloc, "nowhere?");
			}
			break;
			
		default:
			
			break;
		}
		Log(logDebug, 9, "%6s %14s %s\n", preg->pr->name, regloc,
				regused ? (preg->psymr ? preg->psymr->name : "<unknown user>") : ""
			);
	}
}

//***********************************************************************
int REGprolog()
{
	PSLOCAL  ptmp;
	int r;
	
	// clear the register scoreboard
	// (todo - this should really only need to be done once ever, not every function
	//  but every function is pretty small)
	//
	for(r = 0, g_pacc = NULL; r < CPU_NUM_REGS; r++)
	{
		g_regs[r].pr     = &g_gpregs[r];
		g_regs[r].state  = rsFREE;
		g_regs[r].psymr  = NULL;
		g_regs[r].next   = &g_regs[r + 1];
		
		if(g_gpregs[r].type == rACC)
		{
			// shortcut to accumulator
			g_pacc = &g_regs[r];
		}
	}
	g_regs[r-1].next = NULL;

	if(! g_pacc)
	{
		Log(logError, 0, "Internal - No ACC, Can't compile\n");
	}
	// clear the tmpstack
	//
	while(g_tmpstack)
	{
		ptmp = g_tmpstack->next;
		LOCALfree(g_tmpstack);
		g_tmpstack = ptmp;
	}
	g_tmpHighMark = 0;
	
	// reint temp microallocator
	// (todo - this should really only need to be done once ever)
	//
	for(r = 0; r < MAX_EXPRESSION; r++)
	{
		g_tmps[r].offset = 0;
		g_tmps[r].psymr  = NULL;
		g_tmps[r].next   = &g_tmps[r + 1];
	}
	g_tmps[r-1].next = NULL;
	g_freetmps = g_tmps;	

	return 0;
}

//***********************************************************************
void REGclear(PANDITEM pandstack, int clearall)
{
	PREG	  preg;
	PSLOCAL   ploc;
	int		  r;
	
	for(r = 0; r < CPU_NUM_REGS; r++)
	{
		preg = &g_regs[r];
		
		if(preg->state == rsSTACKED)
		{
			// if not optimizing, or the register represents a volatile
			// value, then free it, else, if optimizing, leave it in the 
			// loaded list in case the next expression could use whats loaded
			//
			if(
					! EMIT_OPTIMIZE(1)
				||	clearall
				|| (preg->psymr && preg->psymr->desc.isvol)
			)
			{
				REGfree(pandstack, preg);
			}
			else
			{
				// save register contents as loaded
				preg->state = rsLOADED;
			}
		}
		else if(preg->state == rsSAVED || clearall)
		{
			preg->state = rsFREE;
		}
	}
	// make sure the overflow stack is 0, if not, there
	// was an error
	//
	for(ploc = g_tmpstack; ploc; ploc = ploc->next)
	{
		if(ploc->psymr)
		{
			//Log(logDebug, 3, "Internal - temp left of stack\n");
			SYMREFdestroy(ploc->psymr);
			ploc->psymr = NULL;
		}
	}
}

//***********************************************************************
PSLOCAL LOCALalloc(PFUNCTION pfunc, PSYMREF psymr)
{
	PSLOCAL  ptmp;
	
	ptmp = g_freetmps;
	if(! ptmp)
	{
		Log(logError, 0, "Internal: Expression too complex\n");
		return NULL;
	}
	g_freetmps = g_freetmps->next;

	ptmp->psymr = psymr;
	ptmp->next = g_tmpstack;
	g_tmpstack = ptmp;
		
	if(! g_tmpstack)
	{
		ptmp->offset = 0;
	}
	else
	{
		ptmp->offset = g_tmpstack->offset + (CPU_ADDRESS_SIZE >> 3);
	}
	if(ptmp->offset >= g_tmpHighMark)
	{
		g_tmpHighMark = (CPU_ADDRESS_SIZE >> 3) + ptmp->offset;
	}
	if(pfunc)
	{
		if(g_tmpHighMark > pfunc->tmpalloc)
		{
			Log(logDebug, 9, "Reg - new highmark is %d\n", g_tmpHighMark);
			pfunc->tmpalloc = g_tmpHighMark;
		}
	}
	g_tmpstack = ptmp;
	return ptmp;
}

//***********************************************************************
void LOCALfree(PSLOCAL ptmp)
{
	if(ptmp)
	{
		if(ptmp != g_tmpstack)
		{
			Log(logError, 0, "Internal: attempt to free local not at top\n");
			return;
		}
		g_tmpstack = g_tmpstack->next;
		if(ptmp->psymr)
		{
			SYMREFdestroy(ptmp->psymr);
		}
		ptmp->psymr = NULL;
		ptmp->next = g_freetmps;
		g_freetmps = ptmp;
	}
}

//***********************************************************************
void REGfreeLocal(PSLOCAL ploc)
{
	LOCALfree(ploc);
}

//***********************************************************************
PREG REGsaveAddr(PCCTX px, PFUNCTION pfunc, PANDITEM pandstack, POPENTRY pop, PREG pregitsin)
{
	PREG     preg;
	
	if(! pop || ! pop->psym)
	{
		return NULL; // can't happen
	}
	// if there is no register, then pop must be a symbolic reference
	// as opposed to a calculated address, in which case it doesn't
	// really need saving now does it
	//
	if(! pregitsin)
	{
		if(pop->psym->name[0] == '(')
		{
			Log(logError, 0, "Internal - attempt to save addr while not in a register\n");
		}
		else
		{
			Log(logDebug, 9, " @@--- Skip save addr for symbol %s\n", pop->psym->name);
		}
		return NULL;
	}
	// if the operand is a plain symbol and in a register, we still don't care to
	// save it, since its NOT really an address, its the contents of the address
	// (plus its a waste of a register since its simple to find the address again)
	//
	if(pop->psym->name[0] != '(')
	{
		Log(logDebug, 9, " @@--- Skip save addr for symbol %s\n", pop->psym->name);
		return NULL;
	}
	// if the register its in is a GPR, just leave it there
	//
	if(pregitsin->pr->type != rACC)
	{
		pregitsin->state = rsSAVED;
		return pregitsin;
	}
	// allocate a GPR
	//
	preg = REGalloc(px, pfunc, pandstack);
	if(preg)
	{
		// copy regitsin to reg
		//
		EMITopcode(px, pfunc, codeMOVE, opEQUAL, preg, pregitsin, NULL, pregitsin->psymr);
		preg->psymr = SYMREFcreateCopy(pregitsin->psymr);
		preg->psymr->desc.isauto;
		preg->state = rsSAVED;
		return preg;
	}
	Log(logError, 0, "Internal: Expression too complex\n");
	return NULL;
}

//***********************************************************************
PREG REGgetSpecific(int index)
{
	if(index < 0 || index >= CPU_NUM_REGS)
	{
		return NULL;
	}
	return &g_regs[index];
}

//***********************************************************************
PREG REGalloc(PCCTX px, PFUNCTION pfunc, PANDITEM pandstack)
{
	PREG     preg;
	PSLOCAL  plocal;
	PANDITEM pand, pstack;	

	// look for a free general purpose register
	//
	for(preg = g_regs; preg; preg = preg->next)
	{
		if(preg->pr->type == rGPR && preg->state == rsFREE)
		{
			return preg;
		}
	}
	// look for a loaded register that can be used
	//
	for(preg = g_regs; preg; preg = preg->next)
	{
		if(preg->pr->type == rGPR && preg->state == rsLOADED)
		{
			Log(logDebug, 5, " ------ ANDsteal --- %s from slacker %s\n",
				preg->pr->name,	preg->psymr->name);
			SYMREFdestroy(preg->psymr);
			preg->psymr = NULL;
			preg->state = rsFREE;
			return preg;
		}
	}
	// wow, nothing available, so gotta get tough now and push
	// off a register in the and stack onto the CPU stack
	//
	// find the last entry in the stack that's in a register
	// and push that on the stack.  That way the operands in
	// the (memory) stack are in the same order in memory they
	// are on the andstack, so a stacked operand used in an
	// operation is always at the top of the mem stack
	// the top of the and stack is always the accumulator reg.
	// so skip that one
	//
	for(pand = pandstack->next; pand && pand->preg; pand = pand->next)
	{
		if(! pand->next || ! pand->next->preg)
		{
			break;
		}
	}
	if(! pand || ! pand->preg)
	{
		// no stacked entries on the and stack with registers
		// so have to push off the accumulator and sacrifice it
		//
		Log(logDebug, 6, "No register entries on and stack\n");
		if(pandstack && pandstack->preg)
		{
			pand = pandstack;
		}
		else
		{
			// this can't happen 
			Log(logError, 0, "Internal, no ACC on operand stack\n");
			return NULL;
		}
	}
	preg = pand->preg;

	Log(logDebug, 5, " ------ ANDpushoffanduse --- %s from %s\n",
			preg->pr->name, preg->psymr->name);

	// allocate from the local storage var pool for the register
	// which gives it an offset if there is no real stack operation
	//
	plocal = LOCALalloc(pfunc, pand->preg->psymr);
	if(! plocal)
	{
		Log(logError, 0, "Internal: no local storage\n");
		return NULL;
	}
	// now, STORE this register on the stack
	//
	EMITopcode(px, pfunc, codeTSTORE, opNONE, NULL, pand->preg, plocal->psymr, NULL);

	pand->preg->psymr = NULL; // plocal now owns symr
	
		
	// this, and any other references to this register
	// on the and stack (others unlikely) will now
	// refer to the stack location
	//
	preg = pand->preg;
	
	for(pstack = pandstack; pstack; pstack = pstack->next)
	{
		if(pstack->preg == preg)
		{
			pstack->ploc = plocal;
				pstack->preg = NULL;
		}
	}
	return preg;
}

//***********************************************************************
void REGfree(PANDITEM pandstack, PREG preg)
{
	PANDITEM plist;
	PREG     derefr = NULL;
	
	if(preg)
	{
		// go through the and stack and if there are any references to
		// this register, then dont actually free it
		// (there could be multiple references on the stack if optimizing
		// reuses common sub expressions)
		//
		for(plist = pandstack; plist; plist = plist->next)
		{
			if(plist->preg == preg)
			{
				derefr = preg;
				break;
			}
		}
		if(derefr)
		{
			Log(logDebug, 5, " ------ ANDderef --- %s\n", preg->pr->name);
			return;
		}
		if(preg->psymr)
		{
			SYMREFdestroy(preg->psymr);
		}
		preg->psymr = NULL;
		preg->state = rsFREE;
	}
}

//***********************************************************************
void REGfreeLoaded(PANDITEM pandstack, PREG preg)
{
	if(preg->state != rsLOADED)
	{
		Log(logWarning, 3, "Internal - attempt to freeload non loaded reg\n");
	}
}

//***********************************************************************
PREG REGfromStack(PCCTX px, PFUNCTION pfunc, PANDITEM pandstack, PREG preg, PSLOCAL ploc)
{
	PANDITEM pstack;
	
	Log(logDebug, 5, " ------ ANDpop --- and load %s from stack %08X\n",
			ploc->psymr->name, ploc->offset);

	preg->psymr   = ploc->psymr;
	preg->state   = rsLOADED;
	ploc->psymr   = NULL;
	
	// for all references to this local store in the and stack
	// replace the local store with the register
	//
	for(pstack = pandstack; pstack; pstack = pstack->next)
	{
		if(pstack->ploc == ploc)
		{
			pstack->preg = preg;
			pstack->ploc = NULL;
		}
	}
	// EMIT code to load this operand off the stack
	//
	EMITopcode(px, pfunc, codeTLOAD, opNONE, preg, NULL, NULL, preg->psymr);
	
	// and pop the ploc stack, insisting that ploc
	// was the top entry (used in stack order of alloc)
	//
	LOCALfree(ploc);
	return preg;
}

//***********************************************************************
PREG REGfromMem(PCCTX px, PFUNCTION pfunc, PREG preg, PSYM psym, int loadaddress)
{			
	// indicate in original sym that it has been referenced
	//
	psym->desc.isdef = 1;
	
	// put a copy of the operand sym entry off the register
	//
	preg->psymr = SYMREFcreate(psym);

	// indicate loaded
	//
	preg->state = rsLOADED;
	
	// EMIT code to load this operand, or the address of it
	//
	EMITopcode(
				px,	pfunc, 
				(loadaddress ? codeLOADADDR : codeLOAD), opNONE, 
				preg, NULL, NULL, SYMREFcreate(psym)
				);
		
	// if loaded an address, indicate symbol is now a pointer and not an array
	// and set its size as the size of the original base type
	//
	if(loadaddress)
	{
		preg->psymr->desc.isptr++;
		preg->psymr->desc.isdim = 0;
		preg->psymr->desc.bsize = preg->psymr->psym->type->desc.bsize;
	}
	return preg;
}

//***********************************************************************
PREG REGloadAcc(PCCTX px, PFUNCTION pfunc, PANDITEM pandstack, POPENTRY popand, int loadaddress)
{
	PREG     retreg, altreg;
	PANDITEM pand;

	if(! popand || ! popand->psym)
	{
		Log(logError, 0, "Internal: No operand for reg alloc\n");
		return NULL;
	}
	if(popand->psym->name[0] != '(')
	{
		// need to load the accumulator with a primary operand
		//
#if 0		
		if(EMIT_OPTIMIZE(1))
		{
			// check if thing is already incidentally loaded but available
			//
			prevreg = NULL;
			for(retreg = g_regs; retreg; retreg = retreg->next)
			{
				if(
						retreg->psymr
					&&	(! strcmp(retreg->psymr->name, psym->name))
				)
				{
					// the symbol we are looking for is in a register already
					// and on the freeloaded list, so copy it to the accumulator
					//
					// [TODO]
					break;
				}
			}
			if(retreg)
			{
				if(psymr->desc.isvol)
				{
					Log(logError, 2, "Internal: volatile value reused\n");
					return NULL;
				}
				Log(logDebug, 5, " @----- ANDpreloaded (%s) --- %s %s\n",
						pand ? "andstack" : "loaded",
						retreg->pr->name, retreg->psymr->name);
						
				return retreg;
			}
		}
#endif
		// if the ACC is free, just use it no questions asked
		//
		for(retreg = g_regs; retreg; retreg = retreg->next)
		{
			if(retreg->pr->type == rACC && retreg->state == rsFREE)
			{
				break;
			}
		}
		// The top of the stack is always the accumulator, if the accumulator is
		// in use (it could have been swapped out, used, then freed), so if there
		// is a top of the stack, and its got a register, it has to be the acc
		// since it would be free otherwise, so swap it out
		//
		if(! retreg)
		{
			if(pandstack && pandstack->preg)
			{
				retreg = pandstack->preg;
				
				Log(logDebug, 7, " ----- AND need to push acc off\n");
				
				if(pandstack->preg->pr->type == rACC)
				{
					// get a general register for the ACC if possible.  The idea
					// is that if stack storage is going to be used, the bottom
					// of the stack is pushed off and the acc takes over its register
					// so all registers used to stack operands are at the top
					// it is rare but possible (when acc and opstack top are swapped
					// to do promotions or dexmul) that the acc IS the second thing
					// on the stack, so that makes altreg the ACC in which case no
					// move is needed to put acc contents into altreg
					//
					altreg = REGalloc(px, pfunc, pandstack);
					if(altreg)
					{
						if(altreg != retreg)
						{
							EMITopcode(px, pfunc, codeMOVE, opEQUAL,
									altreg, retreg,
									NULL, retreg->psymr);
							altreg->state = rsSTACKED;
							altreg->psymr = retreg->psymr;
							retreg->state = rsFREE;
							retreg->psymr = NULL;
							pandstack->preg = altreg;
						}
					}
					else
					{
						// can't happen, altreg should at least be the acc
						return NULL;
					}
				}
				else
				{
					Log(logError, 1, "Internal - top of andstack not in ACC\n");
					return NULL;
				}
			}
			else // no andstack, acc must be free
			{
				// acc MUST be free, so just get it
				//
				for(retreg = g_regs; retreg; retreg = retreg->next)
				{
					if(retreg->pr->type == rACC)
					{
						break;
					}
				}
				if(! retreg)
				{
					Log(logError, 1, "Internal - ACC not free with no stacked regs\n");
					return NULL;
				}
				if(retreg->state == rsSTACKED)
				{
					Log(logError, 1, "Internal - Taking ACC from stack but no stack\n");
					return NULL;
				}
				retreg->state = rsFREE;
			}
		}
		if(retreg)
		{
			retreg = REGfromMem(px, pfunc, retreg, popand->psym, loadaddress);
			return retreg;
		}
		else
		{
			Log(logError, 1, "Internal - no acc for operand\n");
		}
	}
	else if(popand->psym->name[0] == '(')
	{
		PSLOCAL ploc;
		
		// intermediate result and on top of the stack, so if its not in the accumulator
		// then it should be, so it must have been pushed off at some point, so put it back
		// (assumes the acc is free, since whatever was in it is popped off now)
		//
		// pop the andstack means pandstack needs to go to next
		//
		if(pandstack)
		{
			pandstack = pandstack->next;
		}
		pand = ANDpop();
		if(! pand)
		{
			Log(logError, 1, "Internal - no stacked operand to get register for\n");
			return NULL;
		}
		// the register is still in use here (in the operation going to
		// happen right after this call) so don't let ANDfree take the 
		// register with it
		//
		ploc   = pand->ploc;
		altreg = pand->preg;
		pand->preg = NULL;
		pand->ploc = NULL;
		ANDfree(pand);

		// the typical case is that this operand is already in the accumulator, so
		// check that first and return the acc if so
		//
		if(altreg && (altreg->pr->type == rACC))
		{
			Log(logDebug, 5, " ------ ANDpopacc - %s %s\n", altreg->pr->name,
					altreg->psymr->name);
			return altreg;
		}
		// not in the acc, so at least find the acc
		//
		for(retreg = g_regs; retreg; retreg = retreg->next)
		{
			if(retreg->pr->type == rACC)
			{
				break;
			}
		}
		if(! retreg)
		{
			// really can't happen!
			return NULL;
		}
		if(retreg->state == rsSTACKED)
		{
			Log(logError, 0, "Internal - ACC in use but needed for operand\n");
			return NULL;
		}
		if(altreg)
		{
			Log(logDebug, 5, " ------ ANDpop --- %s %s\n",
					altreg->pr->name,
					altreg->psymr->name);

			// must have been pushed off before, or from saved so move it from altreg to regreg
			//
			EMITopcode(px, pfunc, codeMOVE, opEQUAL, retreg, altreg,
					NULL, altreg->psymr);
			
			// free up previous register unless its on the saved list
			//
			retreg->state = rsLOADED;
			retreg->psymr = altreg->psymr;
			altreg->state = rsFREE;
			altreg->psymr = NULL;
		}
		else if(ploc)
		{
			// was saved away in function stack frame, re-load it
			//
			retreg = REGfromStack(px, pfunc, pandstack, retreg, ploc);
		}
		else
		{
			Log(logError, 1, "Internal - no location for operand\n");
			return NULL;
		}
	}
	return retreg;
}

//***********************************************************************
PREG REGforOpand(PCCTX px, PFUNCTION pfunc, PANDITEM pandstack, POPENTRY popand, int loadaddress)
{
	PREG     retreg;
	PANDITEM pand;
	
	// get a register symbol psym
	//
	// if the symbol is an intermediate result, it should be the top of the 
	// and stack, and may already be in a register, else need to allocate a
	// register (maybe even the accumulator) for it.
	//
	if(! popand || ! popand->psym)
	{
		Log(logError, 0, "Internal: No operand for reg alloc\n");
		return NULL;
	}
	if(popand->psym->name[0] == '(')
	{
		PSLOCAL ploc;
		
		// already on stack I hope
		//
		if(
				!pandstack
			||	(pandstack->preg && pandstack->preg->psymr->psym != popand->psym)
			||	(pandstack->ploc && pandstack->ploc->psymr->psym != popand->psym)
		)
		{
			Log(logError, 0, "Internal: loading operand which isn't on stack top\n");
			return NULL;
		}
		// pop the andstack means pandstack needs to go to next
		//
		if(pandstack)
		{
			pandstack = pandstack->next;
		}
		pand = ANDpop();
		retreg = pand->preg;
		ploc   = pand->ploc;
		pand->preg = NULL;
		pand->ploc = NULL;
		ANDfree(pand);

		if(retreg)
		{
			// already loaded, no problem
			//
			return retreg;
		}
		else if(ploc)
		{
			// need to load reg from stack
			//
			retreg = REGalloc(px, pfunc, pandstack);
			if(retreg)
			{
				retreg = REGfromStack(px, pfunc, pandstack, retreg, ploc);
			}
			else
			{
				Log(logError, 0, "Internal: no register for stacked operand\n");
			}
		}
		else
		{
			Log(logError, 0, "Internal: no location for operand\n");
			return NULL;
		}
	}
	else
	{
		// a raw symbol needing to load
		//
		retreg = REGalloc(px, pfunc, pandstack);
		if(retreg)
		{
			retreg = REGfromMem(px, pfunc, retreg, popand->psym, loadaddress);
			return retreg;
		}
		else
		{
			Log(logError, 0, "Internal: no register for symbolic operand\n");
		}
	}
	return retreg;
}

//***********************************************************************
PREGREC REGreserve(PCCTX px, PFUNCTION pfunc, PREGREC regrec)
{
	PSLOCAL ploc;
	PREG    preg;
	int		regdex;

	// code gen needs a register absolutely.  first find if
	//
	for(preg = &g_regs[0], regdex = 0; preg; preg = preg->next)
	{
		if(preg->pr == regrec)
			break;
		regdex++;
	}
	if(! preg)
	{
		Log(logError, 0, "Internal - no such register to reserve %s\n", regrec->name);
		return NULL;
	}
	Log(logDebug, 8, " +++ Codegen needs reg %s -- \n", regrec->name);

	// if free, just return it
	//
	if(preg->state == rsFREE)
	{
		Log(logDebug, 8, " ++++++ and its free\n");
		preg->state = rsRESERVED;
		return preg->pr;
	}
	// if loaded, unload and return
	//
	if(preg->state == rsLOADED)
	{
		Log(logDebug, 5, " ++++++ steal --- %s from slacker %s\n",
				preg->pr->name,	preg->psymr->name);
		SYMREFdestroy(preg->psymr);
		preg->psymr = NULL;
		preg->state = rsRESERVED;
		return preg->pr;
	}
	if(preg->state == rsRESERVED)
	{
		// this can't happen unless a code generator forgets to unreserve
		//
		Log(logError, 0, "Internal - code gen forgot to unreserve %s\n", regrec->name);
	}
	// at this point, the register is either saved for use in an inc/dec or ternary
	// or it is on the stack, which means I have to push it off, use it, and pop it
	// back.  The good news is that I don't have to tell anyone, since reserving a
	// register is only valid over one call to CODEgenerate() 
	//
	// todo - look at free regs and move this reg into a free one and back at the end
	// but there is usually no free ones in small reg cpus and usually reserved ones
	// are at the end of the reg reg in large reg cpus, so shouldnt be possible very
	// often 
	//
	ploc = LOCALalloc(pfunc, preg->psymr);
	if(! ploc) return NULL;

	preg->psymr = NULL;

	// push the reg on the stack
	EMITopcode(px, pfunc, codeTSTORE, opNONE, NULL, preg, ploc->psymr, NULL);

	// store the ploc we used in the reservation scoreboard at reg index
	g_resreg[regdex].ploc = ploc;

	// and the state
	g_resreg[regdex].state = preg->state;
	preg->state = rsRESERVED;

	return preg->pr;
}

//***********************************************************************
int REGunreserve(PCCTX px, PFUNCTION pfunc, PREGREC regrec)
{
	PSLOCAL ploc;
	PREG    preg;
	int		regdex;

	for(preg = &g_regs[0], regdex = 0; preg; preg = preg->next)
	{
		if(preg->pr == regrec)
			break;
		regdex++;
	}
	if(! preg)
	{
		Log(logError, 0, "Internal - no such register to unreserve %s\n", regrec->name);
		return 1;
	}
	Log(logDebug, 8, " +++ Codegen unreserves reg %s -- \n", regrec->name);

	if(preg->state != rsRESERVED)
	{
		// this can't happen unless a code generator forgets to unreserve
		//
		Log(logError, 0, "Internal - code gen didn't reserve %s\n", regrec->name);
		return 2;
	}
	ploc = g_resreg[regdex].ploc;
	if(! ploc)
	{
		Log(logError, 0, "Internal - never saved %s for reserve\n", regrec->name);
		return 3;
	}
	// pop the reg off the stack
	EMITopcode(px, pfunc, codeTLOAD, opNONE, preg, NULL, NULL, ploc->psymr);

	g_resreg[regdex].ploc = NULL;

	// and the state
	preg->state = g_resreg[regdex].state;
	preg->psymr = ploc->psymr;
	ploc->psymr = NULL;
	LOCALfree(ploc);

	return 0;
}
