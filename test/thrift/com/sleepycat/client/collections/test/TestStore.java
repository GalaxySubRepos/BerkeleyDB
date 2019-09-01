/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2002, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 */

package com.sleepycat.client.collections.test;

import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

import com.sleepycat.client.bind.EntityBinding;
import com.sleepycat.client.bind.EntryBinding;
import com.sleepycat.client.bind.RecordNumberBinding;
import com.sleepycat.client.collections.CurrentTransaction;
import com.sleepycat.client.compat.DbCompat;
import com.sleepycat.client.SDatabase;
import com.sleepycat.client.SDatabaseException;
import com.sleepycat.client.SEnvironment;
import com.sleepycat.client.SSecondaryConfig;

/**
 * @author Mark Hayes
 */
class TestStore {

    static final TestKeyCreator BYTE_EXTRACTOR = new TestKeyCreator(false);
    static final TestKeyCreator RECNO_EXTRACTOR = new TestKeyCreator(true);
    static final EntryBinding VALUE_BINDING = new TestDataBinding();
    static final EntryBinding BYTE_KEY_BINDING = VALUE_BINDING;
    static final EntryBinding RECNO_KEY_BINDING =
            new RecordNumberBinding(ByteOrder.nativeOrder());
    static final EntityBinding BYTE_ENTITY_BINDING =
            new TestEntityBinding(false);
    static final EntityBinding RECNO_ENTITY_BINDING =
            new TestEntityBinding(true);
    static final TestKeyAssigner BYTE_KEY_ASSIGNER =
            new TestKeyAssigner(false);
    static final TestKeyAssigner RECNO_KEY_ASSIGNER =
            new TestKeyAssigner(true);

    static final TestStore BTREE_UNIQ;
    static final TestStore BTREE_DUP;
    static final TestStore BTREE_DUPSORT;
    static final TestStore BTREE_RECNUM;
    static final TestStore HASH_UNIQ;
    static final TestStore HASH_DUP;
    static final TestStore HASH_DUPSORT;
    static final TestStore QUEUE;
    static final TestStore RECNO;
    static final TestStore RECNO_RENUM;

    static final TestStore[] ALL;
    static {
        List list = new ArrayList();
        SSecondaryConfig config;

        config = new SSecondaryConfig();
        DbCompat.setTypeBtree(config);
        BTREE_UNIQ = new TestStore("btree-uniq", config);
        BTREE_UNIQ.indexOf = BTREE_UNIQ;
        list.add(BTREE_UNIQ);

        if (DbCompat.INSERTION_ORDERED_DUPLICATES) {
            config = new SSecondaryConfig();
            DbCompat.setTypeBtree(config);
            DbCompat.setUnsortedDuplicates(config, true);
            BTREE_DUP = new TestStore("btree-dup", config);
            BTREE_DUP.indexOf = null; // indexes must use sorted dups
            list.add(BTREE_DUP);
        } else {
            BTREE_DUP = null;
        }

        config = new SSecondaryConfig();
        DbCompat.setTypeBtree(config);
        DbCompat.setSortedDuplicates(config, true);
        BTREE_DUPSORT = new TestStore("btree-dupsort", config);
        BTREE_DUPSORT.indexOf = BTREE_UNIQ;
        list.add(BTREE_DUPSORT);

        if (DbCompat.BTREE_RECNUM_METHOD) {
            config = new SSecondaryConfig();
            DbCompat.setTypeBtree(config);
            DbCompat.setBtreeRecordNumbers(config, true);
            BTREE_RECNUM = new TestStore("btree-recnum", config);
            BTREE_RECNUM.indexOf = BTREE_RECNUM;
            list.add(BTREE_RECNUM);
        } else {
            BTREE_RECNUM = null;
        }

        if (DbCompat.HASH_METHOD) {
            config = new SSecondaryConfig();
            DbCompat.setTypeHash(config);
            HASH_UNIQ = new TestStore("hash-uniq", config);
            HASH_UNIQ.indexOf = HASH_UNIQ;
            list.add(HASH_UNIQ);

            if (DbCompat.INSERTION_ORDERED_DUPLICATES) {
                config = new SSecondaryConfig();
                DbCompat.setTypeHash(config);
                DbCompat.setUnsortedDuplicates(config, true);
                HASH_DUP = new TestStore("hash-dup", config);
                HASH_DUP.indexOf = null; // indexes must use sorted dups
                list.add(HASH_DUP);
            } else {
                HASH_DUP = null;
            }

            config = new SSecondaryConfig();
            DbCompat.setTypeHash(config);
            DbCompat.setSortedDuplicates(config, true);
            HASH_DUPSORT = new TestStore("hash-dupsort", config);
            HASH_DUPSORT.indexOf = HASH_UNIQ;
            list.add(HASH_DUPSORT);
        } else {
            HASH_UNIQ = null;
            HASH_DUP = null;
            HASH_DUPSORT = null;
        }

        QUEUE = null;

        RECNO = null;
        RECNO_RENUM = null;

        ALL = new TestStore[list.size()];
        list.toArray(ALL);
    }

    private String name;
    private SSecondaryConfig config;
    private TestStore indexOf;
    private boolean isRecNumFormat;

    private TestStore(String name, SSecondaryConfig config) {

        this.name = name;
        this.config = config;

        isRecNumFormat = isQueueOrRecno() ||
                            (DbCompat.isTypeBtree(config) &&
                             DbCompat.getBtreeRecordNumbers(config));
    }

    EntryBinding getValueBinding() {

        return VALUE_BINDING;
    }

    EntryBinding getKeyBinding() {

        return isRecNumFormat ? RECNO_KEY_BINDING : BYTE_KEY_BINDING;
    }

    EntityBinding getEntityBinding() {

        return isRecNumFormat ? RECNO_ENTITY_BINDING : BYTE_ENTITY_BINDING;
    }

    TestKeyAssigner getKeyAssigner() {

        if (isQueueOrRecno()) {
            return null;
        } else {
            if (isRecNumFormat) {
                return RECNO_KEY_ASSIGNER;
            } else {
                return BYTE_KEY_ASSIGNER;
            }
        }
    }

    String getName() {

        return name;
    }

    boolean isOrdered() {

        return !DbCompat.isTypeHash(config);
    }

    boolean isQueueOrRecno() {

        return DbCompat.isTypeQueue(config) || DbCompat.isTypeRecno(config);
    }

    boolean areKeyRangesAllowed() {
        return isOrdered() && !isQueueOrRecno();
    }

    boolean areDuplicatesAllowed() {

        return DbCompat.getSortedDuplicates(config) ||
               DbCompat.getUnsortedDuplicates(config);
    }

    boolean hasRecNumAccess() {

        return isRecNumFormat;
    }

    boolean areKeysRenumbered() {

        return hasRecNumAccess() &&
                (DbCompat.isTypeBtree(config) ||
                 DbCompat.getRenumbering(config));
    }

    TestStore getIndexOf() {

        return DbCompat.SECONDARIES ? indexOf : null;
    }

    SDatabase open(SEnvironment env, String fileName)
        throws SDatabaseException {

        int fixedLen = (isQueueOrRecno() ? 1 : 0);
        return openDb(env, fileName, fixedLen, null);
    }

    SDatabase openIndex(SDatabase primary, String fileName)
        throws SDatabaseException {

        int fixedLen = (isQueueOrRecno() ? 4 : 0);
        config.setKeyCreator(isRecNumFormat ? RECNO_EXTRACTOR
                                            : BYTE_EXTRACTOR);
        SEnvironment env = primary.getEnvironment();
        return openDb(env, fileName, fixedLen, primary);
    }

    private SDatabase openDb(SEnvironment env, String fileName, int fixedLen,
                            SDatabase primary)
        throws SDatabaseException {

        if (fixedLen > 0) {
            DbCompat.setRecordLength(config, fixedLen);
            DbCompat.setRecordPad(config, 0);
        } else {
            DbCompat.setRecordLength(config, 0);
        }
        config.setAllowCreate(true);
        DbCompat.setReadUncommitted(config, true);
        if (primary != null) {
            return DbCompat.testOpenSecondaryDatabase
                (env, null, fileName, null, primary, config);
        } else {
            return DbCompat.testOpenDatabase
                (env, null, fileName, null, config);
        }
    }
}