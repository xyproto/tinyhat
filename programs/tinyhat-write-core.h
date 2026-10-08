#ifndef TINYHAT_WRITE_CORE_H
#define TINYHAT_WRITE_CORE_H

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <sys/mount.h>
#endif
#endif

#define TW_CHUNK 262144

static uint32_t tw_crc_table[256];
static int tw_crc_ready;

static void tw_crc_init(void)
{
	uint32_t c;
	int n, k;
	for (n = 0; n < 256; n++) {
		c = (uint32_t)n;
		for (k = 0; k < 8; k++)
			c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
		tw_crc_table[n] = c;
	}
	tw_crc_ready = 1;
}

static uint32_t tw_crc_update(uint32_t crc, const unsigned char *p, size_t n)
{
	size_t i;
	if (!tw_crc_ready)
		tw_crc_init();
	crc ^= 0xffffffffu;
	for (i = 0; i < n; i++)
		crc = tw_crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
	return crc ^ 0xffffffffu;
}

typedef struct {
	short count[16];
	short symbol[288];
} tw_huff;

typedef struct {
	const unsigned char *src;
	size_t srclen;
	size_t srcpos;
	uint32_t bitbuf;
	int bitcnt;
	int final;
	int btype;
	unsigned char win[32768];
	uint32_t winpos;
	unsigned char out[TW_CHUNK];
	size_t outlen;
	unsigned char lbuf[320];
	tw_huff lit;
	tw_huff dist;
} tw_gz;

static int tw_bits(tw_gz *z, int need)
{
	uint32_t val;
	while (z->bitcnt < need) {
		if (z->srcpos >= z->srclen)
			return -1;
		z->bitbuf |= (uint32_t)z->src[z->srcpos++] << z->bitcnt;
		z->bitcnt += 8;
	}
	val = z->bitbuf & ((1u << need) - 1);
	z->bitbuf >>= need;
	z->bitcnt -= need;
	return (int)val;
}

static int tw_huff_build(tw_huff *h, const unsigned char *lens, int n)
{
	short offs[16];
	int i, sym, left;
	for (i = 0; i < 16; i++)
		h->count[i] = 0;
	for (i = 0; i < n; i++)
		h->count[lens[i]]++;
	if (h->count[0] == n)
		return 0;
	h->count[0] = 0;
	offs[0] = 0;
	left = 1;
	for (i = 1; i < 16; i++) {
		left <<= 1;
		left -= h->count[i];
		if (left < 0)
			return -1;
		offs[i] = offs[i - 1] + h->count[i - 1];
	}
	for (sym = 0; sym < n; sym++)
		if (lens[sym])
			h->symbol[offs[lens[sym]]++] = (short)sym;
	return left;
}

static int tw_huff_decode(tw_gz *z, const tw_huff *h)
{
	int len, code = 0, first = 0, index = 0, b;
	for (len = 1; len < 16; len++) {
		b = tw_bits(z, 1);
		if (b < 0)
			return -1;
		code |= b;
		if (code - first < h->count[len])
			return h->symbol[index + (code - first)];
		index += h->count[len];
		first = (first + h->count[len]) << 1;
		code <<= 1;
	}
	return -1;
}

static const unsigned short tw_lenbase[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43,
	51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const unsigned char tw_lenextra[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4,
	4, 4, 5, 5, 5, 5, 0
};
static const unsigned short tw_distbase[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
	513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const unsigned char tw_distextra[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,
	10, 10, 11, 11, 12, 12, 13, 13
};
static const unsigned char tw_clorder[19] = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

typedef int (*tw_emit_fn)(void *ud, const unsigned char *p, size_t n);

static void tw_put(tw_gz *z, unsigned char b, tw_emit_fn emit, void *ud)
{
	z->win[z->winpos++ & 32767] = b;
	z->out[z->outlen++] = b;
	if (z->outlen == TW_CHUNK) {
		emit(ud, z->out, z->outlen);
		z->outlen = 0;
	}
}

static int tw_gz_header(tw_gz *z)
{
	unsigned flg;
	if (z->srclen < 18)
		return -1;
	if (z->src[0] != 0x1f || z->src[1] != 0x8b || z->src[2] != 8)
		return -1;
	flg = z->src[3];
	z->srcpos = 10;
	if (flg & 4) {
		unsigned xlen;
		if (z->srcpos + 2 > z->srclen)
			return -1;
		xlen = z->src[z->srcpos] | (z->src[z->srcpos + 1] << 8);
		z->srcpos += 2 + xlen;
	}
	while (flg & 8) {
		if (z->srcpos >= z->srclen)
			return -1;
		if (!z->src[z->srcpos++])
			break;
	}
	while (flg & 16) {
		if (z->srcpos >= z->srclen)
			return -1;
		if (!z->src[z->srcpos++])
			break;
	}
	if (flg & 2)
		z->srcpos += 2;
	return z->srcpos >= z->srclen ? -1 : 0;
}

static int tw_gz_tables(tw_gz *z)
{
	unsigned char lens[288];
	unsigned char dlens[30];
	int i;
	for (i = 0; i < 144; i++)
		lens[i] = 8;
	for (i = 144; i < 256; i++)
		lens[i] = 9;
	for (i = 256; i < 280; i++)
		lens[i] = 7;
	for (i = 280; i < 288; i++)
		lens[i] = 8;
	for (i = 0; i < 30; i++)
		dlens[i] = 5;
	if (tw_huff_build(&z->lit, lens, 288) < 0)
		return -1;
	if (tw_huff_build(&z->dist, dlens, 30) < 0)
		return -1;
	return 0;
}

static int tw_gz_dyntables(tw_gz *z)
{
	unsigned char lens[320];
	unsigned char clc[19];
	int hlit, hdist, hclen, n, idx, sym, prev, repeat, i;
	hlit = tw_bits(z, 5);
	hdist = tw_bits(z, 5);
	hclen = tw_bits(z, 4);
	if (hlit < 0 || hdist < 0 || hclen < 0)
		return -1;
	hlit += 257;
	hdist += 1;
	hclen += 4;
	if (hlit > 288 || hdist > 30 || hclen > 19)
		return -1;
	memset(clc, 0, sizeof(clc));
	for (i = 0; i < hclen; i++) {
		n = tw_bits(z, 3);
		if (n < 0)
			return -1;
		clc[tw_clorder[i]] = (unsigned char)n;
	}
	if (tw_huff_build(&z->lit, clc, 19) < 0)
		return -1;
	idx = 0;
	prev = 0;
	while (idx < hlit + hdist) {
		sym = tw_huff_decode(z, &z->lit);
		if (sym < 0)
			return -1;
		if (sym < 16) {
			lens[idx++] = (unsigned char)sym;
			prev = sym;
		} else if (sym == 16) {
			repeat = tw_bits(z, 2);
			if (repeat < 0 || !idx)
				return -1;
			repeat += 3;
			while (repeat-- && idx < hlit + hdist)
				lens[idx++] = (unsigned char)prev;
		} else if (sym == 17) {
			repeat = tw_bits(z, 3);
			if (repeat < 0)
				return -1;
			repeat += 3;
			prev = 0;
			while (repeat-- && idx < hlit + hdist)
				lens[idx++] = 0;
		} else {
			repeat = tw_bits(z, 7);
			if (repeat < 0)
				return -1;
			repeat += 11;
			prev = 0;
			while (repeat-- && idx < hlit + hdist)
				lens[idx++] = 0;
		}
	}
	memcpy(z->lbuf, lens, (size_t)(hlit + hdist));
	if (tw_huff_build(&z->lit, z->lbuf, hlit) < 0)
		return -1;
	if (tw_huff_build(&z->dist, z->lbuf + hlit, hdist) < 0)
		return -1;
	return 0;
}

static int tw_gz_inflate(tw_gz *z, tw_emit_fn emit, void *ud)
{
	int sym, len, dist, extra;
	uint32_t i;
	if (tw_gz_header(z) < 0)
		return -1;
	for (;;) {
		z->final = tw_bits(z, 1);
		z->btype = tw_bits(z, 2);
		if (z->final < 0 || z->btype < 0)
			return -1;
		if (z->btype == 0) {
			unsigned nlen, want;
			z->bitbuf = 0;
			z->bitcnt = 0;
			if (z->srcpos + 4 > z->srclen)
				return -1;
			want = z->src[z->srcpos] | (z->src[z->srcpos + 1] << 8);
			nlen = z->src[z->srcpos + 2] | (z->src[z->srcpos + 3] << 8);
			z->srcpos += 4;
			if ((want ^ 0xffffu) != nlen)
				return -1;
			while (want--) {
				if (z->srcpos >= z->srclen)
					return -1;
				tw_put(z, z->src[z->srcpos++], emit, ud);
			}
		} else {
			if (z->btype == 1) {
				if (tw_gz_tables(z) < 0)
					return -1;
			} else if (z->btype == 2) {
				if (tw_gz_dyntables(z) < 0)
					return -1;
			} else {
				return -1;
			}
			for (;;) {
				sym = tw_huff_decode(z, &z->lit);
				if (sym < 0)
					return -1;
				if (sym < 256) {
					tw_put(z, (unsigned char)sym, emit, ud);
					continue;
				}
				if (sym == 256)
					break;
				sym -= 257;
				if (sym >= 29)
					return -1;
				extra = tw_bits(z, tw_lenextra[sym]);
				if (extra < 0)
					return -1;
				len = tw_lenbase[sym] + extra;
				sym = tw_huff_decode(z, &z->dist);
				if (sym < 0 || sym >= 30)
					return -1;
				extra = tw_bits(z, tw_distextra[sym]);
				if (extra < 0)
					return -1;
				dist = tw_distbase[sym] + extra;
				for (i = 0; i < (uint32_t)len; i++)
					tw_put(z, z->win[(z->winpos - (uint32_t)dist) & 32767], emit, ud);
			}
		}
		if (z->final)
			break;
	}
	if (z->outlen) {
		emit(ud, z->out, z->outlen);
		z->outlen = 0;
	}
	return 0;
}

static uint32_t tw_gz_crc_of(const unsigned char *src, size_t srclen)
{
	if (srclen < 8)
		return 0;
	return (uint32_t)src[srclen - 8] | ((uint32_t)src[srclen - 7] << 8) |
	       ((uint32_t)src[srclen - 6] << 16) | ((uint32_t)src[srclen - 5] << 24);
}

static uint32_t tw_gz_size_of(const unsigned char *src, size_t srclen)
{
	if (srclen < 8)
		return 0;
	return (uint32_t)src[srclen - 4] | ((uint32_t)src[srclen - 3] << 8) |
	       ((uint32_t)src[srclen - 2] << 16) | ((uint32_t)src[srclen - 1] << 24);
}

typedef struct {
#ifdef _WIN32
	HANDLE h;
#else
	int fd;
#endif
} tw_dev;

static int tw_dev_open(tw_dev *d, const char *path, int write)
{
#ifdef _WIN32
	(void)write;
	d->h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
			   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			   OPEN_EXISTING, 0, NULL);
	if (d->h == INVALID_HANDLE_VALUE)
		d->h = CreateFileA(path, GENERIC_READ,
				   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
				   OPEN_EXISTING, 0, NULL);
	return d->h == INVALID_HANDLE_VALUE ? -1 : 0;
#else
	d->fd = open(path, write ? O_RDWR : O_RDONLY);
	return d->fd < 0 ? -1 : 0;
#endif
}

static void tw_dev_close(tw_dev *d)
{
#ifdef _WIN32
	if (d->h != INVALID_HANDLE_VALUE)
		CloseHandle(d->h);
	d->h = INVALID_HANDLE_VALUE;
#else
	if (d->fd >= 0)
		close(d->fd);
	d->fd = -1;
#endif
}

static int tw_dev_readat(tw_dev *d, uint64_t off, unsigned char *buf, size_t n)
{
#ifdef _WIN32
	DWORD got, done = 0;
	LARGE_INTEGER li;
	li.QuadPart = (LONGLONG)off;
	if (!SetFilePointerEx(d->h, li, NULL, FILE_BEGIN))
		return -1;
	while (done < (DWORD)n) {
		if (!ReadFile(d->h, buf + done, (DWORD)n - done, &got, NULL))
			return -1;
		if (!got)
			return -1;
		done += got;
	}
	return 0;
#else
	size_t done = 0;
	ssize_t got;
	while (done < n) {
		got = pread(d->fd, buf + done, n - done, (off_t)(off + done));
		if (got <= 0)
			return -1;
		done += (size_t)got;
	}
	return 0;
#endif
}

static uint64_t tw_dev_size(tw_dev *d)
{
#ifdef _WIN32
	LARGE_INTEGER li;
	if (!GetFileSizeEx(d->h, &li))
		return 0;
	return (uint64_t)li.QuadPart;
#else
	off_t end = lseek(d->fd, 0, SEEK_END);
	if (end < 0)
		return 0;
	return (uint64_t)end;
#endif
}

static int tw_dev_writeat(tw_dev *d, uint64_t off, const unsigned char *buf, size_t n)
{
#ifdef _WIN32
	DWORD done = 0;
	LARGE_INTEGER li;
	li.QuadPart = (LONGLONG)off;
	if (!SetFilePointerEx(d->h, li, NULL, FILE_BEGIN))
		return -1;
	while (done < (DWORD)n) {
		DWORD wrote;
		if (!WriteFile(d->h, buf + done, (DWORD)n - done, &wrote, NULL) || !wrote)
			return -1;
		done += wrote;
	}
	return 0;
#else
	size_t done = 0;
	ssize_t wrote;
	while (done < n) {
		wrote = pwrite(d->fd, buf + done, n - done, (off_t)(off + done));
		if (wrote <= 0)
			return -1;
		done += (size_t)wrote;
	}
	return 0;
#endif
}

static void tw_dev_flush(tw_dev *d)
{
#ifdef _WIN32
	FlushFileBuffers(d->h);
#else
	fsync(d->fd);
#endif
}

typedef int (*tw_read_fn)(void *ud, uint64_t off, unsigned char *buf, size_t n);

static int tw_dev_read_cb(void *ud, uint64_t off, unsigned char *buf, size_t n)
{
	return tw_dev_readat((tw_dev *)ud, off, buf, n);
}

#define TW_KIND_BLANK 0
#define TW_KIND_SCRATCH 1
#define TW_KIND_FRESH 2
#define TW_KIND_DATA 3

static uint32_t tw_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t tw_le64(const unsigned char *p)
{
	return (uint64_t)tw_le32(p) | ((uint64_t)tw_le32(p + 4) << 32);
}

static int tw_is_fat(const unsigned char *b)
{
	if (b[510] != 0x55 || b[511] != 0xaa)
		return 0;
	return !memcmp(b + 82, "FAT32   ", 8) || !memcmp(b + 54, "FAT12   ", 8) ||
	       !memcmp(b + 54, "FAT16   ", 8) || !memcmp(b + 54, "FAT     ", 8);
}

static int tw_is_exfat(const unsigned char *b)
{
	return !memcmp(b + 3, "EXFAT   ", 8);
}

static int tw_is_ntfs(const unsigned char *b)
{
	return !memcmp(b + 3, "NTFS    ", 8);
}

static int tw_fs_known(const unsigned char *b)
{
	return tw_is_fat(b) || tw_is_exfat(b) || tw_is_ntfs(b);
}

static int tw_fs_empty_fat(const unsigned char *b, tw_read_fn rd, void *rud, uint64_t base)
{
	uint32_t bytes_per_sec, sec_per_clus, fat_secs, total_sec, clusters, free_hint, rsvd, root_secs;
	unsigned fat_count, i, j;
	unsigned char sect[512];
	uint32_t used = 0;
	if (!tw_is_fat(b))
		return 0;
	bytes_per_sec = (uint32_t)b[11] | ((uint32_t)b[12] << 8);
	sec_per_clus = b[13];
	rsvd = (uint32_t)b[14] | ((uint32_t)b[15] << 8);
	fat_count = b[16];
	root_secs = (((uint32_t)b[17] | ((uint32_t)b[18] << 8)) * 32 + (bytes_per_sec - 1)) / bytes_per_sec;
	if (!bytes_per_sec || !sec_per_clus || !fat_count || fat_count > 4 || !rsvd)
		return 0;
	total_sec = (uint32_t)b[19] | ((uint32_t)b[20] << 8);
	if (!total_sec)
		total_sec = tw_le32(b + 32);
	fat_secs = (uint32_t)b[22] | ((uint32_t)b[23] << 8);
	if (!fat_secs)
		fat_secs = tw_le32(b + 36);
	if (!fat_secs || !total_sec)
		return 0;
	clusters = (total_sec - rsvd - fat_secs * fat_count - root_secs) / sec_per_clus;
	for (i = 0; i < fat_secs && i < 512 && used < 8; i++) {
		uint64_t off = base + (uint64_t)(rsvd + i) * bytes_per_sec;
		if (rd(rud, off, sect, 512) < 0)
			return 0;
		for (j = 0; j < 512 && used < 8; j += 4)
			if (sect[j] | sect[j + 1] | sect[j + 2] | sect[j + 3])
				used++;
	}
	if (used > 3)
		return 0;
	if (!memcmp(b + 82, "FAT32   ", 8)) {
		unsigned char fi[512];
		if (rd(rud, base + 512, fi, 512) == 0 && fi[0] == 0x52 && fi[1] == 0x52 &&
		    fi[2] == 0x61 && fi[3] == 0x41) {
			free_hint = tw_le32(fi + 488);
			if (clusters && free_hint && free_hint != 0xffffffffu)
				return free_hint + 8 >= clusters;
		}
	}
	return 1;
}

static int tw_fs_empty_exfat(const unsigned char *b, tw_read_fn rd, void *rud, uint64_t base)
{
	uint64_t heap_off, bitmap_off, root_off, cluster_bytes;
	uint32_t root_clus, bitmap_clus = 0, bitmap_len = 0;
	unsigned char sect[512];
	uint32_t i, nonzero = 0;
	(void)base;
	if (!tw_is_exfat(b))
		return 0;
	cluster_bytes = 1ull << (b[104] + b[105]);
	heap_off = tw_le64(b + 88) << 9;
	root_clus = tw_le32(b + 96);
	if (!heap_off || !root_clus || !cluster_bytes)
		return 0;
	root_off = heap_off + (uint64_t)(root_clus - 2) * cluster_bytes;
	for (i = 0; i < 64; i++) {
		unsigned et;
		if (rd(rud, root_off + (uint64_t)i * 32, sect, 32) < 0)
			return 0;
		et = sect[0];
		if (!et)
			break;
		if (et == 0x81) {
			bitmap_clus = tw_le32(sect + 20);
			bitmap_len = tw_le32(sect + 24);
			break;
		}
	}
	if (!bitmap_clus || !bitmap_len)
		return 0;
	bitmap_off = heap_off + (uint64_t)(bitmap_clus - 2) * cluster_bytes;
	if (bitmap_len > 65536)
		bitmap_len = 65536;
	for (i = 0; i < bitmap_len; i += 512) {
		unsigned n = bitmap_len - i > 512 ? 512 : bitmap_len - i, k;
		if (rd(rud, bitmap_off + i, sect, n) < 0)
			return 0;
		for (k = 0; k < n; k++)
			if (sect[k])
				nonzero++;
	}
	return nonzero <= 8;
}

static int tw_fs_empty(const unsigned char *b, tw_read_fn rd, void *rud, uint64_t base)
{
	if (tw_is_exfat(b))
		return tw_fs_empty_exfat(b, rd, rud, base);
	if (tw_is_fat(b))
		return tw_fs_empty_fat(b, rd, rud, base);
	return 0;
}

#define TW_FS_NONE 0
#define TW_FS_EMPTYABLE 1
#define TW_FS_OTHER 2

static int tw_fs_kind(const unsigned char *b)
{
	if (tw_is_fat(b) || tw_is_exfat(b))
		return TW_FS_EMPTYABLE;
	if (tw_is_ntfs(b))
		return TW_FS_OTHER;
	if (b[1080] == 0x53 && b[1081] == 0xef)
		return TW_FS_OTHER;
	if (!memcmp(b, "XFSB", 4))
		return TW_FS_OTHER;
	if (!memcmp(b + 1024, "H+", 2) || !memcmp(b + 1024, "HX", 2))
		return TW_FS_OTHER;
	return TW_FS_NONE;
}

typedef struct {
	int kind;
	int parts;
	char desc[256];
} tw_verdict;

static void tw_classify(tw_read_fn rd, void *rud, uint64_t tsize, tw_verdict *v)
{
	unsigned char mbr[512], gpth[512], sect[2048];
	uint64_t starts[128], numls[128];
	int nparts = 0, i, empty_all = 1, one_big_empty = 0;
	uint64_t big_num = 0;
	memset(v, 0, sizeof(*v));
	if (rd(rud, 0, mbr, 512) < 0) {
		v->kind = TW_KIND_DATA;
		snprintf(v->desc, sizeof(v->desc), "cannot read the drive");
		return;
	}
	if (rd(rud, 512, gpth, 512) < 0)
		memset(gpth, 0, sizeof(gpth));
	if (!memcmp(gpth, "EFI PART", 8)) {
		uint64_t entries = tw_le64(gpth + 72);
		uint32_t elen = tw_le32(gpth + 84);
		uint64_t estar = tw_le64(gpth + 80);
		if (elen < 128)
			elen = 128;
		if (entries > 128)
			entries = 128;
		for (i = 0; i < (int)entries; i++) {
			unsigned char ent[128];
			uint64_t z = 0;
			int k;
			if (rd(rud, estar * 512 + (uint64_t)i * elen, ent, 128) < 0)
				break;
			for (k = 0; k < 16; k++)
				z |= ent[k];
			if (!z)
				continue;
			starts[nparts] = tw_le64(ent + 32);
			numls[nparts] = tw_le64(ent + 40) - tw_le64(ent + 32) + 1;
			nparts++;
		}
	} else if (mbr[510] == 0x55 && mbr[511] == 0xaa) {
		for (i = 0; i < 4; i++) {
			const unsigned char *p = mbr + 446 + i * 16;
			uint32_t s = tw_le32(p + 8), n = tw_le32(p + 12);
			if (p[4] && p[4] != 0xee && s && n) {
				starts[nparts] = s;
				numls[nparts] = n;
				nparts++;
			}
		}
	}
	if (!nparts) {
		int nonzero = 0;
		int fsk;
		for (i = 0; i < 512; i++)
			if (mbr[i])
				nonzero++;
		if (rd(rud, 0, sect, 2048) < 0)
			memset(sect, 0, sizeof(sect));
		fsk = tw_fs_kind(sect);
		if (fsk == TW_FS_EMPTYABLE && tw_fs_empty(sect, rd, rud, 0)) {
			v->kind = TW_KIND_FRESH;
			snprintf(v->desc, sizeof(v->desc), "freshly formatted, no files");
		} else if (fsk == TW_FS_NONE && !nonzero) {
			v->kind = TW_KIND_BLANK;
			snprintf(v->desc, sizeof(v->desc), "blank drive");
		} else {
			v->kind = TW_KIND_DATA;
			snprintf(v->desc, sizeof(v->desc), "may contain data");
		}
		return;
	}
	for (i = 0; i < nparts; i++) {
		int fsk;
		if (rd(rud, starts[i] * 512, sect, 2048) < 0) {
			empty_all = 0;
			continue;
		}
		fsk = tw_fs_kind(sect);
		if (fsk == TW_FS_EMPTYABLE) {
			if (!tw_fs_empty(sect, rd, rud, starts[i] * 512))
				empty_all = 0;
		} else if (fsk != TW_FS_NONE) {
			empty_all = 0;
		}
		if (numls[i] > big_num)
			big_num = numls[i];
	}
	if (nparts == 1 && empty_all && starts[0] * 512 <= (1u << 20) &&
	    tsize && big_num * 512 >= tsize - (tsize >> 6))
		one_big_empty = 1;
	v->parts = nparts;
	if (one_big_empty) {
		v->kind = TW_KIND_FRESH;
		snprintf(v->desc, sizeof(v->desc), "freshly formatted, no files");
	} else if (empty_all && nparts <= 2) {
		v->kind = TW_KIND_SCRATCH;
		snprintf(v->desc, sizeof(v->desc), "%d empty partition%s, no files", nparts,
			 nparts == 1 ? "" : "s");
	} else if (empty_all) {
		v->kind = TW_KIND_SCRATCH;
		snprintf(v->desc, sizeof(v->desc), "%d empty partitions, no files", nparts);
	} else {
		v->kind = TW_KIND_DATA;
		snprintf(v->desc, sizeof(v->desc), "%d partition%s, may contain data", nparts,
			 nparts == 1 ? "" : "s");
	}
}

#ifdef EMBED_IMG
__asm__(".text\n.balign 8\n.globl tw_img_blob\ntw_img_blob:\n.incbin \"img.gz\"\n.globl tw_img_blob_end\ntw_img_blob_end:\n");
extern const unsigned char tw_img_blob[], tw_img_blob_end[];
#endif

typedef struct {
	const unsigned char *data;
	size_t len;
	int owned;
} tw_image;

static int tw_slurp(const char *path, tw_image *img)
{
	FILE *f = fopen(path, "rb");
	long sz;
	img->data = NULL;
	img->len = 0;
	img->owned = 0;
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0) {
		fclose(f);
		return -1;
	}
	img->data = malloc((size_t)sz);
	if (!img->data) {
		fclose(f);
		return -1;
	}
	if (fread((void *)img->data, 1, (size_t)sz, f) != (size_t)sz) {
		free((void *)img->data);
		img->data = NULL;
		fclose(f);
		return -1;
	}
	fclose(f);
	img->len = (size_t)sz;
	img->owned = 1;
	return 0;
}

static int tw_exe_path(char *out, size_t cap)
{
#ifdef _WIN32
	DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
	if (!n || n >= cap)
		return -1;
#elif defined(__APPLE__)
	uint32_t n = (uint32_t)cap;
	if (_NSGetExecutablePath(out, &n) != 0)
		return -1;
#else
	ssize_t n = readlink("/proc/self/exe", out, cap - 1);
	if (n <= 0)
		return -1;
	out[n] = 0;
#endif
	return 0;
}

static int tw_exe_dir(char *out, size_t cap)
{
	char *s;
	if (tw_exe_path(out, cap) < 0)
		return -1;
#ifdef _WIN32
	s = strrchr(out, '\\');
#else
	s = strrchr(out, '/');
#endif
	if (!s)
		return -1;
	*s = 0;
	return 0;
}

static int tw_load_image(const char *explicit_path, tw_image *img)
{
	if (explicit_path)
		return tw_slurp(explicit_path, img);
#ifdef EMBED_IMG
	img->data = tw_img_blob;
	img->len = (size_t)(tw_img_blob_end - tw_img_blob);
	img->owned = 0;
	return 0;
#else
	{
		char buf[4096];
		FILE *probe;
		if (tw_exe_dir(buf, sizeof(buf)) == 0) {
			strncat(buf, "/tinyhat.img.gz", sizeof(buf) - strlen(buf) - 1);
			if (tw_slurp(buf, img) == 0)
				return 0;
		}
		probe = fopen("tinyhat.img.gz", "rb");
		if (probe) {
			fclose(probe);
			return tw_slurp("tinyhat.img.gz", img);
		}
	}
	return -1;
#endif
}

typedef int (*tw_ask_fn)(void *ud, const char *text);
typedef void (*tw_note_fn)(void *ud, const char *line);

typedef struct {
	tw_ask_fn ask;
	tw_note_fn note;
	void *ud;
} tw_hooks;

typedef struct {
	tw_dev *dev;
	uint64_t off;
	uint32_t crc;
	uint64_t written;
	uint64_t total;
	const tw_hooks *hk;
	int err;
	int last_pct;
} tw_writer;

static int tw_write_emit(void *ud, const unsigned char *p, size_t n)
{
	tw_writer *w = (tw_writer *)ud;
	char line[64];
	if (w->err)
		return -1;
	if (tw_dev_writeat(w->dev, w->off, p, n) < 0) {
		w->err = 1;
		return -1;
	}
	w->off += n;
	w->crc = tw_crc_update(w->crc, p, n);
	w->written += n;
	if (w->total) {
		int pct = (int)(w->written * 100 / w->total);
		if (pct != w->last_pct) {
			w->last_pct = pct;
			snprintf(line, sizeof(line), "PROGRESS %llu",
				 (unsigned long long)w->written);
			w->hk->note(w->hk->ud, line);
		}
	}
	return 0;
}

static void tw_unmount_target(const char *target)
{
#ifdef _WIN32
	char drives[512];
	char vol[32];
	const char *pd = strstr(target, "PhysicalDrive");
	int disk = pd ? atoi(pd + 13) : -1;
	if (disk < 0)
		return;
	if (!GetLogicalDriveStringsA(sizeof(drives) - 1, drives))
		return;
	for (char *d = drives; *d; d += strlen(d) + 1) {
		char vpath[8];
		DWORD got;
		VOLUME_DISK_EXTENTS *ext;
		unsigned char buf[sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 8];
		int match = 0;
		unsigned i;
		snprintf(vpath, sizeof(vpath), "\\\\.\\%c:", d[0]);
		HANDLE hv = CreateFileA(vpath, GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hv == INVALID_HANDLE_VALUE)
			continue;
		ext = (VOLUME_DISK_EXTENTS *)buf;
		if (DeviceIoControl(hv, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, buf,
				    sizeof(buf), &got, NULL)) {
			for (i = 0; i < ext->NumberOfDiskExtents; i++)
				if ((int)ext->Extents[i].DiskNumber == disk)
					match = 1;
		}
		if (match) {
			int tries;
			for (tries = 0; tries < 3; tries++) {
				if (DeviceIoControl(hv, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &got, NULL))
					break;
				Sleep(300);
			}
			DeviceIoControl(hv, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &got, NULL);
		}
		CloseHandle(hv);
	}
	(void)vol;
#elif defined(__linux__)
	FILE *f = fopen("/proc/mounts", "r");
	char dev[256], mnt[256];
	if (!f)
		return;
	while (fscanf(f, "%255s %255s %*s %*s %*d %*d\n", dev, mnt) == 2) {
		if (!strncmp(dev, target, strlen(target)))
			umount2(mnt, MNT_DETACH);
	}
	fclose(f);
#else
	char cmd[512];
	FILE *p;
	const char *rdev = target;
	if (strstr(target, "/dev/rdisk"))
		rdev = target + 2;
	snprintf(cmd, sizeof(cmd), "diskutil unmountDisk force %s >/dev/null 2>&1", rdev);
	p = popen(cmd, "r");
	if (p)
		pclose(p);
#endif
}

static int tw_do_write(const tw_image *img, const char *target, const tw_hooks *hk)
{
	tw_dev dev;
	tw_verdict v;
	tw_writer w;
	tw_gz gz;
	char line[512];
	uint32_t want_crc = tw_gz_crc_of(img->data, img->len);
	uint32_t want_size = tw_gz_size_of(img->data, img->len);
	uint64_t tsize;
	snprintf(line, sizeof(line), "SIZE %u", want_size);
	hk->note(hk->ud, line);
	if (tw_dev_open(&dev, target, 1) < 0) {
		hk->note(hk->ud, "ERROR cannot open the target");
		return 1;
	}
	tsize = tw_dev_size(&dev);
	tw_classify(tw_dev_read_cb, &dev, tsize, &v);
	if (v.kind == TW_KIND_DATA) {
		char ask[512];
		snprintf(ask, sizeof(ask), "The target (%s) is not blank: %s. Erase everything on it?",
			 target, v.desc);
		if (!hk->ask(hk->ud, ask)) {
			hk->note(hk->ud, "ERROR canceled");
			tw_dev_close(&dev);
			return 1;
		}
	}
	tw_unmount_target(target);
	memset(&w, 0, sizeof(w));
	w.dev = &dev;
	w.total = want_size;
	w.hk = hk;
	w.last_pct = -1;
	memset(&gz, 0, sizeof(gz));
	gz.src = img->data;
	gz.srclen = img->len;
	if (tw_gz_inflate(&gz, tw_write_emit, &w) < 0 || w.err) {
		hk->note(hk->ud, "ERROR write failed");
		tw_dev_close(&dev);
		return 1;
	}
	if (strstr(target, "PhysicalDrive") && (w.written & 511)) {
		unsigned char pad[512];
		memset(pad, 0, sizeof(pad));
		tw_dev_writeat(&dev, w.off, pad, 512 - (size_t)(w.written & 511));
	}
	tw_dev_flush(&dev);
	tw_dev_close(&dev);
	if (w.crc != want_crc) {
		hk->note(hk->ud, "ERROR checksum mismatch");
		return 1;
	}
	hk->note(hk->ud, "DONE");
	return 0;
}

#endif
