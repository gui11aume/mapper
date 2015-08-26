#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include "definitions.h"
#include "divsufsort.h"
#include "indexquery.h"

#define STACK_LCP_INITIAL_SIZE 1024
#define NAIVE_LCP_MAX_SAMPLES 1000000
#define BUFFER_SIZE   100
#define GENOME_SIZE   100000


static const char translate[256] = {[0 ... 255] = 4,
                           ['a'] = 0, ['c'] = 1, ['g'] = 2, ['t'] = 3, ['n'] = 4,
                           ['A'] = 0, ['C'] = 1, ['G'] = 2, ['T'] = 3, ['N'] = 4};

static const char revcomp[256] = {[0 ... 255] = 'N',
                   ['a'] = 't', ['c'] = 'g', ['g'] = 'c', ['t'] = 'a', ['u'] = 'a',
                   ['A'] = 'T', ['C'] = 'G', ['G'] = 'C', ['T'] = 'A', ['U'] = 'A' };

static const char uppercase[256] = {[0 ... 255] = 'e',
                           ['a'] = 'a', ['c'] = 'b', ['g'] = 'c', ['t'] = 'd', ['n'] = 'e',
                           ['A'] = 'a', ['C'] = 'b', ['G'] = 'c', ['T'] = 'd', ['N'] = 'e'};

typedef struct stack8_t    stack8_t;
typedef struct stack32_t   stack32_t;
typedef struct lcpsample_t lcpsample_t;
typedef struct lcpcorner_t lcpcorner_t;
typedef struct cstack_t    cstack_t;
typedef struct lcp_t       lcp_t;

struct lcp_t {
   uint64_t    idx_size;
   uint64_t  * idx_sample;
   stack8_t  * lcp_sample;
   uint64_t  * idx_extend;
   stack32_t * lcp_extend;
};

struct stack32_t {
   uint64_t pos;
   uint64_t size;
   int32_t  val[];
};

struct stack8_t {
   uint64_t pos;
   uint64_t size;
   int8_t   val[];
};


struct lcpcorner_t {
   int      lcp;
   int      lcp_next;
   int64_t  pos;
   uint64_t ptr;
};

struct cstack_t {
   uint64_t    pos;
   uint64_t    size;
   lcpcorner_t c[];
};

struct lcpsample_t {
   uint8_t lcp;
   uint64_t ep;
};

int          write_index      (char * filename);
int64_t    * compute_sa       (char * genome, uint64_t gsize);
uint64_t   * compute_occ      (char * genome, uint64_t * sa, uint64_t gsize, uint64_t * occ_size);
uint64_t   * compute_c        (uint64_t * occ);
uint64_t   * compute_lut      (uint64_t * c, uint64_t * occ, int depth);
void         recursive_lut    (uint64_t * c, uint64_t * occ, uint64_t * lut, uint64_t p, int d, int maxd, int * path);
int          stack_push_lcp   (stack8_t ** lcp_stack, uint8_t lcp, int32_t offset);
void         lcp_index_sample (uint64_t * lcp_index, uint64_t pos, int size32);
lcp_t        compute_lcp      (uint64_t idxsize, int min_depth, int sar_bits, uint64_t * sar, char * genome);
char *       compact_genome   (char * filename, uint64_t * genomesize);
uint64_t     compact_array    (uint64_t * array, uint64_t len, int bits);
int64_t      naive_lcp        (int min_depth, uint64_t idxsize, uint64_t * lcpsample, uint64_t * lcpext, stack8_t ** lcp, stack32_t ** ext, uint64_t * sar, int sar_bits, char * genome);
lcpcorner_t  corner_pop       (cstack_t * stack);
int          corner_push      (cstack_t ** stack, lcpcorner_t c);
cstack_t   * cstack_new       (int size);
int          seq_lcp          (char * seq_a, char * seq_b);

stack8_t   * stack8_new       (int size);
stack32_t  * stack32_new      (int size);
int32_t      stack32_push     (stack32_t ** stackp, int32_t val);
int32_t      stack32_pop      (stack32_t * stack);