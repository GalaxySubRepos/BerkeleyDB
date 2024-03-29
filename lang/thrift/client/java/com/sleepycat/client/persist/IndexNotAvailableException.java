/*
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.client.persist;

import com.sleepycat.client.SDatabaseException;

/**
 * Thrown by the {@link EntityStore#getPrimaryIndex getPrimaryIndex}, {@link
 * EntityStore#getSecondaryIndex getSecondaryIndex} and {@link
 * EntityStore#getSubclassIndex getSubclassIndex} when an index has not yet
 * been created.
 *
 * <p>It can be thrown when opening an environment read-only with new
 * persistent classes that define a new primary or secondary index.  The index
 * does not exist because the environment has not yet been opened read-write
 * with the new classes.  When the index is created by a read-write
 * application, the read-only application must close and re-open the
 * environment in order to open the new index.</p>
 *
 * @author Mark Hayes
 */
public class IndexNotAvailableException extends SDatabaseException {

    private static final long serialVersionUID = 1L;

    /** 
     * For internal use only.
     *
     * @param message the message.
     */
    public IndexNotAvailableException(String message) {
        super(message);
    }

}
