/*-
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.client.bind.serial.test;

import java.io.ObjectStreamClass;
import java.util.HashMap;
import java.util.Map;

import com.sleepycat.client.bind.serial.ClassCatalog;
import com.sleepycat.client.bind.tuple.IntegerBinding;
import com.sleepycat.client.SDatabaseEntry;
import com.sleepycat.client.SDatabaseException;

/**
 * @author Mark Hayes
 */
public class TestClassCatalog implements ClassCatalog {

    private final Map<Integer, ObjectStreamClass> idToDescMap =
        new HashMap<Integer, ObjectStreamClass>();
    private final Map<String, Integer> nameToIdMap =
        new HashMap<String, Integer>();
    private int nextId = 1;

    public TestClassCatalog() {
    }

    public void close() {
    }

    public synchronized byte[] getClassID(ObjectStreamClass desc) {
        String className = desc.getName();
        Integer intId = nameToIdMap.get(className);
        if (intId == null) {
            intId = nextId;
            nextId += 1;

            idToDescMap.put(intId, desc);
            nameToIdMap.put(className, intId);
        }
        SDatabaseEntry entry = new SDatabaseEntry();
        IntegerBinding.intToEntry(intId, entry);
        return entry.getData();
    }

    public synchronized ObjectStreamClass getClassFormat(byte[] byteId)
        throws SDatabaseException {

        SDatabaseEntry entry = new SDatabaseEntry();
        entry.setData(byteId);
        int intId = IntegerBinding.entryToInt(entry);

        ObjectStreamClass desc = (ObjectStreamClass) idToDescMap.get(intId);
        if (desc == null) {
            throw new RuntimeException("classID not found");
        }
        return desc;
    }

    public ClassLoader getClassLoader() {
        return null;
    }
}
