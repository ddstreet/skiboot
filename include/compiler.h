#ifndef __COMPILER_H
#define __COMPILER_H

#ifndef __ASSEMBLY__

#include <stddef.h>

/* Macros for various compiler bits and pieces */
#define __packed		__attribute__((packed))
#define __align(x)		__attribute__((__aligned__(x)))
#define __unused		__attribute__((unused))
#define __used			__attribute__((used))
#define __section(x)		__attribute__((__section__(x)))

#if 0 /* Provided by gcc stddef.h */
#define offsetof(type,m)	__builtin_offsetof(type,m)
#endif

/* Compiler barrier */
static inline void barrier(void)
{
	asm volatile("" : : : "memory");
}

#endif /* __ASSEMBLY__ */

/* Stringification macro */
#define __tostr(x)	#x
#define tostr(x)	__tostr(x)

#endif /* __COMPILER_H */
