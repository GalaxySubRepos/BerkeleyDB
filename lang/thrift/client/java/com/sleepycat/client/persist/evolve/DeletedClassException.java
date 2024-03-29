/*-
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 * $Id$
 */

package com.sleepycat.client.persist.evolve;

/**
 * While reading from an index, an instance of a deleted class version was
 * encountered.
 *
 * @see com.sleepycat.client.persist.evolve Class Evolution
 * @author Mark Hayes
 */
public class DeletedClassException extends RuntimeException {

    private static final long serialVersionUID = 518500929L;

    public DeletedClassException(String msg) {
        super(msg);
    }
}
