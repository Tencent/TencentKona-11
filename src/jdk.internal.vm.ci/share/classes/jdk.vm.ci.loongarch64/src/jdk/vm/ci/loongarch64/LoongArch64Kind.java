/*
 * Copyright (c) 2015, 2022, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2022, Loongson Technology. All rights reserved.
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
 */
package jdk.vm.ci.loongarch64;

import jdk.vm.ci.meta.PlatformKind;

public enum LoongArch64Kind implements PlatformKind {

    // scalar
    BYTE(1),
    WORD(2),
    DWORD(4),
    QWORD(8),
    UBYTE(1),
    UWORD(2),
    UDWORD(4),
    SINGLE(4),
    DOUBLE(8),

    // SIMD
    V128_BYTE(16, BYTE),
    V128_WORD(16, WORD),
    V128_DWORD(16, DWORD),
    V128_QWORD(16, QWORD),
    V128_SINGLE(16, SINGLE),
    V128_DOUBLE(16, DOUBLE),
    V256_BYTE(32, BYTE),
    V256_WORD(32, WORD),
    V256_DWORD(32, DWORD),
    V256_QWORD(32, QWORD),
    V256_SINGLE(32, SINGLE),
    V256_DOUBLE(32, DOUBLE);

    private final int size;
    private final int vectorLength;

    private final LoongArch64Kind scalar;
    private final EnumKey<LoongArch64Kind> key = new EnumKey<>(this);

    LoongArch64Kind(int size) {
        this.size = size;
        this.scalar = this;
        this.vectorLength = 1;
    }

    LoongArch64Kind(int size, LoongArch64Kind scalar) {
        this.size = size;
        this.scalar = scalar;

        assert size % scalar.size == 0;
        this.vectorLength = size / scalar.size;
    }

    public LoongArch64Kind getScalar() {
        return scalar;
    }

    @Override
    public int getSizeInBytes() {
        return size;
    }

    @Override
    public int getVectorLength() {
        return vectorLength;
    }

    @Override
    public Key getKey() {
        return key;
    }

    public boolean isInteger() {
        switch (this) {
            case BYTE:
            case WORD:
            case DWORD:
            case QWORD:
            case UBYTE:
            case UWORD:
            case UDWORD:
                return true;
            default:
                return false;
        }
    }

    public boolean isSIMD() {
        switch (this) {
            case SINGLE:
            case DOUBLE:
            case V128_BYTE:
            case V128_WORD:
            case V128_DWORD:
            case V128_QWORD:
            case V128_SINGLE:
            case V128_DOUBLE:
            case V256_BYTE:
            case V256_WORD:
            case V256_DWORD:
            case V256_QWORD:
            case V256_SINGLE:
            case V256_DOUBLE:
                return true;
            default:
                return false;
        }
    }

    @Override
    public char getTypeChar() {
        switch (this) {
            case BYTE:
                return 'b';
            case WORD:
                return 'w';
            case DWORD:
                return 'd';
            case QWORD:
                return 'q';
            case SINGLE:
                return 'S';
            case DOUBLE:
                return 'D';
            case V128_BYTE:
            case V128_WORD:
            case V128_DWORD:
            case V128_QWORD:
            case V128_SINGLE:
            case V128_DOUBLE:
            case V256_BYTE:
            case V256_WORD:
            case V256_DWORD:
            case V256_QWORD:
            case V256_SINGLE:
            case V256_DOUBLE:
                return 'v';
            default:
                return '-';
        }
    }
}
