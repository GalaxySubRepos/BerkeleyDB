/*-
 * Copyright (c) 1996, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 */
/*
 * Copyright (c) 1995, 1996
 *	The President and Fellows of Harvard University.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 */

#include "db_config.h"

#include "db_int.h"
#include "dbinc/blob.h"
#include "dbinc/db_page.h"
#include "dbinc/db_am.h"
#include "dbinc/txn.h"

static int __dbreg_open_file __P((ENV *,
    DB_TXN *, __dbreg_register_args *, void *));
static int __dbreg_register_recover_int __P((ENV *,
    DBT *, DB_LSN *, db_recops, void *, __dbreg_register_args *));

/*
 * PUBLIC: int __dbreg_register_recover
 * PUBLIC:     __P((ENV *, DBT *, DB_LSN *, db_recops, void *));
 */
int
__dbreg_register_recover(env, dbtp, lsnp, op, info)
	ENV *env;
	DBT *dbtp;
	DB_LSN *lsnp;
	db_recops op;
	void *info;
{
	__dbreg_register_args *argp;
	int ret;

	argp = NULL;

	if ((ret = __dbreg_register_read(env, dbtp->data, &argp)) != 0)
		goto out;

	ret = __dbreg_register_recover_int(env, dbtp, lsnp, op, info, argp);

	if (ret == 0)
		*lsnp = argp->prev_lsn;
out:	if (argp != NULL)
		__os_free(env, argp);
	return (ret);
}

/*
 * PUBLIC: int __dbreg_register_42_recover
 * PUBLIC:     __P((ENV *, DBT *, DB_LSN *, db_recops, void *));
 */
int
__dbreg_register_42_recover(env, dbtp, lsnp, op, info)
	ENV *env;
	DBT *dbtp;
	DB_LSN *lsnp;
	db_recops op;
	void *info;
{
	__dbreg_register_42_args *argp;
	__dbreg_register_args arg;
	int ret;

	argp = NULL;
	if ((ret = __dbreg_register_42_read(env, dbtp->data, &argp)) != 0)
		goto err;

	/*
	 * Databases before 6.0 cannot support blobs, so the blob_fid is 0.
	 * After 6.0 they can support blobs, so it is possible it has a non-0
	 * blob_fid, but since logging that value in dbreg_register
	 * is only used in replication, and replication does not support blobs
	 * until 6.1, this is safe.
	 */
	memcpy(&arg, argp, sizeof(__dbreg_register_42_args));
	arg.blob_fid_lo = 0;
	arg.blob_fid_hi = 0;

	ret = __dbreg_register_recover_int(env, dbtp, lsnp, op, info, &arg);

	if (ret == 0)
		*lsnp = argp->prev_lsn;
err:	if (argp != NULL)
		__os_free(env, argp);
	return (ret);
}

/*
 * Internal register recovery function for both the 42 log version and the
 * 61 log version.
 */
static int
__dbreg_register_recover_int(env, dbtp, lsnp, op, info, argp)
	ENV *env;
	DBT *dbtp;
	DB_LSN *lsnp;
	db_recops op;
	void *info;
	__dbreg_register_args *argp;
{
	DB_ENTRY *dbe;
	DB_LOG *dblp;
	DB *dbp;
	u_int32_t opcode, status;
	int do_close, do_open, do_rem, ret, t_ret;
#ifdef DEBUG_RECOVER
	DB_LOG dblog;
#endif
#ifdef	HAVE_REPLICATION
	DB_REP *db_rep;
	DELAYED_BLOB_LIST *dbl;
	int view_partial;

	dbl = NULL;
#endif

	dblp = env->lg_handle;
	dbp = NULL;
	ret = 0;

#ifdef DEBUG_RECOVER
	/*
	 * Since dbregister records tries to print info about the very files
	 * which it can open and close, it is tricky. Note that
	 * __log_print_dbreg_setup() requires a dummy DB_LOG parameter, since
	 * using the environment's real DB_LOG would mess up the real registry
	 * table. So, as with db_printlog.c, use a dummy DB_LOG. Use the same
	 * sort of cleanup code at the end, although there can be just 1 entry.
	 */
	memset(&dblog, 0, sizeof(dblog));

	REC_PRINT_DBREG(__dbreg_register_print, &dblog);
#else
	/* These are only used by the above REC_PRINT. */
	COMPQUIET(dbtp, NULL);
	COMPQUIET(lsnp, NULL);
#endif
	do_open = do_close = 0;

	opcode = FLD_ISSET(argp->opcode, DBREG_OP_MASK);
	switch (opcode) {
	case DBREG_OPEN:
	case DBREG_PREOPEN:
	case DBREG_REOPEN:
	case DBREG_XOPEN:
	case DBREG_XREOPEN:
		/*
		 * In general, we redo the open on REDO and abort on UNDO.
		 * However, a reopen is a second instance of an open of
		 * in-memory files and we don't want to close them yet
		 * on abort, so just skip that here.
		 */
		if ((DB_REDO(op) ||
		    op == DB_TXN_OPENFILES || op == DB_TXN_POPENFILES))
			do_open = 1;
		else if (opcode != DBREG_REOPEN && opcode != DBREG_XREOPEN)
			do_close = 1;
		break;
	case DBREG_CLOSE:
		if (DB_UNDO(op))
			do_open = 1;
		else
			do_close = 1;
		break;
	case DBREG_RCLOSE:
		/*
		 * DBREG_RCLOSE was generated by recover because a file was
		 * left open.  The POPENFILES pass, which is run to open
		 * files to abort prepared transactions, may not include the
		 * open for this file so we open it here.  Note that a normal
		 * CLOSE is not legal before the prepared transaction is
		 * committed or aborted.
		 */
		if (DB_UNDO(op) || op == DB_TXN_POPENFILES)
			do_open = 1;
		else
			do_close = 1;
		break;
	case DBREG_CHKPNT:
	case DBREG_XCHKPNT:
		if (DB_UNDO(op) ||
		    op == DB_TXN_OPENFILES || op == DB_TXN_POPENFILES)
			do_open = 1;
		break;
	default:
		ret = __db_unknown_path(env, "__dbreg_register_recover");
		goto out;
	}

	if (do_open) {
#ifdef	HAVE_REPLICATION
		/*
		 * Partial replication may apply at this time.  Invoke
		 * the callback if several conditions are met:
		 * - We are a view.
		 * - This is the OPENFILES pass of recovery.
		 * - The file is not a BDB owned database.
		 * - The dbreg operation is a create (id != TXN_INVALID).
		 *
		 * If the file is to be skipped, then we have to TXN_IGNORE
		 * the txnlist for that create operation.
		 */
		if (IS_VIEW_SITE(env) && op == DB_TXN_OPENFILES &&
		    (!IS_DB_FILE(argp->name.data) ||
		    IS_BLOB_META(argp->name.data)) &&
		    argp->id != TXN_INVALID) {
			db_rep = env->rep_handle;
			/*
			 * Once a view, always a view.  Must have set
			 * a callback already.
			 */
			if (db_rep->partial == NULL) {
				ret = USR_ERR(env, EINVAL);
				__db_errx(env, DB_STR("1592",
				    "Must set a view callback."));
				goto out;
			}
			if ((ret = __rep_call_partial(env,
			    argp->name.data, &view_partial, 0, &dbl)) != 0)
				goto out;
			DB_ASSERT(env, dbl == NULL);

			/*
			 * If this should not be replicated, then set
			 * the child txnlist to TXN_IGNORE.
			 */
			if (view_partial == 0 &&
			    (ret = __db_txnlist_update(env, info,
			    argp->id, TXN_IGNORE, NULL, &status, 1)) != 0)
				goto out;
		}
#endif
		/*
		 * We must open the db even if the meta page is not
		 * yet written as we may be creating subdatabase.
		 */
		if (op == DB_TXN_OPENFILES && opcode != DBREG_CHKPNT &&
		    opcode != DBREG_XCHKPNT)
			F_SET(dblp, DBLOG_FORCE_OPEN);

		/*
		 * During an abort or an open pass to recover prepared txns,
		 * we need to make sure that we use the same locker id on the
		 * open.  We pass the txnid along to ensure this.
		 */
		ret = __dbreg_open_file(env,
		    op == DB_TXN_ABORT || op == DB_TXN_POPENFILES ? argp->txnp :
		    (info == NULL ? NULL :
		    ((DB_TXNHEAD *)info)->txn), argp, info);
		if (ret == DB_PAGE_NOTFOUND && argp->meta_pgno != PGNO_BASE_MD)
			ret = USR_ERR(env, ENOENT);
		if (ret == ENOENT || ret == EINVAL) {
			/*
			 * If this is an OPEN while rolling forward, it's
			 * possible that the file was recreated since last
			 * time we got here.  In that case, we've got deleted
			 * set and probably shouldn't, so we need to check
			 * for that case and possibly retry.
			 */
			if (DB_REDO(op) && argp->txnp != 0 &&
			    dblp->dbentry[argp->fileid].deleted) {
				dblp->dbentry[argp->fileid].deleted = 0;
				ret = __dbreg_open_file(env,
				    ((DB_TXNHEAD *)info)->txn, argp, info);
				if (ret == DB_PAGE_NOTFOUND &&
				     argp->meta_pgno != PGNO_BASE_MD)
					ret = USR_ERR(env, ENOENT);
			}
			/*
			 * We treat ENOENT as OK since it's possible that
			 * the file was renamed or deleted.
			 * All other errors, we return.
			 */
			if (ret == ENOENT)
				ret = 0;
		}
		F_CLR(dblp, DBLOG_FORCE_OPEN);
	}

	if (do_close) {
		/*
		 * If we are undoing an open, or redoing a close,
		 * then we need to close the file.  If we are simply
		 * revoking then we just need to grab the DBP and revoke
		 * the log id.
		 *
		 * If the file is deleted, then we can just ignore this close.
		 * Otherwise, we should usually have a valid dbp we should
		 * close or whose reference count should be decremented.
		 * However, if we shut down without closing a file, we may, in
		 * fact, not have the file open, and that's OK.
		 */
		do_rem = 0;
		dbe = NULL;
		MUTEX_LOCK(env, dblp->mtx_dbreg);
		if (argp->fileid < dblp->dbentry_cnt) {
			/*
			 * Typically, closes should match an open which means
			 * that if this is a close, there should be a valid
			 * entry in the dbentry table when we get here,
			 * however there are exceptions.  1. If this is an
			 * OPENFILES pass, then we may have started from
			 * a log file other than the first, and the
			 * corresponding open appears in an earlier file.
			 * 2. If we are undoing an open on an abort or
			 * recovery, it's possible that we failed after
			 * the log record, but before we actually entered
			 * a handle here.
			 * 3. If we aborted an open, then we wrote a non-txnal
			 * RCLOSE into the log.  During the forward pass, the
			 * file won't be open, and that's OK.
			 */
			dbe = &dblp->dbentry[argp->fileid];
			if (dbe->dbp == NULL && !dbe->deleted) {
				/* No valid entry here. Nothing to do. */
				MUTEX_UNLOCK(env, dblp->mtx_dbreg);
				goto out;
			}

			/* We have either an open entry or a deleted entry. */
			if ((dbp = dbe->dbp) != NULL) {
				/*
				 * If we're a replication client, it's
				 * possible to get here with a dbp that
				 * the user opened, but which we later
				 * assigned a fileid to.  Be sure that
				 * we only close dbps that we opened in
				 * the recovery code or that were opened
				 * inside a currently aborting transaction
				 * but not by the recovery code.
				 */
				do_rem = (F_ISSET(dbp, DB_AM_RECOVER) ||
				    F2_ISSET(dbp, DB2_AM_EXCL)) ?
				    op != DB_TXN_ABORT :
				    op == DB_TXN_ABORT;
				MUTEX_UNLOCK(env, dblp->mtx_dbreg);
			} else if (dbe->deleted) {
				MUTEX_UNLOCK(env, dblp->mtx_dbreg);
				if ((ret = __dbreg_rem_dbentry(
				    dblp, argp->fileid)) != 0)
					goto out;
			}
		} else
			MUTEX_UNLOCK(env, dblp->mtx_dbreg);

		/*
		 * During recovery, all files are closed.  On an abort, we only
		 * close the file if we opened it during the abort
		 * (DB_AM_RECOVER set), otherwise we simply do a __db_refresh.
		 * For the close case, if remove or rename has closed the file,
		 * don't request a sync, because a NULL mpf would be a problem.
		 *
		 * If we are undoing a create we'd better discard any buffers
		 * from the memory pool.  We identify creates because the
		 * argp->id field contains the transaction containing the file
		 * create; if that id is invalid, we are not creating.
		 *
		 * On the backward pass, we need to "undo" opens even if the
		 * transaction in which they appeared committed, because we have
		 * already undone the corresponding close.  In that case, the
		 * id will be valid, but we do not want to discard buffers.
		 */
		if (do_rem && dbp != NULL) {
			if (argp->id != TXN_INVALID) {
				if ((ret = __db_txnlist_find(env,
				    info, argp->txnp->txnid, &status))
				    != DB_NOTFOUND && ret != 0)
					goto out;
				if (ret == DB_NOTFOUND || status != TXN_COMMIT)
					F_SET(dbp, DB_AM_DISCARD);
				ret = 0;
			}

			if (op == DB_TXN_ABORT) {
				if ((t_ret = __db_refresh(dbp,
				    NULL, DB_NOSYNC, NULL, 0)) != 0 && ret == 0)
					ret = t_ret;
			} else {
				if ((t_ret = __db_close(
				    dbp, NULL, DB_NOSYNC)) != 0 && ret == 0)
					ret = t_ret;
				dbe->dbp = NULL;
			}
		}
	}
out:
#ifdef DEBUG_RECOVER
	if (dblog.dbentry_cnt != 0 && (dbp = dblog.dbentry[0].dbp) != NULL) {
		/* There should be at most(exactly?) one active entry. */
		DB_ASSERT(env, dblog.dbentry[1].dbp == NULL);

		(void)__db_close(dbp, NULL, DB_NOSYNC);
	}
#endif
	return (ret);
}

/*
 * __dbreg_open_file --
 *	Called during log_register recovery.  Make sure that we have an
 *	entry in the dbentry table for this ndx.  Returns 0 on success,
 *	non-zero on error.
 */
static int
__dbreg_open_file(env, txn, argp, info)
	ENV *env;
	DB_TXN *txn;
	__dbreg_register_args *argp;
	void *info;
{
	DB *dbp;
	DB_ENTRY *dbe;
	DB_LOG *dblp;
	db_seq_t blob_file_id;
	u_int32_t id, opcode, status;
	int ret;

	dblp = env->lg_handle;
	opcode = FLD_ISSET(argp->opcode, DBREG_OP_MASK);
	ret = 0;

	/*
	 * When we're opening, we have to check that the name we are opening
	 * is what we expect.  If it's not, then we close the old file and
	 * open the new one.
	 */
	MUTEX_LOCK(env, dblp->mtx_dbreg);
	if (argp->fileid != DB_LOGFILEID_INVALID &&
	    argp->fileid < dblp->dbentry_cnt)
		dbe = &dblp->dbentry[argp->fileid];
	else
		dbe = NULL;

	if (dbe != NULL) {
		if (dbe->deleted) {
			MUTEX_UNLOCK(env, dblp->mtx_dbreg);
			return (USR_ERR(env, ENOENT));
		}

		/*
		 * At the end of OPENFILES, we may have a file open.  If this
		 * is a reopen, then we will always close and reopen.  If the
		 * open was part of a committed transaction, so it doesn't
		 * get undone.  However, if the fileid was previously used,
		 * we'll see a close that may need to get undone.  There are
		 * three ways we can detect this. 1) the meta-pgno in the
		 * current file does not match that of the open file, 2) the
		 * file uid of the current file does not match that of the
		 * previously opened file, 3) the current file is unnamed, in
		 * which case it should never be opened during recovery.
		 * It is also possible that the db open previously failed
		 * because the file was missing.  Check the DB_AM_OPEN_CALLED
		 * bit and try to open it again.
		 */
		if ((dbp = dbe->dbp) != NULL) {
			if (opcode == DBREG_REOPEN ||
			    opcode == DBREG_XREOPEN ||
			    !F_ISSET(dbp, DB_AM_OPEN_CALLED) ||
			    dbp->meta_pgno != argp->meta_pgno ||
			    argp->name.size == 0 ||
			    memcmp(dbp->fileid, argp->uid.data,
			    DB_FILE_ID_LEN) != 0) {
				MUTEX_UNLOCK(env, dblp->mtx_dbreg);
				(void)__dbreg_revoke_id(dbp, 0,
				    DB_LOGFILEID_INVALID);
				if (F_ISSET(dbp, DB_AM_RECOVER)) {
					(void)__db_close(dbp, NULL, DB_NOSYNC);
					dbe->dbp = NULL;
				}
				goto reopen;
			}

			DB_ASSERT(env, dbe->dbp == dbp);
			MUTEX_UNLOCK(env, dblp->mtx_dbreg);

			/*
			 * This is a successful open.  We need to record that
			 * in the txnlist so that we know how to handle the
			 * subtransaction that created the file system object.
			 */
			if (argp != NULL && argp->id != TXN_INVALID &&
			    (ret = __db_txnlist_update(env, info,
			    argp->id, TXN_EXPECTED, NULL, &status, 1)) != 0)
				return (ret);
			return (0);
		}
	}

	MUTEX_UNLOCK(env, dblp->mtx_dbreg);

reopen:
	/*
	 * We never re-open temporary files.  Temp files are only useful during
	 * aborts in which case the dbp was entered when the file was
	 * registered. During recovery, we treat temp files as properly deleted
	 * files, allowing the open to fail and not reporting any errors when
	 * recovery fails to get a valid dbp from __dbreg_id_to_db.
	 */
	if (argp->name.size == 0) {
		(void)__dbreg_add_dbentry(env, dblp, NULL, argp->fileid);
		return (USR_ERR(env, ENOENT));
	}

	/*
	 * We are about to pass a recovery txn pointer into the main library.
	 * We need to make sure that any accessed fields are set appropriately.
	 */
	if (txn != NULL && !F_ISSET(txn, TXN_DISPATCH)) {
		id = txn->txnid;
		memset(txn, 0, sizeof(DB_TXN));
		txn->txnid = id;
		txn->mgrp = env->tx_handle;
	}

	GET_LO_HI(env,
	    argp->blob_fid_lo, argp->blob_fid_hi, blob_file_id, ret);
	if (ret != 0)
		return (ret);
	return (__dbreg_do_open(env, txn, dblp, argp->uid.data,
	    argp->name.data, argp->ftype, argp->fileid,
	    argp->meta_pgno, info, argp->id, opcode, blob_file_id));
}
