#include "bccx.h"

// the current parse context
//
PCCTX g_pctx = NULL;

PSPREC		g_spstack	= NULL;

static PSYM	g_atomicTypes[8];
static int  g_numAtomicTypes;

PSYM g_zero = NULL;
PSYM g_one = NULL;

//***********************************************************************
int Token(FILE* in, PCCTX px)
{
	do
	{
		if(px->retok)
		{
			px->retok = 0;
			
			// reuse the last token
			//
			return 0;
		}
		// so get a token
		px->errs = TokenGet(in, &px->token, px->cppok);
		if(px->errs)
		{
			return px->errs;
		}
		if(px->token.text[0] != '\0')
		{
			// see if its a key-word
			if(px->token.type == kwPlain)
			{
				px->token.type = TokenCheckBuiltin(&px->token, px->cppok);
			}
		}
	}
	while(! ERRMAX && ! px->token.eof && ! px->token.text[0]);
	
	Log(logDebug, 8, "Tok %2d =%s=\n", px->level, px->token.text);	
	
	// convert builtin types to plain, since they are in the symbol tab
	//
	if(px->token.type == kwBuiltinType)
		px->token.type = kwPlain;

	return px->errs;
}

//***********************************************************************
int ParseEOS(FILE* in, PCCTX px, int rc)
{
	int rv;
	
	do
	{
		rv = Token(in, px);
		if(rv) return rv;
	}
	while(
				! px->token.eof
			&&	(
					(px->token.type != kwStatement)
				||	(px->token.type != kwLparen && px->pargrec)
				)
	);
	
	OPclean(&px->pstack);

	return rc;
}

//***********************************************************************
char* AggregateName(int type)
{
	switch(type)
	{
	case AGGRT_STRUCT:	return "struct";
	case AGGRT_UNION:	return "union";
	case AGGRT_CLASS:	return "class";
	default:			return "not an aggrt";
	}
}

//***********************************************************************
char* NewStructName(int aggrtype)
{
	static int nstructs = 0;
	static char nname[128 + MAX_TOKEN];
	
	nstructs++;
	snprintf(nname, sizeof(nname), "%s %sns_%d", AggregateName(aggrtype),
			CPU_LABEL_PREP, nstructs);
	return nname;
}

//***********************************************************************
char* StructType(char* typestr, int aggrtype)
{
	static char nname[128 + MAX_TOKEN];
	
	snprintf(nname, sizeof(nname), "%s %s", AggregateName(aggrtype), typestr);
	return nname;
}

//***********************************************************************
char* NewVarLabel(char* base)
{
	static int nlabs = 0;
	static char nname[32 + MAX_TOKEN];
	
	nlabs++;
	snprintf(nname, sizeof(nname), "%s%s_%d", CPU_LABEL_PREP, base ? base : "L", nlabs);
	return nname;
}

//***********************************************************************
PSYM AddVariable(PCCTX px, char* name)
{
	px->psym = SYMcreate(name);
	if(px->psym)
	{
		// set type to current symbol type ptype folding
		// in any modifiers accumulated in desc
		//
		if(px->level > 0)
		{
			// set auto if not static or extern in local scopes
			//
			if(! px->desc.istatic && ! px->desc.isext)
				px->desc.isauto = 1;
		}
		SYMsetType(px->psym, px->ptype, &px->desc);
		
		if(px->typing && ! px->enumerating)
		{
			// doing a typedef, so this "variable" is really
			// a new type, so add it to the type table
			//
			SYMTABaddSym(px->psym, px->psymtab->types);
		}
		else
		{
			// add symbol to table of variables in this scope
			// or as member if declaring a function
			//
			if(px->funcing && px->pfunc)
			{
				SYMaddMember(px->pfunc, px->psym);
			}
			else if(px->aggrting)
			{
				if(px->pclass->prev->psym)
				{
					SYMaddMember(px->pclass->prev->psym, px->psym);
				}
				else
				{
					Log(logError, 0, "Internal: no class for member %s\n", name);
				}
			}
			else
			{
				if(px->psym->desc.isext)
				{
					// add extern symbols to global table
					SYMTABaddSym(px->psym, px->psymlist->symbols);
				}
				else
				{
					if(px->enumerating)
					{
						px->psym->desc.isconst = 1;
						px->psym->desc.islit   = 1;
						px->psym->desc.istatic = 1;
					}

					// regular vars go on the local table
					SYMTABaddSym(px->psym, px->psymtab->symbols);

					if(px->enumerating && px->ptype)
					{
						// its part of an enumeration initializer so add a
						// copy of it as member of the enum type as well
						px->psym = SYMcreateCopy(px->psym);
						if(px->psym)
						{
							px->psym->desc.isenum  = 0;
							px->psym->type = px->ptype->type;
							SYMaddMember(px->ptype, px->psym);
						}
					}
				}
			}
		}
	}
	else
	{
		Log(logError, 0, "Internal: Symtab memory\n");
	}
	return px->psym;
}

//***********************************************************************
PSYM AddLiteral(PCCTX px, char* name)
{
	SYMTYPE desc;

	px->psym = SYMcreate(name);
	
	memset(&desc, 0, sizeof(desc));

	if(px->psym)
	{
		int lz     = 8;
		int islong = 0;
		int isuns  = 0;
		int isreal = 0;
		int i;
		unsigned long av;
		double        rv;
		char* ts;
		
		if(name[0] == '\"')
		{
			PSYM psym;
			
			// create the global storage block for data
			px->ptype = SYMTABfindType("char", px->psymtab);
			desc.bsize = (strlen(name) - 2) * CPU_CHAR_SIZE;
			desc.isdim   = 1;
			desc.islit   = 1;
			desc.istatic = 1;
			desc.isconst = 1;			
			SYMsetType(px->psym, px->ptype, &desc);

			// create a symbol and point to that symbol as the initializer
			//
			psym = SYMcreate(NewVarLabel("S"));
			psym->init = px->psym;
			SYMsetType(psym, px->ptype, &desc);
			px->psym = psym;
			
			// even if sym is a ptr, initializer isnt;
			psym->desc.isptr = 0;
			
			// even if sym is auto, the initializer is global
			psym->desc.isauto = 0;
		}
		else
		{
			desc.islit   = 1;
			desc.istatic = 1;
			desc.isconst = 1;
			
			if(! strchr(name, '.'))
			{
				if(name[0] == '\'')
				{
					char valname[32];
					
					ts = NULL;
					switch(name[1])
					{
					case '\\':
						switch(name[2])
						{
						case '0':
							for(i = 2, av = 0; name[i]; i++)
							{
								if(name[i] ==  '\'')
									break;
								if(name[i] < '0' || name[i] > '7')
								{
									Log(logError, 0, "Numeric literal syntax near %s\n", name);
									return NULL;
								}
								av *= 8;
								av += name[i] - '0';
							}
							break;
						case 'a':	av = 7;  break;
						case 'b':	av = 8;  break;
						case 'e':	av = 27; break;
						case 'f':	av = 12; break;
						case 'n':	av = 10; break;
						case 'r':	av = 13; break;
						case 't':	av = 9;  break;
						case 'v':	av = 11; break;
						case '\'':	av = 39; break;
						case '\"':	av = 34; break;
						case '\\':	av = 92; break;
						default:
							Log(logError, 0, "Unrecognized escape sequence %s\n", name);
							return NULL;
						}
						break;
					default:
						av = (unsigned long)name[1];
						break;
					}
					snprintf(valname, sizeof(valname), "%u", av);
					SYMrename(px->psym, valname);
				}
				else
				{
					av = strtoul(name, &ts, 0);
				}				
				// find byte size of constant
				//
				for(lz = 8, av >>= 8; av; av >>= 8)
				{
					lz+= 8;
				}
				desc.isuns   = isuns;
				desc.islong  = islong;
				
				if(ts)
				{
					if(strchr(ts, 'U'))
						isuns = 1;
					if(strstr(ts, "LL"))
						islong = 2;
					else if(strchr(ts, 'L'))
						islong = 1;
				}
				// set type to smallest that will fit the literal
				//
 				// (if you want... they end up getting promoted
 				// to int in the code, so they might as well be
 				// int in memory so each time they are used saves a promotion)
				//
#ifdef USE_SMALLEST_LIT_STORE
				if(lz <= CPU_CHAR_SIZE)
				{
					px->ptype = SYMTABfindType("char", px->psymtab);
				}
				else if(lz <= CPU_SHORT_SIZE) 
				{
					px->ptype = SYMTABfindType("short", px->psymtab);
				}
				else
#endif
				if(lz <= CPU_INT_SIZE && ! islong)
				{
					px->ptype = SYMTABfindType("int", px->psymtab);
				}
				else
				{
					px->ptype = SYMTABfindType("long", px->psymtab);
				}
				desc.bsize = px->ptype->desc.bsize;
				SYMsetType(px->psym, px->ptype, &desc);
		
				// never store a lit as an enum type, its just a lit
				px->psym->desc.isenum = 0;

				// warn about number bits exceeding type bits
				//
				if(px->ptype && px->ptype->desc.bsize < (unsigned int)lz)
				{
					Log(logWarning, 0, "Size of constant %s exceeds bit size of type %s\n", 
							name, px->ptype->name);
				}
			}
			else
			{
#ifdef CPU_DOUBLE_SIZE
				rv = strtod(name, NULL);
				lz = CPU_DOUBLE_SIZE;
				desc.isreal = 1;
				desc.bsize = px->ptype->desc.bsize;
				SYMsetType(px->psym, px->ptype, &desc);
#else
				Log(logError, 0, "target CPU does not support floating point\n");
				return NULL;
#endif
			}
		}
		// note I add this to the base (global) table, not the 
		// current (scope) table, since its just a number, also note
		// I actually use the return from addsym, which accounts for
		// duplicate additions returning the older symbol
		//
		px->psym = SYMTABaddSym(px->psym, px->psymlist->symbols);
	}
	else
	{
		Log(logError, 0, "Internal: Symtab memory\n");
	}
	return px->psym;
}

int ParseOperator(FILE* in, PCCTX px);
int ParseVarDecl(FILE* in, PCCTX px);

#define TYPECHECKSET(v, nv)		\
	if(v) Log(logError, 0, "Syntax: near %s\n", px->token.text);	\
	else  v = nv;
	
//***********************************************************************
int EmbellishType(PCCTX px)
{
	switch(px->token.text[0])
	{
	case 'a':
		if(px->desc.istatic || px->desc.isext || px->desc.islit)
			Log(logError, 0, "Syntax: near %s\n", px->token.text);
		TYPECHECKSET(px->desc.isauto, 1)
		break;
	case 'c':
		if(px->token.text[1] == 'o')
		{
			TYPECHECKSET(px->desc.isconst, 1)
		}
		else if(px->token.text[1] == 'l' /* && c++ */)
		{
			TYPECHECKSET(px->desc.isaggrt, AGGRT_CLASS)
		}
		break;
	case 'e':
		if(px->token.text[1] == 'x')
		{
			if(px->desc.istatic || px->desc.isauto)
				Log(logError, 0, "Syntax: near %s\n", px->token.text);
			TYPECHECKSET(px->desc.isext, 1)
		}
		else
		{
			TYPECHECKSET(px->desc.isenum, 1)
		}
		break;
	case 'l':
		if(px->desc.islong > 1)
			TYPECHECKSET(px->desc.islong, 2)
		else
			px->desc.islong++;
		break;
	case 'r':
		TYPECHECKSET(px->desc.isreg, 1)
		break;
	case 's':
		if(px->token.text[2] == 'a')
		{
			if(px->desc.isext || px->desc.isauto)
				Log(logError, 0, "Syntax: near %s\n", px->token.text);
			TYPECHECKSET(px->desc.istatic, 1)
		}
		else if(px->token.text[2] == 'r')
		{
			TYPECHECKSET(px->desc.isaggrt, AGGRT_STRUCT)
		}
		break;
	case 'u':
		if(px->token.text[2] == 's')
		{
			TYPECHECKSET(px->desc.isuns, 1)
		}
		else if(px->token.text[2] == 'i')
		{
			TYPECHECKSET(px->desc.isaggrt, AGGRT_UNION)
		}
		break;
	case 'v':
		if(px->token.text[2] == 'i')
		{
			TYPECHECKSET(px->desc.isvoid, 1)
		}
		else
		{
			TYPECHECKSET(px->desc.isvol, 1)
		}
		break;
	default:
		// cant happen
		Log(logError, 0, "Internal: bad type mod %s\n", px->token.text);
		return 1;
	}
	return 0;
}

//***********************************************************************
int ParseMember(FILE* in, PCCTX px, OPERATOR op)
{
	POPENTRY pop;
	PSYM     pmemb;
	PSYM	 ptype;

	pop = px->pstack;

	if(! pop || ! pop->next)
	{
		Log(logError, 0, "No struct/class for operator %s\n", OPname(op));
		return 1;
	}
	pop = pop->next;

	// make sure that its a struct to reference
	//
	if(pop->type != opERAND || ! pop->psym)
	{
		Log(logError, 0, "No struct/union/class for operator %s\n", OPname(op));
		return 1;
	}
	if(! pop->psym->desc.isaggrt)
	{
		Log(logError, 0, "%s is not a struct/union/class for %s\n",
				pop->psym->name, OPname(op));
		return 1;
	}
	// go to the end of the type chain to get member list
	//
	for(ptype = pop->psym->type; ptype && ptype->type;)
		ptype = ptype->type;

	// make sure the struct is defined
	//
	if(! ptype || ! ptype->members)
	{
		Log(logError, 0, "Attempt to reference incomplete type %s\n", 
				pop->psym->type->name);
		return 1;
	}
	// ok, so we got a struct/class, parse a token and make sure its a member
	//
	px->errs += Token(in, px);
	if(px->errs)
		return px->errs;
	
	if(px->token.type != kwPlain)
	{
		Log(logError, 0, "%s is not the proper type to be a member of struct/class\n",
				px->token.text);
		return 1;
	}
	// look at member list
	//
	for(pmemb = ptype->members; pmemb; pmemb = pmemb->members)
	{
		if(! strcmp(pmemb->name, px->token.text))
		{
			break;
		}
	}
	if(! pmemb)
	{
		Log(logError, 0, "%s is not a member of struct/class %s\n",
				px->token.text, pop->psym->type->name);
		return 1;
	}
	// wow, after all those chances to fail, the programmer got it right!
	//
	return OPpushOpand(&px->pstack, pmemb) == NULL;
}

int ParseBoolean(FILE* in, PCCTX px, OPERATOR op);

//***********************************************************************
int ParseOperand(FILE* in, PCCTX px)
{
	OPERATOR op;
	
	do
	{
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;

		Log(logDebug, 8, "ParseOperand tok=%s\n", px->token.text);
		
		switch(px->token.type)
		{
		case kwPlain:
			
			// this is a symbolic ref in an expression, just let 
			// expression parser handle it
			//
			// 1) look up the symbol in all local/global scopes
			px->psym = SYMTABfindVar(px->token.text, px->psymtab);			

			// 2) if not in scope, look at function arguments of func
			if(! px->psym && px->pcurfunc)
			{
				px->psym = SYMfindMember(px->token.text, px->pcurfunc->pfuncsym);
			}
			// 3) if still not found, see if there is a type being build
			//    and for certain type mods (unsigned, static, volatile, long)
			//    assume this is a var decl of base type int
			//
			if(! px->psym)
			{
				// if token is a type, this is a var decl or a cast
				//
				px->ptype = SYMTABfindType(px->token.text, px->psymtab);
				if(px->ptype)
				{
					// if there is no marker on the opstack, assume vardecl
					// else assume a cast operation
					//
					if(! px->pstack || px->pstack->type != opMARKER)
					{
						// no '(' on stack, its a var decl
						ParseVarDecl(in, px);
					}
					else
					{
						// ( on stack, its a cast, so keep desc and ptype and
						// keep on getting operands in loop
						//
						break;
					}
				}
				else if(
						px->desc.islong > 0
					||	px->desc.isuns
					||	px->desc.istatic
					||	px->desc.isvol
				)
				{
					// let long/unsigned/static var mean long int var
					px->ptype = SYMTABfindType("int", px->psymtab);
					px->retok = 1;
					ParseVarDecl(in, px);
				}
				else
				{
					char varname[MAX_TOKEN];
					
					strcpy(varname, px->token.text);
					
					// 4) if not a var decl, assume this is a function call and
					//    probe for a left paren and if found, do the function
					//    else whine about undefined var
					//
					px->errs = Token(in, px);
					if(px->errs)
						return px->errs;
					// in any event, this token gets reused
					px->retok = 1;
					if(px->token.type != kwLparen)
					{
						// its a var but not thar
						Log(logError, 0, "Undefined var \"%s\" used in expression\n", varname);
						ParseEOS(in, px, 0);
					}
					else
					{
						// its a function call, make a symbol of function type
						// (and extern returning int) and push that as the operand
						//
						DescInit(&px->desc);
						px->desc.isfunc = 1;
						px->desc.isext  = 1;
						px->ptype = SYMTABfindType("int", px->psymtab);
						px->pfunc = AddVariable(px, varname);
						DescInit(&px->desc);
						px->ptype = NULL;
						px->psym  = NULL;
						
						if(! OPpushOpand(&px->pstack, px->pfunc))
							px->errs++;
						return ParseOperator(in, px);
					}
				}
			}
			else
			{
				if(! OPpushOpand(&px->pstack, px->psym))
					px->errs++;
				else
					return ParseOperator(in, px);
			}
			px->psym  = NULL;
			px->ptype = NULL;
			DescInit(&px->desc);
			break;
			
		case kwNumber:
		case kwString:			
		case kwLiteral:			
			// this is a number, see if there is already a number
			// in the global scope and if not, make one
			//
			px->psym = SYMTABfindVar(px->token.text, px->psymtab);
			if(! px->psym)
			{
				px->psym = AddLiteral(px, px->token.text);
				DescInit(&px->desc);
				px->ptype = NULL;
			}
			if(px->psym)
			{
				if(OPpushOpand(&px->pstack, px->psym))
				{
					px->psym  = NULL;
					return ParseOperator(in, px);
				}
			}
			px->psym  = NULL;
			break;

		case kwLparen:
			
			// a sub-expression - push the paren marker on the stack
			// and parse until a ) is reached
			//
			if(OPpushMarker(&px->pstack))
			{
				// parse the operand recursively, until matching rparen
				// and then get next operator
				//
				if(! ParseOperand(in, px))
				{
					if(px->token.type != kwRparen)
					{
						Log(logError, 4, "Missing right parenthesis\n");
					}
					else
					{
						OPpullMarker(&px->pstack);

						// if parsing a function argument list or a
						// boolean expression for a looping conditional
						// then unwind here, else go on to parse
						// the rest of the expression
						//
						if(px->booling || px->pargrec)
						{
							Log(logDebug, 9, "End of bool/funcargs\n");
							return 0;
						}
						else if(px->pstack && px->pstack->type == opERATOR && px->pstack->op == opCAST)
						{
							// a cast operator was put on the stack as a result of last operand
							// so continue looping to get another operand
							//
							break;
						}
						else // normal case
						{
							return ParseOperator(in, px);
						}
					}
				}
					
			}
			else
			{
				px->errs++;
			}
			break;
			
		case kwOperator:
			
			op = OPencode(px->token.text[0]);
			
			// some operators have the same text but diff
			// meaning in prefix context
			//
			if(op == opBITAND)
				op = opADDROF;
			else if(op == opMUL)
				op = opDEREF;
			else if(op == opMINUS)
				op = opNEGATE;
			else if(op == opADD)
				break; // no reason to handle this??

			if(OPisPrefixUnary(op))
			{
				// a unary operator that's OK to come before an operand
				// (like *, ~, !, &, sizeof, etc.)
				//
				// check previous operator precedence (not much
				// is higer prec than unary prefix ops, but there are a few)
				//
				if(! OPeval(px->pcurfunc, &px->pstack, op))
				{
					// handle sizeof before things get on the stack
					// since we want to all a literal and reference that
					//
					if(op == opSIZEOF)
					{
						POPENTRY pand;
						PSYM     psym;
						char	 szsize[16];
						int		 size;
						
						// get the opernad for sizeof
						Log(logDebug, 7, "Get operand for sizeof\n");
						
						if(! ParseOperand(in, px))
						{
							// look at operand on top of stack
							pand = px->pstack;
							
							// make sure operand is not an intermediate result
							if(! pand || pand->type != opERAND)
							{
								Log(logError, 0, "Bad operand for sizeof\n");
								return 1;
							}
							size = SYMgetSizeBytes(pand->psym);
								
							snprintf(szsize, sizeof(szsize), "%d", size);
	
							// add a literal for the resultant size and use its
							// psym as the operand in place of (sizeof(operand))
							//
							psym = AddLiteral(px, szsize);
							if(psym)
							{
								// discard arg to sizeof
								pand = OPpop(&px->pstack);
								OPdestroy(pand);
								
								pand = OPpushOpand(&px->pstack, psym);
								
								px->ptype = NULL;
								px->psym  = NULL;
								DescInit(&px->desc);
								
								// get next operator (recurses rest of expression)
								//
								return ParseOperator(in, px);
							}
							else
							{
								return 1;
							}
						}
					}
					else if(op == opBOOLNOT)
					{
						// just turn this into "== 0"
						if(OPpushOpand(&px->pstack, g_zero))
						{
							if(OPpushOper(&px->pstack, opBOOLEQ))
							{
								// parse an operand for this operator
								Log(logDebug, 7, "Get operand for unary %s\n", OPname(op));
					
								return ParseOperand(in, px);
							}
						}
					}
					else
					{
						// push on operation 
						OPpushOper(&px->pstack, op);
						
						// parse an operand for this operator
						Log(logDebug, 7, "Get operand for unary %s\n", OPname(op));
					
						return ParseOperand(in, px);
					}
				}
			}
			else
			{
				if(op == opINDEX)
				{
					// empty index ok in decls
					return px->errs;
				}
				else
				{
					Log(logError, 0, "Expected operand, not operator %s\n", px->token.text);
					ParseEOS(in, px, 0);
				}
			}
			break;

		case kwStatement:
		case kwColon:

			px->retok = 1;
			return px->errs;
					
		case kwRbrace:

			Log(logError, 0, "Missing \";\" before }\n");
			px->retok = 1;
			return px->errs;

		case kwLbrace:
			
			Log(logError, 0, "Missing \";\" before }\n");
			px->retok = 1;
			return px->errs;

		case kwRparen:
			
			// only valid for empty argument lists in calls
			// or the end of a cast
			//
			if(px->pargrec)
			{
				px->retok = 1;
				return ParseOperator(in, px);
			}
			if(px->ptype && ! px->psym)
			{
				POPENTRY pcast;
				PSYM	 psym;
				
				// its a cast, create a fake symbol to hold the
				// type in, and push it as an operand to the cast operator
				//
				psym = SYMcreate(NewVarLabel("cst"));
				if(psym)
				{
					if(! px->ptype)
					{
						if(
								px->desc.islong > 0
							||	px->desc.isuns
							||	px->desc.istatic
							||	px->desc.isvol
						)
						{
							// let long/unsigned/static var mean long/int var
							px->ptype = SYMTABfindType("int", px->psymtab);
						}
						else
						{
							Log(logError, 0, "No type in cast\n");
							break;
						}
					}
					// there could be ptr* operators on the stack
					//
					while(px->pstack && px->pstack->type == opERATOR && px->pstack->op == opDEREF)
					{
						px->desc.isptr++;
						OPpop(&px->pstack);
					}

					SYMsetType(psym, px->ptype, &px->desc);

					px->ptype = NULL;
					px->psym  = NULL;
					DescInit(&px->desc);
					
					// remove the marker
					if(px->pstack && px->pstack->type == opMARKER)
					{
						OPpop(&px->pstack);
					}
					else
					{
						Log(logError, 0, "Bad Cast\n");
					}
					pcast = OPcreate(opERATOR, opCAST, psym);
					OPpush(&px->pstack, pcast);
				}
				return  0;
			}
			Log(logError, 0, "Unexpected \"%s\" in expression\n", px->token.text);
			ParseEOS(in, px, 0);
			break;
			
		case kwBuiltinTypeMod:
			
			// got a built-in-type modifier, could be a var decl or cast
			//
			EmbellishType(px);
			break;
			
		default:
			
			Log(logError, 0, "Unexpected \"%s\" in expression\n", px->token.text);
			ParseEOS(in, px, 0);
			break;
		}
	}
	while(! px->token.eof && ! ERRMAX);
	return px->errs;
}

int ParseFunctionArguments(FILE* in, PCCTX px, PSYM pfunc);
int ParseTernary(FILE* in, PCCTX px);

//***********************************************************************
int ParseOperator(FILE* in, PCCTX px)
{
	OPERATOR op;
	POPENTRY ptmp, pvtmp;
	
	// expect and operator to follow an operand
	//
	do
	{
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;

		Log(logDebug, 8, "ParseOper tok=%s\n", px->token.text);
		
		switch(px->token.type)
		{
		case kwPlain:
			
			Log(logError, 0, "Operator expected, not \"%s\"\n", px->token.text);
			ParseEOS(in, px, 0);
			break;

		case kwRbrace:

			if(! px->enumerating)
			{
				Log(logError, 0, "Missing \";\" before }\n");
			}
			px->retok = 1;
			return px->errs;

		case kwLbrace:
			
			Log(logError, 0, "Missing \";\" before }\n");
			px->retok = 1;
			return px->errs;
			
		case kwStatement:

			px->retok = 1;
			return px->errs;

		case kwColon:

			px->retok = 1;
			return px->errs;
			
		case kwLparen:
			
			// function call, evaluate stack so far to end
			//
			if(OPeval(px->pcurfunc, &px->pstack, opCALL))
			{
				px->errs++;
				break;
			}
			// look up the function, and whine about it not
			// being declared, but assume its extern int
			//
			if(
					px->pstack
				&&	px->pstack->type == opERAND
				&&	px->pstack->psym
			)
			{
				if(px->pstack->psym && px->pstack->psym->desc.isfunc)
				{
					// *insert* a call operator under the function name
					//
					ptmp = OPpop(&px->pstack);
					OPpushOper(&px->pstack, opCALL);
					OPpush(&px->pstack, ptmp);
					
					OPdumpStack(&px->pstack, 7, "prefun");

					// parse the argument value list
					//
					if(! ParseFunctionArguments(in, px, px->pstack->psym))
					{
						// the closing paren of the function call should
						// have forced the eval to the left paren of the args
						// and then the call, so all set really

						OPdumpStack(&px->pstack, 7, "postfun");
						
						if(px->pstack->type == opMARKER)
						{
							OPdestroy(OPpop(&px->pstack));
						}
						// then parse rest of expression
						return ParseOperator(in, px);
					}
				}
				else
				{
					Log(logError, 0, "%s is not a function\n", px->pstack->psym->name);
				}
			}
			else
			{
				Log(logError, 0, "No function for call\n");
			}
			break;
			
		case kwRparen:
			
			// evaluate the stack so far, as much as possible
			//
			if(OPeval(px->pcurfunc, &px->pstack, opFORCE))
			{
				px->errs++;
				break;
			}
			// there should be only the call and a marker on the stack. 
			// or perhaps an arg for an empty argument
			//
			if(px->pstack && px->pstack->type == opARGUMENT)
			{
				Log(logDebug, 6, "POPing empty argument\n");
				OPpop(&px->pstack);
			}
			if(! px->pstack)
			{
				Log(logError, 0, "Bad Expression\n");
				ParseEOS(in, px, 0);
				break;
			}
#if 1
			// return back to lparen caller
			return px->errs;
#else
			pvtmp = NULL;
			for(ptmp = px->pstack; ptmp; ptmp = ptmp->next)
			{
				if(ptmp->type == opMARKER)
					break;
				if(ptmp->type != opERAND)
					break;
				pvtmp = ptmp;
			}
			if(ptmp && ptmp->type == opMARKER)
			{
				// this POPs the function marker off even
				// if its not on top!
				//
				if(pvtmp)
					pvtmp->next = ptmp->next;
				else
					px->pstack = ptmp->next;
				
				// ptmp is the matching left paren to the token
				// right paren.  If, undereneath the right paren
				// is a CALL on the stack, then that means that
				// this is the end of an argument list parse
				// 
				if(
						ptmp->next
					&&	ptmp->next->next
					&&	ptmp->next->next->type == opERATOR
					&&	ptmp->next->next->op == opCALL
				)
				{
					Log(logDebug, 10, "Rparen ends arg list\n");
				}
				OPdestroy(ptmp);
			}
			else
			{
				Log(logError, 0, "Missing left parenthesis\n");
			}
			// left paren called in here, so unwind back
			//
			return px->errs;
#endif						
		case kwOperator:
			
			op = OPencode(px->token.text[0]);
			
			// inc and dec can be either, so in this context its postdec
			// but token only comes in as predec e.g.
			//
			if(op == opPREINC)
				op = opPOSTINC;
			else if(op == opPREDEC)
				op = opPOSTDEC;
			
			// note that binary context of "shared" ops like * and &
			// are assumed here while expecting an acutal operator
			//
			if(OPisPrefixUnary(op))
			{
				if(op == opTERNARY)
				{
					if(! ParseTernary(in, px))
					{
						return ParseOperator(in, px);
					}
					else
					{
						return ParseEOS(in, px, 1);
					}
				}
				else
				{
					Log(logError, 0, "Unexpected unary operator %s\n", px->token.text);
				}
			}
			else if(OPisUnary(op))
			{
				// post inc/dec, so basically add a '++and;' on the stack
				// at the end of the expression
				//
				switch(op)
				{
				case opPOSTINC:
				case opPOSTDEC:
					
					// evaluate to operation
					//
					OPeval(px->pcurfunc, &px->pstack, op);

					if(OPpushOper(&px->pstack, op))
					{
						// flop the optop stack order, since unary ops
						// are always under the operand on the opstack
						//
						ptmp = OPpop(&px->pstack);
						if(px->pstack)
						{
							ptmp->next = px->pstack->next;
							px->pstack->next = ptmp;
						}
						return ParseOperator(in, px);
					}
					break;
				default:
					Log(logError, 0, "Unexpected unary operator %s\n", px->token.text);
					break;
				}					
			}
			else // binary operator
			{			
				// check previous operator precedence on stack
				// (and eval if needed)
				//
				if(! OPeval(px->pcurfunc, &px->pstack, op))
				{
					if(op == opINDEX)
					{
						if(px->initing)
						{
							// ends dimension parsing
							Log(logDebug, 10, "Stop on Index expression in initializer\n");
							return 0;
						}
						// the left indice pushed a marker on the stack, so eval to that
						// now to finish index generation and pop off the marker
						// put use the marker now as the index operator, seeing how we
						// haven't pushed it on yet, to avoif the inverse-precedence issue
						// (having the indexing done before the index itself was evaluated)
						//
						if(! OPeval(px->pcurfunc, &px->pstack, opFORCE))
						{
							if(! OPpullMarker(&px->pstack))
							{
								POPENTRY pdexand;

								// pull the index operand off the stack for this part
								//
								pdexand = OPpop(&px->pstack);

								// our marker stopped the eval, so eval again before
								// we actually push the index op on the stack
								//
								if(! OPeval(px->pcurfunc, &px->pstack, op))
								{
									// now put the real index op on
									OPpushOper(&px->pstack, op);

									// and push the index on top
									OPpush(&px->pstack, pdexand);

									// get rest of expression
									return ParseOperator(in, px);
								}
							}
							Log(logError, 0, "Bad index expression\n");
							return ParseEOS(in, px, 0);
						}
					}
					else if(op == opBOOLOR || op == opBOOLAND)
					{
						return ParseBoolean(in, px, op);
					}
					// push the operator
					//
					if(OPpushOper(&px->pstack, op))
					{
						// if this is a struct dot or ptr op, parse the member and
						// go on to get rest of expression via operator
						//
						if(op == opSDEREF || op == opPDEREF)
						{
							Log(logDebug, 7, "Get struct member for %s\n", OPname(op));
							if(! ParseMember(in, px, op))
							{
								return ParseOperator(in, px);
							}
							return ParseEOS(in, px, 0);
						}
						else
						{
							Log(logDebug, 7, "Get operand for %s\n", OPname(op));
							return ParseOperand(in, px);
						}
					}
				}
				else
				{
					Log(logError, 0, "Operand Evaluation failed\n");
				}
			}
			break;
			
		case kwOperEquals:
			
			op = OPencode(px->token.text[0]);
			
			// push a copy of the last operand on the stack
			// after an equal, and push the base operator on after
			//
			if(px->pstack)
			{
				ptmp = OPtop(px->pstack);
				
				if(ptmp->type == opERAND)
				{
					if(OPpushOper(&px->pstack, opEQUAL))
					{
						if(OPpushOpand(&px->pstack, ptmp->psym))
						{
							if(OPpushOper(&px->pstack, op))
							{
								return ParseOperand(in, px);
							}
						}
					}
				}	
			}
			else
			{
				Log(logError, 0, "No Operand for %s\n", px->token.text);
			}
			break;
			
		case kwComma:
			
			Log(logDebug, 8, "ParseOper Comma.  Funcing=%p Initing=%d\n",
					px->pargrec, px->initing);
			
			if(px->pargrec)
			{
				// if at the same expression parenthesis nesting as
				// the function call, ignore a comma, its just
				// separating the arguments
				//
				for(ptmp = px->pstack; ptmp; ptmp = ptmp->next)
				{
					if(ptmp->type == opMARKER)
						break;
				}
				if(ptmp == px->pargrec->pmarker)
				{
					// this is a function arglist level comma
					// don't put it on the stack (but still 
					// evaluate to it to finish pending ops)
					//
					OPeval(px->pcurfunc, &px->pstack, opCOMMA);
					Log(logDebug, 9, "Comma ends argument expression\n");
					return 0;
				}
				else
				{
					Log(logDebug, 8, "ParseOper Comma.  next = %s\n", ptmp->next->next ? 
							(ptmp->next->next->psym ? ptmp->next->next->psym->name:
										OPname(ptmp->next->next->op)) : "<no call under>");
				}
			}
			if(px->initing)
			{
				// if at the same expression parenthesis nesting as
				// and initializer expression, stop at a comma, its just
				// separating the variables
				//
				for(ptmp = px->pstack; ptmp; ptmp = ptmp->next)
				{
					if(ptmp->type == opMARKER)
						break;
				}
				if(! ptmp)
				{
					// un-nested comma operator, push it back
					// and return
					//
					px->retok = 1;
					return 0;
				}
			}
			
			// treat a comma in an operator list as a regular binary
			// operator between previous operand and next one
			//
			if(! OPeval(px->pcurfunc, &px->pstack, opCOMMA))
			{
				if(OPpushOper(&px->pstack, opCOMMA))
				{
					Log(logDebug, 10, "Comma stops expression in initializer\n");
					return ParseOperand(in, px);
				}
			}
			break;

		case kwLindice:

			// start of an index operation, push a marker on, to be evaluated to
			// when the index operator is evaluated.
			//
			if(OPpushMarker(&px->pstack))
			{
				Log(logDebug, 10, "Start index expression\n");
				return ParseOperand(in, px);
			}
			break;

		default:
		
			Log(logError, 0, "Unexpected text \"%s\" in expression\n", px->token.text);
			ParseEOS(in, px, 0);
			break;
		}
	}
	while(! px->token.eof && ! ERRMAX);
	return px->errs;
}

//***********************************************************************
int ParseBooleanExpression(FILE* in, PCCTX px)
{
	px->errs = Token(in, px);
	if(px->errs)
		return px->errs;
	
	Log(logDebug, 6, "ParseBoolExpr tok=%s\n", px->token.text);
	
	if(px->token.type != kwLparen)
	{
		Log(logError, 0, "Expected \'(\' not %s\n", px->token.text);
		ParseEOS(in, px, 0);
	}
	if(OPpushMarker(&px->pstack))
	{
		// get the expression, while indicating no '('
		//
		px->booling = 1;
		ParseOperand(in, px);
		px->booling = 0;
	}
	Log(logDebug, 6, "EndParseBoolExpr\n");
	return px->errs;
}

//***********************************************************************
int ParseFunctionArguments(FILE* in, PCCTX px, PSYM pfunc)
{
	int expret;
	
	Log(logDebug, 6, "ParseFunctionArgs\n");
	
	if(OPpushMarker(&px->pstack))
	{
		PFNREC pf;
		
		// push a function arg record on the context
		//
		pf = (PFNREC)malloc(sizeof(FNREC));
		if(! pf)
		{
			Log(logError, 0, "No funcrec memory\n");
			return 1;
		}
		pf->next = px->pargrec;
		pf->argn = 0;
		pf->line = px->token.line;
		pf->pmarker = px->pstack;
		
		px->pargrec = pf;

		do
		{
			// Push on an argument marker as an operator to let OPeval know that 
			// the operand above it on the stack is an argument so it can push it
			// or move it to a special argument register
			//						
			if(! OPpushArgsList(&px->pstack, pf->argn))
			{
				break;
			}
			// get the next agument (recursively) from the input
			//
			expret = ParseOperand(in, px);
			
			if(! expret)
			{
				OPdumpStack(&px->pstack, 7, "inside arglist");
				
				// parsed either a comma or right parenthesis
				//
				if(px->token.type == kwRparen)
				{
					Log(logDebug, 8, "Rparen ends funcarglist\n");
					break;
				}
				else if(px->token.type != kwComma)
				{
					Log(logError, 1, "Bad token in argument list: %s\n", px->token.text);
					break;
				}
				px->pargrec->argn++;
			}
		}
		while(! expret);
		
		// Eval the stack past the call now, before the argrec goes away
		//
		OPeval(px->pcurfunc, &px->pstack, opCALL);
 
		if(px->pargrec)
  		{
  			pf = px->pargrec;
  			px->pargrec = px->pargrec->next;			
  			free(pf);
		}
	}
	Log(logDebug, 6, "EndParseFuncArgs\n");
	return px->errs;
}

//***********************************************************************
int ParseExpressionOrScope(FILE* in, PCCTX px)
{
	px->errs = Token(in, px);
	if(px->errs)
		return px->errs;
	
	Log(logDebug, 6, "ParseExpression tok=%s\n", px->token.text);
	
	if(px->token.type == kwLbrace)
	{
		int level = px->level;

		// a new scope to parse
		ParseScope(in, px, px->level + 1);
		px->level = level;
	}
	else if(
				px->token.type == kwPlain
			||	px->token.type == kwLparen
			||	px->token.type == kwNumber
	)
	{
		// regular old expression line
		
		// reuse this token in the expression
		//
		px->retok = 1;
		
		// parse the expression
		ParseOperand(in, px);

		// get the ';' or ':' that must follow
		//
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;
		if(px->token.type == kwStatement || px->token.type == kwColon)
		{
			if(px->token.type == kwColon)
			{
				// put the colon back on the input so switch parsing can 
				// check syntax in the return from here and do not
				// evaluate now, the switch code will add the operator!
				//
				px->retok = 1;
				return px->errs;
			}
			else // kwStatementent
			{
				// evaluate the stack to the end
				//
				if(OPeval(px->pcurfunc, &px->pstack, opSTATEMENT))
					px->errs++;
				if(OPclean(&px->pstack))
					px->errs++;
			}
		}
		else
		{
			Log(logError, 0, "Missing semicolon after expression %s\n", px->token.text);
		}
	}
	else if(px->token.type == kwOperator && OPisPrefixUnary(OPencode(px->token.text[0])))
	{
		// parse the expression that happens to begin with an operator
		
		px->retok = 1;
		ParseOperand(in, px);
	}
	else
	{
		Log(logError, 0, "Expected expression, not %s\n", px->token.text);
		ParseEOS(in, px, 0);
	}
	Log(logDebug, 6, "EndParseExpr\n");
	return px->errs;
}

//***********************************************************************
char* NewLabelName(PSPREC psp, char* where)
{
	static char nname[32 + MAX_TOKEN];
	
	if(! psp)
	{
		Log(logWarning, 0, "Internal: no sprec for label\n");
		return "";
	}
	switch(psp->type)
	{
	case kwBuiltin_if:
		snprintf(nname, sizeof(nname), "%si%d%s", CPU_LABEL_PREP, psp->labgen, where);
		break;
	case kwBuiltin_while:
		snprintf(nname, sizeof(nname), "%sw%d%s", CPU_LABEL_PREP, psp->labgen, where);
		break;
	case kwBuiltin_do:
		snprintf(nname, sizeof(nname), "%sd%d%s", CPU_LABEL_PREP, psp->labgen, where);
		break;
	case kwBuiltin_for:
		snprintf(nname, sizeof(nname), "%sf%d%s", CPU_LABEL_PREP, psp->labgen, where);
		break;
	case kwBuiltin_switch:
		snprintf(nname, sizeof(nname), "%ss%d%s", CPU_LABEL_PREP, psp->labgen, where);
		break;
	case kwColon:
		snprintf(nname, sizeof(nname), "%st%d%s", CPU_LABEL_PREP, psp->labgen, where);
		break;
	case kwOperator:
		snprintf(nname, sizeof(nname), "%sb%d%s", CPU_LABEL_PREP, psp->labgen, where);
		break;
	default:
		Log(logError, 0, "Internal: Bad type for sprec\n");
		return "??";
	}
	return nname;
}
//***********************************************************************
PSPREC SPRECpush(KEYWORD type, int line)
{
	static int g_labdex = 1;

	PSPREC psp;
	
	psp = (PSPREC)malloc(sizeof(SPREC));
	if(! psp)
	{
		Log(logError, 0, "Internal: no sprec mem\n");
		return NULL;
	}
	psp->incase   = 0;
	psp->pcase    = NULL;
	psp->pswitch  = NULL;
	psp->lvar	  = NULL;
	psp->didelse  = 0;
	psp->diddef   = 0;
	psp->type     = type;
	psp->line     = line;
	psp->next     = g_spstack;
	g_spstack     = psp;
	psp->labgen   = g_labdex++;
	return psp;
}

//***********************************************************************
PSPREC SPRECpop()
{
	PSPREC psp;
	
	psp = g_spstack;
	if(psp)
	{
		g_spstack = g_spstack->next;
		free(psp);
	}
	else
	{
		Log(logError, 0, "Internal: missing sprec\n");
	}
	return g_spstack;
}
	
//***********************************************************************
int ParseLoop(FILE* in, PCCTX px, KEYWORD type)
{
	PSPREC   psp;
	POPENTRY pop;
	
	switch(type)
	{
	case kwBuiltin_while:
		
		// push a looprec on the spstack
		//
		psp = SPRECpush(type, px->token.line);
		if(! psp) return 1;
		
		px->psp = psp;
		
		Log(logDebug, 6, "ParseWhileLoop\n");
		
		// add code to save stack context for break;
		//
		/******/
		//
		// add the top of loop label to code list
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_loop"));
			
		// push a test on the opstack
		//
		if(OPeval(px->pcurfunc, &px->pstack, opTEST))
			px->errs++;
		if(OPpushOper(&px->pstack, opTEST))
			px->errs++;

		// parse a boolean expression
		//
		ParseBooleanExpression(in, px);

		// evaluate the expression at least to TEST (the right paren
		// should have finished the whole bool expression anyway)
		//
		if(OPeval(px->pcurfunc, &px->pstack, opTEST))
			px->errs++;

		// add a conditional branch to the end of loop on the code list
		// (funcaddgoto pops the result of the test off the stack itself)
		//
		FUNCaddGoto(px->pcurfunc, opBOOLEQ, NewLabelName(px->psp, "_pool"));

		if(OPeval(px->pcurfunc, &px->pstack, opSTATEMENT))
			px->errs++;
		if(OPclean(&px->pstack))
			px->errs++;
		
		// parse an expression or scope
		//
		ParseExpressionOrScope(in, px);

		// add the loop back to top branch to the code list
		//
		FUNCaddGoto(px->pcurfunc, opNONE, NewLabelName(px->psp, "_loop"));
		
		// add the end of loop label to the code list
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_pool"));
		
		Log(logDebug, 6, "EndParseWhileLoop\n");
		
		// pop the looprec off the spstack
		//
		px->psp = SPRECpop();
		break;
		
	case kwBuiltin_do:
		
		// push a looprec on the spstack
		//
		psp = SPRECpush(type, px->token.line);
		if(! psp) return 1;

		px->psp = psp;
		
		// add code to save stack context for break;
		//
		/******/
		
		Log(logDebug, 6, "ParseDoLoop\n");
		
		// add the top of loop label to code list
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_loop"));
		
		// parse an expression or scope
		//
		ParseExpressionOrScope(in, px);

		// parse the "while"
		//
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;
		
		Log(logDebug, 6, "ParseDo tok=%s\n", px->token.text);
		
		if(px->token.type != kwBuiltin_while)
		{
			Log(logError, 0, "Missing while at end of loop\n");
			return 1;
		}
		// push a test on the opstack
		//
		if(OPeval(px->pcurfunc, &px->pstack, opTEST))
			px->errs++;
		if(OPpushOper(&px->pstack, opTEST))
			px->errs++;

		// parse a boolean expression
		//
		ParseBooleanExpression(in, px);

		// evaluate the expression at least to TEST (the right paren
		// should have finished the whole bool expression anyway)
		//
		if(OPeval(px->pcurfunc, &px->pstack, opTEST))
			px->errs++;

		// add a conditional branch to top of loop on the code list
		//
		FUNCaddGoto(px->pcurfunc, opBOOLNEQ, NewLabelName(px->psp, "_loop"));

		if(OPclean(&px->pstack))
			px->errs++;
			
		// add the end of loop label to the code list
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_pool"));
		
		// and the strangly requred semicolon
		//
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;
		
		if(px->token.type != kwStatement)
		{
			Log(logError, 0, "Missing semicolon after while\n");
		}
		
		Log(logDebug, 6, "EndParseDoLoop\n");
		
		// pop the looprec off the spstack
		//
		px->psp = SPRECpop();
		break;		
		
	case kwBuiltin_for:
		
		// push a looprec on the spstack
		//
		psp = SPRECpush(type, px->token.line);
		if(! psp) return 1;

		px->psp = psp;
		
		// add code to save stack context for break;
		//
		/******/
		
		Log(logDebug, 6, "ParseForLoop\n");
		
		// Parse the opening parenthesis
		//
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;
		
		if(px->token.type != kwLparen)
		{
			Log(logError, 0, "Missing \'(\' after for\n");
		}
		// Parse the initial expression
		//
		ParseOperand(in, px);
		
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;		
		if(px->token.type != kwStatement)
		{
			Log(logError, 0, "Missing \';\'\n");
		}
		else
		{
			// evaluate the expression as a statement, to clean stack
			//
			if(OPeval(px->pcurfunc, &px->pstack, opSTATEMENT))
				px->errs++;	
		}
		// add a jump to the condition test code.  The
		// per-loop code gets inserted at after the
		// loop label and before the conditional
		//
		FUNCaddGoto(px->pcurfunc, opNONE, NewLabelName(px->psp, "_ftst"));
		
		if(OPclean(&px->pstack))
			px->errs++;
		
		// add the top of loop label to code list
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_for"));

		// save this location for for moving the per-loop code to
		//
		px->psp->pswitch = px->pcurfunc->pendofcode;

		// add the loop test label to code list
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_ftst"));
		
		// push a test on the opstack
		//
		if(OPeval(px->pcurfunc, &px->pstack, opTEST))
			px->errs++;
		if(OPpushOper(&px->pstack, opTEST))
			px->errs++;
		
		// parse the condtional expression 
		//
		ParseOperand(in, px);

		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;		
		if(px->token.type != kwStatement)
		{
			Log(logError, 0, "Missing \';\'\n");
		}
		else
		{
			// evaluate the expression to the TEST
			//
			if(OPeval(px->pcurfunc, &px->pstack, opTEST))
				px->errs++;	
		}		
		// add a conditional branch to end of loop on the code list
		//
		FUNCaddGoto(px->pcurfunc, opBOOLEQ, NewLabelName(px->psp, "_rof"));
		
		if(OPeval(px->pcurfunc, &px->pstack, opSTATEMENT))
			px->errs++;	
		if(OPclean(&px->pstack))
			px->errs++;

		// save this space in the code as the start of what gets
		// moved up under the loop top
		//
		px->psp->pcase = px->pcurfunc->pendofcode;
		
		// parse the looping expression and the closing parenthesis
		//
		OPpushMarker(&px->pstack);		// to match the right paren
		px->booling = 1; 				// to end exp at paren
		ParseOperand(in, px);
		px->booling = 0;
		
		if(px->token.type != kwRparen)
		{
			Log(logError, 0, "Missing \')\'\n");
		}
		else
		{
			if(OPeval(px->pcurfunc, &px->pstack, opSTATEMENT))
				px->errs++;
			if(OPclean(&px->pstack))
				px->errs++;
		}		
		// move the whole looping code up under the top of loop
		//
		for(pop = px->psp->pcase; pop->next;)
			pop = pop->next;
		
		if(pop)
		{
			pop->next = px->psp->pswitch->next;
			px->psp->pswitch->next = px->psp->pcase->next;
			px->psp->pcase->next = NULL;
		}
		// parse an expression or scope
		//
		ParseExpressionOrScope(in, px);

		// loop
		//
		FUNCaddGoto(px->pcurfunc, opNONE, NewLabelName(px->psp, "_for"));

		if(OPeval(px->pcurfunc, &px->pstack, opSTATEMENT))
			px->errs++;
		if(OPclean(&px->pstack))
			px->errs++;

		// add the end of loop label to the code list
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_rof"));
				
		Log(logDebug, 6, "EndParseForLoop\n");
		
		// pop the looprec off the spstack
		//
		px->psp = SPRECpop();
		break;		
		
	case kwBuiltin_break:
		
		if(! px->psp)
		{
			Log(logError, 0, "No loop to break from\n");
			return 1;
		}
		// restore top-of-loop stack context
		//
		/******/
		
		// add a branch to end of loop on the code list
		//
		FUNCaddGoto(px->pcurfunc, opNONE, NewLabelName(px->psp, "_pool"));
		break;
		
	case kwBuiltin_continue:
		
		for(
				psp = px->psp;
				psp && psp->type != kwBuiltin_while && psp->type != kwBuiltin_do;
		)
			psp = psp->next;
			
		if(! psp)
		{
			Log(logError, 0, "No loop to continue\n");
			return 1;
		}
		// restore top-of-loop stack context
		//
		/******/
		
		// add a branch to top of loop on the code list
		//
		FUNCaddGoto(px->pcurfunc, opNONE, NewLabelName(px->psp, "_loop"));
		break;
	}
	return 0;
}

//***********************************************************************
int ParseIf(FILE* in, PCCTX px, KEYWORD type)
{
	PSPREC psp;
	
	switch(type)
	{
	case kwBuiltin_if:
		
		Log(logDebug, 6, "ParseIf\n");
		
		// push an ifrec on the if stack
		//
		psp = SPRECpush(type, px->token.line);
		if(! psp) return 1;
	
		px->psp = psp;
		
		// push the test of condition on after evaluating up to it
		//
		if(OPeval(px->pcurfunc, &px->pstack, opTEST))
			px->errs++;
		if(OPpushOper(&px->pstack, opTEST))
			px->errs++;

		// parse a boolean expression
		//
		ParseBooleanExpression(in, px);
	
		// evaluate the expression at least to TEST (the right paren
		// should have finished the whole bool expression anyway)
		//
		if(OPeval(px->pcurfunc, &px->pstack, opTEST))
			px->errs++;	

		// add a conditional branch on the code list, if the
		// expression is NOT true, to the else/endif clause
		//
		FUNCaddGoto(px->pcurfunc, opBOOLEQ, NewLabelName(px->psp, "_not"));
		
		// add an end-of-statement to pop expression of stack
		if(OPeval(px->pcurfunc, &px->pstack, opSTATEMENT))
			px->errs++;	

		if(OPclean(&px->pstack))
			px->errs++;

		// parse an expression or scope.
		//
		ParseExpressionOrScope(in, px);

		// if the next token is "else", do this again
		//
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;
		
		Log(logDebug, 6, "ParseIf tok=%s\n", px->token.text);
		
		if(px->token.type == kwBuiltin_else)
		{
			// add a jump to the end of the if to
			// jump around the else clause
			//
			FUNCaddGoto(px->pcurfunc, opNONE, NewLabelName(px->psp, "_end"));
			
			// add the alternate branch target label
			//
			FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_not"));
			
			// parse an expression or scope.
			//
			ParseExpressionOrScope(in, px);

			// add the primary condition done label to the code list
			//
			FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_end"));
		}
		else
		{
			// add the no-else primary condition done label to the code list
			//
			FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_not"));
			
			// and push the whole token back
			//
			px->retok = 1;
		}
		Log(logDebug, 6, "EndParseIf\n");

		// pop the ifrec off the if stack
		//
		px->psp = SPRECpop();
		break;		
		
	case kwBuiltin_else:
		
		Log(logError, 0, "else with no matching if\n");
		return 1;
	}
	return 0;
}

//***********************************************************************
int ParseTernary(FILE* in, PCCTX px)
{
	PSPREC   psp;
	POPENTRY pm;
	int      rv;
	
	// push an ifrec on the if stack
	//
	psp = SPRECpush(kwColon, px->token.line);
	if(! psp) return 1;

	px->psp = psp;

	do // TRY
	{
		// the ? test really applies to the current result on the stack,
		// and since test is unary, it really needs to go UNDER the existing
		// operand, so do that, but first eval to the op, to get the proper opand
		//
		rv = OPeval(px->pcurfunc, &px->pstack, opTERNARY);
		if(rv) break;

		if(! px->pstack || px->pstack->type != opERAND)
		{
			Log(logError, 0, "No expression for ?\n");
			rv = -1;
			break;
		}
		pm = OPcreate(opERATOR, opTEST, NULL);
		if(pm)
		{
			Log(logDebug, 7, "   Insert TEST under conditional for opTERN\n");
			pm->next = px->pstack->next;
			px->pstack->next = pm;
		}
		else
		{
			rv = -2;
			break;
		}
		// put a marker UNDER the TEST now, to avoid eval past this point
		// (the ?) until the end of the secondary expression
		//
		pm = OPcreate(opMARKER, opNONE, NULL);
		if(pm)
		{
			Log(logDebug, 7, "   Insert MARKER under TEST for opTERN\n");
			pm->next = px->pstack->next->next;
			px->pstack->next->next = pm;
		}
		else
		{
			rv = -3;
			break;
		}
		// now eval to the test now that its safely over a marker
		//
		rv = OPeval(px->pcurfunc, &px->pstack, opTEST);
		if(rv) break;

		// push a comma on, tells code generator to get rid of the eval operand
		//
		if(! OPpushOper(&px->pstack, opCOMMA))
		{
			rv = 2;
			break;
		}
		rv = OPeval(px->pcurfunc, &px->pstack, opCOMMA);
		if(rv) break;

		// add a conditional branch to the secondary expression on the code list
		// (funcaddgoto pops the result of the test off the stack itself)
		//
		rv = FUNCaddGoto(px->pcurfunc, opBOOLEQ, NewLabelName(px->psp, "_tsec"));
		if(rv) break;

		// a ? operator parses a whole expression after the ?
		// so use parenthesis here, the ':' should happen
		// after a whole statement (but not before)
		//
		if(! OPpushMarker(&px->pstack))
		{
			rv = 3;
			break;
		}

		// parse an operand for this operator
		Log(logDebug, 7, "Get primary expression for ternary\n");

		rv = ParseOperand(in, px);
		if(rv) break;

		// evaluate to marker
		//
		rv = OPeval(px->pcurfunc, &px->pstack, opFORCE);
		if(rv) break;

		// change the marker to an opTERNARY, to force the code generator
		// to at least load the operand if it isn't loaded (ie,the
		// expression didn't actually do anything) and then as an
		// indication that the result should be placed somewhere 
		// that the rest of the expression knows about.\
		//
		pm = px->pstack;
		if(pm && pm->next && pm->next->type == opMARKER)
		{
			pm = pm->next;

			pm->op   = opTERNARY;
			pm->type = opERATOR;
			// eval this safely, theres a marker under it still
			OPeval(px->pcurfunc, &px->pstack, opTEST);
		}
		else
		{
			Log(logError, 0, "Bad primary expression for ternary\n");
			rv = 4;
			break;
		}
		// ending token shoula been the colon
		//
		if(px->token.type != kwColon)
		{
			Log(logError, 0, "Missing colon for ternary operation\n");
			rv = 5;
			break;
		}
		px->retok = 0; // no need to see the colon again

		// pop off the primary result.  In real-life, only one
		// side of the code is executed, so there will only be
		// one operand result on the stack at the end
		//
		OPpop(&px->pstack);

		// add the jump to end of statement
		//
		rv = FUNCaddGoto(px->pcurfunc, opNONE, NewLabelName(px->psp, "_nert"));
		if(rv) break;

		// add the label for secondary expression
		//
		rv = FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_tsec"));
		if(rv) break;

		// push a marker
		if(! OPpushMarker(&px->pstack))
		{
			rv = 6;
			break;
		}
		// parse secondary expression
		//
		rv = ParseOperand(in, px);
		if(rv) break;

		// evaluate to the marker (might already have been)
		//
		rv = OPeval(px->pcurfunc, &px->pstack, opFORCE);
		if(rv) break;

		// if there isn't an intermediate value on the stack, make it
		//
		// change the marker to an opTERNARY, to force the code generator
		// to at least load the operand if it isn't loaded (ie,the
		// expression didn't actually do anything) and to indicate that
		// the register content has to be saved over a lable AND be in
		// the same register as the primary expression
		//
		pm = px->pstack;
		if(pm && pm->next && pm->next->type == opMARKER)
		{
			pm = pm->next;
			pm->op   = opTERNARY;
			pm->type = opERATOR;
			// eval this safely, theres a marker under it still
			OPeval(px->pcurfunc, &px->pstack, opTEST);
		}
		else
		{
			Log(logError, 0, "Bad secondary expression for ternary\n");
			rv = 8;
			break;
		}
		// evaluate now to the original marker which should leave two operands
		// and only two operands on the stack
		//
		rv = OPeval(px->pcurfunc, &px->pstack, opFORCE);
		if(rv) break;

		// remove the marker under primary/secondary results and 
		// reevaluate to the level of whatever token stopped the
		// expression evaluation (which go short-circuited by
		// the marker we put on)
		//
		rv = OPpullMarker(&px->pstack);
		if(rv) break;

		// add end of expression label
		//
		rv = FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_nert"));
		if(rv) break;

		// whatever operator ended parsing of the ternary operation
		// is still in the token, so I push it back and let
		// the re-read of it finish off the expression
		//
		px->retok = 1;
	}
	while(0); // CATCH

	px->psp = SPRECpop();

	return rv ? rv : px->errs;
}

//***********************************************************************
int ParseBoolean(FILE* in, PCCTX px, OPERATOR op)
{
	PSPREC   psp;
	POPENTRY pm;
	int      rv;
	
	psp = NULL;
	rv  = 0;
	
	// push a boolex on the if stack
	//
	psp = SPRECpush(kwOperator, px->token.line);
	if(! psp)
	{
		return -10;
	}
	px->psp = psp;

	do // TRY
	{
		if(! px->pstack || px->pstack->type != opERAND)
		{
			Log(logError, 0, "No expression for Boolean\n");
			rv = -1;
			break;
		}
		pm = OPcreate(opERATOR, opTEST, NULL);
		if(pm)
		{
			Log(logDebug, 7, "   Insert TEST under conditional for Boolean\n");
			pm->next = px->pstack->next;
			px->pstack->next = pm;
		}
		else
		{
			rv = -2;
			break;
		}
		// put a marker UNDER the TEST now, to avoid eval past this point
		//
		pm = OPcreate(opMARKER, opNONE, NULL);
		if(pm)
		{
			Log(logDebug, 7, "   Insert MARKER under TEST for Boolean\n");
			pm->next = px->pstack->next->next;
			px->pstack->next->next = pm;
		}
		else
		{
			rv = -3;
			break;
		}
		// now eval to the test now that its safely over a marker
		//
		rv = OPeval(px->pcurfunc, &px->pstack, opTEST);
		if(rv) break;

		// hack in a special operator that makes sure the result is in the
		// right register, and that it will be saved over the label
		//
		pm = OPcreate(opERATOR, opTERNARY, NULL);
		if(pm)
		{
			Log(logDebug, 7, "   Insert hack TERNARY under second operand for Boolean\n");
			pm->next = px->pstack->next;
			px->pstack->next = pm;
		}
		else
		{
			rv = -3;
			break;
		}
		rv = OPeval(px->pcurfunc, &px->pstack, opTEST);
		if(rv) break;

		// add a conditional branch around the next sub expression.  If
		// this is an OR, bypass if condition true.  If and, bypass if not
		//
		// (funcaddgoto pops the result of the test off the stack itself)
		//
		rv = FUNCaddGoto(px->pcurfunc, (op == opBOOLAND) ? opBOOLEQ : opBOOLNEQ,
				NewLabelName(px->psp, "_skpbool"));
		if(rv) break;

		// push a comma on, tells code generator to get rid of the first test now
		// so it can put on the second
		//
		if(! OPpushOper(&px->pstack, opCOMMA))
		{
			rv = 2;
			break;
		}
		rv = OPeval(px->pcurfunc, &px->pstack, opCOMMA);
		if(rv) break;

		// parse the rest of the expression
		//
		Log(logDebug, 7, " !!!!!! >>> Get expression for Boolean\n");

		rv = ParseOperand(in, px);
		if(rv) break;

		Log(logDebug, 7, " !!!!!! <<< expression for Boolean ends on %s\n", px->token.text);

		// put on a test
		rv = OPeval(px->pcurfunc, &px->pstack, opTEST);
		if(rv) break;

		if(! px->pstack || px->pstack->type != opERAND)
		{
			Log(logError, 0, "No expression for Boolean\n");
			rv = -1;
			break;
		}
		pm = OPcreate(opERATOR, opTEST, NULL);
		if(pm)
		{
			Log(logDebug, 7, "   Insert TEST under second operand for Boolean\n");
			pm->next = px->pstack->next;
			px->pstack->next = pm;
		}
		else
		{
			rv = -3;
			break;
		}
		// hack in a special operator that makes sure the result is in the
		// right register, and that it will be saved over the label
		//
		pm = OPcreate(opERATOR, opTERNARY, NULL);
		if(pm)
		{
			Log(logDebug, 7, "   Insert hack TERNARY under second operand for Boolean\n");
			pm->next = px->pstack->next->next;
			px->pstack->next->next = pm;
		}
		else
		{
			rv = -3;
			break;
		}
		rv = OPeval(px->pcurfunc, &px->pstack, opTEST);
		if(rv) break;

		// evaluate now to the original marker 
		//
		rv = OPeval(px->pcurfunc, &px->pstack, opFORCE);
		if(rv) break;

		// add the label for bypass
		//
		rv = FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_skpbool"));
		if(rv) break;

		// remove the marker under result operand and 
		//
		rv = OPpullMarker(&px->pstack);

		// whatever operator ended parsing of the boolean expression
		// is in the token. If it was a right paren, hand-eval the
		// rest of the expression like it would have but got shorted
		//
		if(px->token.type != kwRparen)
		{
			px->retok = 1;
		}
		else
		{
			rv = OPeval(px->pcurfunc, &px->pstack, opFORCE);
		}
	}
	while(0); // CATCH

	if(px->psp)
	{
		px->psp = SPRECpop();
	}

	return rv ? rv : px->errs;
}

//***********************************************************************
int ParseSwitch(FILE* in, PCCTX px, KEYWORD type)
{
	PSPREC   psp;
	char     swlabel[MAX_TOKEN + 32];

	POPENTRY psrc, pdst, pend;
	
	switch(type)
	{
	case kwBuiltin_switch:
		
		// push a switchrec on the spstack
		//
		psp = SPRECpush(type, px->token.line);
		if(! psp) return 1;

		px->psp = psp;

		Log(logDebug, 6, "ParseSwitch\n");
		
		// add code to save stack context for break;
		//
		/******/
		
		// add a local variable to hold the switch result
		// in, so the code generator can treat it as a
		// fixederand like the opstack does here
		//
		px->psym = SYMcreate(NewLabelName(px->psp, "_res"));
		DescInit(&px->desc);
		px->desc.isauto = 1;
		px->ptype = SYMTABfindType("int", px->psymtab);
		SYMsetType(px->psym, px->ptype, &px->desc);
		SYMTABaddSym(px->psym, px->psymtab->symbols);
		
		// push that var reference on the stack and an = operator
		//
		OPpushOpand(&px->pstack, px->psym);
		
		// save a ptr to the local var sym
		//
		px->psp->lvar = px->psym;
		px->psym  = NULL;
		px->ptype = NULL;
		
		// set it equal to expression
		//
		OPpushOper(&px->pstack, opEQUAL);

		// evaluate the switch expression.  this is not really
		// a bool, but the function does do what we want
		//
		ParseBooleanExpression(in, px);

		// force the stack to finish eval of the switch expression
		// and store it in the local var we just invented
		//
		if(OPeval(px->pcurfunc, &px->pstack, opSTATEMENT))
			px->errs++;
		if(OPclean(&px->pstack))
			px->errs++;
		
		// add a label here, so the code creator can insert
		// the proper jump table here after all the labels
		// have been collected.  We just produce an if-else
		// implementation of the switch, it is up to the 
		// optimizer to change that to table jump if that
		// is more effecient, and these labels will help
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_swt"));
		
		// save the spot here in the code list to start adding 
		// if-jump code to
		//
		px->psp->pswitch = px->pcurfunc->pendofcode;
		
		// add a jump to the default label here, which is 
		// the official end of the if-jump table for now
		//
		FUNCaddGoto(px->pcurfunc, opNONE, NewLabelName(px->psp, "_dflt"));
		
		// parse a scope (note that it must be a scope so
		// I get the right brace meself here)
		//
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;
		
		if(px->token.type == kwLbrace)
		{
			int level = px->level;

			// note that calls to case, break, etc. happen in here
			// recursive like
			//
			ParseScope(in, px, px->level + 1);
			px->level = level;
		}
		else
		{
			Log(logError, 0, "Expected \'{\' after switch( ), not %s\n", px->token.text);
		}		
		// if the source didn't generate a default, add the
		// default label here at the end of switch
		//
		if(! px->psp->diddef)
		{
			FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_deflt"));
		}		
		// add the end of switch label here
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_tws"));
		
		Log(logDebug, 6, "EndParseSwitch\n");
		
		// pop the ifrec off the if stack
		//
		px->psp = SPRECpop();
		break;
		
	case kwBuiltin_case:

		Log(logDebug, 6, "ParseCase\n");
		
		if(! px->psp || (px->psp->type != kwBuiltin_switch))
		{
			Log(logError, 0, "case outside switch\n");
			return 1;
		}
		// set the flag in the psp that we are in a case, so the next
		// occurance of case, break, default, or end-of-switch will
		// flush out the actual end case label
		//
		px->psp->incase = px->psp->incase + 1;
		
		// remember where we are in the code list, we
		// are going to move this code after its created
		//
		px->psp->pcase = px->pcurfunc->pendofcode;

		OPclean(&px->pstack);	

		// push the test of condition on first
		//
		if(OPeval(px->pcurfunc, &px->pstack, opTEST))
			px->errs++;
		if(OPpushOper(&px->pstack, opTEST))
			px->errs++;


		// push a marker on, we want to force this expression
		// to finish but leave opTEST on the stack
		//
		if(OPpushMarker(&px->pstack))
			px->errs++;

		// parse the expression for case up to ':'
		//
		ParseExpressionOrScope(in, px);
		
		// now evaluate to the marker
		//
		OPeval(px->pcurfunc, &px->pstack, opFORCE);
		
		// insist that what is left is a single constant literal
		// with a TEST under it
		//
		if(px->pstack && px->pstack)
		{
			POPENTRY pop;
			
			if(px->pstack->type != opERAND || (! px->pstack->next || px->pstack->next->type != opMARKER))
			{
				Log(logError, 0, "Bad case expression\n");
			}
			else
			{
				
				if(px->pstack->psym && ! px->pstack->psym->desc.islit)
				{
					Log(logError, 0, "case expression must be constant\n");
				}				
				// unlink the marker out of the stack
				//
				pop = px->pstack->next;
				px->pstack->next = pop->next;
				OPdestroy(pop);
			}
		}
		// push an == as the operator
		//
		if(OPpushOper(&px->pstack, opBOOLEQ))
			px->errs++;
		
		// push a  reference to our local var on the stack
		// as the *second* operand of the booleq (second so
		// the emitter uses it as a source, not dest)
		//
		OPpushOpand(&px->pstack, px->psp->lvar);
				
		// evaluate the expression at least to TEST (the right paren
		// should have finished the whole bool expression anyway)
		//
		if(OPeval(px->pcurfunc, &px->pstack, opTEST))
			px->errs++;
		
		// branch if equal to this case's code start
		//
		sprintf(swlabel, "ca%d", px->psp->incase);
		FUNCaddGoto(px->pcurfunc, opBOOLNEQ, NewLabelName(px->psp, swlabel));
		
		if(OPclean(&px->pstack))
			px->errs++;
		
		// parse the ':'
		//
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;
			
		if(px->token.type != kwColon)
		{
			Log(logError, 0, "Missing \':\' after case\n");
		}
		// move all the code generated for this case to
		// the end of the if-jump list after the switch 
		//
		pdst = px->psp->pswitch;
		psrc = px->psp->pcase->next;
		/*
		Log(logDebug, 5, "Code Generated --->\n");
		EMITdumpCode(psrc, psrc, 1000);
		Log(logDebug, 5, "<--- Code Gen\n");
		*/
		// get a ptr to the last line of code we made
		//
		for(pend = psrc; pend->next;)
			pend = pend->next;
			
		// it is possible (first case) that the switch label
		// code line has the case exp code right under it
		// in which case, there's nothing to do
		//
		if(pdst->next != psrc)
		{
			POPENTRY pe;
			
			// break the link at the bottom to the
			// just moved block to the start of new code
			//
			for(pe = pdst; pe && pe->next; pe = pe->next)
			{
				if(pe->next == psrc)
				{
					pe->next = NULL;
					break;
				}
			}
			// link the new source block into the if table
			//
			pend->next = pdst->next;
			pdst->next = psrc;
		}
		px->psp->pswitch = pend;
		/*
		Log(logDebug, 5, "Code From top --->\n");
		EMITdumpCode(px->pcurfunc->pcode, pend, 1000);
		Log(logDebug, 5, "<--- Code top\n");
		*/
		// generate a unique case label as the target of the jump
		//
		sprintf(swlabel, "ca%d", px->psp->incase);
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, swlabel));

		Log(logDebug, 6, "EndParseCase\n");
		break;
		
	case kwBuiltin_break:
		
		if(! px->psp || (px->psp->type != kwBuiltin_switch))
		{
			Log(logError, 0, "No switch for break\n");
		}
		// restore top of switch stack context
		//
		/*****/
		
		// add jump to end of switch
		//
		FUNCaddGoto(px->pcurfunc, opNONE, NewLabelName(px->psp, "_tws"));
		
		// parse the ';'
		//
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;
		
		if(px->token.type != kwStatement)
		{
			Log(logError, 0, "Missing \';\' after break\n");
		}
		break;
		
	case kwBuiltin_default:

		if(! px->psp || (px->psp->type != kwBuiltin_switch))
		{
			Log(logError, 0, "default outside switch\n");
			return 1;
		}
		// parse the ':'
		//
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;
		
		if(px->token.type != kwColon)
		{
			Log(logError, 0, "Missing \':\' after default\n");
		}
		// this really just moves the default jump target from
		// the end of the switch to the current spot in  code
		// and since the label hasn't been generated yet, this is it
		//
		FUNCaddLabel(px->pcurfunc, NewLabelName(px->psp, "_dflt"));		
		px->psp->diddef = 1;
		/*
		Log(logDebug, 5, "Code From top --->\n");
		EMITdumpCode(px->pcurfunc->pcode, NULL, 1000);
		Log(logDebug, 5, "<--- Code top\n");
		*/
		break;		
	}
	return 0;
}


static FUNCTION g_globalfunc = { NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0, NULL };

//***********************************************************************
int ParseInitializer(FILE* in, PCCTX px)
{
	PSYM psym, ptype;
	SYMTYPE desc;
	PFUNCTION pfunc;
	int  oldopt;

	if(! px->psym)
	{
		Log(logError, 0, "No variable to init\n");
		return ParseEOS(in, px, 1);
	}
	// save these so parseoperand doesnt wipe them
	psym  = px->psym;
	ptype = px->ptype;
	memcpy(&desc, &px->desc, sizeof(desc));

	px->ptype = NULL;
	px->psym  = NULL;
	DescInit(&px->desc);

	// force optimization up past 0 to enable
	// internal math
	//
	oldopt = px->optlevel;
	if(oldopt < 1)
		px->optlevel = 1;
	
	// start with a clean stack 
	//
	OPclean(&px->pstack);
	
	// and a clean code list on our special global func
	//
	FUNCdestroyCode(g_globalfunc.pcode);
	g_globalfunc.pcode      = NULL;
	g_globalfunc.pendofcode = NULL;

	// if above level 0, this is actually an assigment
	// operation as well as a local storage alloc
	// unless its an enum initializer
	//
	if(px->level > 0 && ! px->enumerating)
	{
		OPpushOpand(&px->pstack, psym);
		OPpushOper(&px->pstack, opEQUAL);
	}
	// parse a constant expression, and evaluate it down, with the context's current function
	// replaced with the "global" function to hold the emitted code if any
	//
	px->initing = 1;
	pfunc = px->pcurfunc;
	px->pcurfunc = &g_globalfunc;

	ParseOperand(in, px);

	px->pcurfunc = pfunc;
	px->initing = 0;

	OPeval(&g_globalfunc, &px->pstack, opSTATEMENT);

	// restore the actual type and sym
	//
	px->optlevel = oldopt;
	px->psym  = psym;
	px->ptype = ptype;
	memcpy(&px->desc, &desc, sizeof(desc));
	
	// there should be a constant lit on the op stack (level 0)
	// or a single operand (level > 0)
	//
	if(! px->pstack)
	{
		Log(logError, 0, "Missing Initializer for %s\n", psym->name);
		return 1;
	}
	if(px->pstack->type != opERAND)
	{
		Log(logError, 0, "Wrong type in initializer for %s\n", psym->name);
		OPclean(&px->pstack);
		return 1;
	}
	if(px->level == 0 || px->enumerating)
	{
		if(! px->pstack->psym->desc.islit || ! px->pstack->psym->desc.isconst)
		{
			Log(logError, 0, "Initializer for %s not constant\n", psym->name);
			OPclean(&px->pstack);
			return 1;
		}
		psym->init = px->pstack->psym;
	}
	else
	{
		// initializer for local var, use the code generated
		// in the opstack as the code to init the var, gee,
		// wasn't that simple?
		//
		if(px->pcurfunc)
		{
			if(g_globalfunc.pcode)
			{			
				// just hang the code on the function where it is
				// 
				for(g_globalfunc.pendofcode = g_globalfunc.pcode;
					g_globalfunc.pendofcode && g_globalfunc.pendofcode->next;
				)
					g_globalfunc.pendofcode = g_globalfunc.pendofcode->next;
					
				// link in the code
				//
				if(! px->pcurfunc->pcode)
				{
					px->pcurfunc->pcode      = g_globalfunc.pcode;
					px->pcurfunc->pendofcode = g_globalfunc.pendofcode;
				}
				else
				{
					while(px->pcurfunc->pendofcode && px->pcurfunc->pendofcode->next)
						px->pcurfunc->pendofcode = px->pcurfunc->pendofcode->next;
					px->pcurfunc->pendofcode->next = g_globalfunc.pcode;
					px->pcurfunc->pendofcode = g_globalfunc.pendofcode;
				}
			}
			else
			{
				Log(logError, 0, "Null local var initializer\n");
			}
			//EMITdumpCode(px->pcurfunc->pendofcode, g_globalfunc.pcode, 1000);
			g_globalfunc.pcode = 0;
			g_globalfunc.pendofcode = 0;
		}
		else
		{
			if(px->aggrting)							
				Log(logError, 0, "Initializer not allowed in struct/class\n");
			else
				Log(logError, 0, "No function for local var initializer\n");
		}
	}
	OPclean(&px->pstack);
	return 0;
}

//***********************************************************************
int ParseDimensions(FILE* in, PCCTX px)
{
	PSYM psym, ptype;
	int  oldopt;
	int  dim;
	
	if(! px->psym)
	{
		Log(logError, 0, "No variable to dimension\n");
		return ParseEOS(in, px, 1);
	}
	// save these so parseoperand doesnt wipe them
	psym  = px->psym;
	ptype = px->ptype;
	
	px->ptype = NULL;
	px->psym  = NULL;
	
	// force optimization up past 0 to enable
	// internal math on constants
	//
	oldopt = px->optlevel;
	if(oldopt < 1)
		px->optlevel = 1;
	
	// start with a clean stack 
	//
	OPclean(&px->pstack);
	
	// and a clean code list on our special global func
	//
	FUNCdestroyCode(g_globalfunc.pcode);
	g_globalfunc.pcode      = NULL;
	g_globalfunc.pendofcode = NULL;

	// parse a constant expression, and evaluate it down
	//
	px->initing = 1;
	ParseOperand(in, px);
	px->initing = 0;

	OPeval(&g_globalfunc, &px->pstack, opSTATEMENT);

	// restore the actual type and sym
	//
	px->optlevel = oldopt;
	px->psym  = psym;
	px->ptype = ptype;
	
	// there should be a constant lit on the op stack (level 0)
	//
	if(! px->pstack)
	{
		// its ok for empty dimensions on external arrays, or function arguments
		if(! px->psym->desc.isext && ! px->psym->desc.isdim && ! px->funcing)
		{
			Log(logWarning, 0, "No dimension size for %s\n", psym->name);
			// some compilers consider this a warning // return 1;
		}
		dim = 1;

		psym = SYMcreate("1");
		SYMsetType(psym, g_one, &g_one->desc);
	}
	else 
	{
		if(px->pstack->type != opERAND)
		{
			Log(logError, 0, "Wrong type for dimension of %s\n", psym->name);
			OPclean(&px->pstack);
			return 1;
		}
		if(! px->pstack->psym->desc.islit || ! px->pstack->psym->desc.isconst)
		{
			Log(logError, 0, "Dimension of %s is not constant\n", psym->name);
			OPclean(&px->pstack);
			return 1;
		}
		// get the dimension as an unsigned integer
		dim = (int)strtoul(px->pstack->psym->name, NULL, 10);
	
		// create a symbol to hold the dim for this
		//
		psym = SYMcreate(px->pstack->psym->name);
		SYMsetType(psym, px->pstack->psym, &px->pstack->psym->desc);

		OPclean(&px->pstack);
	}
	// add dim to member list of type
	//
	if(psym)
	{
		psym->members = px->psym->members;
		px->psym->members = psym;
	}
	// multiply dim size with existing dim
	if(px->psym->desc.isdim == 0)
	{
		px->psym->desc.isdim = 1;
		px->psym->desc.bsize = dim;
	}
	else
	{
		px->psym->desc.isdim++;
		px->psym->desc.bsize *= dim;
	}

	Log(logDebug, 6, "Dim %s [%d] %d\n", px->psym->name,
			px->psym->desc.bsize, px->psym->desc.isdim);
	
	return 0;
}

//***********************************************************************
int AddFunction(PCCTX px, PSYM pfunc)
{
	PFUNCTION pf;
	
	for(pf = px->pfunctions; pf; pf = pf->next)
	{
		if(pfunc && pfunc->name && ! strcmp(pf->pfuncsym->name, pfunc->name))
		{
			Log(logError, 0, "Function %s already has a body\n", pfunc->name);
			return 1;
		}
	}
	pf = FUNCcreate(pfunc);
	if(! pf)
		return 2;
	pf->next = NULL;
	px->pcurfunc = pf;
	
	return 0;
}

//***********************************************************************
int ParseFunctionDecl(FILE* in, PCCTX px)
{
	enum
	{
		dsArgType, dsPostArgType,
		dsArgName, dsPostArgName,
		dsFuncBody
	}
	declState = dsArgType;
	
	DescInit(&px->desc);
	px->funcing = 1;
	
	do
	{
		px->errs = Token(in, px);
		if(px->errs)
			break;

		Log(logDebug, 6, "ParseFuncDecl state %d tok=%s\n", declState, px->token.text);
		
		switch(declState)
		{
		case dsArgType:
			
			switch(px->token.type)
			{
			case kwPlain:
					
				// plain old token, this is the actual argument type
				//
				px->ptype = SYMTABfindType(px->token.text, px->psymtab);
				if(! px->ptype)
				{
					Log(logError, 0, "Expected type after (, not %s\n", px->ptype ? px->ptype->name : "type");
				}
				declState = dsArgName;
				break;
				
			case kwBuiltinTypeMod:
				
				// got a built-in-type modifier, so accumulate that in the
				// type descriptor, ignoring bad combos for now
				//
				EmbellishType(px);
				break;

			case kwRparen:
				
				declState = dsFuncBody;
				px->funcing = 0;	
				break;
				
			default:
				
				Log(logError, 0, "Expected type after \'(\', not %s\n", px->token.text);
				break;
			}
			break;
				
		case dsArgName:
			
			switch(px->token.type)
			{
			case kwPlain:
					
				// plain old token, this is the actual argument name
				//
				do	// TRY
				{
					px->psym = SYMTABfindType(px->token.text, px->psymtab);
					if(px->psym)
					{
						Log(logError, 0, "Unexpected type %s after %s\n", px->psym->name, px->ptype ? px->ptype->name : "type");
						break;
					}
					px->psym = SYMTABfindVar(px->token.text, px->psymtab);
					if(px->psym)
					{
						Log(logError, 0, "Argument %s hides previous declaration\n", px->psym->name);
						break;
					}
					if(px->pfunc)
					{
						px->psym = SYMfindMember(px->token.text, px->pfunc);
					}
					if(px->psym)
					{
						Log(logError, 0, "Duplicate argument %s\n", px->psym->name);
						break;
					}
					else
					{
						px->desc.isauto = 1;
						px->psym = AddVariable(px, px->token.text);
					}
				}
				while(0); // CATCH
				DescInit(&px->desc);
				px->ptype = NULL;
				declState = dsPostArgName;
				break;
				
			case kwRparen:
				
				if(
					px->pfunc && ! px->pfunc->members && px->ptype &&
					px->ptype->desc.isvoid && ! px->ptype->desc.isptr
				)
				{
					declState = dsFuncBody;
					px->psym = NULL;
					px->funcing = 0;
				}
				else
				{
					Log(logError, 0, "Argument name missing in function %s\n", px->pfunc->name);
				}
				break;

			case kwOperator:
				
				if(px->token.text[0] == '*')
				{
					px->desc.isptr++;
				}
				else
				{
					Log(logError, 0, "Expected argument name, not operator %s\n", px->token.text);
				}
				break;
				
			default:
				
				Log(logError, 0, "Expected argument name, not %s\n", px->token.text);
				break;
			}
			break;
			
		case dsPostArgName:
			
			switch(px->token.type)
			{
			case kwComma:
					
				DescInit(&px->desc);
				declState = dsArgType;
				break;
				
			case kwRparen:
				
				px->funcing = 0;	
				declState = dsFuncBody;
				break;

			case kwLindice:

				ParseDimensions(in, px);
				break;

			default:
				
				Log(logError, 0, "Syntax:  Expected \',\' or \')\', not %s\n", px->token.text);
				ParseEOS(in, px, 0);
				break;
			}
			break;
			
		case dsFuncBody:
			
			px->funcing = 0; // outside arglist now
			
			switch(px->token.type)
			{
			case kwComma:
			case kwStatement:
				
				// this was apparently a declaration or typedef
				// and not a definition, so put the token back
				// and pop out of here
				//
				px->pfunc = NULL;
				px->retok = 1;
				break;
				
			case kwLbrace:
				
				if(px->level > 0)
				{
					Log(logError, 0, "C does not allow nested functions\n");
				}
				else if(px->typing)
				{
					Log(logError, 0, "Function definition inside typedef\n");
				}
				else
				{
					// got a function body!  create a function
					// block in the function list to hold the code
					//
					if(AddFunction(px, px->pfunc))
					{
						px->errs++;
						px->pfunc = NULL;
					}
					else
					{
						int level;

						// emit function prolog
						//
						px->pfunc->desc.isdef = 1;
						px->pfunc->desc.isext = 0;

						// emit the top part of the function body.  This
						// is done early because the prolog also cleans
						// all the reg-alloc and stack structs.
						//
						FUNCemitProlog(px, px->pcurfunc, px->psymlist);
						
						level = px->level;
						ParseScope(in, px, px->level + 1);
						px->level = level;

						// rest of function emit code is in end-of scope
						// code in ParseScope, since the symbol table
						// gets deleted at the end of the scope
						//
					}
				}
				px->pfunc = NULL;
				px->funcing = 0;
				break;
				
			default:
				
				Log(logError, 0, "Expected \',\' or \')\', not %s\n", px->token.text);
				ParseEOS(in, px, 0);
				px->pfunc = NULL;
				break;
			}
			break;
		}			
	}
	while(px->pfunc && ! px->token.eof && ! ERRMAX);

	return px->errs;
}

//***********************************************************************
int ParseVarDecl(FILE* in, PCCTX px)
{
	enum { dsVar, dsPostVar } declState = dsVar;
	
	do
	{
		px->errs = Token(in, px);
		if(px->errs)
			return px->errs;

		Log(logDebug, 6, "ParseVarDecl of type %s  state %d tok=%s\n",
				px->enumerating ?
					"enum" : (px->ptype ? 
						px->ptype->name : "???"),
				declState, px->token.text);

		switch(declState)
		{
		case dsVar:
			
			switch(px->token.type)
			{
			case kwPlain:
					
				// plain old token, this is a variable of type px->ptype
				//
				do	// TRY
				{
					px->psym = SYMTABfindType(px->token.text, px->psymtab);
					if(px->psym)
					{
						Log(logError, 0, "Unexpected type %s after %s\n", px->psym->name, px->ptype ? px->ptype->name : "type");
						ParseEOS(in, px, 0);
						break;
					}
					px->psym = SYMTABfindVar(px->token.text, px->psymtab);
					if(px->psym)
					{
						Log(logWarning, 0, "Declaration of %s hides previous decl.\n", px->psym->name);
						//break;
					}
					AddVariable(px, px->token.text);
				}
				while(0);	// CATCH
				declState = dsPostVar;
				break;
				
			case kwOperator:
				
				if(px->token.text[0] == '*' && ! px->enumerating)
				{
					px->desc.isptr++;
				}
				else
				{
					Log(logError, 0, "Expected variable, not operator %s\n", px->token.text);
				}
				break;
				
			default:
				
				if(px->token.type == kwStatement && px->pclass->psym)
				{
					// end of struct decl, forwards ok
					//
					px->typing = 0;
					px->ptype  = NULL;
				}
				else
				{
					Log(logError, 0, "Expected %s to declare, not %s\n", 
							px->typing ? "type" : (px->enumerating ?
										"enumeration" : "variable"),
							px->token.text);
					ParseEOS(in, px, 0);
				}
				break;
			}
			break;
			
		case dsPostVar:
			
			switch(px->token.type)
			{
			case kwComma:
				
				// got another var to follow
				declState = dsVar;

				// pointer and dim info isn't sticky across commas but all else is
				px->desc.isptr = 0;
				px->desc.isdim = 0;
				break;
				
			case kwLparen:
				
				if(px->enumerating)
				{
					Log(logError, 0, "Unexpected function call in enumeration\n");
					declState = dsPostVar;
					break;
				}
				// this is now a function decl, not a var
				px->psym->desc.isfunc = 1;
				px->pfunc = px->psym;
				ParseFunctionDecl(in, px);
				declState = dsPostVar;
				break;
				
			case kwStatement:
				
				if(px->enumerating)
				{
					Log(logError, 0, "Expected } not ; in enumeration list\n");
				}
				if(! px->aggrting)
				{
					// all set
					px->typing = 0;
					px->ptype  = NULL;
				}
				else
				{
					return px->errs;
				}
				break;

			case kwOperator:
				
				if(px->token.text[0] == '=' && px->token.text[1] == '\0')
				{
					ParseInitializer(in, px);
				}
				else if(! px->enumerating)
				{
					Log(logError, 0, "Expected ',', ';', '(', '[', or '=', not operator %s\n",
							px->token.text);
					ParseEOS(in, px, 0);
				}
				else
				{
					Log(logError, 0, "Expected ',' or '}' not operator %s in enum list\n",
							px->token.text);
					ParseEOS(in, px, 0);
				}
				declState = dsPostVar;
				break;

			case kwLindice:
				if(! px->enumerating)
				{
					ParseDimensions(in, px);
				}
				else
				{
					Log(logError, 0, "Bad \'[\' in enumeration\n");
					ParseEOS(in, px, 0);
				}
				declState = dsPostVar;
				break;

			case kwRbrace:
				
				if(px->enumerating)
				{
					// end enum list
					return px->errs;
				}
				else
				{
					Log(logError, 0, "Syntax:  Unexpected %s\n", px->token.text);
				}
				break;
				
			default:
				
				if(! px->token.eof)
				{
					Log(logError, 0, "Syntax:  Unexpected %s\n", px->token.text);
					ParseEOS(in, px, 0);
				}
				break;
			}
			break;
		}
	}
	while(px->ptype && ! px->token.eof && ! ERRMAX);

	DescInit(&px->desc);
	px->ptype = NULL;
	px->psym  = NULL;
	px->typing = 0;	
	return px->errs;
}

//***********************************************************************
int ParseEnum(FILE* in, PCCTX px)
{
	// get '{' or varname
	//
	px->errs = Token(in, px);
	if(px->errs)
		return px->errs;

	// we get here on "enum {" or "enum tag" where tag was not already defined
	// but is now a type, and px->ptype is pointing there
	//
	switch(px->token.type)
	{
	case kwPlain:
			
		// plain old token, if it was already a type, we wouldn't be here
		// so this is a var of type ptype, so retok and parse vardecl
		// as in "enum a b,c;"
		//
		px->retok = 1;
		return ParseVarDecl(in, px);

	case kwLbrace:
		
		// parse list of "tag <= val>,".  If there's no ptype yet, setup
		// an int enum type
		//
		if(! px->ptype)
		{
			PSYM ptype;

			ptype = SYMTABfindType("int", px->psymtab);
			if(ptype)
			{
				// create a straw-man type based on int
				Log(logDebug, 8, "Typing untagged enum\n");
				px->ptype = SYMcreate(NewVarLabel("enum"));
				if(px->ptype)
				{
					SYMsetType(px->ptype, ptype, &ptype->desc);
					px->ptype->desc.isenum = 1;
					SYMTABaddSym(px->ptype, px->psymtab->types);
				}
			}
		}
		if(px->ptype)
		{
			PSYM pmember, piniter, pinitee;
			int ordinal;
			
			// flag that were parsing an enum list
			px->enumerating = 1;
			
			// parse a var decl, which will put symbols in the member list
			// of the type instead of adding vars
			//
			ParseVarDecl(in, px);
			
			px->enumerating = 0;
			
			// go through members of enumerated type and make sure 
			// each one has an initializer.
			//
			for(
					ordinal = 0, pmember = px->ptype->members;
					pmember;
					ordinal++, pmember = pmember->members
			)
			{
				// find the var associated with this enum member
				pinitee = SYMTABfindVar(pmember->name, px->psymtab);
				if(! pinitee)
				{
					Log(logError, 0, "No enum type for enum %s\n", pmember->name);
					break;
				}
				piniter = pmember->init;
				if(piniter)
				{
					// already have an initer, restart ordinal at this
					ordinal = strtol(piniter->name, NULL, 0);
				}
				else
				{
					char ordbuf[32];

					_snprintf(ordbuf, sizeof(ordbuf), "%d", ordinal);
					
					// create an initer
					piniter = SYMcreate(ordbuf);
					if(piniter)
					{
						SYMsetType(piniter, pmember->type, &pmember->desc);
					}
					else
					{
						break;
					}
				}
				pinitee->init = piniter;
			}
			
			px->errs = Token(in, px);
			if(px->errs)
				return px->errs;
			
			switch(px->token.type)
			{
			case kwPlain:
				// get var to decl of type
				px->retok = 1;
				return ParseVarDecl(in, px);
				break;
				
			case kwStatement:
				// all set with type
				break;
				
			default:
				Log(logError, 0, "Syntax: Unexpected %s\n", px->token.text);
				break;
			}
		}
		break;
		
	default:
		
		Log(logError, 0, "Unexpected type %s after %s\n", px->psym->name, px->ptype ? px->ptype->name : "type");		
		break;
	}
	
	return 0;
}

//***********************************************************************
PSYM GetIntTypeForSize(unsigned int bitsize)
{
	int  t;
	PSYM pr;
	
	// get the atomic type that has bit size "bitsize"
	//
	for(t = 0; t < g_numAtomicTypes; t++)
	{
		pr = g_atomicTypes[t];
		if(! pr) break;
		if(pr->desc.bsize == bitsize)
			return pr;
	}
	Log(logError, 0, "Machine has no integer type of size %d (bits)\n", bitsize);
	return NULL;
}

//***********************************************************************
PSYM GetRealTypeForSize(unsigned int bitsize)
{
	int  t;
	PSYM pr;
	
	// get the atomic type that has bit size "bitsize"
	//
	for(t = 0; t < g_numAtomicTypes; t++)
	{
		pr = g_atomicTypes[t];
		if(pr->desc.bsize == bitsize && pr->desc.isreal)
		{
			return pr;
		}
	}
	Log(logError, 0, "Machine has no real type of size %d (bits)\n", bitsize);
	return NULL;
}

//***********************************************************************
int ParseScope(FILE* in, PCCTX px, int level)
{
	PSYMTAB   ptab;
	PSYMSTACK pclass;
	int		  scope_end;
	
	g_pctx = px;
	
	
	if(level >= 0)
	{
		ptab = SYMTABcreate();
			
		// create symbol table for this scope and stack on
		// symbol tables stack (one per scope)
		//
		if(px->psymtab)
		{
			ptab->prev = px->psymtab;
			px->psymtab->next = ptab;
			px->psymtab = ptab;
		}
		else
		{
			g_numAtomicTypes = 0;
			
			// no list yet, setup the list head
			//
			px->psymtab  = ptab;
			px->psymlist = ptab;
			
			// and add builtin (atomic) types
			//
			DescInit(&px->desc);	
			
			px->desc.bsize = CPU_CHAR_SIZE;
			g_atomicTypes[g_numAtomicTypes] = SYMcreate("char");
			SYMsetType(SYMTABaddSym(g_atomicTypes[g_numAtomicTypes], px->psymlist->types), NULL, &px->desc);
			g_numAtomicTypes++;
			
			px->desc.bsize = CPU_SHORT_SIZE;
			g_atomicTypes[g_numAtomicTypes] = SYMcreate("short");
			SYMsetType(SYMTABaddSym(g_atomicTypes[g_numAtomicTypes], px->psymlist->types), NULL, &px->desc);
			g_numAtomicTypes++;

			px->desc.bsize = CPU_INT_SIZE;;
			g_atomicTypes[g_numAtomicTypes] = SYMcreate("int");
			SYMsetType(SYMTABaddSym(g_atomicTypes[g_numAtomicTypes], px->psymlist->types), NULL, &px->desc);
			g_numAtomicTypes++;

			px->desc.bsize = CPU_LONG_SIZE;;
			g_atomicTypes[g_numAtomicTypes] = SYMcreate("long");
			SYMsetType(SYMTABaddSym(g_atomicTypes[g_numAtomicTypes], px->psymlist->types), NULL, &px->desc);
			g_numAtomicTypes++;
#if CPU_LONG_LONG_SIZE
			px->desc.bsize = CPU_LONG_LONG_SIZE;;
			g_atomicTypes[g_numAtomicTypes] = SYMcreate("long long");
			SYMsetType(SYMTABaddSym(g_atomicTypes[g_numAtomicTypes], px->psymlist->types), NULL, &px->desc);
			g_numAtomicTypes++;
#endif
#if CPU_FLOAT_SIZE
			px->desc.bsize = CPU_FLOAT_SIZE;;
			px->desc.isreal = 1;
			g_atomicTypes[g_numAtomicTypes] = SYMcreate("float");
			SYMsetType(SYMTABaddSym(g_atomicTypes[g_numAtomicTypes], px->psymlist->types), NULL, &px->desc);
			g_numAtomicTypes++;
#endif
#if CPU_DOUBLE_SIZE
			px->desc.bsize = CPU_DOUBLE_SIZE;;
			px->desc.isreal = 1;
			g_atomicTypes[g_numAtomicTypes] = SYMcreate("double");
			SYMsetType(SYMTABaddSym(g_atomicTypes[g_numAtomicTypes], px->psymlist->types), NULL, &px->desc);
			g_numAtomicTypes++;
#endif
			px->desc.bsize = CPU_ADDRESS_SIZE;;
			px->desc.isvoid = 1;
			g_atomicTypes[g_numAtomicTypes] = SYMcreate("void");
			SYMsetType(SYMTABaddSym(g_atomicTypes[g_numAtomicTypes], px->psymlist->types), NULL, &px->desc);
			g_numAtomicTypes++;

			g_zero = SYMTABfindVar("0", px->psymtab);
			if(! g_zero)
			{
				px->ptype = NULL;
				px->psym  = NULL;
				DescInit(&px->desc);
				g_zero = AddLiteral(px, "0");
				px->ptype = NULL;
				px->psym  = NULL;
				DescInit(&px->desc);
			}			
			g_one = SYMTABfindVar("1", px->psymtab);
			if(! g_one)
			{
				px->ptype = NULL;
				px->psym  = NULL;
				DescInit(&px->desc);
				g_one = AddLiteral(px, "1");
				px->ptype = NULL;
				px->psym  = NULL;
				DescInit(&px->desc);
			}			
		}
		// assume not aggregating
		//
		px->aggrting = 0;
		
		// create a class symbol entry for this scope and push on the stack
		// of class symbols for each nexted scope.
		//
		pclass = (PSYMSTACK)malloc(sizeof(SYMSTACK));
		if(! pclass)
		{
			Log(logError, 0, "Internal: Structure memory\n");
		}
		else
		{
			pclass->psym = NULL;
			pclass->prev = px->pclass;
			px->pclass   = pclass;
			
			if(px->pclass->prev && px->pclass->prev->psym)
			{
				// open class in parent scope means we are aggregating 
				// members of the class in this scope
				//
				px->aggrting = 1;
			}
		}
	}
	// Setup the type descriptor to prepare for building up a type
	//
	DescInit(&px->desc);	
	
	// indicate NOT in a typedef
	px->typing = 0;
	
	// indicate NOT in func argslist
	px->funcing = 0;

	// clear function arglist stack
	if(px->pargrec)
	{
		PFNREC pf;
		
		Log(logError, 0, "Unterminated function call at line %d\n", px->pargrec->line);
		while(px->pargrec)
		{
			pf = px->pargrec;
			px->pargrec = px->pargrec->next;
			free(pf);
		}
	}
	// parse tokens in this scope
	//
	px->ptype = NULL;
	px->psym  = NULL;
	px->pfunc = NULL;
	
	scope_end = 0;	

	Log(logDebug, 3, "Scope %d  func=%s aggrt=%d\n",
			level, px->pcurfunc ? px->pcurfunc->pfuncsym->name : "global", px->aggrting);
		
	do
	{
		px->level = level;
		px->errs += Token(in, px);
		if(px->errs > 10)
		{
			return px->errs;
		}

		switch(px->token.type)
		{
		case kwTypeDef:

			// got a typedef keyword, not too exciting, so just
			// set a flag that tokens after the type spec are
			// types not vars when we get to the end
			//
			if(px->ptype)
			{
				Log(logError, 0, "typedef unexpeced after type: %s\n", px->ptype->name);
				break;
			}
			px->typing = 1;
			break;

		case kwBuiltinTypeMod:
			
			if(px->desc.isaggrt)
			{
				Log(logError, 0, "Type mod %s not allowed after %\n",
						px->token.text, AggregateName(px->desc.isaggrt));
				break;
			}
			// got a built-in-type modifier, so accumulate that in the
			// type descriptor, ignoring bad combos for now.
			//
			EmbellishType(px);
			
			// If the type became an aggregate type, create a straw-man
			// variable to hold the members before the var or type sym is made
			//
			if(px->desc.isaggrt)
			{
				if(px->ptype)
				{
					Log(logError, 0, "Type mod %s not allowed with %s\n",
							px->ptype->name, AggregateName(px->desc.isaggrt));
					px->ptype = NULL;
					break;
				}
				if(px->pclass->psym)
				{
					Log(logError, 0, "Nested aggregate without scope not allowed in %s\n",
							px->pclass->psym->name);
					break;
				}
				// setup a new class symbol strawman in case this is a type
				//
				px->pclass->psym = SYMcreate(NewStructName(px->desc.isaggrt));
				SYMTABaddSym(px->pclass->psym, px->psymtab->types);
				SYMsetType(px->pclass->psym, NULL, &px->desc);
			}
			break;

		case kwPlain:
				
			// plain old token maybe an atomic or user defined type
			//
			if(px->ptype)
			{
				Log(logError, 0, "Type %s unexpected after type: %s\n", px->token.text, px->ptype->name);
				break;
			}
			else if(px->pclass->psym)
			{
				// maybe declaring a struct, see if this is already an
				// aggregate type in the table
				//
				px->ptype = SYMTABfindType(StructType(px->token.text, px->desc.isaggrt), px->psymtab);
				if(px->ptype)
				{
					// found a type, expect var or func decl to follow
					// and remove the class straw-man
					//
					SYMTABremoveSym(px->pclass->psym, px->psymtab->types);
					SYMdestroy(px->pclass->psym);
					px->pclass->psym = px->ptype;
					Log(logDebug, 8, "struct is actually type %s\n",
							px->pclass->psym->name);

					// if the next token is an open brace, this is the defintion
					// of the struct and the type found was a forward decl.
					// else the token is a var to decl of type, which could be
					// a struct as well
					//
					px->errs += Token(in, px);
					if(px->errs > 10)
					{
						return px->errs;
					}
					if(px->token.type == kwLbrace)
					{
						if(px->ptype->members)
						{
							// already have a definition
							//
							Log(logError, 0, "Structure %s has already been defined\n",	px->ptype->name);
						}
						// just let lbrace handler below deal
						DescInit(&px->desc);
						px->ptype = NULL;
						px->retok = 1;
						break;
					}
					else
					{
						px->retok = 1;
						ParseVarDecl(in, px);
					}
					px->pclass->psym = NULL;
					DescInit(&px->desc);
					px->ptype = NULL;
				}
				else
				{
					// declaring a struct, this would be the typename
					//
					Log(logDebug, 12, "rename struct %s %s\n",
							px->pclass->psym->name, StructType(px->token.text,
									px->pclass->psym->desc.isaggrt));

					SYMTABremoveSym(px->pclass->psym, px->psymtab->types);
					SYMrename(px->pclass->psym, StructType(px->token.text,
							px->pclass->psym->desc.isaggrt));
					SYMTABaddSym(px->pclass->psym, px->psymtab->types);
				}
			}
			else if(px->desc.isenum)
			{
				px->ptype = SYMTABfindType(px->token.text, px->psymtab);
				if(px->ptype)
				{
					if(px->ptype->desc.isenum)
					{
						// decl an enum var of type "enum token "
						//
						ParseVarDecl(in, px);
					}
					else
					{
						Log(logError, 0, "Type %s is not an enumerated type\n",
								px->token.text);
					}
				}
				else
				{
					// typing an enum type token
					//
					px->ptype = SYMTABfindType("int", px->psymtab);
					if(px->ptype)
					{
						Log(logDebug, 8, "Typing enum %s\n", px->token.text);
						px->psym  = SYMcreate(px->token.text);
						SYMsetType(px->psym, px->ptype, &px->desc);
						SYMTABaddSym(px->psym, px->psymtab->types);
						px->ptype = px->psym;
						px->psym = NULL;
						ParseEnum(in, px);
					}
				}
				DescInit(&px->desc);
				px->ptype = NULL;
			}
			else
			{
				// if token is a type, this is a var decl, else an
				// expression
				//
				px->ptype = SYMTABfindType(px->token.text, px->psymtab);
				if(px->ptype)
				{
					// found a type, expect var or func decl to follow
					//
					ParseVarDecl(in, px);
					DescInit(&px->desc);
					px->ptype = NULL;
				}
				else if(! px->token.eof)
				{
					// not a type, if at global scope, assume its a 
					// "int" function name, like good old C does,
					// and parse the function args
					//
					if(level <= 0)
					{
						char funcname[MAX_TOKEN];
						
						strcpy(funcname, px->token.text);
	
						px->errs = Token(in, px);
						if(px->errs)
						{
							return px->errs;
						}
						if(px->token.type == kwLparen)
						{
							px->desc.isfunc = 1;
							px->ptype = SYMTABfindType("int", px->psymtab);
							px->pfunc = AddVariable(px, funcname);
							DescInit(&px->desc);
							px->ptype = NULL;
							px->psym  = NULL;
							ParseFunctionDecl(in, px);
						}
						else
						{
							Log(logError, 0, "Expected type or function, got %s\n", px->token.text);
							ParseEOS(in, px, 0);
						}
					}
					else
					{
						// this is a symbolic ref in an expression, or a
						// function call perhaps, just let expression parser handle it
						//
						if(px->aggrting)
						{
							Log(logError, 0, "Syntax:  Unexpected %s in struct/class definition\n",
									px->token.text);
							ParseEOS(in, px, 0);
							break;
						}
						px->retok = 1;
						ParseOperand(in, px);
					}
				}
				else
				{
					scope_end = 1;
				}
			}
			break;
			
		case kwOperator:
		case kwLparen:

			// push the token back and let the expression parser
			// deal with it, regardless of what it is
			//
			px->retok = 1;
			ParseOperand(in, px);
			break;
			
		case kwStatement:

			// whine if nothing on the opstack
			//
			if(! px->pstack && ! px->desc.isaggrt)
			{
				Log(logWarning, 0, "Empty expression\n");
			}
			Log(logDebug, 10, "Scope: end of expression\n");
			if(OPeval(px->pcurfunc, &px->pstack, opSTATEMENT))
				px->errs++;
			if(OPclean(&px->pstack))
				px->errs++;
			if(px->desc.isaggrt)
			{
				// forward decl of struct, leave it
				DescInit(&px->desc);
				px->ptype = NULL;
				px->pclass->psym = NULL;
			}
			break;
			
		case kwLbrace:
			
			// got an open brace, recurse into the new scope
			// if not typedefing or aggregating, or var decling
			//
			if(px->ptype)
			{
				Log(logError, 0, "var declaration expected after type: %s\n", px->token.text, px->ptype->name);
				break;
			}
			else if(px->pclass->psym)
			{
				int typing, aggrting;
				
				// save state of typedefing/aggrting over the scope parse
				typing = px->typing;
				px->typing = 0;

				aggrting = px->aggrting;
				px->aggrting = 1;

				// got a { after struct or struct tag, so parseout the members
				//
				ParseScope(in, px, level + 1);
				px->level = level;

				px->typing = typing;
				px->aggrting = aggrting;

				// now that members are parsed, the type becomes
				// the class and now decl vars or types of type
				//
				px->ptype = px->pclass->psym;

				// go ahead and create byte offsets for the struct members
				// now to avoid having to walk type tables
				//
				FUNCenmemberAggrt(px->ptype);

				ParseVarDecl(in, px);

				// this needs to be non-null in parsevardecl to 
				// avoid warnings of no var
				px->pclass->psym = NULL;
				px->ptype = NULL;
			}
			else
			{
				if(! px->pcurfunc)
				{
					// it could be a tagless enum
					if(px->desc.isenum)
					{
						px->retok = 1;
						ParseEnum(in, px);
						px->ptype = NULL;
					}
					else
					{
						Log(logError, 0, "No function for scope\n");
						break;
					}
				}
				else
				{
					// plain old scope
					//
					ParseScope(in, px, level + 1);
					px->level = level;
				}
			}
			DescInit(&px->desc);
			break;
			
		case kwRbrace:
			
			if(level <= 0)
			{
				Log(logError, 0, "Unexpected } at file scope\n");
			}
			else
			{
				// simple end of scope, all set, so pop out
				// after making sure the stack is empty
				//
				if(OPclean(&px->pstack))
					px->errs++;

				Log(logDebug, 7, "end of scope %d\n", px->level);
				scope_end = 1;
			}
			break;
			
		case kwBuiltin_switch:
		case kwBuiltin_case:
		case kwBuiltin_default:
			
			if(ParseSwitch(in, px, px->token.type))
				px->errs++;
			break;
			
		case kwBuiltin_break:
			
			// figure out what the inner construct is
			// (loop or switch)
			//
			{
				PSPREC psp, prealsp;
				
				for(
						psp = px->psp, prealsp = NULL;
						psp && ! prealsp;
						psp = psp->next
				)
				{
					switch(psp->type)
					{
					case kwBuiltin_switch:
					case kwBuiltin_while:
					case kwBuiltin_do:
					case kwBuiltin_for:
						prealsp = px->psp;
						break;
					default:
						break;
					}
				}
				if(prealsp)
				{
					// trick parse code into handling
					// this keyword as if at the base level
					//
					px->psp = psp;
					
					switch(psp->type)
					{
					case kwBuiltin_while:
					case kwBuiltin_do:
					case kwBuiltin_for:
						if(ParseLoop(in, px, px->token.type))
							px->errs++;
						break;
					case kwBuiltin_switch:
						if(ParseSwitch(in, px, px->token.type))
							px->errs++;
						break;
					}
					// revert context to current construct
					//
					px->psp = prealsp;
				}
				else
				{
					Log(logError, 0, "Illegal break\n");
				}
			}
			break;
			
		case kwBuiltin_while:
		case kwBuiltin_do:
		case kwBuiltin_continue:
		case kwBuiltin_for:

			if(ParseLoop(in, px, px->token.type))
				px->errs++;
			break;
			
		case kwBuiltin_if:
		case kwBuiltin_else:

			if(ParseIf(in, px, px->token.type))
				px->errs++;
			break;
			
		case kwBuiltin_asm:
		case kwBuiltin_goto:
		case kwBuiltin_entry:

		/* these are all C++
		*/
		case kwBuiltin_catch:
		case kwBuiltin_delete:
		case kwBuiltin_false:
		case kwBuiltin_new:
		case kwBuiltin_template:
		case kwBuiltin_this:
		case kwBuiltin_throw:
		case kwBuiltin_true:
		case kwBuiltin_try:
			
			Log(logError, 0, "Keyword %s not implemented\n", px->token.text);
			break;
			
		default:
			
			if(! px->token.eof)
			{
				Log(logError, 0, "Expected keyword, type, or typedef, not %s\n", px->token.text);
				ParseEOS(in, px, 0);
			}
			break;
		}
	}	
	while(! scope_end && ! px->token.eof && ! ERRMAX);
		
	if(level >= 0)
	{
		PSYMTAB ptab;
		
		SYMTABdump(px->psymtab, level);
		
		// remove the scope entry from the class stack
		//
		if(px->pclass->psym)
		{
			Log(logError, 0, "Missing '}' in decl of class %s\n",
					px->pclass->psym->name);
			// note that psym is already in, and hence deleted from,
			// the scope symbol table, so don't delete it here
			px->pclass->psym = NULL;
		}
		if(px->pclass->prev)
		{
			PSYMSTACK psx = px->pclass;

			px->pclass = px->pclass->prev;
			free(psx);
		}
		else
		{
			free(px->pclass);
			px->pclass  = NULL;
		}
		// pop the symbol table stack, except leave the
		// bottom layer alone
		//
		ptab = px->psymtab;
		if(ptab->prev)
		{
			// pop ptab off tab stack
			px->psymtab = ptab->prev;
			ptab->prev->next = NULL;

			// hang it on the function's list
			if(px->pcurfunc)
			{			
				// move the symbols for this scope
				// to the list (not stack) of symbols
				// assigned to this function
				//
				if(! px->pcurfunc->psymtab)
				{
					px->pcurfunc->psymtab = ptab;
				}
				else
				{
					PSYMTAB pnt;
					
					for(pnt = px->pcurfunc->psymtab; pnt->prev;)
						pnt = pnt->prev;
					pnt->prev = ptab;
					ptab->next = pnt;
				}
				ptab->prev = NULL;
			}
		}
		if((level == 1) && px->pcurfunc)
		{		
			PFUNCTION pf;
									
			// emit local storage/argument/return allocs
			//
			FUNCemitLocals(px->pcurfunc);
			
			// emit global storage allocs (they go at the top)
			//
			FUNCemitGlobals(px->pcurfunc, px->psymlist);

			// now entire oplist should be generated, so dump it (debug)
			//
			EMITdumpCode(px->pcurfunc->pcode, NULL, 1000);
			
			// emit the body code
			//
			FUNCemitCode(px, px->pcurfunc);
			
			// do function epilog
			//
			FUNCemitEpilog(px, px->pcurfunc);
						
			// get rid of the opentry list
			//
			FUNCdestroyCode(px->pcurfunc->pcode);
			
			px->pcurfunc->pcode = NULL;
			
			// move completed function onto func list (at the end)
			//
			px->pcurfunc->next = NULL;
			pf = px->pfunctions;
			if(! pf)
			{
				px->pfunctions = px->pcurfunc;
			}
			else
			{
				while(pf->next)
					pf = pf->next;
				pf->next = px->pcurfunc;
			}
			px->pcurfunc = NULL;
		}
	}
	return px->errs;	
}

//***********************************************************************
int GenerateCode(PCCTX px)
{
	PFUNCTION pf;
	int       rv;
	
	rv = CODEgenProlog(px, 1);
		
	for(pf = px->pfunctions; pf && !rv; pf = pf->next)
	{
		rv = CODEgenProlog(px, 0);
		rv = CODEoutput(px, pf);
		rv = CODEgenEpilog(px, 0);
	}
	rv += CODEgenEpilog(px, 1);
	return rv;
}


