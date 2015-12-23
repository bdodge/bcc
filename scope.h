#ifndef SCOPE_H_
#define SCOPE_H_ 1

// a context record of a construct (loop, or switch)
//
typedef struct tag_sprec
{
	KEYWORD		type;
	int			line;
	int			labgen;
	int			didelse;
	int			diddef;
	int			incase;
	PSYM		lvar;
	POPENTRY	pcase, pswitch;
	struct tag_sprec* next;
}
SPREC, *PSPREC;

// a context of a function argument list
//
typedef struct tag_funcrec
{
	int			line;
	int			argn;
	POPENTRY	pmarker;
	POPENTRY	postcode;
	struct tag_funcrec* next;
}
FNREC, *PFNREC;

typedef struct tag_context
{
	TOKEN		token;
	int			retok;
	int			optlevel;
	int			underscore_globals;
	int			debuginfo;
	int			asmout;
	int			level;
	int			errs;
	int			toterrs;
	int			warns;
	char		file[MAX_PATH];
	char		asmf[MAX_PATH];
	FILE*		af;
	SYMTYPE		desc;
	int			cppok;
	int			typing;
	int			booling;
	int			funcing;
	int			enumerating;
	int			initing;
	int			aggrting;
	PSPREC		psp;
	POPENTRY	pstack;
	PFUNCTION	pcurfunc, pfunctions;
	PFNREC		pargrec;
	PSYM		psym, ptype, pfunc;
	PSYMSTACK	pclass;
	PSYMTAB		psymtab,	psymlist;
}
CCTX;

//typedef CCTX *PCCTX;

extern PCCTX g_pctx;

#define MAX_ERRS	20
#define ERRMAX		(px->toterrs > MAX_ERRS)

extern PSYM g_one, g_zero;

//***********************************************************************
extern PSYM		GetIntTypeForSize	(unsigned int bitsize);
extern PSYM		GetRealTypeForSize	(unsigned int bitsize);
extern int		ParseScope			(FILE* in, PCCTX px, int level);
extern int		GenerateCode		(PCCTX px);


#endif
