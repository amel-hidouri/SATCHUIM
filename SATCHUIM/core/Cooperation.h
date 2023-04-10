/*****************************************************************************************[Cooperation.h]
Copyright (c) 2008-20011, Youssef Hamadi, Saïd Jabbour and Lakhdar Saïs
 
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
*******************************************************************************************/

#include "core/SolverTypes.h"
#include "core/Solver.h"


namespace Minisat {

//=================================================================================================
// Options:

#define MAX_EXTRA_CLAUSES     2000
#define MAX_EXTRA_UNITS       2000

#define MAX_IMPORT_CLAUSES    4000
#define LIMIT_CONFLICTS_EVAL  6000

#define INITIAL_DET_FREQUENCE 700

#define AIMDX  0.25
#define AIMDY  8



/*=================================================================================================
 Cooperation Class : ->  [class]
Description:
	This class manage clause sharing component between threads,i.e.,
	It controls read and write operations in extraUnits and extraClauses arrays
    iterators headExtra... and tailExtra... are used to to this end
=================================================================================================*/
  
  class Cooperation{
    
  public:
    
    bool		start, end;				// start and end multi-threads search
    int			nbThreads;				// numbe of  threads
    int			limitExportClauses;			// initial limit size of shared clauses
    double**	        pairwiseLimitExportClauses;		// pairwised limit limit size clause sharing
    int                 div_begining;
    Solver*		solvers;				// set of running CDCL algorithms	
    lbool*		answers;				// answer of threads
    
    Lit***		extraUnits;				// where are stored the shared unit clauses 
    int**		headExtraUnits;
    int**		tailExtraUnits;					
    
    Lit****		extraClauses;				// where are stored the set of shared clauses with size > 1
    int**		headExtraClauses;
    int**		tailExtraClauses;				
    
    
    int*		nbImportedExtraUnits;                   // counters fot the total imported Unit clauses
    int*		nbImportedExtraClauses;			// counters fot the total imported Extra clauses
    
    char		ctrl;					// activate control clause sharing size mode
    char                enum_clos;
    int**		pairwiseImportedExtraClauses;		// imported clause of for and from each thread
    
    int                 min_supp;
    vec<vec<Lit> >      VecGuiding;
    vec<vec<Lit> >      list_transactions;       // List of transactions
    vec<vec<int> >      wItemTrans;
    vec<int>            wTrans;
    vec<vec<int> >      appearTrans;
    vec<int> occ;
    vec<vec<Lit> >      correl;
    vec<int>            wocc;
    //=================================================================================================
    
    void exportExtraUnit		(Solver* s, Lit unit);
    void importExtraUnits		(Solver* s);
    void importExtraUnits		(Solver* s, vec<Lit>& lits);
    
    void exportExtraClause		(Solver* s, vec<Lit>& learnt);
    void exportExtraClause		(Solver* s, Clause& c);
    void importExtraClauses		(Solver* s);
    
    void addExtraClause			(Solver* s, int t, Lit* lt);
    void addExtraClause1		(Solver* s, int t, Lit* lt);
    void addExtraClause2		(Solver* s, int t, Lit* lt);

    void uncheckedEnqueue               (Solver* s, int t, Lit l);
    void storeExtraUnits                (Solver* s, int t, Lit l, vec<Lit>& lits);

    bool inferieur                      (vec<Lit>& vec1, vec<Lit>& vec2);
    void permute                        (vec<Lit>& vec1, vec<Lit>& vec2);
    
    void printStats			(int& id);
    void printExMatrix			();
    void Parallel_Info			();
    void buildGuidingPaths              ();
    bool addTableClause_                (vec<Lit>& ps);
    bool addWeightedItems_              (vec<int>& ps);
    
    void LaunchSolvers                  ();

    
    //=================================================================================================
    inline int nThreads      ()			{return nbThreads;		}
    inline int limitszClauses()			{return limitExportClauses;	}
    inline lbool answer      (int t)		{return answers[t];		}
    inline bool setAnswer    (int id, lbool lb) {answers[id] = lb; return true;	}
    
    //=================================================================================================
    // Constructor / Destructor 
    
    Cooperation(int n, int l){
      
      limitExportClauses = l;
      nbThreads	= n;
      end         = false;
      solvers	            = new Solver    [nbThreads];
      answers	            = new lbool     [nbThreads];		
      
      extraUnits            = new Lit**     [nbThreads];
      headExtraUnits        = new int*      [nbThreads];
      tailExtraUnits        = new int*      [nbThreads];

      extraClauses          = new Lit***    [nbThreads];
      headExtraClauses      = new int*      [nbThreads];
      tailExtraClauses      = new int*      [nbThreads];
      
      for(int t = 0; t < nbThreads; t++){
	extraUnits	  [t]    = new Lit* [nbThreads];
	headExtraUnits	  [t]    = new int  [nbThreads];
	tailExtraUnits	  [t]    = new int  [nbThreads];

	extraClauses	  [t]    = new Lit**[nbThreads];
	headExtraClauses  [t]    = new int  [nbThreads];
	tailExtraClauses  [t]    = new int  [nbThreads];
	
	for(int k = 0; k < nbThreads; k++){
	  extraUnits	  [t][k] = new Lit  [MAX_EXTRA_UNITS];
	  headExtraUnits  [t][k] = 0;
	  tailExtraUnits  [t][k] = 0;
	  
	  extraClauses	  [t][k] = new Lit* [MAX_EXTRA_CLAUSES];					
	  headExtraClauses[t][k] = 0;
	  tailExtraClauses[t][k] = 0;
	}
	}
      
      //=================================================================================================
      nbImportedExtraClauses      = new int	[nbThreads];
      nbImportedExtraUnits        = new int	[nbThreads];
      pairwiseImportedExtraClauses= new int*	[nbThreads];
      pairwiseLimitExportClauses  = new double*	[nbThreads];		
      
      for(int t = 0; t < nbThreads; t++){
	answers               [t] = l_Undef;
	nbImportedExtraClauses[t] = 0;
	nbImportedExtraUnits  [t] = 0;
	
	pairwiseImportedExtraClauses [t]= new int   [nbThreads];
	pairwiseLimitExportClauses   [t]= new double[nbThreads];
	
	for(int k = 0; k < nbThreads; k++)
	  pairwiseLimitExportClauses[t][k]= l;
	
      }
      }
    //=================================================================================================
    ~Cooperation(){}				
  };
}



		
		
