//=================================================================================================
//  MfvMusclSimulation.cpp
//  Contains all main functions controlling SPH simulation work-flow.
//
//  This file is part of GANDALF :
//  Graphical Astrophysics code for N-body Dynamics And Lagrangian Fluids
//  https://github.com/gandalfcode/gandalf
//  Contact : gandalfcode@gmail.com
//
//  Copyright (C) 2013  D. A. Hubber, G. Rosotti
//
//  GANDALF is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 2 of the License, or
//  (at your option) any later version.
//
//  GANDALF is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  General Public License (http://www.gnu.org/licenses) for more details.
//=================================================================================================


#include <iostream>
#include <iomanip>
#include <ostream>
#include <fstream>
#include <sstream>
#include <string>
#include <math.h>
#include <time.h>
#include <cstdio>
#include <cstring>
#include "Precision.h"
#include "CodeTiming.h"
#include "Exception.h"
#include "Debug.h"
#include "InlineFuncs.h"
#include "Simulation.h"
#include "Parameters.h"
#include "Nbody.h"
#include "RandomNumber.h"
#include "Sph.h"
#include "RiemannSolver.h"
#include "Ghosts.h"
#include "Sinks.h"
using namespace std;



//=================================================================================================
//  MfvMusclSimulation::MainLoop
/// Main SPH simulation integration loop.
//=================================================================================================
template <int ndim>
void MfvMusclSimulation<ndim>::MainLoop(void)
{
  //int activecount = 0;                 // Flag if we need to recompute particles
  int i;                               // Particle loop counter
  //int it;                              // Time-symmetric iteration counter
  int k;                               // Dimension counter
  FLOAT tghost;                        // Approx. ghost particle lifetime

  debug2("[MfvMusclSimulation:MainLoop]");


  // Compute timesteps for all particles
  if (Nlevels == 1) {
    this->ComputeGlobalTimestep();
  }
  else {
    this->ComputeBlockTimesteps();
  }

#ifdef MPI_PARALLEL

  // Pruned trees are used only to compute which particles to export
  // Therefore we don't need to update them at the start of the loop, and we can do it soon before we need them
  if (Nsteps%ntreebuildstep == 0 || rebuild_tree) {
	  mfvneib->BuildPrunedTree(rank, simbox, mpicontrol->mpinode, mfv);
  }
  else {
	  mfvneib->StockPrunedTree(rank, mfv);
  }

  if (mfv->hydro_forces) {
    mfvneib->UpdateHydroExportList(rank, mfv, nbody, simbox);

    mpicontrol->ExportParticlesBeforeForceLoop(mfv);
  }
#endif
  // Update the numerical fluxes of all active particles
  if (mfv->hydro_forces) {
    mfvneib->UpdateGodunovFluxes(timestep, mfv, nbody, simbox);
  }

#ifdef MPI_PARALLEL
  if (mfv->hydro_forces) mpicontrol->GetExportedParticlesAccelerations(mfv);
#endif
  // Advance all global time variables
  n++;
  Nsteps++;
  t = t + timestep;
  if (n == nresync) Nblocksteps++;
  if (n%integration_step == 0) Nfullsteps++;


  // Integrate positions of particles
  mfv->IntegrateParticles(n, mfv->Nhydro, t, timestep, simbox, mfv->GetMeshlessFVParticleArray());

  // Advance N-body particle positions
  nbody->AdvanceParticles(n, nbody->Nnbody, t, timestep, nbody->nbodydata);

#ifdef MPI_PARALLEL
  if (Nsteps%ntreebuildstep == 0 || rebuild_tree) {
	// Horrible hack in order NOT to trigger a full tree rebuild
	mfvneib->BuildTree(rebuild_tree,Nsteps+1,2, ntreestockstep,timestep,mfv);
	if (rebuild_tree) {
		  mfvneib->BuildPrunedTree(rank, simbox, mpicontrol->mpinode, mfv);
	}
	else {
		mfvneib->StockPrunedTree(rank, mfv);
	}
	mpicontrol->UpdateAllBoundingBoxes(mfv->Nhydro, mfv, mfv->kernp);
    mpicontrol->LoadBalancing(mfv, nbody);
  }
#endif

  // Re-build/re-stock tree now particles have moved
  mfvneib->BuildTree(rebuild_tree, Nsteps, ntreebuildstep, ntreestockstep, timestep, mfv);
#ifdef MPI_PARALLEL
  mfvneib->InitialiseCellWorkCounters();
#endif

	//tghost = timestep*(FLOAT) (ntreebuildstep - 1);
	tghost = 0;
	mfvneib->SearchBoundaryGhostParticles(tghost, simbox, mfv);
	mfvneib->BuildGhostTree(true, Nsteps, ntreebuildstep, ntreestockstep,timestep, mfv);
	#ifdef MPI_PARALLEL
	mpicontrol->UpdateAllBoundingBoxes(mfv->Nhydro + mfv->NPeriodicGhost, mfv, mfv->kernp);
	MpiGhosts->SearchGhostParticles(tghost, simbox, mfv);
	  mfvneib->BuildMpiGhostTree(true, Nsteps, ntreebuildstep, ntreestockstep,  timestep, mfv);
	#endif


  // Search for new sink particles (if activated) and accrete to existing sinks
  if (sink_particles == 1) {
    if (sinks->create_sinks == 1 && (rebuild_tree || Nfullsteps%ntreebuildstep == 0)) {
      sinks->SearchForNewSinkParticles(n, t, mfv, nbody);
    }
    if (sinks->Nsink > 0) {
      mfv->mmean = (FLOAT) 0.0;
      for (i=0; i<mfv->Nhydro; i++) mfv->mmean += mfv->GetMeshlessFVParticlePointer(i).m;
      mfv->mmean /= (FLOAT) mfv->Nhydro;
      mfv->hmin_sink = big_number;
      for (i=0; i<sinks->Nsink; i++) {
        mfv->hmin_sink = min(mfv->hmin_sink, (FLOAT) sinks->sink[i].star->h);
      }
      sinks->AccreteMassToSinks(n, timestep, mfv, nbody);
      nbody->UpdateStellarProperties();
      //if (extra_sink_output) WriteExtraSinkOutput();
    }
    // If we will output a snapshot (regular or for restarts), then delete all accreted particles
    if ((t >= tsnapnext && sinks->Nsink > 0) || n == nresync || kill_simulation ||
         timing->RunningTime()  > (FLOAT) 0.99*tmax_wallclock) {
      hydro->DeleteDeadParticles();
      rebuild_tree = true;
    }

    // Re-build/re-stock tree now particles have moved
    mfvneib->BuildTree(rebuild_tree, Nsteps, ntreebuildstep, ntreestockstep,timestep, mfv);
    mfv->CopyDataToGhosts(simbox);
    mfvneib->BuildGhostTree(rebuild_tree, Nsteps, ntreebuildstep, ntreestockstep, timestep, mfv);
#ifdef MPI_PARALLEL
	MpiGhosts->CopyHydroDataToGhosts(simbox,mfv);
    mfvneib->BuildMpiGhostTree(rebuild_tree, Nsteps, ntreebuildstep, ntreestockstep, timestep, mfv);
#endif

  }



  // Calculate terms due to self-gravity / stars
  if (mfv->self_gravity == 1 || nbody->Nnbody > 0) {
    // Update the density to get the correct softening & grad-h terms.
    mfvneib->UpdateAllProperties(mfv, nbody, simbox);
    mfv->CopyDataToGhosts(simbox);
#ifdef MPI_PARALLEL
    if (mfv->self_gravity ==1 ) {
      if (Nsteps%ntreebuildstep == 0 || rebuild_tree) {
    	     mfvneib->BuildPrunedTree(rank, simbox, mpicontrol->mpinode, mfv);
    	}
      else {
    		 mfvneib->StockPrunedTree(rank, mfv);
      }
      mfvneib->UpdateGravityExportList(rank, mfv, nbody, simbox);
      mpicontrol->ExportParticlesBeforeForceLoop(mfv);
    }
#endif
    // Does only the star forces in mfv->self_gravity != 1
    mfvneib->UpdateAllGravForces(mfv, nbody, simbox, ewald);
#ifdef MPI_PARALLEL
    if (mfv->self_gravity ==1 ) {
    mpicontrol->GetExportedParticlesAccelerations(mfv);
    }
#endif
  }

  // Compute N-body forces
  //-----------------------------------------------------------------------------------------------
  if (nbody->Nnbody > 0) {

    // Zero all acceleration terms
    for (i=0; i<nbody->Nnbody; i++) {
      if (nbody->nbodydata[i]->active) {
        for (k=0; k<ndim; k++) nbody->nbodydata[i]->a[k]     = (FLOAT) 0.0;
        for (k=0; k<ndim; k++) nbody->nbodydata[i]->adot[k]  = (FLOAT) 0.0;
        for (k=0; k<ndim; k++) nbody->nbodydata[i]->a2dot[k] = (FLOAT) 0.0;
        for (k=0; k<ndim; k++) nbody->nbodydata[i]->a3dot[k] = (FLOAT) 0.0;
        nbody->nbodydata[i]->gpot = (FLOAT) 0.0;
        nbody->nbodydata[i]->gpe = (FLOAT) 0.0;
      }
    }

    if (mfv->self_gravity == 1 && mfv->Nhydro > 0) {
      mfvneib->UpdateAllStarGasForces(mfv, nbody);
#if defined MPI_PARALLEL
      // We need to sum up the contributions from the different domains
      mpicontrol->ComputeTotalStarGasForces(nbody);
#endif
    }

    // Calculate forces, force derivatives etc.., for active stars/systems
    if (nbody->nbody_softening == 1) {
      nbody->CalculateDirectSmoothedGravForces(nbody->Nnbody, nbody->nbodydata);
    }
    else {
      nbody->CalculateDirectGravForces(nbody->Nnbody, nbody->nbodydata);
    }

    for (i=0; i<nbody->Nnbody; i++) {
      if (nbody->nbodydata[i]->active) {
        nbody->extpot->AddExternalPotential(nbody->nbodydata[i]->r, nbody->nbodydata[i]->v,
                                            nbody->nbodydata[i]->a, nbody->nbodydata[i]->adot,
                                            nbody->nbodydata[i]->gpot);
      }
    }

    // Calculate correction step for all stars at end of step.
    nbody->CorrectionTerms(n, nbody->Nnbody, t, timestep, nbody->nbodydata);

  }
  //-----------------------------------------------------------------------------------------------


  // End-step terms for all hydro particles
  mfv->EndTimestep(n, mfv->Nhydro, t, timestep, mfv->GetMeshlessFVParticleArray());
  nbody->EndTimestep(n, nbody->Nnbody, t, timestep, nbody->nbodydata);

  // Update all active cell counters in the tree
  mfvneib->UpdateActiveParticleCounters(mfv);

  //Calculate all properties (and copy updated data to ghost particles)
  mfvneib->UpdateAllProperties(mfv, nbody, simbox);

#ifdef MPI_PARALLEL
  mfvneib->UpdateAllProperties(mfv, nbody, simbox);
  mfv->CopyDataToGhosts(simbox);
  MpiGhosts->CopyHydroDataToGhosts(simbox,mfv);
#endif


  // Calculate all matrices and gradients (and copy updated data to ghost particles)
  // TODO:
  //   Compute gradients for all cells neighbouring active ones (use levelneib?).
  mfvneib->UpdateGradientMatrices(mfv, nbody, simbox);

  /* Check that we have sensible smoothing lengths */
  if (periodicBoundaries) {
    double hmax = mfvneib->GetMaximumSmoothingLength() ;
    hmax *= mfv->kernp->kernrange ;
    for (i=0; i < ndim; i++)
      if (simbox.half[i] < 2*hmax){
        string message = "Error: Smoothing length too large, self-interaction will occur" ;
    	ExceptionHandler::getIstance().raise(message);
      }
  }

  return;
}



// Create template class instances of the main MfvMusclSimulation object for
// each dimension used (1, 2 and 3)
template class MfvMusclSimulation<1>;
template class MfvMusclSimulation<2>;
template class MfvMusclSimulation<3>;
