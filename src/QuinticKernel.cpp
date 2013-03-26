// ============================================================================
// QuinticKernel.cpp
// ============================================================================


#include <math.h>
#include <iostream>
#include "Constants.h"
#include "Dimensions.h"
#include "SphKernel.h"
using namespace std;



// ============================================================================
// QuinticKernel::QuinticKernel
// ============================================================================
QuinticKernel::QuinticKernel(int ndimaux, string KernelName)
{
#if !defined(FIXED_DIMENSIONS)
  ndim = ndimaux;
  ndimpr = (FLOAT) ndimaux;
#endif
  kernrange = (FLOAT) 3.0;
  invkernrange = onethird;
  kernrangesqd = (FLOAT) 9.0;
  if (ndim == 1) kernnorm = (FLOAT) (1.0/120.0);
  else if (ndim == 2) kernnorm = invpi*(FLOAT) (7.0/478.0);
  else if (ndim == 3) kernnorm = invpi*(FLOAT) (3.0/359.0);
}



// ============================================================================
// QuinticKernel::~QuinticKernel
// ============================================================================
QuinticKernel::~QuinticKernel()
{
}






