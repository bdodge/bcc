#ifndef CPU_H_
#define CPU_H_ 1

/* register type
*/
typedef enum
{
	rACC	=	1,		// accumulator
	rGPR	=	2,		// general purpose register
	rIDX	=	4,		// index
	rFP		=	8,		// frame pointer
	rSP		=	16,		// stack pointer
	rMR		=	32		// machine status register
}
REGTYPE;

/* a description of a cpu register
*/
typedef struct tag_regrec
{
	REGTYPE			type;
	const char*		name;
	
	/* register names while storing 8,16,32, and 64 bytes
	 * use NULL if register can't hold size
	 */
	const char*		name8;
	const char*		name16;
	const char*		name32;
	const char*		name64;
}
REGREC, *PREGREC;

/* include generated (copied) cpu specific defintions
*/
#include "cpuspec.h"

#define CPU_ALIGN_BYTES (((CPU_PACK_ALIGN) + 7) / 8)

/* prepend this character(s) on all generated labels.  this is used to
 * avoid compiler generated labels interfereing with user code
 * so pick something that asm can handle but C can't.  default is here
 */
#ifndef CPU_LABEL_PREP
	#define CPU_LABEL_PREP "."
#endif

/* an entry in a generated assembly language listing
 * now that OPCODE is defined
*/
typedef struct tag_asm
{
	CODETYPE	type;				// what type of code (instruction, label, declaration, etc.)
	OPCODE		opcode;				// the opcode for instructions
	PREGREC		pra, prb;			// primary and (secondary) register
	PSYMREF		psa, psb;			// reference of symbol in ra, and rb, or, operand2 if prb NULL
	struct	tag_asm *next, *prev;
}
ASM;

/* an entry in a "program" to auto-generate pasm sequences
*  from  canned info
*/
typedef struct tag_asmprog
{
	CODETYPE	type;				// what type of code (instruction, label, declaration, etc.)
	OPCODE		opcode;				// the opcode for instructions
	int			ra,  rb;			// canonical index of register to use
	const char* psa, psb;			// text to create simple symbols with
}
ASMPROG, *PASMPROG;

/* the entries in a pasm program can be
* 
*  ra and rb - cab be a canonical index OR -2 == pra or -1 = prb
*
*  psa and psb can be a string, a number, or a "%a" or "%b" for psa or psb
*/

/**** functions in cpu/generic/cpu_generic.c
*/
extern PASM PASMcreate			(
								CODETYPE type,
								OPCODE   opcode,
								PREGREC ra,  PREGREC rb,
								PSYMREF psa, PSYMREF psb
								);
extern void PASMdestroy			(PASM pasm);
extern PASM PASMprogram			(PASMPROG pprogram, PASM pasm);

extern int	GENERICgenProlog	(PCCTX px, int first);
extern int	GENERICgenEpilog	(PCCTX px, int last);

extern PASM GENERICgenerate		(
								PCCTX 		px,
								PFUNCTION	pf,
								PASM		pasm
								);
extern int	GENERICoutput		(
								PCCTX 		px,
								PFUNCTION	pf,
								PASM		pasm
								);

#endif

