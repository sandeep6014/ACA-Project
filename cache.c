/* cache.c - cache module routines (patched: selectable Write-Back / Write-Through)
 *
 * Based on SimpleScalar 3.0 cache.c
 * Adds a compile-time switch to select WRITE-BACK (default) or WRITE-THROUGH.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "cache.h"

/* ---------------- WRITE POLICY CONTROL ---------------- */
#define WRITE_BACK     1
#define WRITE_THROUGH  2

/* SELECT WRITE POLICY HERE (default: WRITE_BACK) */
#ifndef WRITE_POLICY
#define WRITE_POLICY WRITE_THROUGH /* change to WRITE_THROUGH for comparison */
#endif

/* cache access macros */
#define CACHE_TAG(cp, addr)	((addr) >> (cp)->tag_shift)
#define CACHE_SET(cp, addr)	(((addr) >> (cp)->set_shift) & (cp)->set_mask)
#define CACHE_BLK(cp, addr)	((addr) & (cp)->blk_mask)
#define CACHE_TAGSET(cp, addr)	((addr) & (cp)->tagset_mask)

/* extract/reconstruct a block address */
#define CACHE_BADDR(cp, addr)	((addr) & ~(cp)->blk_mask)
#define CACHE_MK_BADDR(cp, tag, set)					\
  (((tag) << (cp)->tag_shift)|((set) << (cp)->set_shift))

/* index an array of cache blocks, non-trivial due to variable length blocks */
#define CACHE_BINDEX(cp, blks, i)					\
  ((struct cache_blk_t *)(((char *)(blks)) +				\
			  (i)*(sizeof(struct cache_blk_t) +		\
			       ((cp)->balloc				\
				? (cp)->bsize*sizeof(byte_t) : 0))))

/* cache data block accessor, type parameterized */
#define __CACHE_ACCESS(type, data, bofs)				\
  (*((type *)(((char *)data) + (bofs))))

/* cache data block accessors, by type */
#define CACHE_DOUBLE(data, bofs)  __CACHE_ACCESS(double, data, bofs)
#define CACHE_FLOAT(data, bofs)	  __CACHE_ACCESS(float, data, bofs)
#define CACHE_WORD(data, bofs)	  __CACHE_ACCESS(unsigned int, data, bofs)
#define CACHE_HALF(data, bofs)	  __CACHE_ACCESS(unsigned short, data, bofs)
#define CACHE_BYTE(data, bofs)	  __CACHE_ACCESS(unsigned char, data, bofs)

/* cache block hashing macros, this macro is used to index into a cache
   set hash table (to find the correct block on N in an N-way cache), the
   cache set index function is CACHE_SET, defined above */
#define CACHE_HASH(cp, key)						\
  (((key >> 24) ^ (key >> 16) ^ (key >> 8) ^ key) & ((cp)->hsize-1))

/* copy data out of a cache block to buffer indicated by argument pointer p */
#define CACHE_BCOPY(cmd, blk, bofs, p, nbytes)	\
  if (cmd == Read)							\
    {									\
      switch (nbytes) {							\
      case 1:								\
	*((byte_t *)p) = CACHE_BYTE(&blk->data[0], bofs); break;	\
      case 2:								\
	*((half_t *)p) = CACHE_HALF(&blk->data[0], bofs); break;	\
      case 4:								\
	*((word_t *)p) = CACHE_WORD(&blk->data[0], bofs); break;	\
      default:								\
	{ /* >= 8, power of two, fits in block */			\
	  int words = nbytes >> 2;					\
	  while (words-- > 0)						\
	    {								\
	      *((word_t *)p) = CACHE_WORD(&blk->data[0], bofs);	\
	      p += 4; bofs += 4;					\
	    }\
	}\
      }\
    }\
  else /* cmd == Write */						\
    {									\
      switch (nbytes) {							\
      case 1:								\
	CACHE_BYTE(&blk->data[0], bofs) = *((byte_t *)p); break;	\
      case 2:								\
        CACHE_HALF(&blk->data[0], bofs) = *((half_t *)p); break;	\
      case 4:								\
	CACHE_WORD(&blk->data[0], bofs) = *((word_t *)p); break;	\
      default:								\
	{ /* >= 8, power of two, fits in block */			\
	  int words = nbytes >> 2;					\
	  while (words-- > 0)						\
	    {								\
	      CACHE_WORD(&blk->data[0], bofs) = *((word_t *)p);		\
	      p += 4; bofs += 4;					\
	    }\
	}\
    }\
  }

/* bound sqword_t/dfloat_t to positive int */
#define BOUND_POS(N)		((int)(MIN(MAX(0, (N)), 2147483647)))

/* unlink BLK from the hash table bucket chain in SET */
static void
unlink_htab_ent(struct cache_t *cp, struct cache_set_t *set, struct cache_blk_t *blk)
{
  struct cache_blk_t *prev, *ent;
  int index = CACHE_HASH(cp, blk->tag);

  for (prev=NULL,ent=set->hash[index]; ent; prev=ent,ent=ent->hash_next)
    if (ent == blk) break;
  assert(ent);

  if (!prev) set->hash[index] = ent->hash_next;
  else prev->hash_next = ent->hash_next;
  ent->hash_next = NULL;
}

/* insert BLK onto the head of the hash table bucket chain in SET */
static void
link_htab_ent(struct cache_t *cp, struct cache_set_t *set, struct cache_blk_t *blk)
{
  int index = CACHE_HASH(cp, blk->tag);
  blk->hash_next = set->hash[index];
  set->hash[index] = blk;
}

/* where to insert a block onto the ordered way chain */
enum list_loc_t { Head, Tail };

/* insert BLK into the ordered way chain in SET at location WHERE */
static void
update_way_list(struct cache_set_t *set, struct cache_blk_t *blk, enum list_loc_t where)
{
  if (!blk->way_prev && !blk->way_next)
    { assert(set->way_head == blk && set->way_tail == blk); return; }
  else if (!blk->way_prev)
    {
      assert(set->way_head == blk && set->way_tail != blk);
      if (where == Head) return;
      set->way_head = blk->way_next;
      blk->way_next->way_prev = NULL;
    }
  else if (!blk->way_next)
    {
      assert(set->way_head != blk && set->way_tail == blk);
      if (where == Tail) return;
      set->way_tail = blk->way_prev;
      blk->way_prev->way_next = NULL;
    }
  else
    {
      assert(set->way_head != blk && set->way_tail != blk);
      blk->way_prev->way_next = blk->way_next;
      blk->way_next->way_prev = blk->way_prev;
    }

  if (where == Head)
    {
      blk->way_next = set->way_head;
      blk->way_prev = NULL;
      set->way_head->way_prev = blk;
      set->way_head = blk;
    }
  else if (where == Tail)
    {
      blk->way_prev = set->way_tail;
      blk->way_next = NULL;
      set->way_tail->way_next = blk;
      set->way_tail = blk;
    }
  else panic("bogus WHERE designator");
}

/* create and initialize a general cache structure */
struct cache_t *
cache_create(char *name, int nsets, int bsize, int balloc, int usize,
	     int assoc, enum cache_policy policy,
	     unsigned int (*blk_access_fn)(enum mem_cmd, md_addr_t, int, struct cache_blk_t*, tick_t),
	     unsigned int hit_latency)
{
  struct cache_t *cp;
  struct cache_blk_t *blk;
  int i, j, bindex;

  if (nsets <= 0) fatal("cache size (in sets) `%d' must be non-zero", nsets);
  if ((nsets & (nsets-1)) != 0) fatal("cache size (in sets) `%d' is not a power of two", nsets);
  if (bsize < 8) fatal("cache block size (in bytes) `%d' must be 8 or greater", bsize);
  if ((bsize & (bsize-1)) != 0) fatal("cache block size (in bytes) `%d' must be a power of two", bsize);
  if (usize < 0) fatal("user data size (in bytes) `%d' must be a positive value", usize);
  if (assoc <= 0) fatal("cache associativity `%d' must be non-zero and positive", assoc);
  if ((assoc & (assoc-1)) != 0) fatal("cache associativity `%d' must be a power of two", assoc);
  if (!blk_access_fn) fatal("must specify miss/replacement functions");

  cp = (struct cache_t *)calloc(1, sizeof(struct cache_t) + (nsets-1)*sizeof(struct cache_set_t));
  if (!cp) fatal("out of virtual memory");

  cp->name = mystrdup(name);
  cp->nsets = nsets;
  cp->bsize = bsize;
  cp->balloc = balloc;
  cp->usize = usize;
  cp->assoc = assoc;
  cp->policy = policy;
  cp->hit_latency = hit_latency;

  cp->blk_access_fn = blk_access_fn;

  cp->hsize = CACHE_HIGHLY_ASSOC(cp) ? (assoc >> 2) : 0;
  cp->blk_mask = bsize-1;
  cp->set_shift = log_base2(bsize);
  cp->set_mask = nsets-1;
  cp->tag_shift = cp->set_shift + log_base2(nsets);
  cp->tag_mask = (1 << (32 - cp->tag_shift))-1;
  cp->tagset_mask = ~cp->blk_mask;
  cp->bus_free = 0;

  cp->hits = cp->misses = cp->replacements = cp->writebacks = cp->invalidations = 0;

  cp->last_tagset = 0;
  cp->last_blk = NULL;

  cp->data = (byte_t *)calloc(nsets * assoc, sizeof(struct cache_blk_t) +
			      (cp->balloc ? (bsize*sizeof(byte_t)) : 0));
  if (!cp->data) fatal("out of virtual memory");

  for (bindex=0,i=0; i<nsets; i++)
    {
      cp->sets[i].way_head = NULL;
      cp->sets[i].way_tail = NULL;
      if (cp->hsize)
	{
	  cp->sets[i].hash =
	    (struct cache_blk_t **)calloc(cp->hsize, sizeof(struct cache_blk_t *));
	  if (!cp->sets[i].hash) fatal("out of virtual memory");
	}
      cp->sets[i].blks = CACHE_BINDEX(cp, cp->data, bindex);

      for (j=0; j<assoc; j++)
	{
	  blk = CACHE_BINDEX(cp, cp->data, bindex);
	  bindex++;

	  blk->status = 0;
	  blk->tag = 0;
	  blk->ready = 0;
	  blk->user_data = (usize != 0 ? (byte_t *)calloc(usize, sizeof(byte_t)) : NULL);

	  if (cp->hsize) link_htab_ent(cp, &cp->sets[i], blk);

	  blk->way_next = cp->sets[i].way_head;
	  blk->way_prev = NULL;
	  if (cp->sets[i].way_head) cp->sets[i].way_head->way_prev = blk;
	  cp->sets[i].way_head = blk;
	  if (!cp->sets[i].way_tail) cp->sets[i].way_tail = blk;
	}
    }
  return cp;
}

/* parse policy */
enum cache_policy cache_char2policy(char c)
{
  switch (c) {
  case 'l': return LRU;
  case 'r': return Random;
  case 'f': return FIFO;
  default: fatal("bogus replacement policy, `%c'", c);
  }
}

/* print cache configuration */
void cache_config(struct cache_t *cp, FILE *stream)
{
  fprintf(stream,
	  "cache: %s: %d sets, %d byte blocks, %d bytes user data/block\n",
	  cp->name, cp->nsets, cp->bsize, cp->usize);
  fprintf(stream,
	  "cache: %s: %d-way, `%s' replacement policy, %s\n",
	  cp->name, cp->assoc,
	  cp->policy == LRU ? "LRU"
	  : cp->policy == Random ? "Random"
	  : cp->policy == FIFO ? "FIFO"
	  : (abort(), ""),
	  WRITE_POLICY == WRITE_BACK ? "write-back" : "write-through");
}

/* register cache stats */
void cache_reg_stats(struct cache_t *cp, struct stat_sdb_t *sdb)
{
  char buf[512], buf1[512], *name;

  if (!cp->name || !cp->name[0]) name = "<unknown>";
  else name = cp->name;

  sprintf(buf, "%s.accesses", name);
  sprintf(buf1, "%s.hits + %s.misses", name, name);
  stat_reg_formula(sdb, buf, "total number of accesses", buf1, "%12.0f");
  sprintf(buf, "%s.hits", name);
  stat_reg_counter(sdb, buf, "total number of hits", &cp->hits, 0, NULL);
  sprintf(buf, "%s.misses", name);
  stat_reg_counter(sdb, buf, "total number of misses", &cp->misses, 0, NULL);
  sprintf(buf, "%s.replacements", name);
  stat_reg_counter(sdb, buf, "total number of replacements", &cp->replacements, 0, NULL);
  sprintf(buf, "%s.writebacks", name);
  stat_reg_counter(sdb, buf, "total number of writebacks", &cp->writebacks, 0, NULL);
  sprintf(buf, "%s.invalidations", name);
  stat_reg_counter(sdb, buf, "total number of invalidations", &cp->invalidations, 0, NULL);
  sprintf(buf, "%s.miss_rate", name);
  sprintf(buf1, "%s.misses / %s.accesses", name, name);
  stat_reg_formula(sdb, buf, "miss rate (i.e., misses/ref)", buf1, NULL);
  sprintf(buf, "%s.repl_rate", name);
  sprintf(buf1, "%s.replacements / %s.accesses", name, name);
  stat_reg_formula(sdb, buf, "replacement rate (i.e., repls/ref)", buf1, NULL);
  sprintf(buf, "%s.wb_rate", name);
  sprintf(buf1, "%s.writebacks / %s.accesses", name, name);
  stat_reg_formula(sdb, buf, "writeback rate (i.e., wrbks/ref)", buf1, NULL);
  sprintf(buf, "%s.inv_rate", name);
  sprintf(buf1, "%s.invalidations / %s.accesses", name, name);
  stat_reg_formula(sdb, buf, "invalidation rate (i.e., invs/ref)", buf1, NULL);
}

/* print cache stats */
void cache_stats(struct cache_t *cp, FILE *stream)
{
  double sum = (double)(cp->hits + cp->misses);

  fprintf(stream,
	  "cache: %s: %.0f hits %.0f misses %.0f repls %.0f invalidations\n",
	  cp->name, (double)cp->hits, (double)cp->misses,
	  (double)cp->replacements, (double)cp->invalidations);
  fprintf(stream,
	  "cache: %s: miss rate=%f  repl rate=%f  invalidation rate=%f\n",
	  cp->name,
	  (double)cp->misses/sum, (double)(double)cp->replacements/sum,
	  (double)cp->invalidations/sum);
}

/* access a cache */
unsigned int
cache_access(struct cache_t *cp, enum mem_cmd cmd, md_addr_t addr, void *vp,
	     int nbytes, tick_t now, byte_t **udata, md_addr_t *repl_addr)
{
  byte_t *p = vp;
  md_addr_t tag = CACHE_TAG(cp, addr);
  md_addr_t set = CACHE_SET(cp, addr);
  md_addr_t bofs = CACHE_BLK(cp, addr);
  struct cache_blk_t *blk, *repl;
  int lat = 0;

  if (repl_addr) *repl_addr = 0;

  if ((nbytes & (nbytes-1)) != 0 || (addr & (nbytes-1)) != 0)
    fatal("cache: access error: bad size or alignment, addr 0x%08x", addr);

  if ((addr + nbytes) > ((addr & ~cp->blk_mask) + cp->bsize))
    fatal("cache: access error: access spans block, addr 0x%08x", addr);

  if (CACHE_TAGSET(cp, addr) == cp->last_tagset)
    { blk = cp->last_blk; goto cache_fast_hit; }

  if (cp->hsize)
    {
      int hindex = CACHE_HASH(cp, tag);
      for (blk=cp->sets[set].hash[hindex]; blk; blk=blk->hash_next)
	if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	  goto cache_hit;
    }
  else
    {
      for (blk=cp->sets[set].way_head; blk; blk=blk->way_next)
	if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	  goto cache_hit;
    }

  /* -------- MISS -------- */
  cp->misses++;

  switch (cp->policy) {
  case LRU:
  case FIFO:
    repl = cp->sets[set].way_tail;
    update_way_list(&cp->sets[set], repl, Head);
    break;
  case Random:
    {
      int bindex = myrand() & (cp->assoc - 1);
      repl = CACHE_BINDEX(cp, cp->sets[set].blks, bindex);
    }
    break;
  default:
    panic("bogus replacement policy");
  }

  if (cp->hsize) unlink_htab_ent(cp, &cp->sets[set], repl);

  cp->last_tagset = 0;
  cp->last_blk = NULL;

  if (repl->status & CACHE_BLK_VALID)
    {
      cp->replacements++;

      if (repl_addr) *repl_addr = CACHE_MK_BADDR(cp, repl->tag, set);

      lat += BOUND_POS(repl->ready - now);
      lat += BOUND_POS(cp->bus_free - (now + lat));
      cp->bus_free = MAX(cp->bus_free, (now + lat)) + 1;

      if (repl->status & CACHE_BLK_DIRTY)
	{
	  cp->writebacks++;
	  lat += cp->blk_access_fn(Write,
				   CACHE_MK_BADDR(cp, repl->tag, set),
				   cp->bsize, repl, now+lat);
	}
    }

  repl->tag = tag;
  repl->status = CACHE_BLK_VALID;

  lat += cp->blk_access_fn(Read, CACHE_BADDR(cp, addr), cp->bsize, repl, now+lat);

  if (cp->balloc) { CACHE_BCOPY(cmd, repl, bofs, p, nbytes); }

  if (cmd == Write)
  {
#if WRITE_POLICY == WRITE_BACK
    repl->status |= CACHE_BLK_DIRTY;
#else
    cp->writebacks++;
    lat += cp->blk_access_fn(Write, CACHE_BADDR(cp, addr), cp->bsize, repl, now+lat);
#endif
  }

  if (udata) *udata = repl->user_data;

  repl->ready = now+lat;

  if (cp->hsize) link_htab_ent(cp, &cp->sets[set], repl);

  return lat;

  /* -------- HIT (slow) -------- */
cache_hit:
  cp->hits++;

  if (cp->balloc) { CACHE_BCOPY(cmd, blk, bofs, p, nbytes); }

  if (cmd == Write)
  {
#if WRITE_POLICY == WRITE_BACK
    blk->status |= CACHE_BLK_DIRTY;
#else
    cp->writebacks++;
    cp->blk_access_fn(Write, CACHE_BADDR(cp, addr), cp->bsize, blk, now);
#endif
  }

  if (blk->way_prev && cp->policy == LRU)
    update_way_list(&cp->sets[set], blk, Head);

  cp->last_tagset = CACHE_TAGSET(cp, addr);
  cp->last_blk = blk;

  if (udata) *udata = blk->user_data;

  return (int) MAX(cp->hit_latency, (blk->ready - now));

  /* -------- FAST HIT -------- */
cache_fast_hit:
  cp->hits++;

  if (cp->balloc) { CACHE_BCOPY(cmd, blk, bofs, p, nbytes); }

  if (cmd == Write)
  {
#if WRITE_POLICY == WRITE_BACK
    blk->status |= CACHE_BLK_DIRTY;
#else
    cp->writebacks++;
    cp->blk_access_fn(Write, CACHE_BADDR(cp, addr), cp->bsize, blk, now);
#endif
  }

  if (udata) *udata = blk->user_data;

  cp->last_tagset = CACHE_TAGSET(cp, addr);
  cp->last_blk = blk;

  return (int) MAX(cp->hit_latency, (blk->ready - now));
}

/* return non-zero if block containing address ADDR is contained in cache */
int cache_probe(struct cache_t *cp, md_addr_t addr)
{
  md_addr_t tag = CACHE_TAG(cp, addr);
  md_addr_t set = CACHE_SET(cp, addr);
  struct cache_blk_t *blk;

  if (cp->hsize)
  {
    int hindex = CACHE_HASH(cp, tag);
    for (blk=cp->sets[set].hash[hindex]; blk; blk=blk->hash_next)
      if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	return TRUE;
  }
  else
  {
    for (blk=cp->sets[set].way_head; blk; blk=blk->way_next)
      if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	return TRUE;
  }
  return FALSE;
}

/* flush the entire cache, returns latency of the operation */
unsigned int cache_flush(struct cache_t *cp, tick_t now)
{
  int i, lat = cp->hit_latency;
  struct cache_blk_t *blk;

  cp->last_tagset = 0;
  cp->last_blk = NULL;

  for (i=0; i<cp->nsets; i++)
    {
      for (blk=cp->sets[i].way_head; blk; blk=blk->way_next)
	{
	  if (blk->status & CACHE_BLK_VALID)
	    {
	      cp->invalidations++;
	      blk->status &= ~CACHE_BLK_VALID;

	      if (blk->status & CACHE_BLK_DIRTY)
		{
          	  cp->writebacks++;
		  lat += cp->blk_access_fn(Write,
					   CACHE_MK_BADDR(cp, blk->tag, i),
					   cp->bsize, blk, now+lat);
		}
	    }
	}
    }
  return lat;
}

/* flush the block containing ADDR from the cache CP */
unsigned int cache_flush_addr(struct cache_t *cp, md_addr_t addr, tick_t now)
{
  md_addr_t tag = CACHE_TAG(cp, addr);
  md_addr_t set = CACHE_SET(cp, addr);
  struct cache_blk_t *blk;
  int lat = cp->hit_latency;

  if (cp->hsize)
    {
      int hindex = CACHE_HASH(cp, tag);
      for (blk=cp->sets[set].hash[hindex]; blk; blk=blk->hash_next)
	if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	  break;
    }
  else
    {
      for (blk=cp->sets[set].way_head; blk; blk=blk->way_next)
	if (blk->tag == tag && (blk->status & CACHE_BLK_VALID))
	  break;
    }

  if (blk)
    {
      cp->invalidations++;
      blk->status &= ~CACHE_BLK_VALID;

      cp->last_tagset = 0;
      cp->last_blk = NULL;

      if (blk->status & CACHE_BLK_DIRTY)
	{
          cp->writebacks++;
	  lat += cp->blk_access_fn(Write,
				   CACHE_MK_BADDR(cp, blk->tag, set),
				   cp->bsize, blk, now+lat);
	}
      update_way_list(&cp->sets[set], blk, Tail);
    }
  return lat;
}
