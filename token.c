
#include "bccx.h"


//***********************************************************************
void TokenInit(PTOKEN ptok)
{
	ptok->text[0]	 = '\0';
	ptok->type 		 = kwPlain;
	ptok->len  		 = 0;
	ptok->state		 = tsInit;
	ptok->pushedchar = 0;
	ptok->ungotchar  = 0;
	ptok->line		 = 1;
	ptok->col		 = 1;
	ptok->eof   	 = 0;
}

//***********************************************************************
static int TokenInsert(int ic, PTOKEN ptok)
{
	if(ptok->len < MAX_TOKEN - 1)
	{
		ptok->text[ptok->len++] = ic;
		ptok->text[ptok->len] = '\0';
		return 0;
	}
	else
	{
		Log(logError, 0, "Token overflow %s\n", ptok->text);
		return 1;
	}
}

//***********************************************************************
static int TokenNextChar(FILE* inf, PTOKEN ptok)
{
	int ic;
	
	if(ptok->ungotchar)
	{
		ic = ptok->ungotchar;
		ptok->ungotchar = 0;
	}
	else if(ptok->pushedchar)
	{
		ic = ptok->pushedchar;
		ptok->pushedchar = 0;
	}
	else
	{
		ic = fgetc(inf);
	}
	if(ic == EOF)
	{
		ptok->eof = 1;
	}
	if(ic == '\n')
	{
		ptok->line++;
		ptok->col = 1;
	}
	else
	{
		ptok->col++;
	}
	return ic;
}
	
//***********************************************************************
int TokenPushChar(PTOKEN ptok, int c)
{
	if(ptok->pushedchar)
	{
		Log(logError, 4, "Internal: bad parse state %c\n", c);
		return 1;
	}
	ptok->pushedchar = (char)c;
	if(c == '\n')
		ptok->line--;
	else
		ptok->col--;
	return 0;
}

//***********************************************************************
int TokenGet(FILE* inf, PTOKEN ptok, int cppok)
{
	int  gotToken	 = 0;
	int  ic, syntaxerr;
	char* syntaxstr;
	
	ptok->len  = 0;
	ptok->text[0] = '\0';
	ptok->type = kwPlain;
	
	while(! gotToken)
	{
		Log(logDebug, 14, "ts=%d l,c=%d,%d tk=%s\n",
				ptok->state, ptok->line, ptok->col, ptok->text);

		ic = TokenNextChar(inf, ptok);
		if(ic == EOF || ptok->eof)
		{
			gotToken = 1;
			break;
		}
		switch(ptok->state)
		{
		case tsInit:
			
			switch(ic)
			{
			case ' ': case '\t': case '\r': case '\n':
				break;
				
			case ';':
				ptok->type = kwStatement;
				TokenInsert(ic, ptok);
				gotToken = 1;
				break;
				
			case ':':
				ptok->type = kwColon;
				TokenInsert(ic, ptok);
				gotToken = 1;
				break;
				
			case ',':
				ptok->type = kwComma;
				TokenInsert(ic, ptok);
				gotToken = 1;
				break;
				
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
			case '8': case '9':
				ptok->type = kwNumber;
				TokenInsert(ic, ptok);
				ptok->state = tsNumber;
				break;
				
			case '(':
				ptok->type = kwLparen;
				TokenInsert(ic, ptok);
				gotToken = 1;
				break;
				
			case ')':
				ptok->type = kwRparen;
				TokenInsert(ic, ptok);
				gotToken = 1;
				break;
				
			case '{':
				ptok->type = kwLbrace;
				TokenInsert(ic, ptok);
				gotToken = 1;
				break;
				
			case '}':
				ptok->type = kwRbrace;
				TokenInsert(ic, ptok);
				gotToken = 1;
				break;
				
			case '[':
				ptok->type = kwLindice;
				TokenInsert(ic, ptok);
				gotToken = 1;
				break;
				
			case ']':
				ptok->type = kwOperator;
				TokenInsert(ic, ptok);
				gotToken = 1;
				break;
				
			case '\"':
				ptok->state = tsQuotedString;
				ptok->type  = kwString;
				TokenInsert(ic, ptok);
				break;
			
			case '\'':
				ptok->state = tsQuotedLiteral;
				ptok->type  = kwLiteral;
				TokenInsert(ic, ptok);
				break;
				
			case '#':
				ptok->state = tsMacro;
				break;
				
			case '+': case '-': case '*': case '%':
			case '&': case '|': case '^': case '~':
			case '=': case '.': case '<': case '>':
			case '!': case '?':
				ptok->state = tsOperator;
				ptok->type  = kwOperator;
				TokenInsert(ic, ptok);
				break;
				
			case '/':
				ic = TokenNextChar(inf, ptok);
				if(ic == '/')
				{
					ptok->state = tsComment;
				}
				else if(ic == '*')
				{
					ptok->state = tsSpanningComment;
				}
				else
				{
					ptok->state = tsOperator;
					ptok->type  = kwOperator;
					TokenInsert('/', ptok);
					TokenPushChar(ptok, ic);
				}
				break;
				
			default:
				ptok->state = tsPlain;
				ptok->type  = kwPlain;
				TokenInsert(ic, ptok);
				break;
			}
			break;
			
		case tsNumber:
		case tsNumber8:
		case tsNumber10:
		case tsNumber16:
		case tsNumberFloat:
		case tsNumberU:
		case tsNumberL:
		case tsNumberEnd:
			syntaxstr = NULL;
			switch(ic)
			{
			case ' ': case '\t': case '\n': case '\r':
				gotToken = 1;
				break;
				
			case '+': case '-': case '*': case '%':
			case '&': case '|': case '^': case '~':
			case '=': case '<': case '>': case '?': case ':':
			case '(': case ')': case '{': case '}': case '[': case ']':
			case '!': case ',': case ';': case '/':
				gotToken = 1;
				TokenPushChar(ptok, ic);
				break;
			
			case '.': 
				if(ptok->state < tsNumber16 && ptok->len > 0)
				{
					TokenInsert(ic, ptok);
					ptok->state = tsNumberFloat;
				}
				else
				{
					syntaxstr = "Bad char %c in constant\n";
				}
				break;
				
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6':
			case '7': case '8': case '9':
				if(ptok->state == tsNumber)
				{
					if(ptok->text[0] == '0')
					{
						ptok->state = tsNumber8;
					}
					else
					{
						ptok->state = tsNumber10;
					}
				}
				else
				{
					if(ptok->state > tsNumberFloat)
					{
						syntaxstr = "Bad digit %c after number\n";
						break;
					}
					else if(ptok->state == tsNumber8 && ic >= '7')
					{
						syntaxstr = "Bad digit %c in octal constant\n";
						break;
					}
				}
				TokenInsert(ic, ptok);
				break;
			
			case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
			case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
				if(ptok->state != tsNumber16)
				{
					syntaxstr = "Bad char %c in non-hex constant\n";
					break;
				}
				TokenInsert(ic, ptok);
				break;
				
			case 'x':
				if(
						ptok->state < tsNumber16
					&&	 ptok->len == 1
					&&	ptok->text[0] == '0'
				)
				{
					ptok->state = tsNumber16;
					TokenInsert(ic, ptok);
				}
				else
				{
					syntaxstr = "Bad char %c in non-hex constant\n";
					break;
				}
				break;
				
			case 'U':
				if(strchr(ptok->text, 'U'))
				{
					syntaxstr = "Bad second %c in constant\n";
					break;
				}
				TokenInsert(ic, ptok);
				ptok->state = tsNumberEnd;
				break;
				
			case 'L':
				if(strstr(ptok->text, "LL"))
				{
					syntaxstr = "More than 2 %c in a constant\n";
					break;
				}
				TokenInsert(ic, ptok);
				ptok->state = tsNumberEnd;
				break;
				
			default:
				syntaxstr = "Stray char %c in constant\n";
				break;
			}
			if(syntaxstr)
			{
				Log(logError, 0, syntaxstr, ic);
				// let the token parser get a space at least
				break;
			}
			break;
				
		case tsComment:
			
			if(ic == '\n')
			{
				ptok->state = tsInit;
			}
			break;
			
		case tsSpanningComment:
			
			if(ic == '*')
			{
				ic = TokenNextChar(inf, ptok);
				if(ic == '/')
				{
					ptok->state = tsInit;
				}
				else
				{
					TokenPushChar(ptok, ic);
				}
			}
			break;
			
		case tsMacro:
			
			if(ic == '\n')
			{
				ptok->state = tsInit;
			}
			break;
			
		case tsQuotedString:
			
			if(ic == '\\')
			{
				TokenInsert(ic, ptok);
				ic = TokenNextChar(inf, ptok);
				if(ic != EOF)
				{
					TokenInsert(ic, ptok);
				}
			}
			else if(ic == '\"')
			{
				TokenInsert(ic, ptok);
				gotToken = 1;
			}
			else
			{
				TokenInsert(ic, ptok);
			}
			break;
			
		case tsQuotedLiteral:

			if(ic == '\\')
			{
				if(ptok->len > 1)
				{
					Log(logError, 0, "Literal syntax %s\\\n", ptok->text);
					return 1;
				}
				TokenInsert(ic, ptok);
				ic = TokenNextChar(inf, ptok);
				if(ic != EOF)
				{
					TokenInsert(ic, ptok);
				}
			}
			else if(ic == '\'')
			{
				TokenInsert(ic, ptok);
				gotToken = 1;
			}
			else
			{
				TokenInsert(ic, ptok);
			}
			break;
			
		case tsOperator:
			
			syntaxerr = 0;
			switch(ic)
			{
			case ' ': case '\t': case '\r': case '\n':
				gotToken = 1;
				break;

			case '&':
				if(ptok->text[0] == '&')
				{
					gotToken = 1;
					ptok->text[0] = (char)opBOOLAND;
				}
				else
				{
					syntaxerr = 1;
				}
				break;

			case '|':
				if(ptok->text[0] == '|')
				{
					gotToken = 1;
					ptok->text[0] = (char)opBOOLOR;
				}
				else
				{
					syntaxerr = 1;
				}
				break;

			case '=':
				switch(ptok->text[0])
				{
				case '=':
					gotToken = 1;
					ptok->text[0] = (char)opBOOLEQ;
					break;
				case '!': 
					gotToken = 1;
					ptok->text[0] = (char)opBOOLNEQ;
					break;
				case '+': case '-': case '*': case '/': case '%':
				case '&': case '|': case '^': case '~':
					gotToken = 1;
					ptok->type = kwOperEquals;
					break;
				case '<':
					gotToken = 1;
					ptok->text[0] = (char)opBOOLLTEQ;
					break;
				case '>':
					gotToken = 1;
					ptok->text[0] = (char)opBOOLGTEQ;
					break;
				default:
					syntaxerr = 1;
					break;
				}
				break;

			case '+':
				gotToken = 1;
				switch(ptok->text[0])
				{
				case '+':
					ptok->text[0] = (char)opPREINC;	// scope might change this to postinc
					break;
				default:
					ptok->pushedchar = ic;
					break;
				}
				break;

			case '-':
				gotToken = 1;
				switch(ptok->text[0])
				{
				case '-':
					ptok->text[0] = (char)opPREDEC;	// scope might change this to postdec
					ptok->type = kwOperator;
					break;
				default:
					ptok->pushedchar = ic;
					break;
				}
				break;

			case '>':
				if(ptok->text[0] == '>')
				{
					gotToken = 1;
					ptok->text[0] = (char)opSHIFTR;
					ic = TokenNextChar(inf, ptok);
					if(ic == '=')
					{
						ptok->type = kwOperEquals;
					}
					else
					{
						ptok->pushedchar = ic;
					}
				}
				else if(ptok->text[0] == '-')
				{
					gotToken = 1;
					ptok->text[0] = (char)opPDEREF;
				}
				else
				{
					syntaxerr = 1;
				}
				break;

			case '<':
				if(ptok->text[0] = '<')
				{
					gotToken = 1;
					ptok->text[0] = (char)opSHIFTL;
					ic = TokenNextChar(inf, ptok);
					if(ic == '=')
					{
						ptok->type = kwOperEquals;
					}
					else
					{
						TokenPushChar(ptok, ic);
					}
				}
				else
				{
					syntaxerr = 1;
				}
				break;
				
			default:
				gotToken = 1;
				TokenPushChar(ptok, ic);
				break;
			}
			if(syntaxerr)
			{
				Log(logError, 0, "Syntax error %s\n", ptok->text);
				return 1;
			}
			break;
			
		case tsPlain:
			
			switch(ic)
			{
			case ' ': case '\t': case '\n': case '\r':
				gotToken = 1;
				break;
				
			case '\"':
			case '\'':
				break;
				
			case '+': case '-': case '*': case '%':
			case '&': case '|': case '^': case '~':
			case '=': case '.': case '<': case '>':
			case '(': case ')': case '{': case '}': case '[': case ']':
			case '!': case ',': case ';': case '/': case '?': case ':':
				gotToken = 1;
				TokenPushChar(ptok, ic);
				ptok->state = tsInit;
				break;
				
			default:
				TokenInsert(ic, ptok);
				break;
			}
		}
	}
	ptok->state = tsInit;
	return 0;	
}

int TokenCheckBuiltinCPP(PTOKEN ptok)
{
	if(! strcmp(ptok->text, "class"))        return kwBuiltinTypeMod;
	if(! strcmp(ptok->text, "catch"))        return kwBuiltin_catch;
	if(! strcmp(ptok->text, "delete"))       return kwBuiltin_delete;
	if(! strcmp(ptok->text, "false"))        return kwBuiltin_false;
	if(! strcmp(ptok->text, "friend"))       return kwBuiltinTypeMod;
	if(! strcmp(ptok->text, "inline"))       return kwBuiltinTypeMod;
	if(! strcmp(ptok->text, "new"))          return kwBuiltin_new;
	if(! strcmp(ptok->text, "operator"))     return kwBuiltinTypeMod;
	if(! strcmp(ptok->text, "public"))       return kwBuiltinTypeMod;
	if(! strcmp(ptok->text, "private"))      return kwBuiltinTypeMod;
	if(! strcmp(ptok->text, "protected"))    return kwBuiltinTypeMod;
	if(! strcmp(ptok->text, "template"))     return kwBuiltin_template;
	if(! strcmp(ptok->text, "this"))         return kwBuiltin_this;
	if(! strcmp(ptok->text, "throw"))        return kwBuiltin_throw;
	if(! strcmp(ptok->text, "true"))         return kwBuiltin_true;
	if(! strcmp(ptok->text, "try"))          return kwBuiltin_try;
	if(! strcmp(ptok->text, "virtual"))      return kwBuiltinTypeMod;
	return ptok->type;
}

//***********************************************************************
int TokenCheckBuiltin(PTOKEN ptok, int cppok)
{
	switch(ptok->text[0])
	{
	case 'a':
		if(! strcmp(ptok->text, "auto"))     return kwBuiltinTypeMod;
		if(! strcmp(ptok->text, "asm"))      return kwBuiltin_asm;
		break;
	case 'b':
		if(! strcmp(ptok->text, "break"))    return kwBuiltin_break;
		break;
	case 'c':
		if(! strcmp(ptok->text, "const"))    return kwBuiltinTypeMod;
		if(! strcmp(ptok->text, "char"))     return kwBuiltinType;
		if(! strcmp(ptok->text, "case"))     return kwBuiltin_case;
		if(! strcmp(ptok->text, "continue")) return kwBuiltin_continue;
		break;
	case 'd':
		if(! strcmp(ptok->text, "do"))       return kwBuiltin_do;
		if(! strcmp(ptok->text, "default"))  return kwBuiltin_default;
		break;
	case 'e':
		if(! strcmp(ptok->text, "extern"))   return kwBuiltinTypeMod;
		if(! strcmp(ptok->text, "else"))     return kwBuiltin_else;
		if(! strcmp(ptok->text, "enum"))     return kwBuiltinTypeMod;
		if(! strcmp(ptok->text, "extern"))   return kwBuiltinType;
		if(! strcmp(ptok->text, "entry"))    return kwBuiltin_entry;
		break;
	case 'f':
		if(! strcmp(ptok->text, "for"))      return kwBuiltin_for;
		break;
	case 'g':
		if(! strcmp(ptok->text, "goto"))     return kwBuiltin_goto;
		break;
	case 'i':
		if(! strcmp(ptok->text, "inline"))   return kwBuiltinTypeMod;
		if(! strcmp(ptok->text, "int"))      return kwBuiltinType;
		if(! strcmp(ptok->text, "if"))       return kwBuiltin_if;
		break;
	case 'l':
		if(! strcmp(ptok->text, "long"))     return kwBuiltinTypeMod;
		break;
	case 'r':
		if(! strcmp(ptok->text, "register")) return kwBuiltinTypeMod;
		if(! strcmp(ptok->text, "return"))   return kwOperator;
		break;
	case 's':
		if(! strcmp(ptok->text, "sizeof"))   return kwOperator;
		if(! strcmp(ptok->text, "static"))   return kwBuiltinTypeMod;
		if(! strcmp(ptok->text, "struct"))   return kwBuiltinTypeMod;
		if(! strcmp(ptok->text, "switch"))   return kwBuiltin_switch;
		if(! strcmp(ptok->text, "short"))    return kwBuiltinType;
		if(! strcmp(ptok->text, "signed"))   return kwBuiltinTypeMod;
		break;
	case 't':
		if(! strcmp(ptok->text, "typedef"))  return kwTypeDef;
		break;
	case 'u':
		if(! strcmp(ptok->text, "unsigned")) return kwBuiltinTypeMod;
		if(! strcmp(ptok->text, "union"))    return kwBuiltinTypeMod;
		break;
	case 'v':
		if(! strcmp(ptok->text, "void"))     return kwBuiltinType;
		if(! strcmp(ptok->text, "volatile")) return kwBuiltinTypeMod;
		break;
	case 'w':
		if(! strcmp(ptok->text, "while"))    return kwBuiltin_while;
		break;
	default:
		break;
	}
	if(cppok && ptok->type == kwPlain)
		return TokenCheckBuiltinCPP(ptok);
	else
		return ptok->type;
}

