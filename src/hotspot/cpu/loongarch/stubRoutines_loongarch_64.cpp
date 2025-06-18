/*
 * Copyright (c) 2003, 2013, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2015, 2022, Loongson Technology. All rights reserved.
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
#include "runtime/deoptimization.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/thread.inline.hpp"

// a description of how to extend it, see the stubRoutines.hpp file.

//find the last fp value
address StubRoutines::la::_call_stub_compiled_return                       = NULL;

/**
 *  crc_table[] from jdk/src/share/native/java/util/zip/zlib-1.2.5/crc32.h
 */
juint StubRoutines::la::_crc_table[] =
{
    0x00000000UL, 0x77073096UL, 0xee0e612cUL, 0x990951baUL, 0x076dc419UL,
    0x706af48fUL, 0xe963a535UL, 0x9e6495a3UL, 0x0edb8832UL, 0x79dcb8a4UL,
    0xe0d5e91eUL, 0x97d2d988UL, 0x09b64c2bUL, 0x7eb17cbdUL, 0xe7b82d07UL,
    0x90bf1d91UL, 0x1db71064UL, 0x6ab020f2UL, 0xf3b97148UL, 0x84be41deUL,
    0x1adad47dUL, 0x6ddde4ebUL, 0xf4d4b551UL, 0x83d385c7UL, 0x136c9856UL,
    0x646ba8c0UL, 0xfd62f97aUL, 0x8a65c9ecUL, 0x14015c4fUL, 0x63066cd9UL,
    0xfa0f3d63UL, 0x8d080df5UL, 0x3b6e20c8UL, 0x4c69105eUL, 0xd56041e4UL,
    0xa2677172UL, 0x3c03e4d1UL, 0x4b04d447UL, 0xd20d85fdUL, 0xa50ab56bUL,
    0x35b5a8faUL, 0x42b2986cUL, 0xdbbbc9d6UL, 0xacbcf940UL, 0x32d86ce3UL,
    0x45df5c75UL, 0xdcd60dcfUL, 0xabd13d59UL, 0x26d930acUL, 0x51de003aUL,
    0xc8d75180UL, 0xbfd06116UL, 0x21b4f4b5UL, 0x56b3c423UL, 0xcfba9599UL,
    0xb8bda50fUL, 0x2802b89eUL, 0x5f058808UL, 0xc60cd9b2UL, 0xb10be924UL,
    0x2f6f7c87UL, 0x58684c11UL, 0xc1611dabUL, 0xb6662d3dUL, 0x76dc4190UL,
    0x01db7106UL, 0x98d220bcUL, 0xefd5102aUL, 0x71b18589UL, 0x06b6b51fUL,
    0x9fbfe4a5UL, 0xe8b8d433UL, 0x7807c9a2UL, 0x0f00f934UL, 0x9609a88eUL,
    0xe10e9818UL, 0x7f6a0dbbUL, 0x086d3d2dUL, 0x91646c97UL, 0xe6635c01UL,
    0x6b6b51f4UL, 0x1c6c6162UL, 0x856530d8UL, 0xf262004eUL, 0x6c0695edUL,
    0x1b01a57bUL, 0x8208f4c1UL, 0xf50fc457UL, 0x65b0d9c6UL, 0x12b7e950UL,
    0x8bbeb8eaUL, 0xfcb9887cUL, 0x62dd1ddfUL, 0x15da2d49UL, 0x8cd37cf3UL,
    0xfbd44c65UL, 0x4db26158UL, 0x3ab551ceUL, 0xa3bc0074UL, 0xd4bb30e2UL,
    0x4adfa541UL, 0x3dd895d7UL, 0xa4d1c46dUL, 0xd3d6f4fbUL, 0x4369e96aUL,
    0x346ed9fcUL, 0xad678846UL, 0xda60b8d0UL, 0x44042d73UL, 0x33031de5UL,
    0xaa0a4c5fUL, 0xdd0d7cc9UL, 0x5005713cUL, 0x270241aaUL, 0xbe0b1010UL,
    0xc90c2086UL, 0x5768b525UL, 0x206f85b3UL, 0xb966d409UL, 0xce61e49fUL,
    0x5edef90eUL, 0x29d9c998UL, 0xb0d09822UL, 0xc7d7a8b4UL, 0x59b33d17UL,
    0x2eb40d81UL, 0xb7bd5c3bUL, 0xc0ba6cadUL, 0xedb88320UL, 0x9abfb3b6UL,
    0x03b6e20cUL, 0x74b1d29aUL, 0xead54739UL, 0x9dd277afUL, 0x04db2615UL,
    0x73dc1683UL, 0xe3630b12UL, 0x94643b84UL, 0x0d6d6a3eUL, 0x7a6a5aa8UL,
    0xe40ecf0bUL, 0x9309ff9dUL, 0x0a00ae27UL, 0x7d079eb1UL, 0xf00f9344UL,
    0x8708a3d2UL, 0x1e01f268UL, 0x6906c2feUL, 0xf762575dUL, 0x806567cbUL,
    0x196c3671UL, 0x6e6b06e7UL, 0xfed41b76UL, 0x89d32be0UL, 0x10da7a5aUL,
    0x67dd4accUL, 0xf9b9df6fUL, 0x8ebeeff9UL, 0x17b7be43UL, 0x60b08ed5UL,
    0xd6d6a3e8UL, 0xa1d1937eUL, 0x38d8c2c4UL, 0x4fdff252UL, 0xd1bb67f1UL,
    0xa6bc5767UL, 0x3fb506ddUL, 0x48b2364bUL, 0xd80d2bdaUL, 0xaf0a1b4cUL,
    0x36034af6UL, 0x41047a60UL, 0xdf60efc3UL, 0xa867df55UL, 0x316e8eefUL,
    0x4669be79UL, 0xcb61b38cUL, 0xbc66831aUL, 0x256fd2a0UL, 0x5268e236UL,
    0xcc0c7795UL, 0xbb0b4703UL, 0x220216b9UL, 0x5505262fUL, 0xc5ba3bbeUL,
    0xb2bd0b28UL, 0x2bb45a92UL, 0x5cb36a04UL, 0xc2d7ffa7UL, 0xb5d0cf31UL,
    0x2cd99e8bUL, 0x5bdeae1dUL, 0x9b64c2b0UL, 0xec63f226UL, 0x756aa39cUL,
    0x026d930aUL, 0x9c0906a9UL, 0xeb0e363fUL, 0x72076785UL, 0x05005713UL,
    0x95bf4a82UL, 0xe2b87a14UL, 0x7bb12baeUL, 0x0cb61b38UL, 0x92d28e9bUL,
    0xe5d5be0dUL, 0x7cdcefb7UL, 0x0bdbdf21UL, 0x86d3d2d4UL, 0xf1d4e242UL,
    0x68ddb3f8UL, 0x1fda836eUL, 0x81be16cdUL, 0xf6b9265bUL, 0x6fb077e1UL,
    0x18b74777UL, 0x88085ae6UL, 0xff0f6a70UL, 0x66063bcaUL, 0x11010b5cUL,
    0x8f659effUL, 0xf862ae69UL, 0x616bffd3UL, 0x166ccf45UL, 0xa00ae278UL,
    0xd70dd2eeUL, 0x4e048354UL, 0x3903b3c2UL, 0xa7672661UL, 0xd06016f7UL,
    0x4969474dUL, 0x3e6e77dbUL, 0xaed16a4aUL, 0xd9d65adcUL, 0x40df0b66UL,
    0x37d83bf0UL, 0xa9bcae53UL, 0xdebb9ec5UL, 0x47b2cf7fUL, 0x30b5ffe9UL,
    0xbdbdf21cUL, 0xcabac28aUL, 0x53b39330UL, 0x24b4a3a6UL, 0xbad03605UL,
    0xcdd70693UL, 0x54de5729UL, 0x23d967bfUL, 0xb3667a2eUL, 0xc4614ab8UL,
    0x5d681b02UL, 0x2a6f2b94UL, 0xb40bbe37UL, 0xc30c8ea1UL, 0x5a05df1bUL,
    0x2d02ef8dUL
};

ATTRIBUTE_ALIGNED(64) juint StubRoutines::la::_npio2_hw[] = {
    // first, various coefficient values: 0.5, invpio2, pio2_1, pio2_1t, pio2_2,
    // pio2_2t, pio2_3, pio2_3t
    // This is a small optimization wich keeping double[8] values in int[] table
    // to have less address calculation instructions
    //
    // invpio2:  53 bits of 2/pi (enough for cases when trigonometric argument is small)
    // pio2_1:   first  33 bit of pi/2
    // pio2_1t:  pi/2 - pio2_1
    // pio2_2:   second 33 bit of pi/2
    // pio2_2t:  pi/2 - (pio2_1+pio2_2)
    // pio2_3:   third  33 bit of pi/2
    // pio2_3t:  pi/2 - (pio2_1+pio2_2+pio2_3)
    0x00000000, 0x3fe00000, // 0.5
    0x6DC9C883, 0x3FE45F30, // invpio2 = 6.36619772367581382433e-01
    0x54400000, 0x3FF921FB, // pio2_1 = 1.57079632673412561417e+00
    0x1A626331, 0x3DD0B461, // pio2_1t = 6.07710050650619224932e-11
    0x1A600000, 0x3DD0B461, // pio2_2 = 6.07710050630396597660e-11
    0x2E037073, 0x3BA3198A, // pio2_2t = 2.02226624879595063154e-21
    0x2E000000, 0x3BA3198A, // pio2_3 = 2.02226624871116645580e-21
    0x252049C1, 0x397B839A, // pio2_3t = 8.47842766036889956997e-32
    // now, npio2_hw itself
    0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C,
    0x4025FDBB, 0x402921FB, 0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C,
    0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB, 0x403AB41B, 0x403C463A,
    0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
    0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB,
    0x404858EB, 0x404921FB
};

// Coefficients for sin(x) polynomial approximation: S1..S6.
// See kernel_sin comments in macroAssembler_loongarch64_trig.cpp for details
ATTRIBUTE_ALIGNED(64) jdouble StubRoutines::la::_dsin_coef[] = {
    -1.66666666666666324348e-01, // 0xBFC5555555555549
     8.33333333332248946124e-03, // 0x3F8111111110F8A6
    -1.98412698298579493134e-04, // 0xBF2A01A019C161D5
     2.75573137070700676789e-06, // 0x3EC71DE357B1FE7D
    -2.50507602534068634195e-08, // 0xBE5AE5E68A2B9CEB
     1.58969099521155010221e-10  // 0x3DE5D93A5ACFD57C
};

// Coefficients for cos(x) polynomial approximation: C1..C6.
// See kernel_cos comments in macroAssembler_loongarch64_trig.cpp for details
ATTRIBUTE_ALIGNED(64) jdouble StubRoutines::la::_dcos_coef[] = {
     4.16666666666666019037e-02, // c0x3FA555555555554C
    -1.38888888888741095749e-03, // 0xBF56C16C16C15177
     2.48015872894767294178e-05, // 0x3EFA01A019CB1590
    -2.75573143513906633035e-07, // 0xBE927E4F809C52AD
     2.08757232129817482790e-09, // 0x3E21EE9EBDB4B1C4
    -1.13596475577881948265e-11  // 0xBDA8FAE9BE8838D4
};

// Table of constants for 2/pi, 396 Hex digits (476 decimal) of 2/pi.
// Used in cases of very large argument. 396 hex digits is enough to support
// required precision.
// Converted to double to avoid unnecessary conversion in code
// NOTE: table looks like original int table: {0xA2F983, 0x6E4E44,...} with
//       only (double) conversion added
ATTRIBUTE_ALIGNED(64) jdouble StubRoutines::la::_two_over_pi[] = {
  (double)0xA2F983, (double)0x6E4E44, (double)0x1529FC, (double)0x2757D1, (double)0xF534DD, (double)0xC0DB62,
  (double)0x95993C, (double)0x439041, (double)0xFE5163, (double)0xABDEBB, (double)0xC561B7, (double)0x246E3A,
  (double)0x424DD2, (double)0xE00649, (double)0x2EEA09, (double)0xD1921C, (double)0xFE1DEB, (double)0x1CB129,
  (double)0xA73EE8, (double)0x8235F5, (double)0x2EBB44, (double)0x84E99C, (double)0x7026B4, (double)0x5F7E41,
  (double)0x3991D6, (double)0x398353, (double)0x39F49C, (double)0x845F8B, (double)0xBDF928, (double)0x3B1FF8,
  (double)0x97FFDE, (double)0x05980F, (double)0xEF2F11, (double)0x8B5A0A, (double)0x6D1F6D, (double)0x367ECF,
  (double)0x27CB09, (double)0xB74F46, (double)0x3F669E, (double)0x5FEA2D, (double)0x7527BA, (double)0xC7EBE5,
  (double)0xF17B3D, (double)0x0739F7, (double)0x8A5292, (double)0xEA6BFB, (double)0x5FB11F, (double)0x8D5D08,
  (double)0x560330, (double)0x46FC7B, (double)0x6BABF0, (double)0xCFBC20, (double)0x9AF436, (double)0x1DA9E3,
  (double)0x91615E, (double)0xE61B08, (double)0x659985, (double)0x5F14A0, (double)0x68408D, (double)0xFFD880,
  (double)0x4D7327, (double)0x310606, (double)0x1556CA, (double)0x73A8C9, (double)0x60E27B, (double)0xC08C6B,
};

// Pi over 2 value
ATTRIBUTE_ALIGNED(64) jdouble StubRoutines::la::_pio2[] = {
  1.57079625129699707031e+00, // 0x3FF921FB40000000
  7.54978941586159635335e-08, // 0x3E74442D00000000
  5.39030252995776476554e-15, // 0x3CF8469880000000
  3.28200341580791294123e-22, // 0x3B78CC5160000000
  1.27065575308067607349e-29, // 0x39F01B8380000000
  1.22933308981111328932e-36, // 0x387A252040000000
  2.73370053816464559624e-44, // 0x36E3822280000000
  2.16741683877804819444e-51, // 0x3569F31D00000000
};
