#include "../../bccx.h"


static enum { secBAD, secTEXT, secDATA, secRODATA } g_section;

static int g_stackalloc;

#define STR_GLOBAL_UNDSCORE	((px->underscore_globals)?"_":"")

//***********************************************************************
int GENERICgenProlog(PCCTX px, int first)
{
	if(first || ! px->af)
	{
		char* po;
		
		strcpy(px->asmf, px->file);
		for(po = px->asmf; *po && *po != '.';)
			po++;
		strcpy(po, ".s");
		
		px->af = fopen(px->asmf, "w");
		if(! px->af)
		{
			Log(logError, 0, "Can't open output file %s\n", px->asmf);
			return 1;
		}
		fprintf(px->af, "\t.file\t\"%s\"\n", px->file);
		fprintf(px->af, "\t.align\t%d\n", CPU_LONG_SIZE / 8);

		g_stackalloc = 0;
	}
	else if(! first)
	{
		g_section	= secBAD;
	}
	return 0;
}

//***********************************************************************
int GENERICgenEpilog(PCCTX px, int last)
{
	if(last)
	{
		if(px->af)
		{
			fclose(px->af);
			px->af = NULL;
		}
	}
	return 0;
}

//***********************************************************************
PASM PASMcreate(
				CODETYPE type,
				OPCODE   opcode,
				PREGREC ra,  PREGREC rb,
				PSYMREF psa, PSYMREF psb
				)
{
	PASM pemit;
	
	pemit = (PASM)malloc(sizeof(ASM));
	if(! pemit)
	{
		Log(logError, 0, "Internal: no emitter memory\n");
		return NULL;
	}
	pemit->type 	= type;
	pemit->opcode   = opcode;
	pemit->prev 	= NULL;
	pemit->next 	= NULL;
	
	// these are used only to get the register name, and
	// to detect which registers are used in the operation
	//
	pemit->pra  = ra;
	pemit->prb  = rb;
	
	// these symbols are the thing that is IN the register at
	// this time.  I pass on copies so whats in the register
	// after this time, including deleting its symr, can't
	// effect the emit list
	//
	pemit->psa = psa ? SYMREFcreateCopy(psa) : NULL;
	pemit->psb = psb ? SYMREFcreateCopy(psb) : NULL;
	
	return pemit;
}

//***********************************************************************
void PASMdestroy(PASM pasm)
{
	if(pasm) free(pasm);
}

//***********************************************************************
PASM PASMprogram(PASMPROG pprog, PASM pasm)
{
	PASM pnew, pnext, pfirst;

	CODETYPE type;
	OPCODE   opcode;
	PREGREC	 pra, prb;
	PSYMREF	 psa, psb;
	const char* sa, sb;
	
	pfirst = NULL;
	pnext  = NULL;
	
	while(pprog)
	{
		type   = pprog->type;
		opcode = pprog->opcode;
		
		if(pprog->ra >= 0)
		{
			pra = CODEgetRegister(pprog->ra);
		}
		else if(pprog->ra == -2)
		{
			pra = pasm->pra;
		}
		else if(pprog->ra == -1)
		{
			pra = pasm->prb;
		}
		else
		{
			pra = NULL;
		}
		if(pprog->ra >= 0)
		{
			pra = CODEgetRegister(pprog->ra);
		}
		else if(pprog->ra == -2)
		{
			pra = pasm->pra;
		}
		else if(pprog->ra == -1)
		{
			pra = pasm->prb;
		}
		else
		{
			pra = NULL;
		}
		if(pprog->rb >= 0)
		{
			prb = CODEgetRegister(pprog->rb);
		}
		else if(pprog->rb == -2)
		{
			prb = pasm->pra;
		}
		else if(pprog->rb == -1)
		{
			prb = pasm->prb;
		}
		else
		{
			prb = NULL;
		}
		psa = NULL;
		psb = NULL;
		
		pnew = PASMcreate(type, opcode, pra, prb, psa, psb);
		if(pnew)
		{
			if(! pfirst)
			{
				pnext = pfirst = pnew;
			}
			else
			{
				pnext->next = pnew;
			}
			pnext = pnew;
		}			
	}
	return pfirst;
}

//***********************************************************************
PASM GENERICgenerate(PCCTX px, PFUNCTION pfunc, PASM pasm)
{
	switch(pasm->type)
	{
	// these are cpu specific, can't do genericly
	//
	case codeINSN:		// an actual operation
	case codeMOVE:		// ra <- rb
	case codeARG:		// make an argument from ra/psa
	case codeSTORE:		// psa or ra <- rb
	case codeTSTORE:	// push ra
	case codeTLOAD:		// pop ra
	case codeLOAD:		// ra <- psa
	case codeLOADADDR:	// ra <- &psa
	case codeSWAP:		// ra <-> rb
	case codeBR:		// goto psa
	case codeBEQ:		// goto psa if condition == 0
	case codeBNE:		// goto psa if condition != 0
		Log(logError, 0, "Can't genericly handle code\n");
		break;		
	
	// these are just passed on to the list, no mods needed
	//
	case codeLABEL:		// label:
	case codePUBLIC:	// public
	case codeGLOBAL:	// allocate for var
	case codeEXTERN:	// extern
	case codeEOF:		// end of function
	case codeCOMMENT:	// comment in psa
	case codeDEBUG:		// debug info in psa
		break;

	case codeLOCAL:		// allocate stack space (remember how many local bytes)
		break;
		
	default:
		
		Log(logError, 0, "Internal - unhandled code\n");
		break;
	}
	return pasm;	
}

//***********************************************************************
static int GENERICgloballoc(PCCTX px, int chunk, PSYMREF psymr)
{
	char* cname;
	
	if(psymr->psym->init)
	{
		if(
				psymr->psym->init->desc.isdim == 1
			&&	psymr->desc.isdim == 1
		//	&&	psymr->type->desc.bsize == 1
			&&	psymr->psym->init->name[0] == '\"'
		)
		{
			fprintf(px->af, "%s%s:\n\t.string\t%s\n",
				(psymr->desc.istatic ? "" : STR_GLOBAL_UNDSCORE),
					psymr->name, psymr->psym->init->name);
			return 0;
		}
	}
	if(psymr->desc.istatic)
		cname = ".lcomm";
	else
		cname = ".comm";

	fprintf(px->af, "\t%s\t%s%s, %d\n", cname, 
				(psymr->desc.istatic ? "" : STR_GLOBAL_UNDSCORE),
				psymr->name, chunk);
	return 0;
}

//***********************************************************************
int GENERICoutput(PCCTX px, PFUNCTION pfunc, PASM pasm)
{
	// make sure right section is in use
	//
	if(g_section != secTEXT && (pasm->type != codePUBLIC && pasm->type != codeGLOBAL))
	{
		fprintf(px->af, "\t.text\n");
		g_section = secTEXT;
	}
	switch(pasm->type)
	{
	// these are cpu specific, can't do genericly
	//
	case codeINSN:		// an actual operation
	case codeMOVE:		// ra <- rb
	case codeARG:		// make an argument from ra/psa
	case codeSTORE:		// psa or ra <- rb
	case codeTSTORE:	// push ra
	case codeTLOAD:		// pop ra
	case codeLOAD:		// ra <- psa
	case codeLOADADDR:	// ra <- &psa
	case codeSWAP:		// ra <-> rb
	case codeBR:		// goto psa
	case codeBEQ:		// goto psa if condition == 0
	case codeBNE:		// goto psa if condition != 0
		Log(logError, 0, "Can't genericly handle code\n");
		return 1;
	
	case codeLABEL:		// label:
		fprintf(px->af, "%s%s:\n", (pasm->psa->desc.istatic ? "" : STR_GLOBAL_UNDSCORE), pasm->psa->name);
		if(pasm->psa && pasm->psa->desc.isfunc)
		{
			fprintf(px->af, "\t%-6s  %s\n", "pushl", "%ebp");
			fprintf(px->af, "\t%-6s  %s\n", "movl",  "%esp, %ebp");
		}
		break;
		
	case codePUBLIC:	// public
	case codeGLOBAL:	// allocate for var
		if(g_section != secDATA)
		{
			fprintf(px->af, "\t.data\n");
			g_section = secDATA;
		}
		if(pasm->type == codePUBLIC)
		{
			fprintf(px->af, "\t%-6s\t%s%s\n", ".globl", STR_GLOBAL_UNDSCORE, pasm->psa->name);
		}
		if(! pasm->psa->desc.isfunc)
		{
			if(! pasm->psa->desc.islit || pasm->psa->desc.isdim)
			{
				int chunk;
				char* cname;
				
				// figure out what size chunks to alloc based on
				// the symbols type
				//
				chunk = SYMREFgetSizeBytes(pasm->psa);
				if(chunk < CPU_ALIGN_BYTES)
					chunk = CPU_ALIGN_BYTES;
				
				switch(chunk)
				{
				case 1: cname = ".byte"; break;
				case 2: cname = ".half"; break;
				case 4:	cname = ".long"; break;
				default: cname = ".array!"; break;
				}
				if(pasm->psa->desc.isaggrt || pasm->psa->desc.isdim)
				{
					GENERICgloballoc(px, chunk, pasm->psa);
				}
				else
				{
					// the label
					//
					fprintf(px->af, "%s%s:\t", (pasm->psa->desc.istatic ? "" : STR_GLOBAL_UNDSCORE),
							pasm->psa->name);
					
					// the initial value or
					//
					if(pasm->psa->psym->init)
					{
						if(
								pasm->psa->psym->init->desc.isdim == 1
							&&	pasm->psa->desc.isdim == 1
						//	&&	pasm->psa->type->desc.bsize == 1
							&&	pasm->psa->psym->init->name[0] == '\"'
						)
						{
							fprintf(px->af, "\n\t.string\t%s\n", pasm->psa->psym->init->name);
						}
						else
						{
							fprintf(px->af, "\n\t%-6s\t%s\n", cname, pasm->psa->psym->init->name);
						}
					}
					else
					{
						// the size name
						//
						fprintf(px->af, "\n\t%-6s\t0\n", cname);
					}
				}
			}
		}
		break;

	case codeEXTERN:	// extern
		fprintf(px->af, "\t%-6s\t%s%s\n", ".extern", STR_GLOBAL_UNDSCORE, pasm->psa->name);
		break;
		
	case codeLOCAL:		// allocate stack space
		break;
		
	case codeEOF:
		 break;

	case codeCOMMENT:	// comment in psa
	case codeDEBUG:		// debug info in psa
		break;
		
	default:
		
		Log(logError, 0, "Internal - unhandled code\n");
		break;
	}
	return 0;
}

