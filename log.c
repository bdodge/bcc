
#include "bccx.h"

static int g_loglevel = 5;

//**************************************************************************
void Log(LOGTYPE type, int level, char* format, ...)
{
	va_list arg;
	static char	msg[4*MAX_TOKEN];
	static char	finfo[2*MAX_TOKEN];
	char* pmsg;
	int   msgz;
	char* pszType = "";

	if((strlen(format) + 3 * MAX_TOKEN) >= sizeof(msg))
	{
		msgz = strlen(format) + 4 * MAX_TOKEN;
		pmsg = (char*)malloc(msgz);
		if(! pmsg)
		{
			Log(logError, 0, "Internal: log memory\n");
			return;
		}			
	}
	else
	{
		msgz = sizeof(msg);
		pmsg = msg;
	}

	if(level > g_loglevel)
	{
		return;
	}
	va_start(arg, format);
#ifndef Solaris
#ifndef Windows
	vsnprintf(pmsg, msgz, format, arg);
#else
	_vsnprintf(pmsg, msgz, format, arg);
#endif
#else
	vsprintf(pmsg, format, arg);
#endif
	va_end(arg);


	switch (type)
	{
	case logDebug:
		pszType = "DEBUG  ";
		break;
	case logInfo:
		pszType = g_progname;
		break;
	case logWarning:
		if(g_pctx)
		{
			g_pctx->warns++;
			sprintf(finfo, "%s %d:%d Warning - ", g_pctx->file, g_pctx->token.line,
					g_pctx->token.col - g_pctx->token.len);
		}
		else
		{
			pszType = "Warning - ";
		}
		break;
	case logError:
		if(g_pctx)
		{
			g_pctx->errs++;
			g_pctx->toterrs++;
			sprintf(finfo, "%s %d:%d Error - ", g_pctx->file, g_pctx->token.line,
					g_pctx->token.col - g_pctx->token.len);
		}
		else
		{
			sprintf(finfo, "Error - ");
		}
		pszType = finfo;
		break;
	}
#ifdef xxWindows
	OutputDebugString(pszType);
	OutputDebugString(pmsg);
#else
	if(type == logError)
	{
		fprintf(stderr,  "%s %s", pszType, pmsg);
	}
	else
	{
		printf("%s %s", pszType, pmsg);
	}
	if(pmsg != msg)
		free(pmsg);
#endif
}

//**************************************************************************
void SetLogLevel(int level)
{
	if(level < 0) level = 0;
	if(level > 15) level = 15;
	g_loglevel = level;
}

//**************************************************************************
int GetLogLevel()
{
	return g_loglevel;
}



