/*-
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.persist.impl;

import java.util.IdentityHashMap;

/**
 * Extends RawAbstractInput to convert array (ObjectArrayFormat and
 * PrimitiveArrayteKeyFormat) RawObject instances.
 *
 * @author Mark Hayes
 */
class RawSingleInput extends RawAbstractInput {

    private Object singleValue;
    private Format declaredFormat;

    RawSingleInput(Catalog catalog,
                   boolean rawAccess,
                   IdentityHashMap converted,
                   Object singleValue,
                   Format declaredFormat) {
        super(catalog, rawAccess, converted);
        this.singleValue = singleValue;
        this.declaredFormat = declaredFormat;
    }

    @Override
    Object readNext()
        throws RefreshException {

        return checkAndConvert(singleValue, declaredFormat);
    }
}
