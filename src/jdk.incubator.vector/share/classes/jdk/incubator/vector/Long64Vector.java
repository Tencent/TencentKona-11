/*
 * Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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
 * or visit www.oracle.com if you need additional information or have
 * questions.
 */
package jdk.incubator.vector;

import java.nio.ByteBuffer;
import java.util.Arrays;

@SuppressWarnings("cast")
final class Long64Vector extends LongVector<Shapes.S64Bit> {
    static final Long64Species SPECIES = new Long64Species();

    static final Long64Vector ZERO = new Long64Vector();

    long[] vec;

    Long64Vector() {
        vec = new long[SPECIES.length()];
    }

    Long64Vector(long[] v) {
        vec = v;
    }


    // Unary operator

    @Override
    Long64Vector uOp(FUnOp f) {
        long[] res = new long[length()];
        for (int i = 0; i < length(); i++) {
            res[i] = f.apply(i, vec[i]);
        }
        return new Long64Vector(res);
    }

    @Override
    Long64Vector uOp(Mask<Long, Shapes.S64Bit> o, FUnOp f) {
        long[] res = new long[length()];
        Long64Mask m = (Long64Mask) o;
        for (int i = 0; i < length(); i++) {
            res[i] = m.bits[i] ? f.apply(i, vec[i]) : vec[i];
        }
        return new Long64Vector(res);
    }

    // Binary operator

    @Override
    Long64Vector bOp(Vector<Long, Shapes.S64Bit> o, FBinOp f) {
        long[] res = new long[length()];
        Long64Vector v = (Long64Vector) o;
        for (int i = 0; i < length(); i++) {
            res[i] = f.apply(i, vec[i], v.vec[i]);
        }
        return new Long64Vector(res);
    }

    @Override
    Long64Vector bOp(Vector<Long, Shapes.S64Bit> o1, Mask<Long, Shapes.S64Bit> o2, FBinOp f) {
        long[] res = new long[length()];
        Long64Vector v = (Long64Vector) o1;
        Long64Mask m = (Long64Mask) o2;
        for (int i = 0; i < length(); i++) {
            res[i] = m.bits[i] ? f.apply(i, vec[i], v.vec[i]) : vec[i];
        }
        return new Long64Vector(res);
    }

    // Trinary operator

    @Override
    Long64Vector tOp(Vector<Long, Shapes.S64Bit> o1, Vector<Long, Shapes.S64Bit> o2, FTriOp f) {
        long[] res = new long[length()];
        Long64Vector v1 = (Long64Vector) o1;
        Long64Vector v2 = (Long64Vector) o2;
        for (int i = 0; i < length(); i++) {
            res[i] = f.apply(i, vec[i], v1.vec[i], v2.vec[i]);
        }
        return new Long64Vector(res);
    }

    @Override
    Long64Vector tOp(Vector<Long, Shapes.S64Bit> o1, Vector<Long, Shapes.S64Bit> o2, Mask<Long, Shapes.S64Bit> o3, FTriOp f) {
        long[] res = new long[length()];
        Long64Vector v1 = (Long64Vector) o1;
        Long64Vector v2 = (Long64Vector) o2;
        Long64Mask m = (Long64Mask) o3;
        for (int i = 0; i < length(); i++) {
            res[i] = m.bits[i] ? f.apply(i, vec[i], v1.vec[i], v2.vec[i]) : vec[i];
        }
        return new Long64Vector(res);
    }

    @Override
    long rOp(long v, FBinOp f) {
        for (int i = 0; i < length(); i++) {
            v = f.apply(i, v, vec[i]);
        }
        return v;
    }

    //

    @Override
    public String toString() {
        return Arrays.toString(vec);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || this.getClass() != o.getClass()) return false;

        Long64Vector that = (Long64Vector) o;
        return Arrays.equals(vec, that.vec);
    }

    @Override
    public int hashCode() {
        return Arrays.hashCode(vec);
    }

    // Binary test

    @Override
    Long64Mask bTest(Vector<Long, Shapes.S64Bit> o, FBinTest f) {
        Long64Vector v = (Long64Vector) o;
        boolean[] bits = new boolean[length()];
        for (int i = 0; i < length(); i++){
            bits[i] = f.apply(i, vec[i], v.vec[i]);
        }
        return new Long64Mask(bits);
    }

    // Foreach

    @Override
    void forEach(FUnCon f) {
        for (int i = 0; i < length(); i++) {
            f.apply(i, vec[i]);
        }
    }

    @Override
    void forEach(Mask<Long, Shapes.S64Bit> o, FUnCon f) {
        Long64Mask m = (Long64Mask) o;
        forEach((i, a) -> {
            if (m.bits[i]) { f.apply(i, a); }
        });
    }


    Double64Vector toFP() {
        double[] res = new double[this.species().length()];
        for(int i = 0; i < this.species().length(); i++){
            res[i] = Double.longBitsToDouble(vec[i]);
        }
        return new Double64Vector(res);
    }

    @Override
    public Long64Vector rotateEL(int j) {
        long[] res = new long[length()];
        for (int i = 0; i < length(); i++){
            res[j + i % length()] = vec[i];
        }
        return new Long64Vector(res);
    }

    @Override
    public Long64Vector rotateER(int j) {
        long[] res = new long[length()];
        for (int i = 0; i < length(); i++){
            int z = i - j;
            if(j < 0) {
                res[length() + z] = vec[i];
            } else {
                res[z] = vec[i];
            }
        }
        return new Long64Vector(res);
    }

    @Override
    public Long64Vector shiftEL(int j) {
        long[] res = new long[length()];
        for (int i = 0; i < length() - j; i++) {
            res[i] = vec[i + j];
        }
        return new Long64Vector(res);
    }

    @Override
    public Long64Vector shiftER(int j) {
        long[] res = new long[length()];
        for (int i = 0; i < length() - j; i++){
            res[i + j] = vec[i];
        }
        return new Long64Vector(res);
    }

    @Override
    public Long64Vector shuffle(Vector<Long, Shapes.S64Bit> o, Shuffle<Long, Shapes.S64Bit> s) {
        Long64Vector v = (Long64Vector) o;
        return uOp((i, a) -> {
            int e = s.getElement(i);
            if(e >= 0 && e < length()) {
                //from this
                return vec[e];
            } else if(e < length() * 2) {
                //from o
                return v.vec[e - length()];
            } else {
                throw new ArrayIndexOutOfBoundsException("Bad reordering for shuffle");
            }
        });
    }

    @Override
    public Long64Vector swizzle(Shuffle<Long, Shapes.S64Bit> s) {
        return uOp((i, a) -> {
            int e = s.getElement(i);
            if(e >= 0 && e < length()) {
                return vec[e];
            } else {
                throw new ArrayIndexOutOfBoundsException("Bad reordering for shuffle");
            }
        });
    }

    @Override
    public <F, Z extends Shape<Vector<?, ?>>> Vector<F, Z> cast(Class<F> type, Z shape) {
        Vector.Species<F,Z> species = Vector.speciesInstance(type, shape);

        // Whichever is larger
        int blen = Math.max(species.bitSize(), bitSize()) / Byte.SIZE;
        ByteBuffer bb = ByteBuffer.allocate(blen);

        int limit = Math.min(species.length(), length());

        if (type == Byte.class) {
            for (int i = 0; i < limit; i++){
                bb.put(i, (byte) vec[i]);
            }
        } else if (type == Short.class) {
            for (int i = 0; i < limit; i++){
                bb.asShortBuffer().put(i, (short) vec[i]);
            }
        } else if (type == Integer.class) {
            for (int i = 0; i < limit; i++){
                bb.asIntBuffer().put(i, (int) vec[i]);
            }
        } else if (type == Long.class){
            for (int i = 0; i < limit; i++){
                bb.asLongBuffer().put(i, (long) vec[i]);
            }
        } else if (type == Float.class){
            for (int i = 0; i < limit; i++){
                bb.asFloatBuffer().put(i, (float) vec[i]);
            }
        } else if (type == Double.class){
            for (int i = 0; i < limit; i++){
                bb.asDoubleBuffer().put(i, (double) vec[i]);
            }
        } else {
            throw new UnsupportedOperationException("Bad lane type for casting.");
        }

        return species.fromByteBuffer(bb);
    }

    // Accessors

    @Override
    public long get(int i) {
        return vec[i];
    }

    @Override
    public Long64Vector with(int i, long e) {
        long[] res = vec.clone();
        res[i] = e;
        return new Long64Vector(res);
    }

    // Mask

    static final class Long64Mask extends AbstractMask<Long, Shapes.S64Bit> {
        static final Long64Mask TRUE_MASK = new Long64Mask(true);
        static final Long64Mask FALSE_MASK = new Long64Mask(false);

        public Long64Mask(boolean[] bits) {
            super(bits);
        }

        public Long64Mask(boolean val) {
            super(val);
        }

        @Override
        Long64Mask uOp(MUnOp f) {
            boolean[] res = new boolean[species().length()];
            for (int i = 0; i < species().length(); i++) {
                res[i] = f.apply(i, bits[i]);
            }
            return new Long64Mask(res);
        }

        @Override
        Long64Mask bOp(Mask<Long, Shapes.S64Bit> o, MBinOp f) {
            boolean[] res = new boolean[species().length()];
            Long64Mask m = (Long64Mask) o;
            for (int i = 0; i < species().length(); i++) {
                res[i] = f.apply(i, bits[i], m.bits[i]);
            }
            return new Long64Mask(res);
        }

        @Override
        public Long64Species species() {
            return SPECIES;
        }

        @Override
        public Long64Vector toVector() {
            long[] res = new long[species().length()];
            for (int i = 0; i < species().length(); i++) {
                res[i] = (long) (bits[i] ? -1 : 0);
            }
            return new Long64Vector(res);
        }
    }

    // Species

    @Override
    public Long64Species species() {
        return SPECIES;
    }

    static final class Long64Species extends LongSpecies<Shapes.S64Bit> {
        static final int BIT_SIZE = Shapes.S_64_BIT.bitSize();

        static final int LENGTH = BIT_SIZE / Long.SIZE;

        @Override
        public String toString() {
           StringBuilder sb = new StringBuilder("Shape[");
           sb.append(bitSize()).append(" bits, ");
           sb.append(length()).append(" ").append(long.class.getSimpleName()).append("s x ");
           sb.append(elementSize()).append(" bits");
           sb.append("]");
           return sb.toString();
        }

        @Override
        public int bitSize() {
            return BIT_SIZE;
        }

        @Override
        public int length() {
            return LENGTH;
        }

        @Override
        public Class<Long> elementType() {
            return Long.class;
        }

        @Override
        public int elementSize() {
            return Long.SIZE;
        }

        @Override
        public Shapes.S64Bit shape() {
            return Shapes.S_64_BIT;
        }

        @Override
        Long64Vector op(FOp f) {
            long[] res = new long[length()];
            for (int i = 0; i < length(); i++) {
                res[i] = f.apply(i);
            }
            return new Long64Vector(res);
        }

        @Override
        Long64Vector op(Mask<Long, Shapes.S64Bit> o, FOp f) {
            long[] res = new long[length()];
            Long64Mask m = (Long64Mask) o;
            for (int i = 0; i < length(); i++) {
                if (m.bits[i]) {
                    res[i] = f.apply(i);
                }
            }
            return new Long64Vector(res);
        }

        // Factories

        @Override
        public Long64Vector zero() {
            return ZERO;
        }

        @Override
        public Long64Mask constantMask(boolean... bits) {
            return new Long64Mask(bits);
        }

        @Override
        public Long64Mask trueMask() {
            return Long64Mask.TRUE_MASK;
        }

        @Override
        public Long64Mask falseMask() {
            return Long64Mask.FALSE_MASK;
        }
    }
}