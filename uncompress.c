#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "lz4defs.h"

#define CONFIG_BUILD_CRASH

/*
 * printf 64bit/32bit
 */
#ifdef __i386__
	#define FMT_64 "0x%08llx"  // 32-bit %llx for uint64_t
	#define FMT_32 "0x%08lx"   // 32-bit %lx  for uint32_t
	#define STEPSIZE       4
#elif defined(__x86_64__)
	#define FMT_64 "0x%08lx"   // 64-bit  %lx for uint64_t
	#define FMT_32 "0x%08x"    // 64-bit  %x  for uint32_t
	#define STEPSIZE       8
#else
	#error "Unsupported architecture!"
	#define STEPSIZE       8
#endif

/*
 * aml crashdump head info
 */
#define COMPRESS_TAG_SIZE  16
#define COMPRESS_TAG_BL3Z  "AML_RAMDUMPBL3Z"
#define COMPRESS_TAG_HEAD  "AML_RAMDUMPHEAD"
#define COMPRESS_TAG_TAIL  "AML_RAMDUMPTAIL"

enum {
	VERSION_SMALL_DDR = 0,
	VERSION_BIG_DDR   = 1
};

enum {
	RAM_COMPRESS_NORMAL = 1,
	RAM_COMPRESS_COPY   = 2,
	RAM_COMPRESS_SET    = 3    // set ram content to same vale
};

/*
 * struct for decompress data file
 */
struct section_info {
	uint64_t offset;
	uint32_t type;
#ifdef __i386__
	uint32_t padding1;         // aligned to 8 bytes for x86_32
#endif
	uint64_t zip_size;
	uint64_t raw_size;
	uint64_t val;		       // num of chunks
} __attribute__((aligned(8)));

/*
 * In version v1, the crash file header uses uint32_t fields to describe
 * section sizes and file size. As newer platforms require support for
 * devices with more than 4GB of DDR memory, a new version v2 was introduced,
 * upgrading these fields to uint64_t to properly handle larger memory dumps.
 */
struct ram_compress_section_header_v1 {
	uint32_t raw_size;
	uint32_t zip_size;
	uint32_t section_index : 8;
	uint32_t compress_type : 8;
	uint32_t set_value     : 16;
#ifdef __i386__
	uint32_t padding;
#endif
} __attribute__((aligned(8)));

struct ram_compress_file_header_v1 {
	char tag[COMPRESS_TAG_SIZE];
	uint32_t section_count;
	uint32_t file_size;
#ifdef __i386__
    uint32_t padding;
#endif
} __attribute__((aligned(8)));

struct ram_compress_section_header_v2 {
	uint64_t raw_size;
	uint64_t zip_size;
	uint32_t section_index : 8;
	uint32_t compress_type : 8;
	uint32_t set_value     :16;
#ifdef __i386__
	uint32_t padding;
#endif
} __attribute__((aligned(8)));

struct ram_compress_file_header_v2 {
	char tag[COMPRESS_TAG_SIZE];
	uint32_t section_count;
#ifdef __i386__
	uint32_t padding;
#endif
	uint64_t file_size;
} __attribute__((aligned(8)));

#define EXTRA_MEM 128
static int flag_debug_verbose = 1;

static const int dec32table[8] = {0, 3, 2, 3, 0, 0, 0, 0};
static const int dec64table[8] = {
#if LZ4_ARCH64
	0, 0, 0, -1, 0, 1, 2, 3
#else
	0, 0, 0, 0, 0, 0, 0, 0
#endif
};

/* LZ4 ucompress core */
static uint64_t lz4_uncompress_unknownoutputsize(const u8 *source, u8 *dest,
				uint64_t isize, uint64_t maxoutputsize)
{
	const u8 *ip = source;
	const u8 * const iend = ip + (size_t)isize;
	u8 *op = dest;
	u8 * const oend = op + (size_t)maxoutputsize;
	u8 *ref, *cpy;

	while (ip < iend) {
		uint8_t token = *ip++;
		uint32_t length = token >> ML_BITS;

		if (length == RUN_MASK) {
			uint8_t s;
			do {
				s = *ip++;
				length += s;
			} while (s == 255 && ip < iend);
		}

		cpy = op + length;
		if (cpy > oend || (ip + length) > iend)
			return -1;

		memcpy(op, ip, length);
		ip += length;
		op += length;

		if (ip >= iend)
			break;

		uint16_t offset = ip[0] | (ip[1] << 8);
		ip += 2;
		if (offset == 0 || (op - dest) < offset)
			return -1;

		ref = op - offset;

		length = token & ML_MASK;
		if (length == ML_MASK) {
			uint8_t s;
			do {
				s = *ip++;
				length += s;
			} while (s == 255 && ip < iend);
		}
		length += MINMATCH;

		cpy = op + length;
		if (cpy > oend)
			return -1;

		while (op < cpy)
			*op++ = *ref++;
	}
	return (uint64_t)(op - dest);
}

/* lz4 uncompress interface */
int lz4_decompress_unknownoutputsize(const unsigned char *src, uint64_t src_len,
		unsigned char *dest, uint64_t *dest_len)
{
	int ret = -1;
	uint64_t out_len = 0;

	out_len = lz4_uncompress_unknownoutputsize(src, dest, src_len,
					*dest_len);
	if (out_len < 0)
		goto exit_0;
	*dest_len = out_len;

	return 0;
exit_0:
	return ret;
}

/* uncompress version V1 */
static int uncompress_segment_v1(struct section_info *cs,
					FILE *in, FILE*out,
					uint64_t *tw, uint64_t *mp)
{
	uint64_t compresslen, uncomplen;
	uint64_t rs;
	uint32_t sizeinfo, ret;
	uint64_t i, readinfo;
	fpos_t pos;
	uint64_t pos_val = 0;
	char *pComp   = NULL;
	char *pUncomp = NULL;

	fseek(in, cs->offset, SEEK_SET);
	for (i = 0; i < cs->val; i++) {
		uncomplen = 0;
		compresslen = 0;
		rs = fread(&sizeinfo, 1, sizeof(sizeinfo), in);
		if (rs != sizeof(sizeinfo)) {
			printf("expected %lu bytes, but read out %lu\n",
					sizeof(sizeinfo), rs);
			return -1;
		}
		compresslen = (uint32_t)sizeinfo;
		rs = fread(&sizeinfo, 1, sizeof(sizeinfo), in);
		if (rs != sizeof(sizeinfo)) {
			printf("expected %lu bytes, but read out %lu\n",
					sizeof(sizeinfo), rs);
			return -1;
		}
		uncomplen = (uint32_t)sizeinfo;
		pos_val = ftell(in);
		if (pos_val == -1L) {
			printf("ftell failed!\n");
			return -1;
		}
		printf("=> unzip chunk " FMT_64 " -> " FMT_64 ", pos:" FMT_64 ", memp:" FMT_64 "\n",
		       compresslen, uncomplen, pos_val, *mp);
		pComp = malloc((uint64_t)(compresslen + 4 * 1024 * 1024));
		pUncomp = malloc((uint64_t)(uncomplen + 4 * 1024 * 1024));
		memset(pComp, 0, compresslen + 4 * 1024 * 1024);
		memset(pUncomp, 0, uncomplen + 4 * 1024 * 1024);
		rs = fread(pComp, 1, compresslen, in);
		if (rs != compresslen) {
			printf("expected " FMT_64 " bytes, but read out " FMT_64 "\n",
					compresslen, rs);
			return -1;
		}
		ret = lz4_decompress_unknownoutputsize(pComp, compresslen,
								pUncomp, &uncomplen);
		if (ret < 0) {
			printf("failed unzip! ret = %d uncomplen:" FMT_64 "\n",
					ret, uncomplen);
		}
		if (uncomplen !=  sizeinfo) {
			printf("======= ERROR ======\n");
			pos_val = ftell(in);
			if (pos_val == -1L) {
				printf("ftell failed!\n");
				return -1;
			}
			printf("unzip miss match, expected:%8u, actual:" FMT_64 "\n",
			       sizeinfo, uncomplen);
			printf("current fp:" FMT_64 ", fill remain space with zero\n",
				pos_val);
			printf("======= END =======\n");
			memset(pUncomp + uncomplen, 0, sizeinfo - uncomplen);
			uncomplen += (sizeinfo - uncomplen);
		}
		*mp += uncomplen;
		*tw += uncomplen;
		fwrite((void *)pUncomp, 1, uncomplen, out);
		free(pUncomp);
		pUncomp = NULL;
		free(pComp);
		pComp = NULL;
	}
	return 0;
}

static int parse_dump_file_v1(char *in_file, char *out_file, int flag_debug_verbose)
{
	FILE *pInfile = NULL, *pOutfile = NULL;
	uint64_t rs;
	char *pUncomp = NULL;
	uint64_t tw = 0, pretw = 0;
	uint32_t idx, totalsize;
	struct ram_compress_file_header_v1 fhead;
	struct ram_compress_section_header_v1 csh;
	struct section_info *seg_tbl, *cs;
	uint64_t seg_offset;
	int cnt = 0, ret = 0;
	uint64_t mem_pointer = 0;

	/* Open input file */
	pInfile = fopen(in_file, "rb");
	if (!pInfile) {
		printf("Failed opening input file!\n");
		return 3;
	}

	rs = fread(&fhead, 1, sizeof(fhead), pInfile);
	if (rs != sizeof(fhead)) {
		printf("expected %ld bytes, but read out %lu\n",
				sizeof(fhead), rs);
		goto error_exit;
	}
	if (strncmp(fhead.tag, COMPRESS_TAG_HEAD, COMPRESS_TAG_SIZE) &&
		strncmp(fhead.tag, COMPRESS_TAG_BL3Z, COMPRESS_TAG_SIZE)) {
		printf("expected signature %s, actual: %s\n",
				COMPRESS_TAG_HEAD, fhead.tag);
		goto error_exit;
	}

	printf("-----------------[ parse_dump_file v1.0]------------------------\n");
	printf("total number of segment is %d \n", fhead.section_count);
	seg_tbl = malloc(sizeof(struct section_info) * fhead.section_count);

	printf("total compress file size is 0x%08x \n", fhead.file_size);
#ifdef __i386__
	printf("#### arch: i386\n");
#else
	printf("#### arch: x86_64\n");
#endif

	seg_offset = sizeof(fhead);
	if (flag_debug_verbose)
		printf("jump head offset 0x%08lx \n", seg_offset);
	for (cnt = 0; cnt < fhead.section_count; cnt++) {
		memset(&csh, 0, sizeof(csh));
		rs = fread((void *)&csh, 1, sizeof(csh), pInfile);
		if (rs != sizeof(csh)) {
			if (flag_debug_verbose)
				printf("[%d] expected %lu bytes, but read out %lu\n",
					cnt + 1, sizeof(csh), rs);
			continue;
		}
		idx = csh.section_index - 1;
		seg_tbl[idx].offset   = seg_offset + sizeof(csh);
		seg_tbl[idx].type     = csh.compress_type;
		seg_tbl[idx].raw_size = csh.raw_size;
		seg_tbl[idx].zip_size = csh.zip_size;
		seg_tbl[idx].val      = csh.set_value;
		seg_offset           += csh.zip_size;
		fseek(pInfile, csh.zip_size - sizeof(csh), SEEK_CUR);
	}

	printf("\n\nidx      offset  type   orig size    zip size   val\n");
	printf("-----------------------------------------------------------\n");
	for (cnt = 0; cnt < fhead.section_count; cnt++) {
		printf(" %2d, 0x%08lx,   %2u, 0x%08lx, 0x%08lx, 0x%02x\n",
				cnt, seg_tbl[cnt].offset,
				seg_tbl[cnt].type, seg_tbl[cnt].raw_size,
				seg_tbl[cnt].zip_size, seg_tbl[cnt].val);
	}

	fseek(pInfile, -COMPRESS_TAG_SIZE, SEEK_END);
	rs = fread(fhead.tag, 1, COMPRESS_TAG_SIZE, pInfile);
	if (rs != COMPRESS_TAG_SIZE) {
		printf("expected %d bytes, but read out %lu\n",
				COMPRESS_TAG_SIZE, rs);
		goto error_exit;
	}

	if (strncmp(fhead.tag, COMPRESS_TAG_TAIL, COMPRESS_TAG_SIZE)) {
		printf("expected signature %s at the end, actual: %s",
				COMPRESS_TAG_TAIL, fhead.tag);
		//goto error_exit;
	}

	/* Open output file. */
	pOutfile = fopen(out_file, "wb");
	if (!pOutfile) {
		printf("Failed opening output file %s!\n", out_file);
		return 3;
	}

	printf("-----------------[ parse_dump_file v1.0 ]-----------------------\n");
	for (cnt = 0; cnt < fhead.section_count; cnt++) {
		cs = &seg_tbl[cnt];
		switch (cs->type) {
		case RAM_COMPRESS_SET:
			pUncomp = (unsigned char*)malloc(cs->raw_size);
			memset(pUncomp, cs->val, cs->raw_size);
			fwrite(pUncomp, 1, cs->raw_size, pOutfile);
			mem_pointer += cs->raw_size;
			tw += cs->raw_size;
			free(pUncomp);
			pUncomp = NULL;
			printf("write next %lu MB with value %x, memp:%8lx\n",
					cs->raw_size / 1024 / 1024, (unsigned char)cs->val,
					mem_pointer);
			break;

		case RAM_COMPRESS_COPY:
			fseek(pInfile, cs->offset, SEEK_SET);
			pUncomp = (unsigned char *)malloc(cs->raw_size);
			memset(pUncomp, 0, cs->raw_size);
			rs = fread(pUncomp, 1, cs->raw_size, pInfile);
			if (rs != cs->raw_size) {
				printf("expected %lu bytes, but read out %lu\n",
						cs->raw_size, rs);
				break;
			}
			fwrite(pUncomp, 1, cs->raw_size, pOutfile);
			mem_pointer += cs->raw_size;
			tw += cs->raw_size;
			free(pUncomp);
			pUncomp = NULL;
			printf("No compress, just write %lu bytes, memp:%8lx\n",
					cs->raw_size, mem_pointer);
			break;

		case RAM_COMPRESS_NORMAL:
			if (uncompress_segment_v1(cs, pInfile, pOutfile,
					       &tw, &mem_pointer) < 0)
				goto error_exit;
			break;

		default:
			printf("###unsupported type: %u\n", cs->type);
			break;
		}
		printf("****** Seg %2d; size written to file: 0x%016lx ******\n\n",
				cnt, tw-pretw);
		pretw = tw;
	}

	printf("Total written: %lu; in hex: 0x%lx\n", tw, tw);
error_exit:
	if (pUncomp)
		free(pUncomp);
	if (pInfile)
		fclose(pInfile);
	if (pOutfile)
		fclose(pOutfile);
	return 0;
}

/* uncompress version V2 */
int uncompress_segment_v2(struct section_info *cs, FILE *in, FILE *out,
						uint64_t *tw, uint64_t *mp)
{
	uint64_t compresslen, uncomplen;
	uint32_t sizeinfo;
	uint64_t pos_val;
	size_t write_size;
	char *pComp = NULL, *pUncomp = NULL;

	if (fseeko(in, cs->offset, SEEK_SET) != 0) {
		perror("fseeko failed");
		return -1;
	}

	for (uint64_t i = 0; i < cs->val; i++) {
		if (fread(&sizeinfo, 1, sizeof(sizeinfo), in) != sizeof(sizeinfo)) {
			printf("Failed to read compresslen\n");
			return -1;
		}
		compresslen = sizeinfo;

		if (fread(&sizeinfo, 1, sizeof(sizeinfo), in) != sizeof(sizeinfo)) {
			printf("Failed to read uncomplen\n");
			return -1;
		}
		uncomplen = sizeinfo;

		pos_val = ftello64(in);
		if (pos_val == (off_t)-1) {
			perror("ftello failed");
			return -1;
		}

		printf("=> unzip chunk " FMT_64 " -> " FMT_64 ", pos:" FMT_64 ", memp:" FMT_64 "\n",
				compresslen, uncomplen, (uint64_t)pos_val, *mp);

		pComp = calloc(1, compresslen + EXTRA_MEM);
		pUncomp = calloc(1, uncomplen + EXTRA_MEM);
		if (!pComp || !pUncomp) {
			printf("Memory allocation failed!\n");
			free(pComp);
			free(pUncomp);
			return -1;
		}

		if (fread(pComp, 1, compresslen, in) != compresslen) {
			printf("Error reading compressed data\n");
			free(pComp);
			free(pUncomp);
			return -1;
		}

		uint64_t outlen = uncomplen;
		int ret = lz4_decompress_unknownoutputsize((unsigned char *)pComp, compresslen,
													(unsigned char *)pUncomp, &outlen);
		if (ret < 0) {
			printf("failed unzip! ret = %d uncomplen:" FMT_64 "\n", ret, uncomplen);
			free(pComp);
			free(pUncomp);
			return -1;
		}

		if (outlen != uncomplen) {
			printf("Data mismatch: expected=" FMT_64 ", actual=" FMT_64 "\n", uncomplen, outlen);
			memset(pUncomp + outlen, 0, uncomplen - outlen);
		}

		write_size = fwrite(pUncomp, 1, uncomplen, out);
		if (write_size != uncomplen) {
			if ((*mp + uncomplen) >= 0x80000000) {
				printf("Warning: fwrite operation exceeded 2GB file size limit!\n");
			} else {
				printf("Warning: fwrite failed!\n");
				return -1;
			}
		}
		*mp += uncomplen;
		*tw += uncomplen;

		free(pUncomp);
		free(pComp);
	}

	return 0;
}

static int parse_dump_file_v2(char *in_file, char *out_file, int flag_debug_verbose)
{
	FILE *pInfile = NULL, *pOutfile = NULL;
	uint64_t rs;
	char *pUncomp = NULL;
	uint64_t tw = 0, pretw = 0;
	uint32_t idx, totalsize;
	struct ram_compress_file_header_v2 fhead;
	struct ram_compress_section_header_v2 csh;
	struct section_info *seg_tbl, *cs;
	uint64_t seg_offset;
	int cnt = 0, ret = 0;
	uint64_t mem_pointer = 0;

	/* Open input file */
	pInfile = fopen(in_file, "rb");
	if (!pInfile) {
		printf("Failed opening input file!\n");
		return 3;
	}

	rs = fread(&fhead, 1, sizeof(fhead), pInfile);
	if (rs != sizeof(fhead)) {
		printf("expected %ld bytes, but read out %lu\n",
				sizeof(fhead), rs);
		goto error_exit;
	}
	if (strncmp(fhead.tag, COMPRESS_TAG_HEAD, COMPRESS_TAG_SIZE) &&
		strncmp(fhead.tag, COMPRESS_TAG_BL3Z, COMPRESS_TAG_SIZE)) {
		printf("expected signature %s, actual: %s\n",
				COMPRESS_TAG_HEAD, fhead.tag);
		goto error_exit;
	}

	printf("-----------------[ parse_dump_file v2.0]-----------------------\n");
	printf("total number of segment is %d \n", fhead.section_count);
	seg_tbl = malloc(sizeof(struct section_info) * fhead.section_count);

	if (flag_debug_verbose) {
		printf("total compress file size is 0x%08lx\n\n", fhead.file_size);
		printf("sizeof(struct section_info) = %d\n", sizeof(struct section_info));
		printf("offset of(struct section_info, raw_size) = %d\n", offsetof(struct section_info, raw_size));
		printf("sizeof(struct ram_compress_file_header_v2) = %d\n", sizeof(struct ram_compress_file_header_v2));
		printf("sizeof(struct ram_compress_section_header_v2) = %d\n", sizeof(struct ram_compress_section_header_v2));
		printf("sizeof(int) = %d, sizeof(uint64_t) = %d\n\n", sizeof(int), sizeof(uint64_t));
	}
#ifdef __i386__
	printf("#### arch: i386\n");
#else
	printf("#### arch: x86_64\n");
#endif

	seg_offset = sizeof(fhead);
	if (flag_debug_verbose)
		printf("jump fhead offset = 0x%08lx \n", seg_offset);
	for (cnt = 0; cnt < fhead.section_count; cnt++) {
		memset(&csh, 0, sizeof(csh));
		rs = fread((void *)&csh, 1, sizeof(csh), pInfile);
		if (rs != sizeof(csh)) {
			if (flag_debug_verbose)
				printf("Save-%d expected %lu bytes, but read out %lu\n",
					cnt + 1, sizeof(csh), rs);
			continue;
		}
		idx = csh.section_index - 1;
		if (flag_debug_verbose) {
			printf("\nDUMP section head inf (Save-%d: seg_tbl[%d])\n", cnt + 1, idx);
			printf("struct ram_compress_section_header_v2 { 0x%08lx\n", seg_offset);
			printf("    uint64_t raw_size;           | 0x%08lx\n", csh.raw_size);
			printf("    uint64_t zip_size;           | 0x%08lx\n", csh.zip_size);
			printf("    uint32_t section_index : 8;  | 0x%02x \n", csh.section_index);
			printf("    uint32_t compress_type : 8;  | 0x%02x \n", csh.compress_type);
			printf("    uint32_t set_value     :16;  | 0x%02x \n", csh.set_value);
			printf("}; \n\n");
		}

		if (csh.section_index > fhead.section_count) {
			if (flag_debug_verbose)
				printf("csh.section_index(%d) >  fhead.section_count(%d), error, exit.\n",
						csh.section_index, fhead.section_count);
			return -1;
		}

		seg_tbl[idx].offset   = seg_offset + sizeof(csh);
		seg_tbl[idx].type     = csh.compress_type;
		seg_tbl[idx].zip_size = csh.zip_size;
		seg_tbl[idx].raw_size = csh.raw_size;
		seg_tbl[idx].val      = csh.set_value;
		seg_offset           += csh.zip_size;

		if (flag_debug_verbose) {
			printf("\nSAVE section head info (Save-%d: seg_tbl[%d])\n", cnt + 1, idx);
			printf("    uint64_t offset;    | 0x%08lx\n", seg_tbl[idx].offset);
			printf("    uint32_t type;      | 0x%02x\n",  seg_tbl[idx].type);
			printf("    uint64_t zip_size;  | 0x%08lx\n", seg_tbl[idx].zip_size);
			printf("    uint64_t raw_size;  | 0x%08lx\n", seg_tbl[idx].raw_size);
			printf("    uint32_t val;       | 0x%02x\n",  seg_tbl[idx].val);
			printf("}; \n\n");
		}

		fseek(pInfile, csh.zip_size - sizeof(csh), SEEK_CUR);
	}

	if (1) {
		printf("\nsaved    offset    type   raw_size    zip_size   val\n");
		printf("-----------------------------------------------------------\n");
		for (cnt = 0; cnt < fhead.section_count; cnt++) {
			printf(" %2d, " FMT_64 ",   %2u, " FMT_64 ", " FMT_64 ", 0x%02x\n",
				cnt+1, seg_tbl[cnt].offset,
				seg_tbl[cnt].type, seg_tbl[cnt].raw_size,
				seg_tbl[cnt].zip_size, seg_tbl[cnt].val);
		}
	}

	fseek(pInfile, -COMPRESS_TAG_SIZE, SEEK_END);
	rs = fread(fhead.tag, 1, COMPRESS_TAG_SIZE, pInfile);
	if (rs != COMPRESS_TAG_SIZE) {
		if (flag_debug_verbose)
			printf("expected %d bytes, but read out %lu\n",
				COMPRESS_TAG_SIZE, rs);
		goto error_exit;
	}

	if (strncmp(fhead.tag, COMPRESS_TAG_TAIL, COMPRESS_TAG_SIZE)) {
		printf("expected signature %s at the end, actual: %s",
			COMPRESS_TAG_TAIL, fhead.tag);
		//goto error_exit;
	}

	/* Open output file. */
	pOutfile = fopen(out_file, "wb");
	if (!pOutfile) {
		printf("Failed opening output file %s!\n", out_file);
		return 3;
	}
	if (flag_debug_verbose)
		printf("-----------------------------------------------------------\n\n");

	for (cnt = 0; cnt < fhead.section_count; cnt++) {
		printf("\n****** Uncompress Saved-%d:\n", cnt + 1);
		cs = &seg_tbl[cnt];
		switch (cs->type) {
		case RAM_COMPRESS_SET:
			pUncomp = (unsigned char*)malloc(cs->raw_size);
			memset(pUncomp, cs->val, cs->raw_size);
			fwrite(pUncomp, 1, cs->raw_size, pOutfile);
			mem_pointer += cs->raw_size;
			tw += cs->raw_size;
			free(pUncomp);
			pUncomp = NULL;
			printf("write next %lu bytes with value %x, memp:%8lx\n",
					cs->raw_size, (unsigned char)cs->val,
					mem_pointer);
			break;

		case RAM_COMPRESS_COPY:
			fseek(pInfile, cs->offset, SEEK_SET);
			pUncomp = (unsigned char *)malloc(cs->raw_size);
			memset(pUncomp, 0, cs->raw_size);
			rs = fread(pUncomp, 1, cs->raw_size, pInfile);
			if (rs != cs->raw_size) {
				printf("expected %lu bytes, but read out %lu\n",
						cs->raw_size, rs);
				break;
			}
			fwrite(pUncomp, 1, cs->raw_size, pOutfile);
			mem_pointer += cs->raw_size;
			tw += cs->raw_size;
			free(pUncomp);
			pUncomp = NULL;
			printf("No compress, just write %lu bytes, memp:%8lx\n",
					cs->raw_size, mem_pointer);
			break;

		case RAM_COMPRESS_NORMAL:
			if (uncompress_segment_v2(cs, pInfile, pOutfile,
						&tw, &mem_pointer) < 0)
				goto error_exit;
			break;

		default:
			printf("###unsupported type: %u\n", cs->type);
			break;
		}
		printf("****** Section idx %2d, uncompressed size: 0x%08lx ******\n\n",
				cnt + 1, tw-pretw);
		pretw = tw;
	}

	printf("Total written: %llu; in hex: 0x%llx (%lld MB)\n", tw, tw, tw / 1024 / 1024);
	printf("\nfulldump uncompress success!\n\n");

error_exit:
	if (pUncomp)
		free(pUncomp);
	if (pInfile)
		fclose(pInfile);
	if (pOutfile)
		fclose(pOutfile);
	return 0;
}

#ifndef CONFIG_BUILD_CRASH
void show_usage(void)
{
	printf("\nUsage:  [V2.1 2025.06.19]\n");
	printf("    uncompress [-b|-s] -i [bin file] -o [output file]\n");
	printf("    eg: ./uncompress -b -i crashdump-1.bin -o DUMP\n");
	printf("parameter:\n");
	printf("    -b: for new ramdump(bl33z), can also uncompress big ddr(2G/4G/8G)\n");
	printf("    -s: for old ramdump(bl2z), can only uncompress small ddr\n");
	printf("  -v: verbose, print more debug info\n");
	printf("    -i: input file\n");
	printf("    -o: output file\n\n");
}

#define FILE_LENGTH	 256
int main(int argc, char *argv[])
{
	FILE *pfile = NULL;
	char in_file[FILE_LENGTH] = {}, out_file[FILE_LENGTH] = {};
	char compress_head[COMPRESS_TAG_SIZE] = {};
	int i = 0, size = 0;
	int flag_ramdump_version = 0;

	if (argc < 4 || argc > 7) {
		show_usage();
		return 3;
	}

	/* default out put file name */
	strcpy(out_file, "RAMDUMP.bin");
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-i")) {
			if (i + 1 > argc) {
				show_usage();
				return 2;
			}
			strcpy(in_file, argv[++i]);
			continue;
		}
		if (!strcmp(argv[i], "-o")) {
			if (i + 1 > argc) {
				show_usage();
				return 2;
			}
			strcpy(out_file, argv[++i]);
			continue;
		}
		if (!strcmp(argv[i], "-b")) {
			flag_ramdump_version = VERSION_BIG_DDR;
			printf("set ramdump version: v2.0, last modify: 2023.09.21\n");
			continue;
		}
		if (!strcmp(argv[i], "-s")) {
			flag_ramdump_version = VERSION_SMALL_DDR;
			printf("set ramdump version: v1.0 \n");
			continue;
		}
		if (!strcmp(argv[i], "-v")) {
			flag_debug_verbose = 1;
			printf("set ramdump debug verbose. \n");
			continue;
		}
		show_usage();
		return 2;
	}

	/* check file head format again */
	pfile = fopen(in_file, "rb");
	if (!pfile) {
		printf("Failed opening input file!\n");
		return 3;
	}
	size = fread(compress_head, 1, COMPRESS_TAG_SIZE, pfile);
	if (size != COMPRESS_TAG_SIZE) {
		printf("get file head, expected %d bytes, but read out %d\n",
				COMPRESS_TAG_SIZE, size);
		goto error_exit;
	}
	if (!strncmp(compress_head, COMPRESS_TAG_BL3Z, COMPRESS_TAG_SIZE)) {
		printf("head info:%s, New ramdump version!\n", COMPRESS_TAG_BL3Z);
		flag_ramdump_version = VERSION_BIG_DDR;
	}
	fclose(pfile);
	pfile = NULL;

	/* parse fump file */
	if (flag_ramdump_version == VERSION_SMALL_DDR) {
		parse_dump_file_v1(in_file, out_file, 1);
	} else if (flag_ramdump_version == VERSION_BIG_DDR) {
		parse_dump_file_v2(in_file, out_file, 1);
	} else {
		parse_dump_file_v1(in_file, out_file, 1);
	}

error_exit:
	if (pfile)
		fclose(pfile);

	return 0;
}

#else /* CONFIG_BUILD_CRASH */

void show_uncompress_usage(void)
{
	printf("\nUsage:\n");
	printf("	--uncompress [-b|-s] -i [in_file] -o [out_file]\n");
	printf("	eg: crash --uncompress -b -i ./crashdump-1.bin -o ./DUMP\n\n");
	printf("Parameter:\n");
	printf("	--uncompress: Use uncompress. All following parameters apply to this feature.\n");
	printf("	 -s: for old ramdump(bl2z), can only uncompress small ddr.\n");
	printf("	 -b: for new ramdump(bl33z), can also uncompress big ddr(2G/4G/8G)\n");
	printf("	 -i: input compress DUMP file. eg: ./crash-dump-1.bin\n");
	printf("	 -o: output uncompress DUMP file. eg: ./DUMP\n");
	printf("	 -d: set debug level. Default 0, and set to 1 to output more logs.\n");
	printf("	 -h: show this help info.\n\n");
}

int uncompress_fulldump_file(char *in_file, char *out_file, int ramdump_ver, int debug_level)
{
	FILE *pfile = NULL;
	char compress_head[COMPRESS_TAG_SIZE] = {0};
	int size;

	if (!out_file) {
		out_file = "DUMP.bin";
		printf("Info: use default out_file name '%s'!\n", out_file);
	}
	/* check in_file */
	pfile = fopen(in_file, "rb");
	if (!pfile) {
		printf("Error: Failed to open input file '%s'!\n", in_file);
		show_uncompress_usage();
		goto error_exit;
	}

	/* check file head format */
	size = fread(compress_head, 1, COMPRESS_TAG_SIZE, pfile);
	if (size != COMPRESS_TAG_SIZE) {
		printf("Error: File head read failed, expected %d bytes, got %d\n",
			COMPRESS_TAG_SIZE, size);
		goto error_exit;
	}
	if (strncmp(compress_head, COMPRESS_TAG_HEAD, COMPRESS_TAG_SIZE) &&
		strncmp(compress_head, COMPRESS_TAG_BL3Z, COMPRESS_TAG_SIZE)) {
		printf("Error: File head format error. Not found magic sting!\n");
		goto error_exit;
	}

	if (!strncmp(compress_head, COMPRESS_TAG_BL3Z, COMPRESS_TAG_SIZE) ||
		ramdump_ver == VERSION_BIG_DDR) {
		printf("Detected new ramdump format (v2.0, for big ddr size.)\n");
		parse_dump_file_v2(in_file, out_file, debug_level);
	} else {
		printf("Detected old ramdump format (v1.0, for small ddr size.)\n");
		parse_dump_file_v1(in_file, out_file, debug_level);
	}

error_exit:
	if (pfile)
		fclose(pfile);

	return 0;
}
#endif /* CONFIG_BUILD_CRASH */

