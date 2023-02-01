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

@Name(Type.EVENT_NAME_PREFIX + "JavaNativeAllocation")
@Label("Java Native Allocation")
@Category("Java Application")
@Description("Allocation memory when using native")
public final class JavaNativeAllocationEvent extends AbstractJDKEvent {

    public static final ThreadLocal<JavaNativeAllocationEvent> EVENT =
        new ThreadLocal<>() {
            @Override protected JavaNativeAllocationEvent initialValue() {
                return new JavaNativeAllocationEvent();
            }
        };

    @Label("Addr")
    public long addr;

    @Label("Allocation Size")
    @DataAmount
    public long allocationSize;

    public void reset() {
        addr = 0;
        allocationSize = 0;
    }
}
