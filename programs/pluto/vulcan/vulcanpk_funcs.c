#include <sys/mman.h>
#include <dev/hifn/hifn7751reg.h>

#include "dev/hifn/vulcanpk_funcs.h"
#include <sys/types.h>
#include <fcntl.h>

//typedef int bool;

/*
 * Bus read/write barrier methods. (taken from i386/include/bus.h )
 *
 *	void bus_space_write_barrier(void)
 *
 *
 * Note that BUS_SPACE_BARRIER_WRITE doesn't do anything other than
 * prevent reordering by the compiler; all Intel x86 processors currently
 * retire operations outside the CPU in program order.
 */

static __inline void
bus_space_write_barrier(void)
{
  __asm __volatile("" : : : "memory");
}

static int vulcan_fd=-1;

unsigned char * mapvulcanpk(void)
{
	unsigned char *mapping;

	if(vulcan_fd == -1) {
	    vulcan_fd=open("/dev/vulcanpk", O_RDWR);
	    
	    if(vulcan_fd == -1) {
		perror("vulcan mapping open");
		exit(6);
	    }
	}

	/* HIFN_1_PUB_MEMEND */
	mapping = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, vulcan_fd, 0);
	
	if(mapping == NULL) {
		perror("mmap");
		exit(4);
	}
	
	return mapping;

}
void unmapvulcanpk(unsigned char *mapping)
{
    munmap(mapping, 4096);
    if(vulcan_fd!=-1) close(vulcan_fd);
    vulcan_fd=-1;
}

/* in include/, because you never know when you will need it */
#define hexdump_printf DBG_log
#include "hexdump.c"

void print_status(u_int32_t stat)
{
    char line[80], *b=line;
    line[0]='\0';
    
	b+=snprintf(b, 80, "status: %08x ", stat);
	if(stat & HIFN_PUBSTS_DONE) {
		b+=snprintf(b, 80, "done ");
	}
	if(stat & HIFN_PUBSTS_CARRY) {
		b+=snprintf(b, 80, "carry ");
	}
	if(stat & 0x4) {
		b+=snprintf(b, 80, "sign(2) ");
	}
	if(stat & 0x8) {
		b+=snprintf(b, 80, "zero(3) ");
	}
	if(stat & HIFN_PUBSTS_FIFO_EMPTY) {
		b+=snprintf(b, 80, "empty ");
	}
	if(stat & HIFN_PUBSTS_FIFO_FULL) {
		b+=snprintf(b, 80, "full ");
	}
	if(stat & HIFN_PUBSTS_FIFO_OVFL) {
		b+=snprintf(b, 80, "overflow ");
	}
	if(stat & HIFN_PUBSTS_FIFO_WRITE) {
		b+=snprintf(b, 80, "write=%d ", (stat & HIFN_PUBSTS_FIFO_WRITE)>>16);
	}
	if(stat & HIFN_PUBSTS_FIFO_READ) {
		b+=snprintf(b, 80, "read=%d ", (stat & HIFN_PUBSTS_FIFO_READ)>>24);
	}
	DBG_log(line);
}


#define PUB_WORD(offset) *(volatile u_int32_t *)(&mapping[offset])
#define PUB_WORD_WRITE(offset, value) do { if(pk_verbose_execute) DBG_log("write-1 %04x = %08x\n", offset, value); PUB_WORD(offset)=value; } while(0)

inline static void write_pkop(unsigned char *mapping,
		       u_int32_t oplen, u_int32_t op)
{
	volatile u_int32_t *opfifo;

	opfifo = (volatile u_int32_t *)(mapping+HIFN_1_PUB_FIFO_OPLEN);

	opfifo[0]=oplen;
	opfifo[1]=op;
}

#define PKVALUE_BITS  3072
#define PKVALUE_LEN   (PKVALUE_BITS/8)

#define PK_AVALUES 16
#define PK_BVALUES 16

struct pkprogram {
    bool           valuesLittleEndian;
    unsigned char  chunksize;           /* how many 64-byte chunks/register */
    unsigned char *aValues[PK_AVALUES];
    unsigned short aValueLen[PK_AVALUES];
    unsigned int   oOffset;
    unsigned char *oValue;
    unsigned int   oValueLen;
    u_int32_t      pk_program[32];
    int            pk_proglen;
};

int pk_verbose_execute=0;

/*
 * copies 32-bit non-overlapping quantities only.
 */
static inline void vulcan_pk_memcpy32(u_int32_t *dst, const u_int32_t *src, unsigned int len)
{
    unsigned int len4 = (len+3)/4;
    while(len4-->0) *dst++=*src++;
}

static void copyPkValueTo(unsigned char *mapping, struct pkprogram *prog,
		   const char *typeStr, 
		   int pkRegNum,
		   unsigned char *pkValue, unsigned short pkValueLen)
{
    int registerSize = prog->chunksize*64;
    unsigned int pkRegOff = HIFN_1_PUB_MEM + (pkRegNum*registerSize);
    unsigned char *pkReg = mapping + pkRegOff;

    /* do not screw with stuff beyond 1 page */
    if(pkRegOff > 4096) {
	return;
    }

	if(prog->valuesLittleEndian) {
		vulcan_pk_memcpy32((u_int32_t *)pkReg, (const u_int32_t *)pkValue, pkValueLen);
		memset(pkReg+pkValueLen, 0, (registerSize-pkValueLen));
	} else {
		int vi, vd;
		unsigned char pkRegTemp[PKVALUE_LEN];

		/*
		 * we use a temp area, because probably things go badly
		 * if we do byte accesses.
		 */
		memset(pkRegTemp, 0, PKVALUE_LEN);
		for(vd=pkValueLen-1, vi=0; vi<registerSize && vd >=0; vi++, vd--) {
			pkRegTemp[vi]=pkValue[vd];
		}
		/* copy to PK engine, using 32-bit operations only */
		vulcan_pk_memcpy32((u_int32_t *)pkReg, (const u_int32_t *)pkRegTemp, registerSize);
	}
	
	if(pk_verbose_execute) {
		DBG_log("%s[%d]: before\n", typeStr, pkRegNum);
		hexdump(mapping, pkRegOff, registerSize);
	}
}

static void copyPkValueFrom(unsigned char *mapping, struct pkprogram *prog,
		     const char *typeStr, 
		     int pkRegNum,
		     unsigned char *pkValue, unsigned short pkValueLen)
{
    int registerSize = prog->chunksize*64;
    unsigned int pkRegOff = HIFN_1_PUB_MEM + (pkRegNum*registerSize);
    unsigned char *pkReg = mapping + pkRegOff;

    if(prog->valuesLittleEndian) {
	memcpy(pkValue, pkReg, pkValueLen);
    } else {
	int vi, vd;

	unsigned char pkRegTemp[PKVALUE_LEN];

	/*
	 * we use a temp area, because probably things go badly
	 * if we do byte accesses.
	 */
	memcpy(pkRegTemp, pkReg, PKVALUE_LEN);

	memset(pkValue, 0, pkValueLen);
	for(vd=pkValueLen-1, vi=0; vi<registerSize && vd >=0; vi++, vd--) {
	    pkValue[vd]=pkRegTemp[vi];
	}
	
	if(pk_verbose_execute) {
		DBG_log("%s[%d]: after extract\n", typeStr, pkRegNum);
		hexdump(pkValue, 0, pkValueLen);
	}
    }
}

static void dump_registers(unsigned char *mapping, unsigned int registerSize)
{
    unsigned int pkNum;
    unsigned int maxregister = (HIFN_1_PUB_MEMSIZE/registerSize)-1;

    for(pkNum = 0;
	pkNum <= maxregister;
	pkNum++)
    {
	unsigned int pkRegOff = HIFN_1_PUB_MEM + (pkNum*registerSize);
	DBG_log("register[%d]\n", pkNum);
	hexdump(mapping, pkRegOff, registerSize);
    }
}



#if !defined(ENHANCED_MODE)
static inline u_int32_t xlat2compat_oplen(u_int32_t oplen)
{
	unsigned int red,exp,mod;
	red = (oplen >> 24)&0xff;
	exp = (oplen >> 8)&0xfff;
	mod = (oplen >> 0)&0xff;
	  
	oplen = ((red&0xf) << 18) | ((exp&0x7ff) << 7) | (mod & 0x7f);
	
	return oplen;
}

static inline u_int32_t xlat2compat_op(u_int32_t op)
{
	unsigned int opcode,m,b,a;
	opcode = (op>>24)&0xff;
	m      = (op>>16)&0xff;
	b      = (op>>8)&0xff;
	a      = op & 0xff;
	
	op = (opcode << 18)|(m<<12)|(b<<6)|(a<<0);
	
	/* assert that "opcode" is not invalid, may be good */
	return op;
}
#endif

void execute_pkprogram(unsigned char *mapping, struct pkprogram *prog)
{
	/* make sure PK engine is done */
    unsigned int registerSize = prog->chunksize*64;
	int count=5;
	int i;
	volatile u_int32_t stat;
#if !defined(ENHANCED_MODE)
	volatile u_int32_t *opfifo;
#endif
	int pc;

#if 0
	while(count-->0 &&
	      ((stat = PUB_WORD(HIFN_1_PUB_STATUS)) & HIFN_PUBSTS_DONE) != HIFN_PUBSTS_DONE) {
		usleep(1000);
	}
	if(count == 0) {
	    openswan_log("pk_pubsts_done failed to complete: %08x\n", stat);
		exit(6);
	}
#endif

	DBG_log("# mapping is at %p\n", mapping);

	/*
	 * copy source operands into memory, clearing other parts.
	 * hopefully, will turn into a single PCI burst write.
	 */
	for(i=0; i<PK_AVALUES; i++) {
	    if(prog->aValues[i] != NULL) {
		copyPkValueTo(mapping, prog, "a", i, prog->aValues[i], prog->aValueLen[i]);
	    } else {
		unsigned char *pkReg = mapping + HIFN_1_PUB_MEM + (i*registerSize);
		if((pkReg+registerSize) < (mapping + 4096)) {
		    /* clear memory */
		    memset(pkReg, 0, registerSize);
		}
	    }
	}
	
	/* a write barrier, and a cache flush would be good idea here */
	bus_space_write_barrier();
	usleep(1000);

	/* now copy the instructions to the FIFO. */

	pc = 0;

#if !defined(ENHANCED_MODE)
	/*
	 * oops. FIFO is broken, so write them out to oplen/op,
	 * after converting them to compat mode instructions.
	 */
	opfifo = (volatile u_int32_t *)(mapping+HIFN_1_PUB_OPLEN);
	if(pk_verbose_execute) print_status(PUB_WORD(HIFN_1_PUB_STATUS));
	PUB_WORD_WRITE(HIFN_1_PUB_STATUS, PUB_WORD(HIFN_1_PUB_STATUS));
	if(pk_verbose_execute) print_status(PUB_WORD(HIFN_1_PUB_STATUS));

	while(pc < prog->pk_proglen) {
	    u_int32_t op, oplen;

	    oplen = prog->pk_program[pc];
	    op    = prog->pk_program[pc+1];

	    if(pk_verbose_execute) {
		print_status(PUB_WORD(HIFN_1_PUB_STATUS));
		DBG_log("original instruction at %d oplen=%08x/op=%08x\n",
		       pc, oplen, op);
	    }

	    oplen = xlat2compat_oplen(prog->pk_program[pc++]);
	    op = xlat2compat_op(prog->pk_program[pc++]);
	    
	    if(pk_verbose_execute) {
		DBG_log("executing instruction %d (of %d) (%08x/%08x)\n",
		       pc, prog->pk_proglen, oplen, op);
	    }
	    opfifo[0]=oplen;
	    opfifo[1]=op;

	    if(pc < prog->pk_proglen) {
		count=5;
		while(--count>0 &&
		      ((stat = PUB_WORD(HIFN_1_PUB_STATUS)) & HIFN_PUBSTS_DONE) != HIFN_PUBSTS_DONE) {
		    usleep(1000);
		}
	    }
	    pc+=2;
	}
#else
	while(pc < prog->pk_proglen) {
	    u_int32_t op, oplen;

	    oplen = prog->pk_program[pc];
	    op    = prog->pk_program[pc+1];

	    if(pk_verbose_execute) {
		print_status(PUB_WORD(HIFN_1_PUB_STATUS));
		DBG_log("instruction at %d oplen=%08x/op=%08x\n",
		       pc, oplen, op);
	    }
	    
	    write_pkop(mapping, oplen, op);
	    pc+=2;
	}
#endif

	bus_space_write_barrier();
	usleep(1000);
	/* wait for DONE bit */
	if(pk_verbose_execute) print_status(PUB_WORD(HIFN_1_PUB_STATUS));
	count=200;
	
	while(--count>0 &&
	      ((stat = PUB_WORD(HIFN_1_PUB_STATUS)) & HIFN_PUBSTS_DONE) != HIFN_PUBSTS_DONE) {
	    usleep(1000);
	}
	if(count == 0) {
	    openswan_log("failed to complete operation: %08x\n", stat);
	    print_status(stat);
	    exit(6);
	}

	if(pk_verbose_execute) {
	    openswan_log("after running (%d):\n", prog->chunksize);
	    dump_registers(mapping, prog->chunksize*64);
	}
	    
	/* output is usually in b[1] */
	copyPkValueFrom(mapping, prog, "a", prog->oOffset,
			prog->oValue, prog->oValueLen);
}

void vulcanpk_init(unsigned char *mapping)
{
        volatile unsigned int stat;
	int count=60;

	PUB_WORD_WRITE(HIFN_1_PUB_RESET, 0x1);
	
	while(count-->0 && (stat = PUB_WORD(HIFN_1_PUB_RESET)) & 0x01) {
		sleep(1);
	}
	if(count==0) {
	    openswan_log("failed to reset Vulcan PK engine\n");
	    exit(7);
	}

	if(getenv("VULCAN_PKMMAP_VERBOSE")) {
	    pk_verbose_execute=1;
	    setbuf(stderr, NULL);
	    setbuf(stdout, NULL);
	}


#if defined(ENHANCED_MODE)
	PUB_WORD_WRITE(HIFN_1_PUB_MODE, PUB_WORD(HIFN_1_PUB_MODE)|HIFN_PKMODE_ENHANCED);
	DBG_log("# Running vulcan PK engine in enhanced mode pubmode=%08x\n", PUB_WORD(HIFN_1_PUB_MODE));
#else
	PUB_WORD_WRITE(HIFN_1_PUB_MODE, PUB_WORD(HIFN_1_PUB_MODE)&~HIFN_PKMODE_ENHANCED);
	DBG_log("# Running vulcan PK engine in compat mode pubmode=%08x\n", PUB_WORD(HIFN_1_PUB_MODE));
#endif

	/* enable RNG again */
	PUB_WORD_WRITE(HIFN_1_RNG_CONFIG, PUB_WORD(HIFN_1_RNG_CONFIG) | HIFN_RNGCFG_ENA);

	/* clear out PUBLIC DONE */
	PUB_WORD_WRITE(HIFN_1_PUB_STATUS, PUB_WORD(HIFN_1_PUB_STATUS));

	/* clear out public key memory */
	memset((unsigned char *)mapping+HIFN_1_PUB_MEM, 0, HIFN_1_PUB_MEMSIZE);

	/* verify that memory cleared */
	{
		int i;
		volatile unsigned int *n = (volatile unsigned int *)&mapping[HIFN_1_PUB_MEM];
		for(i=HIFN_1_PUB_MEM; i< HIFN_1_PUB_MEMEND; i+=4) {
			if(*n!=0) {
				DBG_log("failed to clear pubkey memory at offset=%04x (%04x,%04x) (value=%02x)\n", i, HIFN_1_PUB_MEM, HIFN_1_PUB_MEMEND, mapping[i]);
				exit(4);
			}
			n++;
		}
	}
	DBG_log("# public key memory cleared properly\n");
}

/*
 * Local Variables:
 * c-basic-offset:4
 * c-style: pluto
 * End:
 */
