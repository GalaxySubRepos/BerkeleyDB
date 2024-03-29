/*-
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.client.persist.impl;

import java.io.Serializable;

import com.sleepycat.client.persist.model.EntityModel;

/**
 * Interface to the "read object" methods of the Format class.  For the
 * latest version format, the Format object provides the implementation of
 * these methods.  For an older version format, an evolver object implements
 * this interface to convert from the old to new format.
 *
 * See {@link Format} for a description of each method.
 * @author Mark Hayes
 */
interface Reader extends Serializable {

    void initializeReader(Catalog catalog,
                          EntityModel model,
                          int initVersion,
                          Format oldFormat);

    Object newInstance(EntityInput input, boolean rawAccess)
        throws RefreshException;

    void readPriKey(Object o, EntityInput input, boolean rawAccess)
        throws RefreshException;

    Object readObject(Object o, EntityInput input, boolean rawAccess)
        throws RefreshException;
        
    Accessor getAccessor(boolean rawAccess);
}
