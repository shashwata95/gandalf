// ============================================================================
// GradhSph.cpp
// Contains all functions for calculating conservative 'grad-h' SPH quantities
// (See Springel & Hernquist (2002) and Price & Monaghan (2007).
// ============================================================================


#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <math.h>
#include "Precision.h"
#include "Exception.h"
#include "Sph.h"
#include "SphKernel.h"
#include "SphParticle.h"
#include "Parameters.h"
#include "EOS.h"
#include "InlineFuncs.h"
using namespace std;



// ============================================================================
// GradhSph::GradhSph
// ============================================================================
template <typename kernelclass>
GradhSph<kernelclass>::GradhSph(int ndimaux, int vdimaux, int bdimaux, int hydro_forces_aux,
	    int self_gravity_aux, FLOAT alpha_visc_aux, FLOAT beta_visc_aux,
	    FLOAT h_fac_aux, FLOAT h_converge_aux, aviscenum avisc_aux,
	    acondenum acond_aux, string gas_eos_aux, string KernelName):
  Sph(ndimaux, vdimaux, bdimaux, hydro_forces_aux,
		    self_gravity_aux, alpha_visc_aux, beta_visc_aux,
		    h_fac_aux, h_converge_aux, avisc_aux,
		    acond_aux, gas_eos_aux, KernelName),
  kern(kernelclass(ndimaux, KernelName))
{
  allocated = false;
  Nsph = 0;
  Nsphmax = 0;
  kernp = &kern;
}



// ============================================================================
// GradhSph::~GradhSph
// ============================================================================
template <typename kernelclass>
GradhSph<kernelclass>::~GradhSph()
{
}



// ============================================================================
// GradhSph::ComputeH
// Compute the value of the smoothing length of particle 'i' by iterating  
// the relation : h = h_fac*(m/rho)^(1/ndim).
// Uses the previous value of h as a starting guess and then uses either 
// a Newton-Rhapson solver, or fixed-point iteration, to converge on the 
// correct value of h.  The maximum tolerance used for deciding whether the 
// iteration has converged is given by the 'h_converge' parameter.
// ============================================================================
template <typename kernelclass>
int GradhSph<kernelclass>::ComputeH
(int i,                                 // id of particle
 int Nneib,                             // No. of potential neighbours
 FLOAT *m,                              // Array of neib. masses
 FLOAT *mu,                             // Array of m*u (not needed here)
 FLOAT *drsqd,                          // Array of neib. distances (squared)
 SphParticle &parti)                    // Particle i data
{
  int j;                                // Neighbour id
  int jj;                               // Aux. neighbour counter
  int k;                                // Dimension counter
  int iteration = 0;                    // h-rho iteration counter
  int iteration_max = 30;               // Max. no of iterations
  FLOAT h_max = big_number;             // Max. allowed value of h
  FLOAT h_lower_bound = 0.0;            // Lower bound on h
  FLOAT h_upper_bound = big_number;     // Upper bound on h
  FLOAT invhsqd;                        // (1 / h)^2
  FLOAT ssqd;                           // Kernel parameter squared, (r/h)^2


  // Main smoothing length iteration loop
  // ==========================================================================
  do {

    // Initialise all variables for this value of h
    iteration++;
    parti.invh = (FLOAT) 1.0/parti.h;
    parti.rho = (FLOAT) 0.0;
    parti.invomega = (FLOAT) 0.0;
    parti.zeta = (FLOAT) 0.0;
    parti.hfactor = pow(parti.invh,ndim);
    invhsqd = parti.invh*parti.invh;

    // Loop over all nearest neighbours in list to calculate 
    // density, omega and zeta.
    // ------------------------------------------------------------------------
    for (j=0; j<Nneib; j++) {
      ssqd = drsqd[j]*invhsqd;
      parti.rho += m[j]*parti.hfactor*kern.w0_s2(ssqd);
      parti.invomega += m[j]*parti.hfactor*parti.invh*kern.womega_s2(ssqd);
      parti.zeta += m[j]*invhsqd*kern.wzeta_s2(ssqd);
    }
    // ------------------------------------------------------------------------

    if (parti.rho > (FLOAT) 0.0) parti.invrho = (FLOAT) 1.0/parti.rho;

    // If h changes below some fixed tolerance, exit iteration loop
    if (parti.rho > (FLOAT) 0.0 && parti.h > h_lower_bound &&
    		fabs(parti.h - h_fac*pow(parti.m*parti.invrho,
    				invndim)) < h_converge) break;

    // Use fixed-point iteration, i.e. h_new = h_fac*(m/rho_old)^(1/ndim), 
    // for now.  If this does not converge in a reasonable number of 
    // iterations (iteration_max), then assume something is wrong and switch 
    // to a bisection method, which should be guaranteed to converge, 
    // albeit much more slowly.  (N.B. will implement Newton-Raphson soon)
    // ------------------------------------------------------------------------
    if (iteration < iteration_max)
      parti.h = h_fac*pow(parti.m*parti.invrho,invndim);

    else if (iteration == iteration_max)
      parti.h = (FLOAT) 0.5*(h_lower_bound + h_upper_bound);

    else if (iteration < 5*iteration_max) {
      if (parti.rho < small_number ||
	  parti.rho*pow(parti.h,ndim) > pow(h_fac,ndim)*parti.m)
	h_upper_bound = parti.h;
      else 
	h_lower_bound = parti.h;
      parti.h = (FLOAT) 0.5*(h_lower_bound + h_upper_bound);
    }

    else {
      string message = "Problem with convergence of h-rho iteration";
      ExceptionHandler::getIstance().raise(message);
    }

    // If the smoothing length is too large for the neighbour list, exit 
    // routine and flag neighbour list error in order to generate a larger
    // neighbour list (not properly implemented yet).
    if (parti.h > h_max) return 0;
    
  } while (parti.h > h_lower_bound && parti.h < h_upper_bound);
  // ==========================================================================


  // Normalise all SPH sums correctly
  parti.h = h_fac*pow(parti.m*parti.invrho,invndim);
  parti.invh = (FLOAT) 1.0/parti.h;
  parti.invomega = (FLOAT) 1.0 + invndim*parti.h*parti.invomega*parti.invrho;
  parti.invomega = (FLOAT) 1.0/parti.invomega;
  parti.zeta = -invndim*parti.h*parti.zeta*parti.invrho*parti.invomega;

  // Set important thermal variables here
  parti.u = eos->SpecificInternalEnergy(parti);
  parti.sound = eos->SoundSpeed(parti);
  parti.hfactor = pow(parti.invh,ndim+1);
  parti.pfactor = eos->Pressure(parti)*parti.invrho*
    parti.invrho*parti.invomega;
  parti.div_v = (FLOAT) 0.0;
  

  return 1;
}



// ============================================================================
// GradhSph::ComputeSphNeibForces
// Compute SPH neighbour force pairs for 
// (i) All neighbour interactions of particle i with i.d. j > i,
// (ii) Active neighbour interactions of particle j with i.d. j > i
// (iii) All inactive neighbour interactions of particle i with i.d. j < i.
// This ensures that all particle-particle pair interactions are only 
// computed once only for efficiency.
// ============================================================================
template <typename kernelclass>
void GradhSph<kernelclass>::ComputeSphNeibForces
(int i,                                 // id of particle
 int Nneib,                             // No. of neighbours in neibpart array
 int *neiblist,                         // id of gather neighbour in neibpart
 FLOAT *drmag,                          // Distances of gather neighbours
 FLOAT *invdrmag,                       // Inverse distances of gather neibs
 FLOAT *dr,                             // Position vector of gather neibs
 SphParticle &parti,                    // Particle i data
 SphParticle *neibpart)                 // Neighbour particle data
{
  int j;                                // Neighbour list id
  int jj;                               // Aux. neighbour counter
  int k;                                // Dimension counter
  FLOAT draux[ndimmax];                 // Relative position vector
  FLOAT dv[ndimmax];                    // Relative velocity vector
  FLOAT dvdr;                           // Dot product of dv and dr
  FLOAT wkerni;                         // Value of w1 kernel function
  FLOAT wkernj;                         // Value of w1 kernel function
  FLOAT vsignal;                        // Signal velocity
  FLOAT paux;                           // Aux. pressure force variable
  FLOAT uaux;                           // Aux. internal energy variable
  FLOAT winvrho;                        // 0.5*(wkerni + wkernj)*invrhomean


  // Compute hydro forces
  // ==========================================================================
  if (hydro_forces == 1) {

    // Loop over all potential neighbours in the list
    // ------------------------------------------------------------------------
    for (jj=0; jj<Nneib; jj++) {
      j = neiblist[jj];
      wkerni = parti.hfactor*kern.w1(drmag[jj]*parti.invh);
      wkernj = neibpart[j].hfactor*kern.w1(drmag[jj]*neibpart[j].invh);

      for (k=0; k<ndim; k++) draux[k] = dr[jj*ndim + k];
      for (k=0; k<ndim; k++) dv[k] = neibpart[j].v[k] - parti.v[k];
      dvdr = DotProduct(dv,draux,ndim);

      // Add contribution to velocity divergence
      parti.div_v -= neibpart[j].m*dvdr*wkerni;
      neibpart[j].div_v -= parti.m*dvdr*wkernj;

      // Main SPH pressure force term
      paux = parti.pfactor*wkerni + neibpart[j].pfactor*wkernj;
      
      // Add dissipation terms (for approaching particle pairs)
      // ----------------------------------------------------------------------
      if (dvdr < (FLOAT) 0.0) {

    	winvrho = (FLOAT) 0.25*(wkerni + wkernj)*
	  (parti.invrho + neibpart[j].invrho);
	
        // Artificial viscosity term
        if (avisc == mon97) {
          vsignal = parti.sound + neibpart[j].sound - beta_visc*dvdr;
          paux -= (FLOAT) alpha_visc*vsignal*dvdr*winvrho;
          uaux = (FLOAT) 0.5*alpha_visc*vsignal*dvdr*dvdr*winvrho;
          parti.dudt -= neibpart[j].m*uaux;
          neibpart[j].dudt -= parti.m*uaux;
        }

        // Artificial conductivity term
        if (acond == wadsley2008) {
	      uaux = (FLOAT) 0.5*dvdr*(neibpart[j].u - parti.u)*
	    		  (parti.invrho*wkerni + neibpart[j].invrho*wkernj);
	      parti.dudt += neibpart[j].m*uaux;
	      neibpart[j].dudt -= parti.m*uaux;
        }
        else if (acond == price2008) {
    	  vsignal = sqrt(fabs(eos->Pressure(parti) -
			      eos->Pressure(neibpart[j]))*0.5*
			 (parti.invrho + neibpart[j].invrho));
          parti.dudt += 0.5*neibpart[j].m*vsignal*
            (parti.u - neibpart[j].u)*winvrho;
          neibpart[j].dudt -= 0.5*parti.m*vsignal*
            (parti.u - neibpart[j].u)*winvrho;
        }
	
      }
      // ----------------------------------------------------------------------

      // Add total hydro contribution to acceleration for particle i
      for (k=0; k<ndim; k++) parti.a[k] += neibpart[j].m*draux[k]*paux;
      
      // If neighbour is also active, add contribution to force here
      for (k=0; k<ndim; k++) neibpart[j].a[k] -= parti.m*draux[k]*paux;

    }
    // ------------------------------------------------------------------------

  }
  // ==========================================================================

   // Compute gravitational contribution
    // ------------------------------------------------------------------------
    /*if (self_gravity == 1) {
      paux = (FLOAT) 0.5*
	(parti.invh*parti.invh*kern.wgrav(drmag[j]*parti.invh) +
	 parti.zeta*wkern - invdrmag[j]*invdrmag[j]);
      for (k=0; k<ndim; k++) parti.agrav[k] += neibpart[j].m*draux[k]*paux;
      parti.gpot += neibpart[j].m*parti.invh*wpot(drmag[j].parti.invh);
      }*/

  return;
}



// ============================================================================
// GradhSph::ComputeSphNeibDudt
// Empty definition
// ============================================================================
template <typename kernelclass>
void GradhSph<kernelclass>::ComputeSphNeibDudt
(int i, int Nneib, int *neiblist, FLOAT *drmag, 
 FLOAT *invdrmag, FLOAT *dr, SphParticle &parti, SphParticle *neibpart)
{
  return;
}



// ============================================================================
// GradhSph::ComputeSphDerivatives
// Empty definition
// ============================================================================
template <typename kernelclass>
void GradhSph<kernelclass>::ComputeSphDerivatives
(int i, int Nneib, int *neiblist, FLOAT *drmag, 
 FLOAT *invdrmag, FLOAT *dr, SphParticle &parti, SphParticle *neibpart)
{
  return;
}



// ============================================================================
// GradhSph::ComputePostHydroQuantities
// ..
// ============================================================================
template <typename kernelclass>
void GradhSph<kernelclass>::ComputePostHydroQuantities
(SphParticle &parti)
{
  parti.div_v *= parti.invrho;
  parti.dudt -= eos->Pressure(parti)*parti.div_v*parti.invrho*parti.invomega;

  return;
}



// ============================================================================
// GradhSph::ComputeGravForces
// Compute the contribution to the total gravitational force of particle 'i' 
// due to 'Nneib' neighbouring particles in the list 'neiblist'.
// ============================================================================
template <typename kernelclass>
void GradhSph<kernelclass>::ComputeDirectGravForces
(int i,                                 // id of particle
 int Ndirect,                           // No. of nearby 'gather' neighbours
 int *directlist,                       // id of gather neighbour in neibpart
 SphParticle &parti,                    // Particle i data
 SphParticle *sph)                      // Neighbour particle data
{
  int j;                                // ..
  int jj;                               // ..
  int k;                                // ..
  FLOAT dr[ndimmax];                    // ..
  FLOAT drsqd;                          // ..
  FLOAT invdrmag;                       // ..

  // Loop over all neighbouring particles in list
  // --------------------------------------------------------------------------
  for (jj=0; jj<Ndirect; jj++) {
    j = directlist[jj];

    for (k=0; k<ndim; k++) dr[k] = sph[j].r[k] - parti.r[k];
    drsqd = DotProduct(dr,dr,ndim);
    invdrmag = 1.0/(sqrt(drsqd) + small_number);

    parti.gpot -= sph[j].m*invdrmag;
    for (k=0; k<ndim; k++) 
      parti.agrav[k] += sph[j].m*dr[ndim*jj + k]*pow(invdrmag,3);

  }

  return;
}



template class GradhSph<M4Kernel>;
template class GradhSph<QuinticKernel>;
template class GradhSph<GaussianKernel>;
template class GradhSph<TabulatedKernel>;
