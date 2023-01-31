/*
 * Copyright (C) 2021, 2023, THL A29 Limited, a Tencent company. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. THL A29 Limited designates
 * this particular file as subject to the "Classpath" exception as provided
 * in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

package jdk.jfr.events;

import jdk.jfr.Category;
import jdk.jfr.Description;
import jdk.jfr.Label;
import jdk.jfr.DataAmount;
import jdk.jfr.Name;
import jdk.jfr.Timespan;
import jdk.jfr.internal.Type;

@Name(Type.EVENT_NAME_PREFIX + "JavaNativeReallocate")
@Label("Java Native Reallocate")
@Category("Java Application")
@Description("Reallocate memory when using native")
public class JavaNativeReallocateEvent extends AbstractJDKEvent {

    public static final ThreadLocal<JavaNativeReallocateEvent> EVENT =
        new ThreadLocal<>() {
            @Override protected JavaNativeReallocateEvent initialValue() {
                return new JavaNativeReallocateEvent();
            }
        };

    @Label("Free Addr")
    public long freeAddr;

    @Label("Alloc Addr")
    public long allocAddr;

    @Label("Allocation Size")
    @DataAmount
    public long allocationSize;

    public void reset() {
        allocationSize = 0;
        freeAddr = 0;
        allocAddr = 0;
    }
}
