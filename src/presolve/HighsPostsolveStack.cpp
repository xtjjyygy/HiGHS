#include "presolve/HighsPostsolveStack.h"

#include <numeric>

namespace presolve {

void HighsPostsolveStack::initializeIndexMaps(int numRow, int numCol) {
  origRowIndex.resize(numRow);
  std::iota(origRowIndex.begin(), origRowIndex.end(), 0);

  origColIndex.resize(numCol);
  std::iota(origColIndex.begin(), origColIndex.end(), 0);
}

void HighsPostsolveStack::compressIndexMaps(
    const std::vector<int>& newRowIndex, const std::vector<int>& newColIndex) {
  // loop over rows, decrease row counter for deleted rows (marked with -1),
  // store original index at new index position otherwise
  int numRow = origRowIndex.size();
  for (size_t i = 0; i != newRowIndex.size(); ++i) {
    if (newRowIndex[i] == -1)
      --numRow;
    else
      origRowIndex[newRowIndex[i]] = origRowIndex[i];
  }
  // resize original index array to new size
  origRowIndex.resize(numRow);

  // now compress the column array
  int numCol = origColIndex.size();
  for (size_t i = 0; i != newColIndex.size(); ++i) {
    if (newColIndex[i] == -1)
      --numCol;
    else
      origColIndex[newColIndex[i]] = origColIndex[i];
  }
  origColIndex.resize(numCol);
}

void HighsPostsolveStack::FreeColSubstitution::undo(
    const std::vector<std::pair<int, double>>& rowValues,
    const std::vector<std::pair<int, double>>& colValues,
    HighsSolution& solution, HighsBasis& basis) {
  double colCoef = 0;
  // compute primal values
  HighsCDouble rowValue = 0;
  for (const auto& rowVal : rowValues) {
    if (rowVal.first == col)
      colCoef = rowVal.second;
    else
      rowValue += rowVal.second * solution.col_value[rowVal.first];
  }

  assert(colCoef != 0);
  solution.row_value[row] =
      double(rowValue + colCoef * solution.col_value[col]);
  solution.col_value[col] = double((rhs - rowValue) / colCoef);

  // if no dual values requested, return here
  if (solution.row_dual.empty()) return;

  // compute the row dual value such that reduced cost of basic column is 0
  solution.row_dual[row] = 0;
  HighsCDouble dualval = -colCost;
  for (const auto& colVal : colValues)
    dualval -= colVal.second * solution.row_dual[colVal.first];

  solution.col_dual[col] = 0;
  solution.row_dual[row] = double(dualval / colCoef);

  // set basis status
  basis.col_status[col] = HighsBasisStatus::BASIC;
  basis.row_status[row] = HighsBasisStatus::NONBASIC;
}

void HighsPostsolveStack::DoubletonEquation::undo(
    const std::vector<std::pair<int, double>>& colValues,
    HighsSolution& solution, HighsBasis& basis) {
  // retrieve the row and column index, the row side and the two
  // coefficients then compute the primal values
  solution.row_value[row] = rhs;
  solution.col_value[colSubst] =
      double((rhs - HighsCDouble(coef) * solution.col_value[col]) / coefSubst);

  // can only do primal postsolve, stop here
  if (solution.row_dual.empty()) return;

  // now compute the current dual value of the substituted row
  HighsCDouble substColDual = substCost;
  solution.row_dual[row] = 0;
  for (const auto& colVal : colValues)
    substColDual += colVal.second * solution.row_dual[colVal.first];

  // todo, I think we should not use Status NONBASIC. The status should
  // always be clear from the presolve rule and the postsolve gets actually
  // simpler if we can trust the status. For this we should follow the
  // invariant that the primal values are exactly the bounds and not
  // computed with numerical errors for the nonbasic variables.
  // The primal values of the basic variables should be the ones that are
  // computed with numerical errors. If we set column dual values, then
  // the basic variables should have an exact zero and the other values
  // should be computed accordingly.
  assert(basis.col_status[col] != HighsBasisStatus::NONBASIC);

  // now decide which of the two columns becomes basic for this row
  // this depends on whether the bounds that are used in the basis
  // on the column that stayed have been computed from the bounds of the
  // substituted column
  if (((!lowerTightened || basis.col_status[col] == HighsBasisStatus::UPPER) &&
       (!upperTightened || basis.col_status[col] == HighsBasisStatus::LOWER)) ||
      basis.col_status[col] == HighsBasisStatus::BASIC) {
    // there is no tightened bound that is used in the basic solution
    // hence we make the substituted column basic
    solution.col_dual[colSubst] = 0;
    basis.col_status[col] = HighsBasisStatus::BASIC;
    solution.row_dual[row] = -double(substColDual / coefSubst);
    basis.row_status[row] = solution.row_dual[row] >= 0
                                ? HighsBasisStatus::UPPER
                                : HighsBasisStatus::LOWER;
    return;
  }
  assert(basis.col_status[col] != HighsBasisStatus::ZERO);
  assert(basis.col_status[col] != HighsBasisStatus::NONBASIC);

  switch (basis.col_status[col]) {
    case HighsBasisStatus::LOWER:
      assert(lowerTightened);
      // the lower bound was tightened and the column sits at this tightened
      // bound
      if (std::signbit(coef) == std::signbit(coefSubst)) {
        // the sign of the coefficients is equal, hence the tightened lower
        // bound comes from the upper bound of the substituted columns:
        //   a*x + d*y = b
        //   x = b/ a - (d/a)*y
        //   x >= b/a - (d/a) * ub_y
        basis.col_status[colSubst] = HighsBasisStatus::UPPER;
        solution.col_value[colSubst] = substUpper;
      } else {
        basis.col_status[colSubst] = HighsBasisStatus::LOWER;
        solution.col_value[colSubst] = substLower;
      }
      break;
    case HighsBasisStatus::UPPER:
      assert(upperTightened);

      if (std::signbit(coef) == std::signbit(coefSubst)) {
        // the sign of the coefficients is equal, hence the tightened upper
        // bound comes from the lower bound of the substituted columns:
        //   a*x + d*y = b
        //   x = b/ a - (d/a)*y
        //   x <= b/a - (d/a) * lb_y
        basis.col_status[colSubst] = HighsBasisStatus::LOWER;
        solution.col_value[colSubst] = substLower;
      } else {
        basis.col_status[colSubst] = HighsBasisStatus::UPPER;
        solution.col_value[colSubst] = substUpper;
      }
      break;
    default:
      assert(false);
  }
  // the substituted column has now a basis status lower or upper and has
  // been set to the exact bound value
  assert(basis.col_status[colSubst] == HighsBasisStatus::LOWER ||
         basis.col_status[colSubst] == HighsBasisStatus::UPPER);

  // now recompute the solution value for the other column from the bound
  // the substituted column has been set to to minimize numerical errors
  solution.col_value[col] = double(
      (rhs - HighsCDouble(coefSubst) * solution.col_value[coefSubst]) / coef);

  // The column that we make basic must get zero reduced cost. Therefore we
  // compute the multiplier for the doubleton equation row such that we can
  // set the basic columns dual value to exact zero.
  basis.col_status[col] = HighsBasisStatus::BASIC;
  solution.row_dual[row] = -(solution.col_dual[col] / coef);
  solution.col_dual[col] = 0;

  // finally add the dual value of this row to the reduced cost for the
  // substituted column and set those to their computed value
  substColDual += solution.row_dual[row] * HighsCDouble(coefSubst);
  solution.col_dual[colSubst] = (double)substColDual;

  // now as we have recomputed the values as above the nonbasic variable
  // sits exactly at its bound and the reduced cost of the basic variable is
  // exactly set to zero. The reduced cost of the nonbasic variable and the
  // primal value of the basic variable are computed as accurately as
  // possible from the solution values.
}

void HighsPostsolveStack::EqualityRowAddition::undo(HighsSolution& solution,
                                                    HighsBasis& basis) {
  // compute the primal value of the row without the added equation
  solution.row_value[row] =
      double(solution.row_value[row] -
             HighsCDouble(eqRowScale) * solution.row_value[addedEqRow]);

  // nothing more to do if the row is zero in the dual solution or there is
  // no dual solution
  if (solution.row_dual.empty() || solution.row_dual[row] == 0.0) return;

  // the dual multiplier of the row implicitly increases the dual multiplier
  // of the equation with the scale the equation was added with
  solution.row_dual[addedEqRow] =
      double(HighsCDouble(eqRowScale) * solution.row_dual[row] +
             solution.row_dual[addedEqRow]);
}

void HighsPostsolveStack::ForcingColumn::undo(
    const std::vector<std::pair<int, double>>& colValues,
    HighsSolution& solution, HighsBasis& basis) {
  // todo
}

void HighsPostsolveStack::SingletonRow::undo(HighsSolution& solution,
                                             HighsBasis& basis) {
  // nothing to do if the rows dual value is zero in the dual solution or
  // there is no dual solution
  solution.row_value[row] = coef * solution.col_value[col];
  if (solution.row_dual.empty() || solution.row_dual[row] == 0.0) return;

  if ((!colLowerTightened ||
       basis.col_status[col] != HighsBasisStatus::LOWER) &&
      (!colUpperTightened ||
       basis.col_status[col] != HighsBasisStatus::UPPER)) {
    // the tightened bound is not used in the basic solution
    // hence we simply make the row basic and give it a dual multiplier of 0
    basis.row_status[row] = HighsBasisStatus::BASIC;
    solution.row_dual[row] = 0;
    return;
  }

  switch (basis.col_status[col]) {
    case HighsBasisStatus::LOWER:
      assert(colLowerTightened);
      if (coef > 0)
        // tightened lower bound comes from row lower bound
        basis.row_status[row] = HighsBasisStatus::LOWER;
      else
        // tightened lower bound comes from row upper bound
        basis.row_status[row] = HighsBasisStatus::UPPER;

      break;
    case HighsBasisStatus::UPPER:
      if (coef > 0)
        // tightened upper bound comes from row lower bound
        basis.row_status[row] = HighsBasisStatus::UPPER;
      else
        // tightened lower bound comes from row upper bound
        basis.row_status[row] = HighsBasisStatus::LOWER;
      break;
    default:
      assert(false);
  }

  // column becomes basic
  basis.col_status[col] = HighsBasisStatus::BASIC;
  // choose the row dual value such that the columns reduced cost becomes
  // zero
  solution.row_dual[row] = -solution.col_dual[col] / coef;
  solution.col_dual[col] = 0;
}

// column fixed to lower or upper bound
void HighsPostsolveStack::FixedCol::undo(
    const std::vector<std::pair<int, double>>& colValues,
    HighsSolution& solution, HighsBasis& basis) {
  // set solution value
  solution.col_value[col] = fixValue;

  if (solution.row_dual.empty()) return;

  // compute reduced cost
  HighsCDouble reducedCost = colCost;
  for (const auto& colVal : colValues)
    reducedCost += colVal.second * solution.row_dual[colVal.first];

  solution.col_dual[col] = double(reducedCost);

  // set basis status
  if (!basis.col_status.empty())
    basis.col_status[col] =
        atLower ? HighsBasisStatus::LOWER : HighsBasisStatus::UPPER;
}

void HighsPostsolveStack::RedundantRow::undo(
    const std::vector<std::pair<int, double>>& rowValues,
    HighsSolution& solution, HighsBasis& basis) {
  // compute primal solution value
  HighsCDouble rowValue = 0;
  for (const auto& rowVal : rowValues)
    rowValue += solution.col_value[rowVal.first] * rowVal.second;

  solution.row_value[row] = double(rowValue);

  // set row dual to zero if dual solution requested
  if (solution.row_dual.empty()) return;

  solution.row_dual[row] = 0.0;

  if (!basis.row_status.empty())
    basis.row_status[row] = HighsBasisStatus::BASIC;
}

void HighsPostsolveStack::ForcingRow::undo(
    const std::vector<std::pair<int, double>>& rowValues,
    HighsSolution& solution, HighsBasis& basis) {
  solution.row_value[row] = side;

  // compute the row dual multiplier and determine the new basic column
  int basicCol = -1;
  solution.row_dual[row] = 0.0;
  for (const auto& rowVal : rowValues) {
    if (solution.col_dual[rowVal.first] * rowVal.second < 0) {
      // column is dual infeasible, compute the row dual such that its
      // reduced cost become zero and check if it is larger then the current
      // dual value if it is we remember this column as the new basic
      // column, otherwise it will become dual feasible due to another
      // column in the row becoming basic
      double dualVal = -solution.col_dual[rowVal.first] / rowVal.second;
      if (dualVal > solution.row_dual[row]) {
        basicCol = rowVal.first;
        solution.row_dual[row] = dualVal;
      }
    }
  }

  if (basicCol != -1) {
    for (const auto& rowVal : rowValues) {
      solution.col_dual[rowVal.first] =
          double(HighsCDouble(solution.col_dual[rowVal.first]) +
                 solution.row_dual[row] * rowVal.second);
    }
    solution.col_dual[basicCol] = 0;
    basis.row_status[row] =
        (atLower ? HighsBasisStatus::LOWER : HighsBasisStatus::UPPER);

    basis.col_status[basicCol] = HighsBasisStatus::BASIC;
  } else {
    // in case all columns are already dual feasible with a dual multiplier
    // of zero just make the row basic
    basis.row_status[row] = HighsBasisStatus::BASIC;
  }
}

void HighsPostsolveStack::DuplicateRow::undo(HighsSolution& solution,
                                             HighsBasis& basis) {
  if (!rowUpperTightened && !rowLowerTightened) {
    // simple case of row2 being redundant, in which case it just gets a
    // dual multiplier of 0 and is made basic
    solution.row_dual[duplicateRow] = 0.0;
    basis.row_status[duplicateRow] = HighsBasisStatus::BASIC;
    return;
  }

  // at least one bound of the row was tightened by using the bound of the
  // scaled parallel row, hence we might need to make the parallel row
  // nonbasic and the row basic

  // first determine the basis status if it is nonbasic without further
  // specification
  if (basis.row_status[row] == HighsBasisStatus::NONBASIC) {
    if (solution.row_dual[row] >= 0)
      basis.row_status[row] = HighsBasisStatus::UPPER;
    else if (solution.row_dual[row] < 0)
      basis.row_status[row] = HighsBasisStatus::LOWER;
  }

  switch (basis.row_status[row]) {
    case HighsBasisStatus::BASIC:
      // if row is basic the parallel row is also basic
      solution.row_dual[duplicateRow] = 0.0;
      basis.row_status[duplicateRow] = HighsBasisStatus::BASIC;
      break;
    case HighsBasisStatus::UPPER:
      // if row sits on its upper bound, and the row upper bound was
      // tightened using the parallel row we make the row basic and
      // transfer its dual value to the parallel row with the proper scale
      if (rowUpperTightened) {
        solution.row_dual[duplicateRow] =
            solution.row_dual[row] / duplicateRowScale;
        solution.row_dual[row] = 0.0;
        basis.row_status[row] = HighsBasisStatus::BASIC;
        if (duplicateRowScale > 0)
          basis.row_status[duplicateRow] = HighsBasisStatus::UPPER;
        else
          basis.row_status[duplicateRow] = HighsBasisStatus::LOWER;
      } else {
        solution.row_dual[duplicateRow] = 0.0;
        basis.row_status[duplicateRow] = HighsBasisStatus::BASIC;
      }
      break;
    case HighsBasisStatus::LOWER:
      if (rowLowerTightened) {
        solution.row_dual[duplicateRow] =
            solution.row_dual[row] / duplicateRowScale;
        basis.row_status[row] = HighsBasisStatus::BASIC;
        solution.row_dual[row] = 0.0;
        if (duplicateRowScale > 0)
          basis.row_status[duplicateRow] = HighsBasisStatus::UPPER;
        else
          basis.row_status[duplicateRow] = HighsBasisStatus::LOWER;
      } else {
        solution.row_dual[duplicateRow] = 0.0;
        basis.row_status[duplicateRow] = HighsBasisStatus::BASIC;
      }
      break;
    default:
      assert(false);
  }
}

void HighsPostsolveStack::DuplicateColumn::undo(HighsSolution& solution,
                                                HighsBasis& basis,
                                                double feastol) {
  // the column dual of the duplicate column is easily computed by scaling
  // since col * colScale yields the coefficient values and cost of the
  // duplicate column.
  if (!solution.col_dual.empty())
    solution.col_dual[duplicateCol] = solution.col_dual[col] * colScale;

  if (!basis.col_status.empty()) {
    // do postsolve using basis status if a basis is available:
    // if the merged column is nonbasic, we can just set both columns
    // to the corresponding basis status and value
    solution.col_dual[duplicateCol] = solution.col_dual[col] * colScale;
    if (basis.col_status[col] == HighsBasisStatus::LOWER) {
      solution.col_value[col] = colLower;
      if (colScale > 0) {
        basis.col_status[duplicateCol] = HighsBasisStatus::LOWER;
        solution.col_value[duplicateCol] = duplicateColLower;
      } else {
        basis.col_status[duplicateCol] = HighsBasisStatus::UPPER;
        solution.col_value[duplicateCol] = duplicateColUpper;
      }
      // nothing else to do
      return;
    }

    if (basis.col_status[col] == HighsBasisStatus::UPPER) {
      solution.col_value[col] = colUpper;
      if (colScale > 0) {
        basis.col_status[duplicateCol] = HighsBasisStatus::UPPER;
        solution.col_value[duplicateCol] = duplicateColUpper;
      } else {
        basis.col_status[duplicateCol] = HighsBasisStatus::LOWER;
        solution.col_value[duplicateCol] = duplicateColLower;
      }
      // nothing else to do
      return;
    }

    assert(basis.col_status[col] == HighsBasisStatus::BASIC);
  }

  // either no basis for postsolve, or column status is basic. One of
  // the two columns must become nonbasic. In case of integrality it is simpler
  // to choose col, since it has a coefficient of +1 in the equation y = col +
  // colScale * duplicateCol where the merged column is y and is currently using
  // the index of col. The duplicateCol can have a positive or negative
  // coefficient. So for postsolve, we first start out with col sitting at the
  // lower bound and compute the corresponding value for the duplicate column as
  // (y - colLower)/colScale. Then the following things might happen:
  // - case 1: the value computed for duplicateCol is within the bounds
  // - case 1.1: duplicateCol is continuous -> accept value, make col nonbasic
  // at lower and duplicateCol basic
  // - case 1.2: duplicateCol is integer -> accept value if integer feasible,
  // otherwise round down and compute value of col as
  // col = y - colScale * duplicateCol
  // - case 2: the value for duplicateCol violates the column bounds: make it
  // sit at the bound that is violated
  //           and compute the value of col as col = y - colScale * duplicateCol
  //           for basis postsolve col is basic and duplicateCol nonbasic at
  //           lower/upper depending on which bound is violated.

  double mergeVal = solution.col_value[col];
  solution.col_value[duplicateCol] =
      double((HighsCDouble(mergeVal) - colLower) / colScale);

  bool recomputeCol = false;

  if (solution.col_value[duplicateCol] > duplicateColUpper) {
    solution.col_value[duplicateCol] = duplicateColUpper;
    recomputeCol = true;
    if (!basis.col_status.empty())
      basis.col_status[duplicateCol] = HighsBasisStatus::UPPER;
  } else if (solution.col_value[duplicateCol] < duplicateColLower) {
    solution.col_value[duplicateCol] = duplicateColLower;
    recomputeCol = true;
    if (!basis.col_status.empty())
      basis.col_status[duplicateCol] = HighsBasisStatus::LOWER;
  } else if (duplicateColIntegral) {
    double roundVal = std::round(solution.col_value[duplicateCol]);
    if (std::abs(roundVal - solution.col_value[duplicateCol]) > feastol) {
      solution.col_value[duplicateCol] =
          std::floor(solution.col_value[duplicateCol]);
      recomputeCol = true;
    }
  }

  if (recomputeCol) {
    solution.col_value[col] =
        mergeVal - colScale * solution.col_value[duplicateCol];
  } else if (!basis.col_status.empty()) {
    // setting col to its lower bound yielded a feasible value for duplicateCol
    basis.col_status[duplicateCol] = basis.col_status[col];
    basis.col_status[col] = HighsBasisStatus::LOWER;
  }
}

}  // namespace presolve