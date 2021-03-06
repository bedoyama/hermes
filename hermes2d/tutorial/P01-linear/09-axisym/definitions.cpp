#include "weakform/weakform.h"
#include "integrals/integrals_h1.h"
#include "boundaryconditions/essential_bcs.h"
#include "weakform_library/h1.h"

using namespace WeakFormsH1;

/* Weak forms */

class CustomWeakFormPoissonNewton : public WeakForm
{
public:
  CustomWeakFormPoissonNewton(double lambda, double alpha, double T0, std::string bdy_heat_flux) : WeakForm(1)
  {
    add_matrix_form(new VolumetricMatrixForms::DefaultLinearDiffusion(0, 0, lambda, HERMES_SYM, HERMES_AXISYM_Y));
    add_matrix_form_surf(new SurfaceMatrixForms::DefaultMatrixFormSurf(0, 0, bdy_heat_flux, alpha, HERMES_AXISYM_Y));
    add_vector_form_surf(new SurfaceVectorForms::DefaultVectorFormSurf(0, bdy_heat_flux, alpha * T0, HERMES_AXISYM_Y));
  };
};

