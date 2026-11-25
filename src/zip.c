/* The .ipg is a zip. Its central directory is read once; a member is read and, if deflated,
 * inflated into memory when the game asks for it, so the 35 MB file is never held whole.
 *
 * inflate_raw is a raw-deflate decoder after zlib's puff (Mark Adler's reference decoder):
 * stored, fixed and dynamic blocks, every length and distance checked against the buffers. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "vx.h"

/* ---------------------------------------------------------------------------------------------
 * Inflate */

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define MAXCODES (MAXLCODES + MAXDCODES)
#define FIXLCODES 288

typedef struct {
    const uint8_t *in;
    unsigned inlen, incnt;
    uint8_t *out;
    unsigned outlen, outcnt;
    int bitbuf, bitcnt;
    int err;
} St;

typedef struct {
    short *count, *symbol;
} Huff;

static int bits(St *s, int need) {
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt == s->inlen) {
            s->err = 1;
            return 0;
        }
        val |= (long)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static int stored(St *s) {
    s->bitbuf = 0, s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return 2;
    unsigned len = s->in[s->incnt] | s->in[s->incnt + 1] << 8;
    unsigned nlen = s->in[s->incnt + 2] | s->in[s->incnt + 3] << 8;
    s->incnt += 4;
    if (len != (~nlen & 0xffff)) return -2;
    if (s->incnt + len > s->inlen) return 2;
    if (s->outcnt + len > s->outlen) return 1;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->outcnt += len, s->incnt += len;
    return 0;
}

static int decode(St *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (s->err) return -10;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -10;
}

static int construct(Huff *h, const short *length, int n) {
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static int codes(St *s, const Huff *lencode, const Huff *distcode) {
    static const short lens[29] = { 3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dists[30] = { 1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                     193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    int symbol;
    do {
        symbol = decode(s, lencode);
        if (symbol < 0) return symbol;
        if (symbol < 256) {
            if (s->outcnt == s->outlen) return 1;
            s->out[s->outcnt++] = (uint8_t)symbol;
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) return -10;
            int len = lens[symbol] + bits(s, lext[symbol]);
            symbol = decode(s, distcode);
            if (symbol < 0) return symbol;
            if (symbol >= 30) return -10;
            unsigned dist = (unsigned)(dists[symbol] + bits(s, dext[symbol]));
            if (s->err) return 2;
            if (dist > s->outcnt) return -11;
            if (s->outcnt + (unsigned)len > s->outlen) return 1;
            uint8_t *o = s->out + s->outcnt;
            for (int i = 0; i < len; i++) o[i] = o[i - (int)dist];
            s->outcnt += (unsigned)len;
        }
    } while (symbol != 256);
    return 0;
}

static int fixed(St *s) {
    static int virgin = 1;
    static short lencnt[MAXBITS + 1], lensym[FIXLCODES];
    static short distcnt[MAXBITS + 1], distsym[MAXDCODES];
    static Huff lencode, distcode;
    if (virgin) {
        short lengths[FIXLCODES];
        lencode.count = lencnt, lencode.symbol = lensym;
        distcode.count = distcnt, distcode.symbol = distsym;
        int symbol = 0;
        for (; symbol < 144; symbol++) lengths[symbol] = 8;
        for (; symbol < 256; symbol++) lengths[symbol] = 9;
        for (; symbol < 280; symbol++) lengths[symbol] = 7;
        for (; symbol < FIXLCODES; symbol++) lengths[symbol] = 8;
        construct(&lencode, lengths, FIXLCODES);
        for (symbol = 0; symbol < MAXDCODES; symbol++) lengths[symbol] = 5;
        construct(&distcode, lengths, MAXDCODES);
        virgin = 0;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic(St *s) {
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[MAXCODES];
    short lencnt[MAXBITS + 1], lensym[MAXLCODES];
    short distcnt[MAXBITS + 1], distsym[MAXDCODES];
    Huff lencode = { lencnt, lensym }, distcode = { distcnt, distsym };
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->err) return 2;
    if (nlen > MAXLCODES || ndist > MAXDCODES) return -3;
    int index = 0;
    for (; index < ncode; index++) lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err) return 2;
    if (construct(&lencode, lengths, 19) != 0) return -4;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        if (symbol < 0) return symbol;
        if (symbol < 16) lengths[index++] = (short)symbol;
        else {
            int len = 0;
            if (symbol == 16) {
                if (index == 0) return -5;
                len = lengths[index - 1];
                symbol = 3 + bits(s, 2);
            } else if (symbol == 17) symbol = 3 + bits(s, 3);
            else symbol = 11 + bits(s, 7);
            if (s->err) return 2;
            if (index + symbol > nlen + ndist) return -6;
            while (symbol--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -9;
    int err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return -7;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return -8;
    return codes(s, &lencode, &distcode);
}

int inflate_raw(const uint8_t *in, unsigned inlen, uint8_t *out, unsigned outlen) {
    St s = { in, inlen, 0, out, outlen, 0, 0, 0, 0 };
    int last, err;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.err) return 2;
        err = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (err) return err;
    } while (!last);
    return s.outcnt == outlen ? 0 : 3;
}

/* ---------------------------------------------------------------------------------------------
 * The zip's directory */

typedef struct {
    char name[96];
    unsigned method, csize, usize, local;
} Entry;

struct Zip {
    FILE *f;
    int n;
    Entry *e;
};

static unsigned u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static unsigned u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

Zip *zip_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long tail = size < 65557 ? size : 65557;
    uint8_t *t = malloc((size_t)tail);
    Zip *z = 0;
    if (!t || fseek(f, size - tail, SEEK_SET) || fread(t, 1, (size_t)tail, f) != (size_t)tail) goto out;
    long at = -1;
    for (long i = tail - 22; i >= 0; --i)
        if (u32(t + i) == 0x06054b50u) {
            at = i;
            break;
        }
    if (at < 0) goto out;
    unsigned count = u16(t + at + 10), dsize = u32(t + at + 12), doff = u32(t + at + 16);
    if ((long)doff + (long)dsize > size) goto out;
    uint8_t *d = malloc(dsize ? dsize : 1);
    if (!d || fseek(f, (long)doff, SEEK_SET) || fread(d, 1, dsize, f) != dsize) {
        free(d);
        goto out;
    }
    z = calloc(1, sizeof *z);
    z->e = calloc(count ? count : 1, sizeof *z->e);
    z->f = f;
    unsigned p = 0;
    for (unsigned i = 0; i < count && p + 46 <= dsize && u32(d + p) == 0x02014b50u; ++i) {
        unsigned nl = u16(d + p + 28), xl = u16(d + p + 30), cl = u16(d + p + 32);
        if (p + 46 + nl > dsize) break;
        Entry *e = &z->e[z->n];
        e->method = u16(d + p + 10);
        e->csize = u32(d + p + 20);
        e->usize = u32(d + p + 24);
        e->local = u32(d + p + 42);
        unsigned l = nl < sizeof e->name - 1 ? nl : sizeof e->name - 1;
        memcpy(e->name, d + p + 46, l);
        e->name[l] = 0;
        if (nl < sizeof e->name) z->n++;
        p += 46 + nl + xl + cl;
    }
    free(d);
out:
    free(t);
    if (!z) fclose(f);
    return z;
}

void zip_close(Zip *z) {
    if (!z) return;
    fclose(z->f);
    free(z->e);
    free(z);
}

static const Entry *find(Zip *z, const char *name) {
    for (int i = 0; i < z->n; ++i)
        if (!strcmp(z->e[i].name, name)) return &z->e[i];
    for (int i = 0; i < z->n; ++i) /* the iPod's file system took names in any case */
        if (!strcasecmp(z->e[i].name, name)) return &z->e[i];
    return 0;
}

int zip_has(Zip *z, const char *name) { return z && find(z, name) != 0; }

uint8_t *zip_read(Zip *z, const char *name, unsigned *size) {
    const Entry *e = z ? find(z, name) : 0;
    if (!e || (e->method != 0 && e->method != 8)) {
        logf_("zip: %s %s", name, e ? "uses a method this can't read" : "isn't in the .ipg");
        return 0;
    }
    uint8_t h[30];
    if (fseek(z->f, (long)e->local, SEEK_SET) || fread(h, 1, 30, z->f) != 30 || u32(h) != 0x04034b50u) return 0;
    long data = (long)e->local + 30 + u16(h + 26) + u16(h + 28);
    uint8_t *c = malloc(e->csize ? e->csize : 1), *u = malloc(e->usize ? e->usize : 1);
    if (!c || !u || fseek(z->f, data, SEEK_SET) || fread(c, 1, e->csize, z->f) != e->csize) {
        free(c), free(u);
        return 0;
    }
    int err = 0;
    if (e->method == 0) memcpy(u, c, e->usize < e->csize ? e->usize : e->csize);
    else err = inflate_raw(c, e->csize, u, e->usize);
    free(c);
    if (err) {
        logf_("zip: %s: inflate error %d", name, err);
        free(u);
        return 0;
    }
    if (size) *size = e->usize;
    return u;
}
