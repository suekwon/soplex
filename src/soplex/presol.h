/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the class library                   */
/*       SoPlex --- the Sequential object-oriented simPlex.                  */
/*                                                                           */
/*    Copyright (C) 1996-2021 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SoPlex is distributed under the terms of the ZIB Academic Licence.       */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SoPlex; see the file COPYING. If not email to soplex@zib.de.  */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#ifdef SOPLEX_WITH_PAPILO

#include <memory>

#include "papilo/core/Presolve.hpp"
#include "papilo/core/ProblemBuilder.hpp"
#include "papilo/Config.hpp"

#include "soplex/spxsimplifier.h"

#include "soplex/spxdefines.h"
#include "soplex/spxsimplifier.h"
#include "soplex/array.h"
#include "soplex/exceptions.h"
#include "soplex/spxdefines.h"


namespace soplex{

   template<class R>
   class Presol : public SPxSimplifier<R> {
   private:

      const papilo::VerbosityLevel verbosityLevel = papilo::VerbosityLevel::kQuiet;

      VectorBase<R> m_prim;       ///< unsimplified primal solution VectorBase<R>.
      VectorBase<R> m_slack;      ///< unsimplified slack VectorBase<R>.
      VectorBase<R> m_dual;       ///< unsimplified dual solution VectorBase<R>.
      VectorBase<R> m_redCost;    ///< unsimplified reduced cost VectorBase<R>.
      DataArray<typename SPxSolverBase<R>::VarStatus> m_cBasisStat; ///< basis status of columns.
      DataArray<typename SPxSolverBase<R>::VarStatus> m_rBasisStat; ///< basis status of rows.


      papilo::PostsolveStorage<R>
          postsolveStorage;        ///< storede postsolve to recalculate the original solution
      bool m_noChanges = false;    ///< did PaPILO reduce the problem?

      bool m_postsolved;           ///< was the solution already postsolve?
      R m_epsilon;                 ///< epsilon zero.
      R m_feastol;                 ///< primal feasibility tolerance.
      R m_opttol;                  ///< dual feasibility tolerance.
      R modifyConsFac;             ///<
      DataArray<int> m_stat;       ///< preprocessing history.
      typename SPxLPBase<R>::SPxSense m_thesense;   ///< optimization sense.

      // TODO: the following parameters were ignored? Maybe I don't exactly know what they suppose to be
      bool m_keepbounds;           ///< keep some bounds (for boundflipping)
      typename SPxSimplifier<R>::Result m_result;     ///< result of the simplification.
//      R m_cutoffbound;             ///< the cutoff bound that is found by heuristics
//      R m_pseudoobj;               ///< the pseudo objective function value

   protected:

      ///
      R epsZero() const {
         return m_epsilon;
      }

      ///
      R feastol() const {
         return m_feastol;
      }

      ///
      R opttol() const {
         return m_opttol;
      }

   public:

      //------------------------------------
      ///@name Constructors / destructors
      ///@{
      /// default constructor.
      //TODO: how to set the DEFAULT constructor and not declare it multiple times
      explicit Presol(Timer::TYPE ttype = Timer::USER_TIME)
              : SPxSimplifier<R>("PaPILO", ttype), m_postsolved(false), m_epsilon(DEFAULT_EPS_ZERO),
                m_feastol(DEFAULT_BND_VIOL), m_opttol(DEFAULT_BND_VIOL), modifyConsFac(0.8), m_thesense(SPxLPBase<R>::MAXIMIZE),
                m_keepbounds(false), m_result(this->OKAY)
     { ; };

      /// copy constructor.
      Presol(const Presol &old)
              : SPxSimplifier<R>(old), m_prim(old.m_prim), m_slack(old.m_slack), m_dual(old.m_dual),
                m_redCost(old.m_redCost), m_cBasisStat(old.m_cBasisStat), m_rBasisStat(old.m_rBasisStat),
                postsolveStorage(old.postsolveStorage), m_postsolved(old.m_postsolved), m_epsilon(old.m_epsilon),
                m_feastol(old.m_feastol), m_opttol(old.m_opttol),modifyConsFac(old.modifyConsFac), m_thesense(old.m_thesense),
                m_keepbounds(old.m_keepbounds), m_result(old.m_result) {
         ;
      }

      /// assignment operator
      Presol &operator=(const Presol &rhs) {
         if( this != &rhs )
         {
            SPxSimplifier<R>::operator=(rhs);
            m_prim = rhs.m_prim;
            m_slack = rhs.m_slack;
            m_dual = rhs.m_dual;
            m_redCost = rhs.m_redCost;
            m_cBasisStat = rhs.m_cBasisStat;
            m_rBasisStat = rhs.m_rBasisStat;
            m_postsolved = rhs.m_postsolved;
            m_epsilon = rhs.m_epsilon;
            m_feastol = rhs.m_feastol;
            m_opttol = rhs.m_opttol;
            m_thesense = rhs.m_thesense;
            m_keepbounds = rhs.m_keepbounds;
            m_result = rhs.m_result;
            postsolveStorage = rhs.postsolveStorage;
            modifyConsFac = rhs.modifyConsFac;
         }
         return *this;
      }

      /// destructor.
      virtual ~Presol() {
         ;
      }

      SPxSimplifier<R> *clone() const {
         return new Presol(*this);
      }

      void
      setModifyConsFrac(R value){
        modifyConsFac = value;
      }

      virtual typename SPxSimplifier<R>::Result simplify(SPxLPBase<R> &lp, R eps, R delta, Real remainingTime) {
         return simplify(lp, eps, delta, delta, remainingTime, false, 0);
      }

      virtual typename SPxSimplifier<R>::Result simplify(SPxLPBase<R> &lp, R eps, R ftol, R otol, Real remainingTime,
                                                         bool keepbounds, uint32_t seed);

      virtual void unsimplify(const VectorBase<R> &, const VectorBase<R> &, const VectorBase<R> &,
                              const VectorBase<R> &,
                              const typename SPxSolverBase<R>::VarStatus[],
                              const typename SPxSolverBase<R>::VarStatus[],
                              bool isOptimal);

      /// returns result status of the simplification
      virtual typename SPxSimplifier<R>::Result result() const {
         return m_result;
      }

      /// specifies whether an optimal solution has already been unsimplified.
      virtual bool isUnsimplified() const {
         return m_postsolved;
      }

      /// returns a reference to the unsimplified primal solution.
      virtual const VectorBase<R> &unsimplifiedPrimal() {
         assert(m_postsolved);
         return m_prim;
      }

      /// returns a reference to the unsimplified dual solution.
      virtual const VectorBase<R> &unsimplifiedDual() {
         assert(m_postsolved);
         return m_dual;
      }

      /// returns a reference to the unsimplified slack values.
      virtual const VectorBase<R> &unsimplifiedSlacks() {
         assert(m_postsolved);
         return m_slack;
      }

      /// returns a reference to the unsimplified reduced costs.
      virtual const VectorBase<R> &unsimplifiedRedCost() {
         assert(m_postsolved);
         return m_redCost;
      }

      /// gets basis status for a single row.
      virtual typename SPxSolverBase<R>::VarStatus getBasisRowStatus(int i) const {
         assert(m_postsolved);
         return m_rBasisStat[i];
      }

      /// gets basis status for a single column.
      virtual typename SPxSolverBase<R>::VarStatus getBasisColStatus(int j) const {
         assert(m_postsolved);
         return m_cBasisStat[j];
      }

      /// get optimal basis.
      virtual void getBasis(typename SPxSolverBase<R>::VarStatus rows[],
                            typename SPxSolverBase<R>::VarStatus cols[], const int rowsSize = -1,
                            const int colsSize = -1) const {
         assert(m_postsolved);
         assert(rowsSize < 0 || rowsSize >= m_rBasisStat.size());
         assert(colsSize < 0 || colsSize >= m_cBasisStat.size());

         for (int i = 0; i < m_rBasisStat.size(); ++i)
            rows[i] = m_rBasisStat[i];

         for (int j = 0; j < m_cBasisStat.size(); ++j)
            cols[j] = m_cBasisStat[j];
      }

   private:

      void initLocalVariables(const SPxLPBase <R> &lp);

      void configurePapilo(papilo::Presolve<R> &presolve, R feasTolerance, R epsilon, uint32_t seed, Real remainingTime) const;

      void applyPresolveResultsToColumns(SPxLPBase <R> &lp, const papilo::Problem<R> &problem,
                                         const papilo::PresolveResult<R> &res) const;

      void applyPresolveResultsToRows(SPxLPBase <R> &lp, const papilo::Problem<R> &problem,
                                      const papilo::PresolveResult<R> &res) const;

      papilo::VarBasisStatus
      convertToPapiloStatus( typename SPxSolverBase<R>::VarStatus status) const;

      typename SPxSolverBase<R>::VarStatus
      convertToSoplexStatus( papilo::VarBasisStatus status) const ;
   };

   template <class R>
   void Presol<R>::unsimplify(const VectorBase<R> &x, const VectorBase<R> &y,
                              const VectorBase<R> &s, const VectorBase<R> &r,
                              const typename SPxSolverBase<R>::VarStatus rows[],
                              const typename SPxSolverBase<R>::VarStatus cols[],
                              bool isOptimal) {

     MSG_INFO1((*this->spxout), (*this->spxout)
                                    << " --- unsimplifying solution and basis"
                                    << std::endl;)
     int nColsReduced = x.dim();
     int nRowsReduced = y.dim();
     assert(nColsReduced <= m_prim.dim());
     assert(nRowsReduced <= m_dual.dim());
     assert(nColsReduced == r.dim());
     assert(nRowsReduced == s.dim());

     //if presolving made no changes then copy the reduced solution to the original
     if (m_noChanges) {
       for (int j = 0; j < nColsReduced; ++j) {
         m_prim[j] = x[j];
         m_redCost[j] = r[j];
         m_cBasisStat[j] = cols[j];
       }

       for (int i = 0; i < nRowsReduced; ++i) {
         m_dual[i] = y[i];
         m_slack[i] = s[i];
         m_rBasisStat[i] = rows[i];
       }
       m_postsolved = true;
       return;
     }
     assert(nColsReduced == (int)postsolveStorage.origcol_mapping.size());
     assert(nRowsReduced == (int)postsolveStorage.origrow_mapping.size());

     papilo::Solution<R> originalSolution{};
     papilo::Solution<R> reducedSolution{};
     reducedSolution.type = papilo::SolutionType::kPrimalDual;
     reducedSolution.basisAvailabe = true;

     reducedSolution.primal.clear();
     reducedSolution.reducedCosts.clear();
     reducedSolution.varBasisStatus.clear();
     reducedSolution.dual.clear();
     reducedSolution.rowBasisStatus.clear();

     reducedSolution.primal.resize(nColsReduced);
     reducedSolution.reducedCosts.resize(nColsReduced);
     reducedSolution.varBasisStatus.resize(nColsReduced);
     reducedSolution.dual.resize(nRowsReduced);
     reducedSolution.rowBasisStatus.resize(nRowsReduced);

     m_postsolved = true;

     // NOTE: for maximization problems, we have to switch signs of dual and
     // reduced cost values, since simplifier assumes minimization problem
     R switch_sign = m_thesense == SPxLPBase<R>::MAXIMIZE ? -1 : 1;

     // assign values of variables in reduced LP
     for (int j = 0; j < nColsReduced; ++j) {
       reducedSolution.primal[j] = isZero(x[j], this->epsZero()) ? 0.0 : x[j];
       reducedSolution.reducedCosts[j] =
           isZero(r[j], this->epsZero()) ? 0.0 : switch_sign * r[j];
       reducedSolution.varBasisStatus[j] = convertToPapiloStatus(cols[j]);
     }

     for (int i = 0; i < nRowsReduced; ++i) {
       reducedSolution.dual[i] = isZero(y[i], this->epsZero()) ? 0.0 : switch_sign * y[i];
       reducedSolution.rowBasisStatus[i] = convertToPapiloStatus(rows[i]);
     }

     /* since PaPILO verbosity is quiet it's irrelevant what's the messager*/
     papilo::Num<R> num {};
     num.setEpsilon(m_epsilon);
     num.setFeasTol(m_feastol);
     papilo::Message msg{};
     msg.setVerbosityLevel(verbosityLevel);
     papilo::Postsolve<R> postsolve{msg, num};
     postsolve.undo(reducedSolution, originalSolution, postsolveStorage);

     for (int j = 0; j < (int)postsolveStorage.nColsOriginal; ++j) {
       m_prim[j] = originalSolution.primal[j];
       m_redCost[j] = originalSolution.reducedCosts[j];
       m_cBasisStat[j] = convertToSoplexStatus(originalSolution.varBasisStatus[j]);
     }

     for (int i = 0; i < (int)postsolveStorage.nRowsOriginal; ++i) {
       m_dual[i] = originalSolution.dual[i];
       m_slack[i] = originalSolution.slack[i];
       m_rBasisStat[i] =convertToSoplexStatus(originalSolution.rowBasisStatus[i]);
     }

   }

   template <class R>
   papilo::VarBasisStatus
   Presol<R>::convertToPapiloStatus( const typename SPxSolverBase<R>::VarStatus status) const {
     switch (status) {
     case SPxSolverBase<R>::ON_UPPER:
       return papilo::VarBasisStatus::ON_UPPER;
     case SPxSolverBase<R>::ON_LOWER:
       return papilo::VarBasisStatus::ON_LOWER;
     case SPxSolverBase<R>::FIXED:
       return papilo::VarBasisStatus::FIXED;
     case SPxSolverBase<R>::BASIC:
       return papilo::VarBasisStatus::BASIC;
     case SPxSolverBase<R>::UNDEFINED:
       return papilo::VarBasisStatus::UNDEFINED;
     case SPxSolverBase<R>::ZERO:
       return papilo::VarBasisStatus::ZERO;
     }
     return papilo::VarBasisStatus::UNDEFINED;
   }

   template <class R>
   typename SPxSolverBase<R>::VarStatus
   Presol<R>::convertToSoplexStatus( papilo::VarBasisStatus status) const {
     switch (status) {
     case papilo::VarBasisStatus::ON_UPPER:
       return SPxSolverBase<R>::ON_UPPER;
     case papilo::VarBasisStatus::ON_LOWER:
       return SPxSolverBase<R>::ON_LOWER;
     case papilo::VarBasisStatus::ZERO:
       return SPxSolverBase<R>::ZERO;
     case papilo::VarBasisStatus::FIXED:
       return SPxSolverBase<R>::FIXED;
     case papilo::VarBasisStatus::UNDEFINED:
       return SPxSolverBase<R>::UNDEFINED;
     case papilo::VarBasisStatus::BASIC:
       return SPxSolverBase<R>::BASIC;
     }
     return SPxSolverBase<R>::UNDEFINED;
   }


   template<class R>
   papilo::Problem<R> buildProblem(SPxLPBase<R> &lp) {
      papilo::ProblemBuilder<R> builder;

      /* build problem from matrix */
      int nnz = lp.nNzos();
      int ncols = lp.nCols();
      int nrows = lp.nRows();
      builder.reserve(nnz, nrows, ncols);

      /* set up columns */
      builder.setNumCols(ncols);
      for (int i = 0; i < ncols; ++i)
      {
         R lowerbound = lp.lower(i);
         R upperbound = lp.upper(i);
         R objective = lp.obj(i);
         builder.setColLb(i, lowerbound);
         builder.setColUb(i, upperbound);
         builder.setColLbInf(i, lowerbound <= -R(infinity));
         builder.setColUbInf(i, upperbound >= R(infinity));

         builder.setColIntegral(i, false);
         builder.setObj(i, objective);
      }

      /* set up rows */
      builder.setNumRows(nrows);
      for (int i = 0; i < nrows; ++i)
      {
         const SVectorBase<R> rowVector = lp.rowVector(i);
         int rowlength = rowVector.size();
         int* indices = new int[rowlength];
         R* rowValues = new R[rowlength];
         for (int j = 0; j < rowlength; j++)
         {
            const Nonzero<R> element = rowVector.element(j);
            indices[j] = element.idx;
            rowValues[j] = element.val;
            std::cout << "writing " <<  indices[j] << " value " << rowValues[j] << "\n";
         }
         builder.addRowEntries(i, rowlength, indices, rowValues);

         R lhs = lp.lhs(i);
         R rhs = lp.rhs(i);
         builder.setRowLhs(i, lhs);
         builder.setRowRhs(i, rhs);
         builder.setRowLhsInf(i, lhs <= -R(infinity));
         builder.setRowRhsInf(i, rhs >= R(infinity));
      }

      return builder.build();
   }


   template<class R>
   typename SPxSimplifier<R>::Result
   Presol<R>::simplify(SPxLPBase<R> &lp, R eps, R ftol, R otol,
                       Real remainingTime, bool keepbounds, uint32_t seed) {

     //TODO: how to use the keepbounds parameter?
      m_keepbounds = keepbounds;

      initLocalVariables(lp);
      papilo::Problem<R> problem = buildProblem(lp);
      papilo::Presolve<R> presolve;

      configurePapilo(presolve, ftol, eps, seed, remainingTime);
      MSG_INFO1((*this->spxout), (*this->spxout) << " --- starting PaPILO" << std::endl;)

      papilo::PresolveResult<R> res = presolve.apply(problem);

      switch (res.status)
      {
         case papilo::PresolveStatus::kInfeasible:
            m_result = SPxSimplifier<R>::INFEASIBLE;
            MSG_INFO1((*this->spxout), (*this->spxout) << " --- presolving detected infeasibility" << std::endl;)
            return SPxSimplifier<R>::INFEASIBLE;
         case papilo::PresolveStatus::kUnbndOrInfeas:
         case papilo::PresolveStatus::kUnbounded:
            m_result = SPxSimplifier<R>::UNBOUNDED;
            MSG_INFO1((*this->spxout), (*this->spxout) << "==== Presolving detected unboundedness of the problem" << std::endl;)
            return SPxSimplifier<R>::UNBOUNDED;
         case papilo::PresolveStatus::kUnchanged:
            // since Soplex has no state unchanged store the value in a new variable
            m_noChanges = true;
            MSG_INFO1((*this->spxout), (*this->spxout) << "==== Presolving found nothing " << std::endl;)
            return SPxSimplifier<R>::OKAY;
         case papilo::PresolveStatus::kReduced:
            break;
      }

      int newNonzeros = problem.getConstraintMatrix().getNnz();

      if(newNonzeros == 0 || ((problem.getNRows() <= modifyConsFac * lp.nRows() ||
            newNonzeros <= modifyConsFac * lp.nNzos())))
      {
         MSG_INFO1((*this->spxout), (*this->spxout) << " --- presolved problem has " << problem.getNRows() << " rows, "
                                                    << problem.getNCols() << " cols and "
                                                    << newNonzeros << " non-zeros"
                                                    << std::endl;)
         postsolveStorage = res.postsolve;
         // remove all constraints and variables
         for (int j = lp.nCols() - 1; j >= 0; j--)
           lp.removeCol(j);
         for (int i = lp.nRows() - 1; i >= 0; i--)
           lp.removeRow(i);
         applyPresolveResultsToColumns(lp, problem, res);
         applyPresolveResultsToRows(lp, problem, res);
         assert(newNonzeros == lp.nNzos());
      }
      else {
        m_noChanges = true;
        MSG_INFO1((*this->spxout),
                  (*this->spxout)
                      << " --- presolve results smaller than the modifyconsfac"
                      << std::endl;)
      }
      if(newNonzeros == 0)
         m_result = SPxSimplifier<R>::VANISHED;
      return m_result;
   }

   template<class R>
   void Presol<R>::initLocalVariables(const SPxLPBase <R> &lp) {
      m_result = SPxSimplifier<R>::OKAY;

      m_thesense = lp.spxSense();
      m_postsolved = false;

      m_prim.reDim(lp.nCols());
      m_slack.reDim(lp.nRows());
      m_dual.reDim(lp.nRows());
      m_redCost.reDim(lp.nCols());
      m_cBasisStat.reSize(lp.nCols());
      m_rBasisStat.reSize(lp.nRows());

      this->m_timeUsed->reset();
      this->m_timeUsed->start();
   }

   template<class R>
   void Presol<R>::configurePapilo(papilo::Presolve<R> &presolve, R feasTolerance, R epsilon, uint32_t seed, Real remainingTime) const {
     /* communicate the SOPLEX parameters to the presolve libary */

      /* communicate the random seed */
      presolve.getPresolveOptions().randomseed = (unsigned int) seed;

     /* set number of threads to be used for presolve */
     /* TODO: set threads for PaPILO? Can Soplex be run with multiple threads?*/
     //      presolve.getPresolveOptions().threads = data->threads;

      presolve.getPresolveOptions().tlim = remainingTime;
      presolve.getPresolveOptions().dualreds = 0;
      presolve.getPresolveOptions().feastol = double(feasTolerance);
      presolve.getPresolveOptions().epsilon = double(epsilon);
      //TODO: has Soplex an parameter to define infinity? otherwise I would use the default
      //      presolve.getPresolveOptions().hugeval = data->hugebound;
      presolve.getPresolveOptions().detectlindep = 0;
      presolve.getPresolveOptions().componentsmaxint = -1;
      presolve.getPresolveOptions().calculate_basis_for_dual = true;

      presolve.setVerbosityLevel(verbosityLevel);

      /* enable lp presolvers with dual postsolve*/
      using uptr = std::unique_ptr<papilo::PresolveMethod<R>>;

      /* fast presolvers*/
      presolve.addPresolveMethod(uptr(new papilo::SingletonCols<R>()));
      presolve.addPresolveMethod(uptr(new papilo::ConstraintPropagation<R>()));

      /* medium presolver */
      presolve.addPresolveMethod(uptr(new papilo::ParallelRowDetection<R>()));
      presolve.addPresolveMethod(uptr(new papilo::ParallelColDetection<R>()));
      presolve.addPresolveMethod(uptr(new papilo::SingletonStuffing<R>()));
      presolve.addPresolveMethod(uptr(new papilo::DualFix<R>()));
      presolve.addPresolveMethod(uptr(new papilo::FixContinuous<R>()));

      /* exhaustive presolvers*/
     presolve.addPresolveMethod(uptr(new papilo::DominatedCols<R>()));

     /**
      * TODO: PaPILO doesn't support dualpostsolve for those presolvers
      *  presolve.addPresolveMethod(uptr(new papilo::SimpleSubstitution<R>()));
      *  presolve.addPresolveMethod(uptr(new papilo::DualInfer<R>()));
      *  presolve.addPresolveMethod(uptr(new papilo::Substitution<R>()));
      *  presolve.addPresolveMethod(uptr(new papilo::Sparsify<R>()));
      *  presolve.getPresolveOptions().removeslackvars = false;
      *  presolve.getPresolveOptions().maxfillinpersubstitution
      *   =data->maxfillinpersubstitution;
      *  presolve.getPresolveOptions().maxshiftperrow = data->maxshiftperrow;
      */


   }

   template<class R>
   void Presol<R>::applyPresolveResultsToColumns(SPxLPBase <R> &lp, const papilo::Problem<R> &problem,
                                                 const papilo::PresolveResult<R> &res) const {

      const papilo::Objective<R> &objective = problem.getObjective();
      const papilo::Vec<R> &upperBounds = problem.getUpperBounds();
      const papilo::Vec<R> &lowerBounds = problem.getLowerBounds();
      const papilo::Vec<papilo::ColFlags> &colFlags = problem.getColFlags();

      for (int col = 0; col < problem.getNCols(); col++) {
        DSVectorBase<R> emptyVector{0};
        R lb = lowerBounds[col];
        if (colFlags[col].test(papilo::ColFlag::kLbInf))
          lb = -R(infinity);
        R ub = upperBounds[col];
        if (colFlags[col].test(papilo::ColFlag::kUbInf))
          ub = R(infinity);
        LPColBase<R> column(objective.coefficients[col], emptyVector, ub, lb);
        lp.addCol(column);
      }
      lp.changeObjOffset(objective.offset);

      assert(problem.getNCols() == lp.nCols());
   }

   template<class R>
   void Presol<R>::applyPresolveResultsToRows(SPxLPBase <R> &lp, const papilo::Problem<R> &problem,
                                           const papilo::PresolveResult<R> &res) const {
      int size = res.postsolve.origrow_mapping.size();

      //add the adjusted constraints
      for(int row =0; row < size; row++){
        R rhs = problem.getConstraintMatrix().getRightHandSides()[row];
        if( problem.getRowFlags()[row].test(papilo::RowFlag::kRhsInf))
          rhs = R(infinity);
        R lhs = problem.getConstraintMatrix().getLeftHandSides()[row];
        if( problem.getRowFlags()[row].test(papilo::RowFlag::kLhsInf))
          lhs = -R(infinity);
        const papilo::SparseVectorView<R> papiloRowVector =
            problem.getConstraintMatrix().getRowCoefficients(row);
        const int* indices = papiloRowVector.getIndices();
        const R *values = papiloRowVector.getValues();

        int length = papiloRowVector.getLength();
        DSVectorBase<R> soplexRowVector{length};
        for( int i=0; i< length; i++){
          soplexRowVector.add(indices[i], values[i]);
        }

        LPRowBase<R> lpRowBase(lhs, soplexRowVector, rhs);
        lp.addRow(lpRowBase);
      }

      assert(problem.getNRows() == lp.nRows());
   }

} // namespace soplex

#endif
