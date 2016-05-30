#include "pressure_solver.h"
#include "boundaries.h"
#include "bpreds.h"

#include "Model.h"
#include "SAMG_Solver.h"
#include "SAMG_Settings.h"
#include "MGMRES_Solver.h"
#include "PDE_Integrator.h"
#include "NumIntegral_dNT_op_dN_dV.h"
#include "NumIntegral_NT_op_N_dV.h"

#include <array>

using namespace std;

namespace csmp {
	namespace tperm{
		

		/**
		Relies on the following variables:

			\code
			// porperly set
			// fractures only (user)
			hydraulic aperture	hy	m	3	0	1	ELEMENT
			mechanical aperture	me	m	1	0	1	ELEMENT
			// matrix only (user)
			permeability	pe	m2	3	1e-25	1e-08	ELEMENT
			// result, exist but must not be initialized, will contain results after exit
			// will be [m2 / Pa.s] for matrix and [m3 / Pa.s] for fractures
			conductivity	co	m2 Pa-1 s-1	3	1e-25	1	ELEMENT
			\endcode

		Viscosity is assumed unity. It follows that stored conductivity is equal to permeability for matrix,
		and equal to transmissivity for fractures.
		*/
		void conductivity(Model<3>& m)
		{
			const bool twod = !containsVolumeElements(m.Region("Model"));
			const Index kKey(m.Database().StorageKey("conductivity"));
			const Index ahKey(m.Database().StorageKey("hydraulic aperture"));
			const Index pKey(m.Database().StorageKey("permeability"));
			const MatrixElement<3> mbp(twod);
			TensorVariable<3> ttemp(PLAIN, 0.);
			// reset first
			m.Region("Model").InputPropertyValue("conductivity", ttemp);
			// set
			for (const auto& ePtr : m.Region("Model").ElementVector()) {
				if (mbp(ePtr)) { // matrix
					ePtr->Read(pKey, ttemp);
				} else { // fracture
					ePtr->Read(ahKey, ttemp);
					ttemp.Power(3);
					ttemp = ttemp / 12.;
				}
				ePtr->Store(kKey, ttemp);
			}
		}


		/**
		Relies on the following variables:

			\code
			// initialized properly (m2 / Pa.s for matrix, m3 / Pa.s for fractures)
			conductivity	co	m2 Pa-1 s-1	3	1e-25	1	ELEMENT
			// working, exist but must not be initialized
			fluid pressure	fl	Pa	1	-1e-05	1e+09	NODE
			fluid volume source	fl	m3 s-1	1	-1	1	ELEMENT
			// result, exist but must not be initialized, will contain results after exit
			// for 0..(dimension - 1), or (number of boundaries - 1)
			pressure gradient 0	pr	Pa m-1	2	-1e+20	1e+20	ELEMENT
			velocity 0	ve	m s-1	2	-100	100	ELEMENT
			\endcode

		@todo Check that flag override when resetting actually works
		@todo Linear boundary conditions
		*/
		void solve(const Boundaries& bds, Model<3>& m)
		{
			const Parameter fp = m.Database().Parameter("fluid pressure");
			// reset fluid volume source
			m.Region("Model").InputPropertyValue("fluid volume source", makeScalar(PLAIN, 0.));
			// for each boundary set
			for (size_t d(0); d < bds.size(); ++d) {
				// reset fluid pressure
				m.Region("Model").InputPropertyValue(fp.name.c_str(), makeScalar(PLAIN, 0.));
				// apply BCs
				dirichlet_scalar_bc(bds[d].first, fp.key, 1.0);
				dirichlet_scalar_bc(bds[d].second, fp.key, -1.0);
				// solve pressure
				solve_pressure(m);
				// store pressure gradient and velocity
				pgrad_and_vel(m, d);
			}
		}


		void dirichlet_scalar_bc(const tperm::Boundary& bd, const Index& k, double val)
		{
			const ScalarVariable bval(DIRICH, val);
			for (size_t i(0); i < bd.size(); ++i)
				bd[i]->Store(k, bval);
		}


		/**
		Relies on the following variables:

			\code
			// initialized properly (m2 / Pa.s for matrix, m3 / Pa.s for fractures)
			conductivity	co	m2 Pa-1 s-1	3	1e-25	1	ELEMENT
			// initialized to proper values (fvs) and boundary conditions (fp)
			// result is contained in (fp)
			fluid pressure	fl	Pa	1	-1e-05	1e+09	NODE
			fluid volume source	fl	m3 s-1	1	-1	1	ELEMENT
			\endcode

		@todo Alternative small budget solver
		*/
		void solve_pressure(csmp::Model<3>& m)
		{
			SAMG_Settings samg_settings;
			//SAMG_Solver lin_solver(&samg_settings);	
MGMRES_Solver lin_solver(1.0E-16, 1.0E-8, 1000, 100);

			PDE_Integrator<3, Region> ssfp(&lin_solver);
			// conductance matrix [K] on the left-hand side
			NumIntegral_dNT_op_dN_dV<3> conductance(m.Database(), "conductivity", "fluid pressure", "fluid pressure");
			// source vector {Q} on the right-hand side
			NumIntegral_NT_op_N_dV<3> source(m.Database(), "fluid volume source", "fluid pressure");
			ssfp.Add(&conductance);
			ssfp.Add(&source);
			m.Apply(ssfp);
		}


		/**
		Relies on the following variables:

			\code
			// initialized material props
			hydraulic aperture	hy	m	3	0	1	ELEMENT
			permeability	pe	m2	3	1e-25	1e-08	ELEMENT
			// pressure field
			fluid pressure	fl	Pa	1	-1e-05	1e+09	NODE
			// result variables, for provided dimension d
			pressure gradient 0	pr	Pa m-1	2	-1e+20	1e+20	ELEMENT
			velocity 0	ve	m s-1	2	-100	100	ELEMENT
			\endcode

		Assumes unit viscosity.
		*/
		void pgrad_and_vel(csmp::Model<3>& m, size_t d)
		{
			const VectorVariable<3> zero_vec(PLAIN, 0.);
			const array<string, 2> rvnames = { (string) "pressure gradient " + to_string(d), (string) "velocity " + to_string(d) };
			const Index pgKey(m.Database().StorageKey(rvnames[0].c_str()));
			const Index vKey(m.Database().StorageKey(rvnames[1].c_str()));
			const bool twod = !containsVolumeElements(m.Region("Model"));
			const MatrixElement<3> mbp(twod);
			const Index ahKey(m.Database().StorageKey("hydraulic aperture"));
			const Index kKey(m.Database().StorageKey("permeability"));
			const Index pKey(m.Database().StorageKey("fluid pressure"));
			DenseMatrix<DM_MIN> DERIV(3, 3);
			TensorVariable<3> k;
			VectorVariable<3> velo, grad;
			double pf(0.);
			// reset
			for (const auto& rv : rvnames)
				m.Region("Model").InputPropertyValue(rv.c_str(), zero_vec);
			// vt = -k (lt grad p)
			for (const auto& eit : m.Region("Model").ElementVector()) {
				// get k tensor, assuming unit viscosity
				if (mbp(eit)) // if matrix element, k is permeability
					eit->Read(kKey, k);
				else { // if fracture element, k is ah^2/12
					eit->Read(ahKey, k);
					k.Power(2.);
					k /= 12.;
				}
				// compute
				velo = 0.; grad = 0.;
				eit->dN_AtBaryCenter(DERIV, 1U);
				const size_t nc(eit->Nodes());
				for (size_t i = 0; i < nc; ++i) {
					pf = eit->N(i)->Read(pKey);
					grad(0) += pf * DERIV(0, i); grad(1) += pf * DERIV(1, i); grad(2) += pf * DERIV(2, i);
					velo(0) += -pf * (k(0, 0)*DERIV(0, i) + k(0, 1)*DERIV(1, i) + k(0, 2)*DERIV(2, i));
					velo(1) += -pf * (k(1, 0)*DERIV(0, i) + k(1, 1)*DERIV(1, i) + k(1, 2)*DERIV(2, i));
					velo(2) += -pf * (k(2, 0)*DERIV(0, i) + k(2, 1)*DERIV(1, i) + k(2, 2)*DERIV(2, i));
				}
				// store
				eit->Store(vKey, velo);
				eit->Store(pgKey, grad);
			}
		}


	} // !tperm
} // !csmp