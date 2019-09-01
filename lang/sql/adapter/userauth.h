/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2014, 2019 Oracle and/or its affiliates.  All rights reserved.
 */

#ifndef _USERAUTH_H_
#define _USERAUTH_H_

#include "sqliteInt.h"

#ifdef SQLITE_USER_AUTHENTICATION

#include "btreeInt.h"

/* Duplicated DB internal encryption parameters. */
#define	DB_MAC_KEY		20
#define	HMAC_OUTPUT_SIZE	20
#define	DB_IV_BYTES		16
#define	DB_AES_CHUNK		16

/* Duplicated DB internal IO parameters. */
#define	DB_OSO_CREATE	0x0002		/* POSIX: O_CREAT */
#define	DB_OSO_EXCL	0x0010		/* POSIX: O_EXCL */
#define	DB_OSO_RDONLY	0x0020		/* POSIX: O_RDONLY */
#define	DB_OSO_TRUNC	0x0200		/* POSIX: O_TRUNC */
#define	DB_IO_READ	1
#define	DB_IO_WRITE	2

#define	AUTH_PW_SALT_LEN	DB_MAC_KEY
#define	AUTH_PW_HASH_LEN	HMAC_OUTPUT_SIZE

#define	MEGABYTE	1048576

/***********************************************************************
 * Key-store based user authentication specific code.
 ***********************************************************************/
#ifdef BDBSQL_USER_AUTHENTICATION_KEYSTORE

#define	KS_FILE			"sql-userauth.ks"
#define	KS_BAK_FILE		"___sql-userauth.ks.bak"
#define	KS_LCK_FILE		"___sql-userauth.ks.lck"
#define	KS_TMP_FILE		"___sql-userauth.ks.tmp"
#define	KS_DEL_TMP_FILE		"___sql-userauth.ks.del.tmp"

#define	AUTH_KS_READONLY	0x00001
#define	AUTH_KS_APPEND		0x00002

/*
 * A keystore file has the following items:
 *
 *    +-----------------------------------------------
 *    | chksum | version | entry | entry | entry | ...
 *    +-----------------------------------------------
 */
#define	KS_CHKSUM_LEN	4 
#define	KS_VER_LEN	4

/*
 * A keystore entry has one header part and one data part. 
 *    HEADER
 *        +-------------------------------------+
 *        | entry_len | username_len | username |
 *        +-------------------------------------+
 *    DATA
 *        +-------------------------------------+
 *        | salt | iv | buf_len | key_len | buf |
 *        +-------------------------------------+
 */
typedef struct __ks_entry_hdr {
	/* 
	 * Keystore entry length. One entry inlcudes a KS_ENTRY_HEADER object 
	 * and a KS_ENTRY_DATA object.
	 */
	u_int8_t entry_len;

	u_int8_t username_len;
	
	u_int8_t username[1];
} KS_ENTRY_HDR;

#define KSE_HDR_LEN(p) sizeof(u_int8_t) * 2 + (p)->username_len

/* 
 * The maximum length of a keystore entry. We preserve more than 200 bytes for 
 * a user/password pair.
 */
#define	MAX_KSE_LEN	256

/* 
 * The keystore entry data part. In the data part, we store the database shared 
 * encryption key and information for the encryption/decryption of the shared 
 * key.
 */
typedef struct __ks_entry_data {
	/*
	 * A random string used for password hashing. This password hash value 
	 * will be used to encrypt the database shared encryption key. This is 
	 * the same as the salt part of the 'pw' field in the 'sqlite_user' 
	 * table.
	 */
	u_int8_t salt[AUTH_PW_SALT_LEN]; 

	/* 
	 * This is the IV generated by BDB encryption. It is used for 
	 * decryption of the shared encryption key. 
	 */
	u_int8_t iv[DB_IV_BYTES];
	
	/* 
	 * The encryption algorism requires the encryption data size to be 
	 * multiplies of DB_AES_CHUNK. So we append some paddings. Besides, the 
	 * length of shared key is also a sensitive information. The paddings 
	 * actually provide protection to it. 
	 */
	u_int8_t buf_len;
	
	/* !!! Fields below will be encrypted by the user's password hash. */

	/* The length of database shared encryption key. */
	u_int8_t key_len;

	/* The shared encryption key, appended with paddings. */
	u_int8_t buf[1];
} KS_ENTRY_DATA;

/* Length of the fields which will be encrypted. */
#define KSE_DATA_ENCR_LEN(p) (p)->buf_len + sizeof(u_int8_t)

#define KSE_DATA_LEN_NOKEY AUTH_PW_SALT_LEN + sizeof(u_int8_t) * 2 + DB_IV_BYTES

#define KSE_DATA_LEN(p) KSE_DATA_LEN_NOKEY + (p)->buf_len

typedef struct __ks_cb_arg {
	sqlite3 *db;
	KS_ENTRY_HDR *hdr;
	KS_ENTRY_DATA *data;
	const char *zUsername;
	const char *aPW;
	int nPW;
	const char *ksPath;
	DB_FH **fhpp;
	off_t offset;
} KS_CB_ARG;

#endif /* BDBSQL_USER_AUTHENTICATION_KEYSTORE */

#endif /* SQLITE_USER_AUTHENTICATION */

#endif /* _USERAUTH_H_ */