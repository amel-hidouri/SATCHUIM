/***************************************************************************************[Solver.cc]
 Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
 Copyright (c) 2007-2010, Niklas Sorensson
 
 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 **************************************************************************************************/

#include <math.h>
#include <omp.h>
#include "mtl/Sort.h"
#include "core/Solver.h"
#include "core/Cooperation.h"


using namespace Minisat;

//=================================================================================================
// Options:


static const char* _cat = "CORE";

static DoubleOption  opt_var_decay         (_cat, "var-decay",   "The variable activity decay factor",            0.95,     DoubleRange(0, false, 1, false));
static DoubleOption  opt_clause_decay      (_cat, "cla-decay",   "The clause activity decay factor",              0.999,    DoubleRange(0, false, 1, false));
static DoubleOption  opt_random_var_freq   (_cat, "rnd-freq",    "The frequency with which the decision heuristic tries to choose a random variable", 0, DoubleRange(0, true, 1, true));
static DoubleOption  opt_random_seed       (_cat, "rnd-seed",    "Used by the random variable selection",         91648253, DoubleRange(0, false, HUGE_VAL, false));
static IntOption     opt_ccmin_mode        (_cat, "ccmin-mode",  "Controls conflict clause minimization (0=none, 1=basic, 2=deep)", 2, IntRange(0, 2));
static IntOption     opt_phase_saving      (_cat, "phase-saving", "Controls the level of phase saving (0=none, 1=limited, 2=full)", 2, IntRange(0, 2));
static BoolOption    opt_rnd_init_act      (_cat, "rnd-init",    "Randomize the initial activity", false);
static BoolOption    opt_luby_restart      (_cat, "luby",        "Use the Luby restart sequence", true);
static IntOption     opt_restart_first     (_cat, "rfirst",      "The base restart interval", 100, IntRange(1, INT32_MAX));
static DoubleOption  opt_restart_inc       (_cat, "rinc",        "Restart interval increase factor", 2, DoubleRange(1, false, HUGE_VAL, false));
static DoubleOption  opt_garbage_frac      (_cat, "gc-frac",     "The fraction of wasted memory allowed before a garbage collection is triggered",  0.20, DoubleRange(0, false, HUGE_VAL, false));


//=================================================================================================
// Constructor/Destructor:


Solver::Solver() :

// Parameters (user settable):
//
verbosity        (0)
, var_decay        (opt_var_decay)
, clause_decay     (opt_clause_decay)
, random_var_freq  (opt_random_var_freq)
, random_seed      (opt_random_seed)
, luby_restart     (opt_luby_restart)
, ccmin_mode       (opt_ccmin_mode)
, phase_saving     (opt_phase_saving)
, rnd_pol          (false)
, rnd_init_act     (opt_rnd_init_act)
, garbage_frac     (opt_garbage_frac)
, restart_first    (opt_restart_first)
, restart_inc      (opt_restart_inc)

// Parameters (the rest):
//
, learntsize_factor((double)1/(double)3), learntsize_inc(1.1)

// Parameters (experimental):
//
, learntsize_adjust_start_confl (100)
, learntsize_adjust_inc         (1.5)

// Statistics: (formerly in 'SolverStats')
//
, solves(0), starts(0), decisions(0), rnd_decisions(0), propagations(0), conflicts(0), dec_vars(0), clauses_literals(0), learnts_literals(0), max_literals(0), tot_literals(0)

, ok                 (true)
, cla_inc            (1)
, var_inc            (1)
, watches            (WatcherDeleted(ca))
, qhead              (0)
, simpDB_assigns     (-1)
, simpDB_props       (0)
, order_heap         (VarOrderLt(activity))
, progress_estimate  (0)
, remove_satisfied   (true)

// Resource constraints:
//
, conflict_budget    (-1)
, propagation_budget (-1)
, asynch_interrupt   (false)
{}


Solver::~Solver()
{
}


//=================================================================================================
// Minor methods:


// Creates a new SAT variable in the solver. If 'decision' is cleared, variable will not be
// used as a decision variable (NOTE! This has effects on the meaning of a SATISFIABLE result).
//
Var Solver::newVar(bool sign, bool dvar)
{
    int v = nVars();
    watches  .init(mkLit(v, false));
    watches  .init(mkLit(v, true ));
    assigns  .push(l_Undef);
    vardata  .push(mkVarData(CRef_Undef, 0));
    //activity .push(0);
    activity .push(rnd_init_act ? drand(random_seed, threadId) * 0.00001 : 0);
    seen     .push(0);
    hu_wei   .push(0);
    useless  .push(0);
    isTrans  .push(0);
    seenItem .push(0);
    polarity .push(sign);
    decision .push();
    trail    .capacity(v+1);
    setDecisionVar(v, dvar);
    return v;
}

//=================================================================================================



bool Solver::addClause_(vec<Lit>& ps)
{
    assert(decisionLevel() == 0);
    if (!ok) return false;
   
    // Check if clause is satisfied and remove false/duplicate literals:
    sort(ps);

    Lit p; int i, j;
    for (i = j = 0, p = lit_Undef; i < ps.size(); i++)
        if (value(ps[i]) == l_True || ps[i] == ~p)
            return true;
        else if (value(ps[i]) != l_False && ps[i] != p)
            ps[j++] = p = ps[i];
    ps.shrink(i - j);


    if (ps.size() == 0){
      return ok = false;
    }
    else if (ps.size() == 1){
        uncheckedEnqueue(ps[0]);
        return ok = (propagate() == CRef_Undef);
    }else{
      
      CRef cr = ca.alloc(ps, false);
      clauses.push(cr);
      attachClause(cr);
    }
    
    nbClauses++;
    return true;
}


void Solver::attachClause(CRef cr) {
    const Clause& c = ca[cr];
    assert(c.size() > 1);
    watches[~c[0]].push(Watcher(cr, c[1])); //c[1]
    watches[~c[1]].push(Watcher(cr, c[0])); //c[0]
    if (c.learnt()) learnts_literals += c.size();
    else            clauses_literals += c.size(); }


void Solver::detachClause(CRef cr, bool strict) {
    const Clause& c = ca[cr];
    assert(c.size() > 1);
    
    if (strict){
        remove(watches[~c[0]], Watcher(cr, c[1]));
        remove(watches[~c[1]], Watcher(cr, c[0]));
    }else{
        // Lazy detaching: (NOTE! Must clean all watcher lists before garbage collecting this clause)
        watches.smudge(~c[0]);
        watches.smudge(~c[1]);
    }
	
    if (c.learnt()) learnts_literals -= c.size();
    else            clauses_literals -= c.size(); }


void Solver::removeClause(CRef cr) {
    Clause& c = ca[cr];
    detachClause(cr);
    // Don't leave pointers to free'd memory!
    if (locked(c)) vardata[var(c[0])].reason = CRef_Undef;
    c.mark(1); 
    ca.free(cr);
}


bool Solver::satisfied(const Clause& c) const {
    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) == l_True)
            return true;
    return false; }


// Revert to the state at given level (keeping all assignment at 'level' but not beyond).
// Backtrack to level level 
void Solver::cancelUntil(int level) {
    if (decisionLevel() > level){
        for (int c = trail.size()-1; c >= trail_lim[level]; c--){
            Var      x  = var(trail[c]);
            assigns [x] = l_Undef;
	    if (phase_saving > 1 || (phase_saving == 1) && c > trail_lim.last())
	      polarity[x] = sign(trail[c]);
	    
	    if(sign(trail[c]) && (x >= (nbItems + nbTrans)))
	      totalWeight += hu_wei[x];
	    
	    if(x < nbItems)
	      insertVarOrder(x); }
        qhead = trail_lim[level];
        trail.shrink(trail.size() - trail_lim[level]);
        trail_lim.shrink(trail_lim.size() - level);
    } }

// Backtrack to level 0 
void Solver::cancelAll() {
  for (int c = trail.size()-1; c >= 0; c--){
    Var      x  = var(trail[c]);
    assigns [x] = l_Undef;

    if(sign(trail[c]) && (x >= (nbItems + nbTrans)))
      totalWeight += hu_wei[x];
    
    if(x < nbItems)
      insertVarOrder(x); }
  qhead = 0;
  int ind = 0;
  trail.shrink(trail.size() - ind);
  trail_lim.shrink(trail_lim.size() - ind);
} 

//=================================================================================================
// Major methods:


Lit Solver::pickBranchLit()
{
    Var next = var_Undef;
    
    // Random decision:
    /*if ((drand(random_seed, threadId) < random_var_freq && !order_heap.empty()) || firstInterpretation){
      next = order_heap[irand(random_seed,order_heap.size(), threadId)];
      if (value(next) == l_Undef && decision[next])
      rnd_decisions++; }*/
	
    // Activity based decision:
    while (next == var_Undef || value(next) != l_Undef || !decision[next])
        if (order_heap.empty()){
            next = var_Undef;
            break;
        }else
            next = order_heap.removeMin();
	
    return next == var_Undef ? lit_Undef : mkLit(next, false);//rnd_pol ? drand(random_seed, threadId) < 0.5 : polarity[next]);
}


/*_________________________________________________________________________________________________
 |
 |  analyze : (confl : Clause*) (out_learnt : vec<Lit>&) (out_btlevel : int&)  ->  [void]
 |  
 |  Description:
 |    Analyze conflict and produce a reason clause.
 |  
 |    Pre-conditions:
 |      * 'out_learnt' is assumed to be cleared.
 |      * Current decision level must be greater than root level.
 |  
 |    Post-conditions:
 |      * 'out_learnt[0]' is the asserting literal at level 'out_btlevel'.
 |      * If out_learnt.size() > 1 then 'out_learnt[1]' has the greatest decision level of the 
 |        rest of literals. There may be others from the same level though.
 |  
 |________________________________________________________________________________________________@*/
void Solver::analyze(CRef confl, vec<Lit>& out_learnt, int& out_btlevel)
{
    int pathC = 0;
    Lit p     = lit_Undef;
	
    // Generate conflict clause:
    //
    out_learnt.push();      // (leave room for the asserting literal)
    int index   = trail.size() - 1;
	
    do{
        assert(confl != CRef_Undef); // (otherwise should be UIP)
        Clause& c = ca[confl];
		
        if (c.learnt())
            claBumpActivity(c);
		
        for (int j = (p == lit_Undef) ? 0 : 1; j < c.size(); j++){
            Lit q = c[j];
			
            if (!seen[var(q)] && level(var(q)) > 0){
                varBumpActivity(var(q));
                seen[var(q)] = 1;
                if (level(var(q)) >= decisionLevel())
                    pathC++;
                else
                    out_learnt.push(q);
            }
        }
        
        // Select next clause to look at:
        while (!seen[var(trail[index--])]);
        p     = trail[index+1];
        confl = reason(var(p));
        seen[var(p)] = 0;
        pathC--;
    }while (pathC > 0);
    out_learnt[0] = ~p;
	
    // Simplify conflict clause:
    //
    int i, j;
    out_learnt.copyTo(analyze_toclear);
    if (ccmin_mode == 2){
        uint32_t abstract_level = 0;
        for (i = 1; i < out_learnt.size(); i++)
            abstract_level |= abstractLevel(var(out_learnt[i])); // (maintain an abstraction of levels involved in conflict)
		
        for (i = j = 1; i < out_learnt.size(); i++)
            if (reason(var(out_learnt[i])) == CRef_Undef || !litRedundant(out_learnt[i], abstract_level))
                out_learnt[j++] = out_learnt[i];
        
    }else if (ccmin_mode == 1){
        for (i = j = 1; i < out_learnt.size(); i++){
            Var x = var(out_learnt[i]);
			
            if (reason(x) == CRef_Undef)
                out_learnt[j++] = out_learnt[i];
            else{
                Clause& c = ca[reason(var(out_learnt[i]))];
                for (int k = 1; k < c.size(); k++)
                    if (!seen[var(c[k])] && level(var(c[k])) > 0){
                        out_learnt[j++] = out_learnt[i];
                        break; }
            }
        }
    }else
        i = j = out_learnt.size();
	
    max_literals += out_learnt.size();
    out_learnt.shrink(i - j);
    tot_literals += out_learnt.size();
	
    // Find correct backtrack level:
    //
    if (out_learnt.size() == 1)
        out_btlevel = 0;
    else{
        int max_i = 1;
        // Find the first literal assigned at the next-highest level:
        for (int i = 2; i < out_learnt.size(); i++)
            if (level(var(out_learnt[i])) > level(var(out_learnt[max_i])))
                max_i = i;
        // Swap-in this literal at index 1:
        Lit p             = out_learnt[max_i];
        out_learnt[max_i] = out_learnt[1];
        out_learnt[1]     = p;
        out_btlevel       = level(var(p));
    }
	
    for (int j = 0; j < analyze_toclear.size(); j++) seen[var(analyze_toclear[j])] = 0;    // ('seen[]' is now cleared)
}


// Check if 'p' can be removed. 'abstract_levels' is used to abort early if the algorithm is
// visiting literals at levels that cannot be removed later.
bool Solver::litRedundant(Lit p, uint32_t abstract_levels)
{
    analyze_stack.clear(); analyze_stack.push(p);
    int top = analyze_toclear.size();
    while (analyze_stack.size() > 0){
        assert(reason(var(analyze_stack.last())) != CRef_Undef);
        Clause& c = ca[reason(var(analyze_stack.last()))]; analyze_stack.pop();
		
        for (int i = 1; i < c.size(); i++){
            Lit p  = c[i];
            if (!seen[var(p)] && level(var(p)) > 0){
                if (reason(var(p)) != CRef_Undef && (abstractLevel(var(p)) & abstract_levels) != 0){
                    seen[var(p)] = 1;
                    analyze_stack.push(p);
                    analyze_toclear.push(p);
                }else{
                    for (int j = top; j < analyze_toclear.size(); j++)
                        seen[var(analyze_toclear[j])] = 0;
                    analyze_toclear.shrink(analyze_toclear.size() - top);
                    return false;
                }
            }
        }
    }
	
    return true;
}


/*_________________________________________________________________________________________________
 |
 |  analyzeFinal : (p : Lit)  ->  [void]
 |  
 |  Description:
 |    Specialized analysis procedure to express the final conflict in terms of assumptions.
 |    Calculates the (possibly empty) set of assumptions that led to the assignment of 'p', and
 |    stores the result in 'out_conflict'.
 |________________________________________________________________________________________________@*/
void Solver::analyzeFinal(Lit p, vec<Lit>& out_conflict)
{
    out_conflict.clear();
    out_conflict.push(p);
	
    if (decisionLevel() == 0)
        return;
	
    seen[var(p)] = 1;
	
    for (int i = trail.size()-1; i >= trail_lim[0]; i--){
        Var x = var(trail[i]);
        if (seen[x]){
            if (reason(x) == CRef_Undef){
                assert(level(x) > 0);
                out_conflict.push(~trail[i]);
            }else{
                Clause& c = ca[reason(x)];
                for (int j = 1; j < c.size(); j++)
                    if (level(var(c[j])) > 0)
                        seen[var(c[j])] = 1;
            }
            seen[x] = 0;
        }
    }
	
    seen[var(p)] = 0;
}


void Solver::uncheckedEnqueue(Lit p, CRef from)
{
    assert(value(p) == l_Undef);
    assigns[var(p)] = lbool(!sign(p));
    vardata[var(p)] = mkVarData(from, decisionLevel());
    trail.push_(p);

    if(sign(p) && (var(p) >= (nbItems + nbTrans)))
      totalWeight -= hu_wei[var(p)];
	
}


/*_________________________________________________________________________________________________
 |
 |  propagate : [void]  ->  [Clause*]
 |  
 |  Description:
 |    Propagates all enqueued facts. If a conflict arises, the conflicting clause is returned,
 |    otherwise CRef_Undef.
 |  
 |    Post-conditions:
 |      * the propagation queue is empty, even if there was a conflict.
 |________________________________________________________________________________________________@*/
CRef Solver::propagate()
{
    CRef    confl     = CRef_Undef;
    int     num_props = 0;
    watches.cleanAll();
	
    while (qhead < trail.size()){
        Lit            p   = trail[qhead++];     // 'p' is enqueued fact to propagate.
        vec<Watcher>&  ws  = watches[p];
        Watcher        *i, *j, *end;
        num_props++;

        for (i = j = (Watcher*)ws, end = i + ws.size();  i != end;){
            // Try to avoid inspecting the clause:
            Lit blocker = i->blocker;
            if (value(blocker) == l_True){
                *j++ = *i++; continue; }
			
			
            // Make sure the false literal is data[1]:
            CRef     cr        = i->cref;
            Clause&  c         = ca[cr];
            Lit      false_lit = ~p;
            if (c[0] == false_lit)
                c[0] = c[1], c[1] = false_lit;
            assert(c[1] == false_lit);
            i++;
			
            // If 0th watch is true, then clause is already satisfied.
            Lit     first = c[0];
            Watcher w     = Watcher(cr, first);
            if (first != blocker){
                *j++ = w; continue; }
			
            // Look for new watch:
	    for (int k = 2; k < c.size(); k++)
	      if (value(c[k]) != l_False){
		c[1] = c[k]; c[k] = false_lit;
		watches[~c[1]].push(w);
		
		    goto NextClause; }
			
            // Did not find watch -- clause is unit under assignment:
            *j++ = w;
	    if(totalWeight < min_supp){
	      confl = cr;
	      qhead = trail.size();
	      // Copy the remaining watches:                                                                                                 
	      while (i < end)
		*j++ = *i++;
	    }
            else if (value(first) == l_False){
                confl = cr;
                qhead = trail.size();
                // Copy the remaining watches:
                while (i < end)
                    *j++ = *i++;
            }else
                uncheckedEnqueue(first, cr);
        NextClause:;
        }
        ws.shrink(i - j);
    }
    propagations += num_props;
    simpDB_props -= num_props;
	
    return confl;
}


/*_________________________________________________________________________________________________
 |
 |  reduceDB : ()  ->  [void]
 |  
 |  Description:
 |    Remove half of the learnt clauses, minus the clauses locked by the current assignment. Locked
 |    clauses are clauses that are reason to some assignment. Binary clauses are never removed.
 |________________________________________________________________________________________________@*/
struct reduceDB_lt { 
    ClauseAllocator& ca;
    reduceDB_lt(ClauseAllocator& ca_) : ca(ca_) {}
    bool operator () (CRef x, CRef y) { 
        return ca[x].size() > 2 && (ca[y].size() == 2 || ca[x].activity() < ca[y].activity()); } 
};
void Solver::reduceDB()
{
    int     i, j;
    //double  extra_lim = cla_inc / learnts.size();    // Remove any clause below this activity
	
    //sort(learnts, reduceDB_lt(ca));
    // Don't delete binary or locked clauses. From the rest, delete clauses from the first half
    // and clauses with activity smaller than 'extra_lim':
    for (i = j = 0; i < clauses.size(); i++){
        Clause& c = ca[clauses[i]];
        if (1)//c.size() > 2 && !locked(c) && (i < learnts.size() / 2 || c.activity() < extra_lim))
            removeClause(clauses[i]);
        else
            clauses[j++] = clauses[i];
    }
    clauses.shrink(i - j);
    //checkGarbage();
}


void Solver::removeSatisfied(vec<CRef>& cs)
{
    int i, j;
    for (i = j = 0; i < cs.size(); i++){
        Clause& c = ca[cs[i]];
        if (satisfied(c))
            removeClause(cs[i]);
        else
            cs[j++] = cs[i];
    }
    cs.shrink(i - j);
}


void Solver::rebuildOrderHeap()
{
    vec<Var> vs;
    for (Var v = 0; v < nVars(); v++)
      if (decision[v] && value(v) == l_Undef && (v < nbItems)) 
	vs.push(v);
    order_heap.build(vs);
}


/*_________________________________________________________________________________________________
 |
 |  simplify : [void]  ->  [bool]
 |  
 |  Description:
 |    Simplify the clause database according to the current top-level assigment. Currently, the only
 |    thing done here is the removal of satisfied clauses, but more things can be put here.
 |________________________________________________________________________________________________@*/
bool Solver::simplify()
{
    assert(decisionLevel() == 0);
	
    if (!ok || propagate() != CRef_Undef)
        return ok = false;
	
    if (nAssigns() == simpDB_assigns || (simpDB_props > 0))
        return true;
	
    // Remove satisfied clauses:
    removeSatisfied(learnts);
    if (remove_satisfied)        // Can be turned off.
        removeSatisfied(clauses);
    checkGarbage();
    rebuildOrderHeap();
	
    simpDB_assigns = nAssigns();
    simpDB_props   = clauses_literals + learnts_literals;   // (shouldn't depend on stats really, but it will do for now)
	
    return true;
}



void Solver::simplifier(){

  for(int i = 0; i < items.size(); i++)
    {
      Lit p = items[i];
      if(value(p) == l_Undef){
	newDecisionLevel();
	uncheckedEnqueue(p);
	CRef cr = propagate();
	
	if(cr != CRef_Undef || nbTrans-nbFalseTrans < 0){//Freq){
	  cancelUntil(0);
	  uncheckedEnqueue(~p);
	}else
	  cancelUntil(0);
      }
    }
}

/*_________________________________________________________________________________________________
 |
 |  search : (nof_conflicts : int) (params : const SearchParams&)  ->  [lbool]
 |  
 |  Description:
 |    Search for a model the specified number of conflicts. 
 |    NOTE! Use negative value for 'nof_conflicts' indicate infinity.
 |  
 |  Output:
 |    'l_True' if a partial assigment that is consistent with respect to the clauseset is found. If
 |    all variables are decision variables, this means that the clause set is satisfiable. 'l_False'
 |    if the clause set is unsatisfiable. 'l_Undef' if the bound on number of conflicts is reached.
 |________________________________________________________________________________________________@*/
//search-based DPLL procedure
lbool Solver::search(int nof_conflicts, Cooperation* coop)
{
  //assert(ok);
   // int       backtrack_level;
    int         conflictC = 0;
    vec<Lit>    learnt_clause;
    lbool       answer;
    starts++;
    ind += coop->nbThreads; 	
    
    for (;;){
      
    Prop:;	
        CRef confl = propagate();
        if (!ok || confl != CRef_Undef){
            // CONFLICT
            conflicts++; conflictC++;
            
	    if (!ok || decisionLevel() == 0) {
	      diviser_state = 0;	      
	      cancelAll();
	      goto div_section;
	    }

	    Lit q = trail[trail_lim[trail_lim.size()-1]];
	    int backtrack_level = decisionLevel()-1;
	    cancelUntil(decisionLevel()-1);
	    uncheckedEnqueue(~q);
	    
	 }else{

	div_section:;
	  if (diviser_state == 0)
	    if(ind < allItems.size()) {
	      ok = true;
	      reduceDB();

	      while((ind < allItems.size())  && !encodeGuidingPath(coop, ind+1))
		ind += coop->nbThreads;
	      
	      if(ind >=  allItems.size())
		return l_False;
	      diviser_state = 1;
	      ind += coop->nbThreads;
	      goto Prop;
	    }else
	      return l_False;
	  
	  if(totalWeight < coop->min_supp){
	    conflicts++;
	    if (decisionLevel() == 0) {
	      diviser_state = 0;
	      cancelAll();
	      goto div_section;
	    }
	    // cancelUntil(backtrack_level);
	    Lit l = trail[trail_lim[decisionLevel()-1]];
	    cancelUntil(decisionLevel()-1);
	    uncheckedEnqueue(~l);
	    goto Prop;
	  }
	  Lit next = lit_Undef;
	  while (decisionLevel() < assumptions.size()){
	    // Perform user provided assumption:
	    Lit p = assumptions[decisionLevel()];
	    if (value(p) == l_True){
	      // Dummy decision level:
	      newDecisionLevel();
                }else if (value(p) == l_False){
	      cancelUntil(0);
	      diviser_state = 0;
	      cancelAll();
	      goto div_section;
                }else{
	      next = p;
	      break;
	    }
	  }
	  
	  if (next == lit_Undef){
	    // New variable decision:
	    decisions++;
	    next = pickBranchLit();
	    
	    if (next == lit_Undef){

	      nbModels++;
	      if(verbosity >= 3){
		printf("->  ");
		for(int i = 0; i < VecItems.size(); i++)
		  if(value(mkLit(VecItems[i], false)) == l_True)
		    printf("%d ", VecItems[i] + 1);
		printf("\n");
	      }
	      	      
	      if (decisionLevel() == 0) {
		diviser_state = 0;
		cancelAll();
		goto div_section;
	      }
	      int last = trail_lim.size();
	      Lit q = trail[trail_lim[last-1]];
	      cancelUntil(decisionLevel()-1);
	      uncheckedEnqueue(~q);
	      goto Prop;
	    }
	  }
	  // Increase decision level and enqueue 'next'
	  newDecisionLevel();
	  uncheckedEnqueue(next);
        }
    }
}


double Solver::progressEstimate() const
{
    double  progress = 0;
    double  F = 1.0 / nVars();
	
    for (int i = 0; i <= decisionLevel(); i++){
        int beg = i == 0 ? 0 : trail_lim[i - 1];
        int end = i == decisionLevel() ? trail.size() : trail_lim[i];
        progress += pow(F, i) * (end - beg);
    }
	
    return progress / nVars();
}

/*
 Finite subsequences of the Luby-sequence:
 
 0: 1
 1: 1 1 2
 2: 1 1 2 1 1 2 4
 3: 1 1 2 1 1 2 4 1 1 2 1 1 2 4 8
 ...
 
 
 */

static double luby(double y, int x){
	
    // Find the finite subsequence that contains index 'x', and the
    // size of that subsequence:
    int size, seq;
    for (size = 1, seq = 0; size < x+1; seq++, size = 2*size+1);
	
    while (size-1 != x){
        size = (size-1)>>1;
        seq--;
        x = x % size;
    }
	
    return pow(y, seq);
}

// NOTE: assumptions passed in member-variable 'assumptions'.
lbool Solver::solve_(Cooperation* coop)
{
  model.clear();
  conflict.clear();
  nbModels = 0;
  diviser_state = 1;
  nbClauses = 0;
  max_clauses = 100;

  if (!ok) return l_False;
  
  ind = threadId;
  while((ind < allItems.size())  && !encodeGuidingPath(coop, ind+1)){
    ind += coop->nbThreads;
  }
  if(ind >=  allItems.size())
    return l_False;
  
  nbModels = 0;
  
  solves++;
  tailUnitLit = 0;
  
  max_learnts               = nClauses() * learntsize_factor;
  learntsize_adjust_confl   = learntsize_adjust_start_confl;
  learntsize_adjust_cnt     = (int)learntsize_adjust_confl;
  lbool   status            = l_Undef;
  
  
  // Search:
  int curr_restarts = 0;
  while (status == l_Undef){
    double rest_base = luby_restart ? luby(restart_inc, curr_restarts) : pow(restart_inc, curr_restarts);
    status = search(rest_base * restart_first, coop);
    if (!withinBudget()) break;
    curr_restarts++;
  }
  
  if ((threadId == 0) && (verbosity >= 1))
    printf(" =======================================================================================================================\n");
  
  
  if (status == l_True){
    // Extend & copy model:
    model.growTo(nVars());
    for (int i = 0; i < nVars(); i++) model[i] = value(i);
  }else if (status == l_False && conflict.size() == 0)
    ok = false;
  
  cancelUntil(0);
  return status;
}	
//=================================================================================================
//Encoding database
void Solver::EncodeDB(Cooperation* coop){

  nbFalseTrans = 0;
  nbItems = nVars();
  diviser_state = 1;
  min_supp = coop->min_supp;
  
  for(int i = 0; i < nVars(); i++){
    local_trans.push();
    occ.push(0);
  }
  
  for( int i= 0; i< nbTrans; i++){
    Var v = newVar();
    isTrans[v]=1;
  }
}


/*_________________________________________________________________________________________________                                                                                                         
 |                                                                                                                                                                                                         
 |  search : (nof_conflicts : int) (params : const SearchParams&)  ->  [lbool]                                                                                                                             
 |                                                                                                                                                                                                         
 |  Description:                                                                                                                                                                                            
 |    Search for a model the specified number of conflicts.                                                                                                                                                 
 |    NOTE! Use negative value for 'nof_conflicts' indicate infinity.                                                                                                                                       |                                                                                                                                                                                                          |  Output:                                                                                                                                                                                                 
 |    'l_True' if a partial assigment that is consistent with respect to the clauseset is found. If                                                                                                        
 |    all variables are decision variables, this means that the clause set is satisfiable. 'l_False'                                                                                                        
 |    if the clause set is unsatisfiable. 'l_Undef' if the bound on number of conflicts is reached.                                                                                                        
 |________________________________________________________________________________________________@*/
//Encoding phase
bool Solver::encodeGuidingPath(Cooperation* coop, int index){

  items.clear();
  Lit p = allItems[index-1];
  Lit pp = p;
  vec<Lit> currentDB;
  if(coop->wocc[var(p)] < coop->min_supp)
    return false;
  //propagate at level 0 the guding path literals
  int i = 0;
  for(i = 0; i < index-1; i++) {
    uncheckedEnqueue(~allItems[i]);
    seen[var(allItems[i])]    = 1;
  }
  uncheckedEnqueue(allItems[i]);
  
  int current_dabase_size = coop->appearTrans[var(p)].size();
  Lit qlit = lit_Undef;

  int init_vars = nbItems + nbTrans;
  totalWeight   = 0;
  int wcurTrans = 0;    

  vec<int> poids;
  vec<Lit> rlits;
  for(int i = 0; i < current_dabase_size; i++){
    int num = coop->appearTrans[var(p)][i];
    qlit = mkLit(num + nbItems, false);
    currentDB.push(mkLit(num + nbItems, false));

    wcurTrans = 0;
    for(int j = 0; j < coop->list_transactions[num].size(); j++){
      Lit r = coop->list_transactions[num][j]; 
      Var v = var(r);   
      if(value (r) != l_False){
	// add boolean function to ....
	Lit a = lit_Undef;
	if (init_vars < nVars()){
	  a = mkLit(init_vars++, false);
	}else{
	  a = mkLit(newVar(), false);
	  init_vars++;
	}	
	rlits.push(a);
	addClause(~a, qlit);
	addClause(~a, r);
	hu_wei[var(a)] = coop->wItemTrans[num][j];
	totalWeight   += coop->wItemTrans[num][j];
	wcurTrans     += coop->wItemTrans[num][j];
      }
      local_trans[v].push(qlit);
      if(!seen[v]){
	seen[v] = 1;
	items.push(coop->list_transactions[num][j]);
      }
    }
    poids.push(wcurTrans);
    for(int j = 0; j < coop->list_transactions[num].size(); j++){
      Lit r = coop->list_transactions[num][j]; 
      occ[var(r)]  += wcurTrans;
    }
    
  }

  for(int i = 0; i < items.size(); i++)
    seen[var(items[i])] = 0;
  for(int i = 0; i < index; i++)
    seen[var(allItems[i])] = 0;

  for(int i = 0; i < items.size(); i++){
    if(value(items[i]) == l_Undef && occ[var(items[i])] < coop->min_supp){
      uncheckedEnqueue(~items[i]);
      propagate();
    }
  }

  if( coop->min_supp <= totalWeight){
    // add support constraints of items in database of D under the scope of p
    for(int i = 0; i < coop->appearTrans[var(p)].size(); i++){
      int num = coop->appearTrans[var(p)][i];
      add_support_constraints(num+nbItems, coop->list_transactions[num], items); 
    }
    // add closure constraints of items in database of D under the scope of p
    if(coop->enum_clos == 1){
      for(int i = 0; i < items.size(); i++){
	Lit q = items[i];
	if(value(q) != l_True)
	  add_closure_constraints(q, currentDB, local_trans[var(q)]);
      }
      // add closure constraints of items in database of D under the scope of p
      for(int i = coop->div_begining; i < index-1; i++){ 
	Lit q = allItems[i];
	//if(coop->appearTrans[var(q)].size() >= coop->min_supp)
	if(coop->min_supp <= occ[var(allItems[i])])
	  add_closure_constraints(currentDB, local_trans[var(q)]);
      }
    } 
  }
  
  // reorder the heap with real variables appearing in the DB under the scope of current guiding path variable
  vec<Var> vs;
  for(int i = 0; i < items.size(); i++){
      Lit q = items[i];
      if(value(q) == l_Undef){
	activity[var(q)] = current_dabase_size - local_trans[var(q)].size();
	vs.push(var(q));
      }
  }
  order_heap.build(vs);
  
  for(int i = 0; i < allItems.size(); i++){
    Var v = var(allItems[i]);
    local_trans[v].clear();
    occ [v]  = 0;
  }
    
  if(clauses.size() > max_clauses){
     checkGarbage();
     max_clauses *= 1.1;//*= 1.1;
   }else
    max_clauses *= 0.9;
 
  
  return true;
}


/*********************************************************************************
//@ add_support_constraints : closure constraint

*********************************************************************************/
void Solver::add_closure_constraints(Lit item, vec<Lit>& trans, vec<Lit>& app){
    vec<Lit> liste;
    for(int i = 0; i < app.size(); i++) seen[var(app[i])] = 1;    
    for(int i = 0; i < trans.size(); i++)
      if(!seen[var(trans[i])])
        liste.push(trans[i]);
    liste.push(item);
    addClause(liste);

    for(int i = 0; i < app.size(); i++) seen[var(app[i])] = 0;
  }


/*********************************************************************************
//@ add_closure_constraints : closure constraint

*********************************************************************************/
void Solver::add_closure_constraints(vec<Lit>& trans, vec<Lit>& app){
  vec<Lit> liste;
  for(int i = 0; i < app.size(); i++) seen[var(app[i])] = 1; 
  for(int i = 0; i < trans.size(); i++)
    if(!seen[var(trans[i])])
      liste.push(trans[i]);
  addClause(liste);
  
  for(int i = 0; i < app.size(); i++) seen[var(app[i])] = 0;
}



/*********************************************************************************
//@ add_support_constraints : closure constraint

*********************************************************************************/
void Solver::add_support_constraints(int num, vec<Lit>& lastTrans, vec<Lit>& items){

  if(verbosity == 1){
  vec<Lit> block_lits;
  lastTrans.copyTo(block_lits);
  block_lits.push(mkLit(num, true));
  addClause(block_lits);
  }
  vec<Lit> liste;

  for(int i = 0; i < lastTrans.size(); i++) seen[var(lastTrans[i])] = 1;

  if(verbosity == 1){
    for(int i = 0; i < items.size(); i++) 
      if(!seen[var(items[i])] && value(items[i]) != l_False)
	liste.push(items[i]);
    liste.push(mkLit(num, false));
    addClause(liste);
  }

  for(int i = 0; i < items.size(); i++){
    vec<Lit> binClause;
    if(!seen[var(items[i])] && value(items[i]) != l_False)
      addClause(mkLit(num, true),~items[i]);
  }		   
  
  for(int i = 0; i < lastTrans.size(); i++)  seen[var(lastTrans[i])] = 0;
}
  
//=================================================================================================

void Solver::Equiv(Lit q, vec<Lit>& lits, Cooperation* coop){

    lits.push(q);
    //addClause(lits);
   // addFileClause(lits, res);

    for(int t = 0; t < coop->nbThreads;t++)
      coop->solvers[t].addClause(lits);    
    vec<Lit> l;
    l.push(~q);
    for(int i=0; i<lits.size()-1; i++){
      l.push(~lits[i]);
      //addClause(l);
      // addFileClause(l, res);
      for(int t = 0; t < coop->nbThreads;t++)
        coop->solvers[t].addClause(l);
      l.pop();
    }
}

//=================================================================================================
$
//chercher le maximum

/**
*   Renvoie l'indice du plus grand élément du tableau
*
*   int tab[] :: tableau dans lequel on effectue la recherche
*   int taille :: taille du tableau
*
*   return int l'indice du plus grand élément
**/
int Solver:: max(vec<int>& tab, int taille)
{
    // on considère que le plus grand élément est le premier
    int i=0, indice_max=0;
    
    while(i < taille)
    {
        if(tab[i] > tab[indice_max])
            indice_max = i;
        i++;
    }
    
    return indice_max;
}



//=================================================================================================

/**
*   Échange deux éléments d'un tableau
*
*   int tab[] :: tableau dans lequel on effectue l'échange
*   int x :: indice du premier élément
*   int y :: indice du second élément
*
*   return void
**/
void Solver::echanger(vec<int>& tab, int x, int y)
{
    int tmp;
    
    tmp = tab[x];
    tab[x] = tab[y];
    tab[y] = tmp;
}



//=================================================================================================

  void Solver::AfficheModel(vec<Lit>& lits){
    for(int i = 0; i < lits.size(); i++)  
    {
if(!isTrans[var(lits[i])]&& !sign(lits[i]))
	printf("%s%d ", sign(lits[i]) ? "-" : "", var(lits[i])+1);  
    }
	printf("\n");
  }


//=================================================================================================




// Writing CNF to DIMACS:
// 
// FIXME: this needs to be rewritten completely.

static Var mapVar(Var x, vec<Var>& map, Var& max)
{
    if (map.size() <= x || map[x] == -1){
        map.growTo(x+1, -1);
        map[x] = max++;
    }
    return map[x];
}


void Solver::printModels(){
  for(int i = 0; i < models.size(); i++)
    printClause(models[i]);
}


void Solver::printClause(CRef cr){
  Clause& c = ca[cr];
  for(int i = 0; i < c.size(); i++)
    printf("%s%d ", sign(c[i])?"-":"", var(c[i])+1);
  printf("0\n");
}


void Solver::printClause(vec<Lit>& lits){
  for(int i = 0; i < lits.size(); i++)
    printf("%s%d ", sign(lits[i])?"-":"", var(lits[i])+1);
  printf("0\n");
}


void Solver::toDimacs(FILE* f, Clause& c, vec<Var>& map, Var& max)
{
    if (satisfied(c)) return;
	
    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) != l_False)
			fprintf(f, "%s%d ", sign(c[i]) ? "-" : "", mapVar(var(c[i]), map, max)+1);
    fprintf(f, "0\n");
}


void Solver::toDimacs(const char *file, const vec<Lit>& assumps)
{
    FILE* f = fopen(file, "wr");
    if (f == NULL)
        fprintf(stderr, "could not open file %s\n", file), exit(1);
    toDimacs(f, assumps);
    fclose(f);
}


void Solver::toDimacs(FILE* f, const vec<Lit>& assumps)
{
    // Handle case when solver is in contradictory state:
    if (!ok){
        fprintf(f, "p cnf 1 2\n1 0\n-1 0\n");
        return; }
	
    vec<Var> map; Var max = 0;
	
    // Cannot use removeClauses here because it is not safe
    // to deallocate them at this point. Could be improved.
    int cnt = 0;
    for (int i = 0; i < clauses.size(); i++)
        if (!satisfied(ca[clauses[i]]))
            cnt++;
	
    for (int i = 0; i < clauses.size(); i++)
        if (!satisfied(ca[clauses[i]])){
            Clause& c = ca[clauses[i]];
            for (int j = 0; j < c.size(); j++)
                if (value(c[j]) != l_False)
                    mapVar(var(c[j]), map, max);
        }
	
    // Assumptions are added as unit clauses:
    cnt += assumptions.size();
	
    fprintf(f, "p cnf %d %d\n", max, cnt);
	
    for (int i = 0; i < assumptions.size(); i++){
        assert(value(assumptions[i]) != l_False);
        fprintf(f, "%s%d 0\n", sign(assumptions[i]) ? "-" : "", mapVar(var(assumptions[i]), map, max)+1);
    }
	
    for (int i = 0; i < clauses.size(); i++)
        toDimacs(f, ca[clauses[i]], map, max);
	
    if (verbosity > 0)
        printf("Wrote %d clauses with %d variables.\n", cnt, max);
}


//=================================================================================================
// Garbage Collection methods:

void Solver::relocAll(ClauseAllocator& to)
{
    // All watchers:
    //
    // for (int i = 0; i < watches.size(); i++)
    watches.cleanAll();
    for (int v = 0; v < nVars(); v++)
        for (int s = 0; s < 2; s++){
            Lit p = mkLit(v, s);
            // printf(" >>> RELOCING: %s%d\n", sign(p)?"-":"", var(p)+1);
            vec<Watcher>& ws = watches[p];
            for (int j = 0; j < ws.size(); j++)
                ca.reloc(ws[j].cref, to);
        }
	
    // All reasons:
    //
    for (int i = 0; i < trail.size(); i++){
        Var v = var(trail[i]);
		
        if (reason(v) != CRef_Undef && (ca[reason(v)].reloced() || locked(ca[reason(v)])))
            ca.reloc(vardata[v].reason, to);
    }
	
    // All learnt:
    //
    for (int i = 0; i < learnts.size(); i++)
        ca.reloc(learnts[i], to);

    for (int i = 0; i < models.size(); i++)
      ca.reloc(models[i], to);
    // All original:
    //
    for (int i = 0; i < clauses.size(); i++)
        ca.reloc(clauses[i], to);
}


void Solver::garbageCollect()
{
    // Initialize the next region to a size corresponding to the estimated utilization degree. This
    // is not precise but should avoid some unnecessary reallocations for the new region:
    ClauseAllocator to(ca.size() - ca.wasted()); 
	
    relocAll(to);
    if (verbosity >= 2)
        printf("|  Garbage collection:   %12d bytes => %12d bytes             |\n", 
               ca.size()*ClauseAllocator::Unit_Size, to.size()*ClauseAllocator::Unit_Size);
    to.moveTo(ca);
}


/*_________________________________________________________________________________________________                                                                                                                                                                                         
                                                                                                                                                                                                                                                                                            
 uncheckedEnqueueImportedUnits : (Cooperation* coop)                                                                                                                                                                                                                                        
 Description :                                                                                                                                                                                                                                                                              
 At level 0, units literals propaged are exported to others threads                                                                                                                                                                                                                         
*/

void Solver::exportClause(Cooperation* coop, vec<Lit>& learnt_clause) {

  if(coop->limitszClauses() < 1)
    return;

  if(decisionLevel() == 0){
    for(int i = tailUnitLit; i < trail.size(); i++)
      coop->exportExtraUnit(this, trail[i]) ;
    tailUnitLit = trail.size();
  }else
    coop->exportExtraClause(this, learnt_clause);
}


//=================================================================================================                                                                                                                                                                                         
// add Clauses received from others threads                                                                                                                                                                                                                                                 

CRef Solver::addExtraClause(vec<Lit>& lits){
  CRef cr = ca.alloc(lits, true);
  learnts.push(cr);
  attachClause(cr);
  claBumpActivity(ca[cr]);
  return cr;
}

//=================================================================================================                                                                                                                                                                                         
// at level 0, unit extra clauses stored are propagated                                                                                                                                                                                                                                     

void Solver::propagateExtraUnits(){
  for(int i = 0; i < extraUnits.size(); i++)
    if(value(extraUnits[i]) == l_Undef)
      uncheckedEnqueue(extraUnits[i]);
}









