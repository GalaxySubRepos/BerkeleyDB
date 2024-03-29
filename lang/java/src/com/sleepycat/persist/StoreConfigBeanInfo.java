/*-
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */

package com.sleepycat.persist;

import com.sleepycat.util.ConfigBeanInfoBase;

import java.beans.BeanDescriptor;
import java.beans.PropertyDescriptor;

public class StoreConfigBeanInfo extends ConfigBeanInfoBase {

    @Override
    public BeanDescriptor getBeanDescriptor() {
        return getBdescriptor(StoreConfig.class);
    }

    @Override
    public PropertyDescriptor[] getPropertyDescriptors() {
        return getPdescriptor(StoreConfig.class);
    }
}
