/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                       */
/*    This file is part of the HiGHS linear optimization suite           */
/*                                                                       */
/*    Written and engineered 2008-2020 at the University of Edinburgh    */
/*                                                                       */
/*    Available as open-source under the MIT License                     */
/*                                                                       */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**@file lp_data/HighsSolutionDebug.h
 * @brief
 * @author Julian Hall, Ivet Galabova, Qi Huangfu and Michael Feldmeier
 */
#ifndef SIMPLEX_HIGHSSOLUTIONDEBUG_H_
#define SIMPLEX_HIGHSSOLUTIONDEBUG_H_

#include "lp_data/HighsInfo.h"
#include "lp_data/HighsLp.h"
#include "lp_data/HighsOptions.h"
#include "lp_data/HighsSolution.h"

HighsDebugStatus debugHighsBasicSolution(const string message,
					 const HighsOptions& options,
					 const HighsLp& lp,
					 const HighsBasis& basis,
					 const HighsSolution& solution,
					 const HighsInfo& info,
					 const HighsModelStatus model_status,
					 const HighsModelStatus scaled_model_status);

HighsDebugStatus debugHighsBasicSolution(const string message,
					 const HighsOptions& options,
					 const HighsLp& lp,
					 const HighsBasis& basis,
					 const HighsSolution& solution,
					 const HighsSolutionParams& solution_params,
					 const HighsModelStatus model_status,
					 const HighsModelStatus scaled_model_status);

void debugHighsBasicSolutionPrimalDualInfeasibilitiesAndErrors(const HighsOptions& options,
							       const HighsLp& lp,
							       const HighsBasis& basis,
							       const HighsSolution& solution,
							       double& primal_objective_value,
							       double& dual_objective_value,
							       HighsSolutionParams& solution_params,
							       HighsPrimalDualErrors& primal_dual_errors);

bool debugBasicSolutionVariable(bool report,
				const double primal_feasibility_tolerance,
				const double dual_feasibility_tolerance,
				const HighsBasisStatus status,
				const double lower, const double upper, 
				const double value, const double dual,
				int& num_non_basic_var,
				int& num_basic_var,
				double& off_bound_nonbasic,
				double& primal_infeasibility,
				double& dual_infeasibility);

HighsDebugStatus debugEqualSolutionParams(const HighsOptions& options,
					  const HighsSolutionParams& solution_params0,
					  const HighsSolutionParams& solution_params1);
HighsDebugStatus debugEqualSolutionObjectiveParams(const HighsOptions& options,
						   const HighsSolutionParams& solution_params0,
						   const HighsSolutionParams& solution_params1);
HighsDebugStatus debugEqualSolutionStatusParams(const HighsOptions& options,
						const HighsSolutionParams& solution_params0,
						const HighsSolutionParams& solution_params1);
HighsDebugStatus debugEqualSolutionInfeasibilityParams(const HighsOptions& options,
						       const HighsSolutionParams& solution_params0,
						       const HighsSolutionParams& solution_params1);

HighsDebugStatus debugCompareSolutionParamValue(const string name, const HighsOptions& options,
						const double v0, const double v1);


#endif  // SIMPLEX_HIGHSSOLUTIONDEBUG_H_
