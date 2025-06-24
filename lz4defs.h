#ifndef __LZ4DEFS_H__
#define __LZ4DEFS_H__
/*
 * lz4defs.h -- architecture specific defines
 *
 * Copyright (C) 2013, LG Electronics, Kyungsik Lee <kyungsik.lee@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Detects 64 bits mode
 */
#define CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
#define LZ4_MEM_COMPRESS	(16384)
#define LZ4HC_MEM_COMPRESS	(262144 + (2 * sizeof(unsigned char *)))

#ifdef __x86_64__
#define LZ4_ARCH64 1
#else
#define LZ4_ARCH64 0
#endif

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;

#define COPYLENGTH 8
#define ML_BITS  4
#define ML_MASK  ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)
#define MEMORY_USAGE	14
#define MINMATCH	4
#define SKIPSTRENGTH	6
#define LASTLITERALS	5
#define MFLIMIT		(COPYLENGTH + MINMATCH)
#define MINLENGTH	(MFLIMIT + 1)
#define MAXD_LOG	16
#define MAXD		(1 << MAXD_LOG)
#define MAXD_MASK	(u32)(MAXD - 1)
#define MAX_DISTANCE	(MAXD - 1)
#define HASH_LOG	(MAXD_LOG - 1)
#define HASHTABLESIZE	(1 << HASH_LOG)
#define MAX_NB_ATTEMPTS	256
#define OPTIMAL_ML	(int)((ML_MASK-1)+MINMATCH)
#define LZ4_64KLIMIT	((1<<16) + (MFLIMIT - 1))
#define HASHLOG64K	((MEMORY_USAGE - 2) + 1)
#define HASH64KTABLESIZE	(1U << HASHLOG64K)
#define LZ4_HASH_VALUE(p)	(((A32(p)) * 2654435761U) >> \
				((MINMATCH * 8) - (MEMORY_USAGE-2)))
#define LZ4_HASH64K_VALUE(p)	(((A32(p)) * 2654435761U) >> \
				((MINMATCH * 8) - HASHLOG64K))
#define HASH_VALUE(p)		(((A32(p)) * 2654435761U) >> \
				((MINMATCH * 8) - HASH_LOG))

enum {
       RAMDUMP_OLD_BL2Z_VER = 0,
       RAMDUMP_NEW_BL33Z_VER = 1,
};

int uncompress_fulldump_file(char *in_file, char *out_file, int ramdump_ver, int debug_level);
void show_uncompress_usage(void);

#endif
