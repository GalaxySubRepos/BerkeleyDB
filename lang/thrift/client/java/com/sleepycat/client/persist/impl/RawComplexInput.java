/*-
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.client.persist.impl;

import com.sleepycat.client.persist.raw.RawObject;

import java.util.IdentityHashMap;

/**
 * Extends RawAbstractInput to convert complex (ComplexFormat and
 * CompositeKeyFormat) RawObject instances.
 *
 * @author Mark Hayes
 */
class RawComplexInput extends RawAbstractInput {

    private FieldInfo[] fields;
    private RawObject[] objects;
    private int index;

    RawComplexInput(Catalog catalog,
                    boolean rawAccess,
                    IdentityHashMap converted,
                    FieldInfo[] fields,
                    RawObject[] objects) {
        super(catalog, rawAccess, converted);
        this.fields = fields;
        this.objects = objects;
    }

    @Override
    Object readNext()
        throws RefreshException {

        RawObject raw = objects[index];
        FieldInfo field = fields[index];
        index += 1;
        Format format = field.getType();
        Object o = raw.getValues().get(field.getName());
        return checkAndConvert(o, format);
    }
}
