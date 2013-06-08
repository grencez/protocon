
#ifndef PFmla_H_
#define PFmla_H_

#include "cx/alphatab.h"
#include "cx/associa.h"
#include "cx/bittable.h"

typedef struct PFmlaBase PFmlaBase;
typedef PFmlaBase* PFmla;
typedef struct XnPFmla XnPFmla;
typedef struct PFmlaVbl PFmlaVbl;
typedef struct PFmlaCtx PFmlaCtx;
typedef struct PFmlaOpVT PFmlaOpVT;

struct PFmlaBase
{
  PFmlaCtx* ctx;
};

struct PFmlaVbl
{
  PFmlaCtx* ctx;
  uint id;
  AlphaTab name;
  AlphaTab img_name;
  uint domsz;
};

struct PFmlaCtx
{
  LgTable vbls;
  Associa vbl_map;
  TableT( TableT_uint ) vbl_lists;
  const PFmlaOpVT* vt;
};

struct PFmlaOpVT
{
  void (*op2_fn) (PFmlaCtx*, PFmla*, BitOp, const PFmla, const PFmla);

  void (*smooth_vbls_fn) (PFmlaCtx*, PFmla*, const PFmla, uint);
  void (*subst_vbls_fn) (PFmlaCtx*, PFmla*, const PFmla, uint, uint);
  void (*pre_fn) (PFmlaCtx*, PFmla*, const PFmla);
  void (*pre1_fn) (PFmlaCtx*, PFmla*, const PFmla, const PFmla);
  void (*img_fn) (PFmlaCtx*, PFmla*, const PFmla);
  void (*img1_fn) (PFmlaCtx*, PFmla*, const PFmla, const PFmla);
  bool (*tautology_ck_fn) (PFmlaCtx*, const PFmla);
  bool (*unsat_ck_fn) (PFmlaCtx*, const PFmla);
  bool (*equiv_ck_fn) (PFmlaCtx*, const PFmla, const PFmla);
  bool (*overlap_ck_fn) (PFmlaCtx*, const PFmla, const PFmla);
  bool (*subseteq_ck_fn) (PFmlaCtx*, const PFmla, const PFmla);

  PFmla (*make_fn) (PFmlaCtx*);
  void (*free_fn) (PFmlaCtx*, PFmla);

  void (*vbl_eql_fn) (PFmlaCtx*, PFmla*, uint, uint);
  void (*vbl_eqlc_fn) (PFmlaCtx*, PFmla*, uint, uint);
  void (*vbl_img_eql_fn) (PFmlaCtx*, PFmla*, uint, uint);
  void (*vbl_img_eqlc_fn) (PFmlaCtx*, PFmla*, uint, uint);

  void (*ctx_commit_initialization_fn) (PFmlaCtx*);
  void* (*ctx_lose_fn) (PFmlaCtx*);
  uint (*ctx_add_vbl_list_fn) (PFmlaCtx*);
  void (*ctx_add_to_vbl_list_fn) (PFmlaCtx*, uint, uint);
};


void
init1_PFmlaCtx (PFmlaCtx* ctx, const PFmlaOpVT* vt);
void
commit_initialization_PFmlaCtx (PFmlaCtx* ctx);
void
free_PFmlaCtx (PFmlaCtx* ctx);


void
wipe1_PFmla (PFmla* g, bool phase);

void
iden_PFmla (PFmla* b, const PFmla a);
void
not_PFmla (PFmla* b, const PFmla a);
void
and_PFmla (PFmla* c, const PFmla a, const PFmla b);
void
or_PFmla (PFmla* c, const PFmla a, const PFmla b);
void
nimp_PFmla (PFmla* c, const PFmla a, const PFmla b);
void
xnor_PFmla (PFmla* c, const PFmla a, const PFmla b);

bool
tautology_ck_PFmla (const PFmla g);
bool
unsat_ck_PFmla (const PFmla g);
bool
equiv_ck_PFmla (const PFmla a, const PFmla b);
bool
overlap_ck_PFmla (const PFmla a, const PFmla b);
bool
subseteq_ck_PFmla (const PFmla a, const PFmla b);
void
smooth_vbls_PFmla (PFmla* dst, const PFmla a, uint list_id);
void
subst_vbls_PFmla (PFmla* dst, const PFmla a, uint list_id_new, uint list_id_old);
void
pre_PFmla (PFmla* dst, const PFmla a);
void
pre1_PFmla (PFmla* dst, const PFmla a, const PFmla b);
void
img_PFmla (PFmla* dst, const PFmla a);
void
img1_PFmla (PFmla* dst, const PFmla a, const PFmla b);
void
eql_PFmlaVbl (PFmla* dst, const PFmlaVbl* a, const PFmlaVbl* b);
void
eqlc_PFmlaVbl (PFmla* dst, const PFmlaVbl* a, uint x);
void
img_eql_PFmlaVbl (PFmla* dst, const PFmlaVbl* a, const PFmlaVbl* b);
void
img_eqlc_PFmlaVbl (PFmla* dst, const PFmlaVbl* a, uint x);

uint
add_vbl_PFmlaCtx (PFmlaCtx* ctx, const char* name, uint domsz);
uint
add_vbl_list_PFmlaCtx (PFmlaCtx* ctx);
void
add_to_vbl_list_PFmlaCtx (PFmlaCtx* ctx, uint listid, uint vblid);

qual_inline
  PFmla
dflt_PFmla ()
{
  return NULL;
}

qual_inline
  PFmla
dflt1_PFmla (bool phase)
{
  PFmla g = dflt_PFmla ();
  wipe1_PFmla (&g, phase);
  return g;
}

qual_inline
  void
init_PFmla (PFmla* g)
{
  *g = dflt_PFmla ();
}

qual_inline
  void
init1_PFmla (PFmla* g, bool phase)
{
  *g = dflt1_PFmla (phase);
}

qual_inline
  PFmla
cons_PFmla (PFmlaCtx* ctx)
{
  PFmla g = ctx->vt->make_fn (ctx);
  g->ctx = ctx;
  return g;
}

qual_inline
  Trit
phase_of_PFmla (const PFmla g)
{
  if (!g)  return Nil;
  if (!g->ctx)  return Yes;
  return May;
}

qual_inline
  void
lose_PFmla (PFmla* g)
{
  if (phase_of_PFmla (*g) == May)
    (*g)->ctx->vt->free_fn ((*g)->ctx, *g);
}

qual_inline
  void
lose_PFmlaVbl (PFmlaVbl* x)
{
  lose_AlphaTab (&x->name);
  lose_AlphaTab (&x->img_name);
}

qual_inline
  void
wipe_PFmla (PFmla* g)
{
  lose_PFmla (g);
  *g = 0;
}

qual_inline
  PFmlaVbl*
vbl_of_PFmlaCtx (PFmlaCtx* ctx, uint id)
{
  return (PFmlaVbl*) elt_LgTable (&ctx->vbls, id);
}

qual_inline
  PFmlaVbl*
vbl_lookup_PFmlaCtx (PFmlaCtx* ctx, const char* s)
{
  AlphaTab alpha = dflt1_AlphaTab (s);
  Assoc* assoc = lookup_Associa (&ctx->vbl_map, &alpha);
  return (PFmlaVbl*) val_of_Assoc (assoc);
}

#endif

