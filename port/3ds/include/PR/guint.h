/* 3DS port copy of include/PR/guint.h: angle-bracket-free includes so the
 * port PR/gu.h (s15.16 Mtx, real LookAt/Hilite) wins over upstream gu.h. */
/**************************************************************************
 *									  *
 *		 Copyright (C) 1994, Silicon Graphics, Inc.		  *
 *									  *
 *  These coded instructions, statements, and computer programs  contain  *
 *  unpublished  proprietary  information of Silicon Graphics, Inc., and  *
 *  are protected by Federal copyright law.  They  may  not be disclosed  *
 *  to  third  parties  or copied or duplicated in any form, in whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *									  *
 **************************************************************************/

#include "PR/mbi.h"
#include "PR/gu.h"

typedef union
{
	struct
	{
		unsigned int hi;
		unsigned int lo;
	} word;

	double	d;
} du;

typedef union
{
	unsigned int	i;
	float		f;
} fu;

#ifndef __GL_GL_H__

typedef	float	Matrix[4][4];

#endif

#define ROUND(d)	(int)(((d) >= 0.0) ? ((d) + 0.5) : ((d) - 0.5))
#define	ABS(d)		((d) > 0) ? (d) : -(d)

extern float	__libm_qnan_f;

/* 3DS port: upstream gets these from libultra's os.h/gu.h chain. */
float sqrtf(float);
signed short sins(unsigned short);
signed short coss(unsigned short);
