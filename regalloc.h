#ifndef REGALLOC_H_
#define REGALLOC_H_ 1


typedef enum { rsFREE, rsLOADED, rsSAVED, rsSTACKED, rsRESERVED } REGSTATE;

// register entry
//
typedef struct reg_score
{
	PREGREC				pr;			// register record
	REGSTATE			state;		// what the reg is used for
	PSYMREF				psymr;		// ref to sym loaded in register
	struct reg_score*	next;		// link
}
REG, *PREG;

//***********************************************************************
int			REGprolog			(void);
void		REGclear			(PANDITEM pandstack, int clearall);
void		REGdumpScore		(PANDITEM pandstack);
PREG		REGalloc			(PCCTX px, PFUNCTION pfunc, PANDITEM pandstack);
void		REGfree				(PANDITEM pandstack, PREG preg);
void		REGfreeSaved		(PREG preg);
void		REGfreeLocal		(PSLOCAL ploc);
void		REGfreeLoaded		(PANDITEM pandstack, PREG preg);
PREG		REGsaveAddr			(PCCTX px, PFUNCTION pfunc, PANDITEM pandstack, POPENTRY pop, PREG pregitsin);
PREG		REGloadAcc			(PCCTX px, PFUNCTION pfunc, PANDITEM pandstack, POPENTRY popand, int loadaddress);
PREG		REGforOpand			(PCCTX px, PFUNCTION pfunc, PANDITEM pandstack, POPENTRY popand, int loadaddress);
PREGREC		REGreserve			(PCCTX px, PFUNCTION pfunc, PREGREC pregrec);
int			REGunreserve		(PCCTX px, PFUNCTION pfunc, PREGREC pregrec);
PSLOCAL		LOCALalloc			(PFUNCTION pfunc, PSYMREF psymr);
void		LOCALfree			(PSLOCAL ptmp);

#endif

