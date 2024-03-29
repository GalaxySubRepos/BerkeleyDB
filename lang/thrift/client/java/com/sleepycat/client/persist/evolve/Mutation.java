/*-
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.client.persist.evolve;

import java.io.Serializable;

/**
 * The base class for all mutations.
 *
 * @see com.sleepycat.client.persist.evolve Class Evolution
 * @author Mark Hayes
 */
public abstract class Mutation implements Serializable {

    private static final long serialVersionUID = -8094431582953129268L;

    private String className;
    private int classVersion;
    private String fieldName;

    Mutation(String className, int classVersion, String fieldName) {
        this.className = className;
        this.classVersion = classVersion;
        this.fieldName = fieldName;
    }

    /**
     * Returns the class to which this mutation applies.
     *
     * @return  the class to which this mutation applies.
     */
    public String getClassName() {
        return className;
    }

    /**
     * Returns the class version to which this mutation applies.
     *
     * @return  the class version to which this mutation applies.
     */
    public int getClassVersion() {
        return classVersion;
    }

    /**
     * Returns the field name to which this mutation applies, or null if this
     * mutation applies to the class itself.
     *
     * @return  the field name to which this mutation applies, or null.
     */
    public String getFieldName() {
        return fieldName;
    }

    /**
     * Returns true if the class name, class version and field name are equal
     * in this object and given object.
     */
    @Override
    public boolean equals(Object other) {
        if (other instanceof Mutation) {
            Mutation o = (Mutation) other;
            return className.equals(o.className) &&
                   classVersion == o.classVersion &&
                   ((fieldName != null) ? fieldName.equals(o.fieldName)
                                        : (o.fieldName == null));
        } else {
            return false;
        }
    }

    @Override
    public int hashCode() {
        return className.hashCode() +
               classVersion +
               ((fieldName != null) ? fieldName.hashCode() : 0);
    }

    @Override
    public String toString() {
        return "Class: " + className + " Version: " + classVersion +
               ((fieldName != null) ? (" Field: " + fieldName) : "");
    }
}
