
#include "synthesis.hh"

#include "stabilization.hh"
#include "cx/fileb.hh"
#include "pla.hh"

typedef Cx::PFmla PF;

  bool
candidate_actions(vector<uint>& candidates, const Xn::Sys& sys)
{
  const Xn::Net& topo = sys.topology;

  if (!sys.invariant.sat_ck()) {
    DBog0( "Invariant is empty!" );
    return false;
  }

  if (sys.invariant.tautology_ck()) {
    DBog0( "All states are invariant!" );
    if (!sys.shadow_puppet_synthesis_ck()) {
      return true;
    }
  }

  for (uint actidx = 0; actidx < topo.n_possible_acts; ++actidx) {
    if (actidx != topo.representative_action_index(actidx)) {
      continue;
    }

    bool add = true;

    Xn::ActSymm act;
    topo.action(act, actidx);
    const Xn::PcSymm& pc_symm = *act.pc_symm;
    const Cx::PFmla& act_pf = topo.action_pfmla(actidx);
    if (add) {
      add = !act_pf.overlap_ck(pc_symm.forbid_pfmla);
    }

    // Check for self-loops.
    if (add) {
      bool selfloop = true;
      bool shadow_exists = false;
      bool shadow_selfloop = true;
      for (uint j = 0; j < pc_symm.wvbl_symms.sz(); ++j) {
        if (pc_symm.wvbl_symms[j]->pure_shadow_ck()) {
          shadow_exists = true;
          if (act.assign(j) != act.aguard(j)) {
            shadow_selfloop = false;
          }
        }
        else {
          if (act.assign(j) != act.aguard(j)) {
            selfloop = false;
          }
        }
      }
      add = !selfloop;
      if (selfloop) {
        if (shadow_exists && shadow_selfloop) {
          add = true;
        }
      }
      if (false && selfloop) {
        OPut((DBogOF << "Action " << actidx << " is a self-loop: "), act) << '\n';
      }
    }

    if (add && sys.direct_invariant_ck()) {
      if (!act_pf.img(sys.invariant).subseteq_ck(sys.invariant)) {
        add = false;
        if (false) {
          OPut((DBogOF << "Action " << actidx << " breaks closure: "), act) << '\n';
        }
      }
      else if (!(act_pf & sys.invariant).subseteq_ck(sys.shadow_pfmla | sys.shadow_self)) {
        add = false;
        if (false) {
          OPut((DBogOF << "Action " << actidx << " breaks shadow protocol: "), act) << '\n';
        }
      }
    }

    if (add) {
      candidates.push_back(actidx);
    }
  }
  if (candidates.size() == 0) {
    DBog0( "No candidates actions!" );
    return false;
  }

  return true;
}

/**
 * Check if two actions can coexist in a
 * deterministic protocol of self-disabling processes.
 */
  bool
coexist_ck(const Xn::ActSymm& a, const Xn::ActSymm& b)
{
  if (a.pc_symm != b.pc_symm)  return true;
  const Xn::PcSymm& pc = *a.pc_symm;

  bool pure_shadow_img_eql = true;
  bool puppet_img_eql = true;
  for (uint i = 0; i < pc.rvbl_symms.sz(); ++i) {
    if (!pc.write_flags[i] && pc.rvbl_symms[i]->puppet_ck()) {
      if (a.guard(i) != b.guard(i))
        puppet_img_eql = false;
    }
  }
  for (uint i = 0; i < pc.wvbl_symms.sz(); ++i) {
    if (pc.wvbl_symms[i]->puppet_ck()) {
      if (a.assign(i) != b.assign(i))
        puppet_img_eql = false;
    }
    else {
      if (a.assign(i) != b.assign(i))
        pure_shadow_img_eql = false;
    }
  }
  if (puppet_img_eql && !pure_shadow_img_eql)
    return false;

  bool enabling = true;
  bool deterministic = false;
  for (uint j = 0; enabling && j < pc.rvbl_symms.sz(); ++j) {
    if (pc.rvbl_symms[j]->pure_shadow_ck())
      continue;
    if (a.guard(j) != b.guard(j)) {
      deterministic = true;
      if (!pc.write_ck(j)) {
        enabling = false;
      }
    }
  }

  if (!deterministic)  return false;
  if (!enabling)  return true;

  bool a_enables = true;
  bool b_enables = true;
  for (uint j = 0; j < pc.wvbl_symms.sz(); ++j) {
    if (pc.wvbl_symms[j]->pure_shadow_ck())
      continue;
    if (a.assign(j) != b.aguard(j)) {
      a_enables = false;
    }
    if (b.assign(j) != a.aguard(j)) {
      b_enables = false;
    }
  }
  if (a_enables || b_enables)
    return false;
  return true;
}

/** Rank the deadlocks by how many actions can resolve them.*/
  void
RankDeadlocksMCV(vector<DeadlockConstraint>& dlsets,
                 const Xn::Net& topo,
                 const vector<uint>& actions,
                 const PF& deadlockPF)
{
  dlsets.clear();
  dlsets.push_back(DeadlockConstraint(deadlockPF));

  Cx::LgTable< Set<uint> > actidcs;
  {
    Cx::Map< Cx::Tuple<uint,2>, uint > same_pre;
    for (uint i = 0; i < actions.size(); ++i) {
      const uint actidx = actions[i];
      Xn::ActSymm act;
      topo.action(act, actions[i]);
      Cx::Tuple<uint,2> key;
      key[0] = topo.pc_symms.index_of(act.pc_symm);
      key[1] = act.pre_idx;

      const uint* j = same_pre.lookup(key);
      if (j) {
        actidcs[*j] << actidx;
      }
      else {
        same_pre[key] = actidcs.sz();
        actidcs.push(Set<uint>(actidx));
      }
    }
  }

  for (uint i = 0; i < actidcs.sz(); ++i) {
    const uint actidx = actidcs[i].elem();
    const Cx::PFmla& guard = topo.action_pfmla(actidx).pre();

    for (uint j = dlsets.size(); j > 0; --j) {
      const Cx::PFmla& resolved = dlsets[j-1].deadlockPF & guard;

      if (resolved.sat_ck()) {
        //dlsets[j-1].deadlockPF -= guard;
        dlsets[j-1].deadlockPF -= resolved;
        const uint dst_idx = j-1 + actidcs[i].sz();
        if (dst_idx >= dlsets.size()) {
          dlsets.resize(dst_idx+1);
        }
        dlsets[dst_idx].deadlockPF |= resolved;
      }
    }
  }

  for (uint i = 0; i < actidcs.sz(); ++i) {
    const uint actidx = actidcs[i].elem();
    const Cx::PFmla& guard = topo.action_pfmla(actidx).pre();
    for (uint j = 0; j < dlsets.size(); ++j) {
      if (guard.overlap_ck(dlsets[j].deadlockPF)) {
        dlsets[j].candidates |= actidcs[i];
      }
    }
  }

  if (DBog_RankDeadlocksMCV) {
    for (uint i = 0; i < dlsets.size(); ++i) {
      if (!dlsets[i].deadlockPF.tautology_ck(false)) {
        DBog2( "Rank %u has %u actions.", i, (uint) dlsets[i].candidates.size() );
      }
    }
  }
}

/**
 * Revise the ranks of deadlocks which are ranked by number candidate actions
 * which can resolve them.
 * \param adds  Actions which were added to the program.
 * \param dels  Actions which were pruned from the list of candidates.
 */
  void
ReviseDeadlocksMCV(vector<DeadlockConstraint>& dlsets,
                   const Xn::Net& topo,
                   const Set<uint>& adds,
                   const Set<uint>& dels)
{
  PF addGuardPF(false);
  PF delGuardPF(false);
  for (Set<uint>::const_iterator it = adds.begin(); it != adds.end(); ++it) {
    addGuardPF |= topo.action_pfmla(*it).pre();
  }
  for (Set<uint>::const_iterator it = dels.begin(); it != dels.end(); ++it) {
    delGuardPF |= topo.action_pfmla(*it).pre();
  }

  for (uint i = 1; i < dlsets.size(); ++i) {
    Set<uint>& candidates1 = dlsets[i].candidates;
    PF& deadlockPF1 = dlsets[i].deadlockPF;

    Set<uint> addCandidates( candidates1 & adds );
    Set<uint> delCandidates( candidates1 & dels );

    Set<uint> diffCandidates1; // To remove from this rank.

    diffCandidates1 = (candidates1 & addCandidates);
    if (!diffCandidates1.empty()) {
      candidates1 -= diffCandidates1;
      diffCandidates1.clear();

      deadlockPF1 -= addGuardPF;
      Set<uint>::iterator it;
      for (it = candidates1.begin(); it != candidates1.end(); ++it) {
        uint actId = *it;
        const PF& candidateGuardPF = topo.action_pfmla(actId);
        if (!deadlockPF1.overlap_ck(candidateGuardPF)) {
          // Action no longer resolves any deadlocks in this rank.
          diffCandidates1 << actId;
        }
      }
      candidates1 -= diffCandidates1;
    }

    diffCandidates1 = (candidates1 & delCandidates);
    if (!diffCandidates1.empty()) {
      const PF& diffDeadlockPF1 = (deadlockPF1 & delGuardPF);
      deadlockPF1 -= diffDeadlockPF1;

      vector<DeadlockConstraint>
        diffDeadlockSets( i+1, DeadlockConstraint(PF(false)) );
      diffDeadlockSets[i].deadlockPF = diffDeadlockPF1;

      Set<uint>::iterator it;

      uint minidx = i;
      for (it = diffCandidates1.begin(); it != diffCandidates1.end(); ++it) {
        const uint actId = *it;
        const PF& candidateGuardPF = topo.action_pfmla(actId).pre();
        Claim2( minidx ,>, 0 );
        for (uint j = minidx; j < diffDeadlockSets.size(); ++j) {
          const PF& diffPF =
            (candidateGuardPF & diffDeadlockSets[j].deadlockPF);
          if (!diffPF.tautology_ck(false)) {
            diffDeadlockSets[j-1].deadlockPF |= diffPF;
            diffDeadlockSets[j].deadlockPF -= diffPF;
            if (j == minidx) {
              -- minidx;
            }
          }
        }
      }

      candidates1 -= diffCandidates1;
      diffCandidates1.clear();
      diffDeadlockSets[i].deadlockPF = deadlockPF1;

      for (it = candidates1.begin(); it != candidates1.end(); ++it) {
        const uint actId = *it;
        const PF& candidateGuardPF = topo.action_pfmla(actId).pre();
        if (!candidateGuardPF.overlap_ck(diffDeadlockPF1)) {
          // This candidate is not affected.
          diffDeadlockSets[i].candidates << actId;
          continue;
        }
        for (uint j = minidx; j < diffDeadlockSets.size(); ++j) {
          if (candidateGuardPF.overlap_ck(diffDeadlockSets[j].deadlockPF)) {
            diffDeadlockSets[j].candidates << actId;
          }
        }
      }

      for (uint j = minidx; j < i; ++j) {
        dlsets[j].deadlockPF |= diffDeadlockSets[j].deadlockPF;
        dlsets[j].candidates |= diffDeadlockSets[j].candidates;
      }
      candidates1 &= diffDeadlockSets[i].candidates;
    }
  }
}

static void
pick_action_candidates(Set<uint>& ret_candidates,
                       const PartialSynthesis& inst,
                       const AddConvergenceOpt& opt,
                       uint dlset_idx);

/**
 * Pick the next candidate action to use.
 * The most constrained variable (MCV) heuristic may be used.
 *
 * \param ret_actidx  Return value. Action to use.
 * \return True iff an action was picked. It should return
 *   true unless you're doing it wrong).
 */
  bool
PickActionMCV(uint& ret_actidx,
              const PartialSynthesis& tape,
              const AddConvergenceOpt& opt)
{
  typedef AddConvergenceOpt Opt;
  const Opt::PickActionHeuristic& pick_method = opt.pick_method;

  if (pick_method == Opt::FullyRandomPick) {
    for (uint inst_idx = 0; inst_idx < tape.sz(); ++inst_idx) {
      if (tape[inst_idx].candidates.size() > 0) {
        uint idx = tape[inst_idx].ctx->urandom.pick(tape[inst_idx].candidates.size());
        ret_actidx = tape[inst_idx].candidates[idx];
        return true;
      }
    }
    return false;
  }

  // Do most constrained variable (MCV).
  // That is, find an action which resolves a deadlock which
  // can only be resolved by some small number of actions.
  uint mcv_dlset_idx = 0;
  for (uint inst_idx = 0; inst_idx < tape.sz(); ++inst_idx) {
    if (tape[inst_idx].no_partial)  continue;
    const PartialSynthesis& inst = tape[inst_idx];
    for (uint i = 0; i < inst.mcv_deadlocks.size(); ++i) {
      if (mcv_dlset_idx > 0 && i >= mcv_dlset_idx)
        break;
      const Set<uint>& inst_candidates = inst.mcv_deadlocks[i].candidates;
      if (!inst_candidates.empty())
        mcv_dlset_idx = i;
    }
  }
  Set<uint> candidates;
  uint mcv_inst_idx = 0;
  for (uint inst_idx = 0; inst_idx < tape.sz(); ++inst_idx) {
    if (tape[inst_idx].no_partial)
      continue;
    const PartialSynthesis& inst = tape[inst_idx];
    if (mcv_dlset_idx >= inst.mcv_deadlocks.size())
      continue;
    Claim2( mcv_dlset_idx ,<, inst.mcv_deadlocks.size() );
    const Set<uint>& inst_candidates = inst.mcv_deadlocks[mcv_dlset_idx].candidates;
    if (inst_candidates.empty())
      continue;

    Set<uint> tmp_candidates;
    pick_action_candidates(tmp_candidates, inst, opt, mcv_dlset_idx);
    if (tmp_candidates.sz() == 0)
      return false;

    if (candidates.sz() == 0 || tmp_candidates.sz() < candidates.sz()) {
      mcv_inst_idx = inst_idx;
      candidates = tmp_candidates;
    }
  }

  if (candidates.sz() == 0) {
    for (uint inst_idx = 0; inst_idx < tape.sz(); ++inst_idx) {
      if (tape[inst_idx].candidates.size() > 0) {
        candidates |= Set<uint>(tape[inst_idx].candidates);
        mcv_inst_idx = inst_idx;
        break;
      }
    }
  }
  Claim2( candidates.sz() ,>, 0 );

  const PartialSynthesis& inst = tape[mcv_inst_idx];
  *inst.log
    << " (lvl " << inst.bt_level
    << ") (psys " << inst.sys_idx
    << ") (mcv " << mcv_dlset_idx
    << ") (mcv-sz " << inst.mcv_deadlocks[mcv_dlset_idx].candidates.sz()
    << ") (rem-sz " << candidates.sz()
    << ")" << inst.log->endl();

  if (opt.randomize_pick) {
    vector<uint> candidates_vec;
    candidates.fill(candidates_vec);
    uint idx = inst.ctx->urandom.pick(candidates_vec.size());
    ret_actidx = candidates_vec[idx];
  }
  else {
    ret_actidx = candidates.elem();
  }
  return true;
}

  void
pick_action_candidates(Set<uint>& ret_candidates,
                       const PartialSynthesis& inst,
                       const AddConvergenceOpt& opt,
                       uint dlset_idx)
{

  typedef AddConvergenceOpt Opt;
  const Opt::PickActionHeuristic& pick_method = opt.pick_method;
  const Opt::NicePolicy& nicePolicy = opt.nicePolicy;
  const Xn::Sys& sys = *inst.ctx->systems[inst.sys_idx];
  const Xn::Net& topo = sys.topology;

  Set<uint> candidates( inst.mcv_deadlocks[dlset_idx].candidates );
  const vector<DeadlockConstraint>& dlsets = inst.mcv_deadlocks;

  Set<uint>::const_iterator it;

  // Try to choose an action which adds a new path to the invariant.
  const Cx::PFmla& reach_pf =
    inst.lo_xn.pre_reach(inst.hi_invariant);
  if (opt.pick_back_reach) {
    Set<uint> candidates_1;
    for (it = candidates.begin(); it != candidates.end(); ++it) {
      const uint actidx = *it;
      const PF& resolve_img =
        topo.action_pfmla(actidx).img(dlsets[dlset_idx].deadlockPF);
      if (reach_pf.overlap_ck(resolve_img)) {
        candidates_1 << actidx;
      }
    }
    candidates = candidates_1;
    if (candidates.empty()) {
      candidates = dlsets[dlset_idx].candidates;
    }
  }


  Map< uint, Set<uint> > biasMap;
  bool biasToMax = true;

  if (nicePolicy == Opt::BegNice) {
    // Only consider actions of highest priority process.
    bool have = false;
    uint niceIdxMin = 0;
    Set<uint> candidates_1;
    for (it = candidates.begin(); it != candidates.end(); ++it) {
      const uint actId = *it;
      const uint pcId = topo.action_pcsymm_index(actId);
      const uint niceIdx = sys.niceIdxOf(pcId);
      if (!have || (niceIdx < niceIdxMin)) {
        have = true;
        candidates_1.clear();
        candidates_1 << actId;
        niceIdxMin = niceIdx;
      }
      else if (niceIdx == niceIdxMin) {
        candidates_1 << actId;
      }
    }
    candidates = candidates_1;
  }

  if (pick_method == Opt::MCVLitePick) {
    biasMap[0] = candidates;
  }
  else if (pick_method == Opt::GreedyPick || pick_method == Opt::GreedySlowPick) {
    biasToMax = true;

    Map< uint, uint > resolveMap;
    for (uint j = dlset_idx; j < dlsets.size(); ++j) {
      const Set<uint>& resolveSet = (candidates & dlsets[j].candidates);
      for (it = resolveSet.begin(); it != resolveSet.end(); ++it) {
        const uint actId = *it;

        uint w = 0; // Weight.
        if (pick_method != Opt::GreedySlowPick) {
          w = j;
        }
        else {
          Set<uint>::const_iterator jt;
          const PF& actPF = topo.action_pfmla(*it);
          for (jt = dlsets[j].candidates.begin();
               jt != dlsets[j].candidates.end();
               ++jt) {
            const uint actId2 = *jt;
            if (actId == actId2) {
              continue;
            }
            const PF& preAct2 = topo.action_pfmla(actId2).pre();
            if (dlsets[j].deadlockPF.overlap_ck(actPF & preAct2)) {
              ++ w;
            }
          }
        }

        uint* n = resolveMap.lookup(actId);
        if (!n) {
          resolveMap[actId] = w;
        }
        else {
          *n += w;
        }
      }
    }

    for (it = candidates.begin(); it != candidates.end(); ++it) {
      const uint actId = *it;
      uint n = *resolveMap.lookup(actId);
      biasMap[n] << actId;
    }
  }
  else if (pick_method == Opt::LCVLitePick) {
    biasToMax = false;

    for (it = candidates.begin(); it != candidates.end(); ++it) {
      const uint actId = *it;
      uint n = 0;
      vector<DeadlockConstraint> tmpDeadlocks( dlsets );
      ReviseDeadlocksMCV(tmpDeadlocks, topo, Set<uint>(actId), Set<uint>());
      for (uint j = 1; j < tmpDeadlocks.size(); ++j) {
        n += tmpDeadlocks[j].candidates.size();
      }

      biasMap[n] << actId;
    }
  }
  else if (pick_method == Opt::LCVHeavyPick) {
    biasToMax = false;

    for (it = candidates.begin(); it != candidates.end(); ++it) {
      const uint actId = *it;
      PartialSynthesis next( inst );
      next.log = &Cx::OFile::null();
      uint n = inst.candidates.size();
      if (next.revise_actions(Set<uint>(actId), Set<uint>()))
      {
        n -= next.candidates.size();
        n /= (next.actions.size() - inst.actions.size());
      }
      //uint n = next.candidates.size() + next.actions.size();
      //uint n = 0;
      //for (uint j = 1; j < next.mcv_deadlocks.size(); ++j) {
      //  n += next.mcv_deadlocks[j].candidates.size() / j;
      //}

      biasMap[n] << actId;
    }
  }
  else if (pick_method == Opt::LCVJankPick) {
    biasToMax = true;
    Map< uint, Set<uint> > overlapSets;

    for (it = candidates.begin(); it != candidates.end(); ++it) {
      overlapSets[*it] = Set<uint>(*it);
    }

    const PF& deadlockPF = dlsets[dlset_idx].deadlockPF;
    for (it = candidates.begin(); it != candidates.end(); ++it) {
      uint actId = *it;
      const PF& actPF = topo.action_pfmla(actId);
      const PF actPrePF = actPF.pre();

      Set<uint>& overlapSet = *overlapSets.lookup(actId);

      Set<uint>::const_iterator jt = it;
      for (++jt; jt != candidates.end(); ++jt) {
        const uint actId2 = *jt;
        const PF& actPF2 = topo.action_pfmla(actId2);
        if (deadlockPF.overlap_ck(actPrePF & actPF2.pre())) {
          overlapSet << actId2;
          *overlapSets.lookup(actId2) << actId;
        }
      }
    }

    bool have = false;
    Set<uint> minOverlapSet;

    Map< uint,Set<uint> >::const_iterator mit;
    for (mit = overlapSets.begin(); mit != overlapSets.end(); ++mit) {
      const Set<uint>& overlapSet = mit->second;
      if (!have || overlapSet.size() < minOverlapSet.size()) {
        have = true;
        minOverlapSet = overlapSet;
      }
    }

    DBog2("(lvl %u) (size %u)", inst.bt_level, minOverlapSet.size());

    for (it = minOverlapSet.begin(); it != minOverlapSet.end(); ++it) {
      const uint actId = *it;
      PartialSynthesis next( inst );
      next.log = &Cx::OFile::null();
      uint n = 0;
      if (next.revise_actions(Set<uint>(actId), Set<uint>()))
        n = next.candidates.size();
      //uint n = next.candidates.size() + next.actions.size();
      //uint n = 0;
      //for (uint j = 1; j < next.mcv_deadlocks.size(); ++j) {
      //  n += next.mcv_deadlocks[j].candidates.size() / j;
      //}
      biasMap[n] << actId;
    }
  }
  else if (pick_method == Opt::ConflictPick) {
    biasToMax = false;
    FlatSet<uint> membs;
    inst.ctx->conflicts.superset_membs(membs, FlatSet<uint>(inst.picks),
                                       FlatSet<uint>(inst.candidates));
    if (membs.overlap_ck(candidates)) {
      biasMap[0] = (candidates & Set<uint>(membs));
    }
#if 0
    else if (membs.sz() > 0) {
      uint idx = inst.ctx->urandom.pick(membs.sz());
      biasMap[0] << membs[idx];
    }
#endif
    else {
      biasMap[0] |= candidates;
    }
  }
  else if (pick_method == Opt::QuickPick) {
    biasToMax = false;
    for (it = candidates.begin(); it != candidates.end(); ++it) {
      uint actId = *it;
      const PF& act_pf = topo.action_pfmla(actId);
      if (sys.shadow_puppet_synthesis_ck()) {
        if (!act_pf.overlap_ck(inst.hi_invariant)) {
          biasMap[0] << actId;
        }
        else {
          biasMap[1] << actId;
        }
        continue;
      }
      if (reach_pf.overlap_ck(act_pf.img())) {
        biasMap[1] << actId;
        if (!(act_pf.pre() <= reach_pf)) {
          biasMap[0] << actId;
        }
      }
      else {
        biasMap[2] << actId;
      }
    }
  }

  if (!biasMap.empty()) {
    const std::pair< uint, Set<uint> >& entry =
      (biasToMax ? *biasMap.rbegin() : *biasMap.begin());
    candidates = entry.second;
  }
  else {
    ret_candidates.clear();
    DBog0( "Bad News: biasMap is empty!!!" );
    return;
  }

  if (nicePolicy == Opt::EndNice) {
    bool have = false;
    uint niceIdxMin = 0;
    Set<uint> candidates_1;
    for (it = candidates.begin(); it != candidates.end(); ++it) {
      const uint actId = *it;
      const uint pcId = topo.action_pcsymm_index(actId);
      const uint niceIdx = sys.niceIdxOf(pcId);
      if (!have || (niceIdx < niceIdxMin)) {
        have = true;
        candidates_1.clear();
        candidates_1 << actId;
        niceIdxMin = niceIdx;
      }
      else if (niceIdx == niceIdxMin) {
        candidates_1 << actId;
      }
    }
    candidates = candidates_1;
  }
  ret_candidates = candidates;
}

/**
 * Do trivial trimming of the candidate actions after using an action.
 * The pruned candidate actions would break our assumption that processes are
 * self-disabling and deterministic.
 */
  void
QuickTrim(Set<uint>& delSet,
          const vector<uint>& candidates,
          const Xn::Net& topo,
          uint actidx)
{
  Xn::ActSymm act0;
  Xn::ActSymm act1;
  for (uint i = 0; i < candidates.size(); ++i) {
    topo.action(act1, candidates[i]);
    for (uint j = 0; j < topo.represented_actions[actidx].sz(); ++j) {
      topo.action(act0, topo.represented_actions[actidx][j]);
      if (!coexist_ck(act0, act1)) {
        delSet << candidates[i];
        break;
      }
    }
  }
}

static
  void
small_cycle_conflict (Cx::Table<uint>& conflict_set,
                      const Cx::PFmla& scc,
                      const vector<uint>& actions,
                      const Xn::Net& topo,
                      const Cx::PFmla& invariant,
                      const SynthesisCtx& synctx)
{
  conflict_set.clear();

  Cx::PFmla edg( false );
  Cx::Table<uint> scc_actidcs;
  Cx::Table<Cx::PFmla> xn_pfmlas(1, Cx::PFmla(false));
  for (uint i = 0; i < actions.size(); ++i) {
    uint actidx = actions[i];
    const Cx::PFmla& act_pfmla = topo.action_pfmla(actidx);
    if (scc.overlap_ck(act_pfmla)) {
      if (scc.overlap_ck(act_pfmla.img())) {
        scc_actidcs.push(actidx);
        edg |= act_pfmla;
        xn_pfmlas.push(edg);
      }
    }
  }

  edg = false;

  // Go in reverse so actions chosen at earlier levels are more likely
  // to be used in conflict set.
  for (uint i = scc_actidcs.sz(); i > 0;) {
    i -= 1;
    Cx::PFmla next_scc(false);
    if (synctx.done_ck()) {
      while (i > 0) {
        conflict_set.push(scc_actidcs[i]);
        --i;
      }
      break;
    }

    if ((edg | xn_pfmlas[i]).cycle_ck(&next_scc, scc)
        && invariant.overlap_ck(next_scc))
    {
    }
    else
    {
      uint actidx = scc_actidcs[i];
      const Cx::PFmla& act_pfmla = topo.action_pfmla(actidx);
      conflict_set.push(actidx);
      edg |= act_pfmla;
    }
  }
  //Claim( edg.cycle_ck(scc) );
}

  uint
count_actions_in_cycle (const Cx::PFmla& scc, Cx::PFmla edg,
                        const vector<uint>& actions, const Xn::Net& topo)
{
  uint n = 1;
  Cx::Table<uint> scc_actidcs;
  for (uint i = 0; i < actions.size(); ++i) {
    const Cx::PFmla& act_pfmla = topo.action_pfmla(actions[i]);
    if (scc.overlap_ck(act_pfmla)) {
      if (scc.overlap_ck(act_pfmla.img())) {
        edg |= act_pfmla;
        scc_actidcs.push(actions[i]);
        ++ n;
      }
    }
  }
  uint nneed = 1;
  uint nmin = 1;
  Cx::PFmla min_edg = edg;
  for (uint i = 0; i < scc_actidcs.size(); ++i) {
    const Cx::PFmla& act_pfmla = topo.action_pfmla(scc_actidcs[i]);
    if (!(edg - act_pfmla).cycle_ck(scc)) {
      ++ nneed;
      ++ nmin;
    }
    else {
      if ((min_edg - act_pfmla).cycle_ck(min_edg)) {
        min_edg -= act_pfmla;
      }
      else {
        ++ nmin;
      }
    }
  }
  DBog1("needed:%u", nneed);
  DBog1("nmin:%u", nmin);
  return n;
}

  uint
PartialSynthesis::add_small_conflict_set(const Cx::Table<uint>& delpicks)
{
  if (this->ctx->done_ck())
    return delpicks.sz();

  bool doit = false;
  for (uint i = 0; i < this->sz(); ++i) {
    if (!(*this)[i].no_conflict) {
      doit = true;
    }
  }
  if (!doit)  return 0;
  ConflictFamily& conflicts = this->ctx->conflicts;
  if (conflicts.conflict_ck(Cx::FlatSet<uint>(delpicks))) {
    if (!conflicts.exact_conflict_ck(Cx::FlatSet<uint>(delpicks))) {
      *this->log << "Conflict subsumed by existing one." << this->log->endl();
      return delpicks.sz();
    }
  }
  conflicts.add_conflict(delpicks);
  if (directly_add_conflicts) {
    return delpicks.sz();
  }
  Set<uint> delpick_set( delpicks );
  for (uint i = 0; i < delpicks.sz(); ++i) {
    PartialSynthesis inst( this->ctx->base_inst );
    for (uint j = 0; j < inst.sz(); ++j) {
      inst[j].log = &Cx::OFile::null();
      inst[j].directly_add_conflicts = true;
      if (inst[j].no_conflict) {
        inst[j].no_partial = true;
      }
    }
    delpick_set -= delpicks[i];
    if (inst.revise_actions(delpick_set, Set<uint>(delpicks[i]))) {
      delpick_set << delpicks[i];
    }
    else {
      if (this->ctx->done_ck()) {
        delpick_set << delpicks[i];
        break;
      }
      conflicts.add_conflict(delpick_set);
    }
  }
  *this->log << "Conflict size:" << delpick_set.sz() << this->log->endl();
  return delpick_set.sz();
}

#if 0
/** Detect cycles added by new action when we have the transitive
 * closure of all actions so far. This is a fast check, but computing
 * the transitive closure takes a long time since it a set of transitions
 * rather than just states.
 */
static
  bool
reach_cycle_ck (const Cx::PFmla& reach, const Cx::PFmla& act_pf, const Cx::PFmla& state_pf)
{
  const Cx::PFmla& trim_reach = act_pf.img() & act_pf.pre().as_img() & reach;

  Cx::PFmla next( state_pf & trim_reach.img() );
  Cx::PFmla pf( false );
  while (!pf.equiv_ck(next))
  {
    pf = next;
    next &= trim_reach.img(act_pf.img(next));
  }
  return pf.sat_ck();
}
#endif

/** Infer and prune actions from candidate list.**/
  bool
PartialSynthesis::check_forward(Set<uint>& adds, Set<uint>& dels, Set<uint>& rejs)
{
  const Xn::Sys& sys = *this->ctx->systems[this->sys_idx];
  const Xn::Net& topo = sys.topology;

  if (this->mcv_deadlocks.size() <= 1) {
    // This should have been caught if no deadlocks remain.
    Claim(!this->deadlockPF.sat_ck());
    this->candidates.clear();
    if (this->ctx->conflicts.conflict_ck(FlatSet<uint>(this->actions))) {
      *this->log << "CONFLICT" << this->log->endl();
      return false;
    }
    return true;
  }
  adds |= this->mcv_deadlocks[1].candidates;

  const Cx::PFmla& lo_xn_pre = this->lo_xn.pre();
  for (uint i = 0; i < this->candidates.size(); ++i) {
    uint actidx = this->candidates[i];
    if (dels.elem_ck(actidx))  continue;

    const Cx::PFmla& act_xn = topo.action_pfmla(actidx);

    if (act_xn.subseteq_ck(lo_xn_pre))
    {
      if (false) {
        Xn::ActSymm act;
        *this->log
          << "DEL (lvl " << this->bt_level
          << ") (sz " << this->actions.size()
          << ") (rem " << this->candidates.size()
          << ")  ";
        topo.action(act, actidx);
        OPut(*this->log, act) << this->log->endl();
      }
      dels << actidx;
      continue;
    }
  }

  FlatSet<uint> action_set( Set<uint>(this->actions) | adds );
  FlatSet<uint> candidate_set( this->candidates );

  Set<uint> membs;
  if (!this->ctx->conflicts.conflict_membs(&membs, action_set, candidate_set)) {
    *this->log << "CONFLICT" << this->log->endl();
    return false;
  }
  dels |= membs;
  rejs |= membs;

  return true;
}

/** Add actions to protocol and delete actions from candidate list.**/
  bool
PartialSynthesis::revise_actions_alone(Set<uint>& adds, Set<uint>& dels, Set<uint>& rejs)
{
  const Xn::Sys& sys = *this->ctx->systems[this->sys_idx];
  const Xn::Net& topo = sys.topology;
  Set<uint>::const_iterator it;
  const bool use_csp = false;
  if (this->ctx->done_ck())
    return false;

  if (this->no_partial) {
    for (it = adds.begin(); it != adds.end(); ++it) {
      Remove1(this->actions, *it);
      Remove1(this->candidates, *it);
      this->actions.push_back(*it);
    }
    for (it = dels.begin(); it != dels.end(); ++it) {
      Remove1(this->candidates, *it);
    }
    adds.clear();
    dels.clear();
    return true;
  }

  Cx::PFmla add_act_pfmla( false );
  Cx::PFmla add_pure_shadow_pfmla( true );
  for (it = adds.begin(); it != adds.end(); ++it) {
    uint actId = *it;
    if (use_csp) {
      this->csp_pfmla &=
        this->ctx->csp_pfmla_ctx.vbl(topo.action_pre_index(actId))
        == topo.action_img_index(actId);
    }

    if (!Remove1(this->candidates, actId)) {
      // This action wasn't a candidate,
      // but that may just mean it introduces nondeterminism
      // or doesn't resolve any existing deadlocks.
    }
    Remove1(this->actions, actId); // No duplicates
    this->actions.push_back(actId);
    Set<uint> tmp_dels;
    QuickTrim(tmp_dels, this->candidates, topo, actId);
    if (tmp_dels.overlap_ck(adds)) {
      *this->log << "QuickTrim() rejects an action!!!" << this->log->endl();
      Xn::ActSymm act;
      topo.action(act, actId);
      *this->log
        << " (sys " << this->sys_idx
        << ") (lvl " << this->bt_level
        << ") (sz " << this->actions.size()
        << ") (rem " << this->candidates.size()
        << ")  ";
      OPut(*this->log, act) << this->log->endl();
      FlatSet<uint> tmp_adds_dels( adds & tmp_dels );
      for (uint i = 0; i < tmp_adds_dels.sz(); ++i) {
        *this->log << "Reject:" << this->log->endl();
        topo.action(act, tmp_adds_dels[i]);
        OPut(*this->log, act) << this->log->endl();
      }
    }
    dels |= tmp_dels;
    add_act_pfmla |= topo.action_pfmla(actId);
    add_pure_shadow_pfmla &= topo.pure_shadow_pfmla(actId);
  }

  if (adds.overlap_ck(dels)) {
    if ((adds & dels) <= this->mcv_deadlocks[1].candidates)
    {
      *this->log << "Conflicting add from MCV." << this->log->endl();
    }
    else
    {
      *this->log << "Tried to add conflicting actions... this is not good!!!" << this->log->endl();
    }
    return false;
  }

  this->lo_puppet_xn |= add_act_pfmla;
  this->lo_xn = this->lo_puppet_xn;
  this->lo_pure_shadow_pfmla &= add_pure_shadow_pfmla;
  if (topo.pure_shadow_vbl_ck()) {
    this->lo_xn &= this->lo_pure_shadow_pfmla & this->lo_pure_shadow_pfmla.as_img();
    this->lo_xn |= topo.proj_puppet(topo.identity_xn) &
      (~this->lo_pure_shadow_pfmla & this->lo_pure_shadow_pfmla.as_img());
  }

  Cx::PFmla del_act_pfmla( false );
  for (it = dels.begin(); it != dels.end(); ++it) {
    uint actidx = *it;
    if (use_csp) {
      this->csp_pfmla &=
        this->ctx->csp_pfmla_ctx.vbl(topo.action_pre_index(actidx))
        != topo.action_img_index(actidx);
    }
    Remove1(this->candidates, actidx);
    del_act_pfmla |= topo.action_pfmla(actidx);
    if (false) {
      Xn::ActSymm act;
      DBogOF << "DEL ";
      topo.action(act, actidx);
      OPut(DBogOF, act) << DBogOF.endl();
    }
  }
  del_act_pfmla -= this->lo_xn;
  for (uint i = 0; i < this->candidates.size(); ++i) {
    del_act_pfmla -= topo.action_pfmla(this->candidates[i]);
  }
  this->hi_puppet_xn -= del_act_pfmla;
  this->hi_xn = this->hi_puppet_xn;
  this->hi_pure_shadow_pfmla = this->lo_pure_shadow_pfmla;
  if (topo.pure_shadow_vbl_ck()) {
    this->hi_xn &= this->hi_pure_shadow_pfmla & this->hi_pure_shadow_pfmla.as_img();
    this->hi_xn |= topo.proj_puppet(topo.identity_xn) &
      (~this->hi_pure_shadow_pfmla & this->hi_pure_shadow_pfmla.as_img());
  }


  Xn::ActSymm act;
  if (this->sys_idx == 0)
  for (it = adds.begin(); it != adds.end(); ++it) {
    uint actId = *it;
    *this->log
      << " (lvl " << this->bt_level
      << ") (sz " << this->actions.size()
      << ") (rem " << this->candidates.size()
      << ")  ";
    topo.action(act, actId);
    OPut(*this->log, act) << this->log->endl();
  }

  Cx::PFmla scc(false);
  this->lo_xn.cycle_ck(&scc);
  if (!scc.subseteq_ck(sys.invariant)) {
    //oput_one_cycle(*this->log, this->lo_xn, scc, topo);
    //uint n = count_actions_in_cycle(scc, act_pf, this->actions, topo);
    //DBog1("scc actions: %u", n);
    *this->log << "CYCLE" << this->log->endl();
    if (true || !this->no_conflict) {
      Cx::Table<uint> conflict_set;
#if 0
      small_cycle_conflict (conflict_set, scc, this->actions, topo, sys.invariant,
                            *this->ctx);
#else
      conflict_set = this->actions;
      find_one_cycle (conflict_set, this->lo_xn, scc, topo);
#endif
      this->ctx->conflicts.add_conflict(conflict_set);
      *this->log << "cycle conflict size:" << conflict_set.sz() << this->log->endl();
    }
    if (!!this->ctx->opt.livelock_ofilepath && &sys == this->ctx->systems.top()) {
      bool big_livelock = true;
      for (uint i = 0; i < this->ctx->systems.sz()-1; ++i) {
        if (!stabilization_ck(Cx::OFile::null(), *this->ctx->systems[i], actions)) {
          *this->log << "still issues in system " << i << this->log->endl();
          big_livelock = false;
          break;
        }
      }
      if (big_livelock) {
        Cx::OFileB ofb;
        ofb.open(this->ctx->opt.livelock_ofilepath + "." + this->ctx->opt.sys_pcidx + "." + this->ctx->opt.n_livelock_ofiles++);
        oput_protocon_file(ofb, sys, false, this->actions);
      }
    }
    return false;
  }

  if (!shadow_ck(&this->hi_invariant, sys, this->lo_xn, this->hi_xn, &scc))
  {
    *this->log << "SHADOW" << this->log->endl();
    return false;
  }

  const Cx::PFmla old_deadlock_pfmla( this->deadlockPF );

  this->deadlockPF =
    ((~this->hi_invariant) | sys.shadow_pfmla.pre())
    - this->lo_xn.pre();

  if (!this->deadlockPF.subseteq_ck(this->hi_xn.pre())) {
    *this->log << "DEADLOCK" << this->log->endl();
    return false;
  }

  if (!weak_convergence_ck(this->hi_xn, this->hi_invariant)) {
    *this->log << "REACH" << this->log->endl();
#if 0
    Cx::PFmla pf( ~this->hi_xn.pre_reach(this->hi_invariant) );
    pf = pf.pick_pre();
    pf = this->hi_xn.img_reach(pf);
    topo.oput_all_pf(*this->log, pf);
    topo.oput_all_xn(*this->log, this->hi_xn);
#endif
    return false;
  }

  bool revise = true;
  if (sys.shadow_puppet_synthesis_ck() &&
      !(this->deadlockPF.subseteq_ck(old_deadlock_pfmla)))
  {
    RankDeadlocksMCV(this->mcv_deadlocks,
                     sys.topology,
                     this->candidates,
                     this->deadlockPF);
    revise = false;
  }

  if (revise) {
    ReviseDeadlocksMCV(this->mcv_deadlocks, topo, adds, dels);
  }

  adds.clear();
  dels.clear();
  return this->check_forward(adds, dels, rejs);
}

  bool
PartialSynthesis::revise_actions(const Set<uint>& adds, const Set<uint>& dels)
{
  Cx::Table< Set<uint> > all_adds( this->sz(), adds );
  Cx::Table< Set<uint> > all_dels( this->sz(), dels );

  // If both sets are empty, we should force a check anyway.
  bool force_revise = (adds.empty() && dels.empty());

  bool keep_going = true;
  while (keep_going) {
    keep_going = false;

    // Generally, systems with higher indices should be more computationally intensive.
    // Therefore if {keep_going} becomes true, we should revise the systems with lower
    // indices before revising the systems with higher indices.
    for (uint i = 0; !keep_going && i < this->sz(); ++i) {
      if (!force_revise && all_adds[i].empty() && all_dels[i].empty())
        continue;

      PartialSynthesis& inst = (*this)[i];
      Set<uint> rejs;
      if (!inst.revise_actions_alone(all_adds[i], all_dels[i], rejs))
        return false;
      Claim( rejs.subseteq_ck(all_dels[i]) );

      if (!all_adds[i].empty() || !all_dels[i].empty() || !rejs.empty()) {
        keep_going = true;
        for (uint j = 0; j < this->sz(); ++j) {
          all_adds[j] |= all_adds[i];
          all_dels[j] |= rejs;
        }
        // Stop forcing if we'll check everything anyway.
        if (!all_adds[i].empty() || !rejs.empty()) {
          force_revise = false;
        }
      }
    }
  }
  for (uint i = 0; i < this->sz()-1; ++i) {
    Claim2( (*this)[i].actions.size() ,==, (*this)[i+1].actions.size() );
  }
  return true;
}

  bool
PartialSynthesis::pick_action(uint act_idx)
{
  for (uint i = 0; i < this->sz(); ++i) {
    (*this)[i].picks.push(act_idx);
  }
  if (!this->revise_actions(Set<uint>(act_idx), Set<uint>())) {
    this->add_small_conflict_set(this->picks);
    return false;
  }

  return true;
}

/**
 * Initialize synthesis structures.
 */
  bool
SynthesisCtx::init(const Xn::Sys& sys,
                   const AddConvergenceOpt& opt)
{
  const Xn::Net& topo = sys.topology;
  SynthesisCtx& synctx = *this;
  synctx.opt = opt;
  synctx.log = opt.log;
  urandom.use_system_urandom(opt.system_urandom);

  for (uint pcidx = 0; pcidx < topo.pc_symms.sz(); ++pcidx)
  {
    const Xn::PcSymm& pc_symm = topo.pc_symms[pcidx];
    for (uint i = 0; i < pc_symm.pre_domsz; ++i)
    {
      Cx::String name = pc_symm.spec->name + "@pre_enum[" + i + "]";
      uint vbl_idx =
        synctx.csp_pfmla_ctx.add_vbl(name, pc_symm.img_domsz);
      Claim2( vbl_idx ,==, pc_symm.pre_dom_offset + i );
    }

    Xn::ActSymm act;
    // Enforce self-disabling actions.
    if (false)
    for (uint actidx = 0; actidx < pc_symm.n_possible_acts; ++actidx) {
      pc_symm.action(act, actidx);
      synctx.csp_base_pfmla &=
        (synctx.csp_pfmla_ctx.vbl(act.pre_idx) != act.img_idx)
        |
        (synctx.csp_pfmla_ctx.vbl(act.pre_idx_of_img) == act.img_idx);
    }
  }
  return synctx.add(sys);
}

  bool
SynthesisCtx::add(const Xn::Sys& sys)
{
  const Xn::Net& topo = sys.topology;
  SynthesisCtx& synctx = *this;
  if (synctx.systems.sz() > 0) {
    synctx.base_inst.instances.push( PartialSynthesis(&synctx, synctx.systems.sz()) );
  }
  synctx.systems.push(&sys);

  PartialSynthesis& inst = synctx.base_inst[synctx.base_inst.sz()-1];
  inst.log = synctx.opt.log;

  inst.csp_pfmla = synctx.csp_base_pfmla;

  bool good =
    candidate_actions(inst.candidates, sys);
  if (!good) {
    return false;
  }
  if (good && inst.candidates.size() == 0) {
    return true;
  }

  for (uint i = 0; i < inst.candidates.size(); ++i) {
    inst.hi_puppet_xn |= topo.action_pfmla(inst.candidates[i]);
  }
  inst.hi_xn = inst.hi_puppet_xn;

  inst.deadlockPF = ~sys.invariant;
  if (sys.shadow_puppet_synthesis_ck()) {
    inst.deadlockPF |= sys.shadow_pfmla.pre();
  }

  RankDeadlocksMCV(inst.mcv_deadlocks,
                   sys.topology,
                   inst.candidates,
                   inst.deadlockPF);

  if (inst.mcv_deadlocks.size() < 2) {
    DBog0("Cannot resolve all deadlocks with known actions!");
    return false;
  }

  Set<uint> act_set(sys.actions);
  act_set |= Set<uint>(synctx.base_inst.actions);

  if (!synctx.base_inst.revise_actions(act_set, this->conflicts.impossible_set)) {
    DBog0("No actions apply!");
    return false;
  }

  if (!inst.deadlockPF.sat_ck() &&
      inst.actions.size() == sys.actions.size() &&
      inst.candidates.size() == 0)
  {
    DBog1("The given protocol is self-stabilizing for system %u.", inst.sys_idx);
  }
  return good;
}

