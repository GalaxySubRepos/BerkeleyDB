/*-
 * Copyright (c) 2000, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.client.bind;

import com.sleepycat.client.SDatabaseEntry;

/**
 * A binding between a key-value entry pair and an entity object.
 *
 * <p><em>WARNING:</em> Binding instances are typically shared by multiple
 * threads and binding methods are called without any special synchronization.
 * Therefore, bindings must be thread safe.  In general no shared state should
 * be used and any caching of computed values must be done with proper
 * synchronization.</p>
 *
 * @author Mark Hayes
 */
public interface EntityBinding<E> {

    /**
     * Converts key and data entry buffers into an entity Object.
     *
     * @param key is the source key entry.
     *
     * @param data is the source data entry.
     *
     * @return the resulting Object.
     */
    E entryToObject(SDatabaseEntry key, SDatabaseEntry data);

    /**
     * Extracts the key entry from an entity Object.
     *
     * @param object is the source Object.
     *
     * @param key is the destination entry buffer.
     */
    void objectToKey(E object, SDatabaseEntry key);

    /**
     * Extracts the data entry from an entity Object.
     *
     * @param object is the source Object.
     *
     * @param data is the destination entry buffer.
     */
    void objectToData(E object, SDatabaseEntry data);
}
