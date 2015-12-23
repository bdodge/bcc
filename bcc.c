
#include "bccx.h"

// compiler context
//
static CCTX g_ctx;

// program name
//
char g_progname[MAX_PATH];

//**************************************************************************
int usage(char* progname)
{
	return fprintf(stderr,
			"\nUsage: %s [options] <filename>\n"
			"    -g                - include debug information.\n"
			"    -o <outfile>      - write to outfile, not stdout.\n"
			"    -O <level>        - optimization level, 0-3.\n"
			"    -v                - print version.\n"
			"    -V <level>        - set debug level 1-10.\n",
			progname
	);
}

//**************************************************************************
int version(char* progname)
{
	return fprintf(stderr,
			"\n%s version 1.0 - 2009\n"
			"C Compiler ------------ by Brian Dodge\n",
			progname
	);
}

//**************************************************************************
int main(int argc, char** argv)
{
	FILE* in;
	FILE* out;
	char* progname;
	char* poutfile;
	char* pinfile;
	char* parg;
	int   argo;
	int   iarg;
	int	  rv;
	
	progname = *argv;
	while(progname && (*progname == '.' || *progname == '/' || *progname == '\\'))
	{
		progname++;
	}
	if(argc < 2)
	{
		return usage(progname);
	}
	argv++;
	argc--;

	poutfile = NULL;
	pinfile  = NULL;

	// gross init of compiler context
	//
	memset(&g_ctx, 0, sizeof(g_ctx));
	g_ctx.optlevel   = 0;
	g_ctx.debuginfo  = 0;
	g_ctx.underscore_globals = CPU_PREPEND_UNDERSCORE ? 1 : 0;

	SetLogLevel(1);

	while(argc > 0)
	{
		if((*argv)[0] == '-')
		{
			char opt = (*argv)[1];
			
			if((*argv)[2])	{
				argo = 2;
			} else {
				argo = 0;
				argv++;
				argc--;
			}
			switch(opt)
			{
			case 'g':		// debug info
				g_ctx.debuginfo = 1;
				argv--;
				argc++;
				break;
			case 'o':		// out file
				if(argc < 0 || ! *argv)
					return usage(progname);
				poutfile = *argv + argo;
				break;
			case 'O':		// OPtimize
				parg = (argc >= 0 && argv) ? *argv + argo : NULL;
				if(parg && *parg && (*parg >= '0' && *parg <= '9'))
				{
					iarg = strtoul(parg, NULL, 0);
				}
				else
				{
					argv--;
					argc++;
					iarg = 1;
				}
				g_ctx.optlevel = iarg;
				break;
			case 'S':		// keep assembly file
				g_ctx.asmout = 1;
				break;
			case 'u':		// underscore globals
				g_ctx.underscore_globals ^= 1;
				argv--;
				argc++;
				break;
			case 'v':		// version
				version(progname);
				return(0);
			case 'V':		// log level
				parg = (argc >= 0 && argv) ? *argv + argo : NULL;
				if(parg && *parg)
					iarg = strtoul(parg, NULL, 0);
				else
					iarg = 5;
				SetLogLevel(iarg);
				break;
			default:
				fprintf(stderr, "%s - bad switch %c\n", progname, opt);
				return usage(progname);
			}
			argv++;
			argc--;
		}
		else
		{
			argc--;
			if(argc == 0)
			{
				pinfile = *argv++;
				break;
			}
			else
			{
				fprintf(stderr, "%s - bad parm %s\n", progname, *argv);
				return usage(progname);
			}
		}
	}
	strncpy(g_progname, progname, sizeof(g_progname));
		
	if(! pinfile)
	{
		return usage(progname);
	}
	in = fopen(pinfile, "r");
	if(! in)
	{
		return fprintf(stderr, "%s: can't open %s\n", progname, pinfile);
	}
	if(poutfile)
	{
		out = fopen(poutfile, "w");
	}
	else
	{
		poutfile = "stdout";
		out = stdout;
	}
	if(! out)
	{
		fclose(in);
		return fprintf(stderr, "%s: can't write %s\n", progname, poutfile);
	}

	/******************************************************************
	/*
	 * initalize rest of compiler context
	 */
	g_ctx.errs       = 0;
	g_ctx.pfunctions = NULL;
	g_ctx.pcurfunc   = NULL;
	g_ctx.pstack     = NULL;
	g_ctx.pclass	 = NULL;
	
	strncpy(g_ctx.file, pinfile, MAX_PATH-1);

	g_ctx.toterrs	= 0;
	g_ctx.warns		= 0;
	
	TokenInit(&g_ctx.token);
	
	/******************************************************************
	/*
	 * Parse the global scope of the source file
	 */
	rv = ParseScope(in, &g_ctx, 0);

	/******************************************************************
	/*
	 * Generate the code compiled into asm
	 */
	if(!rv)
	{
		rv = GenerateCode(&g_ctx);
	}

	/******************************************************************
	/*
	 * Generate the code compiled into asm
	 */
	if(!rv && ! g_ctx.asmout)
	{
		AssembleCode(&g_ctx);
	}

	/******************************************************************
	/*
	 * All set, clean up 
	 */
	if(g_ctx.toterrs || g_ctx.warns)
	{
		Log(logInfo, 0, "%s - %d Errors   %d Warnings\n", 
				g_ctx.file, g_ctx.toterrs, g_ctx.warns);
	}
	g_pctx = NULL;
	
	if(out != stdout)
	{
		fclose(out);
	}
	fclose(in);
	return rv;
}


