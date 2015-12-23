#ifndef LOG_H_
#define LOG_H_ 1

typedef enum 
{
	logDebug,
	logInfo,
	logWarning,
	logError
}
LOGTYPE;

//***********************************************************************
extern void		Log			(LOGTYPE type, int level, char* format, ...);
extern void		SetLogLevel	(int level);
extern int		GetLogLevel	();

#endif

