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
#include "exceptions.h"

namespace soplex
{
/** Initialize Vectors
 
    Computing the right hand side vector for the feasibility vector depends on
    the chosen representation and type of the basis.
 
    In columnwise case, |theFvec| = \f$x_B = A_B^{-1} (- A_N x_N)\f$, where \f$x_N\f$
    are either upper or lower bounds for the nonbasic variables (depending on
    the variables |Status|). If these values remain unchanged throughout the
    simplex algorithm, they may be taken directly from LP. However, in the
    entering type algorith they are changed and, hence, retreived from the
    column or row upper or lower bound vectors.
 
    In rowwise case, |theFvec| = \f$\pi^T_B = (c^T - 0^T A_N) A_B^{-1}\f$. However,
    this applies only to leaving type algorithm, where no bounds on dual
    variables are altered. In entering type algorithm they are changed and,
    hence, retreived from the column or row upper or lower bound vectors.
 */
  template <class R>
void SPxSolver<R>::computeFrhs()
{

   if (rep() == COLUMN)
   {
      theFrhs->clear();

      if (type() == LEAVE)
      {
         computeFrhsXtra();

         for(int i = 0; i < this->nRows(); i++)
         {
            Real x;

            typename SPxBasis<R>::Desc::Status stat = this->desc().rowStatus(i);

            if (!isBasic(this->stat))
            {
               switch (this->stat)
               {
                  // columnwise cases:
               case SPxBasis<R>::Desc::P_FREE :
                  continue;

               case (SPxBasis<R>::Desc::P_FIXED) :
                  assert(EQ(lhs(i), rhs(i)));
                  //lint -fallthrough
               case SPxBasis<R>::Desc::P_ON_UPPER :
                  x = this->rhs(i);
                  break;
               case SPxBasis<R>::Desc::P_ON_LOWER :
                  x = this->lhs(i);
                  break;

               default:
                  MSG_ERROR( std::cerr << "ESVECS01 ERROR: "
                                    << "inconsistent basis must not happen!" 
                                    << std::endl; )
                  throw SPxInternalCodeException("XSVECS01 This should never happen.");
               }
               assert(x < infinity);
               assert(x > -infinity);
               (*theFrhs)[i] += x;     // slack !
            }
         }
      }
      else
      {
         computeFrhs1(*theUbound, *theLbound);
         computeFrhs2(*theCoUbound, *theCoLbound);
      }
   }
   else
   {
      assert(rep() == ROW);

      if (type() == ENTER)
      {
         theFrhs->clear();
         computeFrhs1(*theUbound, *theLbound);
         computeFrhs2(*theCoUbound, *theCoLbound);
         *theFrhs += this->maxObj();
      }
      else
      {
         ///@todo put this into a separate method
         *theFrhs = this->maxObj();
         const typename SPxBasis<R>::Desc& ds = this->desc();
         for (int i = 0; i < this->nRows(); ++i)
         {
            typename SPxBasis<R>::Desc::Status stat = ds.rowStatus(i);

            if (!isBasic(this->stat))
            {
               Real x;

               switch (this->stat)
               {
               case SPxBasis<R>::Desc::D_FREE :
                  continue;

               case SPxBasis<R>::Desc::D_ON_UPPER :
               case SPxBasis<R>::Desc::D_ON_LOWER :
               case (SPxBasis<R>::Desc::D_ON_BOTH) :
                  x = this->maxRowObj(i);
                  break;

               default:
                  assert(this->lhs(i) <= -infinity && this->rhs(i) >= infinity);
                  x = 0.0;
                  break;
               }
               assert(x < infinity);
               assert(x > -infinity);
               // assert(x == 0.0);

               if (x != 0.0)
                  theFrhs->multAdd(x, vector(i));
            }
         }
      }
   }
}

  template <class R>
void SPxSolver<R>::computeFrhsXtra()
{

   assert(rep()  == COLUMN);
   assert(type() == LEAVE);

   for (int i = 0; i < this->nCols(); ++i)
   {
      typename SPxBasis<R>::Desc::Status stat = this->desc().colStatus(i);

      if (!isBasic(this->stat))
      {
         Real x;

         switch (this->stat)
         {
            // columnwise cases:
         case SPxBasis<R>::Desc::P_FREE :
            continue;

         case (SPxBasis<R>::Desc::P_FIXED) :
            assert(EQ(SPxLP::lower(i), SPxLP::upper(i)));
            //lint -fallthrough
         case SPxBasis<R>::Desc::P_ON_UPPER :
            x = SPxLP::upper(i);
            break;
         case SPxBasis<R>::Desc::P_ON_LOWER :
            x = SPxLP::lower(i);
            break;

         default:
            MSG_ERROR( std::cerr << "ESVECS02 ERROR: "
                              << "inconsistent basis must not happen!" 
                              << std::endl; )
            throw SPxInternalCodeException("XSVECS02 This should never happen.");
         }
         assert(x < infinity);
         assert(x > -infinity);

         if (x != 0.0)
            theFrhs->multAdd(-x, vector(i));
      }
   }
}


/** This methods subtracts \f$A_N x_N\f$ or \f$\pi_N^T A_N\f$ from |theFrhs| as
    specified by the |Status| of all nonbasic variables. The values of \f$x_N\f$ or
    \f$\pi_N\f$ are taken from the passed arrays.
 */
  template <class R>
void SPxSolver<R>::computeFrhs1(
   const Vector& ufb,    ///< upper feasibility bound for variables
   const Vector& lfb)    ///< lower feasibility bound for variables
{

   const typename SPxBasis<R>::Desc& ds = this->desc();

   for (int i = 0; i < coDim(); ++i)
   {
      typename SPxBasis<R>::Desc::Status stat = ds.status(i);

      if (!isBasic(this->stat))
      {
         Real x;

         switch (stat)
         {
         case SPxBasis<R>::Desc::D_FREE :
         case SPxBasis<R>::Desc::D_UNDEFINED :
         case SPxBasis<R>::Desc::P_FREE :
            continue;

         case SPxBasis<R>::Desc::P_ON_UPPER :
         case SPxBasis<R>::Desc::D_ON_UPPER :
            x = ufb[i];
            break;
         case SPxBasis<R>::Desc::P_ON_LOWER :
         case SPxBasis<R>::Desc::D_ON_LOWER :
            x = lfb[i];
            break;

         case (SPxBasis<R>::Desc::P_FIXED) :
            assert(EQ(lfb[i], ufb[i]));
            //lint -fallthrough
         case (SPxBasis<R>::Desc::D_ON_BOTH) :
            x = lfb[i];
            break;

         default:
            MSG_ERROR( std::cerr << "ESVECS03 ERROR: "
                              << "inconsistent basis must not happen!" 
                              << std::endl; )
            throw SPxInternalCodeException("XSVECS04 This should never happen.");
         }
         assert(x < infinity);
         assert(x > -infinity);

         if (x != 0.0)
            theFrhs->multAdd(-x, vector(i));
      }
   }
}

/** This methods subtracts \f$A_N x_N\f$ or \f$\pi_N^T A_N\f$ from |theFrhs| as
    specified by the |Status| of all nonbasic variables. The values of 
    \f$x_N\f$ or \f$\pi_N\f$ are taken from the passed arrays.
 */
  template <class R>
void SPxSolver<R>::computeFrhs2(
   Vector& coufb,   ///< upper feasibility bound for covariables
   Vector& colfb)   ///< lower feasibility bound for covariables
{
   const typename SPxBasis<R>::Desc& ds = this->desc();

   for(int i = 0; i < dim(); ++i)
   {
      typename SPxBasis<R>::Desc::Status stat = ds.coStatus(i);

      if (!isBasic(this->stat))
      {
         Real x;

         switch (stat)
         {
         case SPxBasis<R>::Desc::D_FREE :
         case SPxBasis<R>::Desc::D_UNDEFINED :
         case SPxBasis<R>::Desc::P_FREE :
            continue;

         case SPxBasis<R>::Desc::P_ON_LOWER :            // negative slack bounds!
         case SPxBasis<R>::Desc::D_ON_UPPER :
            x = coufb[i];
            break;
         case SPxBasis<R>::Desc::P_ON_UPPER :            // negative slack bounds!
         case SPxBasis<R>::Desc::D_ON_LOWER :
            x = colfb[i];
            break;
         case SPxBasis<R>::Desc::P_FIXED :
         case SPxBasis<R>::Desc::D_ON_BOTH :

            if (colfb[i] != coufb[i])
            {
               MSG_WARNING( (*spxout), (*spxout) << "WSVECS04 Frhs2[" << i << "]: " << stat << " "
                                   << colfb[i] << " " << coufb[i]
                                   << " shouldn't be" << std::endl; )
               if( isZero(colfb[i]) || isZero(coufb[i]) )
                  colfb[i] = coufb[i] = 0.0;
               else
               {
                  Real mid = (colfb[i] + coufb[i]) / 2.0;
                  colfb[i] = coufb[i] = mid;
               }
            }
            assert(EQ(colfb[i], coufb[i]));
            x = colfb[i];
            break;

         default:
            MSG_ERROR( std::cerr << "ESVECS05 ERROR: "
                              << "inconsistent basis must not happen!" 
                              << std::endl; )
            throw SPxInternalCodeException("XSVECS05 This should never happen.");
         }
         assert(x < infinity);
         assert(x > -infinity);

         (*theFrhs)[i] -= x; // This is a slack, so no need to multiply a vector.
      }
   }
}

/** Computing the right hand side vector for |theCoPvec| depends on
    the type of the simplex algorithm. In entering algorithms, the
    values are taken from the inequality's right handside or the
    column's objective value.
    
    In contrast to this leaving algorithms take the values from vectors
    |theURbound| and so on.
    
    We reflect this difference by providing two pairs of methods
    |computeEnterCoPrhs(n, stat)| and |computeLeaveCoPrhs(n, stat)|. The first
    pair operates for entering algorithms, while the second one is intended for
    leaving algorithms.  The return value of these methods is the right hand
    side value for the \f$n\f$-th row or column id, respectively, if it had the
    passed |Status| for both.
 
    Both methods are again split up into two methods named |...4Row(i,n)| and
    |...4Col(i,n)|, respectively. They do their job for the |i|-th basis
    variable, being the |n|-th row or column.  
*/
  template <class R>
void SPxSolver<R>::computeEnterCoPrhs4Row(int i, int n)
{
   assert(baseId(i).isSPxRowId());
   assert(number(SPxRowId(baseId(i))) == n);

   switch (this->desc().rowStatus(n))
   {
   // rowwise representation:
   case SPxBasis<R>::Desc::P_FIXED :
      assert(this->lhs(n) > -infinity);
      assert(EQ(this->rhs(n), this->lhs(n)));
      //lint -fallthrough
   case SPxBasis<R>::Desc::P_ON_UPPER :
      assert(rep() == ROW);
      assert(this->rhs(n) < infinity);
      (*theCoPrhs)[i] = this->rhs(n);
      break;
   case SPxBasis<R>::Desc::P_ON_LOWER :
      assert(rep() == ROW);
      assert(this->lhs(n) > -infinity);
      (*theCoPrhs)[i] = this->lhs(n);
      break;

      // columnwise representation:
      // slacks must be left 0!
   default:
      (*theCoPrhs)[i] = this->maxRowObj(n);
      break;
   }
}

  template <class R>
void SPxSolver<R>::computeEnterCoPrhs4Col(int i, int n)
{
   assert(baseId(i).isSPxColId());
   assert(number(SPxColId(baseId(i))) == n);
   switch (this->desc().colStatus(n))
   {
      // rowwise representation:
   case SPxBasis<R>::Desc::P_FIXED :
      assert(EQ(SPxLP::upper(n), SPxLP::lower(n)));
      assert(SPxLP::lower(n) > -infinity);
      //lint -fallthrough
   case SPxBasis<R>::Desc::P_ON_UPPER :
      assert(rep() == ROW);
      assert(SPxLP::upper(n) < infinity);
      (*theCoPrhs)[i] = SPxLP::upper(n);
      break;
   case SPxBasis<R>::Desc::P_ON_LOWER :
      assert(rep() == ROW);
      assert(SPxLP::lower(n) > -infinity);
      (*theCoPrhs)[i] = SPxLP::lower(n);
      break;

      // columnwise representation:
   case SPxBasis<R>::Desc::D_UNDEFINED :
   case SPxBasis<R>::Desc::D_ON_BOTH :
   case SPxBasis<R>::Desc::D_ON_UPPER :
   case SPxBasis<R>::Desc::D_ON_LOWER :
   case SPxBasis<R>::Desc::D_FREE :
      assert(rep() == COLUMN);
      (*theCoPrhs)[i] = this->maxObj(n);
      break;

   default:             // variable left 0
      (*theCoPrhs)[i] = 0;
      break;
   }
}

  template <class R>
void SPxSolver<R>::computeEnterCoPrhs()
{
   assert(type() == ENTER);

   for (int i = dim() - 1; i >= 0; --i)
   {
      SPxId l_id = this->baseId(i);
      if (l_id.isSPxRowId())
         computeEnterCoPrhs4Row(i, this->number(SPxRowId(l_id)));
      else
         computeEnterCoPrhs4Col(i, this->number(SPxColId(l_id)));
   }
}

  template <class R>
void SPxSolver<R>::computeLeaveCoPrhs4Row(int i, int n)
{
   assert(baseId(i).isSPxRowId());
   assert(this->number(SPxRowId(baseId(i))) == n);
   switch (this->desc().rowStatus(n))
   {
   case SPxBasis<R>::Desc::D_ON_BOTH :
   case SPxBasis<R>::Desc::P_FIXED :
      assert(theLRbound[n] > -infinity);
      assert(EQ(theURbound[n], theLRbound[n]));
      //lint -fallthrough
   case SPxBasis<R>::Desc::D_ON_UPPER :
   case SPxBasis<R>::Desc::P_ON_UPPER :
      assert(theURbound[n] < infinity);
      (*theCoPrhs)[i] = theURbound[n];
      break;

   case SPxBasis<R>::Desc::D_ON_LOWER :
   case SPxBasis<R>::Desc::P_ON_LOWER :
      assert(theLRbound[n] > -infinity);
      (*theCoPrhs)[i] = theLRbound[n];
      break;

   default:
      (*theCoPrhs)[i] = this->maxRowObj(n);
      break;
   }
}

  template <class R>
void SPxSolver<R>::computeLeaveCoPrhs4Col(int i, int n)
{
   assert(baseId(i).isSPxColId());
   assert(this->number(SPxColId(baseId(i))) == n);
   switch (this->desc().colStatus(n))
   {
   case SPxBasis<R>::Desc::D_UNDEFINED :
   case SPxBasis<R>::Desc::D_ON_BOTH :
   case SPxBasis<R>::Desc::P_FIXED :
      assert(theLCbound[n] > -infinity);
      assert(EQ(theUCbound[n], theLCbound[n]));
      //lint -fallthrough
   case SPxBasis<R>::Desc::D_ON_LOWER :
   case SPxBasis<R>::Desc::P_ON_UPPER :
      assert(theUCbound[n] < infinity);
      (*theCoPrhs)[i] = theUCbound[n];
      break;
   case SPxBasis<R>::Desc::D_ON_UPPER :
   case SPxBasis<R>::Desc::P_ON_LOWER :
      assert(theLCbound[n] > -infinity);
      (*theCoPrhs)[i] = theLCbound[n];
      break;

   default:
      (*theCoPrhs)[i] = this->maxObj(n);
      //      (*theCoPrhs)[i] = 0;
      break;
   }
}

  template <class R>
void SPxSolver<R>::computeLeaveCoPrhs()
{
   assert(type() == LEAVE);

   for (int i = dim() - 1; i >= 0; --i)
   {
      SPxId l_id = this->baseId(i);
      if (l_id.isSPxRowId())
         computeLeaveCoPrhs4Row(i, this->number(SPxRowId(l_id)));
      else
         computeLeaveCoPrhs4Col(i, this->number(SPxColId(l_id)));
   }
}


/** When computing the copricing vector, we expect the pricing vector to be
    setup correctly. Then computing the copricing vector is nothing but
    computing all scalar products of the pricing vector with the vectors of the
    LPs constraint matrix.
 */
  template <class R>
void SPxSolver<R>::computePvec()
{
   int i;

   for (i = coDim() - 1; i >= 0; --i)
      (*thePvec)[i] = vector(i) * (*theCoPvec);
}

  template <class R>
void SPxSolver<R>::setupPupdate(void)
{
   SSVector& p = thePvec->delta();
   SSVector& c = theCoPvec->delta();

   if (c.isSetup())
   {
      if (c.size() < 0.95 * theCoPvec->dim())
         p.assign2product4setup(*thecovectors, c);
      else
         p.assign2product(c, *thevectors);
   }
   else
   {
      p.assign2productAndSetup(*thecovectors, c);
   }

   p.setup();
}

  template <class R>
void SPxSolver<R>::doPupdate(void)
{
   theCoPvec->update();
   if (pricing() == FULL)
   {
      // thePvec->delta().setup();
      thePvec->update();
   }
}
} // namespace soplex
