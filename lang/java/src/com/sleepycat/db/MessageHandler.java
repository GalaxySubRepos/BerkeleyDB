/*-
 * Copyright (c) 1997, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 * $Id$
 */
package com.sleepycat.db;

/**
An interface specifying a callback function to be called to display
informational messages.
*/
public interface MessageHandler {
    /**
    A callback function to be called to display informational messages.
    <p>
    There are interfaces in the Berkeley DB library which either directly
    output informational messages or statistical information, or configure
    the library to output such messages when performing other operations,
    {@link com.sleepycat.db.EnvironmentConfig#setVerboseDeadlock EnvironmentConfig.setVerboseDeadlock} for example.
    <p>
    The {@link com.sleepycat.db.EnvironmentConfig#setMessageHandler EnvironmentConfig.setMessageHandler} and
    {@link com.sleepycat.db.DatabaseConfig#setMessageHandler DatabaseConfig.setMessageHandler} methods are used to
    display these messages for the application.
    <p>                 
    @param environment  
    The enclosing database environment handle.
    <p>
    @param msgpfx
    The prefix string, as previously configured by
    {@link com.sleepycat.db.EnvironmentConfig#setMessagePrefix EnvironmentConfig.setMessagePrefix} or
    {@link com.sleepycat.db.DatabaseConfig#setMessagePrefix DatabaseConfig.setMessagePrefix}.
    <p>
    @param message
    An informational message string.
    */
    void message(Environment environment, String msgpfx, String message);
}
