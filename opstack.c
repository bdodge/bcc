
#include "bccx.h"

/*
 * The OP stack converts the expressions encountered in the source
 * into a sequnce of RPN operands and operators suitable to parse
 * directly into assembler or pcode
 *
 * The OP stack entry format is always
	
	//    OPERAND          <---- top
	//    unary operator
	// or
	//    OPERAND          <---- top
	//    binary operator
	//    OPERAND
	//
 *
 * the pops and operations that happen are enlisted into
 * the functions code list
 */


//***********************************************************************
POPENTRY OPcreate(OPTYPE type, OPERATOR op, PSYM psym)
{
	POPENTRY pop;
	
	pop = (POPENTRY)malloc(sizeof(OPENTRY));
	if(! pop)
	{
		Log(logError, 0, "Internal: No opstack memory\n");
		return  NULL;
	}
	pop->type	= type;
	pop->op		= op;
	pop->psym	= psym;
	pop->next	= NULL;
	return pop;
}

//***********************************************************************
void OPdestroy(POPENTRY pop)
{
	if(pop)
	{
		free(pop);
	}
}

//***********************************************************************
void OPdump(const char* msg, int dbglev, POPENTRY pop)
{
	char ts[128];
	char* sps;

	if(pop)
	{
		switch(pop->type)
		{
		case opERAND:
			if(OPisIncDec(pop->op))
				sps = "use sv address";
			else
				sps = "";
			SYMTABtypeString(pop->psym, ts, sizeof(ts));
			Log(logDebug, dbglev, "%s op erand %s %s\n", msg, ts, sps);
			break;
		case opERATOR:
			Log(logDebug, dbglev, "%s op erator %s\n", msg, OPname(pop->op));
			break;
		case opDEXMUL:
			Log(logDebug, dbglev, "%s dexmul X %s\n", msg, pop->psym->name);
			break;
		case opRESULT:
			Log(logDebug, dbglev, "%s op result %s\n", msg, pop->psym->name);
			break;
		case opMARKER:
			Log(logDebug, dbglev, "%s (MARKER\n", msg);
			break;
		case opARGUMENT:
			Log(logDebug, dbglev, "%s ARGUMENT)\n", msg);
			break;
		case opLABEL:
			Log(logDebug, dbglev, "%s op label %s\n", msg, pop->psym->name);
			break;
		case opGOTO:
			Log(logDebug, dbglev, "%s op label %s\n", msg, pop->psym->name);
			break;
		case opALLOC:
			Log(logDebug, dbglev, "%s ALLOC %s\n", msg, pop->psym->name);
			break;
		default:
			Log(logError, 0, "Internal: bad opstack entry %d\n", pop->type);
			break;
		}
	}
	else
	{
		Log(logDebug, dbglev, "%s op - empty\n", msg);
	}
}

//***********************************************************************
POPENTRY OPpush(POPENTRY* pstack, POPENTRY pop)
{
	OPdump("     PUSH", 5, pop);
	pop->next	= *pstack;
	*pstack		= pop;
	return pop;
}

//***********************************************************************
POPENTRY OPpushOpand(POPENTRY* pstack, PSYM psym)
{
	POPENTRY pop;
	
	pop = OPcreate(opERAND, opNONE, psym);
	return OPpush(pstack, pop);
}

//***********************************************************************
POPENTRY OPpushOpandResult(POPENTRY* pstack, PSYM psym, const char* newname)
{
	POPENTRY pop;
	
	if(newname && psym)
		SYMrename(psym, newname);
	pop = OPcreate(opERAND, opNONE, psym);	
	return OPpush(pstack, pop);
}

//***********************************************************************
POPENTRY OPpushOper(POPENTRY* pstack, OPERATOR op)
{
	POPENTRY pop;


	// convert subtract to negate if first, or after operator
	//
	if(
			op == opMINUS && pstack &&
			(! *pstack || (*pstack)->type == opERATOR)
	)
	{
		op = opNEGATE;
		Log(logError, 0, "PREFIX minus leaked into oppush\n");
		exit(0);
	}
	pop = OPcreate(opERATOR, op, NULL);
	return OPpush(pstack, pop);
}

//***********************************************************************
POPENTRY OPpushMarker(POPENTRY* pstack)
{
	POPENTRY pop;

	pop = OPcreate(opMARKER, opNONE, NULL);
	return OPpush(pstack, pop);
}

//***********************************************************************
int OPpullMarker(POPENTRY* pstack)
{
	POPENTRY pop, pm;

	if(! pstack || ! *pstack)
	{
		return 1;
	}
	pop = *pstack;
	if(pop->next)
	{
		pm = pop->next;
		if(pm->type == opMARKER)
		{
			Log(logDebug, 7, "OP Pulling Marker\n");
			pop->next = pm->next;
			OPdestroy(pm);
			return 0;
		}
	}
	Log(logError, 0, "Internal - expected marker\n");
	return 1;
}

//***********************************************************************
POPENTRY OPpushArgsList(POPENTRY* pstack, int argn)
{
	POPENTRY pop;

	pop = OPcreate(opARGUMENT, (OPERATOR)argn, NULL);
	return OPpush(pstack, pop);
}

//***********************************************************************
POPENTRY OPpop(POPENTRY* pstack)
{
	POPENTRY pop = NULL;
	
	if(pstack && *pstack)
	{
		pop = *pstack;
		*pstack = (*pstack)->next;
	}
	OPdump("     POP", 5, pop);
	return pop;
}

//***********************************************************************
POPENTRY OPtop(POPENTRY pstack)
{
	POPENTRY pop = NULL;
	
	if(pstack)
	{
		pop = pstack;
	}
	return pop;
}

//***********************************************************************
void OPdumpStack(POPENTRY* pstack, int dbglev, char* msg)
{
	POPENTRY po;

	Log(logDebug, dbglev, "---------- %s --------\n", msg);
	for(po = *pstack; po; po = po->next)
		OPdump("on stack", dbglev, po);
	Log(logDebug, dbglev, "---- end of stack\n");
}			

//***********************************************************************
int OPisPromotion(OPERATOR op)
{
	return op >= opPROMOTE2UNSIGNED && op <= opPROMOTEFLT2DBL;
}

//***********************************************************************
int OPpromote(PFUNCTION pfunc, PSYM psym)
{
	PSYM pbase;
	
	// do the implicit promotion rules
	//
	// 1) char and short are converted to int
	// 2) float goes to double
	// 3) pointer types are left alone
	//
	if(! psym)
		return 0;
	if(psym->desc.isptr > 0 || psym->desc.isdim > 0)
		return 0;
	
	if(! psym->desc.isreal)
	{
		switch(psym->desc.bsize)
		{
#if CPU_INT_SIZE > CPU_CHAR_SIZE
		case CPU_CHAR_SIZE:
			Log(logDebug, 5, "     --- DO promote cI %s\n", psym->name);
			// promote symbol's base type and size (but leave unsigned, etc.)
			pbase = GetIntTypeForSize(CPU_INT_SIZE);
			psym->type = pbase;
			psym->desc.bsize = pbase->desc.bsize;
			FUNCaddop(pfunc, OPcreate(opERATOR, (psym->desc.isuns) ?
					opPROMOTEUCHAR2UINT : opPROMOTECHAR2INT, SYMcreateCopy(psym)));
			break;
#endif
#if CPU_INT_SIZE > CPU_SHORT_SIZE
		case CPU_SHORT_SIZE:
			Log(logDebug, 5, "     --- DO promote sI %s\n", psym->name);
			// promote symbol's base type and size (but leave unsigned, etc.)
			pbase = GetIntTypeForSize(CPU_INT_SIZE);
			if(! pbase)
				return 1;
			psym->type = pbase;
			psym->desc.bsize = pbase->desc.bsize;
			FUNCaddop(pfunc, OPcreate(opERATOR, (psym->desc.isuns) ?
					opPROMOTEUSHORT2UINT : opPROMOTESHORT2INT, SYMcreateCopy(psym)));
			break;
#endif
		default:
			break;
		}
	}
	else
	{
#ifdef CPU_DOUBLE_SIZE
		if(psym->desc.bsize < CPU_DOUBLE_SIZE)
		{
			Log(logDebug, 5, "     --- DO promote fD %s\n", psym->name);
			// promote symbol's base type and size
			pbase = GetRealTypeForSize(CPU_DOUBLE_SIZE);
			if(! pbase)
				return 1;
			psym->type = pbase;
			psym->desc.bsize = pbase->desc.bsize;
			FUNCaddop(pfunc, OPcreate(opERATOR, opPROMOTEFLT2DBL, SYMcreateCopy(psym)));
		}
#endif
	}
	return 0;
}

//***********************************************************************
int OPcheckTypes(PFUNCTION pfunc, PSYM psym1, PSYM psym2)
{
	if(! psym1->desc.isreal && ! psym2->desc.isreal)
	{
#if (CPU_LONG_SIZE > CPU_INT_SIZE) || (CPU_LONG_LONG_SIZE > CPU_INT_SIZE)
		// note this has to be called twice, with operands reversed
		// so the code gets appended directly after each load!
		//
		while(psym2->desc.islong > psym1->desc.islong)
		{
			if(psym1->desc.islong)
			{
				Log(logDebug, 5, "     --- DO promote lLL %s\n", psym1->name);
				
				psym1->desc.islong++;
				psym1->desc.isuns = psym2->desc.isuns;
				
				FUNCaddop(pfunc, OPcreate(opERATOR, 
						psym2->desc.isuns ?
							opPROMOTEULONG2ULONGLONG :	opPROMOTELONG2LONGLONG,
							SYMcreateCopy(psym1)));
			}
			else
			{
				psym1->desc.islong++;
				psym1->desc.isuns = psym2->desc.isuns;
				
				Log(logDebug, 5, "     --- DO promote iL %s\n", psym1->name);
				
				FUNCaddop(pfunc, OPcreate(opERATOR,
						psym2->desc.isuns ?
							opPROMOTEUINT2ULONG : opPROMOTEINT2LONG,
							SYMcreateCopy(psym2)));
			}
		}
#endif
		if(psym2->desc.isuns != 0 && psym1->desc.isuns == 0)
		{
			// this can't happen, since operand's unsignedness should be
			// the FIRST thing that gets promoted, even before sizes
			//
			Log(logError, 0, "Internal: mix of signed and unsigned operands\n");
		}
	}
	else
	{
#ifdef CPU_DOUBLE_SIZE
		if(psym2->desc.isreal != 0 && psym1->desc.isreal == 0)
		{
			Log(logDebug, 5, "     --- DO promoteD %s\n", psym1->name);
			FUNCaddop(pfunc, OPcreate(opERATOR, opPROMOTEFLT2DBL, NULL));
		}
#endif
	}
	return 0;
}

//***********************************************************************
int OPenlist(PFUNCTION pfunc, POPENTRY pop, const char* msg)
{
	OPdump(msg, 5, pop);
	return FUNCaddop(pfunc, pop);
}
	
//***********************************************************************
int OPcheckPointerBinaryMath(OPTYPE op, PSYM psym1, PSYM psym2)
{
	// check for pointer math on pand1 here
	//
	if(psym1->desc.isptr)
	{
		if(psym2->desc.isptr)
		{
			switch(op)
			{
			case opEQUAL:
			case opBOOLEQ:
			case opBOOLNEQ:
			case opBOOLLT:
			case opBOOLGT:
			case opBOOLLTEQ:
			case opBOOLGTEQ:
			case opMINUS:
				if(psym1->desc.isptr != psym2->desc.isptr)
				{
					Log(logError, 0, "Different levels of indirection %s %s %s\n", 
							psym1->name, OPname(op), psym2->name);
				}
				break;
			default:
				Log(logError, 0, "Invalid binary pointer/pointer operation %s\n", OPname(op));
				return -1;
			}
		}
		else
		{
			if(
					op != opADD
				&&	op != opMINUS
				&&	op != opINDEX
				&&	op != opOFFSET
				&&	op != opEQUAL
			)
			{
				Log(logError, 0, "Invalid binary pointer operation %s\n", OPname(op));
				return -1;
			}
			else if(op != opEQUAL && op != opOFFSET)
			{
				// flag pand2 to indicate it needs a multiply by sizeof type
				// pointed to by psym1
				//
				if(psym1->desc.isptr > 1)
					return SYMgetSizeBytes(psym1);
				else
					return SYMgetSizeBytes(SYMbaseType(psym1->type));
			}
		}
	}
	return 0;
}

//***********************************************************************
int OPcheckPointerUnaryMath(OPTYPE op, PSYM psym1)
{
	// check for pointer math on pand1 here
	//
	if(psym1->desc.isptr)
	{
		switch(op)
		{
		case opPREINC:
		case opPREDEC:
		case opPOSTINC:
		case opPOSTDEC:
			if(psym1->desc.isptr > 1)
				return SYMgetSizeBytes(psym1);
			else
				return SYMgetSizeBytes(SYMbaseType(psym1->type));
			
		case opADDROF:
		case opDEREF:
		case opCAST:
			break;
		default:
			Log(logError, 0, "Invalid unary pointer operation %s\n", OPname(op));
			return -1;
		}
	}
	return 0;
}

//***********************************************************************
int OPaddDexMul(PFUNCTION pfunc, int mul)
{
	static PSYM pt = NULL;
	PSYM     psym;
	POPENTRY pres, pi;
	char valbuf[32];
	
	if(mul == 1)
	{
		// skip dexmul by 1, no point
		return 0;
	}
	// some easy optimizations always done
	//
	// there has to be an index at the end of the func code list
	// so if that's a literal, just do the math now
	//
	pi = pfunc->pendofcode;
	if(pi)
	{
		if(pi->type == opERAND && pi->psym && pi->psym->desc.isconst && pi->psym->desc.islit)
		{
			int  oldval;

			oldval = strtol(pi->psym->name, NULL, 0);
			if(oldval != 0)
			{
				snprintf(valbuf, sizeof(valbuf), "%d", mul * oldval);

				psym = SYMcreate(valbuf);
				SYMsetType(psym, pi->psym->type, &pi->psym->desc);
				psym->desc.islit   = 1;
				psym->desc.isconst = 1;
				psym->desc.istatic = 1;
				pi->psym = psym;

				Log(logDebug, 6, " @@--- dexmul just changes index\n");
				return 0;
			}
			else
			{
				Log(logDebug, 6, " @@--- dexmul detects 0 index, skipping\n");
				return 0;
			}
		}
	}
	if(! pt)
	{
		pt = GetIntTypeForSize(CPU_INT_SIZE);
	}
	if(pt)
	{
		snprintf(valbuf, sizeof(valbuf), "%d", mul);
		psym = SYMcreate(valbuf);
		SYMsetType(psym, pt, &pt->desc);
		psym->desc.islit   = 1;
		psym->desc.isconst = 1;
		psym->desc.istatic = 1;

		pres = OPcreate(opDEXMUL, opMUL, psym);

		OPenlist(pfunc, pres, "     --- DO enlistmul ");
	}
	return 0;
}

//***********************************************************************
int OPeval(PFUNCTION pfunc, POPENTRY* pstack, OPERATOR inop)
{
	static char exp[MAX_EXPRESSION_NAME];
	
	POPENTRY pand1;
	POPENTRY pand2;
	POPENTRY poper;
	POPENTRY pop;
	POPENTRY pres;
	PSYM     psym, pand1sym, pand2sym;
	int		 pand1mul, pand2mul;
	int      rv = 0;
	
	// evaluate the op stack while stacked operations
	// have higher precedence than the one trying to 
	// be pushed on the stack
	//
	Log(logDebug, 5, "  EVAL\n");
		
	OPdumpStack(pstack, 10, "evals");
	
	do
	{
		// need at least TWO things on the stack to do anything
		// except force it to be one operand
		//
		if(! pstack)
			return 0;	
		if(! *pstack)
			return 0;
		if(OPprecedence(inop) != 0)
			if(! (*pstack)->next)
				return 0;

		// there has to be an operand on top of the stack in order to do anything
		//
		pand1  = *pstack;
		if(pand1->type == opERAND)
		{
			if(
					! pand1->next
				||	(
						(pand1->next->type != opERATOR)
					&&	(pand1->next->type != opMARKER)
					&&	(pand1->next->type != opARGUMENT)
					)
			)
			{
				if(inop == opFORCE || inop == opNONE || inop == opSTATEMENT || inop == opCOMMA)
				{
					break;
				}
				Log(logError, 0, "Internal: OP stack order\n");
				return 1;
			}
			pop = pand1->next;
			
			if(pop->type == opARGUMENT)
			{
				// only evaluate past an argument marked for FORCE (the right paren)
				// in that way, the arguments will be in reverse order on the stack
				// like they should be
				//
				if(inop == opFORCE)
				{
					// the operand on the stack is a function call argument
					// the argument "operator" is really the lowest priority
					// so if inop is a force/statement, pop off the arg
					//
					// pop the argument operand off the stack
					pop = OPpop(pstack);
					pop->type = opARGUMENT;
					OPenlist(pfunc, pop, "     --- DO enlistarg ");
					
					// promote arguments to standard C fashion
					psym = SYMcreateCopy(pop->psym);
					OPpromote(pfunc, psym);
					SYMdestroy(psym);
					
					// pop the argument marker off
					pop = OPpop(pstack);
					OPdestroy(pop);
				}
				else
				{
					// not ready to eval yet
					//
					break;
				}
			}
			else if(! OPisUnary(pop->op))
			{
				if(! pop->next || pop->type == opMARKER)
				{
					return 0; // too early to eval
				}
				Log(logDebug, 5, "   eval check binop %s (%d) against %s (%d)\n",
						OPname(pop->op), OPprecedence(pop->op), 
						OPname(inop), OPprecedence(inop));

				if(
						(OPprecedence(pop->op) > OPprecedence(inop))
					||	
						// for same precedence, do the op if associativity is left-to-right
						// otherwise stop and let the new right-to-left operation on the stack
						(
							(OPprecedence(pop->op) == OPprecedence(inop))
						&&	(OPassociativity(inop) == 0)
						)
				)
				{
					// stack binary operation more important 
					// than incoming operation, so do it
					//
					if((*pstack) && (*pstack)->next)
					{
						if((*pstack)->next->op == opCOMMA)
						{
							// a comma, pop the operator and older op off the stack
							// and leave the new operand on the stack, no math!
							//
							pand2 = *pstack;
							poper = (*pstack)->next;
							pand1 = poper ? poper->next : NULL;
							pand2->next = pand1 ? pand1->next : poper->next;
	
							// still put the comma on the code list so the emitter can
							// pop the operand off too
							//
							OPenlist(pfunc, poper, "     --- DO enlistop ");
							
							if(pand1)
							{
								// discarded older operand
								OPdestroy(pand1);
							}
							// check rest of stack, which looks the same now, just
							// missing an older operand/result anda comma
							continue;
						}
					}
					// pand1 = pand1 OP pand2
					//
					pand2 = OPpop(pstack);
					poper = OPpop(pstack);
					pand1 = OPpop(pstack);
										
					if(
								! pand1
							||	(pand1->type != opERAND)
							||	! pand2
							||	(pand2->type != opERAND)
					)
					{
						Log(logError, 0, "Missing operand for expression\n");
						rv = 1;
						break;
					}
					if(pop->op == opEQUAL)
					{
						// make sure the destination is an lval, i.e. a place.  possible are
						//
						// symbolname
						// (*(...))
						//
						// things that are NOT legal are consts that are defined, lits, 
						// arrays, structs, and intermediate results
						//
						if(pand1->psym->name[0] == '(')
						{
							// an intermediate result, only derefs are allowed
							//
							if(pand1->psym->name[1] != '*')
							{
								Log(logError, 0, "Bad lValue in assignment\n");
								return 1;
							}
						}
						if(pand1->psym->desc.isconst)
						{
							if(pand1->psym->desc.isdef)
							{
								Log(logError, 0, "Attempted assignment to const %s\n",
										pand1->psym->name);
								return 1;
							}
						}
						if(pand1->psym->desc.isaggrt && ! pand1->psym->desc.isptr)
						{
							Log(logError, 0, "%s is not a legal lValue\n",
									pand1->psym->name);
							return 1;
						}
						/*** this might be a warning at leasdt
						if(pand1->psym->desc.isdim)
						{
							Log(logError, 0, "Can't assign to array type %s\n",
									pand1->psym->name);
							return 1;
						}
						*/
					}
					// struct ops - replace a.b with *((bytes*)&a + b)
					//              replace a->b with *((byte*)a + b)
					//
					if(poper->op == opSDEREF || poper->op == opPDEREF)
					{
						POPENTRY pm;
						int      skipderef;

						// note the (byte*) cast is not really added, just
						// to show what really happens, the pointermath is
						// not done (see dexmul generation)
						//
						// also, turn off auto-deref here if the in-op is sderef
						// or there is an addrof underneath, just like I do for
						// index operations on arrays
						//
						OPpushMarker(pstack);
						skipderef = 1;

						if(inop == opSDEREF || inop == opPDEREF)
						{
							Log(logDebug, 7, "Skip struct auto-deref cause structref\n");
						}
						else if(
									pstack
								&&	*pstack
								&& (*pstack)->type == opERATOR
								&& (*pstack)->op == opADDROF
						)
						{
							Log(logDebug, 7, "Skip struct auto-deref cause addrof\n");
							// & array[n] is the same as what this result is
							OPpop(pstack);
						}
						else
						{
							skipderef = 0;
							OPpushOper(pstack, opDEREF);
							OPpushMarker(pstack);
						}
						if(poper->op == opSDEREF)
						{
							// its a struct not a ptr, so take addr of
							OPeval(pfunc, pstack, opADDROF);
							OPpushOper(pstack, opADDROF);
						}
						OPpush(pstack, pand1);

						// note: force the addrof, which normally has lower
						// precedence than offset and have to use the force
						// op since addrof is right-to-left assoc
						//
						OPeval(pfunc, pstack, opFORCE);

						OPpushOper(pstack, opOFFSET);
		
						OPpush(pstack, pand2);

						// eval to inner paren level (the (&a + b))
						OPeval(pfunc, pstack, opFORCE);

						// pull the marker
						//
						pm = pstack ? *pstack : NULL;
						if(pm && pm->next && pm->next->type == opMARKER)
						{
							pm = pm->next;
							(*pstack)->next = pm->next;
							OPdestroy(pm);
						}
						else
						{
							Log(logError, 0, "Bad structure expression\n");
							return 1;
						}
						// eval to outer paren level (the (*(&a + b)))
						if(! skipderef)
						{
							OPeval(pfunc, pstack, opFORCE);

							// pull the marker
							//
							pm = pstack ? *pstack : NULL;
							if(pm && pm->next && pm->next->type == opMARKER)
							{
								pm = pm->next;
								(*pstack)->next = pm->next;
								OPdestroy(pm);
							}
							else
							{
								Log(logError, 0, "Bad structure expression\n");
								return 1;
							}
						}
						return OPeval(pfunc, pstack, inop);
					}
					// optimizer - replace operations with two
					// contants/literals with the actual result
					//
					if(
						EMIT_OPTIMIZE(1)
						&&	OPconstMathPossible(poper->op)
						&&	(! pand2->psym->desc.isreal)
						&&	(
								pand2->psym->desc.islit 
								/*	||  (pand2->psym->desc.isconst && pand2->psym->desc.isdef) */
							)
						&&	(! pand1->psym->desc.isreal)
						&&	(
								pand1->psym->desc.islit 
								/*	||  (pand1->psym->desc.isconst && pand1->psym->desc.isdef) */
							)
					)
					{
						// everybody is a constant, yay, so do the math
						//
						OPdotheMath(pand1->psym, pand2->psym, poper, exp);
						Log(logDebug, 5, " OP math %s %s %s => %s\n",
								pand2->psym->name, OPname(poper->op), pand1->psym->name, exp);

						pand1 = OPpushOpandResult(pstack, SYMcreateCopy(pand1->psym), exp);

						// note - no result record is made, no need
					}
					else
					{
						// append the opentries on the function code list
						// in "stack" order, i.e. they should be in the
						// order they were on in the opstack, i.e. pand2 pand1
						//
						// note that the symbols are added PRE promotion
						// so that the emitter knows what type they actually
						// are in memory, etc.
						//
						pand1sym = SYMcreateCopy(pand1->psym);
						pand2sym = SYMcreateCopy(pand2->psym);

						// a reference to an array is really a pointer, so convert it
						// for example: int* a[1][2] is made int* a, makes ptr math
						// checking a lot easier
						//
						// note that original type is maintained in enlistment
						//
						if(pand1sym->desc.isdim)
						{
							pand1sym->desc.isptr++;
							pand1sym->desc.isdim = 0;
						}
						if(pand2sym->desc.isdim)
						{
							pand2sym->desc.isptr++;
							pand2sym->desc.isdim = 0;
						}						
						if(poper->op == opINDEX)
						{
							pand1mul = 0;

							// the indexor has to be a simple integer type
							// 
							if(pand2->psym->desc.isaggrt || pand2->psym->desc.isdim
								|| pand2->psym->desc.isptr || pand2->psym->desc.isreal
								|| pand2->psym->desc.isvoid)
							{
								Log(logError, 0, "Non-integer type as index\n");
								return 1;
							}
							if(pand1->psym->desc.isdim > 1)
							{
								unsigned long dim;

								if(pand1->psym->members)
									dim = strtoul(pand1->psym->members->name, NULL, 0);
								else
									dim = 1;
								if(dim == 0) dim = 1; // can't happen
								pand2mul = (pand1->psym->desc.bsize / dim);
								pand2mul *= SYMgetSizeBytes(SYMbaseType(pand1->psym->type));
							}
							else
							{
								// indexing single dimension array or indexing a pointer
								// can just use regular pointer math multiplier
								//
								if(pand1sym->desc.isptr)
								{
									pand2mul = OPcheckPointerBinaryMath(poper->op, pand1sym, pand2sym);
								}
								else
								{
									Log(logError, 0, "Attempt to index non pointer type\n");
									return 1;
								}
							}
						}
						else
						{
							// check pointer math legality and set flags to fudge indexing
							//
							if(pand1sym->desc.isptr)
								pand2mul = OPcheckPointerBinaryMath(poper->op, pand1sym, pand2sym);
							else
								pand2mul = 0;
							if(pand2mul >= 0 && pand2sym->desc.isptr)
								pand1mul = OPcheckPointerBinaryMath(poper->op, pand2sym, pand1sym);
							else
								pand1mul = 0;
						}
						// add operator and original operand 2 on list
						//
						OPenlist(pfunc, poper, "     --- DO enlistop ");

						OPenlist(pfunc, pand2, "     --- DO enlistand ");

						if(! pand2sym->desc.isuns && pand1sym->desc.isuns)
						{
							Log(logDebug, 7, "  promoting operand2 to unsigned\n");
							pand2sym->desc.isuns = 1;
						}
						// do the default "C" promotions in operand 2 now unless its
						// an equal, so can skip that if pand1 is the same size as pand2
						// already (i.e. the  store can be a byte=byte or word=word or
						// even *(byte*)=byte without having to promote the operand to an int)
						//
						if(
								poper->op == opEQUAL
							&&	(pand1sym->desc.bsize <= pand2sym->desc.bsize)
						)
						{
							Log(logDebug, 5, " @@ skip promotions of %s for ==\n",
									pand2sym->name);
						}
						else
						{
							OPpromote(pfunc, pand2sym);
						}
						// if needing to multiply pand2 as an index to pand1, add the op
						// 
						if(pand2mul)
						{
							OPaddDexMul(pfunc, pand2mul);
						}
						// add original operand1 on the list
						//
						OPenlist(pfunc, pand1, "     --- DO enlistand ");
						
						// if either operand is unsigned, for them both
						// to be unsigned BEFORE they are promoted, so
						// the promotions happen on unsigned operands
						//
						if(! pand1sym->desc.isuns && pand2sym->desc.isuns)
						{
							Log(logDebug, 7, "  promoting operand1 to unsigned\n");
							pand1sym->desc.isuns = 1;
						}
						if(poper->op != opEQUAL)
						{
							// promote operand one to at least an int by default ("C")
							OPpromote(pfunc, pand1sym);
						
							// now check that operand1 is at least as big as operand 2
							OPcheckTypes(pfunc, pand1sym, pand2sym);
						}
						// and make sure its at least the same size as operand 1
						OPcheckTypes(pfunc, pand2sym, pand1sym);

						// if needing to multiply pand1 as an index to pand2, add the op
						// 
						if(pand1mul)
						{
							OPaddDexMul(pfunc, pand1mul);
						}
						if(snprintf(exp, sizeof(exp), "(%s%s%s)", pand1->psym->name,
									OPname(poper->op), pand2->psym->name) >= (sizeof(exp) - 1)
								)
						{
							Log(logError, 0, "Internal: Expression too complex\n");
							return 3;
						}
						// for index operations put the inferred deref operation
						// under the opand result. the INDEX should never
						// appear in function list.
						//
						if(pop->op == opINDEX)
						{
							// it there is an addrof underneath, skip the deref
							// 
							if(inop == opSDEREF || inop == opPDEREF)
							{
								Log(logDebug, 7, "Skip array auto-deref cause structref\n");
							}
							else if(inop == opINDEX && pand1->psym->desc.isdim > 1)
							{
								Log(logDebug, 7, "Skip array auto-deref cause indexing bigdim\n");
							}
							else if(
										pstack
									&&	*pstack
									&& (*pstack)->type == opERATOR
									&& (*pstack)->op == opADDROF
							)
							{
								Log(logDebug, 7, "Skip array auto-deref cause addrof\n");
								// & array[n] is the same as what this result is
								OPpop(pstack);
							}
							else
							{
								POPENTRY pm;

								// this dereferences the array as if it was
								// (*(a + 1)) but the other parens makes it
								// happen now, instead of as part of an exp.
								// since * has lower precedence than some ops
								//
								OPpushMarker(pstack);

								OPpushOper(pstack, opDEREF);
								OPeval(pfunc, pstack, opFORCE);

								// pull the marker
								//
								pm = pstack ? *pstack : NULL;
								if(pm && pm->next && pm->next->type == opMARKER)
								{
									pm = pm->next;
									(*pstack)->next = pm->next;
									OPdestroy(pm);
								}
								else
								{
									Log(logError, 0, "Bad index expression\n");
									return 1;
								}
							}
							// for index operations, the size of the array gets
							// divided by the dimension size, if at the last dim, the
							// type becomes a pointer
							//
							if(pand1->psym->desc.isdim > 0)
							{
								if(pand1->psym->desc.isdim == 1)
								{
									pand1sym->desc.isptr = pand1->psym->desc.isptr + 1;
								}
								else
								{
									unsigned long dim;

									if(pand1->psym->members)
										dim = strtoul(pand1->psym->members->name, NULL, 0);
									else
										dim = 1;
									if(dim == 0) dim = 1; // can't happen
									pand1sym->desc.bsize /= dim;
									pand1sym->members = pand1->psym->members->members;
								}
								pand1sym->desc.isdim = pand1->psym->desc.isdim - 1;
							}
						}
						else if(pop->op == opOFFSET)
						{
							// for offset operations, the result becomes a pointer to the 
							// type of the member, not the type of the struct
							//
							SYMsetType(pand1sym, pand2sym, &pand2sym->desc);
							pand1sym->desc.isptr++;
						}
						else
						{
							// restore array dimensions from ptr levels.  no binary
							// ops change level of ptr/dim, (deref and & are only ones)
							//
							if(pand1->psym->desc.isdim)
							{
								pand1sym->desc.isdim = pand1->psym->desc.isdim;
								pand1sym->desc.isptr = pand1->psym->desc.isptr;
							}
						}

						pand1 = OPpushOpandResult(pstack, pand1sym, exp);

						if(pand1 && pand1->psym)
						{
							// if both operands are constant, result is constant
							pand1->psym->desc.isconst =
								pand1sym->desc.isconst && pand2sym->desc.isconst;

							pres = OPcreate(opRESULT, opNONE, pand1->psym);
							OPenlist(pfunc, pres, "     --- DO enlistop ");
						}
						// now, if op was an index, but added a deref, have to 
					}
				}
				else
				{
					break; // predence on stack is >
				}
			}
			else /* a Unary operation  ------------------------------------- */
			{
				if(pop->type == opMARKER)
				{
					return 0; // too early to eval
				}
				Log(logDebug, 5, "   eval check unaryop %s (%d) against %s (%d)\n",
						OPname(pop->op), OPprecedence(pop->op), 
						OPname(inop), OPprecedence(inop));
					
				if(
						(OPprecedence(pop->op) > OPprecedence(inop))
					||	
						// for same precedence, do the op if associativity is left-to-right
						// otherwise stop and let the new right-to-left operation on the stack
						(
							(OPprecedence(pop->op) == OPprecedence(inop))
						&&	(OPassociativity(inop) == 0)
						)
				)
				{
					pand1 = OPpop(pstack);
					poper = OPpop(pstack);

					if(! pand1 || pand1->type != opERAND)
					{
						if(OPprecedence(inop) == 0) // end of statement type 
						{
							Log(logError, 0, "Missing operand for expression\n");
							rv = 1;
						}
						else
						{
							Log(logDebug, 5, "  eval incomplete expression\n");
						}
						break;
					}
					// optimizer - replace operation with
					// constants/literals with the actual result
					//
					if(
						EMIT_OPTIMIZE(1)
						&&	OPconstMathPossible(poper->op)
						&&	(! pand1->psym->desc.isreal)
						&&	(
								pand1->psym->desc.islit 
								/*	||  (pand1->psym->desc.isconst && pand1->psym->desc.isdef) */
							)
					)
					{
						// operand is a constant, yay, so do the math
						// (dothemath sets exp to result expression)
						//
						OPdotheMath(pand1->psym, NULL, poper, exp);
						Log(logDebug, 5, " OP math %s %s => %s\n",
								OPname(poper->op), pand1->psym->name, exp);
						// sym is already marked a lit, and the result is a lit
						pand1sym = SYMcreateCopy(pand1->psym);

						pand2 = OPpushOpandResult(pstack, pand1sym, exp);
					}
					else
					{
						pand1sym = SYMcreateCopy(pand1->psym);
						
						// a reference to an array is really a pointer, so convert it
						// for example: int* a[1][2] is made: int*** a, makes ptr math
						// easier, note that original type is maintained in enlistment
						//
						if(pand1sym->desc.isdim)
						{
							pand1sym->desc.isptr++;
							pand1sym->desc.isdim = 0;
						}
						
						// if this is an inc/dec operation, and the operand is a pointer type,
						// need to turn this into a binary operation to add the size of the
						// object to the ptr.  Note this code can be used to turn ALL inc/dec
						// int += 1 operations if there is no inc/dec asm operator.
						//
						if(OPisIncDec(poper->op))
						{
							int objsize;
							OPERATOR op = poper->op;
						
							objsize = OPcheckPointerUnaryMath(poper->op, pand1sym);
							if(objsize == 0) objsize = 1;

							if(objsize > 0)
							{								
								OPpushMarker(pstack);

								// turn the stack sequence ptr++ into the sequence
								// (ptr = ptr + 1) and re-evaluate to the marker
								//
								Log(logDebug, 5, " OP turn ptr ++/-- into +=/-=\n");
								
								if(op == opPREINC || op == opPOSTINC)
									poper->op = opADD;
								else
									poper->op = opMINUS;
							
								// push pand1 back on
								OPpushOpand(pstack, pand1->psym);
								if(pstack)
									(*pstack)->op = op;

								// push equals
								OPpushOper(pstack, opEQUAL);

								// again with the opand, but this time set the operator
								// to keepaddr, which saves off the address that's the
								// current intermediate result
								//
								OPpushOpand(pstack, pand1->psym);
								if(pstack)
									(*pstack)->op = op;

								// now the operation
								OPpush(pstack, poper);

								// now "1"
								OPpushOpand(pstack, g_one);

								// and eval to marker, there should be an opernd, then a marker
								// on the opstack, so pull off the marker. For preinc/dec, the
								// operand is to be used for the rest of the expression, for the
								// postinc/dec case, the operand is a "place holder" that gets
								// replaced by magic with the original value (see = case in 
								// function.c)
								//
								OPeval(pfunc, pstack, opFORCE);

								if(pstack && *pstack && (*pstack)->type == opERAND)
								{
									// extricate marker from below operand
									//
									if((*pstack)->next && (*pstack)->next->type == opMARKER)
									{
										POPENTRY ptop;

										ptop = OPpop(pstack);
										OPpop(pstack);
										OPpush(pstack, ptop);

										if(op == opPOSTINC || op == opPOSTDEC)
										{
											// instead of popping the result of this inc/dec
											// off the stack and leaving the original operand
											// on, just rename this operand to the original, 
											// which is what really happens by magic in
											// the = processing in function.c, but make sure
											// it is the "intermediate result" format, since
											// it will already be in a register, so no using
											// the "symbol" name for it as a shortcut and make
											// sure the type gets updated as well
											//
											if(pand1->psym->name[0] != '(')
											{
												char hackname[MAX_TOKEN + 32];

												snprintf(hackname, sizeof(hackname), "(%s)",
														pand1->psym->name);
												SYMrename(ptop->psym, hackname);
												SYMsetType(ptop->psym, pand1->psym->type, &pand1->psym->desc);
											}
											else
											{
												SYMrename(ptop->psym, pand1->psym->name);
											}
										}
									}
									else
									{
										Log(logError, 0, "Expression error\n");
									}
								}
								else
								{
									Log(logError, 0, "Expression error - no operand for inc/dec\n");
									break;
								}
								// continue with the original eval loop
								continue;
							}
						}

						// append the original opentries on the function code list
						// (poper might have been modified a bit)
						//
						OPenlist(pfunc, poper, "     --- DO enlistuop ");						
						OPenlist(pfunc, pand1, "     --- DO enlistand ");
						
						// default "C" promotions to int size
						OPpromote(pfunc, pand1sym);
						
						// prep the push back result string
						//
						if(snprintf(exp, sizeof(exp), "(%s:%s)", OPname(poper->op),
								pand1->psym->name) >= (sizeof(exp) - 1)
						)
						{
							Log(logError, 0, "Internal: Expression too complex\n");
							return 3;
						}
						// fixup result type after a dereference
						//
						if(pop->op == opDEREF)
						{
							// convert result type to actual result type

							if(pand1sym->desc.isptr)
							{
								// deref a pointer at level one gives base type
								// 
								pand1sym->desc.isptr--;
								Log(logDebug, 7, "    >>> pand %s deref to ptr*%d\n", 
											pand1sym->name, pand1sym->desc.isptr);
								if(! pand1sym->desc.isptr)
								{
									Log(logDebug, 7, "    >>> pand %s deref to base type %s \n", 
											pand1sym->name, pand1sym->type->name);
									memcpy(&pand1sym->desc, &pand1sym->type->desc, sizeof(pand1sym->desc));
								}
							}
							else
							{
								Log(logError, 0, "Attempt to dereference non-pointer type: %s\n",
										pand1sym->name);
							}
						}
						else if(pop->op == opADDROF)
						{
							// addrof makes the thing more of a pointer
							//
							pand1sym->desc.isptr++;
						}
						
						// cast just sets the type, regardless
						//
						if(pop->op == opCAST)
						{
							// morph type of operand result to type in operator
							//
							SYMsetType(pand1sym, pop->psym, &pop->psym->desc);
							pand1->psym->type = pop->psym;
						}
						else
						{
							// restore array dimensions from ptr levels now, taking any drop
							// in ptrlevel out of dimensions first, i.e. char* a[][], if deref
							// gives char* a[], not a[][]. and any increase in ptr level
							// into ptr, i.e. &char* a[][] gives char** a[][]
							//
							// isptr can only be off by one, so no range checks needed
							//
							if(pand1->psym->desc.isdim)
							{
								if(pand1sym->desc.isptr < (pand1->psym->desc.isptr + 1))
								{
									// dropped a ptr level, to drop a dim
									pand1sym->desc.isdim = pand1->psym->desc.isdim - 1;
									pand1sym->desc.isptr = pand1->psym->desc.isptr;
								}
								else if(pand1sym->desc.isptr > (pand1->psym->desc.isptr + 1))
								{
									// went up a level, bump ptr up
									pand1sym->desc.isdim = pand1->psym->desc.isdim;
									pand1sym->desc.isptr = pand1->psym->desc.isptr + 1;
								}
								else
								{
									// no change in ptr level
									pand1sym->desc.isdim = pand1->psym->desc.isdim;
									pand1sym->desc.isptr = pand1->psym->desc.isptr;
								}
							}
						}
						pand2 = OPpushOpandResult(pstack, pand1sym, exp);
					
						if(pand2 && pand2->psym)
						{
							pres = OPcreate(opRESULT, opNONE, pand2->psym);

							OPenlist(pfunc, pres, "     --- DO enlistop ");						
						}
					}
				}
				else
				{
					break; // prec on stack >
				}
			}
		}
		else if(pand1->type == opMARKER)
		{
			break;
		}
		else if(pand1->type == opARGUMENT)
		{
			break;
		}
		else if(pand1->type == opERATOR)
		{
			// its possible to have a comma and a force or statement
			// in the stuff we put on the stack for post inc/dec
			//
			if(pand1->op == opCOMMA)
			{
				poper = OPpop(pstack);
				if(pstack)
				{
					pand1 = OPpop(pstack);

					// still put the comma on the code list so the emitter can
					// pop the operand off too
					//
					OPenlist(pfunc, poper, "     --- DO enlistop ");
					
					if(pand1)
					{
						// discarded older operand
						OPdestroy(pand1);
					}
				}

			}
			break;
		}
	}
	while(pstack);
	
	if(inop == opSTATEMENT)
	{
		pand1 = OPcreate(opERATOR, inop, NULL);
		if(pand1)
		{
			OPenlist(pfunc, pand1, "     --- DO enlist st ");
		}
	}
	return rv;
}

//***********************************************************************
int OPclean(POPENTRY* pstack)
{
	POPENTRY pop;
	int		 errs = 0;
	
	Log(logDebug, 5, "  CLEAN\n");
	
	// there should be one operand (the result) on the stack
	//	
	pop = OPpop(pstack);

	if(pstack && *pstack)
	{
		// something left on stack after operand poped!
		//
		pop = *pstack;
		if(pop)
		{
			switch(pop->type)
			{
			case opERATOR:
				Log(logError, 0, "Missing Operand for %s\n", OPname(pop->op));
				errs++;
				break;
				
			case opERAND:
				if(! pop->psym->desc.iscopy)
				{
					// TODO - figure how to avoid this with "a++"
					//Log(logWarning, 0, "Useless Expression %s\n", pop->psym->name);
				}
				break;
				
			case opMARKER:
				Log(logError, 0, "Missing right parenthesis\n");
				errs++;
				break;
			}
		}
		while(*pstack)
		{
			OPdestroy(OPpop(pstack));
		}
	}
	return errs;
}

//***********************************************************************
int OPconstMathPossible(OPERATOR op)
{
	switch(op)
	{
	case opFORCE:
	case opSTATEMENT:
	case opRETURN:
	case opCOMMA:
	case opEQUAL:
	case opTERNARY:
	case opPREINC:
	case opPREDEC:
	case opPOSTINC:
	case opPOSTDEC:
	case opCAST:
	case opDEREF:
	case opADDROF:
	case opSIZEOF:
	case opCALL:
	case opINDEX:
	case opOFFSET:
	case opSDEREF:
	case opPDEREF:
	case opNONE:
		return 0;
	}
	return 1;
}

//***********************************************************************
int OPdotheMath(PSYM pa, PSYM pb, POPENTRY po, char* result)
{
	OPERATOR op;
	unsigned long  ula, ulb;
	signed   long  ila, ilb;
	
	op = po->op;

	if(! OPconstMathPossible(op))
		return 1;

	// get to the bottom of enum lits
	while(pa && pa->init)
		pa = pa->init;
	while(pb && pb->init)
		pb = pb->init;

	if((pa && pa->desc.isuns) || (pb && pb->desc.isuns))
	{
		ula = strtoul(pa->name, NULL, 0);
		if(pb) ulb = strtoul(pb->name, NULL, 0);
		else ulb = 1;
	
		switch(op)
		{
		case opTEST:
			sprintf(result, "%u", ula != 0);	break;
		case opBOOLOR:
			sprintf(result, "%u", ula || ulb);	break;
		case opBOOLAND:
			sprintf(result, "%u", ula && ulb);	break;
		case opBITOR:
			sprintf(result, "%u", ula | ulb);	break;
		case opBITXOR:
			sprintf(result, "%u", ula ^ ulb);	break;
		case opBITAND:
			sprintf(result, "%u", ula & ulb);	break;
		case opBOOLEQ:
			sprintf(result, "%u", ula == ulb);	break;
		case opBOOLNEQ:
			sprintf(result, "%u", ula | ulb);	break;
		case opBOOLLT:
			sprintf(result, "%u", ula < ulb);	break;
		case opBOOLGT:
			sprintf(result, "%u", ula > ulb);	break;
		case opBOOLLTEQ:
			sprintf(result, "%u", ula <= ulb);	break;
		case opBOOLGTEQ:
			sprintf(result, "%u", ula >= ulb);	break;
		case opSHIFTL:
			sprintf(result, "%u", ula << ulb);	break;
		case opSHIFTR:
			sprintf(result, "%u", ula >= ulb);	break;
		case opADD:
			sprintf(result, "%u", ula + ulb);	break;
		case opMINUS:
			sprintf(result, "%u", ula - ulb);	break;
		case opMUL:
			sprintf(result, "%u", ula * ulb);	break;
		case opDIV:
			if(! ulb)
			{
				Log(logError, 0, "Divide by zero in constant expression\n");
				ulb = 1;
			}
			sprintf(result, "%u", ula / ulb);	break;
		case opMOD:
			if(! ula)
			{
				Log(logError, 0, "Mod zero in constant expression\n");
				ula = 1;
			}
			sprintf(result, "%u", ula % ulb);	break;
		case opBOOLNOT:
			sprintf(result, "%u", ula ? 0 : 1);	break;
		case opBITINVERT:
			sprintf(result, "%u", ~ ula);		break;
		case opNEGATE:
			sprintf(result, "%u", - (long)ula);	break;
		case opPROMOTECHAR2INT:
		case opPROMOTEINT2LONG:
		case opPROMOTELONG2LONGLONG:
		case opPROMOTEUCHAR2UINT:
		case opPROMOTEUINT2ULONG:
		case opPROMOTEULONG2ULONGLONG:
		case opPROMOTE2UNSIGNED:
			sprintf(result, "%u", ula);			break;
		case opPROMOTEFLT2DBL:
			sprintf(result, "%u", ula);			break;
		}
	}
	else
	{
		ila = strtol(pa->name, NULL, 0);
		if(pb) ilb = strtol(pb->name, NULL, 0);
		else ilb = 1;

		switch(op)
		{
		case opTEST:
			sprintf(result, "%d", ila != 0);	break;
		case opBOOLOR:
			sprintf(result, "%d", ila || ilb);	break;
		case opBOOLAND:
			sprintf(result, "%d", ila && ilb);	break;
		case opBITOR:
			sprintf(result, "%d", ila | ilb);	break;
		case opBITXOR:
			sprintf(result, "%d", ila ^ ilb);	break;
		case opBITAND:
			sprintf(result, "%d", ila & ilb);	break;
		case opBOOLEQ:
			sprintf(result, "%d", ila == ilb);	break;
		case opBOOLNEQ:
			sprintf(result, "%d", ila | ilb);	break;
		case opBOOLLT:
			sprintf(result, "%d", ila < ilb);	break;
		case opBOOLGT:
			sprintf(result, "%d", ila > ilb);	break;
		case opBOOLLTEQ:
			sprintf(result, "%d", ila <= ilb);	break;
		case opBOOLGTEQ:
			sprintf(result, "%d", ila >= ilb);	break;
		case opSHIFTL:
			sprintf(result, "%d", ila << ilb);	break;
		case opSHIFTR:
			sprintf(result, "%d", ila >= ilb);	break;
		case opADD:
			sprintf(result, "%d", ila + ilb);	break;
		case opMINUS:
			sprintf(result, "%d", ila - ilb);	break;
		case opMUL:
			sprintf(result, "%d", ila * ilb);	break;
		case opDIV:
			if(! ilb)
			{
				Log(logError, 0, "Divide by zero in constant expression\n");
				ilb = 1;
			}
			sprintf(result, "%d", ila / ilb);	break;
		case opMOD:
			if(! ila)
			{
				Log(logError, 0, "Mod zero in constant expression\n");
				ila = 1;
			}
			sprintf(result, "%d", ila % ilb);	break;
		case opBOOLNOT:
			sprintf(result, "%d", ila ? 0 : 1);	break;
		case opBITINVERT:
			sprintf(result, "%d", ~ ila);		break;
		case opNEGATE:
			sprintf(result, "%d", - ila);		break;
		case opPROMOTECHAR2INT:
		case opPROMOTESHORT2INT:
		case opPROMOTEINT2LONG:
		case opPROMOTELONG2LONGLONG:
		case opPROMOTEFLT2DBL:
			sprintf(result, "%d", ila);			break;
		case opPROMOTEUCHAR2UINT:
		case opPROMOTEUSHORT2UINT:
		case opPROMOTEUINT2ULONG:
		case opPROMOTEULONG2ULONGLONG:
		case opPROMOTE2UNSIGNED:
			sprintf(result, "%u", (unsigned)ila);	break;
		}
	}
	return 0;
}

//***********************************************************************
int OPisPrefixUnary(OPERATOR op)
{
	// unarys that can come before an operand
 	//
	switch(op)
	{
	//case opCOMMA:
	case opTERNARY:
	case opRETURN:
	case opPREINC:
	case opPREDEC:
	case opDEREF:
	case opADDROF:
	case opSIZEOF:
	case opNEGATE:
	case opBOOLNOT:
	case opBITINVERT:
		return 1;
	default:
		return 0;
	}
}

//***********************************************************************
int OPisUnary(OPERATOR op)
{
	switch(op)
	{
	case opCALL:
	//case opCOMMA:
	case opTERNARY:
	case opBOOLNOT:
	case opBITINVERT:
	case opPREINC:
	case opPREDEC:
	case opPOSTINC:
	case opPOSTDEC:
	case opNEGATE:
	case opCAST:
	case opDEREF:
	case opADDROF:
	case opSIZEOF:
	
	case opRETURN:
		
	case opPROMOTE2UNSIGNED:
	case opPROMOTECHAR2INT:
	case opPROMOTESHORT2INT:
	case opPROMOTEINT2LONG:
	case opPROMOTELONG2LONGLONG:
	case opPROMOTEUCHAR2UINT:
	case opPROMOTEUSHORT2UINT:
	case opPROMOTEUINT2ULONG:
	case opPROMOTEULONG2ULONGLONG:
	case opPROMOTEFLT2DBL:
	case opTEST:

	case opNONE:
		return 1;
		
	default:
		return 0;
	}
}

//***********************************************************************
int OPisCommutative(OPERATOR op)
{
	switch(op)
	{
	case opFORCE:
	case opSTATEMENT:
	case opRETURN:
	case opTEST:
	case opCOMMA:
	case opEQUAL:
	case opTERNARY:
		return 0;
	case opBOOLOR:
	case opBOOLAND:
	case opBITOR:
	case opBITXOR:
	case opBITAND:
	case opBOOLEQ:
	case opBOOLNEQ:
		return 1;
	case opBOOLLT:
	case opBOOLGT:
	case opBOOLLTEQ:
	case opBOOLGTEQ:
		return 0;
	case opSHIFTL:
	case opSHIFTR:
		return 0;
	case opADD:
		return 1;
	case opMINUS:
		return 0;
	case opMUL:
		return 1;
	case opDIV:
	case opMOD:
		return 0;
	case opBOOLNOT:
	case opBITINVERT:
	case opPREINC:
	case opPREDEC:
	case opPOSTINC:
	case opPOSTDEC:
	case opNEGATE:
	case opCAST:
	case opDEREF:
	case opADDROF:
	case opSIZEOF:
		return 0;
	case opCALL:
	case opINDEX:
	case opOFFSET:
	case opSDEREF:
	case opPDEREF:
		return 0;
	case opPROMOTE2UNSIGNED:
	case opPROMOTECHAR2INT:
	case opPROMOTESHORT2INT:
	case opPROMOTEINT2LONG:
	case opPROMOTELONG2LONGLONG:
	case opPROMOTEUCHAR2UINT:
	case opPROMOTEUSHORT2UINT:
	case opPROMOTEUINT2ULONG:
	case opPROMOTEULONG2ULONGLONG:
	case opPROMOTEFLT2DBL:
		return 0;
	case opNONE:
		return 0;
	}
	return 0;
}

//***********************************************************************
int OPprecedence(OPERATOR op)
{
	switch(op)
	{
	case opFORCE:
	case opSTATEMENT:
	case opRETURN:
		return 0;
	case opTEST:
		return 1;
	case opCOMMA:
		return 2;
	case opEQUAL:
		return 3;
	case opTERNARY:
		return 4;
	case opBOOLOR:
		return 5;
	case opBOOLAND:
		return 6;
	case opBITOR:
		return 7;
	case opBITXOR:
		return 8;
	case opBITAND:
		return 9;
	case opBOOLEQ:
	case opBOOLNEQ:
		return 10;
	case opBOOLLT:
	case opBOOLGT:
	case opBOOLLTEQ:
	case opBOOLGTEQ:
		return 11;
	case opSHIFTL:
	case opSHIFTR:
		return 12;
	case opADD:
	case opMINUS:
		return 13;
	case opMUL:
	case opDIV:
	case opMOD:
		return 14;
	case opBOOLNOT:
	case opBITINVERT:
	case opPREINC:
	case opPREDEC:
	case opNEGATE:
	case opCAST:
	case opDEREF:
	case opADDROF:
	case opSIZEOF:
		return 15;
	case opCALL:
	case opOFFSET:
	case opSDEREF:
	case opPDEREF:
	case opINDEX:
	case opPOSTINC:
	case opPOSTDEC:
		return 16;
	case opPROMOTE2UNSIGNED:
	case opPROMOTECHAR2INT:
	case opPROMOTESHORT2INT:
	case opPROMOTEINT2LONG:
	case opPROMOTELONG2LONGLONG:
	case opPROMOTEUCHAR2UINT:
	case opPROMOTEUSHORT2UINT:
	case opPROMOTEUINT2ULONG:
	case opPROMOTEULONG2ULONGLONG:
	case opPROMOTEFLT2DBL:
		return 18;
	case opNONE:
		return 19;
	}
	return 200;
}

//***********************************************************************
int OPassociativity(OPERATOR op)
{
	switch(op)
	{
	case opFORCE:
	case opSTATEMENT:
	case opRETURN:
		return 0;
	case opTEST:
		return 0;
	case opCOMMA:
		return 0;
	case opEQUAL:
	case opTERNARY:
		return 1;
	case opBOOLOR:
	case opBOOLAND:
	case opBITOR:
	case opBITXOR:
	case opBITAND:
	case opBOOLEQ:
	case opBOOLNEQ:
	case opBOOLLT:
	case opBOOLGT:
	case opBOOLLTEQ:
	case opBOOLGTEQ:
	case opSHIFTL:
	case opSHIFTR:
	case opADD:
	case opMINUS:
	case opMUL:
	case opDIV:
	case opMOD:
		return 0;
	case opBOOLNOT:
	case opBITINVERT:
	case opPREINC:
	case opPREDEC:
	case opNEGATE:
	case opCAST:
	case opDEREF:
	case opADDROF:
	case opSIZEOF:
		return 1;
	case opCALL:
		return 0;
	case opOFFSET:
		return 0;
	case opSDEREF:
	case opPDEREF:
	case opINDEX:
		return 0;
	case opPOSTINC:
	case opPOSTDEC:
		return 0;
	case opPROMOTE2UNSIGNED:
	case opPROMOTECHAR2INT:
	case opPROMOTESHORT2INT:
	case opPROMOTEINT2LONG:
	case opPROMOTELONG2LONGLONG:
	case opPROMOTEUCHAR2UINT:
	case opPROMOTEUSHORT2UINT:
	case opPROMOTEUINT2ULONG:
	case opPROMOTEULONG2ULONGLONG:
	case opPROMOTEFLT2DBL:
		return 0;
	case opNONE:
		return 0;
	}
	return 0;
}

//***********************************************************************
OPERATOR OPencode(char op)
{
	switch(op)
	{
	case ';':	return opSTATEMENT;
	case ')':
	case 'r':	return opRETURN;
	case ',':	return opCOMMA;
	case '=':	return opEQUAL;
	case '?':	return opTERNARY;
	case 'O':	return opBOOLOR;
	case 'A':	return opBOOLAND;
	case '|':	return opBITOR;
	case '^':	return opBITXOR;
	case '&':	return opBITAND;
	case 'Q':	return opBOOLEQ;
	case 'N':	return opBOOLNEQ;
	case '<':	return opBOOLLT;
	case '>':	return opBOOLGT;
	case 't':	return opBOOLLTEQ;
	case 'g':	return opBOOLGTEQ;
	case 'L':	return opSHIFTL;
	case 'R':	return opSHIFTR;
	case '+':	return opADD;
	case '-':	return opMINUS;
	case '*':	return opMUL;
	case '/':	return opDIV;
	case '%':	return opMOD;
	case '!':	return opBOOLNOT;
	case '~':	return opBITINVERT;
	case 'i':	return opPREINC;
	case 'd':	return opPREDEC;
	case 'I':	return opPOSTINC;
	case 'D':	return opPOSTDEC;
	case 'n':	return opNEGATE;
	case 'y':	return opCAST;
	case '$':	return opDEREF;
	case '@':	return opADDROF;
	case 's':
	case 'z':	return opSIZEOF;
	case '(':	return opCALL;
	case ']':	return opINDEX;
	case 'o':	return opOFFSET;
	case '.':	return opSDEREF;
	case ':':	return opPDEREF;
	case '0':	return opPROMOTE2UNSIGNED;
	case '1':	return opPROMOTECHAR2INT;
	case '3':	return opPROMOTESHORT2INT;
	case '5':	return opPROMOTEINT2LONG;
	case '7':	return opPROMOTELONG2LONGLONG;
	case '2':	return opPROMOTEUCHAR2UINT;
	case '4':	return opPROMOTEUSHORT2UINT;
	case '6':	return opPROMOTEUINT2ULONG;
	case '8':	return opPROMOTEULONG2ULONGLONG;
	case '9':	return opPROMOTEFLT2DBL;
	case 'V':	return opTEST;
	case '_':
	default:	return opNONE;
	}
}

//***********************************************************************
const char* OPname(OPERATOR op)
{
	switch(op)
	{
	case opFORCE:	return "force";
	case opSTATEMENT:return "statement";
	case opRETURN:	return "return";
	case opCOMMA:	return ",";	
	case opEQUAL:	return "=";
	case opTERNARY:	return "?";
	case opBOOLOR:	return "||";
	case opBOOLAND:	return "&&";
	case opBITOR:	return "|";
	case opBITXOR:	return "^";
	case opBITAND:	return "&";
	case opBOOLEQ:	return "==";
	case opBOOLNEQ:	return "!=";
	case opBOOLLT:	return "<";
	case opBOOLGT:	return ">";
	case opBOOLLTEQ:return "<=";
	case opBOOLGTEQ:return ">=";
	case opSHIFTL:	return "<<";
	case opSHIFTR:	return ">>";
	case opADD:		return "+";
	case opMINUS:	return "-";
	case opMUL:		return "*";
	case opDIV:		return "/";
	case opMOD:		return "%";
	case opBOOLNOT:	return "!";
	case opBITINVERT:	return "~";
	case opPREINC:	return "++";
	case opPREDEC:	return "--";
	case opPOSTINC:	return "++";
	case opPOSTDEC:	return "--";
	case opNEGATE:	return "-";
	case opCAST:	return "(cast)";
	case opDEREF:	return "*";
	case opADDROF:	return "addr&";
	case opSIZEOF:	return "sizeof";
	case opCALL:	return "call";
	case opINDEX:	return "[+]";
	case opOFFSET:	return "+o+";
	case opSDEREF:	return ".";
	case opPDEREF:	return "->";
	case opPROMOTE2UNSIGNED:		return "to unsigned";
	case opPROMOTECHAR2INT:			return "char to int";
	case opPROMOTESHORT2INT:		return "short to int";
	case opPROMOTEINT2LONG:			return "int to long";
	case opPROMOTELONG2LONGLONG:	return "long to long long";
	case opPROMOTEUCHAR2UINT:		return "uchar to uint";
	case opPROMOTEUSHORT2UINT:		return "ushort to uint";
	case opPROMOTEUINT2ULONG:		return "uint to ulong";
	case opPROMOTEULONG2ULONGLONG:	return "ulong to ulonglong";
	case opPROMOTEFLT2DBL:			return "flt to dbl";
	case opTEST:	return "TST";
	case opNONE:	return "<none>";
	}
	return "badbad";
}


