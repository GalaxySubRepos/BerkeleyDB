/*-
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.client.persist;

import java.util.Iterator;
import java.util.NoSuchElementException;

import com.sleepycat.client.SDatabaseException;
import com.sleepycat.client.SLockMode;
import com.sleepycat.client.util.RuntimeExceptionWrapper;

/**
 * Implements Iterator for an arbitrary EntityCursor.
 *
 * @author Mark Hayes
 */
class BasicIterator<V> implements Iterator<V> {

    private EntityCursor<V> entityCursor;
    private ForwardCursor<V> forwardCursor;
    private SLockMode lockMode;
    private V nextValue;

    /**
     * An EntityCursor is given and the remove() method is supported.
     */
    BasicIterator(EntityCursor<V> entityCursor, SLockMode lockMode) {
        this.entityCursor = entityCursor;
        this.forwardCursor = entityCursor;
        this.lockMode = lockMode;
    }

    /**
     * A ForwardCursor is given and the remove() method is not supported.
     */
    BasicIterator(ForwardCursor<V> forwardCursor, SLockMode lockMode) {
        this.forwardCursor = forwardCursor;
        this.lockMode = lockMode;
    }

    public boolean hasNext() {
        if (nextValue == null) {
            try {
                nextValue = forwardCursor.next(lockMode);
            } catch (SDatabaseException e) {
                throw RuntimeExceptionWrapper.wrapIfNeeded(e);
            }
            return nextValue != null;
        } else {
            return true;
        }
    }

    public V next() {
        if (hasNext()) {
            V v = nextValue;
            nextValue = null;
            return v;
        } else {
            throw new NoSuchElementException();
        }
    }

    public void remove() {
        if (entityCursor == null) {
            throw new UnsupportedOperationException();
        }
        try {
            if (!entityCursor.delete()) {
                throw new IllegalStateException
                    ("Record at cursor position is already deleted");
            }
        } catch (SDatabaseException e) {
            throw RuntimeExceptionWrapper.wrapIfNeeded(e);
        }
    }
}
