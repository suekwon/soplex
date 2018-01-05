/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the class library                   */
/*       SoPlex --- the Sequential object-oriented simPlex.                  */
/*                                                                           */
/*    Copyright (C) 1996-2017 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SoPlex is distributed under the terms of the ZIB Academic Licence.       */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SoPlex; see the file COPYING. If not email to soplex@zib.de.  */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <assert.h>
#include <iostream>

#include "spxdefines.h"
#include "spxsolver.h"
#include "spxpricer.h"
#include "spxratiotester.h"
#include "exceptions.h"

namespace soplex
{

#if 0
  void SPxSolver::localAddRows(int start)
  {
    assert( start <= SPxLP::nRows() );

    /**@todo This method seems to be called, to update
     *       theFvec, theFrhs, ..., but a resolve after
     *       adding a row results in a failure.
     *       To fix this, we call unInit() so that init() is called before solving
     *       in spxsolve.cpp:solve(). In init(), the
     *       vectors are set up, so there is no need
     *       to update them here.
     */
    if( start == SPxLP::nRows() )
      return;

    const SPxBasis::Desc& ds = desc();

    if (type() == ENTER)
      {
        if (rep() == COLUMN)
          {
            int i;
            for (i = start; i < SPxLP::nRows(); ++i)
              {
                theURbound[i] = -lhs(i);
                theLRbound[i] = -rhs(i);
                setEnterBound4Row(i, i);
                computeEnterCoPrhs4Row(i, i);
                // init #theFrhs[i]#:
                Real& v_rhs = (*theFrhs)[i];
                const SVector& row = rowVector(i); 
                v_rhs = 0;
                for (int j = row.size() - 1; j >= 0; --j)
                  {
                    int idx = row.index(j);
                    switch (ds.colStatus(idx))
                      {
                      case Desc::P_ON_UPPER:
                        v_rhs += row.value(j) * theUCbound[idx];
                        break;
                      case Desc::P_ON_LOWER:
                      case Desc::P_FIXED:
                        v_rhs += row.value(j) * theLCbound[idx];
                        break;
                      default:
                        break;
                      }
                  }
              }
            SPxBasis::solve (*theFvec, *theFrhs);
            SPxBasis::coSolve(*theCoPvec, *theCoPrhs);
            for (i = start; i < SPxLP::nRows(); ++i)
              {
                /* we need to compare with tolerance (rep() == COLUMN) ? feastol() : opttol() because theFvec is the primal
                 * vector in COLUMN and the dual vector in ROW representation; this is equivalent to entertol(); this also
                 * fits because we are within the "type() == ENTER" case
                 */
                if (theUBbound[i] + entertol() < (*theFvec)[i])
                  shiftUBbound(i, (*theFvec)[i]);
                else if ((*theFvec)[i] < theLBbound[i] - entertol())
                  shiftLBbound(i, (*theFvec)[i]);
              }
            computePvec();
            computeCoTest();
            computeTest();
          }
        else
          {
            assert(rep() == ROW);
            for (int i = start; i < SPxLP::nRows(); ++i)
              {
                theURbound[i] = theLRbound[i] = maxRowObj(i);
                clearDualBounds(dualRowStatus(i),
                                theURbound[i], theLRbound[i]);
                (*thePvec)[i] = vector(i) * (*theCoPvec);
                theTest[i] = test(i, ds.status(i));
              }
          }
      }
    else
      {
        assert(type() == LEAVE);
        if (rep() == ROW)
          {
            for (int i = start; i < SPxLP::nRows(); ++i)
              {
                theURbound[i] = rhs(i);
                theLRbound[i] = lhs(i);
                (*thePvec)[i] = vector(i) * (*theCoPvec);

                /* we need to compare with tolerance (rep() == ROW) ? feastol() : opttol() because thePvec is the primal
                 * vector in ROW and the dual vector in COLUMN representation; this is equivalent to leavetol(); this also
                 * fits because we are within the "type() == LEAVE" case
                 */
                if (theURbound[i] + leavetol() < (*thePvec)[i])
                  shiftUPbound(i, (*thePvec)[i]);
                else if ((*thePvec)[i] < theLRbound[i] - leavetol())
                  shiftLPbound(i, (*thePvec)[i]);
              }
          }
        else
          {
            assert(rep() == COLUMN);
            int i;
            for (i = start; i < SPxLP::nRows(); ++i)
              {
                theURbound[i] = theLRbound[i] = maxRowObj(i);
                clearDualBounds(ds.rowStatus(i),
                                theURbound[i], theLRbound[i]);
                setLeaveBound4Row(i, i);
                computeLeaveCoPrhs4Row(i, i);
                // init #theFrhs[i]#
                Real& v_rhs = (*theFrhs)[i];
                const SVector& row = rowVector(i); 
                v_rhs = 0;
                for (int j = row.size() - 1; j >= 0; --j)
                  {
                    int idx = row.index(j);
                    switch (ds.colStatus(idx))
                      {
                      case Desc::P_ON_UPPER:
                        v_rhs += row.value(j) * SPxLP::upper(idx);
                        break;
                      case Desc::P_ON_LOWER:
                      case Desc::P_FIXED:
                        v_rhs += row.value(j) * SPxLP::lower(idx);
                        break;
                      default:
                        break;
                      }
                  }
              }
            SPxBasis::solve (*theFvec, *theFrhs);
            SPxBasis::coSolve(*theCoPvec, *theCoPrhs);
            for (i = start; i < SPxLP::nRows(); ++i)
              {
                if ((*theFvec)[i] > theUBbound[i])
                  theCoTest[i] = theUBbound[i] - (*theFvec)[i];
                else
                  theCoTest[i] = (*theFvec)[i] - theLBbound[i];
              }
          }
      }
  }

  void SPxSolver::addedRows(int n)
  {

    SPxLP::addedRows(n);

    if( n > 0 )
      {
        reDim();
      
        if (SPxBasis::status() > SPxBasis::NO_PROBLEM)
          {
            SPxBasis::addedRows(n);

            if (isInitialized())
              {
                localAddRows(nRows() - n);

                assert(thepricer != 0);

                if (rep() == ROW)
                  thepricer->addedVecs(n);
                else
                  thepricer->addedCoVecs(n);            
              }
          }
      }

    /* we must not assert consistency here, since addedCols() might be still necessary to obtain a consistent basis */
  }
#endif //0
  template <class R>
  void SPxSolver<R>::addedRows(int n)
  {

    if( n > 0 )
      {
        SPxLP::addedRows(n);

        unInit();
        reDim();

        if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
          SPxBasis<R>::addedRows(n);
      }

    /* we must not assert consistency here, since addedCols() might be still necessary to obtain a consistent basis */
  }

#if 0
  void SPxSolver::localAddCols(int start)
  {
    assert( start <= SPxLP::nCols() );

    /**@todo This method seems to be called, to update
     *       theFvec, theFrhs, ..., but a resolve after
     *       adding a row results in a failure.
     *       To fix this, we call unIinit() so that init() is called before solving
     *       in spxsolve.cpp:solve(). In init(), the
     *       vectors are set up, so there is no need
     *       to update them here.
     */
    if( start == SPxLP::nCols() )
      return;

    const SPxBasis::Desc& ds = desc();

    if (type() == ENTER)
      {
        if (rep() == COLUMN)
          {
            int reSolve = 0;
            int i;
            Real x;
            for (i = start; i < SPxLP::nCols(); ++i)
              {
                (*thePvec)[i] = vector(i) * (*theCoPvec);
                theTest[i] = test(i, ds.colStatus(i));
                theUCbound[i] = SPxLP::upper(i);
                theLCbound[i] = SPxLP::lower(i);
                switch (ds.colStatus(i))
                  {
                  case SPxBasis::Desc::P_ON_LOWER + SPxBasis::Desc::P_ON_UPPER :
                    assert(SPxLP::lower(i) == SPxLP::upper(i));
                    /*FALLTHROUGH*/
                  case SPxBasis::Desc::P_ON_UPPER :
                    x = SPxLP::upper(i);
                    break;
                  case SPxBasis::Desc::P_ON_LOWER :
                    x = SPxLP::lower(i);
                    break;
                  default:
                    x = 0;
                    break;
                  }
                if (x)
                  {
                    theFrhs->multAdd(-x, vector(i));
                    reSolve = 1;
                  }
              }
            if (reSolve)
              {
                SPxBasis::solve(*theFvec, *theFrhs);
                shiftFvec();
              }
          }
        else
          {
            int i;
            for (i = start; i < SPxLP::nCols(); ++i)
              {
                theUCbound[i] = theLCbound[i] = 0;
                (*theFrhs)[i] = SPxLP::spxSense() * SPxLP::obj(i);
                clearDualBounds(ds.colStatus(i),
                                theUCbound[i], theLCbound[i]);
                setEnterBound4Col(i, i);
                computeEnterCoPrhs4Col(i, i);
              }
            SPxBasis::coSolve(*theCoPvec, *theCoPrhs);
            computePvec();
            computeCoTest();
            computeTest();
            SPxBasis::solve(*theFvec, *theFrhs);
            for (i = start; i < SPxLP::nCols(); ++i)
              {
                /* we need to compare with tolerance (rep() == COLUMN) ? feastol() : opttol() because theFvec is the primal
                 * vector in COLUMN and the dual vector in ROW representation; this is equivalent to entertol(); this also
                 * fits because we are within the "type() == ENTER" case
                 */
                if (theUBbound[i] + entertol() < (*theFvec)[i])
                  shiftUBbound(i, (*theFvec)[i]);
                if ((*theFvec)[i] < theLBbound[i] - entertol())
                  shiftLBbound(i, (*theFvec)[i]);
              }
          }
      }
    else
      {
        if (rep() == ROW)
          {
            int i;
            for (i = start; i < SPxLP::nCols(); ++i)
              {
                theUCbound[i] = SPxLP::upper(i);
                theLCbound[i] = SPxLP::lower(i);
                (*theFrhs)[i] = SPxLP::spxSense() * SPxLP::obj(i);
                setLeaveBound4Col(i, i);
                computeLeaveCoPrhs4Col(i, i);
              }
            SPxBasis::coSolve(*theCoPvec, *theCoPrhs);
            computePvec();
            //          shiftPvec();
            SPxBasis::solve(*theFvec, *theFrhs);
            for (i = start; i < SPxLP::nCols(); ++i)
              {
                if ((*theFvec)[i] > theUBbound[i])
                  theCoTest[i] = theUBbound[i] - (*theFvec)[i];
                else
                  theCoTest[i] = (*theFvec)[i] - theLBbound[i];
              }
          }
        else
          {
            Real x;
            int i;
            int reSolve = 0;
            for (i = start; i < SPxLP::nCols(); ++i)
              {
                theUCbound[i] = theLCbound[i] = -maxObj(i);
                clearDualBounds(ds.colStatus(i),
                                theLCbound[i], theUCbound[i]);
                theUCbound[i] *= -1;
                theLCbound[i] *= -1;

                (*thePvec)[i] = vector(i) * (*theCoPvec);

                /* we need to compare with tolerance (rep() == ROW) ? feastol() : opttol() because thePvec is the primal
                 * vector in ROW and the dual vector in COLUMN representation; this is equivalent to leavetol(); this also
                 * fits because we are within the "type() == LEAVE" case
                 */
                if (theUCbound[i] + leavetol() < (*thePvec)[i])
                  shiftUPbound(i, (*thePvec)[i]);
                if (theLCbound[i] - leavetol() > (*thePvec)[i])
                  shiftLPbound(i, (*thePvec)[i]);

                switch (ds.colStatus(i))
                  {
                  case SPxBasis::Desc::P_ON_LOWER + SPxBasis::Desc::P_ON_UPPER :
                    assert(SPxLP::lower(i) == SPxLP::upper(i));
                    /*FALLTHROUGH*/
                  case SPxBasis::Desc::P_ON_UPPER :
                    x = SPxLP::upper(i);
                    break;
                  case SPxBasis::Desc::P_ON_LOWER :
                    x = SPxLP::lower(i);
                    break;
                  default:
                    x = 0;
                    break;
                  }
                if (x)
                  {
                    theFrhs->multAdd(-x, vector(i));
                    reSolve = 1;
                  }
              }
            if (reSolve)
              {
                SPxBasis::solve(*theFvec, *theFrhs);
                computeFtest();
              }
          }
      }
  }

  void SPxSolver::addedCols(int n)
  {
    SPxLP::addedCols(n);

    if( n > 0 )
      {
        reDim();
      
        if (SPxBasis::status() > SPxBasis::NO_PROBLEM)
          {
            SPxBasis::addedCols(n);
            if (isInitialized())
              {
                localAddCols(nCols() - n);
                assert(thepricer != 0);
                if (rep() == COLUMN)
                  thepricer->addedVecs(n);
                else
                  thepricer->addedCoVecs(n);
              }
          }
      }

    /* we must not assert consistency here, since addedRows() might be still necessary to obtain a consistent basis */
  }
#endif //0

  template <class R>
  void SPxSolver<R>::addedCols(int n)
  {

    if( n > 0 )
      {
        SPxLP::addedCols(n);

        unInit();
        reDim();

        if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
          SPxBasis<R>::addedCols(n);
      }

    /* we must not assert consistency here, since addedRows() might be still necessary to obtain a consistent basis */
  }

  template <class R>
  void SPxSolver<R>::doRemoveRow(int i)
  {

    SPxLP::doRemoveRow(i);

    unInit();

    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        this->removedRow(i);

#if 0
        if (isInitialized())
          {
            int n = SPxLP::nRows();

            theURbound[i] = theURbound[n];
            theLRbound[i] = theLRbound[n];

            if (rep() == ROW)
              {
                (*thePvec)[i] = (*thePvec)[n];
                if (type() == ENTER)
                  theTest[i] = theTest[n];
                reDim();
                assert(thepricer != 0);
                thepricer->removedVec(i);
              }
            else
              {
                unInit();
              }
          }
#endif // 0

        switch (SPxBasis<R>::status())
          {
          case SPxBasis<R>::DUAL:
          case SPxBasis<R>::INFEASIBLE:
            setBasisStatus(SPxBasis<R>::REGULAR);
            break;
          case SPxBasis<R>::OPTIMAL:
            setBasisStatus(SPxBasis<R>::PRIMAL);
            break;
          default:
            break;
          }
      }
  }

  template <class R>
  void SPxSolver<R>::doRemoveRows(int perm[])
  {

    SPxLP::doRemoveRows(perm);

    unInit();

    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        this->removedRows(perm);
#if 0
        if (isInitialized())
          {
            int n = SPxLP::nRows();

            if (rep() == ROW)
              {
                if (type() == ENTER)
                  {
                    for (int i = 0; i < n; ++i)
                      if (perm[i] >= 0)
                        {
                          theURbound[perm[i]] = theURbound[i];
                          theLRbound[perm[i]] = theLRbound[i];
                          (*thePvec)[perm[i]] = (*thePvec)[i];
                          theTest[perm[i]] = theTest[i];
                        }
                  }
                else
                  {
                    for (int i = 0; i < n; ++i)
                      if (perm[i] >= 0)
                        {
                          theURbound[perm[i]] = theURbound[i];
                          theLRbound[perm[i]] = theLRbound[i];
                          (*thePvec)[perm[i]] = (*thePvec)[i];
                        }
                  }
                assert(thepricer != 0);
                thepricer->removedVecs(perm);
                reDim();
              }
            else
              {
                unInit();
              }
          }
#endif
        switch (SPxBasis<R>::status())
          {
          case SPxBasis<R>::DUAL:
          case SPxBasis<R>::INFEASIBLE:
            setBasisStatus(SPxBasis<R>::REGULAR);
            break;
          case SPxBasis<R>::OPTIMAL:
            setBasisStatus(SPxBasis<R>::PRIMAL);
            break;
          default:
            break;
          }
      }
  }

  template <class R>
  void SPxSolver<R>::doRemoveCol(int i)
  {
    forceRecompNonbasicValue();

    SPxLP::doRemoveCol(i);

    unInit();

    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        this->removedCol(i);

#if 0
        if (isInitialized())
          {
            int n = SPxLP::nCols();

            theUCbound[i] = theUCbound[n];
            theLCbound[i] = theLCbound[n];
            if (rep() == COLUMN)
              {
                (*thePvec)[i] = (*thePvec)[n];
                if (type() == ENTER)
                  theTest[i] = theTest[n];
                assert(thepricer != 0);
                thepricer->removedVec(i);
                reDim();
              }
            else
              {
                unInit();
              }
          }
#endif //0
        switch (SPxBasis<R>::status())
          {
          case SPxBasis<R>::PRIMAL:
          case SPxBasis<R>::UNBOUNDED:
            setBasisStatus(SPxBasis<R>::REGULAR);
            break;
          case SPxBasis<R>::OPTIMAL:
            setBasisStatus(SPxBasis<R>::DUAL);
            break;
          default:
            break;
          }
      }
  }

  template <class R>
  void SPxSolver<R>::doRemoveCols(int perm[])
  {
    forceRecompNonbasicValue();

    SPxLP::doRemoveCols(perm);

    unInit();

    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        this->removedCols(perm);

#if 0
        if (isInitialized())
          {
            int n = SPxLP::nCols();

            if (rep() == COLUMN)
              {
                if (type() == ENTER)
                  {
                    for (int i = 0; i < n; ++i)
                      if (perm[i] >= 0)
                        {
                          theUCbound[perm[i]] = theUCbound[i];
                          theLCbound[perm[i]] = theLCbound[i];
                          (*thePvec)[perm[i]] = (*thePvec)[i];
                          theTest[perm[i]] = theTest[i];
                        }
                  }
                else
                  {
                    for (int i = 0; i < n; ++i)
                      if (perm[i] >= 0)
                        {
                          theUCbound[perm[i]] = theUCbound[i];
                          theLCbound[perm[i]] = theLCbound[i];
                          (*thePvec)[perm[i]] = (*thePvec)[i];
                        }
                  }
                assert(thepricer != 0);
                thepricer->removedVecs(perm);
                reDim();
              }
            else
              {
                unInit();
              }
          }
#endif //0
        switch (SPxBasis<R>::status())
          {
          case SPxBasis<R>::PRIMAL:
          case SPxBasis<R>::UNBOUNDED:
            setBasisStatus(SPxBasis<R>::REGULAR);
            break;
          case SPxBasis<R>::OPTIMAL:
            setBasisStatus(SPxBasis<R>::DUAL);
            break;
          default:
            break;
          }
      }
  }

  template <class R>
  void SPxSolver<R>::changeObj(const Vector& newObj, bool scale)
  {
    forceRecompNonbasicValue();

    SPxLP::changeObj(newObj, scale);

    /**@todo Factorization remains valid, we do not need a reDim()
     *       pricing vectors should be recomputed.
     */
    unInit();
  }

  template <class R>
  void SPxSolver<R>::changeObj(int i, const Real& newVal, bool scale)
  {
    forceRecompNonbasicValue();

    SPxLP::changeObj(i, newVal, scale);


    /**@todo Factorization remains valid, we do not need a reDim()
     *       pricing vectors should be recomputed.
     */
    unInit();
  }

  template <class R>
  void SPxSolver<R>::changeMaxObj(const Vector& newObj, bool scale)
  {
    forceRecompNonbasicValue();

    SPxLP::changeMaxObj(newObj, scale);

    /**@todo Factorization remains valid, we do not need a reDim()
     *       pricing vectors should be recomputed.
     */
    unInit();
  }

  template <class R>
  void SPxSolver<R>::changeMaxObj(int i, const Real& newVal, bool scale)
  {
    forceRecompNonbasicValue();

    SPxLP::changeMaxObj(i, newVal, scale);

    /**@todo Factorization remains valid, we do not need a reDim()
     *       pricing vectors should be recomputed.
     */
    unInit();
  }

  template <class R>
  void SPxSolver<R>::changeRowObj(const Vector& newObj, bool scale)
  {
    forceRecompNonbasicValue();

    SPxLP::changeRowObj(newObj, scale);

    /**@todo Factorization remains valid, we do not need a reDim()
     *       pricing vectors should be recomputed.
     */
    unInit();
  }

  template <class R>
  void SPxSolver<R>::changeRowObj(int i, const Real& newVal, bool scale)
  {
    forceRecompNonbasicValue();

    SPxLP::changeRowObj(i, newVal, scale);

    /**@todo Factorization remains valid, we do not need a reDim()
     *       pricing vectors should be recomputed.
     */
    unInit();
  }

  template <class R>
  void SPxSolver<R>::changeLowerStatus(int i, Real newLower, Real oldLower)
  {
    typename SPxBasis<R>::Desc::Status& stat      = this->desc().colStatus(i);
    Real                    currUpper = this->upper(i);
    Real                    objChange = 0.0;

    MSG_DEBUG( std::cout << "DCHANG01 changeLowerStatus(): col " << i
               << "[" << newLower << ":" << currUpper << "] " << stat; )

      switch (stat)
        {
        case SPxBasis<R>::Desc::P_ON_LOWER:
          if (newLower <= -infinity)
            {
              if (currUpper >= infinity)
                {
                  stat = SPxBasis<R>::Desc::P_FREE;
                  if( m_nonbasicValueUpToDate && rep() == COLUMN )
                    objChange = -theLCbound[i] * oldLower;
                }
              else
                {
                  stat = SPxBasis<R>::Desc::P_ON_UPPER;
                  if( m_nonbasicValueUpToDate && rep() == COLUMN )
                    objChange = (theUCbound[i] * currUpper) - (theLCbound[i] * oldLower);
                }
            }
          else if( EQ(newLower, currUpper) )
            {
              stat = SPxBasis<R>::Desc::P_FIXED;
              if( m_nonbasicValueUpToDate && rep() == COLUMN )
                objChange = this->maxObj(i) * (newLower - oldLower);
            }
          else if( m_nonbasicValueUpToDate && rep() == COLUMN )
            objChange = theLCbound[i] * (newLower - oldLower);
          break;
        case SPxBasis<R>::Desc::P_ON_UPPER:
          if( EQ(newLower, currUpper) )
            stat = SPxBasis<R>::Desc::P_FIXED;
          break;
        case SPxBasis<R>::Desc::P_FREE:
          if (newLower > -infinity)
            {
              stat = SPxBasis<R>::Desc::P_ON_LOWER;
              if( m_nonbasicValueUpToDate && rep() == COLUMN )
                objChange = theLCbound[i] * newLower;
            }
          break;
        case SPxBasis<R>::Desc::P_FIXED:
          if( NE(newLower, currUpper) )
            {
              stat = SPxBasis<R>::Desc::P_ON_UPPER;
              if( isInitialized() )
                theUCbound[i] = this->maxObj(i);
            }
          break;
        case SPxBasis<R>::Desc::D_FREE:
        case SPxBasis<R>::Desc::D_ON_UPPER:
        case SPxBasis<R>::Desc::D_ON_LOWER:
        case SPxBasis<R>::Desc::D_ON_BOTH:
        case SPxBasis<R>::Desc::D_UNDEFINED:
          if( rep() == ROW && theShift > 0.0 )
            forceRecompNonbasicValue();
          stat = this->dualColStatus(i);
          break;
        default:
          throw SPxInternalCodeException("XCHANG01 This should never happen.");
        }

    MSG_DEBUG( std::cout << " -> " << stat << std::endl; )

      // we only need to update the nonbasic value in column representation (see nonbasicValue() for comparison/explanation)
      if( rep() == COLUMN )
        updateNonbasicValue(objChange);
  }

  template <class R>
  void SPxSolver<R>::changeLower(const Vector& newLower, bool scale)
  {
    // we better recompute the nonbasic value when changing all lower bounds
    forceRecompNonbasicValue();

    SPxLP::changeLower(newLower, scale);

    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        for (int i = 0; i < newLower.dim(); ++i)
          changeLowerStatus(i, this->lower(i));

        unInit();
      }
  }

  template <class R>
  void SPxSolver<R>::changeLower(int i, const Real& newLower, bool scale)
  {
    if( newLower != this->lowerUnscaled(i) )
      {
        Real oldLower = this->lower(i);
        // This has to be done before calling changeLowerStatus() because that is calling
        // basis.dualColStatus() which calls lower() and needs the changed value.
        SPxLP::changeLower(i, newLower, scale);

        if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
          {
            changeLowerStatus(i, this->lower(i), oldLower);
            unInit();
          }
      }
  }

  template <class R>
  void SPxSolver<R>::changeUpperStatus(int i, Real newUpper, Real oldUpper)
  {
    typename SPxBasis<R>::Desc::Status& stat      = this->desc().colStatus(i);
    Real                    currLower = this->lower(i);
    Real                    objChange = 0.0;

    MSG_DEBUG( std::cout << "DCHANG02 changeUpperStatus(): col " << i
               << "[" << currLower << ":" << newUpper << "] " << stat; )

      switch (stat)
        {
        case SPxBasis<R>::Desc::P_ON_LOWER:
          if (newUpper == currLower)
            stat = SPxBasis<R>::Desc::P_FIXED;
          break;
        case SPxBasis<R>::Desc::P_ON_UPPER:
          if (newUpper >= infinity)
            {
              if (currLower <= -infinity)
                {
                  stat = SPxBasis<R>::Desc::P_FREE;
                  if( m_nonbasicValueUpToDate && rep() == COLUMN )
                    objChange = -theUCbound[i] * oldUpper;
                }
              else
                {
                  stat = SPxBasis<R>::Desc::P_ON_LOWER;
                  if( m_nonbasicValueUpToDate && rep() == COLUMN )
                    objChange = (theLCbound[i] * currLower) - (theUCbound[i] * oldUpper);
                }
            }
          else if (EQ(newUpper, currLower))
            {
              stat = SPxBasis<R>::Desc::P_FIXED;
              if( m_nonbasicValueUpToDate && rep() == COLUMN )
                objChange = this->maxObj(i) * (newUpper - oldUpper);
            }
          else if( m_nonbasicValueUpToDate && rep() == COLUMN )
            objChange = theUCbound[i] * (newUpper - oldUpper);
          break;
        case SPxBasis<R>::Desc::P_FREE:
          if (newUpper < infinity)
            {
              stat = SPxBasis<R>::Desc::P_ON_UPPER;
              if( m_nonbasicValueUpToDate && rep() == COLUMN )
                objChange = theUCbound[i] * newUpper;
            }
          break;
        case SPxBasis<R>::Desc::P_FIXED:
          if( NE(newUpper, currLower) )
            {
              stat = SPxBasis<R>::Desc::P_ON_LOWER;
              if( isInitialized() )
                theLCbound[i] = this->maxObj(i);
            }
          break;
        case SPxBasis<R>::Desc::D_FREE:
        case SPxBasis<R>::Desc::D_ON_UPPER:
        case SPxBasis<R>::Desc::D_ON_LOWER:
        case SPxBasis<R>::Desc::D_ON_BOTH:
        case SPxBasis<R>::Desc::D_UNDEFINED:
          if( rep() == ROW && theShift > 0.0 )
            forceRecompNonbasicValue();
          stat = this->dualColStatus(i);
          break;
        default:
          throw SPxInternalCodeException("XCHANG02 This should never happen.");
        }
    MSG_DEBUG( std::cout << " -> " << stat << std::endl; );

    // we only need to update the nonbasic value in column representation (see nonbasicValue() for comparison/explanation)
    if( rep() == COLUMN )
      updateNonbasicValue(objChange);
  }

  template <class R>
  void SPxSolver<R>::changeUpper(const Vector& newUpper, bool scale)
  {
    // we better recompute the nonbasic value when changing all upper bounds
    forceRecompNonbasicValue();

    SPxLP::changeUpper(newUpper, scale);

    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        for (int i = 0; i < newUpper.dim(); ++i)
          changeUpperStatus(i, this->upper(i));

        unInit();
      }
  }

  template <class R>
  void SPxSolver<R>::changeUpper(int i, const Real& newUpper, bool scale)
  {
    if( newUpper != this->upperUnscaled(i) )
      {
        Real oldUpper = this->upper(i);
        SPxLP::changeUpper(i, newUpper, scale);

        if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
          {
            changeUpperStatus(i, this->upper(i), oldUpper);
            unInit();
          }
      }
  }

  template <class R>
  void SPxSolver<R>::changeBounds(const Vector& newLower, const Vector& newUpper, bool scale)
  {
    changeLower(newLower, scale);
    changeUpper(newUpper, scale);
  }

  template <class R>
  void SPxSolver<R>::changeBounds(int i, const Real& newLower, const Real& newUpper, bool scale)
  {
    changeLower(i, newLower, scale);
    changeUpper(i, newUpper, scale);
  }

  template <class R>
  void SPxSolver<R>::changeLhsStatus(int i, Real newLhs, Real oldLhs)
  {
    typename SPxBasis<R>::Desc::Status& stat      = this->desc().rowStatus(i);
    Real                    currRhs   = this->rhs(i);
    Real                    objChange = 0.0;

    MSG_DEBUG( std::cout << "DCHANG03 changeLhsStatus()  : row " << i
               << ": " << stat; )
      switch (stat)
        {
        case SPxBasis<R>::Desc::P_ON_LOWER:
          if (newLhs <= -infinity)
            {
              if (currRhs >= infinity)
                {
                  stat = SPxBasis<R>::Desc::P_FREE;
                  if( m_nonbasicValueUpToDate && rep() == COLUMN )
                    objChange = -theURbound[i] * oldLhs;
                }
              else
                {
                  stat = SPxBasis<R>::Desc::P_ON_UPPER;
                  if( m_nonbasicValueUpToDate && rep() == COLUMN )
                    objChange = (theLRbound[i] * currRhs) - (theURbound[i] * oldLhs);
                }
            }
          else if( EQ(newLhs, currRhs) )
            {
              stat = SPxBasis<R>::Desc::P_FIXED;
              if( m_nonbasicValueUpToDate && rep() == COLUMN )
                objChange = this->maxRowObj(i) * (newLhs - oldLhs);
            }
          else if( m_nonbasicValueUpToDate && rep() == COLUMN )
            objChange = theURbound[i] * (newLhs - oldLhs);
          break;
        case SPxBasis<R>::Desc::P_ON_UPPER:
          if( EQ(newLhs, currRhs) )
            stat = SPxBasis<R>::Desc::P_FIXED;
          break;
        case SPxBasis<R>::Desc::P_FREE:
          if (newLhs > -infinity)
            {
              stat = SPxBasis<R>::Desc::P_ON_LOWER;
              if( m_nonbasicValueUpToDate && rep() == COLUMN )
                objChange = theURbound[i] * newLhs;
            }
          break;
        case SPxBasis<R>::Desc::P_FIXED:
          if( NE(newLhs, currRhs) )
            {
              stat = SPxBasis<R>::Desc::P_ON_UPPER;
              if( isInitialized() )
                theLRbound[i] = this->maxRowObj(i);
            }
          break;
        case SPxBasis<R>::Desc::D_FREE:
        case SPxBasis<R>::Desc::D_ON_UPPER:
        case SPxBasis<R>::Desc::D_ON_LOWER:
        case SPxBasis<R>::Desc::D_ON_BOTH:
        case SPxBasis<R>::Desc::D_UNDEFINED:
          if( rep() == ROW && theShift > 0.0 )
            forceRecompNonbasicValue();
          stat = this->dualRowStatus(i);
          break;
        default:
          throw SPxInternalCodeException("XCHANG03 This should never happen.");
        }
    MSG_DEBUG( std::cout << " -> " << stat << std::endl; )

      // we only need to update the nonbasic value in column representation (see nonbasicValue() for comparison/explanation)
      if( rep() == COLUMN )
        updateNonbasicValue(objChange);
  }

  template <class R>
  void SPxSolver<R>::changeLhs(const Vector& newLhs, bool scale)
  {
    // we better recompute the nonbasic value when changing all lhs
    forceRecompNonbasicValue();

    SPxLP::changeLhs(newLhs, scale);

    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        for (int i = 0; i < this->nRows(); ++i)
          changeLhsStatus(i, this->lhs(i));

        unInit();
      }
  }

  template <class R>
  void SPxSolver<R>::changeLhs(int i, const Real& newLhs, bool scale)
  {
    if( newLhs != this->lhsUnscaled(i) )
      {
        Real oldLhs = this->lhs(i);
        SPxLP::changeLhs(i, newLhs, scale);

        if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
          {
            changeLhsStatus(i, this->lhs(i), oldLhs);
            unInit();
          }
      }
  }

  template <class R>
  void SPxSolver<R>::changeRhsStatus(int i, Real newRhs, Real oldRhs)
  {
    typename SPxBasis<R>::Desc::Status& stat      = this->desc().rowStatus(i);
    Real                    currLhs   = this->lhs(i);
    Real                    objChange = 0.0;

    MSG_DEBUG( std::cout << "DCHANG04 changeRhsStatus()  : row " << i
               << ": " << stat; )
      switch (stat)
        {
        case SPxBasis<R>::Desc::P_ON_UPPER:
          if (newRhs >= infinity)
            {
              if (currLhs <= -infinity)
                {
                  stat = SPxBasis<R>::Desc::P_FREE;
                  if( m_nonbasicValueUpToDate && rep() == COLUMN )
                    objChange = -theLRbound[i] * oldRhs;
                }
              else
                {
                  stat = SPxBasis<R>::Desc::P_ON_LOWER;
                  if( m_nonbasicValueUpToDate && rep() == COLUMN )
                    objChange = (theURbound[i] * currLhs) - (theLRbound[i] * oldRhs);
                }
            }
          else if( EQ(newRhs, currLhs) )
            {
              stat = SPxBasis<R>::Desc::P_FIXED;
              if( m_nonbasicValueUpToDate && rep() == COLUMN )
                objChange = this->maxRowObj(i) * (newRhs - oldRhs);
            }
          else if( m_nonbasicValueUpToDate && rep() == COLUMN )
            objChange = theLRbound[i] * (newRhs - oldRhs);
          break;
        case SPxBasis<R>::Desc::P_ON_LOWER:
          if( EQ(newRhs, currLhs) )
            stat = SPxBasis<R>::Desc::P_FIXED;
          break;
        case SPxBasis<R>::Desc::P_FREE:
          if (newRhs < infinity)
            {
              stat = SPxBasis<R>::Desc::P_ON_UPPER;
              if( m_nonbasicValueUpToDate && rep() == COLUMN )
                objChange = theLRbound[i] * newRhs;
            }
          break;
        case SPxBasis<R>::Desc::P_FIXED:
          if( NE(newRhs, currLhs) )
            {
              stat = SPxBasis<R>::Desc::P_ON_LOWER;
              if( isInitialized() )
                theURbound[i] = this->maxRowObj(i);
            }
          break;
        case SPxBasis<R>::Desc::D_FREE:
        case SPxBasis<R>::Desc::D_ON_UPPER:
        case SPxBasis<R>::Desc::D_ON_LOWER:
        case SPxBasis<R>::Desc::D_ON_BOTH:
        case SPxBasis<R>::Desc::D_UNDEFINED:
          if( rep() == ROW && theShift > 0.0 )
            forceRecompNonbasicValue();
          stat = this->dualRowStatus(i);
          break;
        default:
          throw SPxInternalCodeException("XCHANG04 This should never happen.");
        }
    MSG_DEBUG( std::cout << " -> " << stat << std::endl; )

      // we only need to update the nonbasic value in column representation (see nonbasicValue() for comparison/explanation)
      if( rep() == COLUMN )
        updateNonbasicValue(objChange);
  }

  template <class R>
  void SPxSolver<R>::changeRhs(const Vector& newRhs, bool scale)
  {
    // we better recompute the nonbasic value when changing all rhs
    forceRecompNonbasicValue();

    SPxLP::changeRhs(newRhs, scale);

    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        for (int i = 0; i < this->nRows(); ++i)
          changeRhsStatus(i, this->rhs(i));
        unInit();
      }
  }

  template <class R>
  void SPxSolver<R>::changeRhs(int i, const Real& newRhs, bool scale)
  {
    if( newRhs != this->rhsUnscaled(i) )
      {
        Real oldRhs = this->rhs(i);
        SPxLP::changeRhs(i, newRhs, scale);

        if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
          {
            changeRhsStatus(i, this->rhs(i), oldRhs);
            unInit();
          }
      }
  }

  template <class R>
  void SPxSolver<R>::changeRange(const Vector& newLhs, const Vector& newRhs, bool scale)
  {
    // we better recompute the nonbasic value when changing all ranges
    forceRecompNonbasicValue();

    SPxLP::changeLhs(newLhs, scale);
    SPxLP::changeRhs(newRhs, scale);
    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        for (int i = this->nRows() - 1; i >= 0; --i)
          {
            changeLhsStatus(i, this->lhs(i));
            changeRhsStatus(i, this->rhs(i));
          }
        unInit();
      }
  }

  template <class R>
  void SPxSolver<R>::changeRange(int i, const Real& newLhs, const Real& newRhs, bool scale)
  {
    Real oldLhs = this->lhs(i);
    Real oldRhs = this->rhs(i);

    SPxLP::changeLhs(i, newLhs, scale);
    SPxLP::changeRhs(i, newRhs, scale);

    if (SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM)
      {
        changeLhsStatus(i, this->lhs(i), oldLhs);
        changeRhsStatus(i, this->rhs(i), oldRhs);
        unInit();
      }
  }

  template <class R>
  void SPxSolver<R>::changeRow(int i, const LPRow& newRow, bool scale)
  {
    forceRecompNonbasicValue();

    SPxLP::changeRow(i, newRow, scale);
    if ( SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM )
      SPxBasis<R>::changedRow( i );
    unInit();
  }
  
  template <class R>
  void SPxSolver<R>::changeCol(int i, const LPCol& newCol, bool scale)
  {
    if( i < 0 )
      return;

    forceRecompNonbasicValue();

    SPxLP::changeCol(i, newCol, scale);
    if ( SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM )
      SPxBasis<R>::changedCol( i );
    unInit();
  }

  template <class R>
  void SPxSolver<R>::changeElement(int i, int j, const Real& val, bool scale)
  {
    if( i < 0 || j < 0 )
      return;

    forceRecompNonbasicValue();

    SPxLP::changeElement(i, j, val, scale);
    if ( SPxBasis<R>::status() > SPxBasis<R>::NO_PROBLEM )
      SPxBasis<R>::changedElement( i, j );
    unInit();
  }

  template <class R>
  void SPxSolver<R>::changeSense(typename SPxLPBase<R>::SPxSense sns)
  {

    SPxLP::changeSense(sns);
    unInit();
  }
} // namespace soplex
 
