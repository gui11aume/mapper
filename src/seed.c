#include "seed.h"

#define SUCCESS 1
#define FAILURE 0

char *
rev_comp
(
 char * seq
)
{
   int64_t slen = strlen(seq);
   char * rseq = malloc(slen+1);
   for (int64_t i = 0; i < slen; i++)
      rseq[slen-1-i] = revcomp[(uint8_t)seq[i]];
   rseq[slen] = 0;
   return rseq;
}


int
seed_thr
(
 char         * seq,
 size_t         slen,
 seedstack_t ** stack,
 seedopt_t      opt,
 index_t      * index
)
{
   int32_t i = slen-1;
   uint64_t new_loci;
   // Start position.
   bwpos_t pos = (bwpos_t){.depth = 0, .sp = 0, .ep = index->size-1};
   while (i >= 0) {
      int nt = translate[(uint8_t)seq[i]];
      if (nt > 3) {
         pos = (bwpos_t){.depth = 0, .sp = 0, .ep = index->size-1};
         i--;
         continue;
      }
      bwpos_t newpos;
      // Extend suffix (Backward search).
      suffix_extend(nt, pos, &newpos, index);
      // Count loci.
      new_loci = (newpos.ep < newpos.sp ? 0 : newpos.ep - newpos.sp + 1);
      // Seed found, save and shrink suffix.
      if (new_loci == 0) {
         // Shrink suffix once.
         suffix_shrink(pos, &newpos, index);
         new_loci = newpos.ep - newpos.sp + 1;
      } else if (new_loci <= opt.thr_seed && pos.depth >= opt.min_len) {
         // Build seed.
         int bulk = 0;//newpos.depth < opt.min_len;
         seed_t seed = (seed_t) {.bulk = bulk, .qry_pos = i, .ref_pos = newpos};
         // Save seed.
         seedstack_push(seed, stack);

         // Shrink previous suffix (it had loci_count > thr).
         suffix_shrink(newpos, &newpos, index);
         // Check Complementary matches.
         if (newpos.depth > opt.min_len && newpos.ep - newpos.sp + 1 <= opt.min_loci) {
            bwpos_t tmp;
            do {
               tmp = newpos;
               suffix_shrink(newpos, &newpos, index);
            } while (newpos.depth > opt.min_len && newpos.ep - newpos.sp + 1 <= opt.min_loci);
            // Save seed if it bears new loci.
            if (tmp.ep - tmp.sp + 1 > new_loci) {
               seed_t seed = (seed_t) {.bulk = 0, .qry_pos = i, .ref_pos = tmp};
               seedstack_push(seed, stack);
            }
         }
         // Extend.
         i--;
      } else {
         i--;
      }
      pos = newpos;
   }

   return 0;
}
/*

int
seed_thr
(
 char         * seq,
 size_t         slen,
 seedstack_t ** stack,
 seedopt_t      opt,
 index_t      * index
)
{
   int32_t i = slen-1;
   uint64_t new_loci;
   // Start position.
   bwpos_t pos = (bwpos_t){.depth = 0, .sp = 0, .ep = index->size-1};

   while (i >= 0) {
      int nt = translate[(uint8_t)seq[i]];
      if (nt > 3) {
         pos = (bwpos_t){.depth = 0, .sp = 0, .ep = index->size-1};
         i--;
         continue;
      }
      bwpos_t newpos;
      // Extend suffix (Backward search).
      suffix_extend(nt, pos, &newpos, index);
      // Count loci.
      new_loci = (newpos.ep < newpos.sp ? 0 : newpos.ep - newpos.sp + 1);
      // Seed found, save and shrink suffix.
      if (new_loci == 0) {
         // Shrink suffix once.
         suffix_shrink(pos, &newpos, index);
         new_loci = newpos.ep - newpos.sp + 1;
      } else if (new_loci <= opt.thr_seed && pos.depth >= opt.min_len) {
         // Build seed.
         int bulk = 0;//newpos.depth < opt.min_len;
         seed_t seed = (seed_t) {.bulk = bulk, .qry_pos = i, .ref_pos = newpos};
         // Save seed.
         seedstack_push(seed, stack);

         // Shrink previous suffix (it had loci_count > thr).
         uint64_t last_loci = new_loci;
         do {
            suffix_shrink(newpos, &newpos, index);
            new_loci = newpos.ep - newpos.sp + 1;
            if (new_loci > last_loci && new_loci <= opt.thr_seed) {
               seed = (seed_t) {.bulk = 0, .qry_pos = i, .ref_pos = newpos};
               seedstack_push(seed,stack);
               last_loci = new_loci;
            }
         } while (new_loci <= opt.thr_seed);
         i--;
      } else {
         i--;
      }
      pos = newpos;
   }

   return 0;
}
*/

int
reseed_mem
(
 char         * seq,
 seedstack_t ** seeds,
 seedopt_t      opt,
 index_t      * index
)
{
   // Copy options.
   seedopt_t rseed_opt = opt;
   size_t n_seeds = (*seeds)->pos;
   for (size_t i = 0; i < n_seeds; i++) {
      // Do not reseed short seeds.
      seed_t seed = (*seeds)->seed[i];
      if (seed.ref_pos.depth < opt.reseed_len || seed.bulk) continue;
      // Reseed, min length = seedlen/2, min loci = seedloci + 1.
      rseed_opt.min_len = max(seed.ref_pos.depth/2, opt.min_len);
      rseed_opt.min_loci = seed.ref_pos.ep - seed.ref_pos.sp + 2;
      // Seed forward.
      int beg = 0;/* seed.qry_pos;*/
      int end = strlen(seq);/* beg + seed.ref_pos.depth;*/
      // Debug.
      if (VERBOSE_DEBUG) {
         fprintf(stdout, "Reseeding: beg=%d,end=%d,min_len=%d,min_loci=%d\n",beg,end,rseed_opt.min_len,rseed_opt.min_loci);
      }
      seed_mem(seq, beg, end, seeds, rseed_opt, index);
   }
   return 0;
}


int
reseed_smem_rec
(
 char         * seq,
 size_t         beg,
 size_t         end,
 seedstack_t ** seeds,
 seedopt_t      opt,
 index_t      * index
)
{
   // Seed stacks.
   seedstack_t * aux  = seedstack_new(SEEDSTACK_SIZE);
   seedstack_t * smem = seedstack_new(SEEDSTACK_SIZE);
   // Find MEMs.
   seed_mem(seq, beg, end, &aux, opt, index);
   // Extract longer nonoverlapping MEMs.
   get_smem(&smem, aux);
   // Reseed long MEMs.
   for (int i = 0; i < smem->pos; i++) {
      seed_t seed = smem->seed[i];
      // Reseed if SMEM is long enough.
      if (seed.ref_pos.depth <= opt.reseed_len || seed.ref_pos.depth <= opt.min_len) continue;
      seedopt_t newopt = opt;
      newopt.min_len  = max(seed.ref_pos.depth/2, opt.min_len);
      newopt.min_loci = seed.ref_pos.ep - seed.ref_pos.sp + 2;
      newopt.max_loci = opt.thr_seed;
      int beg = seed.qry_pos;
      int end = beg + seed.ref_pos.depth;
      // Keep reseeding recursively.
      reseed_smem_rec(seq, beg, end, &aux, newopt, index);
   }
   // Append aux to seeds.
   seedstack_append(seeds, aux);
   // Free stacks.
   free(aux);
   free(smem);
   return 0;
}

int
get_smem
(
 seedstack_t ** smem,
 seedstack_t  * mem
)
{
   if (mem->pos == 0) return 0;
   // Seeds are sorted by start position, descending.
   seed_t cur = mem->seed[mem->pos-1];
   int    len = cur.ref_pos.depth;
   int    end = cur.qry_pos + cur.ref_pos.depth;
   // Find longest non-overlapping seeds.
   for (int i = mem->pos-2; i >= 0; i--) {
      seed_t s = mem->seed[i];
      if (s.qry_pos >= end) {
         seedstack_push(cur, smem);
         cur = s;
         len = cur.ref_pos.depth;
         end = cur.qry_pos + cur.ref_pos.depth;
      } else if (s.ref_pos.depth > len) {
         cur = s;
         len = cur.ref_pos.depth;
         end = cur.qry_pos + cur.ref_pos.depth;
      }
   }
   seedstack_push(cur, smem);

   return 0;
}

int
find_repeats
(
 seedstack_t  * seeds,
 matchlist_t ** repeats,
 int32_t        repeat_thr   
)
{
   // Iterate over all seeds.
   int i = 0;
   while (i < seeds->pos) {
      seed_t s = seeds->seed[i];
      // If seed is not repeated.
      if (s.ref_pos.ep - s.ref_pos.sp <= repeat_thr) i++;
      // Repeated seeds: save read region and remove seed.
      else {
         // Save repeat in list.
         match_t r = {.read_s = s.qry_pos, .read_e = s.qry_pos + s.ref_pos.depth - 1};
         matchlist_add(repeats,r);
         // Remove seed.
         seeds->seed[i] = seeds->seed[--seeds->pos];
      }
   }
   return 0;
}

int
just_extend
(
   bwpos_t *pos,
   char    *seq,
   index_t *index
)
{

   bwpos_t tmpos;
   for (int i = 0 ; i < 10 ; i++) {
      int nt = translate[(uint8_t)seq[-i]];
      suffix_extend(nt, *pos, &tmpos, index);
      if (tmpos.ep < tmpos.sp) return FAILURE;
      *pos = tmpos;
   }

   return SUCCESS;

}


int
seed_wings
(
 char         * seq,
 int32_t         beg,
 int32_t         end,
 seedstack_t ** stack,
 seedopt_t      opt,
 index_t      * index
)
{

   bwpos_t wingR = (bwpos_t){.depth = 0, .sp = 0, .ep = index->size-1};

   int i;
   for (i = end-1 ;  i >= 20 ; i--) { 
      if (just_extend(&wingR, &seq[i], index)) break;
      suffix_shrink(wingR, &wingR, index);
      int nt = translate[(uint8_t)seq[i-12]];
      suffix_extend(nt, wingR, &wingR, index);
   }

   for ( ; i >= 20 ; i--) {

      // Try to extend right wing (should work).
      for (int mut = 0 ; mut < 4 ; mut++) {
         // Insert mutation.
         bwpos_t wingL;
         suffix_extend(mut, wingR, &wingL, index);
         if (wingL.ep < wingL.sp) continue;
         // Try to extend left wing. Save seed if it works.
         if (!just_extend(&wingL, &seq[i-11], index)) continue;
         seed_t seed = (seed_t) {.bulk = 0, .qry_pos = i-20, .ref_pos = wingL};
         seedstack_push(seed, stack);

      }

      // Shrink right wing and add nucleotide.
      suffix_shrink(wingR, &wingR, index);

      int nt = translate[(uint8_t)seq[i-10]];
      suffix_extend(nt, wingR, &wingR, index);

   }

   return 0;
}

int
seed_block_all
(
 char *         seq,
 size_t         slen,
 seedstack_t ** stack,
 seedopt_t      opt,
 index_t      * index
)
{
   // Wing length.
   //int c = opt.block_len/2;
   int c = 12;
   // Convert block length to even number.
   int blen = c*2;
   // Allocate seed cache.
   bwpos_t * cache = calloc(slen, sizeof(bwpos_t));
   // Seed:
   // - pre-compute constant part.
   int e = slen-1;
   int i = slen-1;
   bwpos_t pos = (bwpos_t) {.sp = 0, .ep = index->size-1, .depth = 0};
   while (i >= c) {
      bwpos_t newpos;
      suffix_extend(translate[(uint8_t)seq[i]], pos, &newpos, index);
      if (newpos.sp <= newpos.ep) {
         // If got length c, store and shrink.
         if (newpos.depth == c) {
            cache[e] = newpos;
            bwpos_t tmp;
            suffix_shrink(newpos, &tmp, index);
            // This is necessary to avoid shrinking more than 1 nt.
            if (tmp.depth < c-1) {
               newpos.depth--;
            } else {
               newpos = tmp;
            }
            e--;
         }
         pos = newpos;
         i--;
      } else {
         cache[e--] = newpos;
         bwpos_t tmp;
         int depth = pos.depth;
         suffix_shrink(pos, &tmp, index);
         if (tmp.depth < depth-1) {
            pos.depth--;
         } else {
            pos = tmp;
         }
      }
   }

   // - seed for all mismatched positions (i).
   for (int i = slen-1-c; i >= 0; i--) {
      int beg = min(i+blen-1, slen-1);
      int end = max(i+c, blen-1);
      int nt  = translate[(uint8_t)seq[i]];
      // Interval of this seed (p).
      for (int p = beg; p >= end; p--) {
         bwpos_t current = cache[p];
         // Continue on broken seeds.
         if (current.depth != p - i) continue;
         // Extend with the 4 possible mutations (m).
         for (int m = 0; m < 4; m++) {
            bwpos_t mpos = current;
            suffix_extend(m, mpos, &mpos, index);
            if (mpos.ep < mpos.sp) continue;
            // Cache extension for the next mismatch.
            if (m == nt) {
               cache[p] = mpos;
               continue;
            } 
            int k = i-1;
            // Extend seed until block length.
            while (mpos.ep >= mpos.sp && mpos.depth < blen)
               suffix_extend(translate[(uint8_t)seq[k--]], mpos, &mpos, index);
            // Save seed if seed exists.
            int64_t loci = mpos.ep - mpos.sp + 1;
            if (loci > 0 && loci < opt.thr_seed) {
               // Save seed.
               seed_t seed = (seed_t) {.bulk = 0, .qry_pos = p-blen+1, .ref_pos = mpos};
               seedstack_push(seed, stack);
            }
         }
      }
   }
   return 0;
}  


int
seed_mem
(
 char         * seq,
 int32_t         beg,
 int32_t         end,
 seedstack_t ** stack,
 seedopt_t      opt,
 index_t      * index
)
{

   int32_t i;
   int32_t last_qry_pos = end;
   uint64_t new_loci = index->size;//, last_loci;

   int maxseedlength = 0;
   
   // Start position.
   bwpos_t pos = (bwpos_t){.depth = 0, .sp = 0, .ep = index->size-1};

   for (i = end-1 ; i >= beg ; i--) {
      bwpos_t newpos;
      int nt = translate[(uint8_t)seq[i]];
      // Extend suffix (Backward search).
      suffix_extend(nt, pos, &newpos, index);
      // Count loci.
      //last_loci = new_loci;
      new_loci = (newpos.ep < newpos.sp ? 0 : newpos.ep - newpos.sp + 1);

      // No hits or depth exceeded.
      if (new_loci > 0) {
         pos = newpos;
         continue;
      }

      // Seed found.
      int seedlength = last_qry_pos - i - 1;

      if (seedlength > 24) {
         if (seedlength == maxseedlength) {
               seed_t seed = (seed_t) {.bulk = 0, .qry_pos = i+1, .ref_pos = pos};
               seedstack_push(seed, stack);
         }
         if (seedlength > maxseedlength) {
            maxseedlength = seedlength;
            (*stack[0]->seed).bulk = 0;
            (*stack[0]->seed).qry_pos = i+1;
            (*stack[0]->seed).ref_pos = pos;
            (*stack)->pos = 1;
         }
      }

      if (nt == 4) {
         // Reset 'pos' and jump over N.
         last_qry_pos = i+1;
         pos = (bwpos_t){.depth = 0, .sp = 0, .ep = index->size-1};
      } else {
         do {
            int origdepth = pos.depth;
            suffix_shrink(pos, &pos, index);
            new_loci = pos.ep - pos.sp + 1;
            last_qry_pos -= (origdepth - pos.depth);
         } while (new_loci < opt.min_loci);
         // Redo the loop with updated 'pos'.
         // Quite ugly by the way.
         i++;
      }
   }

   if (last_qry_pos > 20) {
      if (last_qry_pos == maxseedlength) {
         seed_t seed = (seed_t) {.bulk = 0, .qry_pos = 0, .ref_pos = pos};
         seedstack_push(seed, stack);
      }
      if (last_qry_pos > maxseedlength) {
         (*stack[0]->seed).bulk = 0;
         (*stack[0]->seed).qry_pos = 0;
         (*stack[0]->seed).ref_pos = pos;
         (*stack)->pos = 1;
      }
   }

   return 0;
}


hit_t *
compute_hits
(
 seedstack_t * seeds,
 index_t     * index,
 uint64_t    * hit_cnt
)
{
   // Count loci.
   uint64_t cnt = 0;
   for (size_t i = 0; i < seeds->pos; i++) cnt += seeds->seed[i].ref_pos.ep - seeds->seed[i].ref_pos.sp + 1;
   *hit_cnt = cnt;

   // Find loci in Suffix Array.
   hit_t * hits = malloc(cnt * sizeof(hit_t));
   size_t l = 0;
   for (size_t i = 0; i < seeds->pos; i++) {
      seed_t seed = seeds->seed[i];
      for (size_t j = seed.ref_pos.sp; j <= seed.ref_pos.ep; j++) {
         int64_t loc = get_sa(j,index->sa,index->sa_bits);
         hits[l++] = (hit_t) {.locus = loc, .qrypos = seed.qry_pos, .depth = seed.ref_pos.depth, .bulk = seed.bulk};
      }
   }
   
   return hits;
}


int
chain_seeds
(
 seedstack_t * seeds,
 int           seqlen,
 matchlist_t * matchlist,
 index_t     * index,
 int           dist_accept,
 double        read_ref_ratio
)
{
   if (matchlist->size < 1) return -1;

   // Convert seeds to hits.
   uint64_t hit_count;
   hit_t * hits = compute_hits(seeds, index, &hit_count);
   
   // Merge-sort hits.
//   mergesort_mt(hits, hit_count, sizeof(hit_t), 0, 1, compar_hit_locus);

   // Reset matchlist.
   matchlist->pos = 0;

   // Aux variables.
   long minv = 0;
   int  mini = 0;
   size_t i = 0;

   for (i = 0 ; i < hit_count ; i++) {
      // Store match.
      match_t match;
      match.ref_e  = hits[i].locus + hits[i].depth;
      match.read_e = hits[i].qrypos + hits[i].depth;
      match.ref_s  = hits[i].locus;
      match.read_s = hits[i].qrypos;
      match.hits   = hits[i].depth;
      match.flags  = 0;
      match.score  = -1;

      // Debug match
//      if (VERBOSE_DEBUG) {
//         uint64_t g_start;
//         int dir = 0;
//         if (match.ref_s >= index->size/2) {
//            g_start = index->size - match.ref_e - 2;
//            dir = 1;
//         } else {
//            g_start = match.ref_s + 1;
//            dir = 0;
//         }
//         // Search chromosome name.
//         int   chrnum = bisect_search(0, index->chr->nchr-1, index->chr->start, g_start+1)-1;
//         // Print results.
//         fprintf(stdout, "[%d]\t%s,%ld%c\t%ld\t%d,%d\n",
//                 match.hits,
//                 index->chr->name[chrnum],
//                 g_start - index->chr->start[chrnum]+1,
//                 dir ? '-' : '+',
//                 match.ref_s,
//                 match.read_s, match.read_e
//                 );
//      }

      // Append if list not yet full, replace the minimum value otherwise.
      if (matchlist->pos < matchlist->size) {
         matchlist->match[matchlist->pos++] = match;
      }
//      else {
//         if(VERBOSE_DEBUG) {
//            match_t m = matchlist->match[mini];
//            fprintf(stdout, "replaces [%d]\t%ld\t%d,%d\n",m.hits,m.ref_s,m.read_s,m.read_e);
//         }
//         matchlist->match[mini] = match;
//      }
            
      // Find the minimum that will be substituted next.
      else if (matchlist->pos == matchlist->size) {
         mini = 0;
         minv = matchlist->match[mini].hits;
         for (int j = 1 ; j < matchlist->size; j++) {
            if (matchlist->match[j].hits < minv) {
               mini  = j;
               minv = matchlist->match[j].hits;
            }
         }
      }
   }

   // Free hits.
   free(hits);

   return 0;

}


seedstack_t *
naive_smem
(
 char * seq,
 seedopt_t opt,
 index_t * index
)
{
   uint32_t slen = strlen(seq);
   int32_t qry_end = slen - 1;
   int32_t last_qry_pos = slen;
   // Create stack.
   seedstack_t * stack = seedstack_new(SEEDSTACK_SIZE);

   for (int i = slen-1; i > 0; i--) {
      // Start position.
      bwpos_t pos = (bwpos_t){.depth = 0, .sp = 0, .ep = index->size-1};
      qry_end = i;
      int j;
      uint64_t new_loci;
      for (j = 0; j <= i; j++) {
         bwpos_t newpos;
         int nt = translate[(uint8_t)seq[i-j]];
         // Extend suffix (Backward search).
         suffix_extend(nt, pos, &newpos, index);
         // Count loci.
         new_loci = (newpos.ep < newpos.sp ? 0 : newpos.ep - newpos.sp + 1);

         if (new_loci < opt.min_loci || nt == 4) {
            // Check previous suffix.
            uint64_t loci = (pos.ep < pos.sp ? 0 : pos.ep - pos.sp + 1);
            int32_t qry_pos = qry_end + 1 - pos.depth;
            if (loci <= opt.max_loci && pos.depth >= opt.min_len && qry_pos < last_qry_pos) {
               // Seed found.
               seed_t seed = (seed_t) {.qry_pos = qry_pos, .ref_pos = pos};
               seedstack_push(seed, &stack);
               last_qry_pos = qry_pos;
            }
            if (nt == 4) {
               i = i - j;
            }
            break;
         } else if (newpos.depth == opt.max_len || j == i) {
            int32_t qry_pos = qry_end + 1 - newpos.depth;
            if (new_loci <= opt.max_loci && qry_pos < last_qry_pos) {
               // Seed found.
               seed_t seed = (seed_t) {.qry_pos = qry_pos, .ref_pos = newpos};
               seedstack_push(seed, &stack);
               last_qry_pos = qry_pos;
            }
            break;
         }
         pos = newpos;
      }
   }
   return stack;
}


seedstack_t *
seedstack_new
(
 int size
)
{
   seedstack_t * stack = malloc(sizeof(seedstack_t) + size*sizeof(seed_t));
   if (stack == NULL) return NULL;
   stack->pos = 0;
   stack->size = size;
   return stack;
}

int
seedstack_push
(
 seed_t         seed,
 seedstack_t ** stackp
)
{
   seedstack_t * stack = *stackp;
   // Realloc if full.
   if (stack->pos >= stack->size) {
      size_t newsize = stack->size * 2;
      stack = realloc(stack, sizeof(seedstack_t) + newsize * sizeof(seed_t));
      if (stack == NULL) return -1;
      *stackp = stack;
      stack->size = newsize;
   }
   // Push seed.
   stack->seed[stack->pos++] = seed;
   return 0;
}

int
seedstack_append
(
 seedstack_t ** dest,
 seedstack_t  * src
)
{
   if (src->pos < 1) return 0;

   seedstack_t * stack = *dest;
   // Realloc if full.
   size_t newpos = stack->pos + src->pos;
   if (newpos >= stack->size) {
      size_t newsize = newpos + 1;
      stack = realloc(stack, sizeof(seedstack_t) + newsize * sizeof(seed_t));
      if (stack == NULL) return -1;
      *dest = stack;
      stack->size = newsize;
   }
   // Copy seeds.
   memcpy(stack->seed + stack->pos, src->seed, src->pos * sizeof(seed_t));
   stack->pos = newpos;
   return 0;
}

int
compar_hit_locus
(
 const void * a,
 const void * b,
 const int param
)
{
   hit_t * ha = (hit_t *)a;
   hit_t * hb = (hit_t *)b;
   if (ha->locus > hb->locus) return 1;
   else if (ha->locus < hb->locus) return -1;
   else return (ha->depth > hb->depth ? 1 : -1);
}

int
compar_seed_start
(
 const void * a,
 const void * b,
 const int param
)
{
   seed_t * sa = (seed_t *)a;
   seed_t * sb = (seed_t *)b;
   if (sa->qry_pos > sb->qry_pos) return 1;
   else if (sa->qry_pos < sb->qry_pos) return -1;
   else return (sa->ref_pos.depth > sb->ref_pos.depth ? -1 : 1);
}
