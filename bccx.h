#ifndef BCCX_H_
#define BCCX_H_ 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef MAX_PATH
	#define MAX_PATH	1024
#endif
#ifdef Windows
	#define snprintf _snprintf
#endif

typedef struct tag_asm *PASM;
//typedef struct tag_context *PCCTX;

#include "token.h"
#include "log.h"
#include "symtab.h"
#include "opstack.h"
#include "function.h"
#include "scope.h"
#include "cpu.h"
#include "regalloc.h"

extern char g_progname[];

#endif

