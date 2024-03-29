/*-
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.client.persist.raw;

import java.util.List;
import java.util.Map;

import com.sleepycat.client.persist.model.ClassMetadata;
import com.sleepycat.client.persist.model.Entity;
import com.sleepycat.client.persist.model.EntityMetadata;
import com.sleepycat.client.persist.model.Persistent;

/**
 * The type definition for a simple or complex persistent type, or an array
 * of persistent types.
 *
 * <p>{@code RawType} objects are thread-safe.  Multiple threads may safely
 * call the methods of a shared {@code RawType} object.</p>
 *
 * @author Mark Hayes
 */
public interface RawType {

    /**
     * Returns the class name for this type in the format specified by {@link
     * Class#getName}.
     *
     * <p>If this class currently exists (has not been removed or renamed) then
     * the class name may be passed to {@link Class#forName} to get the current
     * {@link Class} object.  However, if this raw type is not the current
     * version of the class, this type information may differ from that of the
     * current {@link Class}.</p>
     *
     * @return the class name.
     */
    String getClassName();

    /**
     * Returns the class version for this type.  For simple types, zero is
     * always returned.
     *
     * @return the version.
     *
     * @see Entity#version
     * @see Persistent#version
     */
    int getVersion();

    /**
     * Returns the internal unique ID for this type.
     *
     * @return the ID.
     */
    int getId();

    /**
     * Returns whether this is a 
     * <a href="{@docRoot}/com/sleepycat/persist/model/Entity.html#simpleTypes">simple type</a>:
     * primitive, primitive wrapper, BigInteger, BigDecimal, String or Date.
     *
     * <p>If true is returned, {@link #isPrimitive} can be called for more
     * information, and a raw value of this type is represented as a simple
     * type object (not as a {@link RawObject}).</p>
     *
     * <p>If false is returned, this is a complex type, an array type (see
     * {@link #isArray}), or an enum type, and a raw value of this type is
     * represented as a {@link RawObject}.</p>
     *
     * @return whether this is a simple type.
     */
    boolean isSimple();

    /**
     * Returns whether this type is a Java primitive: char, byte, short, int,
     * long, float or double.
     *
     * <p>If true is returned, this is also a simple type.  In other words,
     * primitive types are a subset of simple types.</p>
     *
     * <p>If true is returned, a raw value of this type is represented as a
     * non-null instance of the primitive type's wrapper class.  For example,
     * an <code>int</code> raw value is represented as an
     * <code>Integer</code>.</p>
     *
     * @return whether this is a Java primitive.
     */
    boolean isPrimitive();

    /**
     * Returns whether this is an enum type.
     *
     * <p>If true is returned, a value of this type is a {@link RawObject} and
     * the enum constant String is available via {@link RawObject#getEnum}.</p>
     *
     * <p>If false is returned, then this is a complex type, an array type (see
     * {@link #isArray}), or a simple type (see {@link #isSimple}).</p>
     *
     * @return whether this is a enum type.
     */
    boolean isEnum();

    /**
     * Returns an unmodifiable list of the names of the enum instances, or null
     * if this is not an enum type.
     *
     * @return the list of enum names.
     */
    List<String> getEnumConstants();

    /**
     * Returns whether this is an array type.  Raw value arrays are represented
     * as {@link RawObject} instances.
     *
     * <p>If true is returned, the array component type is returned by {@link
     * #getComponentType} and the number of array dimensions is returned by
     * {@link #getDimensions}.</p>
     *
     * <p>If false is returned, then this is a complex type, an enum type (see
     * {@link #isEnum}), or a simple type (see {@link #isSimple}).</p>
     *
     * @return whether this is an array type.
     */
    boolean isArray();

    /**
     * Returns the number of array dimensions, or zero if this is not an array
     * type.
     *
     * @return the number of array dimensions, or zero if this is not an array
     * type.
     */
    int getDimensions();

    /**
     * Returns the array component type, or null if this is not an array type.
     *
     * @return the array component type, or null if this is not an array type.
     */
    RawType getComponentType();

    /**
     * Returns a map of field name to raw field for each non-static
     * non-transient field declared in this class, or null if this is not a
     * complex type (in other words, this is a simple type or an array type).
     *
     * @return a map of field name to raw field, or null.
     */
    Map<String, RawField> getFields();

    /**
     * Returns the type of the superclass, or null if the superclass is Object
     * or this is not a complex type (in other words, this is a simple type or
     * an array type).
     *
     * @return the type of the superclass, or null.
     */
    RawType getSuperType();

    /**
     * Returns the original model class metadata used to create this class, or
     * null if this is not a model class.
     *
     * @return the model class metadata, or null.
     */
    ClassMetadata getClassMetadata();

    /**
     * Returns the original model entity metadata used to create this class, or
     * null if this is not an entity class.
     *
     * @return the model entity metadata, or null.
     */
    EntityMetadata getEntityMetadata();

    /**
     * Returns whether this type has been deleted using a class {@code Deleter}
     * mutation.  A deleted type may be returned by {@link
     * com.sleepycat.client.persist.model.EntityModel#getRawTypeVersion
     * EntityModel.getRawTypeVersion} or {@link
     * com.sleepycat.client.persist.model.EntityModel#getAllRawTypeVersions
     * EntityModel.getAllRawTypeVersions}.
     *
     * @return whether this type has been deleted.
     */
    boolean isDeleted();

    /**
     * Returns an XML representation of the raw type.
     */
    String toString();
}
