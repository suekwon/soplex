/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the class library                   */
/*       SoPlex --- the Sequential object-oriented simPlex.                  */
/*                                                                           */
/*    Copyright (C) 1996-2014 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SoPlex is distributed under the terms of the ZIB Academic Licence.       */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SoPlex; see the file COPYING. If not email to soplex@zib.de.  */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef SOPLEX_LEGACY
#include <iostream>
#include <assert.h>

#include "soplex.h"
#include "statistics.h"

#define DEBUGGING
#define SOLVEIDS_DEBUG
//#define EXTRA_PRINT

/* This file contains the private functions for the Improved Dual Simplex (IDS)
 *
 * An important note about the implementation of the IDS is the reliance on the row representation of the basis matrix.
 * The forming of the reduced and complementary problems is involves identifying rows in the row-form of the basis
 * matrix that have zero dual multipliers.
 *
 * Ideally, this work will be extended such that the IDS can be executed using the column-form of the basis matrix. */

namespace soplex
{
   /// solves LP using the improved dual simplex
   void SoPlex::_solveImprovedDualSimplex()
   {
      assert(_solver.rep() == SPxSolver::ROW);
      assert(_solver.type() == SPxSolver::LEAVE);
      assert(_isRealLPLoaded == true);

      // start timing
      _statistics->solvingTime.start();

      // setting the sense to maximise
      if( intParam(SoPlex::OBJSENSE) == SoPlex::OBJSENSE_MINIMIZE )
      {
         assert(_solver.spxSense() == SPxLPBase<Real>::MINIMIZE);

         _solver.changeObj(_solver.maxObj());
         _solver.changeSense(SPxLPBase<Real>::MAXIMIZE);
      }

#ifdef SOLVEIDS_DEBUG
      printf("Writing the original lp to a file\n");
      _solver.writeFile("original.lp");
#endif

      // it is necessary to solve the initial problem to find a starting basis
      _solver.setIdsStatus(SPxSolver::FINDSTARTBASIS);

      Real degeneracyLevel = 0;
      _idsFeasVector.reDim(_solver.nCols());
      // since the original LP may have been shifted, the dual multiplier will not be correct for the original LP. This
      // loop will recheck the degeneracy level and compute the proper dual multipliers.
      do
      {
         _solver.solve();
         _solver.basis().solve(_idsFeasVector, _solver.maxObj());
         degeneracyLevel = _solver.getDegeneracyLevel(_idsFeasVector);
         if( _solver.type() == SPxSolver::LEAVE || _solver.status() == SPxSolver::OPTIMAL )
         {
            // returning the sense to minimise
            if( intParam(SoPlex::OBJSENSE) == SoPlex::OBJSENSE_MINIMIZE )
            {
               assert(_solver.spxSense() == SPxLPBase<Real>::MAXIMIZE);

               _solver.changeObj(-(_solver.maxObj()));
               _solver.changeSense(SPxLPBase<Real>::MINIMIZE);
            }

            _solveReal();
            return;
         }
         printf("Degeneracy Level: %f, Infeasibility: %f\n", degeneracyLevel, _solver.maxInfeas());
      } while( (degeneracyLevel > 0.9 || degeneracyLevel < 0.1) || !checkBasisDualFeasibility(_idsFeasVector) );

      _solver.setIdsStatus(SPxSolver::DONTFINDSTARTBASIS);
      _hasBasis = true; // this is probably wrong. Will need to set this properly.


      //@todo Need to check this. Where will the solving of the IDS take place? In the SoPlex::solve, _solvereal or
      //_solverational?
      // remember that last solve was in floating-point
      _lastSolveMode = SOLVEMODE_REAL;

      // creating copies of the original problem that will be manipulated to form the reduced and complementary
      // problems.
      _createIdsReducedAndComplementaryProblems();

      // creating the initial reduced problem from the basis information
      _formIdsReducedProblem();
      _isRealLPLoaded = false;

      // setting the statistic information
      numIdsIter = 0;
      numRedProbIter = 0;
      numCompProbIter = 0;

      for( int i = 0; i < 10000; i++ )
      {
#ifdef SOLVEIDS_DEBUG
      printf("Writing the reduced lp to a file\n");
      _solver.writeFile("reduced.lp");
#endif
         printf("Reduced Prob Rows: ");
         for( int j = 0; j < numRowsReal(); j++ )
         {
            if( _idsReducedProbRows[j] )
               printf("%d ", j);
         }
         printf("\n");

         // solve the reduced problem
         _solver.solve();

         // updating the iteration counter
         numRedProbIter += _solver.iterations();

         _idsFeasVector.reDim(_solver.nCols());
         _solver.basis().solve(_idsFeasVector, _solver.maxObj());

         // get the dual solutions from the reduced problem
         DVector reducedLPDualVector(_solver.nRows());
         DVector reducedLPRedcostVector(_solver.nCols());
         _solver.getDual(reducedLPDualVector);
         _solver.getRedCost(reducedLPRedcostVector);

         if( i == 0 )
            _formIdsComplementaryProblem();  // create the complementary problem using
                                             // the solution to the reduced problem
         else
            _updateIdsComplementaryProblem(reducedLPDualVector);

#ifdef SOLVEIDS_DEBUG
         printf("Writing the complementary lp to a file\n");
         _compSolver.writeFile("complement.lp");
#endif
         // solve the complementary problem
         _compSolver.solve();

         // updating the iteration counter
         numCompProbIter += _compSolver.iterations();

#ifdef SOLVEIDS_DEBUG
         printf("Iteration %d Objective Value: %f\n", i, _compSolver.objValue());
#endif

         //SPxLPIds compPrimalLP;
         //_compSolver.buildDualProblem(compPrimalLP);

         //char buffer[50];
         //sprintf(buffer, "lpfiles/complement_%d.lp", i);
         //printf("Writing the complementary primal lp to a file\n");
         //compPrimalLP.writeFile(buffer);

         // check the optimality of the original problem with the objective value of the complementary problem
         if( GE(_compSolver.objValue(), 0.0, 1e-10) || _compSolver.status() == SPxSolver::UNBOUNDED )
            break;

         // get the primal solutions from the complementary problem
         DVector compLPPrimalVector(_compSolver.nCols());
         _compSolver.getPrimal(compLPPrimalVector);

         _updateIdsReducedProblem(_compSolver.objValue(), reducedLPDualVector, reducedLPRedcostVector,
               compLPPrimalVector);

         numIdsIter++;
      }

      SPxLPIds compDualLP;
      _compSolver.buildDualProblem(compDualLP, _idsPrimalRowIDs.get_ptr(), _idsPrimalColIDs.get_ptr(),
            _idsDualRowIDs.get_ptr(), _idsDualColIDs.get_ptr(), &_nPrimalRows, &_nPrimalCols, &_nDualRows, &_nDualCols);

      _compSolver.loadLP(compDualLP);
      printf("Writing the complementary lp to a file\n");
      _compSolver.writeFile("complement.lp");

      // returning the sense to minimise
      if( intParam(SoPlex::OBJSENSE) == SoPlex::OBJSENSE_MINIMIZE )
      {
         assert(_solver.spxSense() == SPxLPBase<Real>::MAXIMIZE);

         _solver.changeObj(-(_solver.maxObj()));
         _solver.changeSense(SPxLPBase<Real>::MINIMIZE);

         // Need to add commands to multiply the objective solution values by -1
      }

      spx_free(_idsCompProbColIDsIdx);
      spx_free(_idsCompProbRowIDsIdx);
      spx_free(_fixedOrigVars);

      // printing the statistics
      printf("Number of algorithm iterations: %d\n", numIdsIter);
      printf("Number of Reduced Problem simplex iterations: %d\n", numRedProbIter);
      printf("Number of Complementary Problem simplex iterations: %d\n", numCompProbIter);

      // stop timing
      _statistics->solvingTime.stop();
   }



   /// creating copies of the original problem that will be manipulated to form the reduced and complementary problems
   void SoPlex::_createIdsReducedAndComplementaryProblems()
   {
      // the reduced problem is formed from the current problem
      // So, we copy the _solver to the _realLP and work on the _solver
      // NOTE: there is no need to preprocess because we always have a starting basis.
      _realLP = 0;
      spx_alloc(_realLP);
      _realLP = new (_realLP) SPxLPIds(_solver);

      // allocating memory for the reduced problem rows and cols flag array
      _idsReducedProbRows = 0;
      spx_alloc(_idsReducedProbRows, numRowsReal());
      _idsReducedProbCols = 0;
      spx_alloc(_idsReducedProbCols, numColsReal());

      // the complementary problem is formulated with all incompatible rows and those from the reduced problem that have
      // a positive reduced cost.
      _compSolver = _solver;
      _compSolver.setSolver(&_compSlufactor);
   }



   /// forms the reduced problem
   void SoPlex::_formIdsReducedProblem()
   {
#ifdef SOLVEIDS_DEBUG
      printf("Forming the reduced problem\n");
#endif
      int* bind = 0;
      int* nonposind = 0;
      int* compatind = 0;
      int* rowsforremoval = 0;
      int* colsforremoval = 0;
      int nnonposind = 0;
      int ncompatind = 0;

      _idsRedLP = 0;
      spx_alloc(_idsRedLP);
      _idsRedLP = new (_idsRedLP) SPxLPIds(_solver);

      // retreiving the basis information
      _basisStatusRows.reSize(numRowsReal());
      _basisStatusCols.reSize(numColsReal());
      _solver.getBasis(_basisStatusRows.get_ptr(), _basisStatusCols.get_ptr());

      // get thie indices of the rows with positive dual multipliers and columns with positive reduced costs.
      spx_alloc(bind, numColsReal());

      spx_alloc(nonposind, numColsReal());
      spx_alloc(colsforremoval, numColsReal());
      _getNonPositiveDualMultiplierInds(_idsFeasVector, nonposind, bind, colsforremoval, &nnonposind);

      // get the compatible columns from the constraint matrix w.r.t the current basis matrix
#ifdef SOLVEIDS_DEBUG
      printf("Computing the compatible columns\n");
      printf("Solving time: %f\n", solveTime());
#endif
      spx_alloc(compatind, numRowsReal());
      spx_alloc(rowsforremoval, numRowsReal());
      _getCompatibleColumns(nonposind, compatind, rowsforremoval, colsforremoval, nnonposind, &ncompatind);

      int* compatboundcons = 0;
      int ncompatboundcons = 0;
      spx_alloc(compatboundcons, numColsReal());

      LPRowSet boundcons;

      // identifying the compatible bound constraints
      _getCompatibleBoundCons(boundcons, compatboundcons, nonposind, &ncompatboundcons, nnonposind);

      // delete rows and columns from the LP to form the reduced problem
#ifdef SOLVEIDS_DEBUG
      printf("Deleting rows and columns to form the reduced problem\n");
      printf("Solving time: %f\n", solveTime());
#endif

      VectorReal origObj = _solver.maxObj();

      // computing the reduced problem obj coefficient vector and updating the problem
      _computeReducedProbObjCoeff();

      printf("Nonposinds obj coefficients\n");
      for( int i = 0; i < nnonposind; i++ )
      {
         if( !isZero(_idsRedLP->maxObj(nonposind[i])) )
            printf("nonposind[%d]: %d, coeff: %f\n", i, nonposind[i], _idsRedLP->maxObj(nonposind[i]));
      }

      _solver.loadLP(*_idsRedLP);

      // the colsforremoval are the columns with a zero reduced cost.
      // the rowsforremoval are the rows identified as incompatible.
      _deleteRowsAndColumnsReducedProblem(colsforremoval, rowsforremoval);

      // adding the rows for the compatible bound constraints
      SPxRowId* addedrowids = 0;
      spx_alloc(addedrowids, ncompatboundcons);
      _solver.addRows(addedrowids, boundcons);

      for( int i = 0; i < ncompatboundcons; i++ )
         _idsReducedProbColRowIDs[compatboundcons[i]] = addedrowids[i];


      VectorReal finalObj = _solver.maxObj();

      // freeing allocated memory
      spx_free(addedrowids);
      spx_free(compatboundcons);
      spx_free(rowsforremoval);
      spx_free(compatind);
      spx_free(colsforremoval);
      spx_free(nonposind);
      spx_free(bind);
      spx_free(_idsRedLP);
   }



   /// forms the complementary problem
   void SoPlex::_formIdsComplementaryProblem()
   {
      // delete rows and columns from the LP to form the reduced problem
      _nElimPrimalRows = 0;
      _idsElimPrimalRowIDs.reSize(_realLP->nRows());   // the number of eliminated rows is less than the number of rows
                                                      // in the reduced problem
      _idsCompProbRowIDsIdx = 0;
      _idsCompProbColIDsIdx = 0;
      spx_alloc(_idsCompProbRowIDsIdx, _realLP->nRows());
      spx_alloc(_idsCompProbColIDsIdx, _realLP->nCols());
      for( int i = 0; i < _realLP->nRows(); i++ )
         _idsCompProbRowIDsIdx[i] = -1;

      int* initFixedVars = 0;
      _fixedOrigVars = 0;
      spx_alloc(initFixedVars, _realLP->nCols());
      spx_alloc(_fixedOrigVars, _realLP->nCols());
      for (int i = 0; i < _realLP->nCols(); i++)
      {
         _idsCompProbColIDsIdx[i] = -1;
         initFixedVars[i] = 0;
         _fixedOrigVars[i] = -2; // this must be set to a value differet from -1, 0, 1 to ensure the
                                 // _updateComplementaryFixedPrimalVars function updates all variables in the problem.
      }

      _deleteAndUpdateRowsComplementaryProblem(initFixedVars);

      // initialising the arrays to store the row id's from the primal and the col id's from the dual
      _idsPrimalRowIDs.reSize(3*_compSolver.nRows());
      _idsPrimalColIDs.reSize(3*_compSolver.nCols());
      _idsDualRowIDs.reSize(3*_compSolver.nCols());
      _idsDualColIDs.reSize(3*_compSolver.nRows());
      _nPrimalRows = 0;
      _nPrimalCols = 0;
      _nDualRows = 0;
      _nDualCols = 0;

      // convert complementary problem to dual problem
      printf("Writing the complementary primal lp to a file\n");
      _compSolver.writeFile("comp_primal.lp");

      SPxLPIds compDualLP;
      _compSolver.buildDualProblem(compDualLP, _idsPrimalRowIDs.get_ptr(), _idsPrimalColIDs.get_ptr(),
            _idsDualRowIDs.get_ptr(), _idsDualColIDs.get_ptr(), &_nPrimalRows, &_nPrimalCols, &_nDualRows, &_nDualCols);

      // setting the id index array for the complementary problem
      for( int i = 0; i < _nPrimalRows; i++ )
      {
         _idsCompProbRowIDsIdx[_realLP->number(_idsPrimalRowIDs[i])] = i;
         // a primal row may result in two dual columns. So the index array points to the first of the indicies for the
         // primal row.
         if( i + 1 < _nPrimalRows &&
               _realLP->number(SPxRowId(_idsPrimalRowIDs[i])) == _realLP->number(SPxRowId(_idsPrimalRowIDs[i + 1])) )
            i++;
      }

      for( int i = 0; i < _nPrimalCols; i++ )
      {
         if( _idsPrimalColIDs[i].getIdx() != _compSlackColId.getIdx() )
            _idsCompProbColIDsIdx[_realLP->number(_idsPrimalColIDs[i])] = i;
      }

      // retrieving the dual row id for the complementary slack column
      // there should be a one to one relationship between the number of primal columns and the number of dual rows.
      // hence, it should be possible to equate the dual row id to the related primal column.
      assert(_nPrimalCols == _nDualRows);
      assert(_compSolver.nCols() == compDualLP.nRows());
      _compSlackDualRowId = compDualLP.rId(_compSolver.number(_compSlackColId));

      _compSolver.loadLP(compDualLP);

      _updateComplementarySlackColCoeff();

      // initalising the array for the dual columns representing the fixed variables in the complementary problem.
      _idsFixedVarDualIDs.reSize(_realLP->nCols());
      _idsVarBoundDualIDs.reSize(2*_realLP->nCols());
      _updateComplementaryFixedPrimalVars(initFixedVars);

      //// setting the sense to maximise
      //if( _compSolver.spxSense() == SPxLPBase<Real>::MINIMIZE )
      //{
         //_compSolver.changeObj(_compSolver.maxObj());
         //_compSolver.changeSense(SPxLPBase<Real>::MAXIMIZE);
      //}

      printf("Writing the complementary primal lp to a file\n");
      _compSolver.writeFile("comp_dual.lp");

      spx_free(initFixedVars);
   }



   /// updates the reduced problem with additional rows using the solution to the complementary problem
   void SoPlex::_updateIdsReducedProblem(Real objValue, DVector dualVector, DVector redcostVector,
         DVector compPrimalVector)
   {
      printf("_updateIdsReducedProblem\n");

      Real maxDualRatio = infinity;

      for( int i = 0; i < _nPrimalRows; i++ )
      {
         Real reducedProbDual = 0;
         Real compProbPrimal = 0;
         Real dualRatio = 0;
         int rownumber = _realLP->number(SPxRowId(_idsPrimalRowIDs[i]));
         if( _idsReducedProbRows[rownumber] )
         {
            int solverRowNum = _solver.number(_idsReducedProbRowIDs[rownumber]);

            // retreiving the reduced problem dual solutions and the complementary problem primal solutions
            reducedProbDual = dualVector[solverRowNum]; // this is y
            if( _solver.rowType(solverRowNum) == LPRowBase<Real>::GREATER_EQUAL )
               reducedProbDual *= -1;
            compProbPrimal = compPrimalVector[_compSolver.number(SPxColId(_idsDualColIDs[i]))]; // this is u

#if 0
            printf("%d redProbNum: %d compProbNum: %d origProbNum: %d. y = %.10f u = %.20f, rowtype: %d\n", i,
                  solverRowNum, _compSolver.number(SPxColId(_idsDualColIDs[i])),
                  _realLP->number(SPxRowId(_idsPrimalRowIDs[i])), reducedProbDual, compProbPrimal,
                  _solver.rowType(solverRowNum));
#endif

            // the translation of the complementary primal problem to the dual some rows resulted in two columns.
            if( i < _nPrimalRows - 1 &&
                  _realLP->number(SPxRowId(_idsPrimalRowIDs[i])) == _realLP->number(SPxRowId(_idsPrimalRowIDs[i + 1])) )
            {
               i++;
               // @todo make sure that this is just a simple sum
               compProbPrimal += compPrimalVector[_compSolver.number(SPxColId(_idsDualColIDs[i]))]; // this is u
            }

            // updating the ratio
#if 0
            if( (LE(reducedProbDual, 0, 1e-10) && LE(compProbPrimal, 0, 1e-10)) ||
                  (GE(reducedProbDual, 0, 1e-10) && GE(compProbPrimal, 0, 1e-10)) )
#endif
            if( GE(compProbPrimal, 0, 1e-10) )
            {
               dualRatio = infinity;
            }
            else if( _solver.rowType(solverRowNum) != LPRowBase<Real>::EQUAL )
            {
               dualRatio = reducedProbDual/compProbPrimal;
            }

            if( dualRatio < maxDualRatio )
               maxDualRatio = dualRatio;
         }
         else
            compProbPrimal = compPrimalVector[_compSolver.number(SPxColId(_idsDualColIDs[i]))]; // this is v

      }

      printf("maxDualRatio: %f\n", maxDualRatio);

#if 0
      for( int i = 0; i < _nPrimalCols; i++ )
      {
         Real reducedProbDual = 0;
         Real compProbPrimal = 0;
         Real dualRatio = 0;
         int colNumber = _realLP->number(SPxColId(_idsPrimalColIDs[i]));
         assert(colNumber >= 0 || (colNumber == -1 && _idsPrimalColIDs[i].getIdx() == _compSlackColId.getIdx()));
         if( colNumber >= 0 && _fixedOrigVars[colNumber] != 0 )
         {
            assert(_fixedOrigVars[colNumber] == -1 || _fixedOrigVars[colNumber] == 1);
            assert(_solver.number(_idsReducedProbColIDs[colNumber]) >= 0);
            assert(_compSolver.number(_idsFixedVarDualIDs[colNumber]) >= 0);

            reducedProbDual = -1*redcostVector[_solver.number(_idsReducedProbColIDs[colNumber])]; // this is y
            compProbPrimal = compPrimalVector[_compSolver.number(_idsFixedVarDualIDs[colNumber])]; // this is u

            printf("%d %d %d %f %f\n", i, colNumber, _fixedOrigVars[colNumber], reducedProbDual, compProbPrimal);

            assert(_realLP->number(SPxColId(_idsPrimalColIDs[i])) != _realLP->number(SPxColId(_idsPrimalColIDs[i + 1])));

            // updating the ratio
            if( LE(compProbPrimal, 0, 1e-10) )
               dualRatio = infinity;
            else
               dualRatio = reducedProbDual/compProbPrimal;

            if( dualRatio < maxDualRatio )
               maxDualRatio = dualRatio;
         }
      }

      printf("maxDualRatio: %f\n", maxDualRatio);
#endif

      LPRowSet updaterows;

      int* newrowidx = 0;
      int nnewrowidx = 0;
      int currnumcols = _solver.nCols();
      spx_alloc(newrowidx, _nPrimalRows);
      for( int i = 0; i < _nPrimalRows; i++ )
      {
         LPRowReal origlprow;
         DSVectorBase<Real> rowtoaddVec(_realLP->nCols());
         Real compProbPrimal = 0;
         int rownumber = _realLP->number(SPxRowId(_idsPrimalRowIDs[i]));
         if( !_idsReducedProbRows[_realLP->number(SPxRowId(_idsPrimalRowIDs[i]))] )
         {
            // retreiving the complementary problem primal solutions
            compProbPrimal = compPrimalVector[_compSolver.number(SPxColId(_idsDualColIDs[i]))]; // this is u

            // the translation of the complementary primal problem to the dual some rows resulted in two columns.
            if( i < _nPrimalRows - 1 &&
                  _realLP->number(SPxRowId(_idsPrimalRowIDs[i])) == _realLP->number(SPxRowId(_idsPrimalRowIDs[i + 1])) )
            {
               i++;
               // @todo make sure that this is just a simple sum
               compProbPrimal += compPrimalVector[_compSolver.number(SPxColId(_idsDualColIDs[i]))]; // this is u
            }

            // add row to the reduced problem the computed dual is positive
            // @todo Check whether this should be a >= or a strict >. This will have to be tested on degenerate
            // instances.
            //if( GT(compProbPrimal*maxDualRatio, 0, 1e-10) ||
                  //(isZero(maxDualRatio, 1e-10) && GT(compProbPrimal, 0, 1e-10)) )
            int compRowNum = _compSolver.number(SPxColId(_idsDualColIDs[i]));
            if( !isZero(compProbPrimal, 1e-10) || GT(compProbPrimal*maxDualRatio, 0, 1e-10)
                  /*|| (//isZero(objValue, 1e-10) &&
                   * _compSolver.basis().desc().rowStatus(compRowNum) > SPxBasis::Desc::D_FREE &&
                     _compSolver.basis().desc().rowStatus(compRowNum) < SPxBasis::Desc::D_UNDEFINED)*/ )
            {
               printf("Adding rows!!!\n");
               LPColSet additionalcols;
               int* newcolidx = 0;
               int nnewcolidx = 0;
               spx_alloc(newcolidx, _realLP->nCols());

               int* changeElementCol = 0;
               int* changeElementRow = 0;
               double* changeElementVal = 0;
               int nChangeElement = 0;
               spx_alloc(changeElementCol, _realLP->nCols());
               spx_alloc(changeElementRow, _realLP->nCols());
               spx_alloc(changeElementVal, _realLP->nCols());

#if 0
               _realLP->getRow(_realLP->number(SPxRowId(_idsPrimalRowIDs[i])), origlprow);
               for( int j = 0; j < origlprow.rowVector().size(); j++ )
               {
                  printf("%d %d\n", j, _solver.number(_idsReducedProbColIDs[origlprow.rowVector().index(j)]));
                  // the column of the new row may not exist in the current reduced problem.
                  // if the column does not exist, then it is necessary to create the column.
                  int colnumber = origlprow.rowVector().index(j);
                  if( !_idsReducedProbCols[colnumber] )
                  {
                     printf("(%d), %d obj: %f, lb: %f, ub: %f\n", currnumcols, colnumber, _realLP->maxObj(colnumber),
                           _realLP->lower(colnumber), _realLP->upper(colnumber));
                     assert(!_idsReducedProbColIDs[colnumber].isValid());
                     additionalcols.create(1, _realLP->maxObj(colnumber), _realLP->lower(colnumber),
                           _realLP->upper(colnumber));

                     rowtoaddVec.add(currnumcols, origlprow.rowVector().value(j));
                     _idsReducedProbCols[colnumber] = true;
                     currnumcols++;

                     newcolidx[nnewcolidx] = colnumber;
                     nnewcolidx++;

                     // Checking the non-zeros of the column
                     LPColReal addedCol;
                     _realLP->getCol(colnumber, addedCol);
                     for( int k = 0; k < addedCol.colVector().size(); k++ )
                     {
                        if( _idsReducedProbRows[addedCol.colVector().index(k)] )
                        {
                           printf("=========================================== Found extra entry!!!!\n");
                           changeElementCol[nChangeElement] = currnumcols - 1;
                           changeElementRow[nChangeElement] =
                              _solver.number(_idsReducedProbRowIDs[addedCol.colVector().index(k)]);
                           changeElementVal[nChangeElement] = addedCol.colVector().value(k);
                           nChangeElement++;
                        }
                     }
                  }
                  else
                     rowtoaddVec.add(_solver.number(_idsReducedProbColIDs[origlprow.rowVector().index(j)]),
                           origlprow.rowVector().value(j));
               }

               updaterows.add(origlprow.lhs(), rowtoaddVec, origlprow.rhs());
#endif
               updaterows.add(transformedRows.lhs(rownumber), transformedRows.rowVector(rownumber),
                     transformedRows.rhs(rownumber));

               _idsReducedProbRows[_realLP->number(SPxRowId(_idsPrimalRowIDs[i]))] = true;
               newrowidx[nnewrowidx] = _realLP->number(SPxRowId(_idsPrimalRowIDs[i]));
               nnewrowidx++;


               SPxColId* addedcolids = 0;
               spx_alloc(addedcolids, nnewcolidx);
#if 0
               _solver.addCols(addedcolids, additionalcols);

               for( int j = 0; j < nnewcolidx; j++ )
                  _idsReducedProbColIDs[newcolidx[j]] = addedcolids[j];

               for( int j = 0; j < nChangeElement; j++ )
                  _solver.changeElement(changeElementRow[j], changeElementCol[j], changeElementVal[j]);
#endif

               // freeing allocated memory
               spx_free(addedcolids);
               spx_free(changeElementVal);
               spx_free(changeElementRow);
               spx_free(changeElementCol);
               spx_free(newcolidx);
            }
         }
      }


      SPxRowId* addedrowids = 0;
      spx_alloc(addedrowids, nnewrowidx);
      _solver.addRows(addedrowids, updaterows);

      for( int i = 0; i < nnewrowidx; i++ )
         _idsReducedProbRowIDs[newrowidx[i]] = addedrowids[i];

      // freeing allocated memory
      spx_free(addedrowids);
      spx_free(newrowidx);
   }




   // This function assumes that the basis is in the row form.
   // @todo extend this to the case when the basis is in the column form.
   //
   // NOTE: Changing "nonposind[*nnonposind] = bind[i]" to "nonposind[*nnonposind] = i"
   void SoPlex::_getNonPositiveDualMultiplierInds(Vector feasVector, int* nonposind, int* bind, int* colsforremoval,
         int* nnonposind)
   {
      assert(_solver.rep() == SPxSolver::ROW);
      bool delCol;

      _idsReducedProbColIDs.reSize(numColsReal());
      *nnonposind = 0;

      // iterating over all columns in the basis matrix
      // this identifies the basis indices and the indicies that are positive.
      for( int i = 0; i < _solver.nCols(); ++i ) // @todo Check the use of numColsReal for the reduced problem.
      {
         printf("%d %d %f ", i, _solver.number(_solver.basis().baseId(i)), feasVector[i]);
         _idsReducedProbCols[i] = true;
         _idsReducedProbColIDs[i].inValidate();
         colsforremoval[i] = i;
         delCol = false;
         // @todo I have questions about my implementation of this function. I don't think that I am getting the right
         // information. Additionally, in getCompatibleColumns the information may not be used correctly.
         if( _solver.basis().baseId(i).isSPxRowId() ) // find the row id's for rows in the basis
         {
            printf("status: %d, Row\n", int(_solver.basis().desc().rowStatus(_solver.number(_solver.basis().baseId(i)))));
            bind[i] = -1 - _realLP->number(SPxRowId(_solver.basis().baseId(i))); // getting the corresponding row
                                                                                   // for the original LP.

            //@todo need to check this regarding min and max problems
            if( isZero(feasVector[i]) )
            {
               nonposind[*nnonposind] = i;
               (*nnonposind)++;

               // NOTE: commenting out the delCol flag at this point. The colsforremoval array should indicate the
               // columns that have a zero reduced cost. Hence, the delCol flag should only be set in the isSPxColId
               // branch of the if statement.
               delCol = true;
            }
         }
         else if( _solver.basis().baseId(i).isSPxColId() )  // get the column id's for the columns in the basis
         {
            printf("status: %d, Col\n", int(_solver.basis().desc().colStatus(_solver.number(_solver.basis().baseId(i)))));
            bind[i] = _realLP->number(SPxColId(_solver.basis().baseId(i)));

            int colnumber = _solver.number(_solver.basis().baseId(i));
            if( isZero(feasVector[i])/* &&
                   _solver.basis().desc().colStatus(colnumber) != SPxBasis::Desc::P_FIXED*/ )
            {
               //nonposind[*nnonposind] = _solver.number(_solver.basis().baseId(i));
               nonposind[*nnonposind] = i;
               (*nnonposind)++;

               delCol = true;
            }
         }

         // setting an array to identify the columns to be removed from the LP to form the reduced problem
         if( delCol )
         {
            //colsforremoval[_solver.number(_solver.basis().baseId(i))] = -1;
            //_idsReducedProbCols[_solver.number(_solver.basis().baseId(i))] = false;
            colsforremoval[i] = -1;
            _idsReducedProbCols[i] = false;
         }
         else if( _solver.basis().baseId(i).isSPxColId() )
         {
#if 0
            colsforremoval[_solver.number(_solver.basis().baseId(i))] = _solver.number(_solver.basis().baseId(i));
            _idsReducedProbColIDs[_solver.number(_solver.basis().baseId(i))] = SPxColId(_solver.basis().baseId(i));
#endif
#if 1
            if( _solver.basis().baseId(i).isSPxColId() )
               _idsReducedProbColIDs[_solver.number(_solver.basis().baseId(i))] = SPxColId(_solver.basis().baseId(i));
            else
               _idsReducedProbCols[_solver.number(_solver.basis().baseId(i))] = false;
#endif
         }
      }
      printf("\n");
   }



   /// retrieves the compatible columns from the constraint matrix
   // This function also updates the constraint matrix of the reduced problem. It is efficient to perform this in the
   // following function because the required linear algebra has been performed.
   void SoPlex::_getCompatibleColumns(int* nonposind, int* compatind, int* rowsforremoval, int* colsforremoval,
         int nnonposind, int* ncompatind)
   {
      bool compatible;
      SSVector y(numColsReal());
      y.unSetup();

      *ncompatind  = 0;

      _idsReducedProbRowIDs.reSize(numRowsReal());
      _idsReducedProbColRowIDs.reSize(numColsReal());

      for( int i = 0; i < numRowsReal(); ++i )
      {
         rowsforremoval[i] = i;
         _idsReducedProbRows[i] = true;

         // the rhs of this calculation are the rows of the constraint matrix
         // so we are solving y B = A_{i,.}
         try
         {
            _solver.basis().solve(y, _solver.vector(i));
         }
         catch( SPxException E )
         {
            MSG_ERROR( spxout << "Caught exception <" << E.what() << "> while computing compatability.\n" );
         }

         compatible = true;
         // a compatible row is given by zeros in all columns related to the nonpositive indices
         for( int j = 0; j < nnonposind; ++j )
         {
            // @todo really need to check this part of the code. Run through this with Ambros or Matthias.
            // 09.02.2014 getting a tolerance issue with this check. Don't know how to fix it.
            if( !isZero(y[nonposind[j]], 1e-15) )
            {
               compatible = false;
               break;
            }
         }

         // changing the matrix coefficients
         DSVector newRowVector;
         LPRowReal rowtoupdate;

         if( y.isSetup() )
         {
            for( int j = 0; j < y.size(); j++ )
               newRowVector.add(y.index(j), y.value(j));
         }
         else
         {
            for( int j = 0; j < numColsReal(); j++ )
            {
               if( !isZero(y[j]) )
                  newRowVector.add(j, y[j]);
            }
         }

         // transforming the original problem rows
         _solver.getRow(i, rowtoupdate);

         rowtoupdate.setRowVector(newRowVector);
         transformedRows.add(rowtoupdate);

         //compatible = false;

         if( compatible )
         {
            compatind[*ncompatind] = i;
            (*ncompatind)++;

            _idsReducedProbRowIDs[i] = _solver.rowId(i);

            // updating the compatible row
            _idsRedLP->changeRow(i, rowtoupdate);

#if 0
            // checking the columns contained in the removed row to ensure that all are removed.
            LPRowReal lprow;
            _realLP->getRow(i, lprow);
            for( int j = 0; j < lprow.rowVector().size(); j++ )
            {
               // if a column not marked for removal exists in the row, the row remains.
               if( colsforremoval[lprow.rowVector().index(j)] < 0 )
               {
                  colsforremoval[lprow.rowVector().index(j)] = lprow.rowVector().index(j);
                  _idsReducedProbCols[lprow.rowVector().index(j)] = true;
                  _idsReducedProbColIDs[lprow.rowVector().index(j)] = SPxColId(_realLP->cId(lprow.rowVector().index(j)));
               }
            }
#endif
         }
         else
         {
            // setting an array to identify the rows to be removed from the LP to form the reduced problem
            rowsforremoval[i] = -1;
            _idsReducedProbRows[i] = false;
#if 0
            // checking the columns contained in the removed row to ensure that all are removed.
            LPRowReal lprow;
            _realLP->getRow(i, lprow);
            for( int j = 0; j < lprow.rowVector().size(); j++ )
            {
               // if a column not marked for removal exists in the row, the row remains.
               if( colsforremoval[lprow.rowVector().index(j)] >= 0 )
               {
                  rowsforremoval[i] = i;
                  _idsReducedProbRows[i] = true;

                  compatind[*ncompatind] = i;
                  (*ncompatind)++;

                  _idsReducedProbRowIDs[i] = _solver.rowId(i);
                  break;
               }
            }
#endif
         }
      }
   }



   /// computes the reduced problem objective coefficients
   void SoPlex::_computeReducedProbObjCoeff()
   {
      SSVector y(numColsReal());
      y.unSetup();

      // the rhs of this calculation is the original objective coefficient vector
      // so we are solving y B = c
      try
      {
         _solver.basis().solve(y, _solver.maxObj());
      }
      catch( SPxException E )
      {
         MSG_ERROR( spxout << "Caught exception <" << E.what() << "> while computing compatability.\n" );
      }

      transformedObj.reDim(numColsReal());
      if( y.isSetup() )
      {
         int ycount = 0;
         for( int i = 0; i < numColsReal(); i++ )
         {
            if( ycount < y.size() && i == y.index(ycount) )
            {
               transformedObj[i] = y.value(ycount);
               ycount++;
            }
            else
               transformedObj[i] = 0.0;
         }
      }
      else
      {
         for( int i = 0; i < numColsReal(); i++ )
         {
            if( isZero(y[i]) )
               transformedObj[i] = 0.0;
            else
               transformedObj[i] = y[i];
         }
      }

      // setting the updated objective vector
      _idsRedLP->changeObj(transformedObj);
   }



   /// computes the compatible bound constraints and adds them to the reduced problem
   // NOTE: No columns are getting removed from the reduced problem. Only the bound constraints are being removed.
   // So in the reduced problem, all variables are free unless the bound constraints are selected as compatible.
   void SoPlex::_getCompatibleBoundCons(LPRowSet& boundcons, int* compatboundcons, int* nonposind,
         int* ncompatboundcons, int nnonposind)
   {
      bool compatible;
      SSVector y(numColsReal());
      y.unSetup();

      _idsReducedProbColRowIDs.reSize(numColsReal());


      // identifying the compatible bound constraints
      for( int i = 0; i < numColsReal(); i++ )
      {
         _idsReducedProbColRowIDs[i].inValidate();

         // setting all variables to free variables.
         // Bound constraints will only be added to the variables with compatible bound constraints.
         _idsRedLP->changeUpper(i, infinity);
         _idsRedLP->changeLower(i, -infinity);

         // the rhs of this calculation are the unit vectors of the bound constraints
         // so we are solving y B = I_{i,.}

         try
         {
            _solver.basis().solve(y, _solver.unitVector(i));
         }
         catch( SPxException E )
         {
            MSG_ERROR( spxout << "Caught exception <" << E.what() << "> while computing compatability.\n" );
         }

         compatible = true;
         // a compatible row is given by zeros in all columns related to the nonpositive indices
         for( int j = 0; j < nnonposind; ++j )
         {
            // @todo really need to check this part of the code. Run through this with Ambros or Matthias.
            if( !isZero(y[nonposind[j]]) )
            {
               compatible = false;
               break;
            }
         }

         // changing the matrix coefficients
         DSVector newRowVector;
         LPRowReal rowtoupdate;

         if( y.isSetup() )
         {
            for( int j = 0; j < y.size(); j++ )
               newRowVector.add(y.index(j), y.value(j));
         }
         else
         {
            for( int j = 0; j < numColsReal(); j++ )
            {
               if( !isZero(y[j]) )
               {
                  newRowVector.add(j, y[j]);
               }
            }
         }

         // will probably need to map this set of rows.
         transformedRows.add(_solver.lower(i), newRowVector, _solver.upper(i));

         compatible = true;

         // if the bound constraint is compatible, it remains in the reduced problem.
         // Otherwise the bound is removed and the variables are free.
         if( compatible )
         {
            Real lhs = -infinity;
            Real rhs = infinity;
            if( GT(_solver.lower(i), -infinity) )
               lhs = _solver.lower(i);

            if( LT(_solver.upper(i), infinity) )
               rhs = _solver.upper(i);

            if( GT(lhs, -infinity) || LT(rhs, infinity) )
            {
               compatboundcons[(*ncompatboundcons)] = i;
               (*ncompatboundcons)++;

               boundcons.add(lhs, newRowVector, rhs);
            }
         }
      }

   }



   // @todo need to put in a check similar to _isRealLPLoaded for the ids LP.
   void SoPlex::_deleteRowsAndColumnsReducedProblem(int* colsforremoval, int* rowsforremoval)
   {
#ifdef SOLVEIDS_DEBUG
      printf("_deleteRowsAndColumnsReducedProblem\n");
#endif
      assert(_basisStatusRows.size() == numRowsReal());
      _solver.removeRows(rowsforremoval);
      //_solver.removeCols(colsforremoval);


      // removing rows from the solver LP
      // @todo not sure whether the first if statement in this function is valid.
      if( _isRealLPLoaded )
      {
         _hasBasis = (_solver.basis().status() > SPxBasis::NO_PROBLEM);
      }
      else if( _hasBasis )
      {
         for( int i = numRowsReal() - 1; i >= 0 && _hasBasis; i-- )
         {
            if( rowsforremoval[i] < 0 && _basisStatusRows[i] != SPxSolver::BASIC )
               _hasBasis = false;
            else if( rowsforremoval[i] >= 0 && rowsforremoval[i] != i )
            {
               assert(rowsforremoval[i] < numRowsReal());
               assert(rowsforremoval[rowsforremoval[i]] < 0);

               _basisStatusRows[rowsforremoval[i]] = _basisStatusRows[i];
            }
         }

         if( _hasBasis )
            _basisStatusRows.reSize(numRowsReal());
#if 0
         // removing columns from the solver LP
         if( _hasBasis )
         {
            for( int i = numColsReal() - 1; i >= 0 && _hasBasis; i-- )
            {
               if( colsforremoval[i] < 0 && _basisStatusCols[i] == SPxSolver::BASIC )
                  _hasBasis = false;
               else if( colsforremoval[i] >= 0 && colsforremoval[i] != i )
               {
                  assert(colsforremoval[i] < numColsReal());
                  assert(colsforremoval[colsforremoval[i]] < 0);

                  _basisStatusCols[colsforremoval[i]] = _basisStatusCols[i];
               }
            }

            if( _hasBasis )
               _basisStatusCols.reSize(numColsReal());
         }
#endif
      }
   }



   /// computes the rows to remove from the complementary problem
   void SoPlex::_getRowsForRemovalComplementaryProblem(int* nonposind, int* bind, int* rowsforremoval,
         int* nrowsforremoval, int nnonposind)
   {
      *nrowsforremoval = 0;

      for( int i = 0; i < nnonposind; i++ )
      {
         if( bind[nonposind[i]] < 0 )
         {
            rowsforremoval[*nrowsforremoval] = -1 - bind[nonposind[i]];
            (*nrowsforremoval)++;
         }
      }
   }



   /// removing rows from the complementary problem.
   // the rows that are removed from idsCompLP are the rows from the reduced problem that have a non-positive dual
   // multiplier in the optimal solution.
   void SoPlex::_deleteAndUpdateRowsComplementaryProblem(int* initFixedVars)
   {
      int nrowsforremoval = 0;
      int* rowsforremoval = 0;
      DSVector slackColCoeff;
      Real slackCoeff = 1.0;

      spx_alloc(rowsforremoval, _solver.nRows());
      for( int i = 0; i < _solver.nCols(); ++i ) // @todo Check the use of numColsReal for the reduced problem.
      {
         if( !_idsReducedProbColRowIDs[i].isValid() )
            continue;

         int fixedDirection = getOrigVarFixedDirection(i);
         if( fixedDirection != 0 )
         {
            // setting the value of the _fixedOrigVars array to indicate which variables are at their bounds.
            int colNumber = _realLP->number(SPxColId(_solver.cId(i)));
            initFixedVars[colNumber] = fixedDirection;
         }
      }

      for( int i = 0; i < _realLP->nRows(); i++ )
      {
         if( !_idsReducedProbRows[i] )
         {
            switch( _realLP->rowType(i) )
            {
               // NOTE: check the sign of the slackCoeff for the Range constraints. This will depend on the method of
               // dual conversion.
               case LPRowBase<Real>::RANGE:
               case LPRowBase<Real>::GREATER_EQUAL:
                  slackColCoeff.add(i, -slackCoeff);
                  break;
               case LPRowBase<Real>::EQUAL:
               case LPRowBase<Real>::LESS_EQUAL:
                  slackColCoeff.add(i, slackCoeff);
                  break;
               default:
                  throw SPxInternalCodeException("XLPFRD01 This should never happen.");
            }
         }
         else
         {
            // The setting of the bounds will depend on the type of constraint. It is possible
            // that a <= constraint can be at its bound in a minimisation problem, hence it will require the equality to
            // be set to the rhs.
            int rowNum = _solver.number(_idsReducedProbRowIDs[i]);
            if( _solver.basis().desc().rowStatus(rowNum) == SPxBasis::Desc::P_FREE )
            {
               _idsElimPrimalRowIDs[_nElimPrimalRows] = _idsReducedProbRowIDs[i];
               _nElimPrimalRows++;
               rowsforremoval[nrowsforremoval] = i;
               nrowsforremoval++;
            }
            else if( _solver.basis().desc().rowStatus(rowNum) == SPxBasis::Desc::P_ON_UPPER ||
                  _solver.basis().desc().rowStatus(rowNum) == SPxBasis::Desc::P_FIXED )// 07.01.2014 check condition
            {
               //_compSolver.changeLhs(_idsReducedProbRowIDs[i], _solver.rhs(_idsReducedProbRowIDs[i]));
               _compSolver.changeLhs(i, _solver.rhs(_idsReducedProbRowIDs[i]));
            }
            else if( _solver.basis().desc().rowStatus(rowNum) == SPxBasis::Desc::P_ON_LOWER )
            {
               //_compSolver.changeRhs(_idsReducedProbRowIDs[i], _solver.lhs(_idsReducedProbRowIDs[i]));
               _compSolver.changeRhs(i, _solver.lhs(_idsReducedProbRowIDs[i]));
            }
            // NOTE: we are missing the equality constraints.
         }
      }

      // setting the objective coefficients of the original variables to zero
      DVector newObjCoeff(numColsReal());
      for( int i = 0; i < numColsReal(); i++ )
      {
         _compSolver.changeBounds(_realLP->cId(i), Real(-infinity), Real(infinity));
         newObjCoeff[i] = 0;
      }

      _compSolver.changeObj(newObjCoeff);

      // adding the slack column to the complementary problem
      SPxColId* addedcolid = 0;
      spx_alloc(addedcolid, 1);
      LPColSetReal compSlackCol;
      compSlackCol.add(1.0, -infinity, slackColCoeff, infinity);
      _compSolver.addCols(addedcolid, compSlackCol);
      _compSlackColId = addedcolid[0];

      int* perm = 0;
      spx_alloc(perm, numRowsReal());
      _compSolver.removeRows(rowsforremoval, nrowsforremoval, perm);

      // freeing allocated memory
      spx_free(perm);
      spx_free(addedcolid);
      spx_free(rowsforremoval);
   }



   /// update the dual complementary problem with additional columns and rows
   // Given the solution to the updated reduced problem, the complementary problem will be updated with modifications to
   // the constraints and the removal of variables
   void SoPlex::_updateIdsComplementaryProblem(DVector dualVector)
   {
      int prevNumCols = _compSolver.nCols(); // number of columns in the previous formulation of the complementary prob
      int prevNumRows = _compSolver.nRows();
      int prevPrimalRowIds = _nPrimalRows;
      int prevDualColIds = _nDualCols;

      LPColSetBase<Real> addElimCols(_nElimPrimalRows);  // columns previously eliminated from the
                                                         // complementary problem that must be added
      int numElimColsAdded = 0;
      int currnumrows = prevNumRows;
      // looping over all rows from the original LP that were eliminated during the formation of the complementary
      // problem. The eliminated rows will be added if they are basic in the reduced problem.
      for( int i = 0; i < _nElimPrimalRows; i++ )
      {
         int rowNumber = _realLP->number(_idsElimPrimalRowIDs[i]);
         if( _solver.isBasic(_idsReducedProbRowIDs[rowNumber]) )
         {
            /*
             * =================================================
             * This is not correct!!! The addition of rows does not take into account the different columns contained in
             * the complementary problem compared to the original problem.
             * MUST FIX!!!!!
             * 07.01.2014 - I think that this is fixed. Still need to check.
             * =================================================
             */
            int solverRowNum = _solver.number(_idsReducedProbRowIDs[rowNumber]);
            if( _solver.basis().desc().rowStatus(solverRowNum) == SPxBasis::Desc::P_ON_UPPER ||
              _solver.basis().desc().rowStatus(solverRowNum) == SPxBasis::Desc::P_ON_LOWER ||
              _solver.basis().desc().rowStatus(solverRowNum) == SPxBasis::Desc::P_FIXED )
            {
               // this assert should stay, but there is an issue with the status and the dual vector
               //assert(isNotZero(dualVector[_solver.number(_idsReducedProbRowIDs[rowNumber])]));
               LPRowReal origlprow;
               DSVectorBase<Real> coltoaddVec(_realLP->nCols());

               LPRowSet additionalrows;
               int nnewrows = 0;

               _realLP->getRow(rowNumber, origlprow);
               for( int j = 0; j < origlprow.rowVector().size(); j++ )
               {
                  // the column of the new row may not exist in the current complementary problem.
                  // if the column does not exist, then it is necessary to create the column.
                  int colNumber = origlprow.rowVector().index(j);
                  if( _idsCompProbColIDsIdx[colNumber] == -1 )
                  {
                     assert(!_idsReducedProbColIDs[colNumber].isValid());
                     _idsPrimalColIDs[_nPrimalCols] = _realLP->cId(colNumber);
                     _idsCompProbColIDsIdx[colNumber] = _nPrimalCols;
                     _fixedOrigVars[colNumber] = -2;
                     _nPrimalCols++;

                     // all columns for the complementary problem are converted to unrestricted.
                     additionalrows.create(1, _realLP->maxObj(colNumber), _realLP->maxObj(colNumber));
                     nnewrows++;

                     coltoaddVec.add(currnumrows, origlprow.rowVector().value(j));
                     currnumrows++;
                  }
                  else
                     coltoaddVec.add(_compSolver.number(_idsDualRowIDs[_idsCompProbColIDsIdx[colNumber]]),
                           origlprow.rowVector().value(j));
               }

               SPxRowId* addedrowids = 0;
               spx_alloc(addedrowids, nnewrows);
               _compSolver.addRows(addedrowids, additionalrows);

               for( int j = 0; j < nnewrows; j++ )
               {
                  _idsDualRowIDs[_nDualRows] = addedrowids[j];
                  _nDualRows++;
               }

               spx_free(addedrowids);



               if( _solver.basis().desc().rowStatus(solverRowNum) == SPxBasis::Desc::P_ON_UPPER ||
                     _solver.basis().desc().rowStatus(solverRowNum) == SPxBasis::Desc::P_FIXED )
               {
                  // this assert should stay, but there is an issue with the status and the dual vector
                  //assert(GT(dualVector[_solver.number(_idsReducedProbRowIDs[rowNumber])], 0.0));
                  // NOTE: This will probably need the SPxRowId passed to the function to get the new row id.
                  assert(LT(_realLP->rhs(_idsElimPrimalRowIDs[i]), infinity));
                  addElimCols.add(_realLP->rhs(_idsElimPrimalRowIDs[i]), -infinity, coltoaddVec, infinity);

                  _idsPrimalRowIDs[_nPrimalRows] = _idsElimPrimalRowIDs[i];
                  _idsCompProbRowIDsIdx[_realLP->number(_idsPrimalRowIDs[_nPrimalRows])] = _nPrimalRows;
                  _nPrimalRows++;

                  _idsElimPrimalRowIDs.remove(i);
                  _nElimPrimalRows--;
                  i--;

                  numElimColsAdded++;
               }
               else if( _solver.basis().desc().rowStatus(solverRowNum) == SPxBasis::Desc::P_ON_LOWER )
               {
                  // this assert should stay, but there is an issue with the status and the dual vector
                  //assert(LT(dualVector[_solver.number(_idsReducedProbRowIDs[rowNumber])], 0.0));
                  assert(GT(_realLP->lhs(_idsElimPrimalRowIDs[i]), -infinity));
                  addElimCols.add(_realLP->lhs(_idsElimPrimalRowIDs[i]), -infinity, coltoaddVec, infinity);

                  _idsPrimalRowIDs[_nPrimalRows] = _idsElimPrimalRowIDs[i];
                  _idsCompProbRowIDsIdx[_realLP->number(SPxColId(_idsPrimalRowIDs[_nPrimalRows]))] = _nPrimalRows;
                  _nPrimalRows++;

                  _idsElimPrimalRowIDs.remove(i);
                  _nElimPrimalRows--;
                  i--;

                  numElimColsAdded++;
               }
            }
         }
      }

      printf("Num added cols: %d\n", numElimColsAdded);

      // updating the _idsDualColIDs with the additional columns from the eliminated rows.
      _compSolver.addCols(addElimCols);
      for( int i = prevNumCols; i < _compSolver.nCols(); i++ )
         _idsDualColIDs[prevDualColIds + i - prevNumCols] = _compSolver.colId(i);

      _nDualCols = _nPrimalRows;

      // looping over all rows from the original problem that were originally contained in the complementary problem.
      // The basic rows will be set as free variables, the non-basic rows will be eliminated from the complementary
      // problem.
      DSVector slackRowCoeff(_compSolver.nCols());
      Real slackCoeff = 1.0;

      int* colsforremoval = 0;
      int ncolsforremoval = 0;
      spx_alloc(colsforremoval, prevPrimalRowIds);
      for( int i = 0; i < prevPrimalRowIds; i++ )
      {
         int rowNumber = _realLP->number(_idsPrimalRowIDs[i]);
         if( _idsReducedProbRows[rowNumber] )
         {
            // rows added to the reduced problem may have been equality constriants. The equality constraints from the
            // original problem are converted into <= and >= constraints. Upon adding these constraints to the reduced
            // problem, only a single dual column is needed in the complementary problem. Hence, one of the dual columns
            // is removed.
            if( i + 1 < prevPrimalRowIds
                  && _realLP->number(_idsPrimalRowIDs[i]) == _realLP->number(_idsPrimalRowIDs[i+1]) )
            {
               colsforremoval[ncolsforremoval] = _compSolver.number(SPxColId(_idsDualColIDs[i + 1]));
               ncolsforremoval++;

               _idsPrimalRowIDs.remove(i + 1);
               _nPrimalRows--;
               _idsDualColIDs.remove(i + 1);
               _nDualCols--;

               prevPrimalRowIds--;
            }

            int solverRowNum = _solver.number(_idsReducedProbRowIDs[rowNumber]);
            assert(solverRowNum >= 0);
            if( _solver.basis().desc().rowStatus(solverRowNum) == SPxBasis::Desc::P_ON_UPPER ||
                  _solver.basis().desc().rowStatus(solverRowNum) == SPxBasis::Desc::P_FIXED)
            {
               //assert(GT(dualVector[solverRowNum], 0.0));
               _compSolver.changeObj(_idsDualColIDs[i], _realLP->rhs(SPxRowId(_idsPrimalRowIDs[i])));
               _compSolver.changeBounds(_idsDualColIDs[i], -infinity, infinity);
            }
            else if( _solver.basis().desc().rowStatus(solverRowNum) == SPxBasis::Desc::P_ON_LOWER )
            {
               //assert(LT(dualVector[solverRowNum], 0.0));
               _compSolver.changeObj(_idsDualColIDs[i], _realLP->lhs(SPxRowId(_idsPrimalRowIDs[i])));
               _compSolver.changeBounds(_idsDualColIDs[i], -infinity, infinity);
            }
            else if ( _solver.basis().desc().rowStatus(solverRowNum) != SPxBasis::Desc::P_FIXED )
            {
               //assert(isZero(dualVector[solverRowNum], 0.0));

               colsforremoval[ncolsforremoval] = _compSolver.number(SPxColId(_idsDualColIDs[i]));
               ncolsforremoval++;

               _idsElimPrimalRowIDs[_nElimPrimalRows] = _idsPrimalRowIDs[i];
               _nElimPrimalRows++;
               _idsCompProbRowIDsIdx[_realLP->number(_idsPrimalRowIDs[i])] = -1;
               _idsPrimalRowIDs.remove(i);
               _nPrimalRows--;
               _idsDualColIDs.remove(i);
               _nDualCols--;

               i--;
               prevPrimalRowIds--;
            }
         }
         else
         {
            switch( _realLP->rowType(_idsPrimalRowIDs[i]) )
            {
               case LPRowBase<Real>::RANGE:
                  assert(_realLP->number(SPxColId(_idsPrimalRowIDs[i])) ==
                        _realLP->number(SPxColId(_idsPrimalRowIDs[i+1])));
                  if( _compSolver.obj(_compSolver.number(SPxColId(_idsDualColIDs[i]))) <
                        _compSolver.obj(_compSolver.number(SPxColId(_idsDualColIDs[i + 1]))))
                  {
                     slackRowCoeff.add(_compSolver.number(SPxColId(_idsDualColIDs[i])), -slackCoeff);
                     slackRowCoeff.add(_compSolver.number(SPxColId(_idsDualColIDs[i + 1])), slackCoeff);
                  }
                  else
                  {
                     slackRowCoeff.add(_compSolver.number(SPxColId(_idsDualColIDs[i])), slackCoeff);
                     slackRowCoeff.add(_compSolver.number(SPxColId(_idsDualColIDs[i + 1])), -slackCoeff);
                  }
                  i++;
                  break;
               case LPRowBase<Real>::EQUAL:
                  assert(_realLP->number(SPxColId(_idsPrimalRowIDs[i])) ==
                        _realLP->number(SPxColId(_idsPrimalRowIDs[i+1])));

                  slackRowCoeff.add(_compSolver.number(SPxColId(_idsDualColIDs[i])), slackCoeff);
                  slackRowCoeff.add(_compSolver.number(SPxColId(_idsDualColIDs[i + 1])), slackCoeff);

                  i++;
                  break;
               case LPRowBase<Real>::GREATER_EQUAL:
                  slackRowCoeff.add(_compSolver.number(SPxColId(_idsDualColIDs[i])), -slackCoeff);
                  break;
               case LPRowBase<Real>::LESS_EQUAL:
                  slackRowCoeff.add(_compSolver.number(SPxColId(_idsDualColIDs[i])), slackCoeff);
                  break;
               default:
                  throw SPxInternalCodeException("XLPFRD01 This should never happen.");
            }
         }
      }

      // updating the slack column in the complementary problem
      LPRowBase<Real> compSlackRow(1.0, slackRowCoeff, 1.0);
      _compSolver.changeRow(_compSlackDualRowId, compSlackRow);


      int* perm = 0;
      spx_alloc(perm, _compSolver.nCols() + numElimColsAdded);
      _compSolver.removeCols(colsforremoval, ncolsforremoval, perm);

      // updating the dual columns to represent the fixed primal variables.
      int* currFixedVars = 0;
      spx_alloc(currFixedVars, _realLP->nCols());
      _identifyComplementaryFixedPrimalVars(currFixedVars);
      _removeComplementaryFixedPrimalVars(currFixedVars);
#ifdef SOLVEIDS_DEBUG
      printf("Writing the complementary problem after column removal\n");
      _compSolver.writeFile("comp_colremove.lp");
#endif
      _updateComplementaryFixedPrimalVars(currFixedVars);


      // freeing allocated memory
      spx_free(currFixedVars);
      spx_free(perm);
      spx_free(colsforremoval);
   }



   /// checking the optimality of the original problem.
   // this function is called if the complementary problem is solved with a non-negative objective value. This implies
   // that the rows currently included in the reduced problem are sufficient to identify the optimal solution to the
   // original problem.
   void SoPlex::_checkOriginalProblemOptimality()
   {

   }



   /// updating the slack column coefficients to adjust for equality constraints
   void SoPlex::_updateComplementarySlackColCoeff()
   {
      Real slackCoeff = 1.0;

      // the slack column for the equality constraints is not handled correctly in the dual conversion. Hence, it is
      // necessary to change the equality coefficients of the dual row related to the slack column.
      for( int i = 0; i < _nPrimalRows; i++ )
      {
         int rowNumber = _realLP->number(SPxRowId(_idsPrimalRowIDs[i]));
         if( !_idsReducedProbRows[rowNumber] && _realLP->rowType(_idsPrimalRowIDs[i]) == LPRowBase<Real>::EQUAL )
         {
            assert(_realLP->lhs(_idsPrimalRowIDs[i]) == _realLP->rhs(_idsPrimalRowIDs[i]));
            _compSolver.changeLower(_idsDualColIDs[i], 0.0);   // setting the lower bound of the dual column to zero.

            LPColBase<Real> addEqualityCol(-1.0*_realLP->rhs(_idsPrimalRowIDs[i]),
                  -1.0*_compSolver.colVector(_idsDualColIDs[i]), infinity, 0.0);    // adding a new column to the dual

            SPxColId newDualCol;
            _compSolver.addCol(newDualCol, addEqualityCol);

            // inserting the row and col ids for the added column. This is to be next to the original column that has
            // been duplicated.
            _idsPrimalRowIDs.insert(i + 1, 1, _idsPrimalRowIDs[i]);
            _idsDualColIDs.insert(i + 1, 1, newDualCol);
            assert(_realLP->number(_idsPrimalRowIDs[i]) == _realLP->number(_idsPrimalRowIDs[i + 1]));

            // updating the slack variable dual row.
            _compSolver.changeElement(_compSlackDualRowId, _idsDualColIDs[i], slackCoeff);
            _compSolver.changeElement(_compSlackDualRowId, _idsDualColIDs[i + 1], slackCoeff);

            i++;
            _nPrimalRows++;
            _nDualCols++;
         }
      }
   }



   /// identify the dual columns related to the fixed variables
   void SoPlex::_identifyComplementaryFixedPrimalVars(int* currFixedVars)
   {
      int numFixedVar = 0;
      for( int i = 0; i < _realLP->nCols(); i++ )
      {
         currFixedVars[i] = 0;
         if( !_idsReducedProbColRowIDs[i].isValid() )
            continue;

         int rowNumber = _solver.number(_idsReducedProbColRowIDs[i]);
         if( _idsReducedProbColRowIDs[i].isValid() &&
              (_solver.basis().desc().rowStatus(rowNumber) == SPxBasis::Desc::P_ON_UPPER ||
               _solver.basis().desc().rowStatus(rowNumber) == SPxBasis::Desc::P_ON_LOWER ||
               _solver.basis().desc().rowStatus(rowNumber) == SPxBasis::Desc::P_FIXED) )
         {
            // setting the value of the _fixedOrigVars array to indicate which variables are at their bounds.
            currFixedVars[i] = getOrigVarFixedDirection(i);

            numFixedVar++;
         }
      }

      printf("Number of fixed variables: %d\n", numFixedVar);
   }



   /// removing the dual columns related to the fixed variables
   void SoPlex::_removeComplementaryFixedPrimalVars(int* currFixedVars)
   {
      SPxColId tempId;
      int ncolsforremoval = 0;
      int* colsforremoval = 0;
      spx_alloc(colsforremoval, _realLP->nCols()*2);
      for( int i = 0; i < _realLP->nCols(); i++ )
      {
         if( _idsCompProbColIDsIdx[i] != -1 )//&& _fixedOrigVars[i] != currFixedVars[i] )
         {
            if( _fixedOrigVars[i] != 0 )
            {
               assert(_compSolver.number(SPxColId(_idsFixedVarDualIDs[i])) >= 0);
               assert(_fixedOrigVars[i] == -1 || _fixedOrigVars[i] == 1);

               colsforremoval[ncolsforremoval] = _compSolver.number(SPxColId(_idsFixedVarDualIDs[i]));
               ncolsforremoval++;

               _idsFixedVarDualIDs[i] = tempId;
            }
            else
            {
               assert((LE(_realLP->lower(i), -infinity) && GE(_realLP->upper(i), infinity)) ||
                     _compSolver.number(SPxColId(_idsVarBoundDualIDs[i*2])) >= 0);
               int varcount = 0;
               if( GT(_realLP->lower(i), -infinity) )
               {
                  colsforremoval[ncolsforremoval] = _compSolver.number(SPxColId(_idsVarBoundDualIDs[i*2 + varcount]));
                  ncolsforremoval++;

                  _idsVarBoundDualIDs[i*2 + varcount] = tempId;
                  varcount++;
               }

               if( LT(_realLP->upper(i), infinity) )
               {
                  colsforremoval[ncolsforremoval] = _compSolver.number(SPxColId(_idsVarBoundDualIDs[i*2 + varcount]));
                  ncolsforremoval++;

                  _idsVarBoundDualIDs[i*2 + varcount] = tempId;
               }

            }
         }
      }


      int* perm = 0;
      spx_alloc(perm, _compSolver.nCols());
      _compSolver.removeCols(colsforremoval, ncolsforremoval, perm);

      // freeing allocated memory
      spx_free(perm);
      spx_free(colsforremoval);
   }



   /// updating the dual columns related to the fixed primal variables.
   void SoPlex::_updateComplementaryFixedPrimalVars(int* currFixedVars)
   {
      Real slackCoeff = 1.0;
      DSVectorBase<Real> col(1);
      LPColSetBase<Real> boundConsCols;
      LPColSetBase<Real> fixedVarsDualCols(_nPrimalCols);
      int numFixedVars = 0;
      // the solution to the reduced problem results in a number of variables at their bounds. If such variables exist
      // it is necessary to include a dual column to the complementary problem related to a variable fixing. This is
      // equivalent to the tight constraints being converted to equality constraints.
      int numBoundConsCols = 0;
      int* boundConsColsAdded = 0;
      spx_alloc(boundConsColsAdded, _realLP->nCols());
      // NOTE: this loop only goes over the primal columns that are included in the complementary problem, i.e. the
      // columns from the original problem. I am not too sure what happens when a new constraint is added to the
      // complementary problem inducing additional columns. Must check!!!
      for( int i = 0; i < _realLP->nCols(); i++ )
      {
         boundConsColsAdded[i] = 0;
         if( _idsCompProbColIDsIdx[i] != -1 )
         {
            if(true)//( _fixedOrigVars[i] != currFixedVars[i] )
            {
               printf("_fixedOrigVars[%d]: %d, currFixedVars[%d]: %d\n", i, _fixedOrigVars[i], i, currFixedVars[i]);
               int idIndex = _idsCompProbColIDsIdx[i];
               assert(_compSolver.number(SPxRowId(_idsDualRowIDs[idIndex])) >= 0);
               col.add(_compSolver.number(SPxRowId(_idsDualRowIDs[idIndex])), 1.0);
               if( currFixedVars[i] != 0 )
               {
                  assert(currFixedVars[i] == -1 || currFixedVars[i] == 1);

                  Real colObjCoeff = 0;
                  if( currFixedVars[i] == -1 )
                     colObjCoeff = _solver.lhs(_idsReducedProbColRowIDs[i]);
                  else
                     colObjCoeff = _solver.rhs(_idsReducedProbColRowIDs[i]);

                  fixedVarsDualCols.add(colObjCoeff, -infinity, col, infinity);
                  numFixedVars++;
               }
               // 09.02.14 I think that the else should only be entered if the column does not exist in the reduced
               // prob. I have tested by just leaving this as an else (without the if), but I think that this is wrong.
               else //if( !_idsReducedProbColRowIDs[i].isValid() )
               {
                  bool isRedProbCol = _idsReducedProbColRowIDs[i].isValid();
                  if( GT(_realLP->lower(i), -infinity) )
                  {
                     if( !isRedProbCol )
                        col.add(_compSolver.number(SPxRowId(_compSlackDualRowId)), -slackCoeff);
                     boundConsCols.add(_realLP->lower(i), Real(-infinity), col, 0.0);

                     if( !isRedProbCol )
                        col.remove(col.size() - 1);

                     boundConsColsAdded[i]++;
                     numBoundConsCols++;
                  }

                  if( LT(_realLP->upper(i), infinity) )
                  {
                     if( !isRedProbCol )
                        col.add(_compSolver.number(SPxRowId(_compSlackDualRowId)), slackCoeff);
                     boundConsCols.add(_realLP->upper(i), 0.0, col, Real(infinity));

                     if( !isRedProbCol )
                        col.remove(col.size() - 1);

                     boundConsColsAdded[i]++;
                     numBoundConsCols++;
                  }
               }
               col.clear();
            }
#if 0
            else if( currFixedVars[i] == 0 )
            {
               int varcount = 0;
               if( GT(_realLP->lower(i), -infinity) )
               {
                  _compSolver.changeElement(_compSlackDualRowId, _idsVarBoundDualIDs[i*2 + varcount], -slackCoeff);
                  varcount++;
               }

               if( LT(_realLP->upper(i), infinity) )
                  _compSolver.changeElement(_compSlackDualRowId, _idsVarBoundDualIDs[i*2 + varcount], slackCoeff);
            }
#endif
            _fixedOrigVars[i] = currFixedVars[i];
         }
      }

      // adding the fixed var dual columns to the complementary problem
      SPxColId* addedcolids = 0;
      spx_alloc(addedcolids, numFixedVars);
      _compSolver.addCols(addedcolids, fixedVarsDualCols);

      SPxColId tempId;
      int addedcolcount = 0;
      for( int i = 0; i < _realLP->nCols(); i++ )
      {
         if( _fixedOrigVars[i] != 0 )
         {
            assert(_fixedOrigVars[i] == -1 || _fixedOrigVars[i] == 1);
            _idsFixedVarDualIDs[i] = addedcolids[addedcolcount];
            addedcolcount++;
         }
         else
            _idsFixedVarDualIDs[i] = tempId;
      }

      // adding the bound cons dual columns to the complementary problem
      SPxColId* addedbndcolids = 0;
      spx_alloc(addedbndcolids, numBoundConsCols);
      _compSolver.addCols(addedbndcolids, boundConsCols);

      addedcolcount = 0;
      for( int i = 0; i < _realLP->nCols(); i++ )
      {
         if( boundConsColsAdded[i] > 0 )
         {
            for( int j = 0; j < boundConsColsAdded[i]; j++ )
            {
               _idsVarBoundDualIDs[i*2 + j] = addedbndcolids[addedcolcount];
               addedcolcount++;
            }
         }

         switch( boundConsColsAdded[i] )
         {
            case 0:
               _idsVarBoundDualIDs[i*2] = tempId;
            case 1:
               _idsVarBoundDualIDs[i*2 + 1] = tempId;
               break;
         }
      }

      // freeing allocated memory
      spx_free(addedbndcolids);
      spx_free(addedcolids);
      spx_free(boundConsColsAdded);
   }


   /// determining which bound the primal variables will be fixed to.
   int SoPlex::getOrigVarFixedDirection(int colNum)
   {
      if( !_idsReducedProbColRowIDs[colNum].isValid() )
         return 0;

      int rowNumber = _solver.number(_idsReducedProbColRowIDs[colNum]);
      // setting the value of the _fixedOrigVars array to indicate which variables are at their bounds.
      if( _solver.basis().desc().rowStatus(rowNumber) == SPxBasis::Desc::P_ON_UPPER ||
       _solver.basis().desc().rowStatus(rowNumber) == SPxBasis::Desc::P_FIXED )
      {
         assert(_solver.rhs(rowNumber) < infinity);
         return 1;
      }
      else if( _solver.basis().desc().rowStatus(rowNumber) == SPxBasis::Desc::P_ON_LOWER )
      {
         assert(_solver.lhs(rowNumber) > -infinity);
         return -1;
      }

      return 0;
   }



   // @todo update this function and related comments. It has only been hacked together.
   /// checks result of the solving process and solves again without preprocessing if necessary
   // @todo need to evaluate the solution to ensure that it is solved to optimality and then we are able to perform the
   // next steps in the algorithm.
   void SoPlex::_evaluateSolutionIDS()
   {
      // process result
      switch( _status )
      {
      case SPxSolver::OPTIMAL:
         if( !_isRealLPLoaded )
         {
            MSG_INFO1( spxout << " --- updating the basis partitioning" << std::endl; )
            // Need to solve the complementary problem
            return;
         }
         else
            _hasBasis = true;
         break;

      case SPxSolver::UNBOUNDED:
      case SPxSolver::INFEASIBLE:
      case SPxSolver::INForUNBD:
      case SPxSolver::SINGULAR:
      case SPxSolver::ABORT_CYCLING:
         // when solving the reduced problem it should not be possible that the problem is infeasible or unbounded. This
         // result is a check to make sure that the partitioning is correct.
         if( !_isRealLPLoaded )
         {
            // in this situation the original problem should be solved again.
            _solver.changeObjOffset(0.0); // NOTE: What is this????
            // build in some recourse to quit the IDS if the reduced problem is infeasible or unbounded
            return;
         }
         else
            _hasBasis = (_solver.basis().status() > SPxBasis::NO_PROBLEM);
         break;

         // else fallthrough
      case SPxSolver::ABORT_TIME:
      case SPxSolver::ABORT_ITER:
      case SPxSolver::ABORT_VALUE:
      case SPxSolver::REGULAR:
      case SPxSolver::RUNNING:
         if( _isRealLPLoaded )
            _hasBasis = (_solver.basis().status() > SPxBasis::NO_PROBLEM);
         // store regular basis if there is no simplifier and the original problem is not in the solver because of
         // scaling; non-optimal bases should currently not be unsimplified
         else if( _simplifier == 0 && _solver.basis().status() > SPxBasis::NO_PROBLEM )
         {
            _basisStatusRows.reSize(numRowsReal());
            _basisStatusCols.reSize(numColsReal());
            assert(_basisStatusRows.size() == _solver.nRows());
            assert(_basisStatusCols.size() == _solver.nCols());

            _solver.getBasis(_basisStatusRows.get_ptr(), _basisStatusCols.get_ptr());
            _hasBasis = true;
         }
         else
            _hasBasis = false;
         break;

      default:
         _hasBasis = false;
         break;
      }
   }




   /// checks the dual feasibility of the current basis
   bool SoPlex::checkBasisDualFeasibility(Vector feasVec)
   {
      Real feastol = realParam(SoPlex::FEASTOL);
      // iterating through all elements in the basis to determine whether the dual feasibility is satisfied.
      for( int i = 0; i < _solver.nCols(); i++ )
      {
         if( _solver.basis().baseId(i).isSPxRowId() ) // find the row id's for rows in the basis
         {
            int rownumber = _solver.number(_solver.basis().baseId(i));
            if( _solver.basis().desc().rowStatus(rownumber) != SPxBasis::Desc::P_ON_UPPER &&
                  _solver.basis().desc().rowStatus(rownumber) != SPxBasis::Desc::P_FIXED )
            {
               if( GT(feasVec[i], 0, feastol) )
                  return false;
            }

            if( _solver.basis().desc().rowStatus(rownumber) != SPxBasis::Desc::P_ON_LOWER &&
                  _solver.basis().desc().rowStatus(rownumber) != SPxBasis::Desc::P_FIXED )
            {
               if( LT(feasVec[i], 0, feastol) )
                  return false;
            }

         }
         else if( _solver.basis().baseId(i).isSPxColId() )  // get the column id's for the columns in the basis
         {
            int colnumber = _solver.number(_solver.basis().baseId(i));
            if( _solver.basis().desc().colStatus(colnumber) != SPxBasis::Desc::P_ON_UPPER &&
                  _solver.basis().desc().colStatus(colnumber) != SPxBasis::Desc::P_FIXED )
            {
               if( GT(feasVec[i], 0, feastol) )
                  return false;
            }

            if( _solver.basis().desc().colStatus(colnumber) != SPxBasis::Desc::P_ON_LOWER &&
                  _solver.basis().desc().colStatus(colnumber) != SPxBasis::Desc::P_FIXED )
            {
               if( LT(feasVec[i], 0, feastol) )
                  return false;
            }
         }
      }

      return true;
   }

} // namespace soplex
#endif
