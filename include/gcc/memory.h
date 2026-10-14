#ifndef _MEMORY_H
#define _MEMORY_H
/*
	memory.h
*/

#ifndef _SIZE_T_DEF
#define _SIZE_T_DEF
typedef unsigned size_t;
#endif

void *memccpy(void *,const void *,int,size_t);
void *memchr(const void *,int,size_t);
int  memcmp(const void *,const void *,size_t);
void *memcpy(void *,const void *,size_t);
int  memicmp(void *,void *,size_t);
void *memmove(void *,const void *,size_t);
void *memset(void *,int,size_t);

void movmem(void *,void *,unsigned);
void setmem(void *,unsigned,int);

#endif
