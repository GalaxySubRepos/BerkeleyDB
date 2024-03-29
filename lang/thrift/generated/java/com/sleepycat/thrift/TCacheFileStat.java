/**
 * Autogenerated by Thrift Compiler (0.11.0)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
package com.sleepycat.thrift;

@SuppressWarnings({"cast", "rawtypes", "serial", "unchecked", "unused"})
@javax.annotation.Generated(value = "Autogenerated by Thrift Compiler (0.11.0)", date = "2019-01-29")
public class TCacheFileStat implements org.apache.thrift.TBase<TCacheFileStat, TCacheFileStat._Fields>, java.io.Serializable, Cloneable, Comparable<TCacheFileStat> {
  private static final org.apache.thrift.protocol.TStruct STRUCT_DESC = new org.apache.thrift.protocol.TStruct("TCacheFileStat");

  private static final org.apache.thrift.protocol.TField BACKUP_SPINS_FIELD_DESC = new org.apache.thrift.protocol.TField("backupSpins", org.apache.thrift.protocol.TType.I64, (short)1);
  private static final org.apache.thrift.protocol.TField CACHE_HIT_FIELD_DESC = new org.apache.thrift.protocol.TField("cacheHit", org.apache.thrift.protocol.TType.I64, (short)2);
  private static final org.apache.thrift.protocol.TField CACHE_MISS_FIELD_DESC = new org.apache.thrift.protocol.TField("cacheMiss", org.apache.thrift.protocol.TType.I64, (short)3);
  private static final org.apache.thrift.protocol.TField FILE_NAME_FIELD_DESC = new org.apache.thrift.protocol.TField("fileName", org.apache.thrift.protocol.TType.STRING, (short)4);
  private static final org.apache.thrift.protocol.TField PAGE_SIZE_FIELD_DESC = new org.apache.thrift.protocol.TField("pageSize", org.apache.thrift.protocol.TType.I32, (short)5);
  private static final org.apache.thrift.protocol.TField PAGE_CREATE_FIELD_DESC = new org.apache.thrift.protocol.TField("pageCreate", org.apache.thrift.protocol.TType.I64, (short)6);
  private static final org.apache.thrift.protocol.TField PAGE_MAPPED_FIELD_DESC = new org.apache.thrift.protocol.TField("pageMapped", org.apache.thrift.protocol.TType.I32, (short)7);
  private static final org.apache.thrift.protocol.TField PAGE_IN_FIELD_DESC = new org.apache.thrift.protocol.TField("pageIn", org.apache.thrift.protocol.TType.I64, (short)8);
  private static final org.apache.thrift.protocol.TField PAGE_OUT_FIELD_DESC = new org.apache.thrift.protocol.TField("pageOut", org.apache.thrift.protocol.TType.I64, (short)9);

  private static final org.apache.thrift.scheme.SchemeFactory STANDARD_SCHEME_FACTORY = new TCacheFileStatStandardSchemeFactory();
  private static final org.apache.thrift.scheme.SchemeFactory TUPLE_SCHEME_FACTORY = new TCacheFileStatTupleSchemeFactory();

  public long backupSpins; // required
  public long cacheHit; // required
  public long cacheMiss; // required
  public java.lang.String fileName; // required
  public int pageSize; // required
  public long pageCreate; // required
  public int pageMapped; // required
  public long pageIn; // required
  public long pageOut; // required

  /** The set of fields this struct contains, along with convenience methods for finding and manipulating them. */
  public enum _Fields implements org.apache.thrift.TFieldIdEnum {
    BACKUP_SPINS((short)1, "backupSpins"),
    CACHE_HIT((short)2, "cacheHit"),
    CACHE_MISS((short)3, "cacheMiss"),
    FILE_NAME((short)4, "fileName"),
    PAGE_SIZE((short)5, "pageSize"),
    PAGE_CREATE((short)6, "pageCreate"),
    PAGE_MAPPED((short)7, "pageMapped"),
    PAGE_IN((short)8, "pageIn"),
    PAGE_OUT((short)9, "pageOut");

    private static final java.util.Map<java.lang.String, _Fields> byName = new java.util.HashMap<java.lang.String, _Fields>();

    static {
      for (_Fields field : java.util.EnumSet.allOf(_Fields.class)) {
        byName.put(field.getFieldName(), field);
      }
    }

    /**
     * Find the _Fields constant that matches fieldId, or null if its not found.
     */
    public static _Fields findByThriftId(int fieldId) {
      switch(fieldId) {
        case 1: // BACKUP_SPINS
          return BACKUP_SPINS;
        case 2: // CACHE_HIT
          return CACHE_HIT;
        case 3: // CACHE_MISS
          return CACHE_MISS;
        case 4: // FILE_NAME
          return FILE_NAME;
        case 5: // PAGE_SIZE
          return PAGE_SIZE;
        case 6: // PAGE_CREATE
          return PAGE_CREATE;
        case 7: // PAGE_MAPPED
          return PAGE_MAPPED;
        case 8: // PAGE_IN
          return PAGE_IN;
        case 9: // PAGE_OUT
          return PAGE_OUT;
        default:
          return null;
      }
    }

    /**
     * Find the _Fields constant that matches fieldId, throwing an exception
     * if it is not found.
     */
    public static _Fields findByThriftIdOrThrow(int fieldId) {
      _Fields fields = findByThriftId(fieldId);
      if (fields == null) throw new java.lang.IllegalArgumentException("Field " + fieldId + " doesn't exist!");
      return fields;
    }

    /**
     * Find the _Fields constant that matches name, or null if its not found.
     */
    public static _Fields findByName(java.lang.String name) {
      return byName.get(name);
    }

    private final short _thriftId;
    private final java.lang.String _fieldName;

    _Fields(short thriftId, java.lang.String fieldName) {
      _thriftId = thriftId;
      _fieldName = fieldName;
    }

    public short getThriftFieldId() {
      return _thriftId;
    }

    public java.lang.String getFieldName() {
      return _fieldName;
    }
  }

  // isset id assignments
  private static final int __BACKUPSPINS_ISSET_ID = 0;
  private static final int __CACHEHIT_ISSET_ID = 1;
  private static final int __CACHEMISS_ISSET_ID = 2;
  private static final int __PAGESIZE_ISSET_ID = 3;
  private static final int __PAGECREATE_ISSET_ID = 4;
  private static final int __PAGEMAPPED_ISSET_ID = 5;
  private static final int __PAGEIN_ISSET_ID = 6;
  private static final int __PAGEOUT_ISSET_ID = 7;
  private byte __isset_bitfield = 0;
  public static final java.util.Map<_Fields, org.apache.thrift.meta_data.FieldMetaData> metaDataMap;
  static {
    java.util.Map<_Fields, org.apache.thrift.meta_data.FieldMetaData> tmpMap = new java.util.EnumMap<_Fields, org.apache.thrift.meta_data.FieldMetaData>(_Fields.class);
    tmpMap.put(_Fields.BACKUP_SPINS, new org.apache.thrift.meta_data.FieldMetaData("backupSpins", org.apache.thrift.TFieldRequirementType.DEFAULT, 
        new org.apache.thrift.meta_data.FieldValueMetaData(org.apache.thrift.protocol.TType.I64)));
    tmpMap.put(_Fields.CACHE_HIT, new org.apache.thrift.meta_data.FieldMetaData("cacheHit", org.apache.thrift.TFieldRequirementType.DEFAULT, 
        new org.apache.thrift.meta_data.FieldValueMetaData(org.apache.thrift.protocol.TType.I64)));
    tmpMap.put(_Fields.CACHE_MISS, new org.apache.thrift.meta_data.FieldMetaData("cacheMiss", org.apache.thrift.TFieldRequirementType.DEFAULT, 
        new org.apache.thrift.meta_data.FieldValueMetaData(org.apache.thrift.protocol.TType.I64)));
    tmpMap.put(_Fields.FILE_NAME, new org.apache.thrift.meta_data.FieldMetaData("fileName", org.apache.thrift.TFieldRequirementType.DEFAULT, 
        new org.apache.thrift.meta_data.FieldValueMetaData(org.apache.thrift.protocol.TType.STRING)));
    tmpMap.put(_Fields.PAGE_SIZE, new org.apache.thrift.meta_data.FieldMetaData("pageSize", org.apache.thrift.TFieldRequirementType.DEFAULT, 
        new org.apache.thrift.meta_data.FieldValueMetaData(org.apache.thrift.protocol.TType.I32)));
    tmpMap.put(_Fields.PAGE_CREATE, new org.apache.thrift.meta_data.FieldMetaData("pageCreate", org.apache.thrift.TFieldRequirementType.DEFAULT, 
        new org.apache.thrift.meta_data.FieldValueMetaData(org.apache.thrift.protocol.TType.I64)));
    tmpMap.put(_Fields.PAGE_MAPPED, new org.apache.thrift.meta_data.FieldMetaData("pageMapped", org.apache.thrift.TFieldRequirementType.DEFAULT, 
        new org.apache.thrift.meta_data.FieldValueMetaData(org.apache.thrift.protocol.TType.I32)));
    tmpMap.put(_Fields.PAGE_IN, new org.apache.thrift.meta_data.FieldMetaData("pageIn", org.apache.thrift.TFieldRequirementType.DEFAULT, 
        new org.apache.thrift.meta_data.FieldValueMetaData(org.apache.thrift.protocol.TType.I64)));
    tmpMap.put(_Fields.PAGE_OUT, new org.apache.thrift.meta_data.FieldMetaData("pageOut", org.apache.thrift.TFieldRequirementType.DEFAULT, 
        new org.apache.thrift.meta_data.FieldValueMetaData(org.apache.thrift.protocol.TType.I64)));
    metaDataMap = java.util.Collections.unmodifiableMap(tmpMap);
    org.apache.thrift.meta_data.FieldMetaData.addStructMetaDataMap(TCacheFileStat.class, metaDataMap);
  }

  public TCacheFileStat() {
  }

  public TCacheFileStat(
    long backupSpins,
    long cacheHit,
    long cacheMiss,
    java.lang.String fileName,
    int pageSize,
    long pageCreate,
    int pageMapped,
    long pageIn,
    long pageOut)
  {
    this();
    this.backupSpins = backupSpins;
    setBackupSpinsIsSet(true);
    this.cacheHit = cacheHit;
    setCacheHitIsSet(true);
    this.cacheMiss = cacheMiss;
    setCacheMissIsSet(true);
    this.fileName = fileName;
    this.pageSize = pageSize;
    setPageSizeIsSet(true);
    this.pageCreate = pageCreate;
    setPageCreateIsSet(true);
    this.pageMapped = pageMapped;
    setPageMappedIsSet(true);
    this.pageIn = pageIn;
    setPageInIsSet(true);
    this.pageOut = pageOut;
    setPageOutIsSet(true);
  }

  /**
   * Performs a deep copy on <i>other</i>.
   */
  public TCacheFileStat(TCacheFileStat other) {
    __isset_bitfield = other.__isset_bitfield;
    this.backupSpins = other.backupSpins;
    this.cacheHit = other.cacheHit;
    this.cacheMiss = other.cacheMiss;
    if (other.isSetFileName()) {
      this.fileName = other.fileName;
    }
    this.pageSize = other.pageSize;
    this.pageCreate = other.pageCreate;
    this.pageMapped = other.pageMapped;
    this.pageIn = other.pageIn;
    this.pageOut = other.pageOut;
  }

  public TCacheFileStat deepCopy() {
    return new TCacheFileStat(this);
  }

  @Override
  public void clear() {
    setBackupSpinsIsSet(false);
    this.backupSpins = 0;
    setCacheHitIsSet(false);
    this.cacheHit = 0;
    setCacheMissIsSet(false);
    this.cacheMiss = 0;
    this.fileName = null;
    setPageSizeIsSet(false);
    this.pageSize = 0;
    setPageCreateIsSet(false);
    this.pageCreate = 0;
    setPageMappedIsSet(false);
    this.pageMapped = 0;
    setPageInIsSet(false);
    this.pageIn = 0;
    setPageOutIsSet(false);
    this.pageOut = 0;
  }

  public long getBackupSpins() {
    return this.backupSpins;
  }

  public TCacheFileStat setBackupSpins(long backupSpins) {
    this.backupSpins = backupSpins;
    setBackupSpinsIsSet(true);
    return this;
  }

  public void unsetBackupSpins() {
    __isset_bitfield = org.apache.thrift.EncodingUtils.clearBit(__isset_bitfield, __BACKUPSPINS_ISSET_ID);
  }

  /** Returns true if field backupSpins is set (has been assigned a value) and false otherwise */
  public boolean isSetBackupSpins() {
    return org.apache.thrift.EncodingUtils.testBit(__isset_bitfield, __BACKUPSPINS_ISSET_ID);
  }

  public void setBackupSpinsIsSet(boolean value) {
    __isset_bitfield = org.apache.thrift.EncodingUtils.setBit(__isset_bitfield, __BACKUPSPINS_ISSET_ID, value);
  }

  public long getCacheHit() {
    return this.cacheHit;
  }

  public TCacheFileStat setCacheHit(long cacheHit) {
    this.cacheHit = cacheHit;
    setCacheHitIsSet(true);
    return this;
  }

  public void unsetCacheHit() {
    __isset_bitfield = org.apache.thrift.EncodingUtils.clearBit(__isset_bitfield, __CACHEHIT_ISSET_ID);
  }

  /** Returns true if field cacheHit is set (has been assigned a value) and false otherwise */
  public boolean isSetCacheHit() {
    return org.apache.thrift.EncodingUtils.testBit(__isset_bitfield, __CACHEHIT_ISSET_ID);
  }

  public void setCacheHitIsSet(boolean value) {
    __isset_bitfield = org.apache.thrift.EncodingUtils.setBit(__isset_bitfield, __CACHEHIT_ISSET_ID, value);
  }

  public long getCacheMiss() {
    return this.cacheMiss;
  }

  public TCacheFileStat setCacheMiss(long cacheMiss) {
    this.cacheMiss = cacheMiss;
    setCacheMissIsSet(true);
    return this;
  }

  public void unsetCacheMiss() {
    __isset_bitfield = org.apache.thrift.EncodingUtils.clearBit(__isset_bitfield, __CACHEMISS_ISSET_ID);
  }

  /** Returns true if field cacheMiss is set (has been assigned a value) and false otherwise */
  public boolean isSetCacheMiss() {
    return org.apache.thrift.EncodingUtils.testBit(__isset_bitfield, __CACHEMISS_ISSET_ID);
  }

  public void setCacheMissIsSet(boolean value) {
    __isset_bitfield = org.apache.thrift.EncodingUtils.setBit(__isset_bitfield, __CACHEMISS_ISSET_ID, value);
  }

  public java.lang.String getFileName() {
    return this.fileName;
  }

  public TCacheFileStat setFileName(java.lang.String fileName) {
    this.fileName = fileName;
    return this;
  }

  public void unsetFileName() {
    this.fileName = null;
  }

  /** Returns true if field fileName is set (has been assigned a value) and false otherwise */
  public boolean isSetFileName() {
    return this.fileName != null;
  }

  public void setFileNameIsSet(boolean value) {
    if (!value) {
      this.fileName = null;
    }
  }

  public int getPageSize() {
    return this.pageSize;
  }

  public TCacheFileStat setPageSize(int pageSize) {
    this.pageSize = pageSize;
    setPageSizeIsSet(true);
    return this;
  }

  public void unsetPageSize() {
    __isset_bitfield = org.apache.thrift.EncodingUtils.clearBit(__isset_bitfield, __PAGESIZE_ISSET_ID);
  }

  /** Returns true if field pageSize is set (has been assigned a value) and false otherwise */
  public boolean isSetPageSize() {
    return org.apache.thrift.EncodingUtils.testBit(__isset_bitfield, __PAGESIZE_ISSET_ID);
  }

  public void setPageSizeIsSet(boolean value) {
    __isset_bitfield = org.apache.thrift.EncodingUtils.setBit(__isset_bitfield, __PAGESIZE_ISSET_ID, value);
  }

  public long getPageCreate() {
    return this.pageCreate;
  }

  public TCacheFileStat setPageCreate(long pageCreate) {
    this.pageCreate = pageCreate;
    setPageCreateIsSet(true);
    return this;
  }

  public void unsetPageCreate() {
    __isset_bitfield = org.apache.thrift.EncodingUtils.clearBit(__isset_bitfield, __PAGECREATE_ISSET_ID);
  }

  /** Returns true if field pageCreate is set (has been assigned a value) and false otherwise */
  public boolean isSetPageCreate() {
    return org.apache.thrift.EncodingUtils.testBit(__isset_bitfield, __PAGECREATE_ISSET_ID);
  }

  public void setPageCreateIsSet(boolean value) {
    __isset_bitfield = org.apache.thrift.EncodingUtils.setBit(__isset_bitfield, __PAGECREATE_ISSET_ID, value);
  }

  public int getPageMapped() {
    return this.pageMapped;
  }

  public TCacheFileStat setPageMapped(int pageMapped) {
    this.pageMapped = pageMapped;
    setPageMappedIsSet(true);
    return this;
  }

  public void unsetPageMapped() {
    __isset_bitfield = org.apache.thrift.EncodingUtils.clearBit(__isset_bitfield, __PAGEMAPPED_ISSET_ID);
  }

  /** Returns true if field pageMapped is set (has been assigned a value) and false otherwise */
  public boolean isSetPageMapped() {
    return org.apache.thrift.EncodingUtils.testBit(__isset_bitfield, __PAGEMAPPED_ISSET_ID);
  }

  public void setPageMappedIsSet(boolean value) {
    __isset_bitfield = org.apache.thrift.EncodingUtils.setBit(__isset_bitfield, __PAGEMAPPED_ISSET_ID, value);
  }

  public long getPageIn() {
    return this.pageIn;
  }

  public TCacheFileStat setPageIn(long pageIn) {
    this.pageIn = pageIn;
    setPageInIsSet(true);
    return this;
  }

  public void unsetPageIn() {
    __isset_bitfield = org.apache.thrift.EncodingUtils.clearBit(__isset_bitfield, __PAGEIN_ISSET_ID);
  }

  /** Returns true if field pageIn is set (has been assigned a value) and false otherwise */
  public boolean isSetPageIn() {
    return org.apache.thrift.EncodingUtils.testBit(__isset_bitfield, __PAGEIN_ISSET_ID);
  }

  public void setPageInIsSet(boolean value) {
    __isset_bitfield = org.apache.thrift.EncodingUtils.setBit(__isset_bitfield, __PAGEIN_ISSET_ID, value);
  }

  public long getPageOut() {
    return this.pageOut;
  }

  public TCacheFileStat setPageOut(long pageOut) {
    this.pageOut = pageOut;
    setPageOutIsSet(true);
    return this;
  }

  public void unsetPageOut() {
    __isset_bitfield = org.apache.thrift.EncodingUtils.clearBit(__isset_bitfield, __PAGEOUT_ISSET_ID);
  }

  /** Returns true if field pageOut is set (has been assigned a value) and false otherwise */
  public boolean isSetPageOut() {
    return org.apache.thrift.EncodingUtils.testBit(__isset_bitfield, __PAGEOUT_ISSET_ID);
  }

  public void setPageOutIsSet(boolean value) {
    __isset_bitfield = org.apache.thrift.EncodingUtils.setBit(__isset_bitfield, __PAGEOUT_ISSET_ID, value);
  }

  public void setFieldValue(_Fields field, java.lang.Object value) {
    switch (field) {
    case BACKUP_SPINS:
      if (value == null) {
        unsetBackupSpins();
      } else {
        setBackupSpins((java.lang.Long)value);
      }
      break;

    case CACHE_HIT:
      if (value == null) {
        unsetCacheHit();
      } else {
        setCacheHit((java.lang.Long)value);
      }
      break;

    case CACHE_MISS:
      if (value == null) {
        unsetCacheMiss();
      } else {
        setCacheMiss((java.lang.Long)value);
      }
      break;

    case FILE_NAME:
      if (value == null) {
        unsetFileName();
      } else {
        setFileName((java.lang.String)value);
      }
      break;

    case PAGE_SIZE:
      if (value == null) {
        unsetPageSize();
      } else {
        setPageSize((java.lang.Integer)value);
      }
      break;

    case PAGE_CREATE:
      if (value == null) {
        unsetPageCreate();
      } else {
        setPageCreate((java.lang.Long)value);
      }
      break;

    case PAGE_MAPPED:
      if (value == null) {
        unsetPageMapped();
      } else {
        setPageMapped((java.lang.Integer)value);
      }
      break;

    case PAGE_IN:
      if (value == null) {
        unsetPageIn();
      } else {
        setPageIn((java.lang.Long)value);
      }
      break;

    case PAGE_OUT:
      if (value == null) {
        unsetPageOut();
      } else {
        setPageOut((java.lang.Long)value);
      }
      break;

    }
  }

  public java.lang.Object getFieldValue(_Fields field) {
    switch (field) {
    case BACKUP_SPINS:
      return getBackupSpins();

    case CACHE_HIT:
      return getCacheHit();

    case CACHE_MISS:
      return getCacheMiss();

    case FILE_NAME:
      return getFileName();

    case PAGE_SIZE:
      return getPageSize();

    case PAGE_CREATE:
      return getPageCreate();

    case PAGE_MAPPED:
      return getPageMapped();

    case PAGE_IN:
      return getPageIn();

    case PAGE_OUT:
      return getPageOut();

    }
    throw new java.lang.IllegalStateException();
  }

  /** Returns true if field corresponding to fieldID is set (has been assigned a value) and false otherwise */
  public boolean isSet(_Fields field) {
    if (field == null) {
      throw new java.lang.IllegalArgumentException();
    }

    switch (field) {
    case BACKUP_SPINS:
      return isSetBackupSpins();
    case CACHE_HIT:
      return isSetCacheHit();
    case CACHE_MISS:
      return isSetCacheMiss();
    case FILE_NAME:
      return isSetFileName();
    case PAGE_SIZE:
      return isSetPageSize();
    case PAGE_CREATE:
      return isSetPageCreate();
    case PAGE_MAPPED:
      return isSetPageMapped();
    case PAGE_IN:
      return isSetPageIn();
    case PAGE_OUT:
      return isSetPageOut();
    }
    throw new java.lang.IllegalStateException();
  }

  @Override
  public boolean equals(java.lang.Object that) {
    if (that == null)
      return false;
    if (that instanceof TCacheFileStat)
      return this.equals((TCacheFileStat)that);
    return false;
  }

  public boolean equals(TCacheFileStat that) {
    if (that == null)
      return false;
    if (this == that)
      return true;

    boolean this_present_backupSpins = true;
    boolean that_present_backupSpins = true;
    if (this_present_backupSpins || that_present_backupSpins) {
      if (!(this_present_backupSpins && that_present_backupSpins))
        return false;
      if (this.backupSpins != that.backupSpins)
        return false;
    }

    boolean this_present_cacheHit = true;
    boolean that_present_cacheHit = true;
    if (this_present_cacheHit || that_present_cacheHit) {
      if (!(this_present_cacheHit && that_present_cacheHit))
        return false;
      if (this.cacheHit != that.cacheHit)
        return false;
    }

    boolean this_present_cacheMiss = true;
    boolean that_present_cacheMiss = true;
    if (this_present_cacheMiss || that_present_cacheMiss) {
      if (!(this_present_cacheMiss && that_present_cacheMiss))
        return false;
      if (this.cacheMiss != that.cacheMiss)
        return false;
    }

    boolean this_present_fileName = true && this.isSetFileName();
    boolean that_present_fileName = true && that.isSetFileName();
    if (this_present_fileName || that_present_fileName) {
      if (!(this_present_fileName && that_present_fileName))
        return false;
      if (!this.fileName.equals(that.fileName))
        return false;
    }

    boolean this_present_pageSize = true;
    boolean that_present_pageSize = true;
    if (this_present_pageSize || that_present_pageSize) {
      if (!(this_present_pageSize && that_present_pageSize))
        return false;
      if (this.pageSize != that.pageSize)
        return false;
    }

    boolean this_present_pageCreate = true;
    boolean that_present_pageCreate = true;
    if (this_present_pageCreate || that_present_pageCreate) {
      if (!(this_present_pageCreate && that_present_pageCreate))
        return false;
      if (this.pageCreate != that.pageCreate)
        return false;
    }

    boolean this_present_pageMapped = true;
    boolean that_present_pageMapped = true;
    if (this_present_pageMapped || that_present_pageMapped) {
      if (!(this_present_pageMapped && that_present_pageMapped))
        return false;
      if (this.pageMapped != that.pageMapped)
        return false;
    }

    boolean this_present_pageIn = true;
    boolean that_present_pageIn = true;
    if (this_present_pageIn || that_present_pageIn) {
      if (!(this_present_pageIn && that_present_pageIn))
        return false;
      if (this.pageIn != that.pageIn)
        return false;
    }

    boolean this_present_pageOut = true;
    boolean that_present_pageOut = true;
    if (this_present_pageOut || that_present_pageOut) {
      if (!(this_present_pageOut && that_present_pageOut))
        return false;
      if (this.pageOut != that.pageOut)
        return false;
    }

    return true;
  }

  @Override
  public int hashCode() {
    int hashCode = 1;

    hashCode = hashCode * 8191 + org.apache.thrift.TBaseHelper.hashCode(backupSpins);

    hashCode = hashCode * 8191 + org.apache.thrift.TBaseHelper.hashCode(cacheHit);

    hashCode = hashCode * 8191 + org.apache.thrift.TBaseHelper.hashCode(cacheMiss);

    hashCode = hashCode * 8191 + ((isSetFileName()) ? 131071 : 524287);
    if (isSetFileName())
      hashCode = hashCode * 8191 + fileName.hashCode();

    hashCode = hashCode * 8191 + pageSize;

    hashCode = hashCode * 8191 + org.apache.thrift.TBaseHelper.hashCode(pageCreate);

    hashCode = hashCode * 8191 + pageMapped;

    hashCode = hashCode * 8191 + org.apache.thrift.TBaseHelper.hashCode(pageIn);

    hashCode = hashCode * 8191 + org.apache.thrift.TBaseHelper.hashCode(pageOut);

    return hashCode;
  }

  @Override
  public int compareTo(TCacheFileStat other) {
    if (!getClass().equals(other.getClass())) {
      return getClass().getName().compareTo(other.getClass().getName());
    }

    int lastComparison = 0;

    lastComparison = java.lang.Boolean.valueOf(isSetBackupSpins()).compareTo(other.isSetBackupSpins());
    if (lastComparison != 0) {
      return lastComparison;
    }
    if (isSetBackupSpins()) {
      lastComparison = org.apache.thrift.TBaseHelper.compareTo(this.backupSpins, other.backupSpins);
      if (lastComparison != 0) {
        return lastComparison;
      }
    }
    lastComparison = java.lang.Boolean.valueOf(isSetCacheHit()).compareTo(other.isSetCacheHit());
    if (lastComparison != 0) {
      return lastComparison;
    }
    if (isSetCacheHit()) {
      lastComparison = org.apache.thrift.TBaseHelper.compareTo(this.cacheHit, other.cacheHit);
      if (lastComparison != 0) {
        return lastComparison;
      }
    }
    lastComparison = java.lang.Boolean.valueOf(isSetCacheMiss()).compareTo(other.isSetCacheMiss());
    if (lastComparison != 0) {
      return lastComparison;
    }
    if (isSetCacheMiss()) {
      lastComparison = org.apache.thrift.TBaseHelper.compareTo(this.cacheMiss, other.cacheMiss);
      if (lastComparison != 0) {
        return lastComparison;
      }
    }
    lastComparison = java.lang.Boolean.valueOf(isSetFileName()).compareTo(other.isSetFileName());
    if (lastComparison != 0) {
      return lastComparison;
    }
    if (isSetFileName()) {
      lastComparison = org.apache.thrift.TBaseHelper.compareTo(this.fileName, other.fileName);
      if (lastComparison != 0) {
        return lastComparison;
      }
    }
    lastComparison = java.lang.Boolean.valueOf(isSetPageSize()).compareTo(other.isSetPageSize());
    if (lastComparison != 0) {
      return lastComparison;
    }
    if (isSetPageSize()) {
      lastComparison = org.apache.thrift.TBaseHelper.compareTo(this.pageSize, other.pageSize);
      if (lastComparison != 0) {
        return lastComparison;
      }
    }
    lastComparison = java.lang.Boolean.valueOf(isSetPageCreate()).compareTo(other.isSetPageCreate());
    if (lastComparison != 0) {
      return lastComparison;
    }
    if (isSetPageCreate()) {
      lastComparison = org.apache.thrift.TBaseHelper.compareTo(this.pageCreate, other.pageCreate);
      if (lastComparison != 0) {
        return lastComparison;
      }
    }
    lastComparison = java.lang.Boolean.valueOf(isSetPageMapped()).compareTo(other.isSetPageMapped());
    if (lastComparison != 0) {
      return lastComparison;
    }
    if (isSetPageMapped()) {
      lastComparison = org.apache.thrift.TBaseHelper.compareTo(this.pageMapped, other.pageMapped);
      if (lastComparison != 0) {
        return lastComparison;
      }
    }
    lastComparison = java.lang.Boolean.valueOf(isSetPageIn()).compareTo(other.isSetPageIn());
    if (lastComparison != 0) {
      return lastComparison;
    }
    if (isSetPageIn()) {
      lastComparison = org.apache.thrift.TBaseHelper.compareTo(this.pageIn, other.pageIn);
      if (lastComparison != 0) {
        return lastComparison;
      }
    }
    lastComparison = java.lang.Boolean.valueOf(isSetPageOut()).compareTo(other.isSetPageOut());
    if (lastComparison != 0) {
      return lastComparison;
    }
    if (isSetPageOut()) {
      lastComparison = org.apache.thrift.TBaseHelper.compareTo(this.pageOut, other.pageOut);
      if (lastComparison != 0) {
        return lastComparison;
      }
    }
    return 0;
  }

  public _Fields fieldForId(int fieldId) {
    return _Fields.findByThriftId(fieldId);
  }

  public void read(org.apache.thrift.protocol.TProtocol iprot) throws org.apache.thrift.TException {
    scheme(iprot).read(iprot, this);
  }

  public void write(org.apache.thrift.protocol.TProtocol oprot) throws org.apache.thrift.TException {
    scheme(oprot).write(oprot, this);
  }

  @Override
  public java.lang.String toString() {
    java.lang.StringBuilder sb = new java.lang.StringBuilder("TCacheFileStat(");
    boolean first = true;

    sb.append("backupSpins:");
    sb.append(this.backupSpins);
    first = false;
    if (!first) sb.append(", ");
    sb.append("cacheHit:");
    sb.append(this.cacheHit);
    first = false;
    if (!first) sb.append(", ");
    sb.append("cacheMiss:");
    sb.append(this.cacheMiss);
    first = false;
    if (!first) sb.append(", ");
    sb.append("fileName:");
    if (this.fileName == null) {
      sb.append("null");
    } else {
      sb.append(this.fileName);
    }
    first = false;
    if (!first) sb.append(", ");
    sb.append("pageSize:");
    sb.append(this.pageSize);
    first = false;
    if (!first) sb.append(", ");
    sb.append("pageCreate:");
    sb.append(this.pageCreate);
    first = false;
    if (!first) sb.append(", ");
    sb.append("pageMapped:");
    sb.append(this.pageMapped);
    first = false;
    if (!first) sb.append(", ");
    sb.append("pageIn:");
    sb.append(this.pageIn);
    first = false;
    if (!first) sb.append(", ");
    sb.append("pageOut:");
    sb.append(this.pageOut);
    first = false;
    sb.append(")");
    return sb.toString();
  }

  public void validate() throws org.apache.thrift.TException {
    // check for required fields
    // check for sub-struct validity
  }

  private void writeObject(java.io.ObjectOutputStream out) throws java.io.IOException {
    try {
      write(new org.apache.thrift.protocol.TCompactProtocol(new org.apache.thrift.transport.TIOStreamTransport(out)));
    } catch (org.apache.thrift.TException te) {
      throw new java.io.IOException(te);
    }
  }

  private void readObject(java.io.ObjectInputStream in) throws java.io.IOException, java.lang.ClassNotFoundException {
    try {
      // it doesn't seem like you should have to do this, but java serialization is wacky, and doesn't call the default constructor.
      __isset_bitfield = 0;
      read(new org.apache.thrift.protocol.TCompactProtocol(new org.apache.thrift.transport.TIOStreamTransport(in)));
    } catch (org.apache.thrift.TException te) {
      throw new java.io.IOException(te);
    }
  }

  private static class TCacheFileStatStandardSchemeFactory implements org.apache.thrift.scheme.SchemeFactory {
    public TCacheFileStatStandardScheme getScheme() {
      return new TCacheFileStatStandardScheme();
    }
  }

  private static class TCacheFileStatStandardScheme extends org.apache.thrift.scheme.StandardScheme<TCacheFileStat> {

    public void read(org.apache.thrift.protocol.TProtocol iprot, TCacheFileStat struct) throws org.apache.thrift.TException {
      org.apache.thrift.protocol.TField schemeField;
      iprot.readStructBegin();
      while (true)
      {
        schemeField = iprot.readFieldBegin();
        if (schemeField.type == org.apache.thrift.protocol.TType.STOP) { 
          break;
        }
        switch (schemeField.id) {
          case 1: // BACKUP_SPINS
            if (schemeField.type == org.apache.thrift.protocol.TType.I64) {
              struct.backupSpins = iprot.readI64();
              struct.setBackupSpinsIsSet(true);
            } else { 
              org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
            }
            break;
          case 2: // CACHE_HIT
            if (schemeField.type == org.apache.thrift.protocol.TType.I64) {
              struct.cacheHit = iprot.readI64();
              struct.setCacheHitIsSet(true);
            } else { 
              org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
            }
            break;
          case 3: // CACHE_MISS
            if (schemeField.type == org.apache.thrift.protocol.TType.I64) {
              struct.cacheMiss = iprot.readI64();
              struct.setCacheMissIsSet(true);
            } else { 
              org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
            }
            break;
          case 4: // FILE_NAME
            if (schemeField.type == org.apache.thrift.protocol.TType.STRING) {
              struct.fileName = iprot.readString();
              struct.setFileNameIsSet(true);
            } else { 
              org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
            }
            break;
          case 5: // PAGE_SIZE
            if (schemeField.type == org.apache.thrift.protocol.TType.I32) {
              struct.pageSize = iprot.readI32();
              struct.setPageSizeIsSet(true);
            } else { 
              org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
            }
            break;
          case 6: // PAGE_CREATE
            if (schemeField.type == org.apache.thrift.protocol.TType.I64) {
              struct.pageCreate = iprot.readI64();
              struct.setPageCreateIsSet(true);
            } else { 
              org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
            }
            break;
          case 7: // PAGE_MAPPED
            if (schemeField.type == org.apache.thrift.protocol.TType.I32) {
              struct.pageMapped = iprot.readI32();
              struct.setPageMappedIsSet(true);
            } else { 
              org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
            }
            break;
          case 8: // PAGE_IN
            if (schemeField.type == org.apache.thrift.protocol.TType.I64) {
              struct.pageIn = iprot.readI64();
              struct.setPageInIsSet(true);
            } else { 
              org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
            }
            break;
          case 9: // PAGE_OUT
            if (schemeField.type == org.apache.thrift.protocol.TType.I64) {
              struct.pageOut = iprot.readI64();
              struct.setPageOutIsSet(true);
            } else { 
              org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
            }
            break;
          default:
            org.apache.thrift.protocol.TProtocolUtil.skip(iprot, schemeField.type);
        }
        iprot.readFieldEnd();
      }
      iprot.readStructEnd();

      // check for required fields of primitive type, which can't be checked in the validate method
      struct.validate();
    }

    public void write(org.apache.thrift.protocol.TProtocol oprot, TCacheFileStat struct) throws org.apache.thrift.TException {
      struct.validate();

      oprot.writeStructBegin(STRUCT_DESC);
      oprot.writeFieldBegin(BACKUP_SPINS_FIELD_DESC);
      oprot.writeI64(struct.backupSpins);
      oprot.writeFieldEnd();
      oprot.writeFieldBegin(CACHE_HIT_FIELD_DESC);
      oprot.writeI64(struct.cacheHit);
      oprot.writeFieldEnd();
      oprot.writeFieldBegin(CACHE_MISS_FIELD_DESC);
      oprot.writeI64(struct.cacheMiss);
      oprot.writeFieldEnd();
      if (struct.fileName != null) {
        oprot.writeFieldBegin(FILE_NAME_FIELD_DESC);
        oprot.writeString(struct.fileName);
        oprot.writeFieldEnd();
      }
      oprot.writeFieldBegin(PAGE_SIZE_FIELD_DESC);
      oprot.writeI32(struct.pageSize);
      oprot.writeFieldEnd();
      oprot.writeFieldBegin(PAGE_CREATE_FIELD_DESC);
      oprot.writeI64(struct.pageCreate);
      oprot.writeFieldEnd();
      oprot.writeFieldBegin(PAGE_MAPPED_FIELD_DESC);
      oprot.writeI32(struct.pageMapped);
      oprot.writeFieldEnd();
      oprot.writeFieldBegin(PAGE_IN_FIELD_DESC);
      oprot.writeI64(struct.pageIn);
      oprot.writeFieldEnd();
      oprot.writeFieldBegin(PAGE_OUT_FIELD_DESC);
      oprot.writeI64(struct.pageOut);
      oprot.writeFieldEnd();
      oprot.writeFieldStop();
      oprot.writeStructEnd();
    }

  }

  private static class TCacheFileStatTupleSchemeFactory implements org.apache.thrift.scheme.SchemeFactory {
    public TCacheFileStatTupleScheme getScheme() {
      return new TCacheFileStatTupleScheme();
    }
  }

  private static class TCacheFileStatTupleScheme extends org.apache.thrift.scheme.TupleScheme<TCacheFileStat> {

    @Override
    public void write(org.apache.thrift.protocol.TProtocol prot, TCacheFileStat struct) throws org.apache.thrift.TException {
      org.apache.thrift.protocol.TTupleProtocol oprot = (org.apache.thrift.protocol.TTupleProtocol) prot;
      java.util.BitSet optionals = new java.util.BitSet();
      if (struct.isSetBackupSpins()) {
        optionals.set(0);
      }
      if (struct.isSetCacheHit()) {
        optionals.set(1);
      }
      if (struct.isSetCacheMiss()) {
        optionals.set(2);
      }
      if (struct.isSetFileName()) {
        optionals.set(3);
      }
      if (struct.isSetPageSize()) {
        optionals.set(4);
      }
      if (struct.isSetPageCreate()) {
        optionals.set(5);
      }
      if (struct.isSetPageMapped()) {
        optionals.set(6);
      }
      if (struct.isSetPageIn()) {
        optionals.set(7);
      }
      if (struct.isSetPageOut()) {
        optionals.set(8);
      }
      oprot.writeBitSet(optionals, 9);
      if (struct.isSetBackupSpins()) {
        oprot.writeI64(struct.backupSpins);
      }
      if (struct.isSetCacheHit()) {
        oprot.writeI64(struct.cacheHit);
      }
      if (struct.isSetCacheMiss()) {
        oprot.writeI64(struct.cacheMiss);
      }
      if (struct.isSetFileName()) {
        oprot.writeString(struct.fileName);
      }
      if (struct.isSetPageSize()) {
        oprot.writeI32(struct.pageSize);
      }
      if (struct.isSetPageCreate()) {
        oprot.writeI64(struct.pageCreate);
      }
      if (struct.isSetPageMapped()) {
        oprot.writeI32(struct.pageMapped);
      }
      if (struct.isSetPageIn()) {
        oprot.writeI64(struct.pageIn);
      }
      if (struct.isSetPageOut()) {
        oprot.writeI64(struct.pageOut);
      }
    }

    @Override
    public void read(org.apache.thrift.protocol.TProtocol prot, TCacheFileStat struct) throws org.apache.thrift.TException {
      org.apache.thrift.protocol.TTupleProtocol iprot = (org.apache.thrift.protocol.TTupleProtocol) prot;
      java.util.BitSet incoming = iprot.readBitSet(9);
      if (incoming.get(0)) {
        struct.backupSpins = iprot.readI64();
        struct.setBackupSpinsIsSet(true);
      }
      if (incoming.get(1)) {
        struct.cacheHit = iprot.readI64();
        struct.setCacheHitIsSet(true);
      }
      if (incoming.get(2)) {
        struct.cacheMiss = iprot.readI64();
        struct.setCacheMissIsSet(true);
      }
      if (incoming.get(3)) {
        struct.fileName = iprot.readString();
        struct.setFileNameIsSet(true);
      }
      if (incoming.get(4)) {
        struct.pageSize = iprot.readI32();
        struct.setPageSizeIsSet(true);
      }
      if (incoming.get(5)) {
        struct.pageCreate = iprot.readI64();
        struct.setPageCreateIsSet(true);
      }
      if (incoming.get(6)) {
        struct.pageMapped = iprot.readI32();
        struct.setPageMappedIsSet(true);
      }
      if (incoming.get(7)) {
        struct.pageIn = iprot.readI64();
        struct.setPageInIsSet(true);
      }
      if (incoming.get(8)) {
        struct.pageOut = iprot.readI64();
        struct.setPageOutIsSet(true);
      }
    }
  }

  private static <S extends org.apache.thrift.scheme.IScheme> S scheme(org.apache.thrift.protocol.TProtocol proto) {
    return (org.apache.thrift.scheme.StandardScheme.class.equals(proto.getScheme()) ? STANDARD_SCHEME_FACTORY : TUPLE_SCHEME_FACTORY).getScheme();
  }
}

