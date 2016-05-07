
#ifndef COMMON_H
#define COMMON_H

#ifdef __cplusplus
# define C_NAMESPACE_BEGIN	extern "C" {
# define C_NAMESPACE_END	}
# include <cstdio>
#else
# define C_NAMESPACE_BEGIN
# define C_NAMESPACE_END
# include <stdio.h>
#endif

#define DIE(code,...) do { fprintf(stderr, __VA_ARGS__); exit(code); } while (0)
#define ARRAY_SIZE(arr)	(sizeof(arr)/sizeof(*(arr)))
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#define MAX(a,b)	((a) > (b) ? (a) : (b))

#endif
