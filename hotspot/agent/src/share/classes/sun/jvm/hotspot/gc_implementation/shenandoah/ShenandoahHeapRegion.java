/*
 * Copyright (c) 2017, 2018, Red Hat, Inc. All rights reserved.
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

package sun.jvm.hotspot.gc_implementation.shenandoah;

import sun.jvm.hotspot.memory.ContiguousSpace;
import sun.jvm.hotspot.types.AddressField;
import sun.jvm.hotspot.types.CIntegerField;
import sun.jvm.hotspot.runtime.VM;
import sun.jvm.hotspot.runtime.VMObject;
import sun.jvm.hotspot.types.Type;
import sun.jvm.hotspot.types.TypeDataBase;
import sun.jvm.hotspot.debugger.Address;

import java.util.Observable;
import java.util.Observer;


public class ShenandoahHeapRegion extends VMObject {
    private static CIntegerField RegionSizeBytes;

    private static AddressField BottomField;
    private static AddressField TopField;
    private static AddressField EndField;

    static {
        VM.registerVMInitializedObserver(new Observer() {
            public void update(Observable o, Object data) {
                initialize(VM.getVM().getTypeDataBase());
            }
        });
    }

    static private synchronized void initialize(TypeDataBase db) {
        Type type = db.lookupType("ShenandoahHeapRegion");
        RegionSizeBytes = type.getCIntegerField("RegionSizeBytes");

        BottomField = type.getAddressField("_bottom");
        TopField = type.getAddressField("_top");
        EndField = type.getAddressField("_end");
    }

    public static long regionSizeBytes() { return RegionSizeBytes.getValue(); }

    public ShenandoahHeapRegion(Address addr) {
        super(addr);
    }

    public Address bottom() {
        return BottomField.getValue(addr);
    }

    public Address top() {
        return TopField.getValue(addr);
    }

    public Address end() {
        return EndField.getValue(addr);
    }
}
