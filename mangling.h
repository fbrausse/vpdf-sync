
#ifndef MANGLING_H
#define MANGLING_H

#if defined(__APPLE__) && defined(__MACH__)
# define ASM_C_FUNC(name)	_ ## name
#else
# define ASM_C_FUNC(name)	name
#endif

#endif
