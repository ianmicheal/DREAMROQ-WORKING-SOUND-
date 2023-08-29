/*
 * Dreamroq by Mike Melanson
 *
 * This is the header file to be included in the programs wishing to
 * use the Dreamroq playback engine.
 */

#ifndef NEWROQ_H
#define NEWROQ_H

#define ROQ_SUCCESS           0
#define ROQ_FILE_OPEN_FAILURE 1
#define ROQ_FILE_READ_FAILURE 2
#define ROQ_CHUNK_TOO_LARGE   3
#define ROQ_BAD_CODEBOOK      4
#define ROQ_INVALID_PIC_SIZE  5
#define ROQ_NO_MEMORY         6
#define ROQ_BAD_VQ_STREAM     7
#define ROQ_INVALID_DIMENSION 8
#define ROQ_RENDER_PROBLEM    9
#define ROQ_CLIENT_PROBLEM    10
#define RoQ_INFO           0x1001
#define RoQ_QUAD_CODEBOOK  0x1002
#define RoQ_QUAD_VQ        0x1011
#define RoQ_SOUND_MONO     0x1020
#define RoQ_SOUND_STEREO   0x1021
#define RoQ_SIGNATURE      0x1084

#define CHUNK_HEADER_SIZE 8

#define LE_16(buf) (*buf | (*(buf+1) << 8))
#define LE_32(buf) (*buf | (*(buf+1) << 8) | (*(buf+2) << 16) | (*(buf+3) << 24))

#define MAX_BUF_SIZE (64 * 2048)


#define ROQ_CODEBOOK_SIZE 256
// Optmized math functions from DreamhaL moop my mentor 
// This math module is hereby released into the public domain in the hope that it
// may prove useful. Now go hit 60 fps! :)
//
// --Moopthehedgehog, January 2020
static inline __attribute__((always_inline)) float MATH_fmac(float a, float b, float c)
{
  __asm__ __volatile__ ("fmac fr0, %[floatb], %[floatc]\n"
    : [floatc] "+f" (c) // outputs, "+" means r/w
    : "w" (a), [floatb] "f" (b) // inputs
    : // no clobbers
  );

  return c;
}

static inline void CacheWriteback(uint32_t addr) {
	__asm__ __volatile__("ocbwb @%0" : : "r" (addr) : "memory");
}
static inline void CachePurge(uint32_t addr) {
	__asm__ __volatile__("ocbp @%0" : : "r" (addr) : "memory");
}
static inline void CacheInvalidate(uint32_t addr) {
	__asm__ __volatile__("ocbi @%0" : : "r" (addr) : "memory");
}

// From TapamNs fbdma driver
#define CACHE_SIZE	(16*1024)
static char CacheFlushArea[CACHE_SIZE] __attribute__ ((aligned (32)));

typedef enum {
	PCT_PURGE_ALL,
	PCT_PURGE_OCI0,
	PCT_PURGE_OCI1,
} pctPurgeType;
static inline void sh4Invalidate(const void *addr) {
	__asm__ __volatile__("ocbi @%0" : : "r" (addr) : "memory");
}
static inline void sh4Purge(const void *addr) {
	__asm__ __volatile__("ocbp @%0" : : "r" (addr) : "memory");
}
static inline void sh4CachelineAllocate(void *addr, int value) {
	register int __value  __asm__("r0") = value;
	__asm__ __volatile__ (
		"movca.l r0,@%0\n\t"
		:  
		: "r" (addr), "r" (__value)
		: "memory"  );
}
static void pctPurgeCache(pctPurgeType banks) {
	volatile unsigned int *CCR = (void*)0xff00001c;
	unsigned int ccrval = *CCR;
	unsigned int size = CACHE_SIZE;
	//Purges the cache by using MOVCA to flush any existing dirty cacheline, then
	//invalidates cacheline to clear dirty bit so MOVCA garbage can't trigger
	//a writeback later
	
	//Treat any invalid value as request to flush entire cache
	if (banks > PCT_PURGE_OCI1)
		banks = PCT_PURGE_ALL;
	
	//Check OCINDEX
	if (ccrval & (1<<7)) {
		//Each bank is half size of full cache
		size >>= 1;
	} else {
		//If not using OCINDEX, ignore banks parameter, only purge cache once
		banks = PCT_PURGE_OCI0;
	}
	
	//Check OCRAM
	if (ccrval & (1<<5)) {
		//Usable cache size is halved
		size >>= 1;
	}
	//~ printf("size %i ", size);
	if (banks != PCT_PURGE_OCI1) {
		void *ptr = CacheFlushArea;
		for(size_t i = 0; i < size; i += 32) {
			sh4CachelineAllocate(ptr + i, 0);
			sh4Invalidate(ptr + i);
		}
	}
	
	if (banks != PCT_PURGE_OCI0) {
		void *ptr = (CacheFlushArea);
		for(size_t i = 0; i < size; i += 32) {
			sh4CachelineAllocate(ptr + i, 0);
			sh4Invalidate(ptr + i);
		}
	}
}

/* The library calls this function when it has a frame ready for display. */
typedef int (*render_callback)(unsigned short *buf, int width, int height,
    int stride, int texture_height);

/* The library calls this function when it has pcm samples ready for output. */
typedef int (*audio_callback)(unsigned char *buf, int samples, int channels);

/* The library calls this function to ask whether it should quit playback.
 * Return non-zero if it's time to quite. */
typedef int (*quit_callback)();

int dreamroq_play(char *filename, int loop, render_callback render_cb,
                  audio_callback audio_cb, quit_callback quit_cb);

#endif  /* NEWROQ_H */
