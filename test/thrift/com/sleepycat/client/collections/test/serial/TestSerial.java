/*-
 * Copyright (c) 2000, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */
package com.sleepycat.client.collections.test.serial;

/**
 * @see StoredClassCatalogTest
 * @author Mark Hayes
 */
class TestSerial implements java.io.Serializable {

    static final long serialVersionUID = -3738980000390384920L;

    private int i = 123;
    private TestSerial other;

    // The following field 's' was added after this class was compiled and
    // serialized instances were saved in resource files.  This allows testing
    // that the original stored instances can be deserialized after changing
    // the class.  The serialVersionUID is needed for this according to Java
    // serialization rules, and was generated with the serialver tool.
    //
    private String s = "string";

    TestSerial(TestSerial other) {

        this.other = other;
    }

    TestSerial getOther() {

        return other;
    }

    int getIntField() {

        return i;
    }

    String getStringField() {

        return s; // this returned null before field 's' was added.
    }

    public boolean equals(Object object) {

        try {
            TestSerial o = (TestSerial) object;
            if ((o.other == null) ? (this.other != null)
                                  : (!o.other.equals(this.other))) {
                return false;
            }
            if (this.i != o.i) {
                return false;
            }
            // the following test was not done before field 's' was added
            if ((o.s == null) ? (this.s != null)
                              : (!o.s.equals(this.s))) {
                return false;
            }
            return true;
        } catch (ClassCastException e) {
            return false;
        }
    }
}
