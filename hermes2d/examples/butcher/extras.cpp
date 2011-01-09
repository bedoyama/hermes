#include "hermes2d.h"

bool HERMES_RESIDUAL_AS_VECTOR = false;
bool rk_time_step(double current_time, double time_step, ButcherTable* bt, 
                  scalar* coeff_vec, DiscreteProblem* dp, MatrixSolverType matrix_solver, 
                  double newton_tol, int newton_max_iter, bool verbose = true, 
                  double newton_damping_coeff = 1.0, double newton_max_allowed_residual_norm = 1e6)
{
  // Set up the solver, matrix, and rhs according to the solver selection.
  SparseMatrix* matrix = create_matrix(matrix_solver);
  Vector* rhs = create_vector(matrix_solver);
  Solver* solver = create_linear_solver(matrix_solver, matrix, rhs);

  // Get number of stages.
  int num_stages = bt->get_size();

  // Space.
  Space* space = dp->get_space(0);

  // Get ndof.
  int ndof = space->get_num_dofs();

  // Extract mesh from space.
  Mesh* mesh = space->get_mesh();

  // Create num_stages spaces for stage solutions.
  Hermes::Tuple<Space*> stage_spaces;
  stage_spaces.push_back(dp->get_space(0));
  for (int i = 1; i < num_stages; i++) {
    stage_spaces.push_back(space->dup(mesh));
    stage_spaces[i]->copy_orders(space);
  }

  // Initialize stage solutions: One for each stage.
  Hermes::Tuple<MeshFunction*> stage_solutions;
  for (int i=0; i < num_stages; i++) {
    Solution* stage_sln = new Solution(mesh);
    stage_sln->set_zero(mesh);
    stage_solutions.push_back(stage_sln);
  }

  // Extract the weak formulation from the original DiscreteProblem.
  WeakForm* wf = dp->get_weak_formulation();
  if (wf->get_neq() != 1) error("wf->neq != 1 not implemented yet.");
  Hermes::Tuple<WeakForm::MatrixFormVol> mfvol_base = wf->get_mfvol();
  Hermes::Tuple<WeakForm::MatrixFormSurf> mfsurf_base = wf->get_mfsurf();
  Hermes::Tuple<WeakForm::VectorFormVol> vfvol_base = wf->get_vfvol();
  Hermes::Tuple<WeakForm::VectorFormSurf> vfsurf_base = wf->get_vfsurf();

  // Weak formulation for all stages.
  WeakForm stage_wf(num_stages);

  // Create a constant Solution to represent the stage time 
  // stage_time = current_time + c_i*time_step.
  // (Temporarily workaround) 
  Solution* stage_time_sol = new Solution(mesh);

  // Duplicate matrix volume forms, enhance them with 
  // additional external solutions, and anter them as
  // blocks of the stage Jacobian.
  for (unsigned int m = 0; m < mfvol_base.size(); m++) {
    WeakForm::MatrixFormVol mfv_base = mfvol_base[m];
    for (int i = 0; i < num_stages; i++) {
      // Set time for stage 'i'.
      stage_time_sol->set_const(mesh, current_time + bt->get_C(i)*time_step);   

      for (int j = 0; j < num_stages; j++) {
        WeakForm::MatrixFormVol mfv_ij;
        mfv_ij.i = i;
        mfv_ij.j = j;
        mfv_ij.sym = mfv_base.sym;
        mfv_ij.area = mfv_base.area;
        mfv_ij.fn = mfv_base.fn;
        mfv_ij.ord = mfv_base.ord;
        mfv_ij.ext.copy(mfv_base.ext);
 
        // Add the i-th stage solution to the form's ExtData.
        mfv_ij.ext.push_back(stage_solutions[i]);

        // Add stage_time_sol as an external function to the form.
        mfv_ij.ext.push_back(stage_time_sol);

        // Add the matrix form to the corresponding block of the 
        // stage Jacobian matrix.
        stage_wf.add_matrix_form(&mfv_ij);
      }
    }
  }

  // Duplicate matrix surface forms, enhance them with 
  // additional external solutions, and anter them as
  // blocks of the stage Jacobian.
  for (unsigned int m = 0; m < mfsurf_base.size(); m++) {
    WeakForm::MatrixFormSurf mfs_base = mfsurf_base[m];
    for (int i = 0; i < num_stages; i++) {
      // Set time for stage 'i'.
      stage_time_sol->set_const(mesh, current_time + bt->get_C(i)*time_step); 
  
      for (int j = 0; j < num_stages; j++) {
        WeakForm::MatrixFormSurf mfs_ij;
        mfs_ij.i = i;
        mfs_ij.j = j;
        mfs_ij.area = mfs_base.area;
        mfs_ij.fn = mfs_base.fn;
        mfs_ij.ord = mfs_base.ord;
        mfs_ij.ext.copy(mfs_base.ext);
 
        // Add the i-th stage solution to the form's ExtData.
        mfs_ij.ext.push_back(stage_solutions[i]);

        // Add stage_time_sol as an external function to the form.
        mfs_ij.ext.push_back(stage_time_sol);

        // Add the matrix form to the corresponding block of the 
        // stage Jacobian matrix.
        stage_wf.add_matrix_form_surf(&mfs_ij);
      }
    }
  }

  // Duplicate vector volume forms, enhance them with 
  // additional external solutions, and anter them as
  // blocks of the stage residual.
  for (unsigned int m = 0; m < vfvol_base.size(); m++) {
    WeakForm::VectorFormVol vfv_base = vfvol_base[m];
    for (int i = 0; i < num_stages; i++) {
      WeakForm::VectorFormVol vfv_i;
      vfv_i.i = i;
      vfv_i.area = vfv_base.area;
      vfv_i.fn = vfv_base.fn;
      vfv_i.ord = vfv_base.ord;
      vfv_i.ext.copy(vfv_base.ext);
 
      // Add the i-th stage solution to the form's ExtData.
      vfv_i.ext.push_back(stage_solutions[i]);
 
      // Set time for stage 'i'.
      stage_time_sol->set_const(mesh, current_time + bt->get_C(i)*time_step);   

      // Add stage_time_sol as an external function to the form.
      vfv_i.ext.push_back(stage_time_sol);

      // Add the matrix form to the corresponding block of the 
      // stage Jacobian matrix.
      stage_wf.add_vector_form(&vfv_i);
    }
  }

  // Duplicate vector surface forms, enhance them with 
  // additional external solutions, and anter them as
  // blocks of the stage residual.
  for (unsigned int m = 0; m < vfsurf_base.size(); m++) {
    WeakForm::VectorFormSurf vfs_base = vfsurf_base[m];
    for (int i = 0; i < num_stages; i++) {
      WeakForm::VectorFormSurf vfs_i;
      vfs_i.i = i;
      vfs_i.area = vfs_base.area;
      vfs_i.fn = vfs_base.fn;
      vfs_i.ord = vfs_base.ord;
      vfs_i.ext.copy(vfs_base.ext);
 
      // Add the i-th stage solution to the form's ExtData.
      vfs_i.ext.push_back(stage_solutions[i]);

      // Set stage time for stage 'i'.
      stage_time_sol->set_const(mesh, current_time + bt->get_C(i)*time_step);   

      // Add stage_time_sol as an external function to the form.
      vfs_i.ext.push_back(stage_time_sol);

      // Add the matrix form to the corresponding block of the 
      // stage Jacobian matrix.
      stage_wf.add_vector_form_surf(&vfs_i);
    }
  }

  // Create a new DiscreteProblem for the stage slutions.
  bool is_linear = dp->get_is_linear();
  DiscreteProblem stage_dp(&stage_wf, stage_spaces, is_linear);

  // Stage vector of length num_stages * ndof, initialize with zeros.
  scalar* stage_vec = new scalar[num_stages*ndof];
  memset(stage_vec, 0, num_stages * ndof * sizeof(scalar));

  // The Newton's loop.
  double residual_norm;
  int it = 1;
  while (true)
  {
    // Prepare external solution for each stage.
    scalar* vec = new scalar[ndof];
    for (int r = 0; r < num_stages; r++) {
      memset(vec, 0, ndof * sizeof(scalar));
      double increment;
      for (int i = 0; i < ndof; i++) {
        increment = 0;
        for (int s = 0; s < num_stages; s++) {
          increment += bt->get_A(r, s) * stage_vec[s*ndof + i]; 
        }
        vec[i] = coeff_vec[i] + time_step * increment;
      }
      Solution::vector_to_solution(vec, space, (Solution*)stage_solutions[r]);
    } 
    delete [] vec;

    // Calculating weight coefficients for blocks in the 
    // Stage jacobian matrix. 
    Table block_weights(num_stages);
    for (int r = 0; r < num_stages; r++) {
      for (int s = 0; s < num_stages; s++) {
        block_weights.set_A(r, s, bt->get_A(r, s) * time_step);
      }
    }

    // Assemble the stage Jacobian matrix and residual vector.
    // Blocks that would be zeroed will not be assembled, and 
    // all assembled blocks will be weighted according to the 
    // Butcher's table.
    bool rhs_only = false;
    stage_dp.assemble(NULL, matrix, rhs, rhs_only, &block_weights);

    // Add 1.0 to each diagonal element of the matrix.
    matrix->add_to_diagonal(1.0);

    // Add stage_vec to rhs.
    for (int i = 0; i < num_stages*ndof; i++) rhs->add(i, stage_vec[i]);

    // Multiply the residual vector with -1 since the matrix 
    // equation reads J(Y^n) \deltaY^{n+1} = -F(Y^n).
    rhs->change_sign();
    
    // Measure the residual norm.
    if (HERMES_RESIDUAL_AS_VECTOR) {
      // Calculate the l2-norm of residual vector.
      residual_norm = get_l2_norm(rhs);
    }
    else {
      // Translate the residual vector into a residual function (or multiple functions) 
      // in the corresponding finite element space(s) and measure their norm(s) there.
      // This is more meaningful since not all components in the coefficient vector 
      // have the same weight when translated into the finite element space.
      Hermes::Tuple<Solution*> residuals;
      Hermes::Tuple<bool> add_dir_lift;
      for (int i = 0; i < num_stages; i++) {
        residuals.push_back((Solution*)stage_solutions[i]);
        add_dir_lift.push_back(false);
      }
      Solution::vector_to_solutions(rhs, stage_dp.get_spaces(), residuals, add_dir_lift);
      residual_norm = calc_norms(residuals);
    }

    // Info for the user.
    if (verbose) info("---- Newton iter %d, ndof %d, residual norm %g", it, ndof, residual_norm);

    // If maximum allowed residual norm is exceeded, fail.
    if (residual_norm > newton_max_allowed_residual_norm) {
      if (verbose) {
        info("Current residual norm: %g", residual_norm);
        info("Maximum allowed residual norm: %g", newton_max_allowed_residual_norm);
        info("Newton solve not successful, returning false.");
      }
      return false;
    }

    // If residual norm is within tolerance, or the maximum number 
    // of iteration has been reached, then quit.
    if ((residual_norm < newton_tol || it > newton_max_iter) && it > 1) break;

    // Solve the linear system.
    if(!solver->solve()) error ("Matrix solver failed.\n");

    // Add \deltaY^{n+1} to Y^n.
    for (int i = 0; i < num_stages*ndof; i++) 
      stage_vec[i] += newton_damping_coeff * solver->get_solution()[i];

    it++;
  }

  // If max number of iterations was exceeded, fail. 
  if (it >= newton_max_iter) {
    if (verbose) info("Maximum allowed number of Newton iterations exceeded, returning false.");
    return false;
  }

  // Calculate new coefficient vector using the stage vector and the Butcher's table.
  for (int i = 0; i < ndof; i++) {
    double increment = 0;
    for (int s = 0; s < num_stages; s++) {
      increment += bt->get_B(s) * stage_vec[s*ndof + i]; 
    }
    coeff_vec[i] += time_step * increment;
  } 

  // Clean up.
  delete matrix;
  delete rhs;
  delete solver;

  // Delete stage spaces, but not the first (original) one.
  for (int i = 1; i < num_stages; i++) delete stage_spaces[i];

  // Delete stage solutions.
  for (int i = 0; i < num_stages; i++) delete stage_solutions[i];

  // Delete solution that represents stage time.
  delete stage_time_sol;

  // Delete stage_vec.
  delete [] stage_vec;
  
  return true;
}
