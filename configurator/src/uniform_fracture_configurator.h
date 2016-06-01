#ifndef TP_UNIFFRACCONFIG_H
#define TP_UNIFFRACCONFIG_H

#include "configurator.h"
#include "TensorVariable.h"
#include <vector>

namespace csmp {
	template<size_t> class Element;

	namespace tperm {

		/// Configures uniform fractures
		class UniformFractureConfigurator : public Configurator
		{
		public:
			UniformFractureConfigurator() = delete;
			/// Allowing for full tensor to be specified
			UniformFractureConfigurator(const csmp::TensorVariable<3>&, double);
			/// Spherical tensor, i.e. isotropic transmissivity
			UniformFractureConfigurator(double, double);

			virtual bool configure(Model&) const;

		protected:			
			void input_am(Model&, const std::vector<Element<3>*>&, double) const;
			void project_ah(Model&, const std::vector<Element<3>*>&, const csmp::TensorVariable<3>&) const;
			void direct_ah(Model&, const std::vector<Element<3>*>&, const csmp::TensorVariable<3>&) const;

			const bool project_; ///< Hydraulic aperture is isotropic (only ah_xx and ah_yy set) and needs projection
			const double am_;
			const csmp::TensorVariable<3> ah_;
		};

		/** \class UniformFractureConfigurator

		Configures (3D model) equidimensional elements to a uniform `hydraulic aperture` and `mechanical aperture` and corresponding
		`conductivity` (m3/Pa.s) and `permeability` (m2) Depending on the chosen ctor, either a provided tensor will be assigned to all,
		or an isotropic hydraulic aperture will be  assigned by projecting the appropriate tensor. The latter requires and element-wise 
		computation of the unit normal. This is expensive.

		The following variables are initialized by the provided configurators:

			hydraulic aperture	hy	m	3	0	1	ELEMENT
			mechanical aperture	me	m	1	0	1	ELEMENT
			conductivity	co	m2 Pa-1 s-1	3	1e-25	1	ELEMENT
			permeability	pe	m2	3	1e-25	1e-08	ELEMENT
		*/

	} // !tperm
} // !csmp

#endif // !TP_UNIFFRACCONFIG_H