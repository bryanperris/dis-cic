#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>


static const int OLD = 0;


typedef unsigned char u8;
typedef unsigned short u16;

static u8 *data;
static u8 *stream;

static u8 m[32], a, x, c, bm, bl, pch, pcl, p[4], stack[8];
static u8 din, dout;

static int skip, jumped, t, done;

static u8 next_off(u8 off)
{
	u8 bit = ((off >> 1) ^ off) & 1;
	return (off >> 1) ^ (bit << 6) ^ 0x40;
}

static void addr(u8 seg, u8 off)
{
	printf("%x%02x", seg, off);
}

static u8 fetch(void)
{
	return data[128*pch + pcl];
}

static u8 fetch1(void)
{
	return data[128*pch + next_off(pcl)];
}

static int len(void)
{
	u8 insn = fetch();
	if (insn >= 0x78 && insn < 0x80)
		return 2;
	return 1;
}

static void x4(u8 insn)
{
	printf(" %x", insn & 0x0f);
}

static void x2(u8 insn)
{
	printf(" %x", insn & 0x03);
}

static u8 readmem(void)
{
	if (bm > 1) {
		printf("BAD READ\n");
		exit(2);
	}

	return m[16*bm + bl];
}

static void writemem(u8 x)
{
	if (bm > 1) {
		printf("BAD WRITE\n");
		exit(2);
	}

	m[16*bm + bl] = x;
}

static u8 readport(void)
{
	if (bl > 3) {
		printf("BAD IN\n");
		exit(2);
	}

	return p[bl];
}

static void writeport(u8 x)
{
	if (bl > 3) {
		printf("BAD OUT\n");
		exit(2);
	}

	p[bl] = x;
}

static void push(void)
{
	int i;

	for (i = 7; i >= 2; i--)
		stack[i] = stack[i - 2];

	stack[0] = pch;
	stack[1] = next_off(next_off(pcl));
}

static void pull(void)
{
	int i;

	pch = stack[0];
	pcl = stack[1];

	for (i = 0; i < 6; i++)
		stack[i] = stack[i + 2];

}

static void longjump(u8 insn)
{
	pcl = fetch1();
	pch = 2*(insn & 3);

	if (pcl & 128) {
		pch++;
		pcl &= 127;
	}
//printf("{%x%02x}", pch, pcl);

	jumped = 1;
}

static void longc(u8 insn)
{
	u8 off = fetch1();
	u8 seg = 2*(insn & 3);
	if (off & 128)
		seg++;
	off &= 127;
	printf(" ");
	addr(seg, off);
}

static void print_insn(void)
{
	u8 insn = fetch();

	switch (insn) {
	case 0x00: printf("nop"); break;

	case 0x01 ... 0x0f: printf("adi"); x4(insn); break;
	case 0x10 ... 0x1f: printf("skai"); x4(insn); break;
	case 0x20 ... 0x2f: printf("lbli"); x4(insn); break;
	case 0x30 ... 0x3f: printf("ldi"); x4(insn); break;

	case 0x40: printf("l"); break;
	case 0x41: printf("x"); break;
	case 0x42: printf("xi"); break;
	case 0x43: printf("xd"); break;

	case 0x44: printf("nega"); break;
	case 0x46: printf("out"); break;	// XXX: find a better name?
	case 0x47: printf("out0"); break;	// XXX: find a better name?

	case 0x48: printf("sc"); break;
	case 0x49: printf("rc"); break;
	case 0x4a: printf("s"); break;

	case 0x4c: printf("rit"); break;
	case 0x4d: printf("ritsk"); break;

	case 0x52: printf("li"); break;

	case 0x54: printf("coma"); break;
	case 0x55: printf("in"); break;
	case 0x57: printf("xal"); break;

	case 0x5c: printf("lxa"); break;
	case 0x5d: printf("xax"); break;

	case 0x60 ... 0x63: printf("skm"); x2(insn); break;
	case 0x64 ... 0x67: printf("ska"); x2(insn); break;
	case 0x68 ... 0x6b: printf("rm"); x2(insn); break;
	case 0x6c ... 0x6f: printf("sm"); x2(insn); break;

	case 0x70: printf("ad"); break;
	case 0x72: printf("adc"); break;
	case 0x73: printf("adcsk"); break;

	case 0x74 ... 0x77: printf("lbmi"); x2(insn); break;

	case 0x78 ... 0x7b: printf("tl"); longc(insn); break;
	case 0x7c ... 0x7f: printf("tml"); longc(insn); break;

	case 0x80 ... 0xff: printf("t "); addr(pch, insn & 127); break;

	default:
		printf("???");
	}
}

static void dostep(void)
{
	u8 insn = fetch();
	u8 m = readmem();

	switch (insn) {
	case 0x00 ... 0x0f: a += insn & 15; if (a > 15) { skip = 1; a &= 15; } break;
	case 0x10 ... 0x1f: if (a == (insn & 15)) skip = 1; break;
	case 0x20 ... 0x2f: bl = insn & 15; break;
	case 0x30 ... 0x3f: a = insn & 15; break;

	case 0x40: a = m; break;
	case 0x41: writemem(a); a = m; break;
	case 0x42: writemem(a); a = m; bl++; if (bl > 15) { skip = 1; bl &= 15; } break;
//	case 0x43: printf("xd"); break;

	case 0x44: a = (15 - a) + (1-OLD); if (a > 15) { skip = 1; a &= 15; } break;
	case 0x46: writeport(a); break;
	case 0x47: writeport(0); break;

	case 0x48: c = (1-OLD); break;
	case 0x49: c = OLD; break;
	case 0x4a: writemem(a); break;

	case 0x4c ... 0x4d: pull(); jumped = 1; skip = insn & 1; break;

	case 0x52: a = m; bl++; if (bl > 15) { skip = 1; bl &= 15; } break;

	case 0x54: a = (15 - a) + OLD; if (a > 15) { skip = 1; a &= 15; } break;
	case 0x55: a = readport(); break;
	case 0x57: m = a; a = bl; bl = m; break;

	case 0x5c: x = a; break;
	case 0x5d: m = a; a = x; x = m; break;

	case 0x60 ... 0x63: if (m & (1 << (insn & 3))) skip = 1; break;
	case 0x64 ... 0x67: if (a & (1 << (insn & 3))) skip = 1; break;
	case 0x68 ... 0x6b: writemem(m & ~(1 << (insn & 3))); break;
//	case 0x6c ... 0x6f: printf("sm"); x2(insn); break;

	case 0x70: a += m; a &= 15; break;
	case 0x72: a += m + c; a &= 15; break;
	case 0x73: a += m + c; if (a > 15) { skip = 1; a &= 15; } break;

	case 0x74 ... 0x77: bm = insn & 3; break;

	case 0x78 ... 0x7b: longjump(insn); break;
	case 0x7c ... 0x7f: push(); longjump(insn); break;

	case 0x80 ... 0xff: pcl = insn & 127; jumped = 1; break;

	default:
		printf("BAD DOG, FIXME\n");
		exit(1);
	}
}

static void print_line_header(void)
{
	int i;

	printf("%6d  ", t);

	for (i = 0; i < 4; i++)
		printf("%x", p[i]);
	printf(" ");

	for (i = 0; i < 16; i++)
		printf("%x", m[i]);
	printf(" ");

	for (i = 0; i < 16; i++)
		printf("%x", m[16 + i]);
	printf(" ");

	printf("%x %x %x%x %c ", x, a, bm, bl, skip ? 'S' : ' ');

	addr(pch, pcl);
	printf(":");

	printf(" %02x", fetch());

	if (len() == 2)
		printf(" %02x", fetch1());
	else
		printf("   ");

	printf("   ");
}

static u8 *map_file(const char *name)
{
	int fd = open(name, O_RDONLY);
	int data_len = lseek(fd, 0, SEEK_END);
	void *map = mmap(0, data_len, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	return map;
}

static void step(void)
{
	int t3 = t + 4;

	if (t3 >= 524288) {
		printf("that's all folks! (NOT BAD)\n");
		done = 1;
		return;
	}

	int n = (t3 / 8) * 2;
	din = (stream[n] >> (t3 % 8)) & 1;
	dout = (stream[n+1] >> (t3 % 8)) & 1;
	p[0] = (p[0] & 13) | (2 * din);
	if (dout != (p[0] & 1))
		printf("BADBADBAD: ");
	printf("%d-%d=%d ", din, dout, p[0] & 1);

	print_line_header();
	print_insn();
	printf("\n");

	int l = len();
//printf("[%d,%d,", l, skip);
	if (skip)
		skip = 0;
	else
		dostep();

//printf("%d]", jumped);

	t += l;

	if (jumped)
		jumped = 0;
	else
		while (l--)
			pcl = next_off(pcl);
}

int main(int argc, const char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s <bin> <stream>\n", argv[0]);
		exit(1);
	}

	data = map_file(argv[1]);
	stream = map_file(argv[2]);

	do {
		step();
	} while (!done);

	return 0;
}
