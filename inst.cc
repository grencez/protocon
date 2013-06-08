/**
 * \file inst.cc
 *
 * Functions to set up problem instances.
 */

#include "inst.hh"
#include "xnsys.hh"
#include <stdio.h>

/** Increment followed by modulo.**/
static
  uint
incmod(uint i, uint by, uint n)
{
  return (i + by) % n;
}

/** Decrement followed by modulo.**/
static
  uint
decmod(uint i, uint by, uint n)
{
  return (i + n - (by % n)) % n;
}

/** Create a unidirectional ring topology.**/
static
  void
UnidirectionalRing(XnNet& topo, uint npcs, uint domsz, const char* basename)
{
  // Build a bidirectional ring where each process P_i
  // has variable m_i of domain size 3.
  for (uint i = 0; i < npcs; ++i) {
    char name[10];
    sprintf(name, "%s%u", basename, i);

    XnPc& pc = Grow1(topo.pcs);
    pc.addVbl(XnVbl(name, domsz));
    pc.addPriv(decmod(i, 1, npcs), 0);
  }
}

/** Create a bidirectional ring topology.
 **/
static
  void
BidirectionalRing(XnNet& topo, uint npcs, uint domsz, const char* basename)
{
  // Build a bidirectional ring where each process P_i
  // has variable m_i of domain size 3.
  for (uint i = 0; i < npcs; ++i) {
    char name[10];
    sprintf(name, "%s%u", basename, i);

    XnPc& pc = Grow1(topo.pcs);
    pc.addVbl(XnVbl(name, domsz));
    pc.addPriv(decmod(i, 1, npcs), 0);
    pc.addPriv(incmod(i, 1, npcs), 0);
  }
}

/**
 * Return the states for which only one process has a token.
 * \param tokenPFs  Formulas for each process having a token.
 */
static
  PF
SingleTokenPF(const vector<PF>& tokenPFs)
{
  const uint n = tokenPFs.size();
  vector<PF> singleToken(n, PF(true));
  for (uint i = 0; i < n; ++i) {
    for (uint j = 0; j < n; ++j) {
      if (j == i) {
        // Process pcIdx has the only token
        // in the /singleToken[j]/ formula.
        singleToken[j] &= tokenPFs[i]; 
      }
      else {
        // Process pcIdx does not have a token
        // in the /singleToken[j]/ formula.
        singleToken[j] -= tokenPFs[i]; 
      }
    }
  }

  PF pf( false );
  for (uint i = 0; i < n; ++i) {
    pf |= singleToken[i];
  }
  return pf;
}

/**
 * Performs the 3 color on a ring problem.  Each process must be assigned
 * a color such that it'd color is not the same as either of its
 * neighbors.  The domain is [0,2], corresponding to each of 3 arbitrary
 * colors.
 *
 * \param sys  The system context
 * \param npcs The number of processes
 */
  void
InstThreeColoringRing(XnSys& sys, uint npcs)
{
  // Initializes the system as a bidirectional ring with a 3 value domain
  // and the topology defined in sys
  XnNet& topo=sys.topology;
  BidirectionalRing(topo,npcs,3,"c");

  // Commit to using this topology, and initilize MDD stuff
  sys.commitInitialization();
  sys.invariant=true;

  for(uint pcidx=0; pcidx<npcs; pcidx++){

    // mq is the current process, mp is the left process,
    // and mr is the right process.
    const PFVbl mp=topo.pfVblR(pcidx,0);
    const PFVbl mq=topo.pfVbl (pcidx,0);
    const PFVbl mr=topo.pfVblR(pcidx,1);

    // Add to the accepting states all of the states where
    // mq is a different color than both mp and mr
    sys.invariant &= (mp!=mq) && (mq!=mr);
  }
}

  void
TestMakingSumNotTwo(uint npcs)
{
  Xn::Net topology;
  Xn::VblSymm* vbl_symm = topology.add_variables("x", npcs, 3);

  // Create a new symmetry for each process.
  for (uint i = 0; i < npcs; ++i) {
    char name[10];
    char idxname[10];
    sprintf(name, "P%u", i);
    Xn::PcSymm* pc_symm = topology.add_processes(name, 1);

    // Make this f(j) = i-1
    Xn::NatMap indices(1);
    indices.membs[0] = (int)i - 1;
    sprintf(idxname, "%d", indices.membs[0]);
    indices.expression_chunks[0] = idxname;
    topology.add_read_access(pc_symm, vbl_symm, indices);

    // Now make this f(j) = i
    indices.membs[0] = (int)i;
    sprintf(idxname, "%d", indices.membs[0]);
    indices.expression_chunks[0] = idxname;
    topology.add_write_access(pc_symm, vbl_symm, indices);

    // Now make this f(j) = i+1
    indices.membs[0] = (int)i + 1;
    sprintf(idxname, "%d", indices.membs[0]);
    indices.expression_chunks[0] = idxname;
    topology.add_read_access(pc_symm, vbl_symm, indices);
  }
}

/**
 * Performs the 2 coloring on a ring problem.
 *
 * \param sys  Return value. The system context.
 * \param npcs The number of processes.
 */
  void
InstTwoColoringRing(XnSys& sys, uint npcs)
{
  if (npcs % 2 == 1) {
    DBog1( "Number of processes is even (%u), this should fail!", npcs );
  }
  XnNet& topo = sys.topology;
  UnidirectionalRing(topo, npcs, 2, "c");

  // Commit to using this topology.
  // MDD stuff is initialized.
  sys.commitInitialization();

  sys.invariant = true;
  // For each process P[i],
  for (uint pcidx = 0; pcidx < npcs; ++pcidx) {
    // c[i-1]
    const PFVbl c_lo = topo.pfVblR(pcidx, 0);
    // c[i]
    const PFVbl c_me = topo.pfVbl (pcidx, 0);
    // c[i] != c[i-1]
    sys.invariant &= (c_me != c_lo);
  }
}

  void
InstMatching(XnSys& sys, uint npcs)
{
  XnNet& topo = sys.topology;
  BidirectionalRing(topo, npcs, 3, "m");

  // Commit to using this topology.
  // MDD stuff is initialized.
  sys.commitInitialization();
  sys.invariant = true;
  // Set priorities.
  for (uint pcIdx = 0; pcIdx < npcs; ++pcIdx) {
    uint niceIdx0 = 2 * pcIdx;
    uint niceIdx1 = 2 * (npcs - pcIdx - 1) + 1;
    uint niceIdx = (niceIdx0 < niceIdx1) ? niceIdx0 : niceIdx1;
    sys.niceIdxFo(pcIdx, niceIdx);
  }

  for (uint pcidx = 0; pcidx < npcs; ++pcidx) {
    const PFVbl mp = topo.pfVblR(pcidx, 0);
    const PFVbl mq = topo.pfVbl (pcidx, 0);
    const PFVbl mr = topo.pfVblR(pcidx, 1);

    // 0 = Self, 1 = Left, 2 = Right
    sys.invariant &=
      (mp == 1 && mq == 0 && mr == 2) || // ( left,  self, right)
      (mp == 2 && mq == 1           ) || // (right,  left,     X)
      (           mq == 2 && mr == 1);   // (    X, right,  left)
  }
}

/**
 * Set up a sum-not-(l-1) instance.
 * You are free to choose the domain size and the target (to miss).
 **/
  void
InstSumNot(XnSys& sys, uint npcs, uint domsz, uint target)
{
  XnNet& topo = sys.topology;
  UnidirectionalRing(topo, npcs, domsz, "x");

  // Commit to using this topology.
  // MDD stuff is initialized.
  sys.commitInitialization();

  sys.invariant = true;
  // For each process P[r],
  for (uint r = 0; r < npcs; ++r) {
    const PFVbl x_lo = topo.pfVblR(r, 0);
    const PFVbl x_me = topo.pfVbl (r, 0);

    // (x[r-1] + x[r]) % domsz != target
    // Equivalently:
    // For all i,
    for (uint i = 0; i < domsz; ++i) {
      // (x[r-1] == i) implies (x[r] != ((target - i) % domsz))
      sys.invariant &= ((x_lo != i) | (x_me != decmod(target, i, domsz)));
    }
  }
}

/** Agreement.
 * Only enforce that a subset of the invariant be closed.
 **/
  void
InstAgreementRing(XnSys& sys, uint npcs)
{
  XnNet& topo = sys.topology;
  // Build a bidirectional ring.
  for (uint i = 0; i < npcs; ++i) {
    char name[10];
    XnPc& pc = Grow1(topo.pcs);

    sprintf(name, "a%u", i);
    pc.addVbl(XnVbl(name, npcs));

    sprintf(name, "x%u", i);
    pc.addVbl(XnVbl(name, npcs));
    sys.markShadowVbl(i, pc.wvbls.size()-1);

    pc.addPriv(decmod(i, 1, npcs), 0);
    //pc.addPriv(incmod(i, 1, npcs), 0);
  }

  // Commit to using this topology, and initilize MDD stuff
  sys.commitInitialization();

  // Set priorities.
  for (uint pcIdx = 0; pcIdx < npcs; ++pcIdx) {
    sys.niceIdxFo(pcIdx, npcs-pcIdx-1);
  }

  sys.invariant = true;
  for (uint pcIdx = 1; pcIdx < npcs; ++pcIdx) {
    sys.invariant &= (topo.pfVbl(pcIdx, 1) == topo.pfVbl(pcIdx-1, 1));
  }
}


/** Dijkstra's original token ring
 * with each process's variable with a domain of size N.
 **/
  void
InstDijkstraTokenRing(XnSys& sys, uint npcs)
{
  XnNet& topo = sys.topology;
  UnidirectionalRing(topo, npcs, npcs, "x");

  // Commit to using this topology, and initilize MDD stuff
  sys.commitInitialization();
  sys.synLegit = true;
  sys.liveLegit = true;

  // Set priorities.
  for (uint pcIdx = 0; pcIdx < npcs; ++pcIdx) {
    sys.niceIdxFo(pcIdx, npcs-pcIdx-1);
  }

  // Formulas for each process having a token.
  vector<PF> tokenPFs(npcs);
  // (x[0] == x[N-1])
  tokenPFs[0] = (topo.pfVbl(0, 0) == topo.pfVblR(0, 0));
  for (uint pcIdx = 1; pcIdx < npcs; ++pcIdx) {
    // (x[i] == x[i-1])
    tokenPFs[pcIdx] = (topo.pfVbl(pcIdx, 0) != topo.pfVblR(pcIdx, 0));
  }
  sys.invariant = SingleTokenPF(tokenPFs);
}

/** Gouda's three bit token ring.**/
  void
InstThreeBitTokenRing(XnSys& sys, uint npcs)
{
  XnNet& topo = sys.topology;
  // Build a bidirectional ring where each process P_i
  // has variable m_i of domain size 3.
  for (uint i = 0; i < npcs; ++i) {
    char name[10];
    XnPc& pc = Grow1(topo.pcs);

    sprintf(name, "e%u", i);
    pc.addVbl(XnVbl(name, 2));
    sprintf(name, "t%u", i);
    pc.addVbl(XnVbl(name, 2));
    sprintf(name, "ready%u", i);
    pc.addVbl(XnVbl(name, 2));

    pc.addPriv(decmod(i, 1, npcs), 0);
    pc.addPriv(decmod(i, 1, npcs), 1);
  }

  // Commit to using this topology, and initilize MDD stuff
  sys.commitInitialization();
  sys.synLegit = true;
  sys.liveLegit = true;

  // Set priorities.
  for (uint pcIdx = 0; pcIdx < npcs; ++pcIdx) {
    sys.niceIdxFo(pcIdx, pcIdx);
  }

  // Formulas for each process having a token.
  vector<PF> tokenPFs(npcs);
  // Formula for existence of an enabled process.
  PF existEnabled(false);

  // e[0] == e[N-1]
  existEnabled |= (topo.pfVbl(0, 0) == topo.pfVblR(0, 0));
  // t_{i-1} == t_i
  tokenPFs[0] = (topo.pfVbl(0, 1) == topo.pfVblR(0, 1));

  for (uint pcIdx = 1; pcIdx < npcs; ++pcIdx) {
    // e_i != e_{i-1}
    existEnabled |= (topo.pfVbl(pcIdx, 0) != topo.pfVblR(pcIdx, 0));
    // t_{i-1} != t_i
    tokenPFs[pcIdx] = (topo.pfVbl(pcIdx, 1) != topo.pfVblR(pcIdx, 1));
  }

  sys.invariant = (SingleTokenPF(tokenPFs) & existEnabled);
}

/** Dijkstra's two bit token "spring".**/
  void
InstTwoBitTokenSpring(XnSys& sys, uint npcs)
{
  if (npcs < 2) {
    DBog1( "Not enough processes (%u), need at least 2.", npcs );
    exit(1);
  }

  XnNet& topo = sys.topology;
  // Build a bidirectional ring where each process P_i
  // has variable m_i of domain size 3.
  for (uint i = 0; i < npcs; ++i) {
    char name[10];
    XnPc& pc = Grow1(topo.pcs);

    sprintf(name, "x%u", i);
    pc.addVbl(XnVbl(name, 2));
    sprintf(name, "up%u", i);
    pc.addVbl(XnVbl(name, 2));

    if (i > 0) {
      pc.addPriv(i-1, 0);
    }
    if (i < npcs-1) {
      pc.addPriv(i+1, 0);
      pc.addPriv(i+1, 1);
    }
  }
  // Commit to using this topology, and initilize MDD stuff
  sys.commitInitialization();
  sys.synLegit = true;
  sys.liveLegit = true;

  // Formulas for each process having a token.
  vector<PF> tokenPFs(npcs);
  // ((x[0] == x[1]) &&
  //  (up[1] == 0))
  tokenPFs[0] = ((topo.pfVbl(0, 0) == topo.pfVbl(1, 0)) &&
                 (topo.pfVbl(1, 1) == 0));
  for (uint pcIdx = 1; pcIdx < npcs-1; ++pcIdx) {
      // ((x[i] != x[i-1])
      //  ||
      //  ((x[i] == x[i+1]) &&
      //  (up[i] == 1) &&
      //  (up[i+1] == 0)))
      tokenPFs[pcIdx] =
        ((topo.pfVbl(pcIdx, 0) != topo.pfVbl(pcIdx-1, 0))
         ||
         ((topo.pfVbl(pcIdx, 0) == topo.pfVbl(pcIdx+1, 0)) &&
          (topo.pfVbl(pcIdx, 1) == 1) &&
          (topo.pfVbl(pcIdx+1, 1) == 0)));
  }
  // (x[N-1] != x[N-2])
  tokenPFs[npcs-1] = (topo.pfVbl(npcs-1, 0) != topo.pfVbl(npcs-2, 0));

  // ((exactly_one_token) && (up[0] == 1) && (up[N-1] == 0))
  sys.invariant = (SingleTokenPF(tokenPFs) &
                   (topo.pfVbl(0, 1) == 1) &
                   (topo.pfVbl(npcs-1, 1) == 0));
}

/** Testing token ring.
 * Only enforce that a subset of the invariant be closed.
 **/
  void
InstTestTokenRing(XnSys& sys, uint npcs)
{
  //const uint domsz = 3;
  XnNet& topo = sys.topology;
  // Build a unidirectional ring where each process P_i.
  for (uint i = 0; i < npcs; ++i) {
    char name[10];
    XnPc& pc = Grow1(topo.pcs);

    sprintf(name, "x%u", i);
    pc.addVbl(XnVbl(name, 2));
    sys.markShadowVbl(i, pc.wvbls.size()-1);

    sprintf(name, "t%u", i);
    pc.addVbl(XnVbl(name, 2));
    pc.addPriv(decmod(i, 1, npcs), pc.wvbls.size()-1);

    sprintf(name, "e%u", i);
    pc.addVbl(XnVbl(name, 2));
    pc.addPriv(decmod(i, 1, npcs), pc.wvbls.size()-1);

    sprintf(name, "ready%u", i);
    pc.addVbl(XnVbl(name, 2));
  }

  // Commit to using this topology, and initilize MDD stuff
  sys.commitInitialization();

  // Set priorities.
  //for (uint pcIdx = 0; pcIdx < npcs; ++pcIdx) {
  //  sys.niceIdxFo(pcIdx, npcs-pcIdx-1);
  //}

  for (uint actId = 0; actId < topo.nPossibleActs(); ++actId)
  {
    const XnAct& act = topo.action(actId);
    bool add = false;
    uint t_me = act.w0[1];
    uint e_me = act.w0[2];
    uint r_me = act.w0[3];
    uint t_lo = act.r0[1];
    uint e_lo = act.r0[2];
    uint t_img = act.w1[1];
    uint e_img = act.w1[2];
    uint r_img = act.w1[3];

    if (act.pcIdx == 0) {
      if (e_me == e_lo && t_me != t_lo) {
        if (e_img != e_me && r_img == 0 && t_img == t_me) {
          //add = true;
        }
      }
      else if (e_me == e_lo && t_me == t_lo && t_me == 0 && r_me == 0)
      {
        if (e_img != e_me && r_img == 1 && t_img == t_me) {
          //add = true;
        }
      }
      else if (e_me == e_lo && t_me == t_lo && (t_me == 1 || r_me == 1))
      {
        if (e_img != e_me && t_img != t_me && r_img == 0) {
          //add = true;
        }
      }
    }
    else {
      if (e_me != e_lo && t_me == t_lo) {
        if (e_img != e_me && r_img == 0 && t_img == t_me) {
          //add = true;
        }
      }
      else if (e_me != e_lo && t_me != t_lo && t_me == 0 && r_me == 0)
      {
        if (e_img != e_me && r_img == 1 && t_img == t_me) {
          //add = true;
        }
      }
      else if (e_me != e_lo && t_me != t_lo && (t_me == 1 || r_me == 1))
      {
        if (e_img != e_me && t_img != t_me && r_img == 0) {
          //add = true;
        }
      }
    }
    if (add) {
      sys.actions.push_back(actId);
    }
  }

  // Formulas for each process having a token.
  vector<PF> tokenPFs(npcs);

  for (uint pcidx = 0; pcidx < npcs; ++pcidx) {
    const uint pcidx_p = decmod(pcidx, 1, npcs);
    Set< Cx::Tuple<uint,2> > vbls;
    vbls |= Cx::Tuple<uint,2>(pcidx, 0);
    vbls |= Cx::Tuple<uint,2>(pcidx_p, 0);

    if (pcidx == 0) {
      const Cx::PFmla& pf =
        (topo.pfVbl(pcidx, 0) == topo.pfVbl(pcidx_p, 0))
        & (topo.pfVblPrimed(pcidx, 0) != topo.pfVblPrimed(pcidx_p, 0));
      sys.addShadowAct(pf, vbls);
    }
    else {
      const Cx::PFmla& pf =
        (topo.pfVbl(pcidx, 0) != topo.pfVbl(pcidx_p, 0))
        & (topo.pfVblPrimed(pcidx, 0) == topo.pfVblPrimed(pcidx_p, 0));
      sys.addShadowAct(pf, vbls);
    }
  }


  // x[0] == x[N-1]
  tokenPFs[0] = (topo.pfVbl(0, 0) == topo.pfVbl(npcs-1, 0));
  for (uint pcIdx = 1; pcIdx < npcs; ++pcIdx) {
    // x[i] != x[i-1]
    tokenPFs[pcIdx] = (topo.pfVbl(pcIdx, 0) != topo.pfVbl(pcIdx-1, 0));
  }

  sys.invariant = (SingleTokenPF(tokenPFs));
}

