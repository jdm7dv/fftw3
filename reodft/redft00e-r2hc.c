/*
 * Copyright (c) 2002 Matteo Frigo
 * Copyright (c) 2002 Steven G. Johnson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* $Id: redft00e-r2hc.c,v 1.15 2003-01-09 23:16:39 stevenj Exp $ */

/* Do a REDFT00 problem via an R2HC problem, with some pre/post-processing. */

#include "reodft.h"

typedef struct {
     solver super;
} S;

typedef struct {
     plan_rdft super;
     plan *cld;
     twid *td;
     int is, os;
     uint n;
     uint vl;
     int ivs, ovs;
} P;

/* Use the trick from FFTPACK, also documented in a similar form
   by Numerical Recipes. */

static void apply(plan *ego_, R *I, R *O)
{
     P *ego = (P *) ego_;
     int is = ego->is, os = ego->os;
     uint i, n = ego->n;
     uint iv, vl = ego->vl;
     int ivs = ego->ivs, ovs = ego->ovs;
     R *W = ego->td->W;
     R *buf;
     E csum;

     buf = (R *) fftw_malloc(sizeof(R) * n, BUFFERS);

     for (iv = 0; iv < vl; ++iv, I += ivs, O += ovs) {
	  buf[0] = I[0] + I[is * n];
	  csum = I[0] - I[is * n];
	  for (i = 1; i < n - i; ++i) {
	       E a, b, apb, amb;
	       a = I[is * i];
	       b = I[is * (n - i)];
	       csum += W[2*i] * (2.0*(a - b));
	       amb = W[2*i+1] * (2.0*(a - b));
	       apb = (a + b);
	       buf[i] = apb - amb;
	       buf[n - i] = apb + amb;
	  }
	  if (i == n - i) {
	       buf[i] = 2.0 * I[is * i];
	  }
	  
	  {
	       plan_rdft *cld = (plan_rdft *) ego->cld;
	       cld->apply((plan *) cld, buf, buf);
	  }
	  
	  /* FIXME: use recursive/cascade summation for better stability? */
	  O[0] = buf[0];
	  O[os] = csum;
	  for (i = 1; i + i < n; ++i) {
	       uint k = i + i;
	       O[os * k] = buf[i];
	       O[os * (k + 1)] = O[os * (k - 1)] - buf[n - i];
	  }
	  if (i + i == n) {
	       O[os * n] = buf[i];
	  }
     }

     X(free)(buf);
}

static void awake(plan *ego_, int flg)
{
     P *ego = (P *) ego_;
     static const tw_instr redft00e_tw[] = {
          { TW_COS, 0, 1 },
          { TW_SIN, 0, 1 },
          { TW_NEXT, 1, 0 }
     };

     AWAKE(ego->cld, flg);
     X(twiddle_awake)(flg, &ego->td, redft00e_tw, 2*ego->n, 1, (ego->n+1)/2);
}

static void destroy(plan *ego_)
{
     P *ego = (P *) ego_;
     X(plan_destroy)(ego->cld);
}

static void print(plan *ego_, printer *p)
{
     P *ego = (P *) ego_;
     p->print(p, "(redft00e-r2hc-%u%v%(%p%))", ego->n + 1, ego->vl, ego->cld);
}

static int applicable0(const solver *ego_, const problem *p_)
{
     UNUSED(ego_);
     if (RDFTP(p_)) {
          const problem_rdft *p = (const problem_rdft *) p_;
          return (1
		  && p->sz->rnk == 1
		  && p->vecsz->rnk <= 1
		  && p->kind[0] == REDFT00
		  && p->sz->dims[0].n > 1  /* n == 1 is not well-defined */
	       );
     }

     return 0;
}

static int applicable(const solver *ego, const problem *p, const planner *plnr)
{
     return (!NO_UGLYP(plnr) && applicable0(ego, p));
}

static plan *mkplan(const solver *ego_, const problem *p_, planner *plnr)
{
     P *pln;
     const problem_rdft *p;
     plan *cld;
     R *buf;
     uint n;

     static const plan_adt padt = {
	  X(rdft_solve), awake, print, destroy
     };

     if (!applicable(ego_, p_, plnr))
          return (plan *)0;

     p = (const problem_rdft *) p_;

     n = p->sz->dims[0].n - 1;
     A(n > 0);
     buf = (R *) fftw_malloc(sizeof(R) * n, BUFFERS);

     cld = X(mkplan_d)(plnr, X(mkproblem_rdft_1_d)(X(mktensor_1d)(n, 1, 1), 
						   X(mktensor_0d)(), 
						   buf, buf, R2HC));
     X(free)(buf);
     if (!cld)
          return (plan *)0;

     pln = MKPLAN_RDFT(P, &padt, apply);

     pln->n = n;
     pln->is = p->sz->dims[0].is;
     pln->os = p->sz->dims[0].os;
     pln->cld = cld;
     pln->td = 0;

     X(tensor_tornk1)(p->vecsz, &pln->vl, &pln->ivs, &pln->ovs);
     
     pln->super.super.ops = cld->ops;
     /* FIXME */

     return &(pln->super.super);
}

/* constructor */
static solver *mksolver(void)
{
     static const solver_adt sadt = { mkplan };
     S *slv = MKSOLVER(S, &sadt);
     return &(slv->super);
}

void X(redft00e_r2hc_register)(planner *p)
{
     REGISTER_SOLVER(p, mksolver());
}
