/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2000, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 */

package com.sleepycat.client.collections;

import com.sleepycat.client.compat.DbCompat;
import com.sleepycat.client.bind.EntityBinding;
import com.sleepycat.client.bind.EntryBinding;
import com.sleepycat.client.SCursorConfig;
import com.sleepycat.client.SDatabase;
import com.sleepycat.client.SDatabaseConfig;
import com.sleepycat.client.SDatabaseEntry;
import com.sleepycat.client.SDatabaseException;
import com.sleepycat.client.SEnvironment;
import com.sleepycat.client.SJoinConfig;
import com.sleepycat.client.SOperationStatus;
import com.sleepycat.client.SSecondaryConfig;
import com.sleepycat.client.SSecondaryDatabase;
import com.sleepycat.client.SSecondaryKeyCreator;
import com.sleepycat.client.STransaction;
import com.sleepycat.client.util.RuntimeExceptionWrapper;
import com.sleepycat.client.util.keyrange.KeyRange;
import com.sleepycat.client.util.keyrange.KeyRangeException;

import java.nio.ByteOrder;

/**
 * Represents a Berkeley DB database and adds support for indices, bindings and
 * key ranges.
 *
 * <p>This class defines a view and takes care of reading and updating indices,
 * calling bindings, constraining access to a key range, etc.</p>
 *
 * @author Mark Hayes
 */
final class DataView implements Cloneable {

    SDatabase db;
    SSecondaryDatabase secDb;
    CurrentTransaction currentTxn;
    KeyRange range;
    EntryBinding keyBinding;
    EntryBinding valueBinding;
    EntityBinding entityBinding;
    PrimaryKeyAssigner keyAssigner;
    SSecondaryKeyCreator secKeyCreator;
    SCursorConfig cursorConfig;      // Used for all operations via this view
    boolean writeAllowed;           // Read-write view
    boolean ordered;                // Not a HASH Db
    boolean keyRangesAllowed;       // BTREE only
    boolean recNumAllowed;          // QUEUE, RECNO, or BTREE-RECNUM Db
    boolean recNumAccess;           // recNumAllowed && using a rec num binding
    boolean btreeRecNumDb;          // BTREE-RECNUM Db
    boolean btreeRecNumAccess;      // recNumAccess && BTREE-RECNUM Db
    boolean recNumRenumber;         // RECNO-RENUM Db
    boolean keysRenumbered;         // recNumRenumber || btreeRecNumAccess
    boolean dupsAllowed;            // Dups configured
    boolean dupsOrdered;            // Sorted dups configured
    boolean transactional;          // Db is transactional
    boolean readUncommittedAllowed; // Read-uncommited is optional in DB-CORE

    /*
     * If duplicatesView is called, dupsView will be true and dupsKey will be
     * the secondary key used as the "single key" range.  dupRange will be set
     * as the range of the primary key values if subRange is subsequently
     * called, to further narrow the view.
     */
    SDatabaseEntry dupsKey;
    boolean dupsView;
    KeyRange dupsRange;

    /**
     * Creates a view for a given database and bindings.  The initial key range
     * of the view will be open.
     */
    DataView(SDatabase database, EntryBinding keyBinding,
             EntryBinding valueBinding, EntityBinding entityBinding,
             boolean writeAllowed, PrimaryKeyAssigner keyAssigner)
        throws IllegalArgumentException {

        if (database == null) {
            throw new IllegalArgumentException("database is null");
        }
        db = database;
        try {
            currentTxn =
                CurrentTransaction.getInstanceInternal(db.getEnvironment());
            SDatabaseConfig dbConfig;
            if (db instanceof SSecondaryDatabase) {
                secDb = (SSecondaryDatabase) database;
                SSecondaryConfig secConfig = secDb.getSecondaryConfig();
                secKeyCreator = secConfig.getKeyCreator();
                dbConfig = secConfig;
            } else {
                dbConfig = db.getConfig();
            }
            ordered = !DbCompat.isTypeHash(dbConfig);
            keyRangesAllowed = DbCompat.isTypeBtree(dbConfig);
            recNumAllowed = DbCompat.isTypeQueue(dbConfig) ||
                            DbCompat.isTypeRecno(dbConfig) ||
                            DbCompat.getBtreeRecordNumbers(dbConfig);
            recNumRenumber = DbCompat.getRenumbering(dbConfig);
            dupsAllowed = DbCompat.getSortedDuplicates(dbConfig) ||
                          DbCompat.getUnsortedDuplicates(dbConfig);
            dupsOrdered = DbCompat.getSortedDuplicates(dbConfig);
            transactional = currentTxn.isTxnMode() &&
                            dbConfig.getTransactional();
            readUncommittedAllowed = DbCompat.getReadUncommitted(dbConfig);
            btreeRecNumDb = recNumAllowed && DbCompat.isTypeBtree(dbConfig);
            range = new KeyRange(dbConfig.getBtreeComparator());
        } catch (SDatabaseException e) {
            throw RuntimeExceptionWrapper.wrapIfNeeded(e);
        }
        this.writeAllowed = writeAllowed;
        this.keyBinding = keyBinding;
        this.valueBinding = valueBinding;
        this.entityBinding = entityBinding;
        this.keyAssigner = keyAssigner;
        cursorConfig = SCursorConfig.DEFAULT;

        if (valueBinding != null && entityBinding != null)
            throw new IllegalArgumentException
                ("both valueBinding and entityBinding are non-null");

        if (keyBinding instanceof com.sleepycat.client.bind.RecordNumberBinding) {
            if (!recNumAllowed) {
                throw new IllegalArgumentException
                    ("RecordNumberBinding requires DB_BTREE/DB_RECNUM, " +
                     "DB_RECNO, or DB_QUEUE");
            }
            recNumAccess = true;
            if (btreeRecNumDb) {
                btreeRecNumAccess = true;
            }
        }
        keysRenumbered = recNumRenumber || btreeRecNumAccess;
    }

    /**
     * Clones the view.
     */
    private DataView cloneView() {

        try {
            return (DataView) super.clone();
        } catch (CloneNotSupportedException willNeverOccur) {
            throw DbCompat.unexpectedState();
        }
    }

    /**
     * Return a new key-set view derived from this view by setting the
     * entity and value binding to null.
     *
     * @return the derived view.
     */
    DataView keySetView() {

        if (keyBinding == null) {
            throw new UnsupportedOperationException("Must have keyBinding");
        }
        DataView view = cloneView();
        view.valueBinding = null;
        view.entityBinding = null;
        return view;
    }

    /**
     * Return a new value-set view derived from this view by setting the
     * key binding to null.
     *
     * @return the derived view.
     */
    DataView valueSetView() {

        if (valueBinding == null && entityBinding == null) {
            throw new UnsupportedOperationException
                ("Must have valueBinding or entityBinding");
        }
        DataView view = cloneView();
        view.keyBinding = null;
        return view;
    }

    /**
     * Return a new value-set view for single key range.
     *
     * @param singleKey the single key value.
     *
     * @return the derived view.
     *
     * @throws SDatabaseException if a database problem occurs.
     *
     * @throws KeyRangeException if the specified range is not within the
     * current range.
     */
    DataView valueSetView(Object singleKey)
        throws SDatabaseException, KeyRangeException {

        /*
         * Must do subRange before valueSetView since the latter clears the
         * key binding needed for the former.
         */
        KeyRange singleKeyRange = subRange(range, singleKey);
        DataView view = valueSetView();
        view.range = singleKeyRange;
        return view;
    }

    /**
     * Return a new value-set view for key range, optionally changing
     * the key binding.
     */
    DataView subView(Object beginKey, boolean beginInclusive,
                     Object endKey, boolean endInclusive,
                     EntryBinding keyBinding)
        throws SDatabaseException, KeyRangeException {

        DataView view = cloneView();
        view.setRange(beginKey, beginInclusive, endKey, endInclusive);
        if (keyBinding != null) view.keyBinding = keyBinding;
        return view;
    }

    /**
     * Return a new duplicates view for a given secondary key.
     */
    DataView duplicatesView(Object secondaryKey,
                            EntryBinding primaryKeyBinding)
        throws SDatabaseException, KeyRangeException {

        if (!isSecondary()) {
            throw new UnsupportedOperationException
                ("Only allowed for maps on secondary databases");
        }
        if (dupsView) {
            throw DbCompat.unexpectedState();
        }
        DataView view = cloneView();
        view.range = subRange(view.range, secondaryKey);
        view.dupsKey = view.range.getSingleKey();
        view.dupsView = true;
        view.keyBinding = primaryKeyBinding;
        return view;
    }

    /**
     * Returns a new view with a specified cursor configuration.
     */
    DataView configuredView(SCursorConfig config) {

        DataView view = cloneView();
        view.cursorConfig = (config != null) ?
            DbCompat.cloneCursorConfig(config) : SCursorConfig.DEFAULT;
        return view;
    }

    /**
     * Returns the current transaction for the view or null if the environment
     * is non-transactional.
     */
    CurrentTransaction getCurrentTxn() {

        return transactional ? currentTxn : null;
    }

    /**
     * Sets this view's range to a subrange with the given parameters.
     */
    private void setRange(Object beginKey, boolean beginInclusive,
                          Object endKey, boolean endInclusive)
        throws SDatabaseException, KeyRangeException {

        if ((beginKey != null || endKey != null) && !keyRangesAllowed) {
            throw new UnsupportedOperationException
                ("Key ranges allowed only for BTREE databases");
        }
        KeyRange useRange = useSubRange();
        useRange = subRange
            (useRange, beginKey, beginInclusive, endKey, endInclusive);
        if (dupsView) {
            dupsRange = useRange;
        } else {
            range = useRange;
        }
    }

    /**
     * Returns the key thang for a single key range, or null if a single key
     * range is not used.
     */
    SDatabaseEntry getSingleKeyThang() {

        return range.getSingleKey();
    }

    /**
     * Returns the environment for the database.
     */
    final SEnvironment getEnv() {

        return currentTxn.getEnvironment();
    }

    /**
     * Returns whether this is a view on a secondary database rather
     * than directly on a primary database.
     */
    final boolean isSecondary() {

        return (secDb != null);
    }

    /**
     * Returns whether no records are present in the view.
     *
     * Auto-commit must be performed by the caller.
     */
    boolean isEmpty()
        throws SDatabaseException {

        DataCursor cursor = new DataCursor(this, false);
        try {
            return cursor.getFirst(false) != SOperationStatus.SUCCESS;
        } finally {
            cursor.close();
        }
    }

    /**
     * Appends a value and returns the new key.  If a key assigner is used
     * it assigns the key, otherwise a QUEUE or RECNO database is required.
     *
     * Auto-commit must be performed by the caller.
     */
    SOperationStatus append(Object value,
                           Object[] retPrimaryKey,
                           Object[] retValue)
        throws SDatabaseException {

        /*
         * Flags will be NOOVERWRITE if used with assigner, or APPEND
         * otherwise.
         * Requires: if value param, value or entity binding
         * Requires: if retPrimaryKey, primary key binding (no index).
         * Requires: if retValue, value or entity binding
         */
        SDatabaseEntry keyThang = new SDatabaseEntry();
        SDatabaseEntry valueThang = new SDatabaseEntry();
        useValue(value, valueThang, null);
        SOperationStatus status;
        if (keyAssigner != null) {
            keyAssigner.assignKey(keyThang);
            if (!range.check(keyThang)) {
                throw new IllegalArgumentException
                    ("assigned key out of range");
            }
            DataCursor cursor = new DataCursor(this, true);
            try {
                status = cursor.getCursor().putNoOverwrite(keyThang,
                                                           valueThang);
            } finally {
                cursor.close();
            }
        } else {
            throw new IllegalStateException("Key assigner must be used.");
        }
        if (status == SOperationStatus.SUCCESS) {
            returnPrimaryKeyAndValue(keyThang, valueThang,
                                     retPrimaryKey, retValue);
        }
        return status;
    }

    /**
     * Returns the current transaction if the database is transaction, or null
     * if the database is not transactional or there is no current transaction.
     */
    STransaction useTransaction() {
        return transactional ?  currentTxn.getTransaction() : null;
    }

    /**
     * Deletes all records in the current range.
     *
     * Auto-commit must be performed by the caller.
     */
    void clear()
        throws SDatabaseException {

        DataCursor cursor = new DataCursor(this, true);
        try {
            SOperationStatus status = SOperationStatus.SUCCESS;
            while (status == SOperationStatus.SUCCESS) {
                if (keysRenumbered) {
                    status = cursor.getFirst(true);
                } else {
                    status = cursor.getNext(true);
                }
                if (status == SOperationStatus.SUCCESS) {
                    cursor.delete();
                }
            }
        } finally {
            cursor.close();
        }
    }

    /**
     * Returns a cursor for this view that reads only records having the
     * specified index key values.
     */
    DataCursor join(DataView[] indexViews, Object[] indexKeys,
                    SJoinConfig joinConfig)
        throws SDatabaseException {

        DataCursor joinCursor = null;
        DataCursor[] indexCursors = new DataCursor[indexViews.length];
        try {
            for (int i = 0; i < indexViews.length; i += 1) {
                indexCursors[i] = new DataCursor(indexViews[i], false);
                indexCursors[i].getSearchKey(indexKeys[i], null, false);
            }
            joinCursor = new DataCursor(this, indexCursors, joinConfig, true);
            return joinCursor;
        } finally {
            if (joinCursor == null) {
                // An exception is being thrown, so close cursors we opened.
                for (int i = 0; i < indexCursors.length; i += 1) {
                    if (indexCursors[i] != null) {
                        try { indexCursors[i].close(); }
                        catch (Exception e) {
                            /* FindBugs, this is ok. */
                        }
                    }
                }
            }
        }
    }

    /**
     * Returns a cursor for this view that reads only records having the
     * index key values at the specified cursors.
     */
    DataCursor join(DataCursor[] indexCursors, SJoinConfig joinConfig)
        throws SDatabaseException {

        return new DataCursor(this, indexCursors, joinConfig, false);
    }

    /**
     * Returns primary key and value if return parameters are non-null.
     */
    private void returnPrimaryKeyAndValue(SDatabaseEntry keyThang,
                                          SDatabaseEntry valueThang,
                                          Object[] retPrimaryKey,
                                          Object[] retValue) {
        // Requires: if retPrimaryKey, primary key binding (no index).
        // Requires: if retValue, value or entity binding

        if (retPrimaryKey != null) {
            if (keyBinding == null) {
                throw new IllegalArgumentException
                    ("returning key requires primary key binding");
            } else if (isSecondary()) {
                throw new IllegalArgumentException
                    ("returning key requires unindexed view");
            } else {
                retPrimaryKey[0] = keyBinding.entryToObject(keyThang);
            }
        }
        if (retValue != null) {
            retValue[0] = makeValue(keyThang, valueThang);
        }
    }

    /**
     * Populates the key entry and returns whether the key is within range.
     */
    boolean useKey(Object key, Object value, SDatabaseEntry keyThang,
                   KeyRange checkRange)
        throws SDatabaseException {

        if (key != null) {
            if (keyBinding == null) {
                throw new IllegalArgumentException
                    ("non-null key with null key binding");
            }
            keyBinding.objectToEntry(key, keyThang);
        } else {
            if (value == null) {
                throw new IllegalArgumentException("null key and null value");
            }
            if (entityBinding == null) {
                throw new IllegalStateException
                    ("EntityBinding required to derive key from value");
            }
            if (!dupsView && isSecondary()) {
                SDatabaseEntry primaryKeyThang = new SDatabaseEntry();
                entityBinding.objectToKey(value, primaryKeyThang);
                SDatabaseEntry valueThang = new SDatabaseEntry();
                entityBinding.objectToData(value, valueThang);
                secKeyCreator.createSecondaryKey(secDb, primaryKeyThang,
                                                 valueThang, keyThang);
            } else {
                entityBinding.objectToKey(value, keyThang);
            }
        }
        if (recNumAccess &&
            DbCompat.getRecordNumber(keyThang, getServerByteOrder()) <= 0) {
            return false;
        }
        if (checkRange != null && !checkRange.check(keyThang)) {
            return false;
        }
        return true;
    }

    ByteOrder getServerByteOrder() {
        return db.getServerByteOrder();
    }

    /**
     * Returns whether data keys can be derived from the value/entity binding
     * of this view, which determines whether a value/entity object alone is
     * sufficient for operations that require keys.
     */
    final boolean canDeriveKeyFromValue() {

        return (entityBinding != null);
    }

    /**
     * Populates the value entry and throws an exception if the primary key
     * would be changed via an entity binding.
     */
    void useValue(Object value, SDatabaseEntry valueThang,
                  SDatabaseEntry checkKeyThang) {
        if (valueBinding != null) {
            /* Allow binding to handle null value. */
            valueBinding.objectToEntry(value, valueThang);
        } else if (entityBinding != null) {
            if (value == null) {
                throw new IllegalArgumentException
                    ("null value with entity binding");
            }
            entityBinding.objectToData(value, valueThang);
            if (checkKeyThang != null) {
                SDatabaseEntry thang = new SDatabaseEntry();
                entityBinding.objectToKey(value, thang);
                if (!KeyRange.equalBytes(thang, checkKeyThang)) {
                    throw new IllegalArgumentException
                        ("cannot change primary key");
                }
            }
        } else {
            if (value != null) {
                throw new IllegalArgumentException
                    ("non-null value with null value/entity binding");
            }
            valueThang.setData(KeyRange.ZERO_LENGTH_BYTE_ARRAY);
        }
    }

    /**
     * Converts a key entry to a key object.
     */
    Object makeKey(SDatabaseEntry keyThang, SDatabaseEntry priKeyThang) {

        if (keyBinding == null) {
            throw new UnsupportedOperationException();
        } else {
            SDatabaseEntry thang = dupsView ? priKeyThang : keyThang;
            if (thang.getSize() == 0) {
                return null;
            } else {
                return keyBinding.entryToObject(thang);
            }
        }
    }

    /**
     * Converts a key-value entry pair to a value object.
     */
    Object makeValue(SDatabaseEntry primaryKeyThang, SDatabaseEntry valueThang) {

        Object value;
        if (valueBinding != null) {
            value = valueBinding.entryToObject(valueThang);
        } else if (entityBinding != null) {
            value = entityBinding.entryToObject(primaryKeyThang,
                                                    valueThang);
        } else {
            throw new UnsupportedOperationException
                ("Requires valueBinding or entityBinding");
        }
        return value;
    }

    /**
     * Intersects the given key and the current range.
     */
    KeyRange subRange(KeyRange useRange, Object singleKey)
        throws SDatabaseException, KeyRangeException {

        return useRange.subRange(makeRangeKey(singleKey));
    }

    /**
     * Intersects the given range and the current range.
     */
    KeyRange subRange(KeyRange useRange,
                      Object beginKey, boolean beginInclusive,
                      Object endKey, boolean endInclusive)
        throws SDatabaseException, KeyRangeException {

        if (beginKey == endKey && beginInclusive && endInclusive) {
            return subRange(useRange, beginKey);
        }
        if (!ordered) {
            throw new UnsupportedOperationException
                ("Cannot use key ranges on an unsorted database");
        }
        SDatabaseEntry beginThang =
            (beginKey != null) ? makeRangeKey(beginKey) : null;
        SDatabaseEntry endThang =
            (endKey != null) ? makeRangeKey(endKey) : null;

        return useRange.subRange(beginThang, beginInclusive,
                                 endThang, endInclusive);
    }

    /**
     * Returns the range to use for sub-ranges.  Returns range if this is not a
     * dupsView, or the dupsRange if this is a dupsView, creating dupsRange if
     * necessary.
     */
    KeyRange useSubRange()
        throws SDatabaseException {

        if (dupsView) {
            synchronized (this) {
                if (dupsRange == null) {
                    SDatabaseConfig config =
                        secDb.getPrimaryDatabase().getConfig();
                    dupsRange = new KeyRange(config.getBtreeComparator());
                }
            }
            return dupsRange;
        } else {
            return range;
        }
    }

    /**
     * Given a key object, make a key entry that can be used in a range.
     */
    private SDatabaseEntry makeRangeKey(Object key)
        throws SDatabaseException {

        SDatabaseEntry thang = new SDatabaseEntry();
        if (keyBinding != null) {
            useKey(key, null, thang, null);
        } else {
            useKey(null, key, thang, null);
        }
        return thang;
    }
}
