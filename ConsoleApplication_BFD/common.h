#ifndef _COMMON_H
#define _COMMON_H
#include "windows.h"
#include "bfd.h"

#define EXIT() {\
				printf_s("\n��������������,����: %s",__FUNCTION__);\
				printf_s("\n��%s �� %d ��",__FILE__,__LINE__);\
				printf_s("\n�������");\
				printf_s("\n");\
				system("Pause");\
				exit(42);\
					}

#define MAX(x,y)	(((x)>(y))?(x):(y))


#define BFD_PORT		(3784)

#define SET				(1)
#define RESET			(0)

/* free and zero (to avoid double-free) */
#define free_z(p) do { if (p) { free(p); (p) = 0; } } while (0)

#endif