/* Copyright (c) 2018, 2020, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2018, Cavium. All rights reserved. (By BELLSOFT)
 * Copyright (c) 2022, 2024, Loongson Technology. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "macroAssembler_loongarch.hpp"

#define T0 RT0
#define T1 RT1
#define T2 RT2
#define T3 RT3
#define T4 RT4
#define T5 RT5
#define T6 RT6
#define T7 RT7
#define T8 RT8

// The following code is a optimized version of fdlibm sin/cos implementation
// (C code is in share/runtime/sharedRuntimeTrig.cpp) adapted for LOONGARCH64.

// Please refer to sin/cos approximation via polynomial and
// trigonometric argument reduction techniques to the following literature:
//
// [1] Muller, Jean-Michel, Nicolas Brisebarre, Florent De Dinechin,
// Claude-Pierre Jeannerod, Vincent Lefevre, Guillaume Melquiond,
// Nathalie Revol, Damien Stehlé, and Serge Torres:
// Handbook of floating-point arithmetic.
// Springer Science & Business Media, 2009.
// [2] K. C. Ng
// Argument Reduction for Huge Arguments: Good to the Last Bit
// July 13, 1992, SunPro
//
// HOW TO READ THIS CODE:
// This code consists of several functions. Each function has following header:
// 1) Description
// 2) C-pseudo code with differences from fdlibm marked by comments starting
//        with "NOTE". Check unmodified fdlibm code in
//        share/runtime/SharedRuntimeTrig.cpp
// 3) Brief textual description of changes between fdlibm and current
//        implementation along with optimization notes (if applicable)
// 4) Assumptions, input and output
// 5) (Optional) additional notes about intrinsic implementation
// Each function is separated in blocks which follow the pseudo-code structure
//
// HIGH-LEVEL ALGORITHM DESCRIPTION:
//    - entry point: generate_dsin_dcos(...);
//    - check corner cases: NaN, INF, tiny argument.
//    - check if |x| < Pi/4. Then approximate sin/cos via polynomial (kernel_sin/kernel_cos)
//    -- else proceed to argument reduction routine (__ieee754_rem_pio2) and
//           use reduced argument to get result via kernel_sin/kernel_cos
//
// HIGH-LEVEL CHANGES BETWEEN INTRINSICS AND FDLIBM:
// 1) two_over_pi table fdlibm representation is int[], while intrinsic version
// has these int values converted to double representation to load converted
// double values directly (see stubRoutines_aarch4::_two_over_pi)
// 2) Several loops are unrolled and vectorized: see comments in code after
// labels: SKIP_F_LOAD, RECOMP_FOR1_CHECK, RECOMP_FOR2
// 3) fdlibm npio2_hw table now has "prefix" with constants used in
// calculation. These constants are loaded from npio2_hw table instead of
// constructing it in code (see stubRoutines_loongarch64.cpp)
// 4) Polynomial coefficients for sin and cos are moved to table sin_coef
// and cos_coef to use the same optimization as in 3). It allows to load most of
// required constants via single instruction
//
//
//
///* __ieee754_rem_pio2(x,y)
// *
// * returns the remainder of x rem pi/2 in y[0]+y[1] (i.e. like x div pi/2)
// * x is input argument, y[] is hi and low parts of reduced argument (x)
// * uses __kernel_rem_pio2()
// */
// // use tables(see stubRoutines_loongarch64.cpp): two_over_pi and modified npio2_hw
//
// BEGIN __ieee754_rem_pio2 PSEUDO CODE
//
//static int __ieee754_rem_pio2(double x, double *y) {
//  double z,w,t,r,fn;
//  double tx[3];
//  int e0,i,j,nx,n,ix,hx,i0;
//
//  i0 = ((*(int*)&two24A)>>30)^1;        /* high word index */
//  hx = *(i0+(int*)&x);          /* high word of x */
//  ix = hx&0x7fffffff;
//  if(ix<0x4002d97c) {  /* |x| < 3pi/4, special case with n=+-1 */
//    if(hx>0) {
//      z = x - pio2_1;
//      if(ix!=0x3ff921fb) {    /* 33+53 bit pi is good enough */
//        y[0] = z - pio2_1t;
//        y[1] = (z-y[0])-pio2_1t;
//      } else {                /* near pi/2, use 33+33+53 bit pi */
//        z -= pio2_2;
//        y[0] = z - pio2_2t;
//        y[1] = (z-y[0])-pio2_2t;
//      }
//      return 1;
//    } else {    /* negative x */
//      z = x + pio2_1;
//      if(ix!=0x3ff921fb) {    /* 33+53 bit pi is good enough */
//        y[0] = z + pio2_1t;
//        y[1] = (z-y[0])+pio2_1t;
//      } else {                /* near pi/2, use 33+33+53 bit pi */
//        z += pio2_2;
//        y[0] = z + pio2_2t;
//        y[1] = (z-y[0])+pio2_2t;
//      }
//      return -1;
//    }
//  }
//  if(ix<=0x413921fb) { /* |x| ~<= 2^19*(pi/2), medium size */
//    t  = fabsd(x);
//    n  = (int) (t*invpio2+half);
//    fn = (double)n;
//    r  = t-fn*pio2_1;
//    w  = fn*pio2_1t;    /* 1st round good to 85 bit */
//    // NOTE: y[0] = r-w; is moved from if/else below to be before "if"
//    y[0] = r-w;
//    if(n<32&&ix!=npio2_hw[n-1]) {
//      // y[0] = r-w;       /* quick check no cancellation */ // NOTE: moved earlier
//    } else {
//      j  = ix>>20;
//      // y[0] = r-w; // NOTE: moved earlier
//      i = j-(((*(i0+(int*)&y[0]))>>20)&0x7ff);
//      if(i>16) {  /* 2nd iteration needed, good to 118 */
//        t  = r;
//        w  = fn*pio2_2;
//        r  = t-w;
//        w  = fn*pio2_2t-((t-r)-w);
//        y[0] = r-w;
//        i = j-(((*(i0+(int*)&y[0]))>>20)&0x7ff);
//        if(i>49)  {     /* 3rd iteration need, 151 bits acc */
//          t  = r;       /* will cover all possible cases */
//          w  = fn*pio2_3;
//          r  = t-w;
//          w  = fn*pio2_3t-((t-r)-w);
//          y[0] = r-w;
//        }
//      }
//    }
//    y[1] = (r-y[0])-w;
//    if(hx<0)    {y[0] = -y[0]; y[1] = -y[1]; return -n;}
//    else         return n;
//  }
//  /*
//   * all other (large) arguments
//   */
//  // NOTE: this check is removed, because it was checked in dsin/dcos
//  // if(ix>=0x7ff00000) {          /* x is inf or NaN */
//  //  y[0]=y[1]=x-x; return 0;
//  // }
//  /* set z = scalbn(|x|,ilogb(x)-23) */
//  *(1-i0+(int*)&z) = *(1-i0+(int*)&x);
//  e0    = (ix>>20)-1046;        /* e0 = ilogb(z)-23; */
//  *(i0+(int*)&z) = ix - (e0<<20);
//
//  // NOTE: "for" loop below in unrolled. See comments in asm code
//  for(i=0;i<2;i++) {
//    tx[i] = (double)((int)(z));
//    z     = (z-tx[i])*two24A;
//  }
//
//  tx[2] = z;
//  nx = 3;
//
//  // NOTE: while(tx[nx-1]==zeroA) nx--;  is unrolled. See comments in asm code
//  while(tx[nx-1]==zeroA) nx--;  /* skip zero term */
//
//  n  =  __kernel_rem_pio2(tx,y,e0,nx,2,two_over_pi);
//  if(hx<0) {y[0] = -y[0]; y[1] = -y[1]; return -n;}
//  return n;
//}
//
// END __ieee754_rem_pio2 PSEUDO CODE
//
// Changes between fdlibm and intrinsic for __ieee754_rem_pio2:
//     1. INF/NaN check for huge argument is removed in comparison with fdlibm
//     code, because this check is already done in dcos/dsin code
//     2. Most constants are now loaded from table instead of direct initialization
//     3. Two loops are unrolled
// Assumptions:
//     1. Assume |X| >= PI/4
//     2. Assume SCR1 = 0x3fe921fb00000000  (~ PI/4)
//     3. Assume ix = A3
// Input and output:
//     1. Input: X = A0
//     2. Return n in A2, y[0] == y0 == FA4, y[1] == y1 == FA5
// NOTE: general purpose register names match local variable names in C code
// NOTE: fpu registers are actively reused. See comments in code about their usage
void MacroAssembler::generate__ieee754_rem_pio2(address npio2_hw, address two_over_pi, address pio2) {
  const int64_t PIO2_1t = 0x3DD0B4611A626331ULL;
  const int64_t PIO2_2  = 0x3DD0B4611A600000ULL;
  const int64_t PIO2_2t = 0x3BA3198A2E037073ULL;
  Label X_IS_NEGATIVE, X_IS_MEDIUM_OR_LARGE, X_IS_POSITIVE_LONG_PI, LARGE_ELSE,
        REDUCTION_DONE, X_IS_MEDIUM_BRANCH_DONE, X_IS_LARGE, NX_SET,
        X_IS_NEGATIVE_LONG_PI;
  Register X = A0, n = A2, ix = A3, jv = A4, tmp5 = A5, jx = A6,
           tmp3 = A7, iqBase = T0, ih = T1, i = T2;
  FloatRegister v0 = FA0, v1 = FA1, v2 = FA2, v3 = FA3, v4 = FA4, v5 = FA5, v6 = FA6, v7 = FA7,
                vt = FT1, v24 = FT8, v26 = FT10, v27 = FT11, v28 = FT12, v29 = FT13, v31 = FT15;

  push2(S0, S1);

    // initializing constants first
    li(SCR1, 0x3ff921fb54400000); // PIO2_1
    li(SCR2, 0x4002d97c); // 3*PI/4 high word
    movgr2fr_d(v1, SCR1); // v1 = PIO2_1
    bge(ix, SCR2, X_IS_MEDIUM_OR_LARGE);

    block_comment("if(ix<0x4002d97c) {...  /* |x| ~< 3pi/4 */ "); {
      blt(X, R0, X_IS_NEGATIVE);

      block_comment("if(hx>0) {"); {
        fsub_d(v2, v0, v1); // v2 = z = x - pio2_1
        srli_d(SCR1, SCR1, 32);
        li(n, 1);
        beq(ix, SCR1, X_IS_POSITIVE_LONG_PI);

        block_comment("case: hx > 0 &&  ix!=0x3ff921fb {"); { /* 33+53 bit pi is good enough */
          li(SCR2, PIO2_1t);
          movgr2fr_d(v27, SCR2);
          fsub_d(v4, v2, v27); // v4 = y[0] = z - pio2_1t;
          fsub_d(v5, v2, v4);
          fsub_d(v5, v5, v27); // v5 = y[1] = (z-y[0])-pio2_1t
          b(REDUCTION_DONE);
        }

        block_comment("case: hx > 0 &*& ix==0x3ff921fb {"); { /* near pi/2, use 33+33+53 bit pi */
          bind(X_IS_POSITIVE_LONG_PI);
            li(SCR1, PIO2_2);
            li(SCR2, PIO2_2t);
            movgr2fr_d(v27, SCR1);
            movgr2fr_d(v6, SCR2);
            fsub_d(v2, v2, v27); // z-= pio2_2
            fsub_d(v4, v2, v6);  // y[0] = z - pio2_2t
            fsub_d(v5, v2, v4);
            fsub_d(v5, v5, v6);  // v5 = (z - y[0]) - pio2_2t
            b(REDUCTION_DONE);
        }
      }

      block_comment("case: hx <= 0)"); {
        bind(X_IS_NEGATIVE);
          fadd_d(v2, v0, v1); // v2 = z = x + pio2_1
          srli_d(SCR1, SCR1, 32);
          li(n, -1);
          beq(ix, SCR1, X_IS_NEGATIVE_LONG_PI);

          block_comment("case: hx <= 0 && ix!=0x3ff921fb) {"); { /* 33+53 bit pi is good enough */
            li(SCR2, PIO2_1t);
            movgr2fr_d(v27, SCR2);
            fadd_d(v4, v2, v27); // v4 = y[0] = z + pio2_1t;
            fsub_d(v5, v2, v4);
            fadd_d(v5, v5, v27); // v5 = y[1] = (z-y[0]) + pio2_1t
            b(REDUCTION_DONE);
          }

          block_comment("case: hx <= 0 && ix==0x3ff921fb"); { /* near pi/2, use 33+33+53 bit pi */
            bind(X_IS_NEGATIVE_LONG_PI);
              li(SCR1, PIO2_2);
              li(SCR2, PIO2_2t);
              movgr2fr_d(v27, SCR1);
              movgr2fr_d(v6, SCR2);
              fadd_d(v2, v2, v27); // z += pio2_2
              fadd_d(v4, v2, v6);  // y[0] = z + pio2_2t
              fsub_d(v5, v2, v4);
              fadd_d(v5, v5, v6);  // v5 = (z - y[0]) + pio2_2t
              b(REDUCTION_DONE);
          }
      }
  }
  bind(X_IS_MEDIUM_OR_LARGE);
    li(SCR1, 0x413921fb);
    blt(SCR1, ix, X_IS_LARGE); // ix < = 0x413921fb ?

    block_comment("|x| ~<= 2^19*(pi/2), medium size"); {
      li(ih, npio2_hw);
      fld_d(v4, ih, 0);
      fld_d(v5, ih, 8);
      fld_d(v6, ih, 16);
      fld_d(v7, ih, 24);
      fabs_d(v31, v0);           // v31 = t = |x|
      addi_d(ih, ih, 64);
      fmadd_d(v2, v31, v5, v4);  // v2 = t * invpio2 + half (invpio2 = 53 bits of 2/pi, half = 0.5)
      ftintrz_w_d(vt, v2);       // n = (int) v2
      movfr2gr_s(n, vt);
      vfrintrz_d(v2, v2);
      fnmsub_d(v3, v2, v6, v31); // v3 = r = t - fn * pio2_1
      fmul_d(v26, v2, v7);       // v26 = w = fn * pio2_1t
      fsub_d(v4, v3, v26);       // y[0] = r - w. Calculated before branch
      li(SCR1, 32);
      blt(SCR1, n, LARGE_ELSE);
      addi_w(tmp5, n, -1);       // tmp5 = n - 1
      alsl_d(tmp5, tmp5, ih, 2 - 1);
      ld_w(jv, tmp5, 0);
      bne(ix, jv, X_IS_MEDIUM_BRANCH_DONE);

      block_comment("else block for if(n<32&&ix!=npio2_hw[n-1])"); {
        bind(LARGE_ELSE);
          movfr2gr_d(jx, v4);
          srli_d(tmp5, ix, 20);                    // j = ix >> 20
          slli_d(jx, jx, 1);
          srli_d(tmp3, jx, 32 + 20 + 1);           // r7 = j-(((*(i0+(int*)&y[0]))>>20)&0x7ff);
          sub_d(tmp3, tmp5, tmp3);

          block_comment("if(i>16)"); {
            li(SCR1, 16);
            bge(SCR1, tmp3, X_IS_MEDIUM_BRANCH_DONE);
            // i > 16. 2nd iteration needed
            fld_d(v6, ih, -32);
            fld_d(v7, ih, -24);
            fmov_d(v28, v3);                        // t = r
            fmul_d(v29, v2, v6);                    // w = v29 = fn * pio2_2
            fsub_d(v3, v28, v29);                   // r = t - w
            fsub_d(v31, v28, v3);                   // v31 = (t - r)
            fsub_d(v31, v29, v31);                  // v31 = w - (t - r) = - ((t - r) - w)
            fmadd_d(v26, v2, v7, v31);              // v26 = w = fn*pio2_2t - ((t - r) - w)
            fsub_d(v4, v3, v26);                    // y[0] = r - w
            movfr2gr_d(jx, v4);
            slli_d(jx, jx, 1);
            srli_d(tmp3, jx, 32 + 20 + 1);          // r7 = j-(((*(i0+(int*)&y[0]))>>20)&0x7ff);
            sub_d(tmp3, tmp5, tmp3);

            block_comment("if(i>49)"); {
              li(SCR1, 49);
              bge(SCR1, tmp3, X_IS_MEDIUM_BRANCH_DONE);
              // 3rd iteration need, 151 bits acc
              fld_d(v6, ih, -16);
              fld_d(v7, ih, -8);
              fmov_d(v28, v3);                      // save "r"
              fmul_d(v29, v2, v6);                  // v29 = fn * pio2_3
              fsub_d(v3, v28, v29);                 // r = r - w
              fsub_d(v31, v28, v3);                 // v31 = (t - r)
              fsub_d(v31, v29, v31);                // v31 = w - (t - r) = - ((t - r) - w)
              fmadd_d(v26, v2, v7, v31);            // v26 = w = fn*pio2_3t - ((t - r) - w)
              fsub_d(v4, v3, v26);                  // y[0] = r - w
            }
          }
      }
    block_comment("medium x tail"); {
      bind(X_IS_MEDIUM_BRANCH_DONE);
        fsub_d(v5, v3, v4);                         // v5 = y[1] = (r - y[0])
        fsub_d(v5, v5, v26);                        // v5 = y[1] = (r - y[0]) - w
        blt(R0, X, REDUCTION_DONE);
        fneg_d(v4, v4);
        sub_w(n, R0, n);
        fneg_d(v5, v5);
        b(REDUCTION_DONE);
    }
  }

  block_comment("all other (large) arguments"); {
    bind(X_IS_LARGE);
      srli_d(SCR1, ix, 20);                        // ix >> 20
      li(tmp5, 0x4170000000000000);
      addi_w(SCR1, SCR1, -1046);                   // e0
      movgr2fr_d(v24, tmp5);                       // init two24A value
      slli_w(jv, SCR1, 20);                        // ix - (e0<<20)
      sub_w(jv, ix, jv);
      slli_d(jv, jv, 32);
      addi_w(SCR2, SCR1, -3);
      bstrins_d(jv, X, 31, 0);                     // jv = z
      li(i, 24);
      movgr2fr_d(v26, jv);                         // v26 = z

      block_comment("unrolled for(i=0;i<2;i++) {tx[i] = (double)((int)(z));z = (z-tx[i])*two24A;}"); {
        // tx[0,1,2] = v6,v7,v26
        vfrintrz_d(v6, v26);                       // v6 = (double)((int)v26)
        div_w(jv, SCR2, i);                        // jv = (e0 - 3)/24
        fsub_d(v26, v26, v6);
        addi_d(SP, SP, -560);
        fmul_d(v26, v26, v24);
        vfrintrz_d(v7, v26);                       // v7 = (double)((int)v26)
        li(jx, 2); // calculate jx as nx - 1, which is initially 2. Not a part of unrolled loop
        fsub_d(v26, v26, v7);
      }

      block_comment("nx calculation with unrolled while(tx[nx-1]==zeroA) nx--;"); {
        vxor_v(vt, vt, vt);
        fcmp_cne_d(FCC0, v26, vt);                 // if NE then jx == 2. else it's 1 or 0
        addi_d(iqBase, SP, 480);                   // base of iq[]
        fmul_d(v3, v26, v24);
        bcnez(FCC0, NX_SET);
        fcmp_cne_d(FCC0, v7, vt);                  // v7 == 0 => jx = 0. Else jx = 1
        movcf2gr(jx, FCC0);
      }
    bind(NX_SET);
      generate__kernel_rem_pio2(two_over_pi, pio2);
      // now we have y[0] = v4, y[1] = v5 and n = r2
      bge(X, R0, REDUCTION_DONE);
      fneg_d(v4, v4);
      fneg_d(v5, v5);
      sub_w(n, R0, n);
  }
  bind(REDUCTION_DONE);

  pop2(S0, S1);
}

///*
// * __kernel_rem_pio2(x,y,e0,nx,prec,ipio2)
// * double x[],y[]; int e0,nx,prec; int ipio2[];
// *
// * __kernel_rem_pio2 return the last three digits of N with
// *              y = x - N*pi/2
// * so that |y| < pi/2.
// *
// * The method is to compute the integer (mod 8) and fraction parts of
// * (2/pi)*x without doing the full multiplication. In general we
// * skip the part of the product that are known to be a huge integer (
// * more accurately, = 0 mod 8 ). Thus the number of operations are
// * independent of the exponent of the input.
// *
// * NOTE: 2/pi int representation is converted to double
// * // (2/pi) is represented by an array of 24-bit integers in ipio2[].
// *
// * Input parameters:
// *      x[]     The input value (must be positive) is broken into nx
// *              pieces of 24-bit integers in double precision format.
// *              x[i] will be the i-th 24 bit of x. The scaled exponent
// *              of x[0] is given in input parameter e0 (i.e., x[0]*2^e0
// *              match x's up to 24 bits.
// *
// *              Example of breaking a double positive z into x[0]+x[1]+x[2]:
// *                      e0 = ilogb(z)-23
// *                      z  = scalbn(z,-e0)
// *              for i = 0,1,2
// *                      x[i] = floor(z)
// *                      z    = (z-x[i])*2**24
// *
// *
// *      y[]     ouput result in an array of double precision numbers.
// *              The dimension of y[] is:
// *                      24-bit  precision       1
// *                      53-bit  precision       2
// *                      64-bit  precision       2
// *                      113-bit precision       3
// *              The actual value is the sum of them. Thus for 113-bit
// *              precsion, one may have to do something like:
// *
// *              long double t,w,r_head, r_tail;
// *              t = (long double)y[2] + (long double)y[1];
// *              w = (long double)y[0];
// *              r_head = t+w;
// *              r_tail = w - (r_head - t);
// *
// *      e0      The exponent of x[0]
// *
// *      nx      dimension of x[]
// *
// *      prec    an interger indicating the precision:
// *                      0       24  bits (single)
// *                      1       53  bits (double)
// *                      2       64  bits (extended)
// *                      3       113 bits (quad)
// *
// *      NOTE: ipio2[] array below is converted to double representation
// *      //ipio2[]
// *      //        integer array, contains the (24*i)-th to (24*i+23)-th
// *      //        bit of 2/pi after binary point. The corresponding
// *      //        floating value is
// *
// *                      ipio2[i] * 2^(-24(i+1)).
// *
// * Here is the description of some local variables:
// *
// *      jk      jk+1 is the initial number of terms of ipio2[] needed
// *              in the computation. The recommended value is 2,3,4,
// *              6 for single, double, extended,and quad.
// *
// *      jz      local integer variable indicating the number of
// *              terms of ipio2[] used.
// *
// *      jx      nx - 1
// *
// *      jv      index for pointing to the suitable ipio2[] for the
// *              computation. In general, we want
// *                      ( 2^e0*x[0] * ipio2[jv-1]*2^(-24jv) )/8
// *              is an integer. Thus
// *                      e0-3-24*jv >= 0 or (e0-3)/24 >= jv
// *              Hence jv = max(0,(e0-3)/24).
// *
// *      jp      jp+1 is the number of terms in PIo2[] needed, jp = jk.
// *
// *      q[]     double array with integral value, representing the
// *              24-bits chunk of the product of x and 2/pi.
// *
// *      q0      the corresponding exponent of q[0]. Note that the
// *              exponent for q[i] would be q0-24*i.
// *
// *      PIo2[]  double precision array, obtained by cutting pi/2
// *              into 24 bits chunks.
// *
// *      f[]     ipio2[] in floating point
// *
// *      iq[]    integer array by breaking up q[] in 24-bits chunk.
// *
// *      fq[]    final product of x*(2/pi) in fq[0],..,fq[jk]
// *
// *      ih      integer. If >0 it indicates q[] is >= 0.5, hence
// *              it also indicates the *sign* of the result.
// *
// */
//
// Use PIo2 table(see stubRoutines_loongarch64.cpp)
//
// BEGIN __kernel_rem_pio2 PSEUDO CODE
//
//static int __kernel_rem_pio2(double *x, double *y, int e0, int nx, int prec, /* NOTE: converted to double */ const double *ipio2 // const int *ipio2) {
//  int jz,jx,jv,jp,jk,carry,n,iq[20],i,j,k,m,q0,ih;
//  double z,fw,f[20],fq[20],q[20];
//
//  /* initialize jk*/
//  // jk = init_jk[prec]; // NOTE: prec==2 for double. jk is always 4.
//  jp = jk; // NOTE: always 4
//
//  /* determine jx,jv,q0, note that 3>q0 */
//  jx =  nx-1;
//  jv = (e0-3)/24; if(jv<0) jv=0;
//  q0 =  e0-24*(jv+1);
//
//  /* set up f[0] to f[jx+jk] where f[jx+jk] = ipio2[jv+jk] */
//  j = jv-jx; m = jx+jk;
//
//  // NOTE: split into two for-loops: one with zeroB and one with ipio2[j]. It
//  //       allows the use of wider loads/stores
//  for(i=0;i<=m;i++,j++) f[i] = (j<0)? zeroB : /* NOTE: converted to double */ ipio2[j]; //(double) ipio2[j];
//
//  // NOTE: unrolled and vectorized "for". See comments in asm code
//  /* compute q[0],q[1],...q[jk] */
//  for (i=0;i<=jk;i++) {
//    for(j=0,fw=0.0;j<=jx;j++) fw += x[j]*f[jx+i-j]; q[i] = fw;
//  }
//
//  jz = jk;
//recompute:
//  /* distill q[] into iq[] reversingly */
//  for(i=0,j=jz,z=q[jz];j>0;i++,j--) {
//    fw    =  (double)((int)(twon24* z));
//    iq[i] =  (int)(z-two24B*fw);
//    z     =  q[j-1]+fw;
//  }
//
//  /* compute n */
//  z  = scalbnA(z,q0);           /* actual value of z */
//  z -= 8.0*floor(z*0.125);              /* trim off integer >= 8 */
//  n  = (int) z;
//  z -= (double)n;
//  ih = 0;
//  if(q0>0) {    /* need iq[jz-1] to determine n */
//    i  = (iq[jz-1]>>(24-q0)); n += i;
//    iq[jz-1] -= i<<(24-q0);
//    ih = iq[jz-1]>>(23-q0);
//  }
//  else if(q0==0) ih = iq[jz-1]>>23;
//  else if(z>=0.5) ih=2;
//
//  if(ih>0) {    /* q > 0.5 */
//    n += 1; carry = 0;
//    for(i=0;i<jz ;i++) {        /* compute 1-q */
//      j = iq[i];
//      if(carry==0) {
//        if(j!=0) {
//          carry = 1; iq[i] = 0x1000000- j;
//        }
//      } else  iq[i] = 0xffffff - j;
//    }
//    if(q0>0) {          /* rare case: chance is 1 in 12 */
//      switch(q0) {
//      case 1:
//        iq[jz-1] &= 0x7fffff; break;
//      case 2:
//        iq[jz-1] &= 0x3fffff; break;
//      }
//    }
//    if(ih==2) {
//      z = one - z;
//      if(carry!=0) z -= scalbnA(one,q0);
//    }
//  }
//
//  /* check if recomputation is needed */
//  if(z==zeroB) {
//    j = 0;
//    for (i=jz-1;i>=jk;i--) j |= iq[i];
//    if(j==0) { /* need recomputation */
//      for(k=1;iq[jk-k]==0;k++);   /* k = no. of terms needed */
//
//      for(i=jz+1;i<=jz+k;i++) {   /* add q[jz+1] to q[jz+k] */
//        f[jx+i] = /* NOTE: converted to double */ ipio2[jv+i]; //(double) ipio2[jv+i];
//        for(j=0,fw=0.0;j<=jx;j++) fw += x[j]*f[jx+i-j];
//        q[i] = fw;
//      }
//      jz += k;
//      goto recompute;
//    }
//  }
//
//  /* chop off zero terms */
//  if(z==0.0) {
//    jz -= 1; q0 -= 24;
//    while(iq[jz]==0) { jz--; q0-=24;}
//  } else { /* break z into 24-bit if necessary */
//    z = scalbnA(z,-q0);
//    if(z>=two24B) {
//      fw = (double)((int)(twon24*z));
//      iq[jz] = (int)(z-two24B*fw);
//      jz += 1; q0 += 24;
//      iq[jz] = (int) fw;
//    } else iq[jz] = (int) z ;
//  }
//
//  /* convert integer "bit" chunk to floating-point value */
//  fw = scalbnA(one,q0);
//  for(i=jz;i>=0;i--) {
//    q[i] = fw*(double)iq[i]; fw*=twon24;
//  }
//
//  /* compute PIo2[0,...,jp]*q[jz,...,0] */
//  for(i=jz;i>=0;i--) {
//    for(fw=0.0,k=0;k<=jp&&k<=jz-i;k++) fw += PIo2[k]*q[i+k];
//    fq[jz-i] = fw;
//  }
//
//  // NOTE: switch below is eliminated, because prec is always 2 for doubles
//  /* compress fq[] into y[] */
//  //switch(prec) {
//  //case 0:
//  //  fw = 0.0;
//  //  for (i=jz;i>=0;i--) fw += fq[i];
//  //  y[0] = (ih==0)? fw: -fw;
//  //  break;
//  //case 1:
//  //case 2:
//    fw = 0.0;
//    for (i=jz;i>=0;i--) fw += fq[i];
//    y[0] = (ih==0)? fw: -fw;
//    fw = fq[0]-fw;
//    for (i=1;i<=jz;i++) fw += fq[i];
//    y[1] = (ih==0)? fw: -fw;
//  //  break;
//  //case 3:       /* painful */
//  //  for (i=jz;i>0;i--) {
//  //    fw      = fq[i-1]+fq[i];
//  // fq[i]  += fq[i-1]-fw;
//  //    fq[i-1] = fw;
//  //  }
//  //  for (i=jz;i>1;i--) {
//  //    fw      = fq[i-1]+fq[i];
//  //    fq[i]  += fq[i-1]-fw;
//  //    fq[i-1] = fw;
//  //  }
//  //  for (fw=0.0,i=jz;i>=2;i--) fw += fq[i];
//  //  if(ih==0) {
//  //    y[0] =  fq[0]; y[1] =  fq[1]; y[2] =  fw;
//  //  } else {
//  //    y[0] = -fq[0]; y[1] = -fq[1]; y[2] = -fw;
//  //  }
//  //}
//  return n&7;
//}
//
// END __kernel_rem_pio2 PSEUDO CODE
//
// Changes between fdlibm and intrinsic:
//     1. One loop is unrolled and vectorized (see comments in code)
//     2. One loop is split into 2 loops (see comments in code)
//     3. Non-double code is removed(last switch). Sevaral variables became
//         constants because of that (see comments in code)
//     4. Use of jx, which is nx-1 instead of nx
// Assumptions:
//     1. Assume |X| >= PI/4
// Input and output:
//     1. Input: X = A0, jx == nx - 1 == A6, e0 == SCR1
//     2. Return n in A2, y[0] == y0 == FA4, y[1] == y1 == FA5
// NOTE: general purpose register names match local variable names in C code
// NOTE: fpu registers are actively reused. See comments in code about their usage
void MacroAssembler::generate__kernel_rem_pio2(address two_over_pi, address pio2) {
  Label Q_DONE, JX_IS_0, JX_IS_2, COMP_INNER_LOOP, RECOMP_FOR2, Q0_ZERO_CMP_LT,
        RECOMP_CHECK_DONE_NOT_ZERO, Q0_ZERO_CMP_DONE, COMP_FOR, Q0_ZERO_CMP_EQ,
        INIT_F_ZERO, RECOMPUTE, IH_FOR_INCREMENT, IH_FOR_STORE, RECOMP_CHECK_DONE,
        Z_IS_LESS_THAN_TWO24B, Z_IS_ZERO, FW_Y1_NO_NEGATION,
        RECOMP_FW_UPDATED, Z_ZERO_CHECK_DONE, FW_FOR1, IH_AFTER_SWITCH, IH_HANDLED,
        CONVERTION_FOR, FW_Y0_NO_NEGATION, FW_FOR1_DONE, FW_FOR2, FW_FOR2_DONE,
        IH_FOR, SKIP_F_LOAD, RECOMP_FOR1, RECOMP_FIRST_FOR, INIT_F_COPY,
        RECOMP_FOR1_CHECK;
  Register tmp2 = A1, n = A2, jv = A4, tmp5 = A5, jx = A6,
           tmp3 = A7, iqBase = T0, ih = T1, i = T2, tmp1 = T3,
           jz = S0, j = T5, twoOverPiBase = T6, tmp4 = S1, qBase = T8;
  FloatRegister v0 = FA0, v1 = FA1, v2 = FA2, v3 = FA3, v4 = FA4, v5 = FA5, v6 = FA6, v7 = FA7,
                vt = FT1, v17 = FT2, v18 = FT3, v19 = FT4, v20 = FT5, v21 = FT6, v22 = FT7, v24 = FT8,
                v25 = FT9, v26 = FT10, v27 = FT11, v28 = FT12, v29 = FT13, v30 = FT14, v31 = FT15;
    // jp = jk == init_jk[prec] = init_jk[2] == {2,3,4,6}[2] == 4
    // jx = nx - 1
    li(twoOverPiBase, two_over_pi);
    slti(SCR2, jv, 0);
    addi_w(tmp4, jx, 4); // tmp4 = m = jx + jk = jx + 4. jx is in {0,1,2} so m is in [4,5,6]
    masknez(jv, jv, SCR2);
    if (UseLASX)
      xvxor_v(v26, v26, v26);
    else
      vxor_v(v26, v26, v26);
    addi_w(tmp5, jv, 1);                    // jv+1
    sub_w(j, jv, jx);
    addi_d(qBase, SP, 320);                 // base of q[]
    mul_w(SCR2, i, tmp5);                   // q0 =  e0-24*(jv+1)
    sub_w(SCR1, SCR1, SCR2);
    // use double f[20], fq[20], q[20], iq[20] on stack, which is
    // (20 + 20 + 20) x 8 + 20 x 4 = 560 bytes. From lower to upper addresses it
    // will contain f[20], fq[20], q[20], iq[20]
    // now initialize f[20] indexes 0..m (inclusive)
    // for(i=0;i<=m;i++,j++) f[i] = (j<0)? zeroB : /* NOTE: converted to double */ ipio2[j]; // (double) ipio2[j];
    move(tmp5, SP);

    block_comment("for(i=0;i<=m;i++,j++) f[i] = (j<0)? zeroB : /* NOTE: converted to double */ ipio2[j]; // (double) ipio2[j];"); {
        xorr(i, i, i);
        bge(j, R0, INIT_F_COPY);
      bind(INIT_F_ZERO);
        if (UseLASX) {
          xvst(v26, tmp5, 0);
        } else {
          vst(v26, tmp5, 0);
          vst(v26, tmp5, 16);
        }
        addi_d(tmp5, tmp5, 32);
        addi_w(i, i, 4);
        addi_w(j, j, 4);
        blt(j, R0, INIT_F_ZERO);
        sub_w(i, i, j);
        move(j, R0);
      bind(INIT_F_COPY);
        alsl_d(tmp1, j, twoOverPiBase, 3 - 1); // ipio2[j] start address
        if (UseLASX) {
          xvld(v18, tmp1, 0);
          xvld(v19, tmp1, 32);
        } else {
          vld(v18, tmp1, 0);
          vld(v19, tmp1, 16);
          vld(v20, tmp1, 32);
          vld(v21, tmp1, 48);
        }
        alsl_d(tmp5, i, SP, 3 - 1);
        if (UseLASX) {
          xvst(v18, tmp5, 0);
          xvst(v19, tmp5, 32);
        } else {
          vst(v18, tmp5, 0);
          vst(v19, tmp5, 16);
          vst(v20, tmp5, 32);
          vst(v21, tmp5, 48);
        }
    }
    // v18..v21 can actually contain f[0..7]
    beqz(i, SKIP_F_LOAD); // i == 0 => f[i] == f[0] => already loaded
    if (UseLASX) {
      xvld(v18, SP, 0);   // load f[0..7]
      xvld(v19, SP, 32);
    } else {
      vld(v18, SP, 0);    // load f[0..7]
      vld(v19, SP, 16);
      vld(v20, SP, 32);
      vld(v21, SP, 48);
    }
  bind(SKIP_F_LOAD);
    // calculate 2^q0 and 2^-q0, which we'll need further.
    // q0 is exponent. So, calculate biased exponent(q0+1023)
    sub_w(tmp4, R0, SCR1);
    addi_w(tmp5, SCR1, 1023);
    addi_w(tmp4, tmp4, 1023);
    // Unroll following for(s) depending on jx in [0,1,2]
    // for (i=0;i<=jk;i++) {
    //   for(j=0,fw=0.0;j<=jx;j++) fw += x[j]*f[jx+i-j]; q[i] = fw;
    // }
    // Unrolling for jx == 0 case:
    //   q[0] = x[0] * f[0]
    //   q[1] = x[0] * f[1]
    //   q[2] = x[0] * f[2]
    //   q[3] = x[0] * f[3]
    //   q[4] = x[0] * f[4]
    //
    // Vectorization for unrolled jx == 0 case:
    //   {q[0], q[1]} = {f[0], f[1]} * x[0]
    //   {q[2], q[3]} = {f[2], f[3]} * x[0]
    //   q[4] = f[4] * x[0]
    //
    // Unrolling for jx == 1 case:
    //   q[0] = x[0] * f[1] + x[1] * f[0]
    //   q[1] = x[0] * f[2] + x[1] * f[1]
    //   q[2] = x[0] * f[3] + x[1] * f[2]
    //   q[3] = x[0] * f[4] + x[1] * f[3]
    //   q[4] = x[0] * f[5] + x[1] * f[4]
    //
    // Vectorization for unrolled jx == 1 case:
    //   {q[0], q[1]} = {f[0], f[1]} * x[1]
    //   {q[2], q[3]} = {f[2], f[3]} * x[1]
    //   q[4] = f[4] * x[1]
    //   {q[0], q[1]} += {f[1], f[2]} * x[0]
    //   {q[2], q[3]} += {f[3], f[4]} * x[0]
    //   q[4] += f[5] * x[0]
    //
    // Unrolling for jx == 2 case:
    //   q[0] = x[0] * f[2] + x[1] * f[1] + x[2] * f[0]
    //   q[1] = x[0] * f[3] + x[1] * f[2] + x[2] * f[1]
    //   q[2] = x[0] * f[4] + x[1] * f[3] + x[2] * f[2]
    //   q[3] = x[0] * f[5] + x[1] * f[4] + x[2] * f[3]
    //   q[4] = x[0] * f[6] + x[1] * f[5] + x[2] * f[4]
    //
    // Vectorization for unrolled jx == 2 case:
    //   {q[0], q[1]} = {f[0], f[1]} * x[2]
    //   {q[2], q[3]} = {f[2], f[3]} * x[2]
    //   q[4] = f[4] * x[2]
    //   {q[0], q[1]} += {f[1], f[2]} * x[1]
    //   {q[2], q[3]} += {f[3], f[4]} * x[1]
    //   q[4] += f[5] * x[1]
    //   {q[0], q[1]} += {f[2], f[3]} * x[0]
    //   {q[2], q[3]} += {f[4], f[5]} * x[0]
    //   q[4] += f[6] * x[0]
  block_comment("unrolled and vectorized computation of q[0]..q[jk]"); {
      li(SCR2, 1);
      slli_d(tmp5, tmp5, 52);                  // now it's 2^q0 double value
      slli_d(tmp4, tmp4, 52);                  // now it's 2^-q0 double value
      if (UseLASX)
        xvpermi_d(v6, v6, 0);
      else
        vreplvei_d(v6, v6, 0);
      blt(jx, SCR2, JX_IS_0);
      addi_d(i, SP, 8);
      if (UseLASX) {
        xvld(v26, i, 0);                       // load f[1..4]
        xvpermi_d(v3, v3, 0);
        xvpermi_d(v7, v7, 0);
        xvpermi_d(v20, v19, 85);
        xvpermi_d(v21, v19, 170);
      } else {
        vld(v26, i, 0);                        // load f[1..4]
        vld(v27, i, 16);
        vreplvei_d(v3, v3, 0);
        vreplvei_d(v7, v7, 0);
        vreplvei_d(vt, v20, 1);
        vreplvei_d(v21, v21, 0);
      }
      blt(SCR2, jx, JX_IS_2);
      // jx == 1
      if (UseLASX) {
        xvfmul_d(v28, v18, v7);                // f[0,3] * x[1]
        fmul_d(v30, v19, v7);                  // f[4] * x[1]
        xvfmadd_d(v28, v26, v6, v28);
        fmadd_d(v30, v6, v20, v30);            // v30 += f[5] * x[0]
      } else {
        vfmul_d(v28, v18, v7);                 // f[0,1] * x[1]
        vfmul_d(v29, v19, v7);                 // f[2,3] * x[1]
        fmul_d(v30, v20, v7);                  // f[4] * x[1]
        vfmadd_d(v28, v26, v6, v28);
        vfmadd_d(v29, v27, v6, v29);
        fmadd_d(v30, v6, vt, v30);             // v30 += f[5] * x[0]
      }
      b(Q_DONE);
    bind(JX_IS_2);
      if (UseLASX) {
        xvfmul_d(v28, v18, v3);                // f[0,3] * x[2]
        fmul_d(v30, v19, v3);                  // f[4] * x[2]
        xvfmadd_d(v28, v26, v7, v28);
        fmadd_d(v30, v7, v20, v30);            // v30 += f[5] * x[1]
        xvpermi_q(v18, v19, 3);
        xvfmadd_d(v28, v18, v6, v28);
      } else {
        vfmul_d(v28, v18, v3);                 // f[0,1] * x[2]
        vfmul_d(v29, v19, v3);                 // f[2,3] * x[2]
        fmul_d(v30, v20, v3);                  // f[4] * x[2]
        vfmadd_d(v28, v26, v7, v28);
        vfmadd_d(v29, v27, v7, v29);
        fmadd_d(v30, v7, vt, v30);             // v30 += f[5] * x[1]
        vfmadd_d(v28, v19, v6, v28);
        vfmadd_d(v29, v20, v6, v29);
      }
      fmadd_d(v30, v6, v21, v30);              // v30 += f[6] * x[0]
      b(Q_DONE);
    bind(JX_IS_0);
      if (UseLASX) {
        xvfmul_d(v28, v18, v6);                // f[0,3] * x[0]
        fmul_d(v30, v19, v6);                  // f[4] * x[0]
      } else {
        vfmul_d(v28, v18, v6);                 // f[0,1] * x[0]
        vfmul_d(v29, v19, v6);                 // f[2,3] * x[0]
        fmul_d(v30, v20, v6);                  // f[4] * x[0]
      }
    bind(Q_DONE);
      if (UseLASX) {
        xvst(v28, qBase, 0);                   // save calculated q[0]...q[jk]
      } else {
        vst(v28, qBase, 0);                    // save calculated q[0]...q[jk]
        vst(v29, qBase, 16);
      }
      fst_d(v30, qBase, 32);
  }
  li(i, 0x3E70000000000000);
  li(jz, 4);
  movgr2fr_d(v17, i);                          // v17 = twon24
  movgr2fr_d(v30, tmp5);                       // 2^q0
  vldi(v21, -960);                             // 0.125 (0x3fc0000000000000)
  vldi(v20, -992);                             // 8.0   (0x4020000000000000)
  movgr2fr_d(v22, tmp4);                       // 2^-q0

  block_comment("recompute loop"); {
    bind(RECOMPUTE);
      //  for(i=0,j=jz,z=q[jz];j>0;i++,j--) {
      //    fw    =  (double)((int)(twon24* z));
      //    iq[i] =  (int)(z-two24A*fw);
      //    z     =  q[j-1]+fw;
      //  }
      block_comment("distill q[] into iq[] reversingly"); {
          xorr(i, i, i);
          move(j, jz);
          alsl_d(tmp2, jz, qBase, 3 - 1);                  // q[jz] address
          fld_d(v18, tmp2, 0);                             // z = q[j] and moving address to q[j-1]
          addi_d(tmp2, tmp2, -8);
        bind(RECOMP_FIRST_FOR);
          fld_d(v27, tmp2, 0);
          addi_d(tmp2, tmp2, -8);
          fmul_d(v29, v17, v18);                           // twon24*z
          vfrintrz_d(v29, v29);                            // (double)(int)
          fnmsub_d(v28, v24, v29, v18);                    // v28 = z-two24A*fw
          ftintrz_w_d(vt, v28);                            // (int)(z-two24A*fw)
          alsl_d(SCR2, i, iqBase, 2 - 1);
          fst_s(vt, SCR2, 0);
          fadd_d(v18, v27, v29);
          addi_w(i, i, 1);
          addi_w(j, j, -1);
          blt(R0, j, RECOMP_FIRST_FOR);
      }
      // compute n
      fmul_d(v18, v18, v30);
      fmul_d(v2, v18, v21);
      vfrintrm_d(v2, v2);                                  // v2 = floor(v2) == rounding towards -inf
      fnmsub_d(v18, v2, v20, v18);                         // z -= 8.0*floor(z*0.125);
      li(ih, 2);
      vfrintrz_d(v2, v18);                                 // v2 = (double)((int)z)
      ftintrz_w_d(vt, v18);                                // n  = (int) z;
      movfr2gr_s(n, vt);
      fsub_d(v18, v18, v2);                                // z -= (double)n;

      block_comment("q0-dependent initialization"); {
          blt(SCR1, R0, Q0_ZERO_CMP_LT);                   // if (q0 > 0)
          addi_w(j, jz, -1);                               // j = jz - 1
          alsl_d(SCR2, j, iqBase, 2 - 1);
          ld_w(tmp2, SCR2, 0);                             // tmp2 = iq[jz-1]
          beq(SCR1, R0, Q0_ZERO_CMP_EQ);
          li(tmp4, 24);
          sub_w(tmp4, tmp4, SCR1);                         // == 24 - q0
          srl_w(i, tmp2, tmp4);                            // i = iq[jz-1] >> (24-q0)
          sll_w(tmp5, i, tmp4);
          sub_w(tmp2, tmp2, tmp5);                         // iq[jz-1] -= i<<(24-q0);
          alsl_d(SCR2, j, iqBase, 2 - 1);
          st_w(tmp2, SCR2, 0);                             // store iq[jz-1]
          addi_w(SCR2, tmp4, -1);                          // == 23 - q0
          add_w(n, n, i);                                  // n+=i
          srl_w(ih, tmp2, SCR2);                           // ih = iq[jz-1] >> (23-q0)
          b(Q0_ZERO_CMP_DONE);
        bind(Q0_ZERO_CMP_EQ);
          srli_d(ih, tmp2, 23);                            // ih = iq[z-1] >> 23
          b(Q0_ZERO_CMP_DONE);
        bind(Q0_ZERO_CMP_LT);
          vldi(v4, -928);                                  // 0.5 (0x3fe0000000000000)
          fcmp_clt_d(FCC0, v18, v4);
          movcf2gr(SCR2, FCC0);
          masknez(ih, ih, SCR2);                           // if (z<0.5) ih = 0
      }
    bind(Q0_ZERO_CMP_DONE);
      bge(R0, ih, IH_HANDLED);

    block_comment("if(ih>) {"); {
      // use rscratch2 as carry

      block_comment("for(i=0;i<jz ;i++) {...}"); {
          addi_w(n, n, 1);
          xorr(i, i, i);
          xorr(SCR2, SCR2, SCR2);
        bind(IH_FOR);
          alsl_d(j, i, iqBase, 2 - 1);
          ld_w(j, j, 0);                                   // j = iq[i]
          li(tmp3, 0x1000000);
          sub_w(tmp3, tmp3, SCR2);
          bnez(SCR2, IH_FOR_STORE);
          beqz(j, IH_FOR_INCREMENT);
          li(SCR2, 1);
        bind(IH_FOR_STORE);
          sub_w(tmp3, tmp3, j);
          alsl_d(tmp1, i, iqBase, 2 - 1);
          st_w(tmp3, tmp1, 0);                             // iq[i] = 0xffffff - j
        bind(IH_FOR_INCREMENT);
          addi_w(i, i, 1);
          blt(i, jz, IH_FOR);
      }

      block_comment("if(q0>0) {"); {
        bge(R0, SCR1, IH_AFTER_SWITCH);
        // tmp3 still has iq[jz-1] value. no need to reload
        // now, zero high tmp3 bits (rscratch1 number of bits)
        li(j, 0xffffffff);
        addi_w(i, jz, -1);                                 // set i to jz-1
        srl_d(j, j, SCR1);
        srli_w(tmp1, j, 8);
        andr(tmp3, tmp3, tmp1);                            // we have 24-bit-based constants
        alsl_d(tmp1, i, iqBase, 2 - 1);
        st_w(tmp3, tmp1, 0);                               // save iq[jz-1]
      }
      bind(IH_AFTER_SWITCH);
        li(tmp1, 2);
        bne(ih, tmp1, IH_HANDLED);

        block_comment("if(ih==2) {"); {
          vldi(v25, -912);                                 // 1.0 (0x3ff0000000000000)
          fsub_d(v18, v25, v18);                           // z = one - z;
          beqz(SCR2, IH_HANDLED);
          fsub_d(v18, v18, v30);                           // z -= scalbnA(one,q0);
        }
    }
    bind(IH_HANDLED);
      // check if recomputation is needed
      vxor_v(vt, vt, vt);
      fcmp_cne_d(FCC0, v18, vt);
      bcnez(FCC0, RECOMP_CHECK_DONE_NOT_ZERO);

      block_comment("if(z==zeroB) {"); {

        block_comment("for (i=jz-1;i>=jk;i--) j |= iq[i];"); {
            addi_w(i, jz, -1);
            xorr(j, j, j);
            b(RECOMP_FOR1_CHECK);
          bind(RECOMP_FOR1);
            alsl_d(tmp1, i, iqBase, 2 - 1);
            ld_w(tmp1, tmp1, 0);
            orr(j, j, tmp1);
            addi_w(i, i, -1);
          bind(RECOMP_FOR1_CHECK);
            li(SCR2, 4);
            bge(i, SCR2, RECOMP_FOR1);
        }
        bnez(j, RECOMP_CHECK_DONE);

        block_comment("if(j==0) {"); {
            // for(k=1;iq[jk-k]==0;k++); // let's unroll it. jk == 4. So, read
            // iq[3], iq[2], iq[1], iq[0] until non-zero value
            ld_d(tmp1, iqBase, 0);                 // iq[0..3]
            ld_d(tmp3, iqBase, 8);
            li(j, 2);
            masknez(tmp1, tmp1, tmp3);             // set register for further consideration
            orr(tmp1, tmp1, tmp3);
            masknez(j, j, tmp3);                   // set initial k. Use j as k
            srli_d(SCR2, tmp1, 32);
            sltu(SCR2, R0, SCR2);
            addi_w(i, jz, 1);
            add_w(j, j, SCR2);

          block_comment("for(i=jz+1;i<=jz+k;i++) {...}"); {
              add_w(jz, i, j); // i = jz+1, j = k-1. j+i = jz+k (which is a new jz)
            bind(RECOMP_FOR2);
              add_w(tmp1, jv, i);
              alsl_d(SCR2, tmp1, twoOverPiBase, 3 - 1);
              fld_d(v29, SCR2, 0);
              add_w(tmp2, jx, i);
              alsl_d(SCR2, tmp2, SP, 3 - 1);
              fst_d(v29, SCR2, 0);
              // f[jx+i] = /* NOTE: converted to double */ ipio2[jv+i]; //(double) ipio2[jv+i];
              // since jx = 0, 1 or 2 we can unroll it:
              // for(j=0,fw=0.0;j<=jx;j++) fw += x[j]*f[jx+i-j];
              // f[jx+i-j] == (for first iteration) f[jx+i], which is already v29
              alsl_d(tmp2, tmp2, SP, 3 - 1);     // address of f[jx+i]
              fld_d(v4, tmp2, -16);              // load f[jx+i-2] and f[jx+i-1]
              fld_d(v5, tmp2, -8);
              fmul_d(v26, v6, v29); // initial fw
              beqz(jx, RECOMP_FW_UPDATED);
              fmadd_d(v26, v7, v5, v26);
              li(SCR2, 1);
              beq(jx, SCR2, RECOMP_FW_UPDATED);
              fmadd_d(v26, v3, v4, v26);
            bind(RECOMP_FW_UPDATED);
              alsl_d(SCR2, i, qBase, 3 - 1);
              fst_d(v26, SCR2, 0);               // q[i] = fw;
              addi_w(i, i, 1);
              bge(jz, i, RECOMP_FOR2);           // jz here is "old jz" + k
          }
            b(RECOMPUTE);
        }
      }
    }
    bind(RECOMP_CHECK_DONE);
      // chop off zero terms
      vxor_v(vt, vt, vt);
      fcmp_ceq_d(FCC0, v18, vt);
      bcnez(FCC0, Z_IS_ZERO);

      block_comment("else block of if(z==0.0) {"); {
        bind(RECOMP_CHECK_DONE_NOT_ZERO);
          fmul_d(v18, v18, v22);
          fcmp_clt_d(FCC0, v18, v24);                        // v24 is stil two24A
          bcnez(FCC0, Z_IS_LESS_THAN_TWO24B);
          fmul_d(v1, v18, v17);                              // twon24*z
          vfrintrz_d(v1, v1);                                // v1 = (double)(int)(v1)
          fnmsub_d(v2, v24, v1, v18);
          ftintrz_w_d(vt, v1);                               // (int)fw
          movfr2gr_s(tmp3, vt);
          ftintrz_w_d(vt, v2);                               // double to int
          movfr2gr_s(tmp2, vt);
          alsl_d(SCR2, jz, iqBase, 2 - 1);
          st_w(tmp2, SCR2, 0);
          addi_w(SCR1, SCR1, 24);
          addi_w(jz, jz, 1);
          alsl_d(SCR2, jz, iqBase, 2 - 1);
          st_w(tmp3, SCR2, 0);                               // iq[jz] = (int) fw
          b(Z_ZERO_CHECK_DONE);
        bind(Z_IS_LESS_THAN_TWO24B);
          ftintrz_w_d(vt, v18);                              // (int)z
          movfr2gr_s(tmp3, vt);
          alsl_d(SCR2, jz, iqBase, 2 - 1);
          st_w(tmp3, SCR2, 0);                               // iq[jz] = (int) z
          b(Z_ZERO_CHECK_DONE);
      }

      block_comment("if(z==0.0) {"); {
        bind(Z_IS_ZERO);
          addi_w(jz, jz, -1);
          alsl_d(SCR2, jz, iqBase, 2 - 1);
          ld_w(tmp1, SCR2, 0);
          addi_w(SCR1, SCR1, -24);
          beqz(tmp1, Z_IS_ZERO);
      }
      bind(Z_ZERO_CHECK_DONE);
        // convert integer "bit" chunk to floating-point value
        // v17 = twon24
        // update v30, which was scalbnA(1.0, <old q0>);
        addi_w(tmp2, SCR1, 1023); // biased exponent
        slli_d(tmp2, tmp2, 52);   // put at correct position
        move(i, jz);
        movgr2fr_d(v30, tmp2);

        block_comment("for(i=jz;i>=0;i--) {q[i] = fw*(double)iq[i]; fw*=twon24;}"); {
          bind(CONVERTION_FOR);
            alsl_d(SCR2, i, iqBase, 2 - 1);
            fld_s(v31, SCR2, 0);
            vffintl_d_w(v31, v31);
            fmul_d(v31, v31, v30);
            alsl_d(SCR2, i, qBase, 3 - 1);
            fst_d(v31, SCR2, 0);
            fmul_d(v30, v30, v17);
            addi_w(i, i, -1);
            bge(i, R0, CONVERTION_FOR);
        }
        addi_d(SCR2, SP, 160);   // base for fq
        // reusing twoOverPiBase
        li(twoOverPiBase, pio2);

      block_comment("compute PIo2[0,...,jp]*q[jz,...,0]. for(i=jz;i>=0;i--) {...}"); {
          move(i, jz);
          move(tmp2, R0); // tmp2 will keep jz - i == 0 at start
        bind(COMP_FOR);
          // for(fw=0.0,k=0;k<=jp&&k<=jz-i;k++) fw += PIo2[k]*q[i+k];
          vxor_v(v30, v30, v30);
          alsl_d(tmp5, i, qBase, 3 - 1); // address of q[i+k] for k==0
          li(tmp3, 4);
          slti(tmp4, tmp2, 5);
          alsl_d(tmp1, i, qBase, 3 - 1); // used as q[i] address
          masknez(tmp3, tmp3, tmp4);     // min(jz - i, jp);
          maskeqz(tmp4, tmp2, tmp4);
          orr(tmp3, tmp3, tmp4);
          move(tmp4, R0);                // used as k

          block_comment("for(fw=0.0,k=0;k<=jp&&k<=jz-i;k++) fw += PIo2[k]*q[i+k];"); {
            bind(COMP_INNER_LOOP);
              alsl_d(tmp5, tmp4, tmp1, 3 - 1);
              fld_d(v18, tmp5, 0);                                      // q[i+k]
              alsl_d(tmp5, tmp4, twoOverPiBase, 3 - 1);
              fld_d(v19, tmp5, 0);                                      // PIo2[k]
              fmadd_d(v30, v18, v19, v30);                              // fw += PIo2[k]*q[i+k];
              addi_w(tmp4, tmp4, 1);                                    // k++
              bge(tmp3, tmp4, COMP_INNER_LOOP);
          }
          alsl_d(tmp5, tmp2, SCR2, 3 - 1);
          fst_d(v30, tmp5, 0);                                          // fq[jz-i]
          addi_d(tmp2, tmp2, 1);
          addi_w(i, i, -1);
          bge(i, R0, COMP_FOR);
      }

      block_comment("switch(prec) {...}. case 2:"); {
        // compress fq into y[]
        // remember prec == 2

        block_comment("for (i=jz;i>=0;i--) fw += fq[i];"); {
            vxor_v(v4, v4, v4);
            move(i, jz);
          bind(FW_FOR1);
            alsl_d(tmp5, i, SCR2, 3 - 1);
            fld_d(v1, tmp5, 0);
            addi_w(i, i, -1);
            fadd_d(v4, v4, v1);
            bge(i, R0, FW_FOR1);
        }
        bind(FW_FOR1_DONE);
          // v1 contains fq[0]. so, keep it so far
          fsub_d(v5, v1, v4); // fw = fq[0] - fw
          beqz(ih, FW_Y0_NO_NEGATION);
          fneg_d(v4, v4);
        bind(FW_Y0_NO_NEGATION);

        block_comment("for (i=1;i<=jz;i++) fw += fq[i];"); {
            li(i, 1);
            blt(jz, i, FW_FOR2_DONE);
          bind(FW_FOR2);
            alsl_d(tmp5, i, SCR2, 3 - 1);
            fld_d(v1, tmp5, 0);
            addi_w(i, i, 1);
            fadd_d(v5, v5, v1);
            bge(jz, i, FW_FOR2);
        }
        bind(FW_FOR2_DONE);
          beqz(ih, FW_Y1_NO_NEGATION);
          fneg_d(v5, v5);
        bind(FW_Y1_NO_NEGATION);
          addi_d(SP, SP, 560);
      }
}

///* __kernel_sin( x, y, iy)
// * kernel sin function on [-pi/4, pi/4], pi/4 ~ 0.7854
// * Input x is assumed to be bounded by ~pi/4 in magnitude.
// * Input y is the tail of x.
// * Input iy indicates whether y is 0. (if iy=0, y assume to be 0).
// *
// * Algorithm
// *      1. Since sin(-x) = -sin(x), we need only to consider positive x.
// *      2. if x < 2^-27 (hx<0x3e400000 0), return x with inexact if x!=0.
// *      3. sin(x) is approximated by a polynomial of degree 13 on
// *         [0,pi/4]
// *                               3            13
// *              sin(x) ~ x + S1*x + ... + S6*x
// *         where
// *
// *      |sin(x)         2     4     6     8     10     12  |     -58
// *      |----- - (1+S1*x +S2*x +S3*x +S4*x +S5*x  +S6*x   )| <= 2
// *      |  x                                               |
// *
// *      4. sin(x+y) = sin(x) + sin'(x')*y
// *                  ~ sin(x) + (1-x*x/2)*y
// *         For better accuracy, let
// *                   3      2      2      2      2
// *              r = x *(S2+x *(S3+x *(S4+x *(S5+x *S6))))
// *         then                   3    2
// *              sin(x) = x + (S1*x + (x *(r-y/2)+y))
// */
//static const double
//S1  = -1.66666666666666324348e-01, /* 0xBFC55555, 0x55555549 */
//S2  =  8.33333333332248946124e-03, /* 0x3F811111, 0x1110F8A6 */
//S3  = -1.98412698298579493134e-04, /* 0xBF2A01A0, 0x19C161D5 */
//S4  =  2.75573137070700676789e-06, /* 0x3EC71DE3, 0x57B1FE7D */
//S5  = -2.50507602534068634195e-08, /* 0xBE5AE5E6, 0x8A2B9CEB */
//S6  =  1.58969099521155010221e-10; /* 0x3DE5D93A, 0x5ACFD57C */
//
// NOTE: S1..S6 were moved into a table: StubRoutines::la::_dsin_coef
//
// BEGIN __kernel_sin PSEUDO CODE
//
//static double __kernel_sin(double x, double y, bool iy)
//{
//        double z,r,v;
//
//        // NOTE: not needed. moved to dsin/dcos
//        //int ix;
//        //ix = high(x)&0x7fffffff;                /* high word of x */
//
//        // NOTE: moved to dsin/dcos
//        //if(ix<0x3e400000)                       /* |x| < 2**-27 */
//        //   {if((int)x==0) return x;}            /* generate inexact */
//
//        z       =  x*x;
//        v       =  z*x;
//        r       =  S2+z*(S3+z*(S4+z*(S5+z*S6)));
//        if(iy==0) return x+v*(S1+z*r);
//        else      return x-((z*(half*y-v*r)-y)-v*S1);
//}
//
// END __kernel_sin PSEUDO CODE
//
// Changes between fdlibm and intrinsic:
//     1. Removed |x| < 2**-27 check, because if was done earlier in dsin/dcos
//     2. Constants are now loaded from table dsin_coef
//     3. C code parameter "int iy" was modified to "bool iyIsOne", because
//         iy is always 0 or 1. Also, iyIsOne branch was moved into
//         generation phase instead of taking it during code execution
// Input ans output:
//     1. Input for generated function: X argument = x
//     2. Input for generator: x = register to read argument from, iyIsOne
//         = flag to use low argument low part or not, dsin_coef = coefficients
//         table address
//     3. Return sin(x) value in FA0
void MacroAssembler::generate_kernel_sin(FloatRegister x, bool iyIsOne, address dsin_coef) {
  FloatRegister y = FA5, z = FA6, v = FA7, r = FT0, s1 = FT1, s2 = FT2,
                s3 = FT3, s4 = FT4, s5 = FT5, s6 = FT6, half = FT7;
  li(SCR2, dsin_coef);
  fld_d(s5, SCR2, 32);
  fld_d(s6, SCR2, 40);
  fmul_d(z, x, x); // z =  x*x;
  fld_d(s1, SCR2, 0);
  fld_d(s2, SCR2, 8);
  fld_d(s3, SCR2, 16);
  fld_d(s4, SCR2, 24);
  fmul_d(v, z, x); // v =  z*x;

  block_comment("calculate r =  S2+z*(S3+z*(S4+z*(S5+z*S6)))"); {
    fmadd_d(r, z, s6, s5);
    // initialize "half" in current block to utilize 2nd FPU. However, it's
    // not a part of this block
    vldi(half, -928);       // 0.5 (0x3fe0000000000000)
    fmadd_d(r, z, r, s4);
    fmadd_d(r, z, r, s3);
    fmadd_d(r, z, r, s2);
  }

  if (!iyIsOne) {
    // return x+v*(S1+z*r);
    fmadd_d(s1, z, r, s1);
    fmadd_d(FA0, v, s1, x);
  } else {
    // return x-((z*(half*y-v*r)-y)-v*S1);
    fmul_d(s6, half, y);    // half*y
    fnmsub_d(s6, v, r, s6); // half*y-v*r
    fnmsub_d(s6, z, s6, y); // y - z*(half*y-v*r) = - (z*(half*y-v*r)-y)
    fmadd_d(s6, v, s1, s6); // - (z*(half*y-v*r)-y) + v*S1 == -((z*(half*y-v*r)-y)-v*S1)
    fadd_d(FA0, x, s6);
  }
}

///*
// * __kernel_cos( x,  y )
// * kernel cos function on [-pi/4, pi/4], pi/4 ~ 0.785398164
// * Input x is assumed to be bounded by ~pi/4 in magnitude.
// * Input y is the tail of x.
// *
// * Algorithm
// *      1. Since cos(-x) = cos(x), we need only to consider positive x.
// *      2. if x < 2^-27 (hx<0x3e400000 0), return 1 with inexact if x!=0.
// *      3. cos(x) is approximated by a polynomial of degree 14 on
// *         [0,pi/4]
// *                                       4            14
// *              cos(x) ~ 1 - x*x/2 + C1*x + ... + C6*x
// *         where the remez error is
// *
// *      |              2     4     6     8     10    12     14 |     -58
// *      |cos(x)-(1-.5*x +C1*x +C2*x +C3*x +C4*x +C5*x  +C6*x  )| <= 2
// *      |                                                      |
// *
// *                     4     6     8     10    12     14
// *      4. let r = C1*x +C2*x +C3*x +C4*x +C5*x  +C6*x  , then
// *             cos(x) = 1 - x*x/2 + r
// *         since cos(x+y) ~ cos(x) - sin(x)*y
// *                        ~ cos(x) - x*y,
// *         a correction term is necessary in cos(x) and hence
// *              cos(x+y) = 1 - (x*x/2 - (r - x*y))
// *         For better accuracy when x > 0.3, let qx = |x|/4 with
// *         the last 32 bits mask off, and if x > 0.78125, let qx = 0.28125.
// *         Then
// *              cos(x+y) = (1-qx) - ((x*x/2-qx) - (r-x*y)).
// *         Note that 1-qx and (x*x/2-qx) is EXACT here, and the
// *         magnitude of the latter is at least a quarter of x*x/2,
// *         thus, reducing the rounding error in the subtraction.
// */
//
//static const double
//C1  =  4.16666666666666019037e-02, /* 0x3FA55555, 0x5555554C */
//C2  = -1.38888888888741095749e-03, /* 0xBF56C16C, 0x16C15177 */
//C3  =  2.48015872894767294178e-05, /* 0x3EFA01A0, 0x19CB1590 */
//C4  = -2.75573143513906633035e-07, /* 0xBE927E4F, 0x809C52AD */
//C5  =  2.08757232129817482790e-09, /* 0x3E21EE9E, 0xBDB4B1C4 */
//C6  = -1.13596475577881948265e-11; /* 0xBDA8FAE9, 0xBE8838D4 */
//
// NOTE: C1..C6 were moved into a table: StubRoutines::la::_dcos_coef
//
// BEGIN __kernel_cos PSEUDO CODE
//
//static double __kernel_cos(double x, double y)
//{
//  double a,h,z,r,qx=0;
//
//  // NOTE: ix is already initialized in dsin/dcos. Reuse value from register
//  //int ix;
//  //ix = high(x)&0x7fffffff;              /* ix = |x|'s high word*/
//
//  // NOTE: moved to dsin/dcos
//  //if(ix<0x3e400000) {                   /* if x < 2**27 */
//  //  if(((int)x)==0) return one;         /* generate inexact */
//  //}
//
//  z  = x*x;
//  r  = z*(C1+z*(C2+z*(C3+z*(C4+z*(C5+z*C6)))));
//  if(ix < 0x3FD33333)                   /* if |x| < 0.3 */
//    return one - (0.5*z - (z*r - x*y));
//  else {
//    if(ix > 0x3fe90000) {               /* x > 0.78125 */
//      qx = 0.28125;
//    } else {
//      set_high(&qx, ix-0x00200000); /* x/4 */
//      set_low(&qx, 0);
//    }
//    h = 0.5*z-qx;
//    a = one-qx;
//    return a - (h - (z*r-x*y));
//  }
//}
//
// END __kernel_cos PSEUDO CODE
//
// Changes between fdlibm and intrinsic:
//     1. Removed |x| < 2**-27 check, because if was done earlier in dsin/dcos
//     2. Constants are now loaded from table dcos_coef
// Input and output:
//     1. Input for generated function: X argument = x
//     2. Input for generator: x = register to read argument from, dcos_coef
//        = coefficients table address
//     3. Return cos(x) value in FA0
void MacroAssembler::generate_kernel_cos(FloatRegister x, address dcos_coef) {
  Register ix = A3;
  FloatRegister qx = FA1, h = FA2, a = FA3, y = FA5, z = FA6, r = FA7, C1 = FT0,
        C2 = FT1, C3 = FT2, C4 = FT3, C5 = FT4, C6 = FT5, one = FT6, half = FT7;
  Label IX_IS_LARGE, SET_QX_CONST, DONE, QX_SET;
    li(SCR2, dcos_coef);
    fld_d(C1, SCR2, 0);
    fld_d(C2, SCR2, 8);
    fld_d(C3, SCR2, 16);
    fld_d(C4, SCR2, 24);
    fld_d(C5, SCR2, 32);
    fld_d(C6, SCR2, 40);
    fmul_d(z, x, x);                               // z=x^2
    block_comment("calculate r = z*(C1+z*(C2+z*(C3+z*(C4+z*(C5+z*C6)))))"); {
      fmadd_d(r, z, C6, C5);
      vldi(half, -928);                            // 0.5 (0x3fe0000000000000)
      fmadd_d(r, z, r, C4);
      fmul_d(y, x, y);
      fmadd_d(r, z, r, C3);
      li(SCR1, 0x3FD33333);
      fmadd_d(r, z, r, C2);
      fmul_d(x, z, z);                             // x = z^2
      fmadd_d(r, z, r, C1);                        // r = C1+z(C2+z(C4+z(C5+z*C6)))
    }
    // need to multiply r by z to have "final" r value
    vldi(one, -912);                               // 1.0 (0x3ff0000000000000)
    bge(ix, SCR1, IX_IS_LARGE);
    block_comment("if(ix < 0x3FD33333) return one - (0.5*z - (z*r - x*y))"); {
      // return 1.0 - (0.5*z - (z*r - x*y)) = 1.0 - (0.5*z + (x*y - z*r))
      fnmsub_d(FA0, x, r, y);
      fmadd_d(FA0, half, z, FA0);
      fsub_d(FA0, one, FA0);
      b(DONE);
    }
  block_comment("if(ix >= 0x3FD33333)"); {
    bind(IX_IS_LARGE);
      li(SCR2, 0x3FE90000);
      blt(SCR2, ix, SET_QX_CONST);
      block_comment("set_high(&qx, ix-0x00200000); set_low(&qx, 0);"); {
        li(SCR2, 0x00200000);
        sub_w(SCR2, ix, SCR2);
        slli_d(SCR2, SCR2, 32);
        movgr2fr_d(qx, SCR2);
      }
      b(QX_SET);
    bind(SET_QX_CONST);
      block_comment("if(ix > 0x3fe90000) qx = 0.28125;"); {
        vldi(qx, -942);         // 0.28125 (0x3fd2000000000000)
      }
    bind(QX_SET);
      fmsub_d(C6, x, r, y);     // z*r - xy
      fmsub_d(h, half, z, qx);  // h = 0.5*z - qx
      fsub_d(a, one, qx);       // a = 1-qx
      fsub_d(C6, h, C6);        // = h - (z*r - x*y)
      fsub_d(FA0, a, C6);
  }
  bind(DONE);
}

// generate_dsin_dcos creates stub for dsin and dcos
// Generation is done via single call because dsin and dcos code is almost the
// same(see C code below). These functions work as follows:
// 1) handle corner cases: |x| ~< pi/4, x is NaN or INF, |x| < 2**-27
// 2) perform argument reduction if required
// 3) call kernel_sin or kernel_cos which approximate sin/cos via polynomial
//
// BEGIN dsin/dcos PSEUDO CODE
//
//dsin_dcos(jdouble x, bool isCos) {
//  double y[2],z=0.0;
//  int n, ix;
//
//  /* High word of x. */
//  ix = high(x);
//
//  /* |x| ~< pi/4 */
//  ix &= 0x7fffffff;
//  if(ix <= 0x3fe921fb) return isCos ? __kernel_cos : __kernel_sin(x,z,0);
//
//  /* sin/cos(Inf or NaN) is NaN */
//  else if (ix>=0x7ff00000) return x-x;
//  else if (ix<0x3e400000) {                   /* if ix < 2**27 */
//    if(((int)x)==0) return isCos ? one : x;         /* generate inexact */
//  }
//  /* argument reduction needed */
//  else {
//    n = __ieee754_rem_pio2(x,y);
//    switch(n&3) {
//    case 0: return isCos ?  __kernel_cos(y[0],y[1])      :  __kernel_sin(y[0],y[1], true);
//    case 1: return isCos ? -__kernel_sin(y[0],y[1],true) :  __kernel_cos(y[0],y[1]);
//    case 2: return isCos ? -__kernel_cos(y[0],y[1])      : -__kernel_sin(y[0],y[1], true);
//    default:
//      return isCos ? __kernel_sin(y[0],y[1],1) : -__kernel_cos(y[0],y[1]);
//    }
//  }
//}
// END dsin/dcos PSEUDO CODE
//
// Changes between fdlibm and intrinsic:
//     1. Moved ix < 2**27 from kernel_sin/kernel_cos into dsin/dcos
//     2. Final switch use equivalent bit checks(tbz/tbnz)
// Input ans output:
//     1. Input for generated function: X = A0
//     2. Input for generator: isCos = generate sin or cos, npio2_hw = address
//         of npio2_hw table, two_over_pi = address of two_over_pi table,
//         pio2 = address if pio2 table, dsin_coef = address if dsin_coef table,
//         dcos_coef = address of dcos_coef table
//     3. Return result in FA0
// NOTE: general purpose register names match local variable names in C code
void MacroAssembler::generate_dsin_dcos(bool isCos, address npio2_hw,
                                        address two_over_pi, address pio2,
                                        address dsin_coef, address dcos_coef) {
  Label DONE, ARG_REDUCTION, TINY_X, RETURN_SIN, EARLY_CASE;
  Register X = A0, absX = A1, n = A2, ix = A3;
  FloatRegister y0 = FA4, y1 = FA5;

    block_comment("check |x| ~< pi/4, NaN, Inf and |x| < 2**-27 cases"); {
      movfr2gr_d(X, FA0);
      li(SCR2, 0x3e400000);
      li(SCR1, 0x3fe921fb);                          // high word of pi/4.
      bstrpick_d(absX, X, 62, 0);                    // absX
      li(T0, 0x7ff0000000000000);
      srli_d(ix, absX, 32);                          // set ix
      blt(ix, SCR2, TINY_X);                         // handle tiny x (|x| < 2^-27)
      bge(SCR1, ix, EARLY_CASE);                     // if(ix <= 0x3fe921fb) return
      blt(absX, T0, ARG_REDUCTION);
      // X is NaN or INF(i.e. 0x7FF* or 0xFFF*). Return NaN (mantissa != 0).
      // Set last bit unconditionally to make it NaN
      ori(T0, T0, 1);
      movgr2fr_d(FA0, T0);
      jr(RA);
    }
  block_comment("kernel_sin/kernel_cos: if(ix<0x3e400000) {<fast return>}"); {
    bind(TINY_X);
      if (isCos) {
        vldi(FA0, -912);                             // 1.0 (0x3ff0000000000000)
      }
      jr(RA);
  }
  bind(ARG_REDUCTION); /* argument reduction needed */
    block_comment("n = __ieee754_rem_pio2(x,y);"); {
      generate__ieee754_rem_pio2(npio2_hw, two_over_pi, pio2);
    }
    block_comment("switch(n&3) {case ... }"); {
      if (isCos) {
        srli_w(T0, n, 1);
        xorr(absX, n, T0);
        andi(T0, n, 1);
        bnez(T0, RETURN_SIN);
      } else {
        andi(T0, n, 1);
        beqz(T0, RETURN_SIN);
      }
      generate_kernel_cos(y0, dcos_coef);
      if (isCos) {
        andi(T0, absX, 1);
        beqz(T0, DONE);
      } else {
        andi(T0, n, 2);
        beqz(T0, DONE);
      }
      fneg_d(FA0, FA0);
      jr(RA);
    bind(RETURN_SIN);
      generate_kernel_sin(y0, true, dsin_coef);
      if (isCos) {
        andi(T0, absX, 1);
        beqz(T0, DONE);
      } else {
        andi(T0, n, 2);
        beqz(T0, DONE);
      }
      fneg_d(FA0, FA0);
      jr(RA);
    }
  bind(EARLY_CASE);
    vxor_v(y1, y1, y1);
    if (isCos) {
      generate_kernel_cos(FA0, dcos_coef);
    } else {
      generate_kernel_sin(FA0, false, dsin_coef);
    }
  bind(DONE);
    jr(RA);
}
