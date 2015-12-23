#ifndef OPSTACK_H_
#define OPSTACK_H_ 1

typedef enum { 
		opERATOR,
		opERAND,
		opRESULT,
		opDEXMUL,
		opMARKER,
		opARGUMENT,
		opLABEL,
		opGOTO,
		opALLOC
}
OPTYPE;

typedef struct tag_op_entry
{
	OPTYPE					type;
	OPERATOR				op;
	PSYM					psym;
	struct tag_op_entry*	next;
	struct tag_op_entry*	allocnext;
}
OPENTRY, *POPENTRY;

typedef struct tag_func_body* PFUNC;

#define OPisIncDec(op) ((op == opPREINC)||(op == opPREDEC)||(op == opPOSTINC)||(op == opPOSTDEC))

//***********************************************************************
extern POPENTRY		OPcreate			(OPTYPE type, OPERATOR op, PSYM psym);
extern void 		OPdestroy			(POPENTRY pop);

extern void			OPdumpStack			(POPENTRY* pstack, int dbglev, char* msg);

extern int			OPisPrefixUnary		(OPERATOR op);
extern int			OPisUnary			(OPERATOR op);
extern int			OPisCommutative		(OPERATOR op);
extern int			OPprecedence		(OPERATOR op);
extern int			OPassociativity		(OPERATOR op);
extern OPERATOR		OPencode			(char code);
extern const char*	OPname				(OPERATOR op);
extern void			OPdump				(const char* msg, int dbglev, POPENTRY pop);
extern int			OPisPromotion		(OPERATOR op);

extern POPENTRY		OPpush				(POPENTRY* pstack, POPENTRY pop);
extern POPENTRY		OPpushOpand			(POPENTRY* pstack, PSYM psym);
extern POPENTRY		OPpushOpandResult	(POPENTRY* pstack, PSYM psym, const char* newname);
extern POPENTRY		OPpushOper			(POPENTRY* pstack, OPERATOR op);
extern POPENTRY		OPpushMarker		(POPENTRY* pstack);
extern int			OPpullMarker		(POPENTRY* pstack);
extern POPENTRY		OPpushArgsList		(POPENTRY* pstack, int argn);
extern POPENTRY		OPpop				(POPENTRY* pstack);
extern POPENTRY		OPtop				(POPENTRY pstack);
extern int 			OPeval				(PFUNC pfunc, POPENTRY* pstack, OPERATOR inop);
extern int 			OPclean				(POPENTRY* pstack);

extern int			OPconstMathPossible	(OPERATOR op);
extern int			OPdotheMath			(PSYM pa, PSYM pb, POPENTRY po, char* result);

#endif

