#ifndef CPUi686_H_
#define CPUi686_H_ 1

/* Architecture type.  The expression analysis can generate code
*  that uses "accumulator" or "register" semantics depending on
*  what type of machine you are compiling for.
*
*  The accumulator method depends on a single register of rACC
*  type and AT LEAST one other general purpose register.  The
*  GPR must be loadable from memory, swappable with the the ACC
*  but that's about it.
*
*  The register method assumes all registers are GPR (general 
*  purpose) but you can still mark one as rACC to use it as
*  the "primary" register, which can help if your ABI has a
*  specific register it needs for function returns, etc.
*/
/* define this to 1 to use accumulator-only semantics
*/
//#define CPU_ARCHITECTURE_ACC	1
#define CPU_ARCHITECTURE_ACC	0

/* Some CPUs (RISCs) need all operands in registers except
*  load source / store dest.  define this to one to
*  force all operands into registers before operations
*/
#define CPU_ARCHITECTURE_RISC	0

/* define this to 1 to add an underscore to global/extern symbols
*/
#define CPU_PREPEND_UNDERSCORE	0

/******************************************
*
* Machine Byte Size, Short size, and Int size, in bits
*/
#define CPU_CHAR_SIZE		8
#define CPU_SHORT_SIZE		16
#define CPU_INT_SIZE		32

/*
 * larger integer size
 */
#define CPU_LONG_SIZE		32
#define CPU_LONG_LONG_SIZE	64

/*
 * floating point size
 */
#define CPU_FLOAT_SIZE		32
#define CPU_DOUBLE_SIZE		64

/*
 * Size of an Address
 */
#define CPU_ADDRESS_SIZE	32

/*
 * Aligment of structure members in bits
 */
#define CPU_PACK_ALIGN		32

/*
 * Aligment of function arguments in bits
 */
#define CPU_ARG_ALIGN		32

/******************************************
*
* Machine Registers and Names
*/
//#define CPU_NUM_REGS		2
#define CPU_NUM_REGS		4

extern REGREC	g_gpregs[];
extern REGREC	g_fpreg;
extern REGREC	g_spreg;

/* the (canonical index in reg names) of register which is used as 
 * function return value holder
 */
#define CPU_FUNCTION_RET_REGISTER	0

/* the (canonical index in reg names) of register which is the
 * accumulator to be used for expressions.
 */
#define CPU_ACCUMULATOR_REGISTER	0

/* the list of (index in reg names) of registers which are used to
 * pass function arguments, in order of used, e.g. a CPU which
 * used R0,R1,R2 for arguments 1,2,3 might use { 0,1,2 } here.
 * a { -1 } indicates no registers are specific to arg passing
 */
#define CPU_FUNCTION_ARG_REGISTERS { -1 }
 
/* the list of (index in reg names) of registers which can
 * freely be modified in a function, or { -1 } if all can be
 */
#define CPU_SCRATCH_REGISTERS	{ 0, 1, 2, 3 }

/* the enumeration of opcodes for this cpu
*/
typedef enum
{
	iUNDEF,
	iMOV,
	iMOVAT,
	iXCHG,
	iLEA,
	iMOVSBL,
	iMOVSWL,
	iMOVZBL,
	iMOVZWL,
	iADD,
	iADDOFF,
	iSUB,
	iOR,
	iXOR,
	iAND,
	iNOT,
	iNEG,
	iSAL,
	iSAR,
	iSLL,
	iSLR,
	iCMP,
	iTSTB,
	iSETE,
	iSETNE,
	iSETLT,
	iSETLE, 
	iSETGT,
	iSETGE,
	iINC,
	iDEC,
	iMUL,
	iDIV,
	iPUSH,
	iPOP,
	iTSTOR,
	iTLOAD,
	iJMP,
	iJEQ,
	iJNE,
	iCALL,
	iRET
}
OPCODE;

extern PREGREC	CODEgetRegister		(int index);
extern int		CODEgenProlog		(PCCTX px, int first);
extern int		CODEoptimize		(PCCTX px, PFUNCTION pf);
extern int		CODEoutput			(PCCTX px, PFUNCTION pf);
extern const char* CODEopStr		(OPCODE opcode, int opsize);
extern int		CODEgenEpilog		(PCCTX px, int last);
					  
/**** function which generates a string of assembly given a code type
*/
extern PASM CODEgenerate		(
								PCCTX px,
								PFUNCTION pf,
								CODETYPE type,
								OPTYPE   op,
								PREGREC ra,  PREGREC rb,
								PSYMREF psa, PSYMREF psb
								);
#endif
