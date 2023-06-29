/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2012-2023. All rights reserved.
 */

package sun.jvm.hotspot.utilities;

import java.util.*;
import sun.jvm.hotspot.debugger.*;
import sun.jvm.hotspot.oops.*;
import sun.jvm.hotspot.types.*;
import sun.jvm.hotspot.runtime.*;
import sun.jvm.hotspot.utilities.*;

public class OffsetCompactHashTable extends CompactHashTable {
  static {
    VM.registerVMInitializedObserver(new Observer() {
      public void update(Observable o, Object data) {
        initialize(VM.getVM().getTypeDataBase());
      }
    });
  }

  private static synchronized void initialize(TypeDataBase db) throws WrongTypeException {
    Type type = db.lookupType("SymbolOffsetCompactHashtable");
    baseAddressField = type.getAddressField("_base_address");
    bucketCountField = type.getCIntegerField("_bucket_count");
    entryCountField = type.getCIntegerField("_entry_count");
    bucketsField = type.getAddressField("_buckets");
    entriesField = type.getAddressField("_entries");
    uintSize = db.lookupType("u4").getSize();
  }

  public OffsetCompactHashTable(Address addr) {
    super(addr);
  }

}


