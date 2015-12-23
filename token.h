
#define MAX_TOKEN 256
#define MAX_SYMLEN (MAX_TOKEN)

typedef enum
{
	tsInit,
	tsPlain,
	tsComment,
	tsSpanningComment,
	tsQuotedString,
	tsQuotedLiteral,
	tsNumber,
	tsNumber8,
	tsNumber10,
	tsNumber16,
	tsNumberFloat,
	tsNumberU,
	tsNumberL,
	tsNumberEnd,
	tsOperator,
	tsMacro
}
TOKENSTATE;

typedef enum
{
	kwLiteral,
	kwString,
	kwNumber,
	kwOperator,
	kwOperEquals,
	kwBuiltin_auto,
	kwBuiltin_asm,
	kwBuiltin_break,
	kwBuiltin_case,
	kwBuiltin_continue,
	kwBuiltin_do,
	kwBuiltin_default,
	kwBuiltin_else,
	kwBuiltin_entry,
	kwBuiltin_for,
	kwBuiltin_goto,
	kwBuiltin_if,
	kwBuiltin_switch,
	kwBuiltin_while,
	kwBuiltin_catch,
	kwBuiltin_delete,
	kwBuiltin_false,
	kwBuiltin_new,
	kwBuiltin_template,
	kwBuiltin_this,
	kwBuiltin_throw,
	kwBuiltin_true,
	kwBuiltin_try,
	kwBuiltinType,
	kwBuiltinTypeMod,
	kwTypeDef,
	kwPlain,
	kwComma,
	kwColon,
	kwStatement,
	kwLparen,
	kwRparen,
	kwLindice,
	kwLbrace,
	kwRbrace
}
KEYWORD;

typedef enum
{
	// C operators
	//
	opFORCE		= 'F',
	opSTATEMENT	= ';',
	opRETURN	= ')',
	opCOMMA		= ',',
	opEQUAL		= '=',
	opTERNARY	= '?',
	opBOOLOR	= 'O',
	opBOOLAND	= 'A',
	opBITOR		= '|',
	opBITXOR	= '^',
	opBITAND	= '&',
	opBOOLEQ	= 'Q',
	opBOOLNEQ	= 'N',
	opBOOLLT	= '<',
	opBOOLGT	= '>',
	opBOOLLTEQ	= 't',
	opBOOLGTEQ	= 'g',
	opSHIFTL	= 'L',
	opSHIFTR	= 'R',
	opADD		= '+',
	opMINUS		= '-',
	opMUL		= '*',
	opDIV		= '/',
	opMOD		= '%',
	opBOOLNOT	= '!',
	opBITINVERT	= '~',
	opPREINC	= 'i',
	opPREDEC	= 'd',
	opPOSTINC	= 'I',
	opPOSTDEC	= 'D',
	opNEGATE	= 'n',
	opCAST		= 'y',
	opDEREF		= '$',
	opADDROF	= '@',
	opSIZEOF	= 'z',
	opCALL		= '(',
	opINDEX		= '[',
	opOFFSET	= 'o',
	opSDEREF	= '.',
	opPDEREF	= ':',
	opPROMOTE2UNSIGNED	= '0',
	opPROMOTECHAR2INT	= '1',
	opPROMOTESHORT2INT	= '3',
	opPROMOTEINT2LONG	= '5',
	opPROMOTELONG2LONGLONG	= '7',
	opPROMOTEUCHAR2UINT		= '2',
	opPROMOTEUSHORT2UINT	= '4',
	opPROMOTEUINT2ULONG		= '6',
	opPROMOTEULONG2ULONGLONG= '8',
	opPROMOTEFLT2DBL	= '9',
	opTEST		= 'V',
	opNONE		= '_'
}
OPERATOR, *POPERATOR;

typedef struct tag_token
{
	char		text[MAX_TOKEN];
	KEYWORD		type;
	TOKENSTATE	state;
	int			len;
	int			pushedchar;
	int			ungotchar;
	int			eof;
	int			line;
	int			col;
}
TOKEN, *PTOKEN;


//***********************************************************************
// token
void		TokenInit			(PTOKEN ptok);
int			TokenGet			(FILE* inf, PTOKEN ptok, int cppok);
int			TokenCheckBuiltin	(PTOKEN ptok, int cppok);


