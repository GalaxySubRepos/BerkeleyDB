/*-
 * Copyright (c) 2004, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 * $Id$
 */

#include "db_config.h"

#include "db_int.h"
#include "dbinc/blob.h"
#include "dbinc/db_page.h"
#include "dbinc/db_am.h"
#include "dbinc/fop.h"
#include "dbinc/lock.h"
#include "dbinc/mp.h"
#include "dbinc/qam.h"
#include "dbinc/txn.h"

/*
 * Context information needed for buffer management during the building of a
 * list of database files present in the environment.  When fully built, the
 * buffer is in the form of an UPDATE message: a (marshaled) update_args,
 * followed by some number of (marshaled) fileinfo_args.
 *
 * Note that the fileinfo for the first file in the list always appears at
 * (constant) offset __REP_UPDATE_SIZE in the buffer.
 */
#define	FILE_CTX_INMEM_ONLY	0x01
typedef struct {
	u_int8_t	*buf;	/* Buffer base address. */
	u_int32_t	size;	/* Total allocated buffer size. */
	u_int8_t	*fillptr; /* Pointer to first unused space. */
	u_int32_t	count;	/* Number of entries currently in list. */
	u_int32_t	version; /* Rep version of marshaled format. */
	u_int32_t	flags;	/* Context flags. */
} FILE_LIST_CTX;
#define	FIRST_FILE_PTR(buf)	((buf) + __REP_UPDATE_SIZE)

/*
 * Flags used to show the state of blob files on the master in messages
 * sent to the client.
 */
#define	BLOB_DONE           0x01
#define	BLOB_DELETE         0x02
#define	BLOB_CHUNK_FAIL     0x04
#define	BLOB_REREQ          0x08

#define	BLOB_ID_SIZE	    sizeof(db_seq_t)
#define	BLOB_KEY_SIZE	    (2 * BLOB_ID_SIZE)

/*
 * Function that performs any desired processing on a single file, as part of
 * the traversal of a list of database files, such as with internal init.
 */
typedef int (FILE_WALK_FN) __P((ENV *, __rep_fileinfo_args *, void *));

static int __rep_add_files_to_list __P((
    ENV *, const char *, const char *, FILE_LIST_CTX *, const char **, int));
static int __rep_blob_chunk_gap __P((
    ENV *, int, DB_THREAD_INFO *, REP *, int *, db_seq_t, int, u_int32_t));
static int __rep_blob_cleanup __P((ENV *, REP *));
static int __rep_blobdone
    __P((ENV *, int, DB_THREAD_INFO *, REP *, db_seq_t, int, u_int32_t));
static int __rep_blob_find_files __P((ENV *, DB_THREAD_INFO *, const char *,
    db_seq_t *, db_seq_t, db_seq_t, db_seq_t *, DBT *, size_t *, u_int32_t *));
static int __rep_blob_sort_dirs __P((ENV *,
    int (*)(const char *), char **, int, char ***, int *));
static FILE_WALK_FN __rep_check_uid;
static int __rep_clean_interrupted __P((ENV *));
static FILE_WALK_FN __rep_cleanup_nimdbs;
static int __rep_filedone __P((ENV *, DB_THREAD_INFO *ip, int,
     REP *, __rep_fileinfo_args *, u_int32_t));
static int __rep_find_dbs __P((ENV *, FILE_LIST_CTX *));
static FILE_WALK_FN __rep_find_inmem;
static int __rep_get_fileinfo __P((ENV *, const char *,
    const char *, __rep_fileinfo_args *, u_int8_t *));
static int __rep_get_file_list __P((ENV *,
    DB_FH *, u_int32_t *, DBT *));
static int __rep_init_file_list_context __P((ENV *,
    u_int32_t, u_int32_t, int, FILE_LIST_CTX *));
static int __rep_is_replicated_db __P((const char *, const char *));
static int __rep_log_setup __P((ENV *,
    REP *, u_int32_t, u_int32_t, DB_LSN *));
static int __rep_mpf_open __P((ENV *, DB_MPOOLFILE **,
    __rep_fileinfo_args *, u_int32_t));
static int __rep_nextfile __P((ENV *, int, REP *));
static int __rep_page_gap __P((ENV *,
     REP *, __rep_fileinfo_args *, u_int32_t));
static int __rep_page_sendpages __P((ENV *, DB_THREAD_INFO *, int,
    __rep_control_args *, __rep_fileinfo_args *, DB_MPOOLFILE *, DB *));
static int __rep_queue_filedone __P((ENV *,
    DB_THREAD_INFO *, REP *, __rep_fileinfo_args *));
static int __rep_remove_all __P((ENV *, u_int32_t, DBT *));
static FILE_WALK_FN __rep_remove_by_list;
static int __rep_remove_by_prefix __P((ENV *, const char *, const char *,
    size_t, APPNAME));
static FILE_WALK_FN __rep_remove_file;
static int __rep_remove_logs __P((ENV *));
static int __rep_remove_nimdbs __P((ENV *));
static int __rep_rollback __P((ENV *, DB_LSN *));
static int __rep_select_blob_file __P((const char *));
static int __rep_select_blob_sdb __P((const char *));
static int __rep_unlink_by_list __P((ENV *, u_int32_t,
    u_int8_t *, u_int32_t, u_int32_t));
static FILE_WALK_FN __rep_unlink_file;
static int __rep_walk_blob_dir __P((ENV *, FILE_LIST_CTX*));
static int __rep_walk_filelist __P((ENV *, u_int32_t, u_int8_t *,
    u_int32_t, u_int32_t, FILE_WALK_FN *, void *));
static int __rep_walk_dir __P((ENV *, const char *, const char *,
    FILE_LIST_CTX*));
static int __rep_write_page __P((ENV *,
    DB_THREAD_INFO *, REP *, __rep_fileinfo_args *));

/*
 * __rep_update_req -
 *	Process an update_req and send the file information to clients.
 *
 * PUBLIC: int __rep_update_req __P((ENV *, __rep_control_args *));
 */
int
__rep_update_req(env, rp)
	ENV *env;
	__rep_control_args *rp;
{
	DBT updbt, vdbt;
	DB_LOG *dblp;
	DB_LOGC *logc;
	DB_LSN lsn;
	DB_REP *db_rep;
	REP *rep;
	__rep_update_args u_args;
	FILE_LIST_CTX context;
	size_t updlen;
	u_int32_t flag, version;
	int ret, t_ret;

	/*
	 * Start by allocating 1Meg, which ought to be plenty enough to describe
	 * all databases in the environment.  (If it's not, __rep_walk_dir can
	 * grow the size.)
	 *
	 * The data we send looks like this:
	 *	__rep_update_args
	 *	__rep_fileinfo_args
	 *	__rep_fileinfo_args
	 *	...
	 */
	db_rep = env->rep_handle;
	rep = db_rep->region;
	memset(&context, 0, sizeof(FILE_LIST_CTX));

	REP_SYSTEM_LOCK(env);
	if (F_ISSET(rep, REP_F_INUPDREQ)) {
		REP_SYSTEM_UNLOCK(env);
		return (0);
	}
	F_SET(rep, REP_F_INUPDREQ);
	REP_SYSTEM_UNLOCK(env);

	dblp = env->lg_handle;
	logc = NULL;

	/*
	 * Get our first LSN.  We send the lsn of the first
	 * non-archivable log file.
	 *
	 * We must get the first LSN before getting database file
	 * information.  If we get the database file information first,
	 * we risk a situation where the stable_lsn moves forward during
	 * the time we are getting the file information.  If the
	 * stable_lsn advances by more than a log file or two, it can
	 * prevent the client from requesting all the needed log files for
	 * a correct recovery after copying all the database pages.  A
	 * symptom of this situation is that some client database files
	 * have a contiguous set of empty pages in the middle that show up
	 * in a db_dump as page 0's.  These empty pages eventually lead to
	 * a log sequence error or other failures.
	 */
	flag = DB_SET;
	if ((ret = __log_get_stable_lsn(env, &lsn, 0)) != 0) {
		if (ret != DB_NOTFOUND)
			goto err_noalloc;
		/*
		 * If ret is DB_NOTFOUND then there is no checkpoint
		 * in this log, that is okay, just start at the beginning.
		 */
		ret = 0;
		flag = DB_FIRST;
	}

	/*
	 * Now get the version number of the log file of that LSN.
	 */
	if ((ret = __log_cursor(env, &logc)) != 0)
		goto err_noalloc;

	memset(&vdbt, 0, sizeof(vdbt));
	/*
	 * Set our log cursor on the LSN we are sending.  Or
	 * to the first LSN if we have no stable LSN.
	 */
	if ((ret = __logc_get(logc, &lsn, &vdbt, flag)) != 0) {
		/*
		 * We could be racing a fresh master starting up.  If we
		 * have no log records, assume an initial LSN and current
		 * log version.
		 */
		if (ret != DB_NOTFOUND)
			goto err_noalloc;
		INIT_LSN(lsn);
		version = DB_LOGVERSION;
	} else {
		if ((ret = __logc_version(logc, &version)) != 0)
			goto err_noalloc;
	}

	/* Reserve space for the update_args, and fill in file info. */
	if ((ret = __rep_init_file_list_context(env, rp->rep_version,
	    F_ISSET(rp, REPCTL_INMEM_ONLY) ? FILE_CTX_INMEM_ONLY : 0,
	    1, &context)) != 0)
		goto err_noalloc;
	if ((ret = __rep_find_dbs(env, &context)) != 0)
		goto err;

	/*
	 * Package up the update information.
	 */
	u_args.first_lsn = lsn;
	u_args.first_vers = version;
	u_args.num_files = context.count;
	if ((ret = __rep_update_marshal(env,
	    &u_args, context.buf, __REP_UPDATE_SIZE, &updlen)) != 0)
		goto err;
	DB_ASSERT(env, updlen == __REP_UPDATE_SIZE);

	/*
	 * We have all the file information now.  Send it.
	 */
	DB_INIT_DBT(updbt, context.buf, context.fillptr - context.buf);

	LOG_SYSTEM_LOCK(env);
	lsn = ((LOG *)dblp->reginfo.primary)->lsn;
	LOG_SYSTEM_UNLOCK(env);
	(void)__rep_send_message(
	    env, DB_EID_BROADCAST, REP_UPDATE, &lsn, &updbt, 0, 0);

err:
	if (context.buf != NULL)
		__os_free(env, context.buf);
err_noalloc:
	if (logc != NULL && (t_ret = __logc_close(logc)) != 0 && ret == 0)
		ret = t_ret;
	REP_SYSTEM_LOCK(env);
	F_CLR(rep, REP_F_INUPDREQ);
	REP_SYSTEM_UNLOCK(env);
	return (ret);
}

/*
 * Passed to the __rep_blob_sort_dirs function.
 * Select blob files, of the form __db.bl###
 */
static int
__rep_select_blob_file(file)
	    const char *file;
{
	if (strncmp(BLOB_FILE_PREFIX, file, strlen(BLOB_FILE_PREFIX)) == 0)
		return (1);
	else
		return (0);
}

/*
 * Passed to the __rep_blob_sort_dirs function.
 * Select blob subdatabase directories, of the form __db###
 */
static int
__rep_select_blob_sdb(file)
	    const char *file;
{
	if (strncmp(BLOB_DIR_PREFIX, file, strlen(BLOB_DIR_PREFIX)) == 0 &&
	    strncmp(BLOB_FILE_PREFIX, file, strlen(BLOB_FILE_PREFIX)) != 0 &&
	    strcmp(BLOB_META_FILE_NAME, file) != 0)
		return (1);
	else
		return (0);
}

/*
 * __rep_blob_sort_dirs
 *	Create a sorted list of directory names that all share a type that
 *	is selected using the given function.
 */
static int
__rep_blob_sort_dirs(env, select_fn, dirs, dirs_cnt, sorted, sorted_cnt)
	ENV *env;
	int (*select_fn) __P((const char *));
	char **dirs;
	int dirs_cnt;
	char ***sorted;
	int *sorted_cnt;
{
	char **sort, *tmp;
	int i, ret, size, sort_cnt, swapped;

	*sorted = NULL;
	*sorted_cnt = 0;
	sort_cnt = 0;

	if ((ret = __os_malloc(env,
	    (sizeof(char *) * (unsigned int)dirs_cnt), &sort)) != 0)
		return (ret);

	for (i = 0; i < dirs_cnt; i++) {
		if (select_fn(dirs[i])) {
			sort[sort_cnt] = dirs[i];
			sort_cnt++;
		}
	}

	/*
	 * Directories are usually returned in order, or close to it, so use
	 * Bubble Sort to sort the list.
	 */
	size = sort_cnt;
	swapped = 1;
	while (swapped == 1 && size > 1) {
		swapped = 0;
		for (i = 0; (i + 1) < size; i++) {
			if (strcmp(sort[i], sort[i+1]) > 0) {
				tmp = sort[i];
				sort[i] = sort[i+1];
				sort[i+1] = tmp;
				swapped = 1;
			}
		}
		size--;
	}

	*sorted = sort;
	*sorted_cnt = sort_cnt;

	return (0);
}

#define	BLOB_THROTTLE_DEFAULT	(10 * MEGABYTE)

/*
 * __rep_blob_update_req
 *	Send a list of blob files, starting after the blob id and sub-database
 *	id sent in the BLOB_UPDATE_REQ message.
 *
 * PUBLIC: int __rep_blob_update_req __P((ENV *, int, DB_THREAD_INFO *, DBT *));
 */
int
__rep_blob_update_req(env, eid, ip, rec)
	ENV *env;
	int eid;
	DB_THREAD_INFO *ip;
	DBT *rec;
{
	DBT rbudbt;
	REP *rep;
	__rep_blob_update_args rbu;
	__rep_blob_update_req_args rbur;
	__rep_blob_update_req_v8_args rbur8;
	db_seq_t blob_fid, blob_id, blob_sdb, tmp;
	int cur, dirs_cnt, ret, sdb_cnt;
	size_t sent;
	char *blob_sub_dir, *dir, **dirs, **sdb;
	u_int32_t num_blobs, throttle;
	u_int8_t *ptr;

	memset(&rbu, 0, sizeof(__rep_blob_update_args));
	memset(&rbudbt, 0, sizeof(DBT));
	blob_sub_dir  = dir = NULL;
	dirs = sdb = NULL;
	sent = 0;
	num_blobs = 0;
	cur = dirs_cnt = sdb_cnt = 0;
	rep = env->rep_handle->region;
	throttle = rep->gbytes * GIGABYTE + rep->bytes;
	if (throttle == 0)
		throttle = BLOB_THROTTLE_DEFAULT;

	if (rep->version < DB_REPVERSION_62) {
		if ((ret = __rep_blob_update_req_v8_unmarshal(
		    env, &rbur8, rec->data, rec->size, &ptr)) != 0)
			goto err;
		rbur.blob_fid = rbur8.blob_fid;
		rbur.blob_sid = rbur8.blob_sid;
		rbur.blob_id = rbur8.blob_id;
		rbur.highest_id = rbur8.highest_id;
		rbur.flags = 0;
	} else {
		if ((ret = __rep_blob_update_req_unmarshal(
		    env, &rbur, rec->data, rec->size, &ptr)) != 0)
			goto err;
	}

	RPRINT(env, (env, DB_VERB_REP_SYNC,
"blob_update_req: file_id %llu sdb_id %llu blob_id %llu highest %llu flag %lu",
	    (long long)rbur.blob_fid, (long long)rbur.blob_sid,
	    (long long)rbur.blob_id, (long long)rbur.highest_id,
	    (long)rbur.flags));

	rbu.blob_fid = rbur.blob_fid;
	if (F_ISSET(&rbur, BLOB_REREQ))
		F_SET(&rbu, BLOB_REREQ);

	if ((ret = __os_malloc(env, MEGABYTE, &rbudbt.data)) != 0)
		goto err;
	rbudbt.ulen = MEGABYTE;
	rbudbt.size = __REP_BLOB_UPDATE_SIZE;

	blob_fid = (db_seq_t)rbur.blob_fid;
	blob_sdb = (db_seq_t)rbur.blob_sid;
	blob_id = (db_seq_t)rbur.blob_id;

	/* Find the first blob file if it is unknown. */
	if (blob_id == 0 && blob_sdb == 0) {
find_sdb:	if (dirs == NULL) {
			if ((ret = __blob_make_sub_dir(
			    env, &blob_sub_dir, blob_fid, 0)) != 0)
				goto err;
			if ((ret = __db_appname(
			    env, DB_APP_BLOB, blob_sub_dir, NULL, &dir)) != 0)
				goto err;
			/* If no directory, there are no blobs to send. */
			if (__os_exists(env, dir, NULL) != 0)
				goto filedone;

			if ((ret = __os_dirlist(
			    env, dir, 1, &dirs, &dirs_cnt)) != 0)
				goto err;

			if (dirs_cnt == 0)
				goto filedone;

			if ((ret = __rep_blob_sort_dirs(
			    env, __rep_select_blob_sdb,
			    dirs, dirs_cnt, &sdb, &sdb_cnt)) != 0)
				goto err;
		}
		/*
		 * Iterate through the list of subdirectories, until we find
		 * one that has an id larger than the current subdirectory id.
		 */
		while (cur < sdb_cnt) {
			if ((ret = __blob_path_to_dir_ids(
			    env, sdb[cur], &tmp, NULL)) != 0)
				goto err;
			if (blob_sdb < tmp) {
				blob_sdb = tmp;
				break;
			}
			cur++;
		}
		/* Check if no more subdirectories to search */
		if (sdb_cnt != 0 && cur == sdb_cnt)
			goto filedone;
		if (dir != NULL)
			__os_free(env, dir);
		dir = NULL;
		if (blob_sub_dir != NULL)
			__os_free(env, blob_sub_dir);
		blob_sub_dir = NULL;
	}

	if (blob_sub_dir == NULL && (ret =
	    __blob_make_sub_dir(env, &blob_sub_dir, blob_fid, blob_sdb)) != 0)
		goto err;

	if (dir == NULL && (ret = __db_appname(
	    env, DB_APP_BLOB, blob_sub_dir, NULL, &dir)) != 0)
		goto err;
	/* Search the current directory for blob files with id > blob_id. */
	if ((ret = __rep_blob_find_files(
	    env, ip, dir, &blob_id, blob_sdb, blob_fid,
	    (db_seq_t *)&rbur.highest_id, &rbudbt, &sent, &num_blobs)) != 0)
		goto err;

	/*
	 * If we have not reached the send limit, and there are still
	 * directories to search, then search the next directory.
	 */
	if (sent <  throttle) {
		if (blob_sdb != 0) {
			rbur.highest_id = 0;
			blob_id = 0;
			__os_free(env, blob_sub_dir);
			blob_sub_dir = NULL;
			__os_free(env, dir);
			dir = NULL;
			goto find_sdb;
		} else {
			/* Mark as the end of the files. */
filedone:		F_SET(&rbu, BLOB_DONE);
			rbur.highest_id = 0;
		}
	} else
		STAT(rep->stat.st_nthrottles++);

	rbu.num_blobs = num_blobs;
	rbu.highest_id = rbur.highest_id;
	__rep_blob_update_marshal(env, &rbu, rbudbt.data);
	RPRINT(env, (env, DB_VERB_REP_SYNC,
"Sending blob_update: file_id %llu, num_blobs %lu, highest %llu, flags %lu",
	    (long long)rbu.blob_fid, (long)num_blobs,
	    (long long)rbu.highest_id, (unsigned long)rbu.flags));
	(void)__rep_send_message(
	    env, eid, REP_BLOB_UPDATE, NULL, &rbudbt, 0, 0);

err:	if (sdb != NULL)
		__os_free(env, sdb);
	if (dirs != NULL)
		__os_dirfree(env, dirs, dirs_cnt);
	if (dir != NULL)
		__os_free(env, dir);
	if (blob_sub_dir != NULL)
		__os_free(env, blob_sub_dir);
	if (rbudbt.data != NULL)
		__os_free(env, rbudbt.data);
	return (ret);
}

/*
 * __rep_blob_find_files
 *
 * Search a directory for blob files, starting with the given blob id and
 * sub-database id.  Add information for each blob to the message buffer until
 * there are no more files, or it has reached the maximum send amount in terms
 * of combined blob files size.
 *
 * This search is complicated because the blobs have to be sent in order by id,
 * but there can be huge holes between a blob file and the one with the next
 * highest id, so iterating through the ids looking to see if the file exists
 * for each id will take too long.  The solution is to walk the directory
 * hierarchy in order, reading every file in that directory, sorting them by
 * id, and adding them to the update list.
 */
static int
__rep_blob_find_files(
    env, ip, dir, blob_id, blob_sid, blob_fid, highest, buf, sent, num)
	ENV *env;
	DB_THREAD_INFO *ip;
	const char *dir;
	db_seq_t *blob_id;
	db_seq_t blob_sid;
	db_seq_t blob_fid;
	db_seq_t *highest;
	DBT *buf;
	size_t *sent;
	u_int32_t *num;
{
	DB *bmd;
	DB_FH *fhp;
	DB_TXN *txn;
	REP *rep;
	__rep_blob_file_args rbf;
	char blob_path[MAX_BLOB_PATH_SZ], **dirs, **files, *path, *ptr;
	db_seq_t tmp;
	int blob_path_len, cur, depth, dirs_cnt, files_cnt, ret;
	off_t blob_size;
	size_t len;
	u_int32_t bytes, mbytes, throttle;

	bmd = NULL;
	txn = NULL;
	fhp = NULL;
	path = NULL;
	dirs = files = NULL;
	dirs_cnt = files_cnt = 0;
	rbf.blob_sid = (u_int64_t)blob_sid;
	rep = env->rep_handle->region;
	throttle = rep->gbytes * GIGABYTE + rep->bytes;
	if (throttle == 0)
		throttle = BLOB_THROTTLE_DEFAULT;

	if ((ret = __os_malloc(
	    env, strlen(dir) + MAX_BLOB_PATH_SZ, &path)) != 0)
		goto err;

	/*
	 * Read the highest possible blob id from the blob meta database, so
	 * we know when to stop looking for files for this database.  The
	 * highest value is reset everytime we switch to a new subdatabase.
	 */
	if (*highest == 0) {
		if ((ret = __db_create_internal(&bmd, env, 0)) != 0)
			goto err;

		if ((ret = __txn_begin(
		    env, ip, NULL, &txn, DB_IGNORE_LEASE)) != 0)
			goto err;

		bmd->blob_file_id = blob_fid;
		bmd->blob_sdb_id = blob_sid;

		if ((ret = __blob_highest_id(bmd, txn, highest) ) != 0)
			goto err;

		if ((ret = __txn_abort(txn)) != 0)
			goto err;
		txn = NULL;
		if ((ret = __db_close(bmd, NULL, 0)) != 0)
			goto err;
		bmd = NULL;
		(*highest)++;
	}

	(*blob_id)++;
	while (*sent < throttle && *blob_id < *highest) {
		memset(blob_path, 0, MAX_BLOB_PATH_SZ);
		blob_path_len = depth = 0;

		/* Calucate the subdirectory from the blob id. */
		__blob_calculate_dirs(
		    *blob_id, blob_path, &blob_path_len, &depth);
		if (blob_path_len != 0) {
			(void)sprintf(path, "%s%c%s%c",
			dir, PATH_SEPARATOR[0], blob_path, PATH_SEPARATOR[0]);
		} else
			(void)sprintf(path, "%s", dir);
		len = strlen(path);

		/* If the sub-directory does not exist, look for the next. */
		if (__os_exists(env, path, NULL) != 0) {
			(*blob_id) +=
			    BLOB_DIR_ELEMS - (*blob_id % BLOB_DIR_ELEMS);
			continue;
		}

		/* Get a list of all the blob files, sorted by id. */
		if ((ret = __os_dirlist(env, path, 0, &dirs, &dirs_cnt)) != 0)
			goto err;

		if ((ret = __rep_blob_sort_dirs(env, __rep_select_blob_file,
		    dirs, dirs_cnt, &files, &files_cnt)) != 0)
			goto err;

		/*
		 * Find the first blob file with an id greater than or equal to
		 * the last id.
		 */
		for (cur = 0; cur < files_cnt; cur++) {
			ptr = files[cur];
			ptr += strlen(BLOB_FILE_PREFIX);
			if ((ret = __blob_str_to_id(
			    env, (const char **)&ptr, &tmp)) != 0)
				goto err;
			DB_ASSERT(env, tmp != 0);
			if (tmp >= *blob_id)
				break;
		}

		/* Add each remaining blob file to the message buffer. */
		while (cur < files_cnt) {
			/* Get the blob id from the current file name. */
			(void)sprintf(path + len, "%s", files[cur]);
			ptr = path + len + strlen(BLOB_FILE_PREFIX);
			if ((ret = __blob_str_to_id(
			    env, (const char **)&ptr, blob_id)) != 0)
				goto err;
			rbf.blob_id = (u_int64_t)*blob_id;
			/* Open the file and get its size. */
			if ((ret = __os_open(
			    env, path, 0, DB_OSO_RDONLY, 0, &fhp)) != 0) {
				if (ret == ENOENT) {
					ret = 0;
					RPRINT(env, (env, DB_VERB_REP_SYNC,
			"blob_update external file: %llu deleted, skipping.",
					    (long long)rbf.blob_id));
					cur++;
					continue;
				}
				goto err;
			}
			if ((ret = __os_ioinfo(
			    env, path, fhp, &mbytes, &bytes, NULL)) != 0)
				goto err;
			if ((ret =__os_closehandle(env, fhp)) != 0)
				goto err;
			fhp = NULL;
			blob_size = ((off_t)mbytes * (off_t)MEGABYTE) + bytes;
			rbf.blob_size = (u_int64_t)blob_size;
			if (blob_size > UINT32_MAX)
				(*sent) = throttle + 1;
			else {
				if (((*sent) + (size_t)blob_size) < (*sent))
					(*sent) = throttle + 1;
				else
					(*sent) += (size_t)blob_size;
			}
			__rep_blob_file_marshal(
			    env, &rbf, (u_int8_t *)buf->data + buf->size);
			(*num)++;
			buf->size += __REP_BLOB_FILE_SIZE;
			RPRINT(env, (env, DB_VERB_REP_SYNC,
	"blob_update adding: blob_sid %llu, blob_id %llu blob_size %llu",
			    (long long)rbf.blob_sid,
			    (long long)rbf.blob_id, (long long)rbf.blob_size));
			if ((*sent) > throttle)
				goto err;

			/* Resize if there is not enough space to grow. */
			if (buf->size > (buf->ulen - __REP_BLOB_FILE_SIZE)) {
				if ((ret = __os_realloc(
				    env, buf->ulen * 2, &buf->data)) != 0)
					goto err;
				buf->ulen *= 2;
			}
			cur++;
		}
		/*
		 * Move to the next directory of blob files by setting the blob
		 * id to the next largest possible value.
		 */
		(*blob_id) += BLOB_DIR_ELEMS - (*blob_id % BLOB_DIR_ELEMS);
		__os_free(env, files);
		files = NULL;
		__os_dirfree(env, dirs, dirs_cnt);
		dirs = NULL;
	}
err:
	if (path != NULL)
		__os_free(env, path);
	if (files != NULL)
		__os_free(env, files);
	if (dirs != NULL)
		__os_dirfree(env, dirs, dirs_cnt);
	if (fhp != NULL)
		(void)__os_closehandle(env, fhp);
	if (txn != NULL)
		(void)__txn_abort(txn);
	if (bmd != NULL)
		(void)__db_close(bmd, NULL, 0);

	return (ret);
}

/*
 * __rep_find_dbs -
 *	Walk through all the named files/databases including those in the
 *	environment or data_dirs and those that in named and in-memory.  We
 *	need to	open them, gather the necessary information and then close
 *	them.
 *
 * May be called either while holding REP_SYSTEM_LOCK or without.
 */
static int
__rep_find_dbs(env, context)
	ENV *env;
	FILE_LIST_CTX *context;
{
	DB_ENV *dbenv;
	int ret;
	char **ddir, *real_dir;

	dbenv = env->dbenv;
	ret = 0;
	real_dir = NULL;

	/*
	 * If we have a data directory, walk it get a list of the
	 * replicated user databases.  If the application has a metadata_dir,
	 * this will also find any persistent internal system databases.
	 */
	if (!F_ISSET(context, FILE_CTX_INMEM_ONLY) &&
	    dbenv->db_data_dir != NULL) {
		for (ddir = dbenv->db_data_dir; *ddir != NULL; ++ddir) {
			if ((ret = __db_appname(env,
			    DB_APP_NONE, *ddir, NULL, &real_dir)) != 0)
				break;
			if ((ret = __rep_walk_dir(env,
			    real_dir, *ddir, context)) != 0)
				break;
			__os_free(env, real_dir);
			real_dir = NULL;
		}
	}

	/*
	 * Walk the environment directory.  If the application doesn't
	 * have a metadata_dir, this will return persistent internal system
	 * databases.  If the application doesn't have a separate data
	 * directory, this will also return all user databases.
	 */
	if (!F_ISSET(context, FILE_CTX_INMEM_ONLY) && ret == 0)
		ret = __rep_walk_dir(env, env->db_home, NULL, context);

	/* Gather the databases in the blob directory. */
	if (!F_ISSET(context, FILE_CTX_INMEM_ONLY) && ret == 0)
		ret = __rep_walk_blob_dir(env, context);

	/*
	 * Now, collect any in-memory named databases.  We do this no
	 * matter if the INMEM_ONLY flag is set or not.
	 */
	if (ret == 0)
		ret = __rep_walk_dir(env, NULL, NULL, context);

	if (real_dir != NULL)
		__os_free(env, real_dir);
	return (ret);
}

/*
 * __rep_walk_blob_dir --
 *
 * The blob directory hierarchy consists of a top layer that contains the
 * blob meta database (BMD) and a set of blob directories (BLDIR).
 * Each BLDIR corresponds to a database file.  If the database file doesn't
 * contain subdatabases, the BLDIR contains a BMD and blob files.  If the
 * database file contains subdatabases, the BLDIR contains a BLSDIR
 * subdirectory for each subdatabase.  Each BLSDIR contains a BMD and blob
 * files.
 *
 * This function walks the blob directory hierarchy and records any BMD.
 * It first checks if the top level BMD exists, and if it does searches
 * the first and second layers of the hierarchy for BMDs.
 */
static int
__rep_walk_blob_dir(env, context)
	ENV *env;
	FILE_LIST_CTX *context;
{
	int cnt, cnt2, i, j, ret;
	size_t len;
	char *blob_dir, *blob_sub, **dirs, *name, *name2, **subdirs;
	char blob_sub_buf[MAX_BLOB_PATH_SZ];
	const char *bmd, *dirp;

	cnt = cnt2 = 0;
	blob_dir = name = name2 = NULL;
	dirs = subdirs = NULL;
	bmd = BLOB_META_FILE_NAME;
	blob_sub = blob_sub_buf;

	if ((ret = __db_appname(
	    env, DB_APP_BLOB, BLOB_META_FILE_NAME, &dirp, &name)) != 0)
		goto err;

	/*
	 * If the main blob meta database does not exist, then no databases in
	 * the environment supports blobs.
	 */
	if ((ret = __os_exists(env, name, NULL)) != 0) {
		ret = 0;
		goto err;
	}

	/* Get the blob directory. */
	if ((ret = __db_appname(
	    env, DB_APP_BLOB, NULL, &dirp, &blob_dir)) != 0)
		goto err;

	if ((ret = __rep_add_files_to_list(
	    env, blob_dir, NULL, context, &bmd, 1)) != 0)
		goto err;

	if ((ret = __os_dirlist(env, blob_dir, 1, &dirs, &cnt)) != 0)
		goto err;

	__os_free(env, name);
	name = NULL;
	if ((ret = __os_malloc(
	    env, MAX_BLOB_PATH_SZ + strlen(blob_dir), &name)) != 0)
		goto err;

	for (i = 0; i < cnt; i++) {
		/*
		 * Skip blob files and the top level BMD
		 * (which was handled above).
		 */
		if (IS_BLOB_META(dirs[i]) || IS_BLOB_FILE(dirs[i]))
			continue;
		len = strlen(blob_dir) +
		    strlen(dirs[i]) + strlen(BLOB_META_FILE_NAME) + 3;
		(void)snprintf(name, len, "%s%c%s%c%s", blob_dir,
		    PATH_SEPARATOR[0], dirs[i], PATH_SEPARATOR[0],
		    BLOB_META_FILE_NAME);
		/*
		 * If a blob meta database exists, add it to the list, and move
		 * on to the next directory, otherwise get a directory list and
		 * check the second layer for BMD.  If a directory contains a
		 * BMD, then it cannot contain subdirectories with BMD.
		 */
		if (__os_exists(env, name, NULL) == 0) {
			(void)snprintf(blob_sub,
			    strlen(dirs[i]) + strlen(bmd) + 2,
			    "%s%c%s", dirs[i], PATH_SEPARATOR[0], bmd);
			if ((ret = __rep_add_files_to_list(env, blob_dir,
			    NULL, context, (const char **)&blob_sub, 1)) != 0)
				goto err;
		} else {
			len = strlen(blob_dir) + strlen(dirs[i]) + 2;
			(void)snprintf(name, len, "%s%c%s",
			    blob_dir, PATH_SEPARATOR[0], dirs[i]);
			if ((ret = __os_dirlist(
			    env, name, 1, &subdirs, &cnt2)) != 0)
				goto err;
			if (name2 == NULL) {
				if ((ret = __os_malloc(env,
				    MAX_BLOB_PATH_SZ + strlen(name),
				    &name2)) != 0)
					goto err;
			}
			for (j = 0; j < cnt2; j++) {
				if (IS_BLOB_FILE(subdirs[j]))
					continue;
				len = strlen(name) + strlen(subdirs[j])
				    + strlen(BLOB_META_FILE_NAME) + 3;
				(void)snprintf(name2, len, "%s%c%s%c%s",
				    name, PATH_SEPARATOR[0], subdirs[j],
				    PATH_SEPARATOR[0], BLOB_META_FILE_NAME);
				if ((ret = __os_exists(
				    env, name2, NULL)) == 0) {
					len = strlen(dirs[i])
					    + strlen(subdirs[j])
					    + strlen(bmd) + 3;
					(void)snprintf(blob_sub,
					    len, "%s%c%s%c%s", dirs[i],
					    PATH_SEPARATOR[0], subdirs[j],
					    PATH_SEPARATOR[0], bmd);
					if ((ret = __rep_add_files_to_list(
					    env, blob_dir, NULL, context,
					    (const char **)&blob_sub, 1)) != 0)
						goto err;
				}
			}
			__os_dirfree(env, subdirs, cnt2);
			subdirs = NULL;
		}
	}

err:	if (name != NULL)
		__os_free(env, name);
	if (name2 != NULL)
		__os_free(env, name2);
	if (blob_dir != NULL)
		__os_free(env, blob_dir);
	if (dirs != NULL)
		__os_dirfree(env, dirs, cnt);
	if (subdirs != NULL)
		__os_dirfree(env, subdirs, cnt2);
	return (ret);
}

/*
 * __rep_walk_dir --
 *
 * This is the routine that walks a directory and fills in the structures
 * that we use to generate messages to the client telling it what
 * files are available.  If the directory name is NULL, then we should
 * walk the list of in-memory named files.
 */
static int
__rep_walk_dir(env, dir, datadir, context)
	ENV *env;
	const char *dir, *datadir;
	FILE_LIST_CTX *context;
{
	int cnt, ret;
	char **names;

	if (dir == NULL) {
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Walk_dir: Getting info for in-memory named files"));
		if ((ret = __memp_inmemlist(env, &names, &cnt)) != 0)
			return (ret);
	} else {
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Walk_dir: Getting info for datadir %s, dir: %s",
		    datadir == NULL ? "NULL" : datadir, dir));
		if ((ret = __os_dirlist(env, dir, 0, &names, &cnt)) != 0)
			return (ret);
	}
	VPRINT(env, (env, DB_VERB_REP_SYNC, "Walk_dir: Dir %s has %d files",
	    (dir == NULL) ? "INMEM" : dir, cnt));
	ret = __rep_add_files_to_list(
	    env, dir, datadir, context, (const char **)names, cnt);

	__os_dirfree(env, names, cnt);
	return (ret);
}

/*
 * __rep_add_files_to_list --
 *
 * Add the given files to the file list.
 */
static int
__rep_add_files_to_list(env, dir, datadir, context, names, cnt)
	ENV *env;
	const char *dir, *datadir;
	FILE_LIST_CTX *context;
	const char **names;
	int cnt;
{
	__rep_fileinfo_args tmpfp;
	size_t avail, len;
	int first_file, i, ret;
	u_int8_t uid[DB_FILE_ID_LEN];
	const char *file, *subdb;

	first_file = 1;
	ret = 0;
	for (i = 0; i < cnt; i++) {
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Walk_dir: File %d name: %s", i, names[i]));
		if (!__rep_is_replicated_db(names[i], dir))
			continue;

		/* We found a file to process. */
		if (dir == NULL) {
			file = NULL;
			subdb = names[i];
		} else {
			file = names[i];
			subdb = NULL;
		}
		if ((ret = __rep_get_fileinfo(env,
		    file, subdb, &tmpfp, uid)) != 0) {
			/*
			 * If we find a file that isn't a database, skip it.
			 */
			RPRINT(env, (env, DB_VERB_REP_SYNC,
			    "Walk_dir: File %d %s: returned error %s",
			    i, names[i], db_strerror(ret)));
			ret = 0;
			continue;
		}
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Walk_dir: File %s at 0x%lx: pgsize %lu, max_pgno %lu",
		    names[i], P_TO_ULONG(context->fillptr),
		    (u_long)tmpfp.pgsize, (u_long)tmpfp.max_pgno));

		/*
		 * On the first time through the loop, check to see if the file
		 * we're about to add is already on the list.  If it is, it must
		 * have been added in a previous call, and that means the
		 * directory we're currently scanning has already been scanned
		 * before.  (This can happen if the user called
		 * env->set_data_dir() more than once for the same directory.)
		 * If that's the case, we're done: not only is it a waste of
		 * time to scan the same directory again, but doing so would
		 * result in the same files appearing in the list more than
		 * once.
		 */
		if (first_file && dir != NULL &&
		    (ret = __rep_walk_filelist(env, context->version,
		    FIRST_FILE_PTR(context->buf), context->size,
		    context->count, __rep_check_uid, uid)) != 0) {
			if (ret == DB_KEYEXIST)
				ret = 0;
			goto err;
		}
		first_file = 0;

		/*
		 * Finally we know that this file is a suitable database file
		 * that we haven't yet included on our list.
		 */
		tmpfp.filenum = context->count++;

		if (datadir != NULL)
			DB_SET_DBT(tmpfp.dir, datadir, strlen(datadir) + 1);
		else {
			DB_SET_DBT(tmpfp.dir, NULL, 0);
		}
		DB_SET_DBT(tmpfp.info, names[i], strlen(names[i]) + 1);
		DB_SET_DBT(tmpfp.uid, uid, DB_FILE_ID_LEN);
retry:		avail = (size_t)(&context->buf[context->size] -
		    context->fillptr);
		/*
		 * It is safe to cast to the old structs
		 * because the first part of the current
		 * struct matches the old structs.
		 */
		if (context->version < DB_REPVERSION_53)
			ret = __rep_fileinfo_v6_marshal(env,
			    (__rep_fileinfo_v6_args *)&tmpfp,
			    context->fillptr, avail, &len);
		else if (context->version < DB_REPVERSION_61)
			ret = __rep_fileinfo_v7_marshal(env,
			    (__rep_fileinfo_v7_args *)&tmpfp,
			    context->fillptr, avail, &len);
		else
			ret = __rep_fileinfo_marshal(env,
			    &tmpfp, context->fillptr, avail, &len);
		if (ret == ENOMEM) {
			/*
			 * Here, 'len' is the total space in use in the buffer.
			 */
			len = (size_t)(context->fillptr - context->buf);
			context->size *= 2;

			if ((ret = __os_realloc(env,
			    context->size, &context->buf)) != 0)
				goto err;
			context->fillptr = context->buf + len;

			/*
			 * Now that we've reallocated the space, try to
			 * store it again.
			 */
			goto retry;
		}
		/*
		 * Here, 'len' (still) holds the length of the marshaled
		 * information about the current file (as filled in by the last
		 * call to  __rep_fileinfo_marshal()).
		 */
		context->fillptr += len;
	}
err:	return (ret);
}

/*
 * Returns a boolean to indicate whether a file/database with the given name
 * should be included in internal init.
 */
static int
__rep_is_replicated_db(name, dir)
	const char *name, *dir;
{
	if (strcmp(name, "DB_CONFIG") == 0 || strcmp(name, "pragma") == 0)
		return (0);
	if (IS_LOG_FILE(name))
		return (0);

	/*
	 * Remaining things that don't have a "__db" prefix are eligible.
	 */
	if (!IS_DB_FILE(name) || IS_BLOB_META(name))
		return (1);

	/* Here, we know we have a "__db" name. */
	if (name[sizeof(DB_REGION_PREFIX) - 1] == 'p')
		return (1);	/* Partition files are eligible. */

	/*
	 * Replicated system databases are eligible.  When on disk, both DBs are
	 * sub-databases of a single database file.
	 */
	if (dir == NULL) {
		if (strcmp(name, REPMEMBERSHIP) == 0 ||
		    strcmp(name, REPLSNHIST) == 0)
			return (1);
	} else {
		if (IS_REP_FILE(name))
			return (1);
	}

	/* Some other "__db" named file. */
	return (0);
}

/*
 * Check whether the given uid is already present in the list of files being
 * built in the context buffer.  A return of DB_KEYEXIST means it is.
 */
static int
__rep_check_uid(env, rfp, uid)
	ENV *env;
	__rep_fileinfo_args *rfp;
	void *uid;
{
	int ret;

	ret = 0;
	if (memcmp(rfp->uid.data, uid, DB_FILE_ID_LEN) == 0) {
		VPRINT(env, (env, DB_VERB_REP_SYNC,
			"Check_uid: Found matching file."));
		ret = USR_ERR(env, DB_KEYEXIST);
	}
	return (ret);

}

static int
__rep_get_fileinfo(env, file, subdb, rfp, uid)
	ENV *env;
	const char *file, *subdb;
	__rep_fileinfo_args *rfp;
	u_int8_t *uid;
{
	DB *dbp;
	DB_THREAD_INFO *ip;
	int lorder, ret, t_ret;
	u_int32_t flags;

	dbp = NULL;

	ENV_GET_THREAD_INFO(env, ip);

	if ((ret = __db_create_internal(&dbp, env, 0)) != 0)
		goto err;
	/*
	 * Use DB_AM_RECOVER to prevent getting handle locks during the open,
	 * otherwise exclusive database handles would block the master from
	 * handling UPDATE_REQ.
	 */
	F_SET(dbp, DB_AM_RECOVER);
	flags = DB_RDONLY | (F_ISSET(env, ENV_THREAD) ? DB_THREAD : 0);
	if (file != NULL && IS_BLOB_META(file))
		LF_SET(DB_INTERNAL_BLOB_DB);
	if ((ret = __db_open(dbp, ip, NULL,
	    file, subdb, DB_UNKNOWN, flags, 0, PGNO_BASE_MD)) != 0)
		goto err;

	SET_LO_HI_VAR(dbp->blob_file_id, rfp->blob_fid_lo, rfp->blob_fid_hi);

	rfp->pgno = 0;

	/*
	 * Queue is a special-case.  We need to set max_pgno to 0 so that
	 * the client can compute the pages from the meta-data.
	 */
	if (dbp->type == DB_QUEUE)
		rfp->max_pgno = 0;
	else
		rfp->max_pgno = dbp->mpf->mfp->last_pgno;
	rfp->pgsize = dbp->pgsize;
	memcpy(uid, dbp->fileid, DB_FILE_ID_LEN);
	rfp->type = (u_int32_t)dbp->type;
	rfp->db_flags = dbp->flags;
	rfp->finfo_flags = 0;
	/* Remember the byte order of this database. */
	(void)__db_get_lorder(dbp, &lorder);
	if (lorder == 1234)
		FLD_SET(rfp->finfo_flags, REPINFO_DB_LITTLEENDIAN);
	else
		FLD_CLR(rfp->finfo_flags, REPINFO_DB_LITTLEENDIAN);

err:
	if (dbp != NULL && (t_ret = __db_close(dbp, NULL, 0)) != 0 && ret == 0)
		ret = t_ret;
	return (ret);
}

/*
 * __rep_page_req
 *	Process a page_req and send the page information to the client.
 *
 * PUBLIC: int __rep_page_req __P((ENV *,
 * PUBLIC:     DB_THREAD_INFO *, int, __rep_control_args *, DBT *));
 */
int
__rep_page_req(env, ip, eid, rp, rec)
	ENV *env;
	DB_THREAD_INFO *ip;
	int eid;
	__rep_control_args *rp;
	DBT *rec;
{
	__rep_fileinfo_args *msgfp, msgf;
	__rep_fileinfo_v6_args *msgfpv6;
	__rep_fileinfo_v7_args *msgfpv7;
	DB_MPOOLFILE *mpf;
	DB_REP *db_rep;
	REP *rep;
	int ret, t_ret;
	u_int8_t *next;
	void *msgfree;

	db_rep = env->rep_handle;
	rep = db_rep->region;

	/*
	 * Build a current struct by copying in the older
	 * version struct and then setting up the new fields.
	 * This is safe because all old fields are in the
	 * same location in the current struct.
	 */
	if (rp->rep_version < DB_REPVERSION_53) {
		if ((ret = __rep_fileinfo_v6_unmarshal(env,
		    &msgfpv6, rec->data, rec->size, &next)) != 0)
			return (ret);
		memcpy(&msgf, msgfpv6, sizeof(__rep_fileinfo_v6_args));
		msgf.dir.data = NULL;
		msgf.dir.size = 0;
		msgf.blob_fid_lo = msgf.blob_fid_hi = 0;
		msgfp = &msgf;
		msgfree = msgfpv6;
	} else if (rp->rep_version < DB_REPVERSION_61) {
		if ((ret = __rep_fileinfo_v7_unmarshal(env,
		    &msgfpv7, rec->data, rec->size, &next)) != 0)
			return (ret);
		memcpy(&msgf, msgfpv7, sizeof(__rep_fileinfo_v7_args));
		msgf.blob_fid_lo = msgf.blob_fid_hi = 0;
		msgfp = &msgf;
		msgfree = msgfpv7;
	} else {
		if ((ret = __rep_fileinfo_unmarshal(env,
		    &msgfp, rec->data, rec->size, &next)) != 0)
			return (ret);
		msgfree = msgfp;
	}

	DB_TEST_SET(env->test_abort, DB_TEST_NO_PAGES);
	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "page_req: file %d page %lu to %lu",
	    msgfp->filenum, (u_long)msgfp->pgno, (u_long)msgfp->max_pgno));

	/*
	 * We need to open the file and then send its pages.
	 * If we cannot open the file, we send REP_FILE_FAIL.
	 */
	VPRINT(env, (env, DB_VERB_REP_SYNC,
	    "page_req: Open %d via mpf_open", msgfp->filenum));
	if ((ret = __rep_mpf_open(env, &mpf, msgfp, 0)) != 0) {
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "page_req: Open %d failed", msgfp->filenum));
		if (F_ISSET(rep, REP_F_MASTER))
			(void)__rep_send_message(env, eid, REP_FILE_FAIL,
			    NULL, rec, 0, 0);
		else
			ret = USR_ERR(env, DB_NOTFOUND);
		goto err;
	}

	ret = __rep_page_sendpages(env, ip, eid, rp, msgfp, mpf, NULL);
	t_ret = __memp_fclose(mpf, 0);
	if (ret == 0 && t_ret != 0)
		ret = t_ret;
err:
DB_TEST_RECOVERY_LABEL
	__os_free(env, msgfree);
	return (ret);
}

static int
__rep_page_sendpages(env, ip, eid, rp, msgfp, mpf, dbp)
	ENV *env;
	DB_THREAD_INFO *ip;
	int eid;
	__rep_control_args *rp;
	__rep_fileinfo_args *msgfp;
	DB_MPOOLFILE *mpf;
	DB *dbp;
{
	DB *qdbp;
	DBC *qdbc;
	DBT msgdbt;
	DB_LOG *dblp;
	DB_LSN lsn;
	DB_REP *db_rep;
	PAGE *pagep;
	REP *rep;
	REP_BULK bulk;
	REP_THROTTLE repth;
	db_pgno_t p;
	uintptr_t bulkoff;
	size_t len, msgsz;
	u_int32_t bulkflags, use_bulk;
	int opened, ret, t_ret;
	u_int8_t *buf;

	dblp = env->lg_handle;
	db_rep = env->rep_handle;
	rep = db_rep->region;
	opened = 0;
	t_ret = 0;
	qdbp = NULL;
	qdbc = NULL;
	buf = NULL;
	bulk.addr = NULL;
	use_bulk = FLD_ISSET(rep->config, REP_C_BULK);
	if (msgfp->type == (u_int32_t)DB_QUEUE) {
		if (dbp == NULL) {
			if ((ret = __db_create_internal(&qdbp, env, 0)) != 0)
				goto err;
			/*
			 * We need to check whether this is in-memory so that
			 * we pass the name correctly as either the file or
			 * the database name.
			 */
			if ((ret = __db_open(qdbp, ip, NULL,
			    FLD_ISSET(msgfp->db_flags, DB_AM_INMEM) ?
			    NULL : msgfp->info.data,
			    FLD_ISSET(msgfp->db_flags, DB_AM_INMEM) ?
			    msgfp->info.data : NULL,
			    DB_UNKNOWN,
			DB_RDONLY | (F_ISSET(env, ENV_THREAD) ? DB_THREAD : 0),
			    0, PGNO_BASE_MD)) != 0)
				goto err;
			opened = 1;
		} else
			qdbp = dbp;
		if ((ret = __db_cursor(qdbp, ip, NULL, &qdbc, 0)) != 0)
			goto err;
	}
	msgsz = __REP_FILEINFO_SIZE + DB_FILE_ID_LEN + msgfp->pgsize +
	    msgfp->dir.size;
	if ((ret = __os_calloc(env, 1, msgsz, &buf)) != 0)
		goto err;
	memset(&msgdbt, 0, sizeof(msgdbt));
	VPRINT(env, (env, DB_VERB_REP_SYNC,
	    "sendpages: file %d page %lu to %lu",
	    msgfp->filenum, (u_long)msgfp->pgno, (u_long)msgfp->max_pgno));
	memset(&repth, 0, sizeof(repth));
	/*
	 * If we're doing bulk transfer, allocate a bulk buffer to put our
	 * pages in.  We still need to initialize the throttle info
	 * because if we encounter a page larger than our entire bulk
	 * buffer, we need to send it as a singleton.
	 *
	 * Use a local var so that we don't need to worry if someone else
	 * turns on/off bulk in the middle of our call here.
	 */
	if (use_bulk && (ret = __rep_bulk_alloc(env, &bulk, eid,
	    &bulkoff, &bulkflags, REP_BULK_PAGE)) != 0)
		goto err;
	REP_SYSTEM_LOCK(env);
	repth.gbytes = rep->gbytes;
	repth.bytes = rep->bytes;
	repth.type = REP_PAGE;
	repth.data_dbt = &msgdbt;
	REP_SYSTEM_UNLOCK(env);

	for (p = msgfp->pgno; p <= msgfp->max_pgno; p++) {
		if (msgfp->type == (u_int32_t)DB_QUEUE && p != 0) {
			/*
			 * If queue returns ENOENT or if queue is not configured
			 * convert it into PAGE_NOTFOUND.  Queue might return
			 * ENOENT if an entire extent file does not exist in the
			 * "middle" of the database.
			 */
#ifdef HAVE_QUEUE
			if ((ret = __qam_fget(qdbc, &p, 0, &pagep)) == ENOENT)
#endif
				ret = USR_ERR(env, DB_PAGE_NOTFOUND);
		} else
			ret = __memp_fget(mpf, &p, ip, NULL, 0, &pagep);
		msgfp->pgno = p;
		if (ret == DB_PAGE_NOTFOUND) {
			if (F_ISSET(rep, REP_F_MASTER)) {
				ret = 0;
				RPRINT(env, (env, DB_VERB_REP_SYNC,
				    "sendpages: PAGE_FAIL on page %lu",
				    (u_long)p));
				/*
				 * It is safe to cast to the old structs
				 * because the first part of the current
				 * struct matches the old structs.
				 */
				if (rp->rep_version < DB_REPVERSION_53)
					ret = __rep_fileinfo_v6_marshal(env,
					    (__rep_fileinfo_v6_args *)msgfp,
					    buf, msgsz, &len);
				else if (rp->rep_version < DB_REPVERSION_61)
					ret = __rep_fileinfo_v7_marshal(env,
					    (__rep_fileinfo_v7_args *)msgfp,
					    buf, msgsz, &len);
				else
					ret = __rep_fileinfo_marshal(env,
					    msgfp, buf, msgsz, &len);
				if (ret != 0)
					goto err;
				LOG_SYSTEM_LOCK(env);
				lsn = ((LOG *)dblp->reginfo.primary)->lsn;
				LOG_SYSTEM_UNLOCK(env);
				DB_SET_DBT(msgdbt, buf, len);
				(void)__rep_send_message(env, eid,
				    REP_PAGE_FAIL, &lsn, &msgdbt, 0, 0);
				continue;
			} else
				ret = USR_ERR(env, DB_NOTFOUND);
			goto err;
		} else if (ret != 0)
			goto err;
		else
			DB_SET_DBT(msgfp->info, pagep, msgfp->pgsize);
		len = 0;
		/*
		 * Send along an indication of the byte order of this mpool
		 * page.  Since mpool always keeps pages in the native byte
		 * order of the local environment, this is simply my
		 * environment's byte order.
		 *
		 * Since pages can be served from a variety of sites when using
		 * client-to-client synchronization, the receiving client needs
		 * to know the byte order of each page independently.
		 */
		if (F_ISSET(env, ENV_LITTLEENDIAN))
			FLD_SET(msgfp->finfo_flags, REPINFO_PG_LITTLEENDIAN);
		else
			FLD_CLR(msgfp->finfo_flags, REPINFO_PG_LITTLEENDIAN);
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "sendpages: %lu, page lsn [%lu][%lu]", (u_long)p,
		    (u_long)pagep->lsn.file, (u_long)pagep->lsn.offset));
		/*
		 * It is safe to cast to the old structs
		 * because the first part of the current
		 * structs matches the old struct.
		 */
		if (rp->rep_version < DB_REPVERSION_53)
			ret = __rep_fileinfo_v6_marshal(env,
			    (__rep_fileinfo_v6_args *)msgfp,
			    buf, msgsz, &len);
		else if (rp->rep_version < DB_REPVERSION_61)
			ret = __rep_fileinfo_v7_marshal(env,
			    (__rep_fileinfo_v7_args *)msgfp,
			    buf, msgsz, &len);
		else
			ret = __rep_fileinfo_marshal(env,
			    msgfp, buf, msgsz, &len);
		if (msgfp->type != (u_int32_t)DB_QUEUE || p == 0)
			t_ret = __memp_fput(mpf,
			    ip, pagep, DB_PRIORITY_UNCHANGED);
#ifdef HAVE_QUEUE
		else
			/*
			 * We don't need an #else for HAVE_QUEUE here because if
			 * we're not compiled with queue, then we're guaranteed
			 * to have set REP_PAGE_FAIL above.
			 */
			t_ret = __qam_fput(qdbc, p, pagep, qdbp->priority);
#endif
		if (t_ret != 0 && ret == 0)
			ret = t_ret;
		if (ret != 0)
			goto err;

		DB_ASSERT(env, len <= msgsz);
		DB_SET_DBT(msgdbt, buf, len);

		LOG_SYSTEM_LOCK(env);
		repth.lsn = ((LOG *)dblp->reginfo.primary)->lsn;
		LOG_SYSTEM_UNLOCK(env);
		/*
		 * If we are configured for bulk, try to send this as a bulk
		 * request.  If not configured, or it is too big for bulk
		 * then just send normally.
		 */
		if (use_bulk)
			ret = __rep_bulk_message(env, &bulk, &repth,
			    &repth.lsn, &msgdbt, 0);
		if (!use_bulk || ret == DB_REP_BULKOVF)
			ret = __rep_send_throttle(env, eid, &repth, 0, 0);
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "sendpages: %lu, lsn [%lu][%lu]", (u_long)p,
		    (u_long)repth.lsn.file, (u_long)repth.lsn.offset));
		/*
		 * If we have REP_PAGE_MORE we need to break this loop.
		 * Otherwise, with REP_PAGE, we keep going.
		 */
		if (repth.type == REP_PAGE_MORE || ret != 0) {
			/* Ignore send failure, except to break the loop. */
			if (ret == DB_REP_UNAVAIL)
				ret = 0;
			break;
		}
	}

err:
	/*
	 * We're done, force out whatever remains in the bulk buffer and
	 * free it.
	 */
	if (use_bulk && bulk.addr != NULL &&
	    (t_ret = __rep_bulk_free(env, &bulk, 0)) != 0 && ret == 0 &&
	    t_ret != DB_REP_UNAVAIL)
		ret = t_ret;
	if (qdbc != NULL && (t_ret = __dbc_close(qdbc)) != 0 && ret == 0)
		ret = t_ret;
	if (opened && (t_ret = __db_close(qdbp, NULL, DB_NOSYNC)) != 0 &&
	    ret == 0)
		ret = t_ret;
	if (buf != NULL)
		__os_free(env, buf);
	return (ret);
}

/*
 * __rep_update_setup
 *	Process and setup with this file information.
 *
 * PUBLIC: int __rep_update_setup __P((ENV *, int, __rep_control_args *,
 * PUBLIC:     DBT *, time_t, DB_LSN *));
 */
int
__rep_update_setup(env, eid, rp, rec, savetime, lsn)
	ENV *env;
	int eid;
	__rep_control_args *rp;
	DBT *rec;
	time_t savetime;
	DB_LSN *lsn;
{
	DB_LOG *dblp;
	DB_REP *db_rep;
	DB_THREAD_INFO *ip;
	LOG *lp;
	REGENV *renv;
	REGINFO *infop;
	REP *rep;
	__rep_update_args *rup;
	DB_LSN verify_lsn;
	int clientdb_locked, *origbuf, ret;
	u_int32_t count, size;
	u_int8_t *end, *next;

	db_rep = env->rep_handle;
	rep = db_rep->region;
	dblp = env->lg_handle;
	lp = dblp->reginfo.primary;
	infop = env->reginfo;
	renv = infop->primary;
	clientdb_locked = 0;
	ret = 0;

	MUTEX_LOCK(env, rep->mtx_clientdb);
	verify_lsn = lp->verify_lsn;
	MUTEX_UNLOCK(env, rep->mtx_clientdb);
	REP_SYSTEM_LOCK(env);
	if (rep->sync_state != SYNC_UPDATE || IN_ELECTION(rep)) {
		REP_SYSTEM_UNLOCK(env);
		return (0);
	}
	rep->sync_state = SYNC_OFF;

	if ((ret = __rep_update_unmarshal(env,
	    &rup, rec->data, rec->size, &next)) != 0)
		return (ret);
	DB_ASSERT(env, next == FIRST_FILE_PTR((u_int8_t*)rec->data));

	/*
	 * If we're doing an abbreviated internal init, it's because we found a
	 * sync point but we needed to materialize any NIMDBs.  However, if we
	 * now see that there are no NIMDBs we can just skip to verify_match,
	 * just as we would have done if we had already loaded the NIMDBs.  In
	 * other words, if there are no NIMDBs, then I can trivially say that
	 * I've already loaded all of them!  The whole abbreviated internal init
	 * turns out not to have been necessary after all.
	 */
	if (F_ISSET(rep, REP_F_ABBREVIATED)) {
		count = rup->num_files;
		end = &((u_int8_t*)rec->data)[rec->size];
		size = (u_int32_t)(end - next);
		if ((ret = __rep_walk_filelist(env, rp->rep_version,
		    next, size, count, __rep_find_inmem, NULL)) == 0) {
			/*
			 * Not found: there are no NIMDBs on the list.  Revert
			 * to VERIFY state, so that we can pick up where we left
			 * off, except that from now on (i.e., future master
			 * changes) we can skip checking for NIMDBs if we find a
			 * sync point.
			 */
			RPRINT(env, (env, DB_VERB_REP_SYNC,
			    "UPDATE msg reveals no NIMDBs"));
			F_SET(rep, REP_F_NIMDBS_LOADED);
			rep->sync_state = SYNC_VERIFY;
			F_CLR(rep, REP_F_ABBREVIATED);
#ifdef HAVE_REPLICATION_THREADS
			db_rep->abbrev_init = FALSE;
#endif
			ret = __rep_notify_threads(env, AWAIT_NIMDB);

			REP_SYSTEM_UNLOCK(env);
			if (ret == 0 && (ret = __rep_verify_match(env,
			    &verify_lsn, savetime)) == DB_REP_WOULDROLLBACK)
				*lsn = verify_lsn;
			__os_free(env, rup);
			return (ret);
		} else if (ret != DB_KEYEXIST)
			goto err;
	}

	/*
	 * We know we're the first to come in here due to the
	 * SYNC_UPDATE state.
	 */
	rep->sync_state = SYNC_PAGE;
	/*
	 * We should not ever be in internal init with a lease granted.
	 */
	DB_ASSERT(env,
	    !IS_USING_LEASES(env) || __rep_islease_granted(env) == 0);

	/*
	 * We do not clear REP_LOCKOUT_* in this code.
	 * We'll eventually call the normal __rep_verify_match recovery
	 * code and that will clear all the flags and allow others to
	 * proceed.  We lockout both the messages and API here.
	 * We lockout messages briefly because we are about to reset
	 * all our LSNs and we do not want another thread possibly
	 * using/needing those.  We have to lockout the API for
	 * the duration of internal init.
	 */
	if ((ret = __rep_lockout_msg(env, rep, 1)) != 0)
		goto err;

	if ((ret = __rep_lockout_api(env, rep)) != 0)
		goto err;
	/*
	 * We need to update the timestamp and kill any open handles
	 * on this client.  The files are changing completely.
	 */
	(void)time(&renv->rep_timestamp);

	REP_SYSTEM_UNLOCK(env);
	MUTEX_LOCK(env, rep->mtx_clientdb);
	__os_gettime(env, &lp->rcvd_ts, 1);
	lp->wait_ts = rep->request_gap;
	ZERO_LSN(lp->ready_lsn);
	ZERO_LSN(lp->verify_lsn);
	ZERO_LSN(lp->prev_ckp);
	ZERO_LSN(lp->waiting_lsn);
	ZERO_LSN(lp->max_wait_lsn);
	ZERO_LSN(lp->max_perm_lsn);
	ret = __rep_blob_cleanup(env, rep);
	if (ret == 0 && db_rep->rep_db == NULL)
		ret = __rep_client_dbinit(env, 0, REP_DB);
	MUTEX_UNLOCK(env, rep->mtx_clientdb);
	if (ret != 0)
		goto err_nolock;

	/*
	 * We need to empty out any old log records that might be in the
	 * temp database.
	 */
	ENV_GET_THREAD_INFO(env, ip);
	if ((ret = __db_truncate(db_rep->rep_db, ip, NULL, &count)) != 0)
		goto err_nolock;
	STAT_SET(env,
	    rep, log_queued, rep->stat.st_log_queued, 0, &lp->ready_lsn);

	REP_SYSTEM_LOCK(env);
	if (F_ISSET(rep, REP_F_ABBREVIATED)) {
		/*
		 * For an abbreviated internal init, the place from which we'll
		 * want to request master's logs after (NIMDB) pages are loaded
		 * is precisely the sync point we found during VERIFY.  We'll
		 * roll back to there in a moment.
		 *
		 * We don't need first_vers, because it's only used with
		 * __log_newfile, which only happens with non-ABBREVIATED
		 * internal init.
		 */
		rep->first_lsn = verify_lsn;
	} else {
		/*
		 * We will remove all logs we have so we need to request
		 * from the master's beginning.
		 */
		rep->first_lsn = rup->first_lsn;
		rep->first_vers = rup->first_vers;
	}
	rep->last_lsn = rp->lsn;
	rep->nfiles = rup->num_files;

	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "Update setup for %d files.", rep->nfiles));
	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "Update setup:  First LSN [%lu][%lu].",
	    (u_long)rep->first_lsn.file, (u_long)rep->first_lsn.offset));
	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "Update setup:  Last LSN [%lu][%lu]",
	    (u_long)rep->last_lsn.file, (u_long)rep->last_lsn.offset));

	if (rep->nfiles > 0) {
		rep->infoversion = rp->rep_version;
		rep->originfolen = rep->infolen =
		    rec->size - __REP_UPDATE_SIZE;
		MUTEX_LOCK(env, renv->mtx_regenv);
		ret = __env_alloc(infop, (size_t)rep->infolen, &origbuf);
		MUTEX_UNLOCK(env, renv->mtx_regenv);
		if (ret != 0)
			goto err;
		else
			rep->originfo_off = R_OFFSET(infop, origbuf);
		memcpy(R_ADDR(infop, rep->originfo_off),
		    FIRST_FILE_PTR((u_int8_t*)rec->data), rep->infolen);
	}

	/*
	 * Clear the decks to make room for the logs and databases that we will
	 * request as part of this internal init.  For a normal, full internal
	 * init, that means all logs and databases.  For an abbreviated internal
	 * init, it means only the NIMDBs, and only that portion of the log
	 * after the sync point.
	 */
	if (F_ISSET(rep, REP_F_ABBREVIATED)) {
		/*
		 * Note that in order to pare the log back to the sync point, we
		 * can't just crudely hack it off there.  We need to make sure
		 * that pages in regular databases get rolled back to a state
		 * consistent with that sync point.  So we have to do a real
		 * recovery step.
		 */
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Will roll back for abbreviated internal init"));
		if ((ret = __rep_rollback(env, &rep->first_lsn)) != 0) {
			if (ret == DB_REP_WOULDROLLBACK) {
				DB_ASSERT(env, LOG_COMPARE(&rep->first_lsn,
				    &verify_lsn) == 0);
				*lsn = verify_lsn;
			}
			goto err;
		}
		ret = __rep_remove_nimdbs(env);
	} else
		ret = __rep_remove_all(env, rp->rep_version, rec);
	if (ret != 0)
		goto err;
	FLD_CLR(rep->lockout_flags, REP_LOCKOUT_MSG);

	REP_SYSTEM_UNLOCK(env);
	MUTEX_LOCK(env, rep->mtx_clientdb);
	clientdb_locked = 1;
	REP_SYSTEM_LOCK(env);
	rep->curfile = 0;
	ret = __rep_nextfile(env, eid, rep);
	if (ret != 0)
		goto err;

	if (0) {
err_nolock:	REP_SYSTEM_LOCK(env);
	}

err:	/*
	 * If we get an error, we cannot leave ourselves in the RECOVER_PAGE
	 * state because we have no file information.  That also means undo'ing
	 * the rep_lockout.  We need to move back to the RECOVER_UPDATE stage.
	 * In the non-error path, we will have already cleared LOCKOUT_MSG,
	 * but it doesn't hurt to clear it again.
	 */
	FLD_CLR(rep->lockout_flags, REP_LOCKOUT_MSG);
	if (ret != 0) {
		if (rep->originfo_off != INVALID_ROFF) {
			MUTEX_LOCK(env, renv->mtx_regenv);
			__env_alloc_free(infop,
			    R_ADDR(infop, rep->originfo_off));
			MUTEX_UNLOCK(env, renv->mtx_regenv);
			rep->originfo_off = INVALID_ROFF;
		}
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Update_setup: Error: Clear PAGE, set UPDATE again. %s",
		    db_strerror(ret)));
		rep->sync_state = SYNC_UPDATE;
		CLR_LOCKOUT_BDB(rep);
	}
	REP_SYSTEM_UNLOCK(env);
	if (clientdb_locked)
		MUTEX_UNLOCK(env, rep->mtx_clientdb);
	__os_free(env, rup);
	return (ret);
}

/*
 * __rep_blob_update
 *	Prepare to receive blob file data by setting up the blob gap database,
 *	then requesting the blob file data.
 *
 * PUBLIC: int __rep_blob_update __P((ENV *, int, DB_THREAD_INFO *, DBT *));
 */
int
__rep_blob_update(env, eid, ip, rec)
	ENV *env;
	int eid;
	DB_THREAD_INFO *ip;
	DBT *rec;
{
	DBC *dbc;
	DB_REP *db_rep;
	DBT data, key;
	REP *rep;
	REGINFO *infop;
	__rep_blob_file_args rbf;
	__rep_blob_update_args rbu;
	__rep_fileinfo_args *rfp;
	db_seq_t blob_fid;
	int ret;
	off_t offset;
	size_t len;
	u_int32_t num_blobs;
	u_int8_t keybuf[BLOB_KEY_SIZE], *ptr;

	db_rep = env->rep_handle;
	rep = db_rep->region;
	infop = env->reginfo;
	rfp = NULL;
	dbc = NULL;
	memset(&rbu, 0, sizeof(__rep_blob_update_args));
	memset(&rbf, 0, sizeof(__rep_blob_file_args));

	if ((ret = __rep_blob_update_unmarshal(
	    env, &rbu, rec->data, rec->size, &ptr)) != 0)
		return (ret);
	len = rec->size - __REP_BLOB_UPDATE_SIZE;

	RPRINT(env, (env, DB_VERB_REP_SYNC,
"blob_update: file_id %llu, num_blobs %lu, flags %lu, highest %llu",
	    (long long)rbu.blob_fid, (long)rbu.num_blobs,
	    (unsigned long)rbu.flags, (long long)rbu.highest_id));

	MUTEX_LOCK(env, rep->mtx_clientdb);
	REP_SYSTEM_LOCK(env);

	/*
	 * Check if the world changed.
	 */
	if (rep->sync_state != SYNC_PAGE)
		goto unlock;

	/* Make sure this is for the current database. */
	GET_CURINFO(rep, infop, rfp);
	GET_LO_HI(env, rfp->blob_fid_lo, rfp->blob_fid_hi, blob_fid, ret);
	if (ret != 0)
		goto unlock;

	if (blob_fid != (db_seq_t)rbu.blob_fid)
		goto unlock;

	rep->highest_id = (db_seq_t)rbu.highest_id;
	/*
	 * For each blob file, add an entry to the database for each 1 MB
	 * section of that file.  The entries will be deleted as the
	 * coresponding blob chunks arrive and are written to disk.
	 */
	if (db_rep->blob_dbp == NULL &&
	    (ret = __rep_client_dbinit(env, 0, REP_BLOB)) != 0)
		goto unlock;

	if ((ret = __db_cursor(db_rep->blob_dbp, ip, NULL, &dbc, 0)) != 0)
		goto unlock;

	/*
	 * Make sure no one else has populated the database, this could happen
	 * if the update message is sent twice.
	 */
	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));
	if ((ret = __dbc_get(dbc, &key, &data, DB_FIRST)) != DB_NOTFOUND)
		goto unlock;

	/* It is possible for a blob database to have no blobs. */
	if (rbu.num_blobs == 0) {
		(void)__dbc_close(dbc);
		dbc = NULL;
		rep->blob_more_files = 0;
		rep->gap_bl_hi_id = rep->gap_bl_hi_sid = 0;
		rep->last_blob_id = rep->last_blob_sid = 0;
		rep->prev_blob_id = rep->prev_blob_sid = 0;
		rep->gap_bl_hi_off = 0;
		rep->blob_sync = 0;
		rep->highest_id = 0;
		ret = __rep_blobdone(env, eid, ip, rep, blob_fid, 0, 0);
		goto unlock;
	}

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));
	data.flags = key.flags = DB_DBT_USERMEM;
	key.data = keybuf;
	key.ulen = key.size = BLOB_KEY_SIZE;
	data.data = (void *)&offset;
	data.ulen = data.size = sizeof(offset);
	num_blobs = 0;
	while (num_blobs < rbu.num_blobs) {
		if ((ret =
		    __rep_blob_file_unmarshal(env, &rbf, ptr, len, &ptr)) != 0)
			goto unlock;
		len -= __REP_BLOB_FILE_SIZE;

		RPRINT(env, (env, DB_VERB_REP_SYNC,
	"blob_update adding file: blob_id %llu, sdb_id %llu, blob_size %llu",
		    (long long)rbf.blob_id, (long long)rbf.blob_sid,
		    (long long)rbf.blob_size));

		memcpy(keybuf, &rbf.blob_sid, BLOB_ID_SIZE);
		memcpy(&(keybuf[BLOB_ID_SIZE]), &rbf.blob_id, BLOB_ID_SIZE);
		offset = 0;
		/*
		 * Add an entry for each megabyte of the blob file.  Zero
		 * length blob files should have at least one entry.
		 */
		do {
			if ((ret = __dbc_put(dbc, &key, &data, 0)) != 0)
				goto unlock;
			offset += MEGABYTE;
			/*
			 * Check for overflow, this can happen when the master
			 * supports 64 file offsets, but the client does not.
			 */
			if (offset < 0) {
				ret = USR_ERR(env, EINVAL);
				__db_errx(env,
				    DB_STR("3704",
					"External file offset overflow"));
				goto unlock;
			}
		} while ((u_int32_t)offset < rbf.blob_size);
		num_blobs++;
	}
	/* Set whether there are more files after the ones on the list. */
	if (F_ISSET(&rbu, BLOB_DONE))
		rep->blob_more_files = 0;
	else
		rep->blob_more_files = 1;
	rep->prev_blob_id = rep->last_blob_id;
	rep->prev_blob_sid = rep->last_blob_sid;
	rep->last_blob_sid = (db_seq_t)rbf.blob_sid;
	rep->last_blob_id = (db_seq_t)rbf.blob_id;

	/*
	 * Send the same message payload in a REP_BLOB_ALL_REQ message to get
	 * the blob data.  Building the list of blob files is expensive, which
	 * is why it is sent back to whatever site is tasked with returning the
	 * data.
	 *
	 * If this is a re-request, send it to the master, otherwise send it
	 * anywhere.
	 */
	if (F_ISSET(&rbu, BLOB_REREQ))
		(void)__rep_send_message(
		    env, rep->master_id, REP_BLOB_ALL_REQ, NULL, rec, 0, 0);
	else
		(void)__rep_send_message(
		    env, eid, REP_BLOB_ALL_REQ, NULL, rec, 0, DB_REP_ANYWHERE);

unlock:	REP_SYSTEM_UNLOCK(env);
	MUTEX_UNLOCK(env, rep->mtx_clientdb);
	if (dbc != NULL)
		(void)__dbc_close(dbc);

	return (ret);
}

/*
 * __rep_blob_allreq
 *	Request blob file data.
 *
 * PUBLIC: int __rep_blob_allreq __P((ENV *, int, DBT *));
 */
int
__rep_blob_allreq(env, eid, rec)
	ENV *env;
	int eid;
	DBT *rec;
{
	DB *dbp;
	DB_FH *fhp;
	DBT msg;
#ifdef	CONFIG_TEST
	REP *rep;
#endif
	__rep_blob_chunk_args rbc;
	__rep_blob_file_args rbf;
	__rep_blob_update_args rbu;
	db_seq_t old_sdb_id;
	int done, ret;
	off_t offset;
	size_t len;
	u_int32_t num_blobs;
	u_int8_t *chunk_buf, *msg_buf, *ptr;
#ifdef	CONFIG_TEST
	rep = env->rep_handle->region;
#endif
	dbp = NULL;
	fhp = NULL;
	chunk_buf = msg_buf = NULL;
	memset(&rbu, 0, sizeof(__rep_blob_update_args));
	memset(&rbc, 0, sizeof(__rep_blob_chunk_args));
	memset(&msg, 0, sizeof(DBT));

	if ((ret =
	    __os_malloc(env, MEGABYTE + __REP_BLOB_CHUNK_SIZE, &msg_buf)) != 0)
		goto err;
	msg.data = msg_buf;
	msg.ulen = MEGABYTE + __REP_BLOB_CHUNK_SIZE;
	if ((ret = __os_malloc(env, MEGABYTE, &chunk_buf)) != 0)
		goto err;
	rbc.data.data = chunk_buf;
	rbc.data.ulen = MEGABYTE;
	rbc.data.flags = DB_DBT_USERMEM;

	/*
	 * The REP_BLOB_ALL_REQ message sends the REP_BLOB_UPDATE message
	 * payload back to the master/peer to request the actual blobs after
	 * the client has prepared itself to receive them.
	 */
	len = rec->size;
	if ((ret = __rep_blob_update_unmarshal(
	    env, &rbu, rec->data, rec->size, &ptr)) != 0)
		goto err;
	len -= __REP_BLOB_UPDATE_SIZE;

	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "blob_all_req: file_id %llu, num_blobs %lu, flags %lu",
	    (long long)rbu.blob_fid, (long)rbu.num_blobs,
	    (unsigned long)rbu.flags));

	if ((ret = __db_create_internal(&dbp, env, 0)) != 0)
		goto err;
	dbp->blob_file_id = (db_seq_t)rbu.blob_fid;
	rbc.blob_fid = rbu.blob_fid;
	num_blobs = 0;
	/*
	 * The list of files to send is included in the message, go
	 * through the list and send each file in pieces.
	 */
	while (num_blobs < rbu.num_blobs) {
		num_blobs++;
		if ((ret = __rep_blob_file_unmarshal(
		    env, &rbf, ptr, len, &ptr)) != 0)
			goto err;
		len -= __REP_BLOB_FILE_SIZE;
		old_sdb_id = dbp->blob_sdb_id;
		dbp->blob_sdb_id = (db_seq_t)rbf.blob_sid;
		rbc.flags = 0;
		rbc.blob_sid = rbf.blob_sid;
		rbc.blob_id = rbf.blob_id;
		/* Free the sub-directory information if it has changed. */
		if (old_sdb_id != dbp->blob_sdb_id &&
		    dbp->blob_sub_dir != NULL) {
			__os_free(env, dbp->blob_sub_dir);
			dbp->blob_sub_dir = NULL;
		}
		if (dbp->blob_sub_dir == NULL) {
			if ((ret = __blob_make_sub_dir(env, &dbp->blob_sub_dir,
			    dbp->blob_file_id, dbp->blob_sdb_id)) != 0)
				goto err;
		}
		if ((ret = __blob_file_open(dbp,
		    &fhp, (db_seq_t)rbf.blob_id, DB_FOP_READONLY, 0)) != 0) {
			/*
			 * The file may have been deleted between creating the
			 * list and sending the data.  Send a message saying
			 * the file has been deleted.
			 */
			if (ret == ENOENT) {
				if (IS_VIEW_SITE(env) && num_blobs == 1) {
					ret = DB_NOTFOUND;
					goto err;
				}
				F_SET(&rbc, BLOB_DELETE);
				rbc.data.size = 0;
				__rep_blob_chunk_marshal(env, &rbc, msg.data);
				msg.size = __REP_BLOB_CHUNK_SIZE;
				(void)__rep_send_message(env,
				    eid, REP_BLOB_CHUNK, NULL, &msg, 0, 0);
				ret = 0;
				fhp = NULL;
#ifdef	CONFIG_TEST
				STAT_INC(env, rep,
				    ext_deleted, rep->stat.st_ext_deleted, eid);
#endif
				continue;
			}
			goto err;
		}
		offset = 0;
		do {
			done = 0;
			rbc.flags = 0;
			if ((ret = __blob_file_read(
			    env, fhp, &rbc.data, offset, MEGABYTE)) != 0)
				goto err;
			DB_ASSERT(env, rbc.data.size <= MEGABYTE);

			/*
			 * In rare cases the blob file may have gotten shorter
			 * since the list was created.
			 */
			if (rbc.data.size < (u_int32_t)MEGABYTE && (u_int64_t)
			    (offset + rbc.data.size) < rbf.blob_size) {
				F_SET(&rbc, BLOB_CHUNK_FAIL);
				done = 1;
#ifdef	CONFIG_TEST
				STAT_INC(env, rep, ext_truncated,
				    rep->stat.st_ext_truncated, eid);
#endif
			}
			/* File may have grown since the list was made. */
			if ((u_int64_t)
			    (offset + rbc.data.size) > rbf.blob_size) {
				rbc.data.size =
				    (u_int32_t)((off_t)rbf.blob_size - offset);
			}
			rbc.offset = (u_int64_t)offset;
			__rep_blob_chunk_marshal(env, &rbc, msg.data);
			msg.size = __REP_BLOB_CHUNK_SIZE + rbc.data.size;
			(void)__rep_send_message(
			    env, eid, REP_BLOB_CHUNK, NULL, &msg, 0, 0);
			offset += MEGABYTE;
		} while ((u_int64_t)offset < rbf.blob_size && !done);

		if (fhp != NULL && (ret = __os_closehandle(env, fhp)) != 0)
			goto err;
		fhp = NULL;
		/*
		 * DB_TEST_NO_CHUNKS is set during testing to test
		 * that a client will send a BLOB_CHUNK_REQ message
		 * if any BLOB_CHUNK messages are lost.
		 *
		 * Have to send at least one blob chunk message or
		 * BDB will send a BLOB_UPDATE message instead of a
		 * BLOB_CHUNK_REQ message.
		 */
		DB_TEST_SET(env->test_abort, DB_TEST_NO_CHUNKS);
	}
err:
DB_TEST_RECOVERY_LABEL
	if (chunk_buf != NULL)
		__os_free(env, chunk_buf);
	if (msg_buf != NULL)
		__os_free(env, msg_buf);
	if (fhp != NULL)
		(void)__os_closehandle(env, fhp);
	if (dbp != 0)
		(void)__db_close(dbp, NULL, 0);
	return (ret);
}

static int
__rep_find_inmem(env, rfp, unused)
	ENV *env;
	__rep_fileinfo_args *rfp;
	void *unused;
{
	COMPQUIET(env, NULL);
	COMPQUIET(unused, NULL);

	/*
	 * Cannot assume all databases are in-memory because abbreviated
	 * internal inits from 5.3 and earlier are not limited to in-memory
	 * databases.
	 */
	return (FLD_ISSET(rfp->db_flags, DB_AM_INMEM) ? DB_KEYEXIST : 0);
}

/*
 * Removes any currently existing NIMDBs.  We do this at the beginning of
 * abbreviated internal init, when any existing NIMDBs should be intact, so
 * walk_dir should produce reliable results.
 */
static int
__rep_remove_nimdbs(env)
	ENV *env;
{
	FILE_LIST_CTX context;
	int ret;

	if ((ret = __rep_init_file_list_context(env,
	    DB_REPVERSION, 0, 0, &context)) != 0)
		return (ret);

	/* NB: "NULL" asks walk_dir to consider only in-memory DBs */
	if ((ret = __rep_walk_dir(env, NULL, NULL, &context)) != 0)
		goto out;

	if ((ret = __rep_closefiles(env)) != 0)
		goto out;

	ret = __rep_walk_filelist(env, context.version, context.buf,
	    context.size, context.count, __rep_remove_file, NULL);

out:
	__os_free(env, context.buf);
	return (ret);
}

/*
 * Removes all existing logs and databases, at the start of internal init.  But
 * before we do, write a list of the databases onto the init file, so that in
 * case we crash in the middle, we'll know how to resume when we restart.
 * Finally, also write into the init file the UPDATE message from the master (in
 * the "rec" DBT), which includes the (new) list of databases we intend to
 * request copies of (again, so that we know what to do if we crash in the
 * middle).
 *
 * For the sake of simplicity, these database lists are in the form of an UPDATE
 * message (since we already have the mechanisms in place), even though strictly
 * speaking that contains more information than we really need to store.
 *
 * !!! Must be called with the REP_SYSTEM_LOCK held.
 */
static int
__rep_remove_all(env, msg_version, rec)
	ENV *env;
	u_int32_t msg_version;
	DBT *rec;
{
	FILE_LIST_CTX context;
	__rep_update_args u_args;
	DB_FH *fhp;
	DB_REP *db_rep;
#ifdef HAVE_REPLICATION_THREADS
	DBT dbt;
#endif
	REP *rep;
	size_t cnt, updlen;
	u_int32_t bufsz, fvers, mvers, zero;
	int ret, t_ret;
	char *fname;

	fname = NULL;
	fhp = NULL;
#ifdef HAVE_REPLICATION_THREADS
	dbt.data = NULL;
#endif
	db_rep = env->rep_handle;
	rep = db_rep->region;

	/*
	 * 1. Get list of databases currently present at this client, which we
	 *    intend to remove.
	 */

	/* Reserve space for the marshaled update_args. */
	if ((ret = __rep_init_file_list_context(env,
	    DB_REPVERSION, 0, 1, &context)) != 0)
		return (ret);

	if ((ret = __rep_find_dbs(env, &context)) != 0)
		goto out;
	ZERO_LSN(u_args.first_lsn);
	u_args.first_vers = 0;
	u_args.num_files = context.count;
	if ((ret = __rep_update_marshal(env,
	    &u_args, context.buf, __REP_UPDATE_SIZE, &updlen)) != 0)
		goto out;
	DB_ASSERT(env, updlen == __REP_UPDATE_SIZE);

	/*
	 * 2. Before removing anything, safe-store the database list, so that in
	 *    case we crash before we've removed them all, when we restart we
	 *    can clean up what we were doing. Only write database list to
	 *    file if not running in-memory replication.
	 *
	 * As of 4.7 the file has the following form:
	 * 0 (4 bytes - to indicate a new style file)
	 * file version (4 bytes)
	 * data1 version (4 bytes)
	 * data1 size (4 bytes)
	 * data1
	 * data2 version (possibly) (4 bytes)
	 * data2 size (possibly) (4 bytes)
	 * data2 (possibly)
	 *
	 * As of 5.3, repmgr can add group membership information to the end
	 * of the file.
	 */
	if (!FLD_ISSET(rep->config, REP_C_INMEM)) {
		if ((ret = __db_appname(env,
		    DB_APP_META, REP_INITNAME, NULL, &fname)) != 0)
			goto out;
		/* Sanity check that the write size fits into 32 bits. */
		DB_ASSERT(env, (size_t)(context.fillptr - context.buf) ==
		    (u_int32_t)(context.fillptr - context.buf));
		bufsz = (u_int32_t)(context.fillptr - context.buf);

		/*
		 * (Short writes aren't possible, so we don't have to verify
		 * 'cnt'.) This first list is generated internally, so it is
		 * always in the form of the current message version.
		 */
		zero = 0;
		fvers = REP_INITVERSION;
		mvers = DB_REPVERSION;
		if ((ret = __os_open(env, fname, 0,
		    DB_OSO_CREATE | DB_OSO_TRUNC, DB_MODE_600, &fhp)) != 0 ||
		    (ret =
		    __os_write(env, fhp, &zero, sizeof(zero), &cnt)) != 0 ||
		    (ret =
		    __os_write(env, fhp, &fvers, sizeof(fvers), &cnt)) != 0 ||
		    (ret =
		    __os_write(env, fhp, &mvers, sizeof(mvers), &cnt)) != 0 ||
		    (ret =
		    __os_write(env, fhp, &bufsz, sizeof(bufsz), &cnt)) != 0 ||
		    (ret =
		    __os_write(env, fhp, context.buf, bufsz, &cnt)) != 0 ||
		    (ret = __os_fsync(env, fhp)) != 0) {
			__db_err(env, ret, "%s", fname);
			goto out;
		}
	}

	/*
	 * 3. Go ahead and remove logs and databases.  The databases get removed
	 *    according to the list we just finished safe-storing.
	 *
	 * Clearing NIMDBS_LOADED might not really be necessary, since once
	 * we've committed to removing all there's no chance of doing an
	 * abbreviated internal init.  This just keeps us honest.
	 */
	if ((ret = __rep_remove_logs(env)) != 0)
		goto out;
	if ((ret = __rep_closefiles(env)) != 0)
		goto out;
	F_CLR(rep, REP_F_NIMDBS_LOADED);
	if ((ret = __rep_walk_filelist(env, context.version,
	    FIRST_FILE_PTR(context.buf), context.size,
	    context.count, __rep_remove_file, NULL)) != 0)
		goto out;
	/* Remove the blob directory. */
	if ((ret = __blob_del_hierarchy(env)) != 0)
		goto out;

	/*
	 * 4. Safe-store the (new) list of database files we intend to copy from
	 *    the master (again, so that in case we crash before we're finished
	 *    doing so, we'll have enough information to clean up and start over
	 *    again).  This list is the list from the master, so it uses
	 *    the message version. Only write to file if not running
	 *    in-memory replication.
	 */
	if (!FLD_ISSET(rep->config, REP_C_INMEM)) {
		mvers = msg_version;
		if ((ret =
		    __os_write(env, fhp, &mvers, sizeof(mvers), &cnt)) != 0 ||
		    (ret = __os_write(env, fhp,
		    &rec->size, sizeof(rec->size), &cnt)) != 0 ||
		    (ret =
		    __os_write(env, fhp, rec->data, rec->size, &cnt)) != 0 ||
		    (ret = __os_fsync(env, fhp)) != 0) {
			__db_err(env, ret, "%s", fname);
			goto out;
		}
#ifdef HAVE_REPLICATION_THREADS
		/* Invite repmgr to save any info it needs. */
		if ((ret = __repmgr_init_save(env, &dbt)) != 0)
			goto out;
		if (dbt.size > 0 &&
		    ((ret = __os_write(env, fhp,
		    &dbt.size, sizeof(dbt.size), &cnt)) != 0 ||
		    (ret = __os_write(env, fhp,
		    dbt.data, dbt.size, &cnt)) != 0))
			goto out;
#endif
	}

out:
#ifdef HAVE_REPLICATION_THREADS
	if (dbt.data != NULL)
		__os_free(env, dbt.data);
#endif
	if (fhp != NULL && (t_ret = __os_closehandle(env, fhp)) && ret == 0)
		ret = t_ret;
	if (fname != NULL)
		__os_free(env, fname);
	__os_free(env, context.buf);
	return (ret);
}

/*
 * __rep_remove_logs -
 *	Remove our logs to prepare for internal init.
 */
static int
__rep_remove_logs(env)
	ENV *env;
{
	DB_LOG *dblp;
	DB_LSN lsn;
	LOG *lp;
	u_int32_t fnum, lastfile;
	int ret;
	char *name;

	dblp = env->lg_handle;
	lp = dblp->reginfo.primary;
	ret = 0;

	/*
	 * Call memp_sync to flush any pages that might be in the log buffers
	 * and not on disk before we remove files on disk.  If there were no
	 * dirty pages, the log isn't flushed.  Yet the log buffers could still
	 * be dirty: __log_flush should take care of this rare situation.
	 */
	if ((ret = __memp_sync_int(env,
	    NULL, 0, DB_SYNC_CACHE | DB_SYNC_INTERRUPT_OK, NULL, NULL)) != 0)
		return (ret);
	if ((ret = __log_flush(env, NULL)) != 0)
		return (ret);
	/*
	 * Forcibly remove existing log files or reset
	 * the in-memory log space.
	 */
	if (lp->db_log_inmemory) {
		ZERO_LSN(lsn);
		if ((ret = __log_zero(env, &lsn)) != 0)
			return (ret);
	} else {
		lastfile = lp->lsn.file;
		for (fnum = 1; fnum <= lastfile; fnum++) {
			if ((ret = __log_name(dblp, fnum, &name, NULL, 0)) != 0)
				return (ret);
			(void)time(&lp->timestamp);
			(void)__os_unlink(env, name, 0);
			__os_free(env, name);
		}
	}
	return (0);
}

/*
 * Removes a file during internal init.  Assumes underlying subsystems are
 * active; therefore, this can't be used for internal init crash recovery.
 */
static int
__rep_remove_file(env, rfp, unused)
	ENV *env;
	__rep_fileinfo_args *rfp;
	void *unused;
{
	DB *dbp;
#ifdef HAVE_QUEUE
	DB_THREAD_INFO *ip;
#endif
	APPNAME appname;
	db_seq_t blob_fid, blob_sid;
	char *name;
	int ret, t_ret;

	COMPQUIET(unused, NULL);
	dbp = NULL;
	name = rfp->info.data;

	/*
	 * Calling __fop_remove will both purge any matching
	 * fileid from mpool and unlink it on disk.
	 */
#ifdef HAVE_QUEUE
	/*
	 * Handle queue separately.  __fop_remove will not
	 * remove extent files.  Use __qam_remove to remove
	 * extent files that might exist under this name.  Note that
	 * in-memory queue databases can't have extent files.
	 */
	if (rfp->type == (u_int32_t)DB_QUEUE &&
	    !FLD_ISSET(rfp->db_flags, DB_AM_INMEM)) {
		if ((ret = __db_create_internal(&dbp, env, 0)) != 0)
			return (ret);

		/*
		 * At present, qam_remove expects the passed-in dbp to have a
		 * locker allocated, and if not, db_open allocates a locker
		 * which qam_remove then leaks.
		 *
		 * TODO: it would be better to avoid cobbling together this
		 * sequence of low-level operations, if fileops provided some
		 * API to allow us to remove a database without write-locking
		 * its handle.
		 */
		if ((ret = __lock_id(env, NULL, &dbp->locker)) != 0)
			goto out;

		ENV_GET_THREAD_INFO(env, ip);
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "QAM: Unlink %s via __qam_remove", name));
		if ((ret = __qam_remove(dbp, ip, NULL, name, NULL, 0)) != 0) {
			RPRINT(env, (env, DB_VERB_REP_SYNC,
			    "qam_remove returned %d", ret));
			goto out;
		}
	}
#endif
	/*
	 * We call fop_remove even if we've called qam_remove.
	 * That will only have removed extent files.  Now
	 * we need to deal with the actual file itself.
	 */
	appname = __rep_is_internal_rep_file(rfp->info.data) ?
	    DB_APP_META : (IS_BLOB_META(rfp->info.data) ?
	    DB_APP_BLOB : DB_APP_DATA);
	if (FLD_ISSET(rfp->db_flags, DB_AM_INMEM)) {
		if ((ret = __db_create_internal(&dbp, env, 0)) != 0)
			return (ret);
		MAKE_INMEM(dbp);
		F_SET(dbp, DB_AM_RECOVER); /* Skirt locking. */
		ret = __db_inmem_remove(dbp, NULL, name);
	} else if ((ret = __fop_remove(env, NULL, rfp->uid.data, name,
	    (const char **)&rfp->dir.data, appname, 0)) != 0) {
			/*
			 * If fop_remove fails, it could be because
			 * the client has a different data_dir
			 * structure than the master.  Retry with the
			 * local, default settings.
			 */
			ret = __fop_remove(env,
			    NULL, rfp->uid.data, name, NULL, appname, 0);
#ifdef DB_WIN32
			/*
			 * Deleting a blob meta database can result in a
			 * ERROR_PATH_NOT_FOUND error on windows, so treat
			 * that as an ENOENT.
			 */
			if (__os_posix_err(ret) == ENOENT)
				ret = ENOENT;
#endif
	}
	    /* Clean any blob directories. */
	if (ret == 0 && appname == DB_APP_BLOB) {
		/* dbp has not been set, since queues do not support blobs. */
		DB_ASSERT(env, dbp == NULL);
		if ((ret = __db_create_internal(&dbp, env, 0)) != 0)
			goto out;
		if ((ret = __blob_path_to_dir_ids(
		    env, name, &blob_fid, &blob_sid)) != 0)
			goto out;
		/* blob_fid == 0 if it is the top level blob meta db. */
		if (blob_fid != 0) {
			dbp->blob_file_id = blob_fid;
			dbp->blob_sdb_id = blob_sid;
			if ((ret = __blob_del_all(dbp, NULL, 0)) != 0)
				goto out;
		}
	}
out:
	if (dbp != NULL &&
	    (t_ret = __db_close(dbp, NULL, DB_NOSYNC)) != 0 && ret == 0)
		ret = t_ret;
	return (ret);
}

/*
 * __rep_bulk_page
 *	Process a bulk page message.
 *
 * PUBLIC: int __rep_bulk_page __P((ENV *,
 * PUBLIC:     DB_THREAD_INFO *, int, __rep_control_args *, DBT *));
 */
int
__rep_bulk_page(env, ip, eid, rp, rec)
	ENV *env;
	DB_THREAD_INFO *ip;
	int eid;
	__rep_control_args *rp;
	DBT *rec;
{
	__rep_control_args tmprp;
	__rep_bulk_args b_args;
	int ret;
	u_int8_t *p, *ep;

	/*
	 * We're going to be modifying the rp LSN contents so make
	 * our own private copy to play with.  We need to set the
	 * rectype to REP_PAGE because we're calling through __rep_page
	 * to process each page, and lower functions make decisions
	 * based on the rectypes (for throttling/gap processing)
	 */
	memcpy(&tmprp, rp, sizeof(tmprp));
	tmprp.rectype = REP_PAGE;
	ret = 0;
	for (ep = (u_int8_t *)rec->data + rec->size, p = (u_int8_t *)rec->data;
	    p < ep;) {
		/*
		 * First thing in the buffer is the length.  Then the LSN
		 * of this page, then the page info itself.
		 */
		if ((ret = __rep_bulk_unmarshal(env,
		    &b_args, p, rec->size, &p)) != 0)
			return (ret);
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "rep_bulk_page: Processing LSN [%lu][%lu]",
		    (u_long)tmprp.lsn.file, (u_long)tmprp.lsn.offset));
		VPRINT(env, (env, DB_VERB_REP_SYNC,
    "rep_bulk_page: p %#lx ep %#lx pgrec data %#lx, size %lu (%#lx)",
		    P_TO_ULONG(p), P_TO_ULONG(ep),
		    P_TO_ULONG(b_args.bulkdata.data),
		    (u_long)b_args.bulkdata.size,
		    (u_long)b_args.bulkdata.size));
		/*
		 * Now send the page info DBT to the page processing function.
		 */
		ret = __rep_page(env, ip, eid, &tmprp, &b_args.bulkdata);
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "rep_bulk_page: rep_page ret %d", ret));

		/*
		 * If this set of pages is already done just return.
		 */
		if (ret != 0) {
			if (ret == DB_REP_PAGEDONE)
				ret = 0;
			break;
		}
	}
	return (ret);
}

/*
 * __rep_page
 *	Process a page message.  This processes any page related
 * message: REP_PAGE, REP_PAGE_FAIL and REP_PAGE_MORE.
 *
 * PUBLIC: int __rep_page __P((ENV *,
 * PUBLIC:     DB_THREAD_INFO *, int, __rep_control_args *, DBT *));
 */
int
__rep_page(env, ip, eid, rp, rec)
	ENV *env;
	DB_THREAD_INFO *ip;
	int eid;
	__rep_control_args *rp;
	DBT *rec;
{

	DB_REP *db_rep;
	DBT data, key;
	REP *rep;
	__rep_fileinfo_args *msgfp, msgf;
	__rep_fileinfo_v6_args *msgfpv6;
	__rep_fileinfo_v7_args *msgfpv7;
	db_recno_t recno;
	int ret;
	char *msg;
	void *msgfree;

	ret = 0;
	db_rep = env->rep_handle;
	rep = db_rep->region;

	if (rep->sync_state != SYNC_PAGE)
		return (DB_REP_PAGEDONE);

	if (rp->rectype == REP_PAGE_FAIL)
		msg = "PAGE_FAIL";
	else if (rp->rectype == REP_PAGE_MORE)
		msg = "PAGE_MORE";
	else
		msg = "PAGE";
	/*
	 * If we restarted internal init, it is possible to receive
	 * an old REP_PAGE message, while we're in the current
	 * stage of recovering pages.  Until we have some sort of
	 * an init generation number, ignore any message that has
	 * a message LSN that is before this internal init's first_lsn.
	 */
	if (LOG_COMPARE(&rp->lsn, &rep->first_lsn) < 0) {
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "%s: Old page: msg LSN [%lu][%lu] first_lsn [%lu][%lu]",
		    msg, (u_long)rp->lsn.file, (u_long)rp->lsn.offset,
		    (u_long)rep->first_lsn.file,
		    (u_long)rep->first_lsn.offset));
		return (DB_REP_PAGEDONE);
	}
	/*
	 * Build a current struct by copying in the older
	 * version struct and then setting up the new fields.
	 * This is safe because all old fields are in the
	 * same location in the current struct.
	 */
	if (rp->rep_version < DB_REPVERSION_53) {
		if ((ret = __rep_fileinfo_v6_unmarshal(env,
		    &msgfpv6, rec->data, rec->size, NULL)) != 0)
			return (ret);
		memcpy(&msgf, msgfpv6, sizeof(__rep_fileinfo_v6_args));
		msgf.dir.data = NULL;
		msgf.dir.size = 0;
		msgf.blob_fid_lo = msgf.blob_fid_hi = 0;
		msgfp = &msgf;
		msgfree = msgfpv6;
	} else if (rp->rep_version < DB_REPVERSION_61) {
		if ((ret = __rep_fileinfo_v7_unmarshal(env,
		    &msgfpv7, rec->data, rec->size, NULL)) != 0)
			return (ret);
		memcpy(&msgf, msgfpv7, sizeof(__rep_fileinfo_v7_args));
		msgf.blob_fid_lo = msgf.blob_fid_hi = 0;
		msgfp = &msgf;
		msgfree = msgfpv7;
	} else {
		if ((ret = __rep_fileinfo_unmarshal(env,
		    &msgfp, rec->data, rec->size, NULL)) != 0)
			return (ret);
		msgfree = msgfp;
	}
	MUTEX_LOCK(env, rep->mtx_clientdb);
	REP_SYSTEM_LOCK(env);
	/*
	 * Check if the world changed or if we are in the blob sync phase.
	 */
	if (rep->sync_state != SYNC_PAGE || rep->blob_sync != 0) {
		ret = DB_REP_PAGEDONE;
		goto err;
	}
	/*
	 * We should not ever be in internal init with a lease granted.
	 */
	DB_ASSERT(env,
	    !IS_USING_LEASES(env) || __rep_islease_granted(env) == 0);

	VPRINT(env, (env, DB_VERB_REP_SYNC,
	    "%s: Received page %lu from file %d",
	    msg, (u_long)msgfp->pgno, msgfp->filenum));
	/*
	 * Check if this page is from the file we're expecting.
	 * This may be an old or delayed page message.
	 */
	/*
	 * !!!
	 * If we allow dbrename/dbremove on the master while a client
	 * is updating, then we'd have to verify the file's uid here too.
	 */
	if (msgfp->filenum != rep->curfile) {
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Msg file %d != curfile %d",
		    msgfp->filenum, rep->curfile));
		ret = DB_REP_PAGEDONE;
		goto err;
	}
	/*
	 * We want to create/open our dbp to the database
	 * where we'll keep our page information.
	 */
	if ((ret = __rep_client_dbinit(env, 1, REP_PG)) != 0) {
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "%s: Client_dbinit %s", msg, db_strerror(ret)));
		goto err;
	}

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	recno = (db_recno_t)(msgfp->pgno + 1);
	key.data = &recno;
	key.ulen = key.size = sizeof(db_recno_t);
	key.flags = DB_DBT_USERMEM;

	/*
	 * If we already have this page, then we don't want to bother
	 * rewriting it into the file.  Otherwise, any other error
	 * we want to return.
	 */
	ret = __db_put(db_rep->file_dbp, ip, NULL, &key, &data, DB_NOOVERWRITE);
	if (ret == DB_KEYEXIST) {
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "%s: Received duplicate page %lu from file %d",
		    msg, (u_long)msgfp->pgno, msgfp->filenum));
		STAT(rep->stat.st_pg_duplicated++);
		PERFMON4(env, rep, pg_duplicated, eid,
		    msgfp->pgno, msgfp->filenum, rep->stat.st_pg_duplicated);
		ret = 0;
		goto err;
	}
	if (ret != 0)
		goto err;

	/*
	 * We put the page in the database file itself.
	 */
	if (rp->rectype != REP_PAGE_FAIL) {
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "%s: Write page %lu into mpool", msg, (u_long)msgfp->pgno));
		if ((ret = __rep_write_page(env, ip, rep, msgfp)) != 0) {
			/*
			 * We got an error storing the page, therefore, we need
			 * remove this page marker from the page database too.
			 * !!!
			 * I'm ignoring errors from the delete because we want
			 * to return the original error.  If we cannot write the
			 * page and we cannot delete the item we just put,
			 * what should we do?  Panic the env and return
			 * DB_RUNRECOVERY?
			 */
			(void)__db_del(db_rep->file_dbp, NULL, NULL, &key, 0);
			goto err;
		}
	}
	STAT_INC(env, rep, pg_record, rep->stat.st_pg_records, eid);
	rep->npages++;

	/*
	 * Now check the LSN on the page and save it if it is later
	 * than the one we have.
	 */
	if (LOG_COMPARE(&rp->lsn, &rep->last_lsn) > 0)
		rep->last_lsn = rp->lsn;

	/*
	 * We've successfully written the page.  Now we need to see if
	 * we're done with this file.  __rep_filedone will check if we
	 * have all the pages expected and if so, set up for the next
	 * file and send out a page request for the next file's pages.
	 */
	ret = __rep_filedone(env, ip, eid, rep, msgfp, rp->rectype);

err:	REP_SYSTEM_UNLOCK(env);
	MUTEX_UNLOCK(env, rep->mtx_clientdb);

	__os_free(env, msgfree);
	return (ret);
}

/*
 * __rep_blob_chunk
 *	Process a blob chunk message.  When a blob chunk arrives, delete its
 *	entry in the blob chunk gap database to show that it has arrived, and
 *	write the data to the blob file.
 *
 * PUBLIC: int __rep_blob_chunk __P((ENV *, int, DB_THREAD_INFO *, DBT *));
 */
int
__rep_blob_chunk(env, eid, ip, rec)
	ENV *env;
	int eid;
	DB_THREAD_INFO *ip;
	DBT *rec;
{
	DB_REP *db_rep;
	DBC *dbc;
	DB_FH *fhp;
	DBT data, key;
	REP *rep;
	REGINFO *infop;
	__rep_blob_chunk_args rbc;
	__rep_fileinfo_args *rfp;
	db_seq_t blob_fid;
	char *blob_sub_dir, *last, *mkpath, *name, *path;
	int ret;
	off_t offset;
	u_int8_t keybuf[BLOB_KEY_SIZE], *ptr;

	ret = 0;
	db_rep = env->rep_handle;
	rep = db_rep->region;
	infop = env->reginfo;
	dbc = NULL;
	blob_sub_dir = name = NULL;
	path = NULL;
	fhp = NULL;

	if (rep->sync_state != SYNC_PAGE)
		return (DB_REP_PAGEDONE);

	if ((ret = __rep_blob_chunk_unmarshal(
	    env, &rbc, rec->data, rec->size, &ptr)) != 0)
		return (ret);

	MUTEX_LOCK(env, rep->mtx_clientdb);
	REP_SYSTEM_LOCK(env);
	/*
	 * Check if the world changed.
	 */
	if (rep->sync_state != SYNC_PAGE) {
		ret = DB_REP_PAGEDONE;
		goto err;
	}
	/*
	 * We should not ever be in internal init with a lease granted.
	 */
	DB_ASSERT(env,
	    !IS_USING_LEASES(env) || __rep_islease_granted(env) == 0);

	/* Make sure this is for the current file. */
	GET_CURINFO(rep, infop, rfp);
	GET_LO_HI(env, rfp->blob_fid_lo, rfp->blob_fid_hi, blob_fid, ret);
	if (ret != 0)
		goto err;

	if (blob_fid != (db_seq_t)rbc.blob_fid) {
		ret = DB_REP_PAGEDONE;
		goto err;
	}

	RPRINT(env, (env, DB_VERB_REP_SYNC,
"REP_BLOB_CHUNK: blob_fid %llu, blob_sid %llu, blob_id %llu, offset %llu",
	    (unsigned long long)rbc.blob_fid,
	    (unsigned long long)rbc.blob_sid,
	    (unsigned long long)rbc.blob_id, (long long)rbc.offset));

	if (db_rep->blob_dbp == NULL &&
	    (ret = __rep_client_dbinit(env, 0, REP_BLOB)) != 0) {
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "REP_BLOB_CHUNK: Client_dbinit %s",
		    db_strerror(ret)));
		goto err;
	}

	/* Set the highest blob chunk received. */
	if (rbc.blob_sid > (u_int64_t)rep->gap_bl_hi_sid ||
	    (rbc.blob_sid == (u_int64_t)rep->gap_bl_hi_sid &&
	    rbc.blob_id > (u_int64_t)rep->gap_bl_hi_id) ||
	    (rbc.blob_sid == (u_int64_t)rep->gap_bl_hi_sid &&
	    rbc.blob_id == (u_int64_t)rep->gap_bl_hi_id &&
	    rbc.offset > (u_int64_t)rep->gap_bl_hi_off)) {
		rep->gap_bl_hi_id = (db_seq_t)rbc.blob_id;
		rep->gap_bl_hi_sid = (db_seq_t)rbc.blob_sid;
		rep->gap_bl_hi_off = (off_t)rbc.offset;
	}

	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));
	data.flags = key.flags = DB_DBT_USERMEM;
	key.data = keybuf;
	key.ulen = key.size = BLOB_KEY_SIZE;
	data.data = (void *)&offset;
	data.ulen = data.size = sizeof(offset);
	/* BLOB_DELETE is set if the blob file was deleted. */
	if (F_ISSET(&rbc, BLOB_DELETE)) {
		memcpy(keybuf, &rbc.blob_sid, BLOB_ID_SIZE);
		memcpy(&(keybuf[BLOB_ID_SIZE]), &rbc.blob_id, BLOB_ID_SIZE);
		if ((ret = __db_del(
		    db_rep->blob_dbp, ip, NULL, &key, 0)) != 0) {
			if (ret == DB_NOTFOUND)
				ret = 0;
			goto err;
		}
#ifdef	CONFIG_TEST
		STAT_INC(env, rep, ext_deleted, rep->stat.st_ext_deleted, eid);
#endif
		goto done;
	}

	if ((ret = __db_cursor(db_rep->blob_dbp, ip, NULL, &dbc, 0)) != 0)
		goto err;
	offset = (off_t)rbc.offset;
	memcpy(keybuf, &rbc.blob_sid, BLOB_ID_SIZE);
	memcpy(&(keybuf[BLOB_ID_SIZE]), &rbc.blob_id, BLOB_ID_SIZE);
	/* If not found we have already dealt with this chunk. */
	if ((ret = __dbc_get(dbc, &key, &data, DB_GET_BOTH)) != 0) {
		if (ret == DB_NOTFOUND) {
			STAT_INC(env, rep,
			    ext_duplicated, rep->stat.st_ext_duplicated, eid);
			ret = 0;
			goto done;
		}
		goto err;
	}
	/*
	 * BLOB_CHUNK_FAIL is set if the blob file was truncated to shorter
	 * than the BLOB_CHUNK offset.
	 */
	if (F_ISSET(&rbc, BLOB_CHUNK_FAIL)) {
		while (ret == 0) {
			if ((ret = __dbc_del(dbc, 0)) != 0)
				goto err;
			ret = __dbc_get(dbc, &key, &data, DB_NEXT_DUP);
		}
		if (ret == DB_NOTFOUND)
			ret = 0;
		if ((ret = __dbc_close(dbc)) != 0)
			goto err;
#ifdef	CONFIG_TEST
		STAT_INC(env, rep, ext_truncated,
		    db_rep->region->stat.st_ext_truncated, eid);
#endif
		dbc = NULL;
		goto done;
	}
	if ((ret = __dbc_del(dbc, 0)) != 0)
		goto err;
	if ((ret = __dbc_close(dbc)) != 0)
		goto err;
	dbc = NULL;

	if ((ret = __blob_make_sub_dir(env, &blob_sub_dir,
	    (db_seq_t)rbc.blob_fid, (db_seq_t)rbc.blob_sid)) != 0)
		goto err;

	if ((ret = __blob_id_to_path(
	    env, blob_sub_dir, (db_seq_t)rbc.blob_id, &name, 1)) != 0)
		goto err;

	if ((ret = __db_appname(env, DB_APP_BLOB, name, NULL, &path)) != 0 )
		goto err;

	last = __db_rpath(path);
	DB_ASSERT(env, last != NULL);
	*last = '\0';
	if (__os_exists(env, path, NULL) != 0) {
		*last = PATH_SEPARATOR[0];
		mkpath = path;
#ifdef DB_WIN32
		/*
		 * Absolute paths on windows can result in it creating a "C"
		 *  or "D" directory in the working directory.
		 */
		if (__os_abspath(mkpath))
			mkpath += 2;
#endif
		if ((ret = __db_mkpath(env, mkpath)) != 0)
			goto err;
	}
	*last = PATH_SEPARATOR[0];
	if ((ret = __os_open(
	    env, path, 0, DB_OSO_CREATE, env->db_mode, &fhp)) != 0)
		goto err;

	/* Write the data into the blob file. */
	if ((ret = __fop_write_file(env, NULL, name, NULL, DB_APP_BLOB,
	    fhp, (off_t)rbc.offset, rbc.data.data, rbc.data.size, 0)) != 0)
		goto err;
	if ((ret = __os_closehandle(env, fhp)) != 0)
		goto err;
	fhp = NULL;
	STAT_INC(env, rep, ext_records, rep->stat.st_ext_records, eid);

done:	ret = __rep_blobdone(env, eid, ip, rep, blob_fid, 0, 0);

err:	REP_SYSTEM_UNLOCK(env);
	MUTEX_UNLOCK(env, rep->mtx_clientdb);
	if (path != NULL)
		__os_free(env, path);
	if (blob_sub_dir != NULL)
		__os_free(env, blob_sub_dir);
	if (name != NULL)
		__os_free(env, name);
	if (fhp != NULL)
		(void)__os_closehandle(env, fhp);
	if (dbc != NULL)
		(void)__dbc_close(dbc);

	return (ret);
}

/*
 * __rep_write_page -
 *	Write this page into a database.
 */
static int
__rep_write_page(env, ip, rep, msgfp)
	ENV *env;
	DB_THREAD_INFO *ip;
	REP *rep;
	__rep_fileinfo_args *msgfp;
{
	DB db;
	DBT pgcookie;
	DB_MPOOLFILE *mpf;
	DB_PGINFO *pginfo;
	DB_REP *db_rep;
	REGINFO *infop;
	APPNAME appname;
	__rep_fileinfo_args *rfp;
	char *blob_path;
	int ret;
	void *dst;

	db_rep = env->rep_handle;
	infop = env->reginfo;
	rfp = NULL;
	blob_path = NULL;

	/*
	 * If this is the first page we're putting in this database, we need
	 * to create the mpool file.  Otherwise call memp_fget to create the
	 * page in mpool.  Then copy the data to the page, and memp_fput the
	 * page to give it back to mpool.
	 *
	 * We need to create the file, removing any existing file and associate
	 * the correct file ID with the new one.
	 */
	GET_CURINFO(rep, infop, rfp);
	if (db_rep->file_mpf == NULL) {
		if (!FLD_ISSET(rfp->db_flags, DB_AM_INMEM)) {
			/*
			 * Recreate the file on disk.  We'll be putting
			 * the data into the file via mpool.  System
			 * databases should go into the environment
			 * directory, not the data directory.
			 */
			RPRINT(env, (env, DB_VERB_REP_SYNC,
			    "rep_write_page: Calling fop_create for %s",
			    (char *)rfp->info.data));
			appname = (__rep_is_internal_rep_file(rfp->info.data) ?
			    DB_APP_META : (IS_BLOB_META((char *)rfp->info.data)
			    ? DB_APP_BLOB : DB_APP_DATA));
			/*
			 * May have to create the directory structure for blob
			 * metadata databases.
			 */
			if (appname == DB_APP_BLOB) {
				if ((ret = __db_appname(env,
				     appname, rfp->info.data,
				    (const char **)&rfp->dir.data,
				    &blob_path)) != 0)
					goto err;

				if ((ret = __db_mkpath(env, blob_path)) != 0)
					goto err;
			}
			if ((ret = __fop_create(env, NULL, NULL,
			    rfp->info.data, (const char **)&rfp->dir.data,
			    appname, env->db_mode, 0)) != 0) {
				/*
				 * If fop_create fails, it could be because
				 * the client has a different data_dir
				 * structure than the master.  Retry with the
				 * local, default settings.
				 */
				RPRINT(env, (env, DB_VERB_REP_SYNC,
    "rep_write_page: fop_create ret %d.  Retry for %s, master datadir %s",
				    ret, (char *)rfp->info.data,
				    rfp->dir.data == NULL ? "NULL" :
				    (char *)rfp->dir.data));
				if ((ret = __fop_create(env, NULL, NULL,
				    rfp->info.data, NULL,
				    __rep_is_internal_rep_file(rfp->info.data) ?
				    DB_APP_META : DB_APP_DATA,
				    env->db_mode, 0)) != 0)
					goto err;
			}
		}

		if ((ret =
		    __rep_mpf_open(env, &db_rep->file_mpf, rfp,
		    FLD_ISSET(rfp->db_flags, DB_AM_INMEM) ?
		    DB_CREATE : 0)) != 0)
			goto err;
	}
	/*
	 * Handle queue specially.  If we're a QUEUE database, we need to
	 * use the __qam_fget/put calls.  We need to use db_rep->queue_dbc for
	 * that.  That dbp is opened after getting the metapage for the
	 * queue database.  Since the meta-page is always in the queue file,
	 * we'll use the normal path for that first page.  After that we
	 * can assume the dbp is opened.
	 */
	if (msgfp->type == (u_int32_t)DB_QUEUE && msgfp->pgno != 0) {
#ifdef HAVE_QUEUE
		ret = __qam_fget(db_rep->queue_dbc, &msgfp->pgno,
		    DB_MPOOL_CREATE | DB_MPOOL_DIRTY, &dst);
#else
		/*
		 * This always returns an error.
		 */
		ret = __db_no_queue_am(env);
#endif
	} else
		ret = __memp_fget(db_rep->file_mpf, &msgfp->pgno, ip, NULL,
		    DB_MPOOL_CREATE | DB_MPOOL_DIRTY, &dst);

	if (ret != 0)
		goto err;

	/*
	 * Before writing this page into our local mpool, see if its byte order
	 * needs to be swapped.  When in mpool the page should be in the native
	 * byte order of our local environment.  But the page image we've
	 * received may be in the opposite order (as indicated in finfo_flags).
	 */
	if ((F_ISSET(env, ENV_LITTLEENDIAN) &&
	    !FLD_ISSET(msgfp->finfo_flags, REPINFO_PG_LITTLEENDIAN)) ||
	    (!F_ISSET(env, ENV_LITTLEENDIAN) &&
	    FLD_ISSET(msgfp->finfo_flags, REPINFO_PG_LITTLEENDIAN))) {
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "write_page: Page %d needs to be swapped", msgfp->pgno));
		/*
		 * Set up a dbp to pass into the swap functions.  We need
		 * only a few things:  The environment and any special
		 * dbp flags and some obvious basics like db type and
		 * pagesize.  Those flags were set back in rep_mpf_open
		 * and are available in the pgcookie set up with the
		 * mpoolfile associated with this database.
		 */
		memset(&db, 0, sizeof(db));
		db.env = env;
		db.type = (DBTYPE)msgfp->type;
		db.pgsize = msgfp->pgsize;
		mpf = db_rep->file_mpf;
		if ((ret = __memp_get_pgcookie(mpf, &pgcookie)) != 0)
			goto err;
		pginfo = (DB_PGINFO *)pgcookie.data;
		db.flags = pginfo->flags;
		if ((ret = __db_pageswap(env,
		     &db, msgfp->info.data, msgfp->pgsize, NULL, 1)) != 0)
			goto err;
	}

	memcpy(dst, msgfp->info.data, msgfp->pgsize);
#ifdef HAVE_QUEUE
	if (msgfp->type == (u_int32_t)DB_QUEUE && msgfp->pgno != 0)
		ret = __qam_fput(db_rep->queue_dbc,
		     msgfp->pgno, dst, db_rep->queue_dbc->priority);
	else
#endif
		ret = __memp_fput(db_rep->file_mpf,
		    ip, dst, db_rep->file_dbp->priority);

err:	if (blob_path != NULL)
		__os_free(env, blob_path);

	return (ret);
}

/*
 * __rep_page_gap -
 *	After we've put the page into the database, we need to check if
 *	we have a page gap and whether we need to request pages.
 */
static int
__rep_page_gap(env, rep, msgfp, type)
	ENV *env;
	REP *rep;
	__rep_fileinfo_args *msgfp;
	u_int32_t type;
{
	DBC *dbc;
	DBT data, key;
	DB_LOG *dblp;
	DB_REP *db_rep;
	DB_THREAD_INFO *ip;
	LOG *lp;
	REGINFO *infop;
	__rep_fileinfo_args *rfp;
	db_recno_t recno;
	int ret, t_ret;

	db_rep = env->rep_handle;
	dblp = env->lg_handle;
	lp = dblp->reginfo.primary;
	infop = env->reginfo;
	ret = 0;
	dbc = NULL;

	/*
	 * We've successfully put this page into our file.
	 * Now we need to account for it and re-request new pages
	 * if necessary.
	 */
	/*
	 * We already hold both the db mutex and rep mutex.
	 */
	GET_CURINFO(rep, infop, rfp);

	/*
	 * Make sure we're still talking about the same file.
	 * If not, we're done here.
	 */
	if (rfp->filenum != msgfp->filenum || rep->blob_sync != 0) {
		ret = DB_REP_PAGEDONE;
		goto err;
	}

	/*
	 * We have 3 possible states:
	 * 1.  We receive a page we already have accounted for.
	 *	msg pgno < ready pgno
	 * 2.  We receive a page that is beyond a gap.
	 *	msg pgno > ready pgno
	 * 3.  We receive the page we're expecting next.
	 *	msg pgno == ready pgno
	 */
	/*
	 * State 1.  This can happen once we put our page record into the
	 * database, but by the time we acquire the mutex other
	 * threads have already accounted for this page and moved on.
	 * We just want to return.
	 */
	if (msgfp->pgno < rep->ready_pg) {
		VPRINT(env, (env, DB_VERB_REP_SYNC,
		    "PAGE_GAP: pgno %lu < ready %lu, waiting %lu",
		    (u_long)msgfp->pgno, (u_long)rep->ready_pg,
		    (u_long)rep->waiting_pg));
		goto err;
	}

	/*
	 * State 2.  This page is beyond the page we're expecting.
	 * We need to update waiting_pg if this page is less than
	 * (earlier) the current waiting_pg.  There is nothing
	 * to do but see if we need to request.
	 */
	VPRINT(env, (env, DB_VERB_REP_SYNC,
    "PAGE_GAP: pgno %lu, max_pg %lu ready %lu, waiting %lu max_wait %lu",
	    (u_long)msgfp->pgno, (u_long)rfp->max_pgno, (u_long)rep->ready_pg,
	    (u_long)rep->waiting_pg, (u_long)rep->max_wait_pg));
	if (msgfp->pgno > rep->ready_pg) {
		/*
		 * We receive a page larger than the one we're expecting.
		 */
		__os_gettime(env, &rep->last_pg_ts, 1);
		if (rep->waiting_pg == PGNO_INVALID ||
		    msgfp->pgno < rep->waiting_pg)
			rep->waiting_pg = msgfp->pgno;
	} else {
		/*
		 * We received the page we're expecting.
		 */
		rep->ready_pg++;
		__os_gettime(env, &lp->rcvd_ts, 1);
		if (rep->ready_pg == rep->waiting_pg) {
			/*
			 * If we get here we know we just filled a gap.
			 * Move the cursor to that place and then walk
			 * forward looking for the next gap, if it exists.
			 * Similar to log gaps, if we fill a gap we want to
			 * request the next gap right away if it has been
			 * a while since we last received a later page.
			 */
			lp->rcvd_ts = rep->last_pg_ts;
			lp->wait_ts = rep->request_gap;
			rep->max_wait_pg = PGNO_INVALID;
			/*
			 * We need to walk the recno database looking for the
			 * next page we need or expect.
			 */
			memset(&key, 0, sizeof(key));
			memset(&data, 0, sizeof(data));
			ENV_GET_THREAD_INFO(env, ip);
			if ((ret = __db_cursor(db_rep->file_dbp, ip, NULL,
			    &dbc, 0)) != 0)
				goto err;
			/*
			 * Set cursor to the first waiting page.
			 * Page numbers/record numbers are offset by 1.
			 */
			recno = (db_recno_t)rep->waiting_pg + 1;
			key.data = &recno;
			key.ulen = key.size = sizeof(db_recno_t);
			key.flags = DB_DBT_USERMEM;
			/*
			 * We know that page is there, this should
			 * find the record.
			 */
			ret = __dbc_get(dbc, &key, &data, DB_SET);
			if (ret != 0)
				goto err;
			VPRINT(env, (env, DB_VERB_REP_SYNC,
			    "PAGE_GAP: Set cursor for ready %lu, waiting %lu",
			    (u_long)rep->ready_pg, (u_long)rep->waiting_pg));
		}
		while (ret == 0 && rep->ready_pg == rep->waiting_pg) {
			rep->ready_pg++;
			ret = __dbc_get(dbc, &key, &data, DB_NEXT);
			/*
			 * If we get to the end of the list, there are no
			 * more gaps.  Reset waiting_pg.
			 */
			if (ret == DB_NOTFOUND || ret == DB_KEYEMPTY) {
				rep->waiting_pg = PGNO_INVALID;
				VPRINT(env, (env, DB_VERB_REP_SYNC,
	    "PAGE_GAP: Next cursor No next - ready %lu, waiting %lu",
				    (u_long)rep->ready_pg,
				    (u_long)rep->waiting_pg));
				break;
			}
			/*
			 * Subtract 1 from waiting_pg because record numbers
			 * are 1-based and pages are 0-based and we added 1
			 * into the page number when we put it into the db.
			 */
			rep->waiting_pg = *(db_pgno_t *)key.data;
			rep->waiting_pg--;
			VPRINT(env, (env, DB_VERB_REP_SYNC,
	    "PAGE_GAP: Next cursor ready %lu, waiting %lu",
			    (u_long)rep->ready_pg, (u_long)rep->waiting_pg));
		}
	}

	/*
	 * If we filled a gap and now have the entire file, there's
	 * nothing to do.  We're done when ready_pg is > max_pgno
	 * because ready_pg is larger than the last page we received.
	 */
	if (rep->ready_pg > rfp->max_pgno)
		goto err;

	/*
	 * Check if we need to ask for more pages.
	 */
	if ((rep->waiting_pg != PGNO_INVALID &&
	    rep->ready_pg != rep->waiting_pg) || type == REP_PAGE_MORE) {
		/*
		 * We got a page but we may still be waiting for more.
		 * If we got REP_PAGE_MORE we always want to ask for more.
		 * We need to set rfp->pgno to the current page number
		 * we will use to ask for more pages.
		 */
		if (type == REP_PAGE_MORE)
			rfp->pgno = msgfp->pgno;
		if ((__rep_check_doreq(env, rep) || type == REP_PAGE_MORE) &&
		    ((ret = __rep_pggap_req(env, rep, rfp,
		    (type == REP_PAGE_MORE) ? REP_GAP_FORCE : 0)) != 0))
			goto err;
	} else {
		lp->wait_ts = rep->request_gap;
		rep->max_wait_pg = PGNO_INVALID;
	}

err:
	if (dbc != NULL && (t_ret = __dbc_close(dbc)) != 0 && ret == 0)
		ret = t_ret;

	return (ret);
}

/*
 * __rep_blob_cleanup -
 *	Clean up blob internal init information.
 *
 *	Caller must hold client database mutex (mtx_clientdb) and
 *	REP_SYSTEM_LOCK.
 */
static int
__rep_blob_cleanup(env, rep)
	ENV *env;
	REP *rep;
{
	DB_REP *db_rep;
	DB_THREAD_INFO *ip;
	int ret, t_ret;
	u_int32_t count;

	ret = 0;
	db_rep = env->rep_handle;

	/*
	 * Delete any remaining records in the blob chunk database.  The blob
	 * chunk database contains descriptions of the blob chunks that have
	 * yet to arrive.  If not deleted, the remaining records could
	 * interfere with how the next REP_BLOB_UPDATE message is handled.
	 */
	if (db_rep->blob_dbp != NULL) {
		ENV_GET_THREAD_INFO(env, ip);
		ret = __db_truncate(db_rep->blob_dbp, ip, NULL, &count);
		t_ret = __db_close(db_rep->blob_dbp, NULL, DB_NOSYNC);
		if (ret == 0)
			ret = t_ret;
		db_rep->blob_dbp = NULL;
	}
	/* Reset blob internal init control values. */
	rep->gap_bl_hi_id = rep->gap_bl_hi_sid = 0;
	rep->last_blob_id = rep->last_blob_sid = 0;
	rep->prev_blob_id = rep->prev_blob_sid = 0;
	rep->gap_bl_hi_off = 0;
	rep->blob_more_files = 0;
	rep->blob_sync = 0;
	rep->highest_id = 0;

	return (ret);
}

/*
 * __rep_init_cleanup -
 *	Clean up internal initialization pieces.
 *
 * !!!
 * Caller must hold client database mutex (mtx_clientdb) and REP_SYSTEM_LOCK.
 *
 * PUBLIC: int __rep_init_cleanup __P((ENV *, REP *, int));
 */
int
__rep_init_cleanup(env, rep, force)
	ENV *env;
	REP *rep;
	int force;
{
	DB *queue_dbp;
	DB_REP *db_rep;
	REGENV *renv;
	REGINFO *infop;
	int ret, t_ret;

	db_rep = env->rep_handle;
	infop = env->reginfo;
	renv = infop->primary;
	ret = 0;
	/*
	 * 1.  Close up the file data pointer we used.
	 * 2.  Close/reset the page database.
	 * 3.  Close/truncate the blob chunk gap database.
	 * 4.  Close/reset the queue database if we're forcing a cleanup.
	 * 5.  Free current file info.
	 * 6.  If we have all files or need to force, free original file info.
	 */
	if (db_rep->file_mpf != NULL) {
		ret = __memp_fclose(db_rep->file_mpf, 0);
		db_rep->file_mpf = NULL;
	}
	if (db_rep->file_dbp != NULL) {
		t_ret = __db_close(db_rep->file_dbp, NULL, DB_NOSYNC);
		db_rep->file_dbp = NULL;
		if (ret == 0)
			ret = t_ret;
	}
	/*
	 * Truncate the blob chunk gap database, since entries in the database
	 * are for blob chunks we are expecting to arrive.  Also reset blob
	 * internal init control values.
	 */
	t_ret = __rep_blob_cleanup(env, rep);
	if (ret == 0)
		ret = t_ret;

	if (force && db_rep->queue_dbc != NULL) {
		queue_dbp = db_rep->queue_dbc->dbp;
		if ((t_ret = __dbc_close(db_rep->queue_dbc)) != 0 && ret == 0)
			ret = t_ret;
		db_rep->queue_dbc = NULL;
		if ((t_ret = __db_close(queue_dbp, NULL, DB_NOSYNC)) != 0 &&
		    ret == 0)
			ret = t_ret;
	}
	if (rep->curinfo_off != INVALID_ROFF) {
		MUTEX_LOCK(env, renv->mtx_regenv);
		__env_alloc_free(infop, R_ADDR(infop, rep->curinfo_off));
		MUTEX_UNLOCK(env, renv->mtx_regenv);
		rep->curinfo_off = INVALID_ROFF;
	}
	if (IN_INTERNAL_INIT(rep) && force) {
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "clean up interrupted internal init"));
		t_ret = F_ISSET(rep, REP_F_ABBREVIATED) ?
		    __rep_walk_filelist(env, rep->infoversion,
			R_ADDR(infop, rep->originfo_off), rep->originfolen,
			rep->nfiles, __rep_cleanup_nimdbs, NULL) :
		    __rep_clean_interrupted(env);
		if (ret == 0)
			ret = t_ret;

		if (rep->originfo_off != INVALID_ROFF) {
			MUTEX_LOCK(env, renv->mtx_regenv);
			__env_alloc_free(infop,
			    R_ADDR(infop, rep->originfo_off));
			MUTEX_UNLOCK(env, renv->mtx_regenv);
			rep->originfo_off = INVALID_ROFF;
		}
	}

	return (ret);
}

/*
 * Remove NIMDBs that may have been fully or partially loaded during an
 * abbreviated internal init, when the init gets interrupted.  At this point,
 * we know that any databases we have processed are listed in originfo.
 */
static int
__rep_cleanup_nimdbs(env, rfp, unused)
	ENV *env;
	__rep_fileinfo_args *rfp;
	void *unused;
{
	DB *dbp;
	char *namep;
	int ret, t_ret;

	COMPQUIET(unused, NULL);

	ret = 0;
	dbp = NULL;

	if (FLD_ISSET(rfp->db_flags, DB_AM_INMEM)) {
		namep = rfp->info.data;

		if ((ret = __db_create_internal(&dbp, env, 0)) != 0)
			goto out;
		MAKE_INMEM(dbp);
		F_SET(dbp, DB_AM_RECOVER); /* Skirt locking. */

		/*
		 * Some of these "files" (actually NIMDBs) may not exist
		 * yet, simply because the interrupted abbreviated
		 * internal init had not yet progressed far enough to
		 * retrieve them.  So ENOENT is an acceptable outcome.
		 */
		if ((ret = __db_inmem_remove(dbp, NULL, namep)) == ENOENT)
			ret = 0;
		if ((t_ret = __db_close(dbp, NULL, DB_NOSYNC)) != 0 && ret == 0)
			ret = t_ret;
	}

out:
	return (ret);
}

/*
 * Clean up files involved in an interrupted internal init.
 */
static int
__rep_clean_interrupted(env)
	ENV *env;
{
	REP *rep;
	DB_LOG *dblp;
	LOG *lp;
	REGINFO *infop;
	int ret, t_ret;

	rep = env->rep_handle->region;
	infop = env->reginfo;

	/*
	 * 1. logs
	 *   a) remove old log files
	 *   b) set up initial log file #1
	 * 2. database files
	 * 3. the "init file"
	 *
	 * Steps 1 and 2 can be attempted independently.  Step 1b is
	 * dependent on successful completion of 1a.
	 */

	/* Step 1a. */
	if ((ret = __rep_remove_logs(env)) == 0) {
		/*
		 * Since we have no logs, recover by making it look like
		 * the case when a new client first starts up, namely we
		 * have nothing but a fresh log file #1.  This is a
		 * little wasteful, since we may soon remove this log
		 * file again.  But it's insignificant in the context of
		 * interrupted internal init.
		 */
		dblp = env->lg_handle;
		lp = dblp->reginfo.primary;

		/* Step 1b. */
		ret = __rep_log_setup(env,
		    rep, 1, DB_LOGVERSION, &lp->ready_lsn);
	}

	/* Step 2. */
	if ((t_ret = __rep_walk_filelist(env, rep->infoversion,
	    R_ADDR(infop, rep->originfo_off), rep->originfolen,
	    rep->nfiles, __rep_remove_by_list, NULL)) != 0 && ret == 0)
		ret = t_ret;

	/*
	 * Step 3 must not be done if anything fails along the way, because the
	 * init file's raison d'etre is to show that some files remain to be
	 * cleaned up.
	 */
	if (ret == 0)
		ret = __rep_remove_init_file(env);

	return (ret);
}

/*
 * __rep_filedone -
 *	We need to check if we're done with the current file after
 *	processing the current page.  Stat the database to see if
 *	we have all the pages and blobs.  If so, we need to clean up/close
 *	this one, set up for the next one, and ask for its pages and blobs,
 *	or if this is the last file, request the log records and
 *	move to the REP_RECOVER_LOG state.
 */
static int
__rep_filedone(env, ip, eid, rep, msgfp, type)
	ENV *env;
	DB_THREAD_INFO *ip;
	int eid;
	REP *rep;
	__rep_fileinfo_args *msgfp;
	u_int32_t type;
{
	DBT msg;
	REGINFO *infop;
	__rep_fileinfo_args *rfp;
	__rep_blob_update_req_args rbur;
	int ret;
	u_int8_t buf[__REP_BLOB_UPDATE_REQ_SIZE];

	memset(&msg, 0, sizeof(DBT));

	/*
	 * We've put our page, now we need to do any gap processing
	 * that might be needed to re-request pages.
	 */
	ret = __rep_page_gap(env, rep, msgfp, type);
	/*
	 * The world changed while we were doing gap processing.
	 * We're done here.
	 */
	if (ret == DB_REP_PAGEDONE)
		return (0);

	infop = env->reginfo;
	GET_CURINFO(rep, infop, rfp);
	/*
	 * max_pgno is 0-based and npages is 1-based, so we don't have
	 * all the pages until npages is > max_pgno.
	 */
	VPRINT(env, (env, DB_VERB_REP_SYNC,
	    "FILEDONE: have %lu pages. Need %lu.",
	    (u_long)rep->npages, (u_long)rfp->max_pgno + 1));
	if (rep->npages <= rfp->max_pgno)
		return (0);

	/*
	 * If we're queue and we think we have all the pages for this file,
	 * we need to do special queue processing.  Queue is handled in
	 * several stages.
	 */
	if (rfp->type == (u_int32_t)DB_QUEUE &&
	    ((ret = __rep_queue_filedone(env, ip, rep, rfp)) !=
	    DB_REP_PAGEDONE))
		return (ret);

	/* Request blob files. */
	if (rfp->blob_fid_lo != 0 || rfp->blob_fid_hi != 0) {
		ret = 0;
		rep->blob_sync = 1;
		memset(&rbur, 0, sizeof(__rep_blob_update_req_args));
		GET_LO_HI(env,
		    rfp->blob_fid_lo, rfp->blob_fid_hi, rbur.blob_fid, ret);
		msg.size = __REP_BLOB_UPDATE_REQ_SIZE;
		msg.data = buf;
		/*
		 * This is safe since the beginning of both messages is the
		 * same.
		 */
		if (rep->version < DB_REPVERSION_62)
			__rep_blob_update_req_v8_marshal(env,
			    (__rep_blob_update_req_v8_args *)&rbur, msg.data);
		else
			__rep_blob_update_req_marshal(env, &rbur, msg.data);
		(void)__rep_send_message(env,
		    rep->master_id, REP_BLOB_UPDATE_REQ, NULL, &msg, 0, 0);
		return (ret);
	}

	/*
	 * We have all the data for this file.  Clean up.
	 */
	if ((ret = __rep_init_cleanup(env, rep, 0)) != 0)
		return (ret);

	rep->curfile++;
	ret = __rep_nextfile(env, eid, rep);

	return (ret);
}

/*
 * __rep_blobdone -
 *	We need to check if we're done with the current file after
 *	processing the current blob chunk.
 *
 *	Caller must hold client database mutex (mtx_clientdb) and
 *	REP_SYSTEM_LOCK.
 */
static int
__rep_blobdone(env, eid, ip, rep, blob_fid, force, gapflags)
	ENV *env;
	int eid;
	DB_THREAD_INFO *ip;
	REP *rep;
	db_seq_t blob_fid;
	int force;
	u_int32_t gapflags;
{
	DBT msg;
	__rep_blob_update_req_args rbur;
	int done, ret;
	u_int8_t buf[__REP_BLOB_UPDATE_REQ_SIZE];

	/*
	 * We've written our blob chunk, now we need to do any gap processing
	 * that might be needed to re-request chunks.
	 */
	done = 0;
	ret = __rep_blob_chunk_gap(
	    env, eid, ip, rep, &done, blob_fid, force, gapflags);
	/*
	 * The world changed while we were doing gap processing.
	 * We're done here.
	 */
	if (ret == DB_REP_PAGEDONE)
		return (0);
	else if (ret != 0)
		goto err;

	/*
	 * If the blob database is empty then all files in the current list
	 * have been processed.  However, there may be more files on the
	 * master, so request the next list if that is the case.
	 */
	if (done && rep->blob_more_files) {
		memset(&rbur, 0, sizeof(__rep_blob_update_req_args));
		rbur.blob_fid = (u_int64_t)blob_fid;
		rbur.blob_sid = (u_int64_t)rep->last_blob_sid;
		rbur.blob_id = (u_int64_t)rep->last_blob_id;
		rbur.highest_id = (u_int64_t)rep->highest_id;
		/*
		 * Re-requesting a BLOB_ALL_REQ message poses a special
		 * problem.  Sending a BLOB_ALL_REQ message is basically taking
		 * the BLOB_UPDATE message and sending it back to the
		 * master/peer.  When doing a BLOB_ALL_REQ re-request, the
		 * BLOB_UPDATE message is no longer available, so internal init
		 * has to go back to the BLOB_UPDATE_REQ step.  However, when
		 * dealing with a view that does not replicate that database,
		 * this could result in an infinite loop of the peer not
		 * responding to the BLOB_ALL_REQ message, so the client
		 * re-sends the BLOB_UPDATE_REQ message to the master, the
		 * master responding with BLOB_UPDATE, but since the client no
		 * longer knows that this is a re-request situation it sends
		 * the BLOB_ALL_REQ message to the peer again.
		 */
		if (gapflags != 0)
			F_SET(&rbur, BLOB_REREQ);
		rep->gap_bl_hi_id = rep->gap_bl_hi_sid = 0;
		rep->gap_bl_hi_off = 0;
		msg.size = __REP_BLOB_UPDATE_REQ_SIZE;
		msg.data = buf;
		/*
		 * This is safe since the beginning of both messages is the
		 * same.
		 */
		if (rep->version < DB_REPVERSION_62)
			__rep_blob_update_req_v8_marshal(env,
			    (__rep_blob_update_req_v8_args *)&rbur, msg.data);
		else
			__rep_blob_update_req_marshal(env, &rbur, msg.data);
		(void)__rep_send_message(env,
		    rep->master_id, REP_BLOB_UPDATE_REQ, NULL, &msg, 0, 0);
		return (0);
	} else if (!done)
		return (0);

	/*
	 * We have all the data for this file.  Clean up.
	 */
	if ((ret = __rep_init_cleanup(env, rep, 0)) != 0)
		goto err;

	rep->curfile++;
	ret = __rep_nextfile(env, eid, rep);
err:
	return (ret);
}

/*
 * __rep_blob_chunk_gap -
 *	We have written a blob chunk.  Now check if there are any that need
 *	to be re-requested.  The blob chunk gap database contains
 *	descriptions of all the blob chunks that have yet to arrive.
 *
 *	Caller must hold client database mutex (mtx_clientdb) and
 *	REP_SYSTEM_LOCK.
 */
static int
__rep_blob_chunk_gap(env, eid, ip, rep, done, blob_fid, force, gapflags)
	ENV *env;
	int eid;
	DB_THREAD_INFO *ip;
	REP *rep;
	int *done;
	db_seq_t blob_fid;
	int force;
	u_int32_t gapflags;
{
	DBC *dbc;
	DBT data, high, key, msg;
	DB_LOG *dblp;
	DB_REP *db_rep;
	LOG *lp;
	REGINFO *infop;
	__rep_blob_chunk_req_args rbcr;
	__rep_fileinfo_args *rfp;
	db_seq_t cur_blob_fid;
	off_t offset;
	int ret;
	u_int32_t flags;
	u_int8_t buf[BLOB_KEY_SIZE], msgbuf[__REP_BLOB_CHUNK_REQ_SIZE];

	db_rep = env->rep_handle;
	dblp = env->lg_handle;
	lp = dblp->reginfo.primary;
	infop = env->reginfo;
	ret = 0;
	dbc = NULL;
	*done = 0;

	/*
	 * Make sure we're still talking about the same file.
	 * If not, we're done here.
	 */
	GET_CURINFO(rep, infop, rfp);
	GET_LO_HI(env, rfp->blob_fid_lo, rfp->blob_fid_hi, cur_blob_fid, ret);
	if (cur_blob_fid != blob_fid) {
		ret = DB_REP_PAGEDONE;
		goto err;
	}

	if (FLD_ISSET(gapflags, REP_GAP_REREQUEST))
		flags = DB_REP_REREQUEST;
	else
		flags = DB_REP_ANYWHERE;

	/* Get the first missing blob chunk. */
	if ((ret = __db_cursor(db_rep->blob_dbp, ip, NULL, &dbc, 0)) != 0)
		goto err;
	memset(&key, 0, sizeof(DBT));
	memset(&data, 0, sizeof(DBT));
	ret = __dbc_get(dbc, &key, &data, DB_FIRST);
	if (ret == DB_NOTFOUND) {
		/* All blobs received. */
		ret = 0;
		*done = 1;
		goto err;
	} else if (ret != 0)
		goto err;

	DB_ASSERT(env, key.size == BLOB_KEY_SIZE);
	DB_ASSERT(env, data.size == sizeof(off_t));
	offset = *(off_t *)data.data;
	/*
	 * Format the sdbid and id of the high chunk as a blob gap
	 * database key, so it can be compared with the entries in that
	 * database.
	 */
	memset(&high, 0, sizeof(DBT));
	memcpy(buf, &rep->gap_bl_hi_sid, BLOB_ID_SIZE);
	memcpy(buf + BLOB_ID_SIZE, &rep->gap_bl_hi_id, BLOB_ID_SIZE);
	high.data = buf;
	high.size = BLOB_KEY_SIZE;

	/*
	 * If the first chunk in the database is larger than the highest chunk
	 * received, then there is no gap.
	 *
	 * If a gap does exist, check if it is time to do a re-request.  If so,
	 * re-request every chunk that exists before the highest received.
	 */
	if (!force && (__rep_blob_cmp(NULL, &key, &high, NULL) > 0 ||
	    (__rep_blob_cmp(NULL, &key, &high, NULL) == 0 &&
	    offset > rep->gap_bl_hi_off))) {
		lp->wait_ts = db_rep->request_gap;
		__os_gettime(env, &lp->rcvd_ts, 1);
	} else if (force || __rep_check_doreq(env, rep)) {
		/*
		 * Re-request every chunk less than the highest one, plus the
		 * next blob chunk that we are expecting.  The next expected
		 * blob chunk is requested in case the last blob chunk is lost
		 * in transit.
		 */
		do {
			memset(&rbcr, 0, sizeof(__rep_blob_chunk_req_args));
			memcpy(&(rbcr.blob_sid), key.data, BLOB_ID_SIZE);
			memcpy(&(rbcr.blob_id),
			    (u_int8_t *)key.data + BLOB_ID_SIZE, BLOB_ID_SIZE);
			rbcr.offset = *(u_int64_t *)data.data;
			rbcr.blob_fid = (u_int64_t)blob_fid;
			msg.size = __REP_BLOB_CHUNK_REQ_SIZE;
			msg.data = msgbuf;
			RPRINT(env, (env, DB_VERB_REP_SYNC,
"blob_chunk_gap: Req file_id %llu, sdb_id %llu, blob_id %llu, offset %llu",
			    (long long)rbcr.blob_fid, (long long)rbcr.blob_sid,
			    (long long)rbcr.blob_id, (long long)rbcr.offset));
			__rep_blob_chunk_req_marshal(env, &rbcr, msg.data);
			(void)__rep_send_message(env, eid,
			    REP_BLOB_CHUNK_REQ, NULL, &msg, 0, flags);
			STAT_INC(env, rep, ext_chunk_rereq,
			    rep->stat.st_ext_rereq, rep->master_id);
			/*
			 * Break after requesting the chunk after the highest
			 * one.
			 */
			if (__rep_blob_cmp(NULL, &key, &high, NULL) > 0 ||
			    (__rep_blob_cmp(NULL, &key, &high, NULL) == 0 &&
			    offset > rep->gap_bl_hi_off))
				break;
			if ((ret = __dbc_get(
			    dbc, &key, &data, DB_NEXT)) != 0) {
				if (ret == DB_NOTFOUND) {
					ret = 0;
					break;
				}
				goto err;
			}
		} while (1);
	}

err:	if (dbc != NULL)
		(void)__dbc_close(dbc);

	return (ret);
}

/*
 * __rep_blob_chunk_req
 *	Answer a request for a specific blob chunk.
 *
 * PUBLIC: int __rep_blob_chunk_req __P((ENV *, int, DBT *));
 */
int
__rep_blob_chunk_req(env, eid, rec)
	ENV *env;
	int eid;
	DBT *rec;
{
	DB *dbp;
	DBT msg;
	DB_FH *fhp;
#ifdef	CONFIG_TEST
	DB_REP *db_rep;
#endif
	__rep_blob_chunk_args rbc;
	__rep_blob_chunk_req_args rbcr;
	int ret;
	u_int8_t *chunk_buf, *msg_buf, *ptr;

	dbp = NULL;
	fhp = NULL;
	chunk_buf = msg_buf = NULL;
#ifdef	CONFIG_TEST
	db_rep = env->rep_handle;
#endif

	if ((ret =
	    __os_malloc(env, MEGABYTE + __REP_BLOB_CHUNK_SIZE, &msg_buf)) != 0)
		goto err;
	memset(&msg, 0, sizeof(DBT));
	msg.data = msg_buf;
	msg.ulen = MEGABYTE + __REP_BLOB_CHUNK_SIZE;
	if ((ret = __os_malloc(env, MEGABYTE, &chunk_buf)) != 0)
		goto err;
	memset(&rbc, 0, sizeof(__rep_blob_chunk_args));
	rbc.data.data = chunk_buf;
	rbc.data.ulen = MEGABYTE;
	rbc.data.flags = DB_DBT_USERMEM;

	if ((ret = __rep_blob_chunk_req_unmarshal(
	    env, &rbcr, rec->data, rec->size, &ptr)) != 0)
		goto err;

	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "blob_chunk_req: file_id %llu, sdbid %llu, id %llu, offset %llu",
	    (long long)rbcr.blob_fid, (long long)rbcr.blob_sid,
	    (long long)rbcr.blob_id, (long long)rbcr.offset));

	rbc.blob_fid = rbcr.blob_fid;
	rbc.blob_id = rbcr.blob_id;
	rbc.blob_sid = rbcr.blob_sid;
	rbc.offset = rbcr.offset;
	if ((ret = __db_create_internal(&dbp, env, 0)) != 0)
		goto err;
	dbp->blob_file_id = (db_seq_t)rbcr.blob_fid;
	dbp->blob_sdb_id = (db_seq_t)rbcr.blob_sid;
	if ((ret = __blob_make_sub_dir(env, &dbp->blob_sub_dir,
	    (db_seq_t)rbcr.blob_fid, (db_seq_t)rbcr.blob_sid)) != 0)
		goto err;
	if ((ret = __blob_file_open(
	    dbp, &fhp, (db_seq_t)rbcr.blob_id, DB_FOP_READONLY, 0)) != 0) {
		/*
		* The file may have been deleted between creating the
		* list and sending the request.  Send a message saying
		* the file has been deleted.
		*/
		if (ret == ENOENT) {
			if (IS_VIEW_SITE(env)) {
				ret = DB_NOTFOUND;
				goto err;
			}
			ret = 0;
			F_SET(&rbc, BLOB_DELETE);
			rbc.data.size = 0;
			__rep_blob_chunk_marshal(env, &rbc, msg.data);
			msg.size = __REP_BLOB_CHUNK_SIZE;
			(void)__rep_send_message(
			    env, eid, REP_BLOB_CHUNK, NULL, &msg, 0, 0);
#ifdef	CONFIG_TEST
			STAT_INC(env, rep, ext_deleted,
			    db_rep->region->stat.st_ext_deleted, eid);
#endif
			goto err;
		}
		goto err;
	}
	if ((ret = __blob_file_read(
	    env, fhp, &rbc.data, (off_t)rbcr.offset, MEGABYTE)) != 0)
		goto err;
	DB_ASSERT(env, rbc.data.size <= MEGABYTE);

	/*
	 * In rare cases the blob file may have gotten shorter
	 * since the list was created.
	 */
	if (rbc.data.size == 0) {
		F_SET(&rbc, BLOB_CHUNK_FAIL);
#ifdef	CONFIG_TEST
		STAT_INC(env, rep, ext_truncated,
		    db_rep->region->stat.st_ext_truncated, eid);
#endif
	}
	__rep_blob_chunk_marshal(env, &rbc, msg.data);
	msg.size = __REP_BLOB_CHUNK_SIZE + rbc.data.size;
	(void)__rep_send_message(env, eid, REP_BLOB_CHUNK, NULL, &msg, 0, 0);

err:	if (chunk_buf != NULL)
		__os_free(env, chunk_buf);
	if (msg_buf != NULL)
		__os_free(env, msg_buf);
	if (fhp != NULL)
		(void)__os_closehandle(env, fhp);
	if (dbp != 0)
		(void)__db_close(dbp, NULL, 0);
	return (ret);
}

/*
 * Starts requesting pages for the next file in the list (if any), or if not,
 * proceeds to the next stage: requesting logs.
 *
 * !!!
 * Must be called with both clientdb_mutex and REP_SYSTEM, though we may drop
 * REP_SYSTEM_LOCK momentarily in order to send a LOG_REQ (but not a PAGE_REQ).
 */
static int
__rep_nextfile(env, eid, rep)
	ENV *env;
	int eid;
	REP *rep;
{
	DBT dbt;
	__rep_logreq_args lr_args;
	DB_LOG *dblp;
	DB_REP *db_rep;
	DELAYED_BLOB_LIST *dbl;
	LOG *lp;
	REGENV *renv;
	REGINFO *infop;
	__rep_fileinfo_args *curinfo, *rfp, rf;
	__rep_fileinfo_v6_args *rfpv6;
	__rep_fileinfo_v7_args *rfpv7;
	int *curbuf, ret, view_partial;
	u_int8_t *buf, *info_ptr, lrbuf[__REP_LOGREQ_SIZE], *nextinfo;
	size_t len, msgsz;
	char *name;
	void *rffree;

	infop = env->reginfo;
	renv = infop->primary;
	db_rep = env->rep_handle;
	rfp = NULL;
	dbl = NULL;

	/*
	 * Always direct the next request to the master (at least nominally),
	 * regardless of where the current response came from.  The application
	 * can always still redirect it to another client.
	 */
	if (rep->master_id != DB_EID_INVALID)
		eid = rep->master_id;

	while (rep->curfile < rep->nfiles) {
		/* Set curinfo to next file and examine it. */
		info_ptr = R_ADDR(infop,
		    rep->originfo_off + (rep->originfolen - rep->infolen));
		/*
		 * Build a current struct by copying in the older
		 * version struct and then setting up the new fields.
		 * This is safe because all old fields are in the
		 * same location in the current struct.
		 */
		if (rep->infoversion < DB_REPVERSION_53) {
			if ((ret = __rep_fileinfo_v6_unmarshal(env, &rfpv6,
			    info_ptr, rep->infolen, &nextinfo)) != 0)
				return (ret);
			memcpy(&rf, rfpv6, sizeof(__rep_fileinfo_v6_args));
			rf.dir.data = NULL;
			rf.dir.size = 0;
			rf.blob_fid_lo = rf.blob_fid_hi = 0;
			rfp = &rf;
			rffree = rfpv6;
		} else if (rep->infoversion < DB_REPVERSION_61) {
			if ((ret = __rep_fileinfo_v7_unmarshal(env, &rfpv7,
			    info_ptr, rep->infolen, &nextinfo)) != 0)
				return (ret);
			memcpy(&rf, rfpv7, sizeof(__rep_fileinfo_v7_args));
			rf.blob_fid_lo = rf.blob_fid_hi = 0;
			rfp = &rf;
			rffree = rfpv7;
		} else {
			if ((ret = __rep_fileinfo_unmarshal(env,
			    &rfp, info_ptr, rep->infolen, &nextinfo)) != 0) {
				RPRINT(env, (env, DB_VERB_REP_SYNC,
				    "NEXTINFO: Fileinfo read: %s",
				    db_strerror(ret)));
				return (ret);
			}
			rffree = rfp;
		}
#ifndef HAVE_64BIT_TYPES
		if (rfp->blob_fid_lo != 0 || rfp->blob_fid_hi != 0) {
		    __db_errx(env, DB_STR("0243",
		"External files require 64 integer compiler support."));
			__os_free(env, rffree);
			return (DB_OPNOTSUP);
		}
#endif
		rep->infolen -= (u_int32_t)(nextinfo - info_ptr);
		MUTEX_LOCK(env, renv->mtx_regenv);
		ret = __env_alloc(infop, sizeof(__rep_fileinfo_args) +
		    rfp->uid.size + rfp->info.size + rfp->dir.size, &curbuf);
		MUTEX_UNLOCK(env, renv->mtx_regenv);
		if (ret != 0) {
			__os_free(env, rffree);
			return (ret);
		} else
			rep->curinfo_off = R_OFFSET(infop, curbuf);
		/* Copy fileinfo basic structure into curinfo. */
		memcpy(R_ADDR(infop, rep->curinfo_off),
		    (u_int8_t*)rfp, sizeof(__rep_fileinfo_args));
		/* Set up curinfo pointers to the various DBT data fields. */
		GET_CURINFO(rep, infop, curinfo);
		/* Copy uid and info DBT data from originfo buffer. */
		if (rfp->uid.size > 0)
			memcpy(curinfo->uid.data,
			    rfp->uid.data, rfp->uid.size);
		if (rfp->info.size > 0)
			memcpy(curinfo->info.data,
			    rfp->info.data, rfp->info.size);
		if (rfp->dir.size > 0)
			memcpy(curinfo->dir.data,
			    rfp->dir.data, rfp->dir.size);
		__os_free(env, rffree);

		/*
		 * If a partial callback is set, invoke the callback to see if
		 * this file should be replicated.
		 */
		if (IS_VIEW_SITE(env) && curinfo->info.size > 0 &&
		    !FLD_ISSET(curinfo->db_flags, DB_AM_INMEM)) {
			name = (char *)curinfo->info.data;
			DB_ASSERT(env, db_rep->partial != NULL);
			/*
			 * Always replicate system owned databases.
			 */
			if (IS_DB_FILE(name) && !IS_BLOB_META(name))
				view_partial = 1;
			else if ((ret = __rep_call_partial(env,
			    name, &view_partial, 0, &dbl)) != 0) {
				VPRINT(env, (env, DB_VERB_REP_SYNC,
				    "rep_nextfile: partial cb err %d for %s",
				    ret, name));
				return (ret);
			}
			/*
			 * dbl != NULL when we could not find the name of the
			 * database that owns a blob meta database.  If that
			 * happens then it was never opened, which means it
			 * was not replicated, and as such neither should its
			 * bmd be replicated.
			 */
			if (dbl != NULL) {
				view_partial = 0;
				__os_free(env, dbl);
				dbl = NULL;
			}
			VPRINT(env, (env, DB_VERB_REP_SYNC,
			    "rep_nextfile: %s file %s %d on view site.",
			    view_partial == 0 ?
			    "Skipping" : "Replicating",
			    name, curinfo->filenum));
			/*
			 * If we're skipping the file, move to the next one.
			 */
			if (view_partial == 0) {
				MUTEX_LOCK(env, renv->mtx_regenv);
				__env_alloc_free(infop,
				    R_ADDR(infop, rep->curinfo_off));
				MUTEX_UNLOCK(env, renv->mtx_regenv);
				rep->curinfo_off = INVALID_ROFF;
				rep->curfile++;
				continue;
			}
		}

		/*
		 * Skip over regular DB's in "abbreviated" internal inits.
		 * Masters at 6.0 and later only send in-memory databases for
		 * abbreviated internal inits, but 5.3 and earlier masters
		 * send all database files and this code is still needed to
		 * avoid requesting pages for on-disk database files in
		 * those cases.
		 */
		if (F_ISSET(rep, REP_F_ABBREVIATED) &&
		    !FLD_ISSET(curinfo->db_flags, DB_AM_INMEM) &&
		    rep->infoversion <= DB_REPVERSION_53) {
			VPRINT(env, (env, DB_VERB_REP_SYNC,
			    "Skipping file %d in abbreviated internal init",
			    curinfo->filenum));
			MUTEX_LOCK(env, renv->mtx_regenv);
			__env_alloc_free(infop,
			    R_ADDR(infop, rep->curinfo_off));
			MUTEX_UNLOCK(env, renv->mtx_regenv);
			rep->curinfo_off = INVALID_ROFF;
			rep->curfile++;
			continue;
		}

		/* Request this file's pages. */
		DB_ASSERT(env, curinfo->pgno == 0);
		rep->ready_pg = 0;
		rep->npages = 0;
		rep->waiting_pg = PGNO_INVALID;
		rep->max_wait_pg = PGNO_INVALID;
		memset(&dbt, 0, sizeof(dbt));
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Next file %d: pgsize %lu, maxpg %lu",
		    curinfo->filenum, (u_long)curinfo->pgsize,
		    (u_long)curinfo->max_pgno));
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "name %s dir %s",
		    curinfo->info.size > 0 ?  (char *) curinfo->info.data :
		    "NULL", curinfo->dir.size > 0 ?
		    (char *)curinfo->dir.data : "NULL"));
		msgsz = __REP_FILEINFO_SIZE + curinfo->dir.size +
		    curinfo->uid.size + curinfo->info.size;
		if ((ret = __os_calloc(env, 1, msgsz, &buf)) != 0)
			return (ret);
		/*
		 * It is safe to cast to the old structs
		 * because the first part of the current
		 * struct matches the old structs.
		 */
		if (rep->infoversion < DB_REPVERSION_53)
			ret = __rep_fileinfo_v6_marshal(env,
			    (__rep_fileinfo_v6_args *)curinfo, buf,
			    msgsz, &len);
		else if (rep->infoversion < DB_REPVERSION_61)
			ret = __rep_fileinfo_v7_marshal(env,
			    (__rep_fileinfo_v7_args *)curinfo, buf,
			    msgsz, &len);
		else
			ret = __rep_fileinfo_marshal(env,
			    curinfo, buf, msgsz, &len);
		if (ret != 0) {
			__os_free(env, buf);
			return (ret);
		}
		DB_INIT_DBT(dbt, buf, len);
		(void)__rep_send_message(env, eid, REP_PAGE_REQ,
		    NULL, &dbt, 0, DB_REP_ANYWHERE);
		__os_free(env, buf);

		return (0);
	}

	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "NEXTFILE: have %d files.  RECOVER_LOG now", rep->nfiles));
	/*
	 * Move to REP_RECOVER_LOG state.
	 * Request logs.
	 */
	/*
	 * We need to do a sync here so that any later opens
	 * can find the file and file id.  We need to do it
	 * before we clear SYNC_PAGE so that we do not
	 * try to flush the log.
	 */
	if ((ret = __memp_sync_int(env, NULL, 0,
	    DB_SYNC_CACHE | DB_SYNC_INTERRUPT_OK, NULL, NULL)) != 0)
		return (ret);
	rep->sync_state = SYNC_LOG;
	memset(&dbt, 0, sizeof(dbt));
	lr_args.endlsn = rep->last_lsn;
	if ((ret = __rep_logreq_marshal(env, &lr_args, lrbuf,
	    __REP_LOGREQ_SIZE, &len)) != 0)
		return (ret);
	DB_INIT_DBT(dbt, lrbuf, len);

	/*
	 * Get the logging subsystem ready to receive the first log record we
	 * are going to ask for.  In the case of a normal internal init, this is
	 * pretty simple, since we only deal in whole log files.  In the
	 * ABBREVIATED case we've already taken care of this, back when we
	 * processed the UPDATE message, because we had to do it by rolling back
	 * to a sync point at an arbitrary LSN.
	 */
	dblp = env->lg_handle;
	lp = dblp->reginfo.primary;
	/*
	 * Update ready_lsn so that future rerequests and VERIFY_FAILs know
	 * where to start.
	 */
	if (!F_ISSET(rep, REP_F_ABBREVIATED) &&
	    (ret = __rep_log_setup(env, rep,
	    rep->first_lsn.file, rep->first_vers, &lp->ready_lsn)) != 0)
		return (ret);
	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "NEXTFILE: LOG_REQ from LSN [%lu][%lu] to [%lu][%lu]",
	    (u_long)rep->first_lsn.file, (u_long)rep->first_lsn.offset,
	    (u_long)rep->last_lsn.file, (u_long)rep->last_lsn.offset));
	REP_SYSTEM_UNLOCK(env);
	__os_gettime(env, &lp->rcvd_ts, 1);
	lp->wait_ts = rep->request_gap;
	(void)__rep_send_message(env, eid,
	    REP_LOG_REQ, &rep->first_lsn, &dbt, REPCTL_INIT, DB_REP_ANYWHERE);
	REP_SYSTEM_LOCK(env);
	return (0);
}

/*
 * Run a recovery, for the purpose of rolling back the client environment to a
 * specific sync point, in preparation for doing an abbreviated internal init
 * (materializing only NIMDBs, when we already have the on-disk DBs).
 *
 * REP_SYSTEM_LOCK should be held on entry, and will be held on exit, but we
 * drop it momentarily during the call.
 */
static int
__rep_rollback(env, lsnp)
	ENV *env;
	DB_LSN *lsnp;
{
	DB_LOG *dblp;
	DB_REP *db_rep;
	LOG *lp;
	REP *rep;
	DB_THREAD_INFO *ip;
	DB_LSN trunclsn;
	int ret;
	u_int32_t unused;

	db_rep = env->rep_handle;
	rep = db_rep->region;
	dblp = env->lg_handle;
	lp = dblp->reginfo.primary;
	ENV_GET_THREAD_INFO(env, ip);

	DB_ASSERT(env, FLD_ISSET(rep->lockout_flags,
	    REP_LOCKOUT_API | REP_LOCKOUT_MSG | REP_LOCKOUT_OP));

	REP_SYSTEM_UNLOCK(env);

	if ((ret = __rep_dorecovery(env, lsnp, &trunclsn)) != 0)
		goto errlock;

	MUTEX_LOCK(env, rep->mtx_clientdb);
	lp->ready_lsn = trunclsn;
	ZERO_LSN(lp->waiting_lsn);
	ZERO_LSN(lp->max_wait_lsn);
	lp->max_perm_lsn = *lsnp;
	lp->wait_ts = rep->request_gap;
	__os_gettime(env, &lp->rcvd_ts, 1);
	ZERO_LSN(lp->verify_lsn);

	if (db_rep->rep_db == NULL &&
	    (ret = __rep_client_dbinit(env, 0, REP_DB)) != 0) {
		MUTEX_UNLOCK(env, rep->mtx_clientdb);
		goto errlock;
	}

	F_SET(db_rep->rep_db, DB_AM_RECOVER);
	MUTEX_UNLOCK(env, rep->mtx_clientdb);
	ret = __db_truncate(db_rep->rep_db, ip, NULL, &unused);
	MUTEX_LOCK(env, rep->mtx_clientdb);
	F_CLR(db_rep->rep_db, DB_AM_RECOVER);
	STAT_SET(env, rep, log_queued, rep->stat.st_log_queued, 0, lsnp);
	MUTEX_UNLOCK(env, rep->mtx_clientdb);

errlock:
	REP_SYSTEM_LOCK(env);

	return (ret);
}

/*
 * __rep_mpf_open -
 *	Create and open the mpool file for a database.
 *	Used by both master and client to bring files into mpool.
 */
static int
__rep_mpf_open(env, mpfp, rfp, flags)
	ENV *env;
	DB_MPOOLFILE **mpfp;
	__rep_fileinfo_args *rfp;
	u_int32_t flags;
{
	DB db;
	int ret;

	if ((ret = __memp_fcreate(env, mpfp)) != 0)
		return (ret);

	/*
	 * We need a dbp to pass into to __env_mpool.  Set up
	 * only the parts that it needs.
	 */
	memset(&db, 0, sizeof(db));
	db.env = env;
	db.type = (DBTYPE)rfp->type;
	db.pgsize = rfp->pgsize;
	memcpy(db.fileid, rfp->uid.data, DB_FILE_ID_LEN);
	db.flags = rfp->db_flags;
	/* We need to make sure the dbp isn't marked open. */
	F_CLR(&db, DB_AM_OPEN_CALLED);
	/*
	 * The byte order of this database may be different from my local native
	 * byte order.  If so, set the swap bit so that the necessary swapping
	 * will be done during file I/O.
	 */
	if ((F_ISSET(env, ENV_LITTLEENDIAN) &&
	    !FLD_ISSET(rfp->finfo_flags, REPINFO_DB_LITTLEENDIAN)) ||
	    (!F_ISSET(env, ENV_LITTLEENDIAN) &&
	    FLD_ISSET(rfp->finfo_flags, REPINFO_DB_LITTLEENDIAN))) {
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "rep_mpf_open: Different endian database.  Set swap bit."));
		F_SET(&db, DB_AM_SWAP);
	} else
		F_CLR(&db, DB_AM_SWAP);

	db.mpf = *mpfp;
	if (F_ISSET(&db, DB_AM_INMEM))
		(void)__memp_set_flags(db.mpf, DB_MPOOL_NOFILE, 1);
	if ((ret = __env_mpool(&db, rfp->info.data, flags)) != 0) {
		(void)__memp_fclose(db.mpf, 0);
		*mpfp = NULL;
	}
	return (ret);
}

/*
 * __rep_pggap_req -
 *	Request a page gap.  Assumes the caller holds the rep_mutex.
 *
 * PUBLIC: int __rep_pggap_req __P((ENV *, REP *, __rep_fileinfo_args *,
 * PUBLIC:     u_int32_t));
 */
int
__rep_pggap_req(env, rep, reqfp, gapflags)
	ENV *env;
	REP *rep;
	__rep_fileinfo_args *reqfp;
	u_int32_t gapflags;
{
	DBT max_pg_dbt;
	REGINFO *infop;
	__rep_fileinfo_args *curinfo, *tmpfp, t;
	size_t len, msgsz;
	u_int32_t flags;
	int alloc, master, ret;
	u_int8_t *buf;

	infop = env->reginfo;
	ret = 0;
	alloc = 0;
	/*
	 * There is a window where we have to set REP_RECOVER_PAGE when
	 * we receive the update information to transition from getting
	 * file information to getting page information.  However, that
	 * thread does release and then reacquire mutexes.  So, we might
	 * try re-requesting before the original thread can get curinfo
	 * setup.  If curinfo isn't set up there is nothing to do.
	 */
	if (rep->curinfo_off == INVALID_ROFF)
		return (0);
	GET_CURINFO(rep, infop, curinfo);
	if (reqfp == NULL) {
		if ((ret = __rep_finfo_alloc(env, curinfo, &tmpfp)) != 0)
			return (ret);
		alloc = 1;
	} else {
		t = *reqfp;
		tmpfp = &t;
	}

	/*
	 * If we've never requested this page, then
	 * request everything between it and the first
	 * page we have.  If we have requested this page
	 * then only request this record, not the entire gap.
	 */
	flags = 0;
	memset(&max_pg_dbt, 0, sizeof(max_pg_dbt));
	/*
	 * If this is a PAGE_MORE and we're forcing then we want to
	 * force the request to ask for the next page after this one.
	 */
	if (FLD_ISSET(gapflags, REP_GAP_FORCE))
		tmpfp->pgno++;
	else
		tmpfp->pgno = rep->ready_pg;
	msgsz = __REP_FILEINFO_SIZE + tmpfp->dir.size +
	    tmpfp->uid.size + tmpfp->info.size;
	if ((ret = __os_calloc(env, 1, msgsz, &buf)) != 0)
		goto err;
	if (rep->max_wait_pg == PGNO_INVALID ||
	    FLD_ISSET(gapflags, REP_GAP_FORCE | REP_GAP_REREQUEST)) {
		/*
		 * Request the gap - set max to waiting_pg - 1 or if
		 * there is no waiting_pg, just ask for one.
		 */
		if (rep->waiting_pg == PGNO_INVALID) {
			if (FLD_ISSET(gapflags,
			    REP_GAP_FORCE | REP_GAP_REREQUEST))
				rep->max_wait_pg = curinfo->max_pgno;
			else
				rep->max_wait_pg = rep->ready_pg;
		} else {
			/*
			 * If we're forcing, and waiting_pg is less than
			 * the page we want to start this request at, then
			 * we set max_wait_pg to the max pgno in the file.
			 */
			if (FLD_ISSET(gapflags, REP_GAP_FORCE) &&
			  rep->waiting_pg < tmpfp->pgno)
				rep->max_wait_pg = curinfo->max_pgno;
			else
				rep->max_wait_pg = rep->waiting_pg - 1;
		}
		tmpfp->max_pgno = rep->max_wait_pg;
		/*
		 * Gap requests are "new" and can go anywhere.
		 */
		if (FLD_ISSET(gapflags, REP_GAP_REREQUEST))
			flags = DB_REP_REREQUEST;
		else
			flags = DB_REP_ANYWHERE;
	} else {
		/*
		 * Request 1 page - set max to ready_pg.
		 */
		rep->max_wait_pg = rep->ready_pg;
		tmpfp->max_pgno = rep->ready_pg;
		/*
		 * If we're dropping to singletons, this is a rerequest.
		 */
		flags = DB_REP_REREQUEST;
	}
	if ((master = rep->master_id) != DB_EID_INVALID) {

		STAT_INC(env,
		    rep, pg_request, rep->stat.st_pg_requested, master);
		/*
		 * We need to request the pages, but we need to get the
		 * new info into rep->finfo.  Assert that the sizes never
		 * change.  The only thing this should do is change
		 * the pgno field.  Everything else remains the same.
		 *
		 * It is safe to cast to the old structs
		 * because the first part of the current
		 * struct matches the old structs.
		 */
		if (rep->infoversion < DB_REPVERSION_53)
			ret = __rep_fileinfo_v6_marshal(env,
			    (__rep_fileinfo_v6_args *)tmpfp, buf,
			    msgsz, &len);
		else if (rep->infoversion < DB_REPVERSION_61)
			ret = __rep_fileinfo_v7_marshal(env,
			    (__rep_fileinfo_v7_args *)tmpfp, buf,
			    msgsz, &len);
		else
			ret = __rep_fileinfo_marshal(env,
			    tmpfp, buf, msgsz, &len);
		if (ret == 0) {
			DB_INIT_DBT(max_pg_dbt, buf, len);
			DB_ASSERT(env, len == max_pg_dbt.size);
			(void)__rep_send_message(env, master,
			    REP_PAGE_REQ, NULL, &max_pg_dbt, 0, flags);
		}
	} else
		(void)__rep_send_message(env, DB_EID_BROADCAST,
		    REP_MASTER_REQ, NULL, NULL, 0, 0);

	__os_free(env, buf);
err:
	if (alloc)
		__os_free(env, tmpfp);
	return (ret);
}

/*
 * __rep_blob_rereq -
 *
 * Re-request lost blob messages, such as REP_BLOB_CHUNK_REQ, REP_BLOB_ALL_REQ,
 * or REP_BLOB_UPDATE_REQ.  Note that the blob chunk gap database contains
 * descriptions of the blob chunks that we are expecting to arrive.
 *
 * Assumes the caller holds mtx_clientdb and rep_mutex.
 *
 * PUBLIC: int __rep_blob_rereq __P((ENV *, REP *, u_int32_t));
 */
int
__rep_blob_rereq(env, rep, gapflags)
	ENV *env;
	REP *rep;
	u_int32_t gapflags;
{
	DB_REP *db_rep;
	DB_THREAD_INFO *ip;
	REGINFO *infop;
	__rep_fileinfo_args *rfp;
	db_seq_t blob_fid;
	int master, ret;
	u_int32_t count;

	db_rep = env->rep_handle;
	infop = env->reginfo;
	rfp = NULL;
	ret = 0;

	/* First check if the master is around to answer the re-request. */
	master = rep->master_id;
	if (master == DB_EID_INVALID) {
		(void)__rep_send_message(env,
		    DB_EID_BROADCAST, REP_MASTER_REQ, NULL, NULL, 0, 0);
		goto err;
	}

	if (db_rep->blob_dbp == NULL &&
	    (ret = __rep_client_dbinit(env, 0, REP_BLOB)) != 0) {
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "REP_BLOB_CHUNK: Client_dbinit %s",
		    db_strerror(ret)));
		goto err;
	}

	/*
	 * If the gap blob id is 0 then we either lost a REP_BLOB_ALL_REQ or
	 * a REP_BLOB_UPDATE_REQ message.  Since we do not have the information
	 * to reconstruct a REP_BLOB_ALL_REQ message, reset the blob gap
	 * database and start over at the REP_BLOB_UPDATE_REQ stage.
	 *
	 * If the blob gap id is not 0, we lost a REP_BLOB_CHUNK_REQ message,
	 * so perform blob gap processing.
	 */
	ENV_GET_THREAD_INFO(env, ip);
	if (rep->gap_bl_hi_id == 0) {
		if ((ret = __db_truncate(
		    db_rep->blob_dbp, ip, NULL, &count)) != 0)
			goto err;
		rep->blob_more_files = 1;
		rep->last_blob_id = rep->prev_blob_id;
		rep->last_blob_sid = rep->prev_blob_sid;
		STAT_INC(env, rep, ext_update_rereq,
		    rep->stat.st_ext_update_rereq, rep->master_id);
		/*
		 * Always direct all REP_BLOB_UPDATE_REQ and REP_BLOB_ALL_REQ
		 * messages to go to the master in case of a re-request, to
		 * avoid an infinite loop of rerequests if the client is in
		 * recovery.
		 */
		gapflags |= REP_GAP_REREQUEST;
	}

	GET_CURINFO(rep, infop, rfp);
	GET_LO_HI(env, rfp->blob_fid_lo, rfp->blob_fid_hi, blob_fid, ret);
	if (ret != 0)
		goto err;
	/*
	 * If there are entries in the blob gap database, __rep_blobdone
	 * will perform gap processing, otherwise it will send
	 * a REP_BLOB_UPDATE_REQ.
	 */
	ret = __rep_blobdone(env, master, ip, rep, blob_fid, 1, gapflags);

err:
	return (ret);
}

/*
 * __rep_finfo_alloc -
 *	Allocate and initialize a fileinfo structure.
 *
 * PUBLIC: int __rep_finfo_alloc __P((ENV *, __rep_fileinfo_args *,
 * PUBLIC:     __rep_fileinfo_args **));
 */
int
__rep_finfo_alloc(env, rfpsrc, rfpp)
	ENV *env;
	__rep_fileinfo_args *rfpsrc, **rfpp;
{
	__rep_fileinfo_args *rfp;
	size_t size;
	int ret;
	void *dirp, *infop, *uidp;

	/*
	 * Allocate enough for the structure and the DBT data areas.
	 */
	size = sizeof(__rep_fileinfo_args) + rfpsrc->uid.size +
	    rfpsrc->info.size + rfpsrc->dir.size;
	if ((ret = __os_malloc(env, size, &rfp)) != 0)
		return (ret);

	/*
	 * Copy the structure itself, and then set the DBT data pointers
	 * to their space and copy the data itself as well.
	 */
	memcpy(rfp, rfpsrc, sizeof(__rep_fileinfo_args));
	uidp = (u_int8_t *)rfp + sizeof(__rep_fileinfo_args);
	rfp->uid.data = uidp;
	memcpy(uidp, rfpsrc->uid.data, rfpsrc->uid.size);

	infop = (u_int8_t *)uidp + rfpsrc->uid.size;
	rfp->info.data = infop;
	memcpy(infop, rfpsrc->info.data, rfpsrc->info.size);

	dirp = (u_int8_t *)infop + rfpsrc->info.size;
	if (rfpsrc->dir.size > 0) {
		rfp->dir.data = dirp;
		memcpy(dirp, rfpsrc->dir.data, rfpsrc->dir.size);
	} else
		rfp->dir.data = NULL;
	*rfpp = rfp;
	return (ret);
}

/*
 * __rep_log_setup -
 *	We know our first LSN and need to reset the log subsystem
 *	to get our logs set up for the proper file.
 */
static int
__rep_log_setup(env, rep, file, version, lsnp)
	ENV *env;
	REP *rep;
	u_int32_t file;
	u_int32_t version;
	DB_LSN *lsnp;
{
	DB_LOG *dblp;
	DB_LSN lsn;
	DB_TXNMGR *mgr;
	DB_TXNREGION *region;
	LOG *lp;
	int ret;

	dblp = env->lg_handle;
	lp = dblp->reginfo.primary;
	mgr = env->tx_handle;
	region = mgr->reginfo.primary;

	/*
	 * Set up the log starting at the file number of the first LSN we
	 * need to get from the master.
	 */
	LOG_SYSTEM_LOCK(env);
	if ((ret = __log_newfile(dblp, &lsn, file, version)) == 0 &&
	    lsnp != NULL)
		*lsnp = lsn;
	LOG_SYSTEM_UNLOCK(env);

	/*
	 * We reset first_lsn to the lp->lsn.  We were given the LSN of
	 * the checkpoint and we now need the LSN for the beginning of
	 * the file, which __log_newfile conveniently set up for us
	 * in lp->lsn.
	 */
	rep->first_lsn = lp->lsn;
	TXN_SYSTEM_LOCK(env);
	ZERO_LSN(region->last_ckp);
	TXN_SYSTEM_UNLOCK(env);
	return (ret);
}

/*
 * __rep_queue_filedone -
 *	Determine if we're really done getting the pages for a queue file.
 *	Queue is handled in several steps.
 *	1.  First we get the meta page only.
 *	2.  We use the meta-page information to figure out first and last
 *	    page numbers (and if queue wraps, first can be > last.
 *	3.  If first < last, we do a REP_PAGE_REQ for all pages.
 *	4.  If first > last, we REP_PAGE_REQ from first -> max page number.
 *	    Then we'll ask for page 1 -> last.
 *
 * This function can return several things:
 *	DB_REP_PAGEDONE - if we're done with this file.
 *	0 - if we're not done with this file.
 *	error - if we get an error doing some operations.
 *
 * This function will open a dbp handle to the queue file.  This is needed
 * by most of the QAM macros.  We'll open it on the first pass through
 * here and we'll close it whenever we decide we're done.
 */
static int
__rep_queue_filedone(env, ip, rep, rfp)
	ENV *env;
	DB_THREAD_INFO *ip;
	REP *rep;
	__rep_fileinfo_args *rfp;
{
#ifndef HAVE_QUEUE
	COMPQUIET(ip, NULL);
	COMPQUIET(rep, NULL);
	COMPQUIET(rfp, NULL);
	return (__db_no_queue_am(env));
#else
	DB *queue_dbp;
	DB_REP *db_rep;
	db_pgno_t first, last;
	u_int32_t flags;
	int empty, ret, t_ret;

	db_rep = env->rep_handle;
	ret = 0;
	queue_dbp = NULL;
	if (db_rep->queue_dbc == NULL) {
		/*
		 * We need to do a sync here so that the open
		 * can find the file and file id.
		 */
		if ((ret = __memp_sync_int(env, NULL, 0,
		    DB_SYNC_CACHE | DB_SYNC_INTERRUPT_OK, NULL, NULL)) != 0)
			goto out;
		if ((ret =
		    __db_create_internal(&queue_dbp, env, 0)) != 0)
			goto out;
		flags = DB_NO_AUTO_COMMIT |
		    (F_ISSET(env, ENV_THREAD) ? DB_THREAD : 0);
		/*
		 * We need to check whether this is in-memory so that we pass
		 * the name correctly as either the file or the database name.
		 */
		if ((ret = __db_open(queue_dbp, ip, NULL,
		    FLD_ISSET(rfp->db_flags, DB_AM_INMEM) ? NULL :
			rfp->info.data,
		    FLD_ISSET(rfp->db_flags, DB_AM_INMEM) ? rfp->info.data :
			NULL,
		    DB_QUEUE, flags, 0, PGNO_BASE_MD)) != 0)
			goto out;

		if ((ret = __db_cursor(queue_dbp,
		    ip, NULL, &db_rep->queue_dbc, 0)) != 0)
			goto out;
	} else
		queue_dbp = db_rep->queue_dbc->dbp;

	if ((ret = __queue_pageinfo(queue_dbp,
	    &first, &last, &empty, 0, 0)) != 0)
		goto out;
	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "Queue fileinfo: first %lu, last %lu, empty %d",
	    (u_long)first, (u_long)last, empty));
	/*
	 * We can be at the end of 3 possible states.
	 * 1.  We have received the meta-page and now need to get the
	 *     rest of the pages in the database.
	 * 2.  We have received from first -> max_pgno.  We might be done,
	 *     or we might need to ask for wrapped pages.
	 * 3.  We have received all pages in the file.  We're done.
	 */
	if (rfp->max_pgno == 0) {
		/*
		 * We have just received the meta page.  Set up the next
		 * pages to ask for and check if the file is empty.
		 */
		if (empty)
			goto out;
		if (first > last) {
			rfp->max_pgno =
			    QAM_RECNO_PAGE(db_rep->queue_dbc->dbp, UINT32_MAX);
		} else
			rfp->max_pgno = last;
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Queue fileinfo: First req: first %lu, last %lu",
		    (u_long)first, (u_long)rfp->max_pgno));
		goto req;
	} else if (rfp->max_pgno != last) {
		/*
		 * If max_pgno != last that means we're dealing with a
		 * wrapped situation.  Request next batch of pages.
		 * Set npages to 1 because we already have page 0, the
		 * meta-page, now we need pages 1-max_pgno.
		 */
		first = 1;
		rfp->max_pgno = last;
		RPRINT(env, (env, DB_VERB_REP_SYNC,
		    "Queue fileinfo: Wrap req: first %lu, last %lu",
		    (u_long)first, (u_long)last));
req:
		/*
		 * Since we're simulating a "gap" to resend new PAGE_REQ
		 * for this file, we need to set waiting page to last + 1
		 * so that we'll ask for all from ready_pg -> last.
		 */
		rep->npages = first;
		rep->ready_pg = first;
		rep->waiting_pg = rfp->max_pgno + 1;
		rep->max_wait_pg = PGNO_INVALID;
		ret = __rep_pggap_req(env, rep, rfp, 0);
		return (ret);
	}
	/*
	 * max_pgno == last
	 * If we get here, we have all the pages we need.
	 * Close the dbp and return.
	 */
out:
	if (db_rep->queue_dbc != NULL &&
	    (t_ret = __dbc_close(db_rep->queue_dbc)) != 0 && ret == 0)
		ret = t_ret;
	db_rep->queue_dbc = NULL;

	if (queue_dbp != NULL &&
	    (t_ret = __db_close(queue_dbp, NULL, DB_NOSYNC)) != 0 && ret == 0)
		ret = t_ret;
	if (ret == 0)
		ret = DB_REP_PAGEDONE;
	return (ret);
#endif
}

/*
 * PUBLIC: int __rep_remove_init_file __P((ENV *));
 */
int
__rep_remove_init_file(env)
	ENV *env;
{
	DB_REP *db_rep;
	REP *rep;
	int ret;
	char *name;

	db_rep = env->rep_handle;
	rep = db_rep->region;

	/*
	 * If running in-memory replication, return without any file
	 * operations.
	 */
	if (FLD_ISSET(rep->config, REP_C_INMEM))
		return (0);

	/* Abbreviated internal init doesn't use an init file. */
	if (F_ISSET(rep, REP_F_ABBREVIATED))
		return (0);

	if ((ret = __db_appname(env,
	    DB_APP_META, REP_INITNAME, NULL, &name)) != 0)
		return (ret);
	(void)__os_unlink(env, name, 0);
	__os_free(env, name);
	return (0);
}

/*
 * Checks for the existence of the internal init flag file.  If it exists, we
 * remove all logs and databases, and then remove the flag file.  This is
 * intended to force the internal init to start over again, and thus affords
 * protection against a client crashing during internal init.  This function
 * must be called before normal recovery in order to be properly effective.
 *
 * !!!
 * This function should only be called during initial set-up of the environment,
 * before various subsystems are initialized.  It doesn't rely on the
 * subsystems' code having been initialized, and it summarily deletes files "out
 * from under" them, which might disturb the subsystems if they were up.
 *
 * PUBLIC: int __rep_reset_init __P((ENV *));
 */
int
__rep_reset_init(env)
	ENV *env;
{
	DB_FH *fhp;
	__rep_update_args *rup;
	DBT dbt;
	char *allocated_dir, *dir, *init_name;
	size_t cnt;
	u_int32_t dbtvers, fvers, zero;
	u_int8_t *next;
	int ret, t_ret;

	allocated_dir = NULL;
	rup = NULL;
	dbt.data = NULL;

	if ((ret = __db_appname(env,
	    DB_APP_META, REP_INITNAME, NULL, &init_name)) != 0)
		return (ret);

	if ((ret = __os_open(
	    env, init_name, 0, DB_OSO_RDONLY, DB_MODE_600, &fhp)) != 0) {
		if (ret == ENOENT)
			ret = 0;
		goto out;
	}

	RPRINT(env, (env, DB_VERB_REP_SYNC,
	    "Cleaning up interrupted internal init"));

	/* There are a few possibilities:
	 *   1. no init file, or less than 1 full file list
	 *   2. exactly one full file list
	 *   3. more than one, less then a second full file list
	 *   4. second file list in full
	 *
	 * In cases 2 or 4, we need to remove all logs, and then remove files
	 * according to the (most recent) file list.  (In case 1 or 3, we don't
	 * have to do anything.)
	 *
	 * The __rep_get_file_list function takes care of folding these cases
	 * into two simple outcomes.
	 *
	 * As of 4.7, the first 4 bytes are 0.  Read the first 4 bytes now.
	 * If they are non-zero it means we have an old-style init file.
	 * Otherwise, pass the file version in to rep_get_file_list.
	 */
	if ((ret = __os_read(env, fhp, &zero, sizeof(zero), &cnt)) != 0)
		goto out;
	/*
	 * If we read successfully, but not enough, then unlink the file.
	 */
	if (cnt != sizeof(zero))
		goto rm;
	if ((ret = __os_read(env,
	    fhp, &fvers, sizeof(fvers), &cnt)) != 0)
		goto out;
	else if (cnt != sizeof(fvers))
		goto rm;
	ret = __rep_get_file_list(env, fhp, &dbtvers, &dbt);
	if ((t_ret = __os_closehandle(env, fhp)) != 0 || ret != 0) {
		if (ret == 0)
			ret = t_ret;
		goto out;
	}
	if (dbt.data == NULL) {
		/*
		 * The init file did not end with an intact file list.  Since we
		 * never start log/db removal without an intact file list
		 * sync'ed to the init file, this must mean we don't have any
		 * partial set of files to clean up.  So all we need to do is
		 * remove the init file.
		 */
		goto rm;
	}

	/* Remove all log files. */
	if (env->dbenv->db_log_dir == NULL)
		dir = env->db_home;
	else {
		if ((ret = __db_appname(env,
		    DB_APP_NONE, env->dbenv->db_log_dir, NULL, &dir)) != 0)
			goto out;
		allocated_dir = dir;
	}

	if ((ret = __rep_remove_by_prefix(env,
	    dir, LFPREFIX, sizeof(LFPREFIX)-1, DB_APP_LOG)) != 0)
		goto out;

	/*
	 * Remove databases according to the list, and queue extent files by
	 * searching them out on a walk through the data_dir's.
	 */
	if ((ret = __rep_update_unmarshal(env,
	    &rup, dbt.data, dbt.size, &next)) != 0)
		goto out;
	if ((ret = __rep_unlink_by_list(env, dbtvers,
	    next, dbt.size, rup->num_files)) != 0)
		goto out;

	/* Here, we've established that the file exists. */
rm:	(void)__os_unlink(env, init_name, 0);
out:	if (rup != NULL)
		__os_free(env, rup);
	if (allocated_dir != NULL)
		__os_free(env, allocated_dir);
	if (dbt.data != NULL)
		__os_free(env, dbt.data);

	__os_free(env, init_name);
	return (ret);
}

/*
 * Reads the last fully intact file list from the init file.  If the file ends
 * with a partial list (or is empty), we're not interested in it.  Lack of a
 * full file list is indicated by a NULL dbt->data.  On success, the list is
 * returned in allocated space, which becomes the responsibility of the caller.
 *
 * The file format is a u_int32_t buffer length, in native format, followed by
 * the file list itself, in the same format as in an UPDATE message (though
 * many parts of it in this case are meaningless).
 */
static int
__rep_get_file_list(env, fhp, dbtvers, dbt)
	ENV *env;
	DB_FH *fhp;
	u_int32_t *dbtvers;
	DBT *dbt;
{
#ifdef HAVE_REPLICATION_THREADS
	DBT mgrdbt;
#endif
	u_int32_t length, mvers;
	size_t cnt;
	int i, ret;

	/* At most 2 file lists: old and new. */
	dbt->data = NULL;
	length = 0;
#ifdef HAVE_REPLICATION_THREADS
	mgrdbt.data = NULL;
#endif
	for (i = 1; i <= 2; i++) {
		if ((ret = __os_read(env, fhp, &mvers,
		    sizeof(mvers), &cnt)) != 0)
			goto err;
		if (cnt == 0 && dbt->data != NULL)
			break;
		if (cnt != sizeof(mvers))
			goto err;
		if ((ret = __os_read(env,
		    fhp, &length, sizeof(length), &cnt)) != 0)
			goto err;

		/*
		 * Reaching the end here is fine, if we've been through at least
		 * once already.
		 */
		if (cnt == 0 && dbt->data != NULL)
			break;
		if (cnt != sizeof(length))
			goto err;

		if ((ret = __os_realloc(env,
		    (size_t)length, &dbt->data)) != 0)
			goto err;

		if ((ret = __os_read(
		    env, fhp, dbt->data, length, &cnt)) != 0 ||
		    cnt != (size_t)length)
			goto err;
	}

#ifdef HAVE_REPLICATION_THREADS
	if (i == 3) {
		if ((ret = __os_read(env, fhp,
		    &mgrdbt.size, sizeof(mgrdbt.size), &cnt)) != 0)
			goto err;
		if (cnt == 0)
			goto absent;
		if (cnt != sizeof(mgrdbt.size))
			goto err;
		if ((ret = __os_malloc(env,
		    (size_t)mgrdbt.size, &mgrdbt.data)) != 0)
			goto err;
		if ((ret = __os_read(env, fhp,
		    mgrdbt.data, mgrdbt.size, &cnt)) != 0 &&
		    cnt != (size_t)mgrdbt.size)
			goto err;
		/* Repmgr takes ownership of the allocated memory. */
		if ((ret = __repmgr_init_restore(env, &mgrdbt)) != 0)
			goto err;
	}
absent:
#endif

	*dbtvers = mvers;
	dbt->size = length;
	return (0);

err:
#ifdef HAVE_REPLICATION_THREADS
	if (mgrdbt.data != NULL)
		__os_free(env, mgrdbt.data);
#endif
	/*
	 * Note that it's OK to get here with a zero value in 'ret': it means we
	 * read less than we expected, and dbt->data == NULL indicates to the
	 * caller that we don't have an intact list.
	 */
	if (dbt->data != NULL)
		__os_free(env, dbt->data);
	dbt->data = NULL;
	return (ret);
}

/*
 * Removes every file in a given directory that matches a given prefix.  Notice
 * how similar this is to __rep_walk_dir.
 */
static int
__rep_remove_by_prefix(env, dir, prefix, pref_len, appname)
	ENV *env;
	const char *dir;
	const char *prefix;
	size_t pref_len;
	APPNAME appname;	/* What kind of name. */
{
	char *namep, **names;
	int cnt, i, ret;

	if ((ret = __os_dirlist(env, dir, 0, &names, &cnt)) != 0)
		return (ret);
	for (i = 0; i < cnt; i++) {
		if (strncmp(names[i], prefix, pref_len) == 0) {
			if ((ret = __db_appname(env,
			    appname, names[i], NULL, &namep)) != 0)
				goto out;
			(void)__os_unlink(env, namep, 0);
			__os_free(env, namep);
		}
	}
out:	__os_dirfree(env, names, cnt);
	return (ret);
}

/*
 * Removes database files according to the contents of a list.
 *
 * This function must support removal either during environment creation, or
 * when an internal init is reset in the middle.  This means it must work
 * regardless of whether underlying subsystems are initialized.  However, it may
 * assume that databases are not open.  That means there is no REP!
 */
static int
__rep_unlink_by_list(env, version, files, size, count)
	ENV *env;
	u_int32_t version;
	u_int8_t *files;
	u_int32_t size;
	u_int32_t count;
{
	DB_ENV *dbenv;
	char **ddir, *dir;
	int ret;

	dbenv = env->dbenv;

	if ((ret = __rep_walk_filelist(env, version,
	    files, size, count, __rep_unlink_file, NULL)) != 0)
		goto out;

	/* Notice how similar this code is to __rep_find_dbs. */
	if (dbenv->db_data_dir == NULL)
		ret = __rep_remove_by_prefix(env, env->db_home,
		    QUEUE_EXTENT_PREFIX, sizeof(QUEUE_EXTENT_PREFIX) - 1,
		    DB_APP_DATA);
	else {
		for (ddir = dbenv->db_data_dir; *ddir != NULL; ++ddir) {
			if ((ret = __db_appname(env,
			    DB_APP_NONE, *ddir, NULL, &dir)) != 0)
				break;
			ret = __rep_remove_by_prefix(env, dir,
			    QUEUE_EXTENT_PREFIX, sizeof(QUEUE_EXTENT_PREFIX)-1,
			    DB_APP_DATA);
			__os_free(env, dir);
			if (ret != 0)
				break;
		}
	}

out:
	return (ret);
}

static int
__rep_unlink_file(env, rfp, unused)
	ENV *env;
	__rep_fileinfo_args *rfp;
	void *unused;
{
	char *namep;
	int ret;

	COMPQUIET(unused, NULL);

	if ((ret = __db_appname(env,
	    DB_APP_DATA, rfp->info.data, NULL, &namep)) == 0) {
		(void)__os_unlink(env, namep, 0);
		__os_free(env, namep);
	}
	return (ret);
}

static int
__rep_remove_by_list(env, rfp, unused)
	ENV *env;
	__rep_fileinfo_args *rfp;
	void *unused;
{
	int ret;

	COMPQUIET(unused, NULL);

	if ((ret = __rep_remove_file(env, rfp, NULL)) == ENOENT) {
		/*
		 * If the file already doesn't exist, that's perfectly
		 * OK.  This can easily happen if we're cleaning up an
		 * interrupted internal init, and we only got part-way
		 * through the list of files.
		 */
		ret = 0;
	}
	return (ret);
}

static int
__rep_walk_filelist(env, version, files, size, count, fn, arg)
	ENV *env;
	u_int32_t version;
	u_int8_t *files;
	u_int32_t size;
	u_int32_t count;
	FILE_WALK_FN *fn;
	void *arg;
{
	__rep_fileinfo_args *rfp, rf;
	__rep_fileinfo_v6_args *rfpv6;
	__rep_fileinfo_v7_args *rfpv7;
	u_int8_t *next;
	int ret;
	void *rffree;

	ret = 0;
	rfp = NULL;
	rfpv6 = NULL;
	rffree = NULL;
	while (count-- > 0) {
		/*
		 * Build a current struct by copying in the older
		 * version struct and then setting up the new fields.
		 * This is safe because all old fields are in the
		 * same location in the current struct.
		 */
		if (version < DB_REPVERSION_53) {
			if ((ret = __rep_fileinfo_v6_unmarshal(env,
			    &rfpv6, files, size, &next)) != 0)
				break;
			memcpy(&rf, rfpv6, sizeof(__rep_fileinfo_v6_args));
			rf.dir.data = NULL;
			rf.dir.size = 0;
			rf.blob_fid_lo = rf.blob_fid_hi = 0;
			rfp = &rf;
			rffree = rfpv6;
		} else if (version < DB_REPVERSION_61) {
			if ((ret = __rep_fileinfo_v7_unmarshal(env,
			    &rfpv7, files, size, &next)) != 0)
				break;
			memcpy(&rf, rfpv7, sizeof(__rep_fileinfo_v7_args));
			rf.blob_fid_lo = rf.blob_fid_hi = 0;
			rfp = &rf;
			rffree = rfpv7;
		} else {
			if ((ret = __rep_fileinfo_unmarshal(env,
			    &rfp, files, size, &next)) != 0)
				break;
			rffree = rfp;
		}
		size -= (u_int32_t)(next - files);
		files = next;

		if ((ret = (*fn)(env, rfp, arg)) != 0)
			break;
		__os_free(env, rffree);
		rfp = NULL;
		rfpv6 = NULL;
		rffree = NULL;
	}

	if (rffree != NULL)
		__os_free(env, rffree);
	return (ret);
}

/*
 * Initializes a FILE_LIST_CTX structure.
 *
 * Pass in a non-zero value for update_space to reserve space for
 * update_args in the context's buffer.
 */
static int
__rep_init_file_list_context(env, version, flags, update_space, context)
	ENV *env;
	u_int32_t version;
	u_int32_t flags;
	int update_space;
	FILE_LIST_CTX *context;
{
	int ret;

	if ((ret = __os_calloc(env, 1, MEGABYTE, &context->buf)) != 0)
		return (ret);
	context->size = MEGABYTE;
	context->count = 0;
	context->version = version;
	context->flags = flags;
	/* Reserve space for update_args. */
	if (update_space)
		context->fillptr = FIRST_FILE_PTR(context->buf);
	else
		context->fillptr = context->buf;
	return (ret);
}
