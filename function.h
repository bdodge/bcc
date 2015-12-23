#ifndef FUNCTION_H_
#define FUNCTION_H_ 1

typedef enum
{
		codeINSN,
		codeMOVE,
		codeARG,
		codeSTORE,
		codeTSTORE,
		codeLOAD,
		codeLOADADDR,
		codeTLOAD,
		codeSWAP,
		codeBR,
		codeBEQ,
		codeBNE,
		codeLABEL,
		codePUBLIC,
		codeEXTERN,
		codeLOCAL,
		codeGLOBAL,
		codeEOF,
		codeCOMMENT,
		codeDEBUG
}
CODETYPE;

// local storage entry
//
typedef struct loc_store
{
	unsigned long		 offset;
	PSYMREF		         psymr;
	struct loc_store*    next;
}
SLOCAL, *PSLOCAL;

typedef struct reg_score *_PREG;

typedef struct tag_iand_list
{
	// stacked operand
	PSLOCAL		ploc;
	
	// OR register operand
	_PREG		preg;
	
	struct tag_iand_list* next;
}
ANDITEM, *PANDITEM;

typedef struct tag_func_body
{
	PSYM					pfuncsym;
	POPENTRY				pcode;
	POPENTRY				pendofcode;
	PASM					pemit;
	PASM				 	pendofemit;
	PSYMTAB				 	psymtab;
	unsigned int			localloc;
	unsigned int			tmpalloc;
	unsigned int			maxargs;
	struct tag_func_body*	next;
}
FUNCTION, *PFUNCTION;


// how deep can intermediate result stack get
//
#define MAX_EXPRESSION	128

// how long can an expression, in complete text form, be
#define MAX_EXPRESSION_NAME	64*MAX_TOKEN

#define EMIT_OPTIMIZE(v)	((v) <= (g_pctx ? g_pctx->optlevel : 0))

typedef struct tag_context *PCCTX;

//***********************************************************************
extern PFUNCTION	FUNCcreate			(PSYM name);
extern void			FUNCdestroy			(PFUNCTION pf);
extern int			FUNCaddop			(PFUNCTION pfunc, POPENTRY pop);
extern int			FUNCinsertop		(PFUNCTION pfunc, POPENTRY pafter, POPENTRY pop);
extern int			FUNCaddLabel		(PFUNCTION pfunc, char* label);
extern int			FUNCaddGoto			(PFUNCTION pfunc, OPERATOR condition, char* label);
extern int			FUNCenmemberAggrt	(PSYM psym);
extern int			FUNCemitLocals		(PFUNCTION pfunc);
extern int			FUNCemitGlobals		(PFUNCTION pfunc, PSYMTAB ptab);
extern int			FUNCdestroyCode		(POPENTRY pcode);
extern int			FUNCemitProlog		(PCCTX px, PFUNCTION pfunc, PSYMTAB ptypes);
extern int			FUNCemitCode		(PCCTX px, PFUNCTION pfunc);
extern int			FUNCemitEpilog		(PCCTX px, PFUNCTION pfunc);

extern const char*	FUNCcodename		(CODETYPE type);

extern void			EMITdumpCode		(POPENTRY pop, POPENTRY pm, int len);

extern int			AssembleCode		(PCCTX px);

#endif

