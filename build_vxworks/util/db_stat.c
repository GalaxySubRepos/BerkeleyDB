/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1996, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * $Id$
 */

#include "db_config.h"

#include "db_int.h"

#ifndef lint
static const char copyright[] =
    "Copyright (c) 1996, 2019 Oracle and/or its affiliates.  All rights reserved.\n";
#endif

typedef enum { T_NOTSET, T_DB,
    T_ENV, T_LOCK, T_LOG, T_MPOOL, T_MUTEX, T_REP, T_TXN } test_t;

int	 db_stat_db_init __P((DB_ENV *, char *, test_t, u_int32_t, int *));
int	 db_stat_main __P((int, char *[]));
void	 db_stat_usage __P((void));

const char *progname;

int
db_stat(args)
	char *args;
{
	int argc;
	char **argv;

	__db_util_arg("db_stat", args, &argc, &argv);
	return (db_stat_main(argc, argv) ? EXIT_FAILURE : EXIT_SUCCESS);
}

#include <stdio.h>
#define	ERROR_RETURN	ERROR

int
db_stat_main(argc, argv)
	int argc;
	char *argv[];
{
	extern char *optarg;
	extern int optind, __db_getopt_reset;
	DB_ENV	*dbenv;
	DB *dbp, *dbvp;
	test_t ttype;
	u_int32_t cache, flags, vflag;
	int ch, exitval;
	int nflag, private, resize, ret;
	char *db, *home, *p, *passwd, *region_dir, *subdb, *vopt;

	progname = __db_util_arg_progname(argv[0]);

	if ((ret = __db_util_version_check(progname)) != 0)
		return (ret);

	dbenv = NULL;
	dbp = dbvp = NULL;
	ttype = T_NOTSET;
	cache = MEGABYTE;
	flags = nflag = private = 0;
	exitval = EXIT_SUCCESS;
	db = region_dir = subdb = home = passwd = vopt = NULL;

	__db_getopt_reset = 1;
	while ((ch = getopt(argc,
	    argv, "aC:cd:Eefgh:L:lM:mNP:p:R:rS:s:tVxX:Z")) != EOF)
		switch (ch) {
		case 'a':
			LF_SET(DB_STAT_ALLOC);
			break;
		case 'C': case 'c':
			if (ttype != T_NOTSET && ttype != T_LOCK)
				goto argcombo;
			ttype = T_LOCK;
			if (ch != 'c')
				for (p = optarg; *p; ++p)
					switch (*p) {
					case 'A':
						LF_SET(DB_STAT_ALL);
						break;
					case 'c':
						LF_SET(DB_STAT_LOCK_CONF);
						break;
					case 'l':
						LF_SET(DB_STAT_LOCK_LOCKERS);
						break;
					case 'm': /* Backward compatible. */
						break;
					case 'o':
						LF_SET(DB_STAT_LOCK_OBJECTS);
						break;
					case 'p':
						LF_SET(DB_STAT_LOCK_PARAMS);
						break;
					default:
						goto usage_err;
					}
			break;
		case 'd':
			if (ttype != T_NOTSET && ttype != T_DB)
				goto argcombo;
			ttype = T_DB;
			db = optarg;
			break;
		case 'E': case 'e':
			if (ttype != T_NOTSET && ttype != T_ENV)
				goto argcombo;
			ttype = T_ENV;
			LF_SET(DB_STAT_SUBSYSTEM);
			if (ch == 'E')
				LF_SET(DB_STAT_ALL);
			break;
		case 'f':
			if (ttype != T_NOTSET && ttype != T_DB)
				goto argcombo;
			ttype = T_DB;
			LF_SET(DB_FAST_STAT);
			break;
		case 'h':
			home = optarg;
			break;
		case 'L': case 'l':
			if (ttype != T_NOTSET && ttype != T_LOG)
				goto argcombo;
			ttype = T_LOG;
			if (ch != 'l')
				for (p = optarg; *p; ++p)
					switch (*p) {
					case 'A':
						LF_SET(DB_STAT_ALL);
						break;
					default:
						goto usage_err;
					}
			break;
		case 'M': case 'm':
			if (ttype != T_NOTSET && ttype != T_MPOOL)
				goto argcombo;
			ttype = T_MPOOL;
			if (ch != 'm')
				for (p = optarg; *p; ++p)
					switch (*p) {
					case 'A':
						LF_SET(DB_STAT_ALL);
						break;
					case 'h':
						LF_SET(DB_STAT_MEMP_HASH);
						break;
					case 'm': /* Backward compatible. */
						break;
					default:
						goto usage_err;
					}
			break;
		case 'N':
			nflag = 1;
			break;
		case 'P':
			if (__db_util_arg_password(progname, 
 			    optarg, &passwd) != 0)
  				goto err;
			break;
		case 'p':
			region_dir = optarg;
			break;
		case 'R': case 'r':
			if (ttype != T_NOTSET && ttype != T_REP)
				goto argcombo;
			ttype = T_REP;
			if (ch != 'r')
				for (p = optarg; *p; ++p)
					switch (*p) {
					case 'A':
						LF_SET(DB_STAT_ALL);
						break;
					default:
						goto usage_err;
					}
			break;
		case 's':
			if (ttype != T_NOTSET && ttype != T_DB)
				goto argcombo;
			ttype = T_DB;
			subdb = optarg;
			break;
		case 'S':
			vopt = optarg;
			switch (*vopt) {
			case 'o':
				vflag = DB_NOORDERCHK;
				break;
			case 'v':
				vflag = 0;
				break;
			default:
				(void)db_stat_usage();
				goto err;
			}
			break;
		case 't':
			if (ttype != T_NOTSET) {
argcombo:			fprintf(stderr, DB_STR_A("5006",
				    "%s: illegal option combination\n",
				    "%s\n"), progname);
				goto usage_err;
			}
			ttype = T_TXN;
			break;
		case 'V':
			printf("%s\n", db_version(NULL, NULL, NULL));
			goto done;
		case 'X': case 'x':
			if (ttype != T_NOTSET && ttype != T_MUTEX)
				goto argcombo;
			ttype = T_MUTEX;
			if (ch != 'x')
				for (p = optarg; *p; ++p)
					switch (*p) {
						case 'A':
							LF_SET(DB_STAT_ALL);
							break;
						default:
							goto usage_err;
					}
			break;
		case 'Z':
			LF_SET(DB_STAT_CLEAR);
			break;
		case '?':
		default:
			goto usage_err;
		}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		goto usage_err;

	switch (ttype) {
	case T_DB:
		if (db == NULL)
			goto usage_err;
		break;
	case T_ENV:
	case T_LOCK:
	case T_LOG:
	case T_MPOOL:
	case T_MUTEX:
	case T_REP:
	case T_TXN:
		if (vopt != NULL) {
			fprintf(stderr, "usage: %s %s\n", progname,
		    "you must specify a database file to run verification.");
			goto usage_err;
		}
		break;
	case T_NOTSET:
		goto usage_err;
	}

	if (LF_ISSET(DB_STAT_ALL | DB_STAT_ALLOC) == DB_STAT_ALLOC)
		goto usage_err;

	/* Handle possible interruptions. */
	__db_util_siginit();

retry:	
	if (__db_util_env_create(&dbenv, progname, passwd, NULL) != 0)
		goto err;

	if (nflag) {
		if ((ret = dbenv->set_flags(dbenv, DB_NOLOCKING, 1)) != 0) {
			dbenv->err(dbenv, ret, "set_flags: DB_NOLOCKING");
			goto err;
		}
		if ((ret = dbenv->set_flags(dbenv, DB_NOPANIC, 1)) != 0) {
			dbenv->err(dbenv, ret, "set_flags: DB_NOPANIC");
			goto err;
		}
	}

	if (region_dir != NULL &&
	    (ret = dbenv->set_region_dir(dbenv, region_dir)) != 0) {
		dbenv->err(dbenv, ret, "region_dir");
		goto err;
	}

	/* Initialize the environment. */
	if (db_stat_db_init(dbenv, home, ttype, cache, &private) != 0)
		goto err;

	switch (ttype) {
	case T_DB:
		/* Create the DB object and open the file. */
		if ((ret = db_create(&dbp, dbenv, 0)) != 0) {
			dbenv->err(dbenv, ret, "db_create");
			goto err;
		}

		if (vopt != NULL && (db_create(&dbvp, dbenv, 0) != 0
		    || dbvp->verify(dbvp, db, subdb, stdout, vflag) != 0))
			goto err;

		/*
		 * We open the database for writing so we can update the cached
		 * statistics, but it's OK to fail, we can open read-only and
		 * proceed.
		 *
		 * Turn off error messages for now -- we can't open lots of
		 * databases read-write (for example, master databases and
		 * hash databases for which we don't know the hash function).
		 */
		dbenv->set_errfile(dbenv, NULL);
		ret = dbp->open(dbp, NULL, db, subdb, DB_UNKNOWN, 0, 0);
		dbenv->set_errfile(dbenv, stderr);
		if (ret != 0) {
			/* Handles cannot be reused after a failed DB->open. */
			(void)dbp->close(dbp, 0);
			if ((ret = db_create(&dbp, dbenv, 0)) != 0) {
				dbenv->err(dbenv, ret, "db_create");
				goto err;
			}

			if ((ret = dbp->open(dbp,
			    NULL, db, subdb, DB_UNKNOWN, DB_RDONLY, 0)) != 0) {
				dbenv->err(dbenv, ret, "DB->open: %s", db);
				goto err;
			}
		}

		/* Check if cache is too small for this DB's pagesize. */
		if (private) {
			if ((ret = __db_util_cache(dbp, &cache, &resize)) != 0)
				goto err;
			if (resize) {
				(void)dbp->close(dbp, DB_NOSYNC);
				dbp = NULL;

				(void)dbenv->close(dbenv, 0);
				dbenv = NULL;
				goto retry;
			}
		}

		if (dbp->stat_print(dbp, flags))
			goto err;
		break;
	case T_ENV:
		if (dbenv->stat_print(dbenv, flags))
			goto err;
		break;
	case T_LOCK:
		if (dbenv->lock_stat_print(dbenv, flags))
			goto err;
		break;
	case T_LOG:
		if (dbenv->log_stat_print(dbenv, flags))
			goto err;
		break;
	case T_MPOOL:
		if (dbenv->memp_stat_print(dbenv, flags))
			goto err;
		break;
	case T_MUTEX:
		if (dbenv->mutex_stat_print(dbenv, flags))
			goto err;
		break;
	case T_REP:
#ifdef HAVE_REPLICATION_THREADS
		if (dbenv->repmgr_stat_print(dbenv, flags))
			goto err;
#endif
		if (dbenv->rep_stat_print(dbenv, flags))
			goto err;
		break;
	case T_TXN:
		if (dbenv->txn_stat_print(dbenv, flags))
			goto err;
		break;
	case T_NOTSET:
		dbenv->errx(dbenv, DB_STR("5007",
		    "Unknown statistics flag"));
		goto err;
	}

	if (0) {
usage_err:	db_stat_usage();
err:		exitval = EXIT_FAILURE;
	}
done:	if (dbp != NULL && (ret = dbp->close(dbp, DB_NOSYNC)) != 0) {
		exitval = EXIT_FAILURE;
		dbenv->err(dbenv, ret, DB_STR("5008", "close"));
	}
	if (dbenv != NULL && (ret = dbenv->close(dbenv, 0)) != 0) {
		exitval = EXIT_FAILURE;
		fprintf(stderr,
		    "%s: dbenv->close: %s\n", progname, db_strerror(ret));
	}

	if (passwd != NULL)
		free(passwd);

	/* Resend any caught signal. */
	__db_util_sigresend();

	return (exitval);
}

/*
 * db_init --
 *	Initialize the environment.
 */
int
db_stat_db_init(dbenv, home, ttype, cache, is_private)
	DB_ENV *dbenv;
	char *home;
	test_t ttype;
	u_int32_t cache;
	int *is_private;
{
	u_int32_t oflags;
	int ret;

	/*
	 * If our environment open fails, and we're trying to look at a
	 * shared region, it's a hard failure.
	 *
	 * We will probably just drop core if the environment we join does
	 * not include a memory pool.  This is probably acceptable; trying
	 * to use an existing environment that does not contain a memory
	 * pool to look at a database can be safely construed as operator
	 * error, I think.
	 */
	*is_private = 0;
	if ((ret = dbenv->open(dbenv, home, DB_USE_ENVIRON, 0)) == 0)
		return (0);
	if (ret == DB_VERSION_MISMATCH)
		goto err;
	if (ttype != T_DB && ttype != T_LOG) {
		dbenv->err(dbenv, ret, "DB_ENV->open%s%s",
		    home == NULL ? "" : ": ", home == NULL ? "" : home);
		return (1);
	}

	/*
	 * We're looking at a database or set of log files and no environment
	 * exists.  Create one, but make it private so no files are actually
	 * created.  Declare a reasonably large cache so that we don't fail
	 * when reporting statistics on large databases.
	 *
	 * An environment is required to look at databases because we may be
	 * trying to look at databases in directories other than the current
	 * one.
	 */
	if ((ret = dbenv->set_cachesize(dbenv, 0, cache, 1)) != 0) {
		dbenv->err(dbenv, ret, "set_cachesize");
		return (1);
	}
	*is_private = 1;
	oflags = DB_CREATE | DB_PRIVATE | DB_USE_ENVIRON;
	if (ttype == T_DB)
		oflags |= DB_INIT_MPOOL;
	if (ttype == T_LOG)
		oflags |= DB_INIT_LOG;
	if ((ret = dbenv->open(dbenv, home, oflags, 0)) == 0)
		return (0);

	/* An environment is required. */
err:	dbenv->err(dbenv, ret, "DB_ENV->open");
	return (1);
}

void
db_stat_usage()
{
	fprintf(stderr, "usage: %s %s\n", progname,
	    "-d file [-fN] [-h home] [-P password] [-s database] [-S ov]");
	fprintf(stderr, "usage: %s %s\n\t%s\n", progname,
	    "[-cEelmrtVx] [-C Aclop]",
"[-h home] [-L A] [-M Ah] [-P password] [-p region_dir] [-R A] [-X A] [-aNZ]");
}
