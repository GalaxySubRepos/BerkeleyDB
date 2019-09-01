/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2016, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * $Id$
 */

#include "db_config.h"

#include "db_int.h"

#ifndef lint
static const char copyright[] =
    "Copyright (c) 2016, 2019 Oracle and/or its affiliates.  All rights reserved.\n";
#endif

int main __P((int, char *[]));
void usage __P((void));
int version_check __P((void));
int isbigendian __P((void));

const char *progname;


int
main(argc, argv)
	int argc;
	char *argv[];
{
	extern char *optarg;
	extern int optind;
	DB *dbp;
	DB_ENV *dbenv;
	int ch, exitval, lorder, ret, t_ret, verbose;
	char *home, *passwd;

	if ((progname = __db_rpath(argv[0])) == NULL)
		progname = argv[0];
	else
		++progname;

	if ((ret = version_check()) != 0)
		return (ret);

	dbenv = NULL;
	verbose = 0;
	exitval = EXIT_SUCCESS;
	home = passwd = NULL;
	lorder = isbigendian() ? 4321 : 1234;
	while ((ch = getopt(argc, argv, "bh:lP:Vv")) != EOF)
		switch (ch) {
		case 'b':
			lorder = 4321;
			break;
		case 'h':
			home = optarg;
			break;
		case 'l':
			lorder = 1234;
			break;
		case 'P':
			if (passwd != NULL) {
				fprintf(stderr, DB_STR("5131",
					"Password may not be specified twice"));
				goto err;
			}
			passwd = strdup(optarg);
			memset(optarg, 0, strlen(optarg));
			if (passwd == NULL) {
				fprintf(stderr, DB_STR_A("5018",
				    "%s: strdup: %s\n", "%s %s\n"),
				    progname, strerror(errno));
				goto err;
			}
			break;
		case 'V':
			printf("%s\n", db_version(NULL, NULL, NULL));
			goto done;
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			goto usage_err;
		}
	argc -= optind;
	argv += optind;

	if (argc <= 0)
		goto usage_err;

	/* Handle possible interruptions. */
	__db_util_siginit();

	/*
	 * Create an environment object and initialize it for error
	 * reporting.
	 */
	if ((ret = db_env_create(&dbenv, 0)) != 0) {
		fprintf(stderr, "%s: db_env_create: %s\n",
		    progname, db_strerror(ret));
		goto err;
	}

	dbenv->set_errfile(dbenv, stderr);
	dbenv->set_errpfx(dbenv, progname);

	if (passwd != NULL && (ret = dbenv->set_encrypt(dbenv,
	    passwd, DB_ENCRYPT_AES)) != 0) {
		dbenv->err(dbenv, ret, "set_passwd");
		goto err;
	}

	/*
	 * If attaching to a pre-existing environment fails, create a
	 * private one and try again.
	 */
	if ((ret = dbenv->open(dbenv, home, DB_USE_ENVIRON, 0)) != 0 &&
	    (ret == DB_VERSION_MISMATCH || ret == DB_REP_LOCKOUT ||
	    (ret = dbenv->open(dbenv, home,
	    DB_CREATE | DB_INIT_MPOOL | DB_PRIVATE | DB_USE_ENVIRON,
	    0)) != 0)) {
		dbenv->err(dbenv, ret, "DB_ENV->open");
		goto err;
	}

	for (; !__db_util_interrupted() && argv[0] != NULL; ++argv) {
		if ((ret = db_create(&dbp, dbenv, 0)) != 0) {
			fprintf(stderr,
			    "%s: db_create: %s\n", progname, db_strerror(ret));
			goto err;
		}
		dbp->set_errfile(dbp, stderr);
		dbp->set_errpfx(dbp, progname);
		if ((ret = dbp->convert(dbp, argv[0], lorder)) != 0)
			dbp->err(dbp, ret, "DB->upgrade: %s", argv[0]);
		if ((t_ret = dbp->close(dbp, 0)) != 0 && ret == 0) {
			dbenv->err(dbenv, ret, "DB->close: %s", argv[0]);
			ret = t_ret;
		}
		if (ret != 0)
			goto err;
		/*
		 * People get concerned if they don't see a success message.
		 * If verbose is set, give them one.
		 */
		if (verbose)
			printf(DB_STR_A("5145",
			    "%s: %s converted successfully to %s\n",
			    "%s %s %s\n"), progname, argv[0],
			    lorder == 1234 ? "little-endian" : "big-endian");
	}

	if (0) {
usage_err:	usage();
err:		exitval = EXIT_FAILURE;
	}
done:	if (dbenv != NULL && (ret = dbenv->close(dbenv, 0)) != 0) {
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

void
usage()
{
	fprintf(stderr, "usage: %s %s\n", progname,
	    "[-blVv] [-h home] [-P password] db_file ...");
}

int
version_check()
{
	int v_major, v_minor, v_patch;

	/* Make sure we're loaded with the right version of the DB library. */
	(void)db_version(&v_major, &v_minor, &v_patch);
	if (v_major != DB_VERSION_MAJOR || v_minor != DB_VERSION_MINOR) {
		fprintf(stderr, DB_STR_A("5020",
		    "%s: version %d.%d doesn't match library version %d.%d\n",
		    "%s %d %d %d %d\n"), progname, DB_VERSION_MAJOR,
		    DB_VERSION_MINOR, v_major, v_minor);
		return (EXIT_FAILURE);
	}
	return (0);
}

/*
 * __db_isbigendian --
 *	Return 1 if big-endian (Motorola and Sparc), not little-endian
 *	(Intel and Vax).  We do this work at run-time, rather than at
 *	configuration time so cross-compilation and general embedded
 *	system support is simpler.
 *
 * PUBLIC: int __db_isbigendian __P((void));
 */
int
isbigendian()
{
	union {					/* From Harbison & Steele.  */
		long l;
		char c[sizeof(long)];
	} u;

	u.l = 1;
	return (u.c[sizeof(long) - 1] == 1);
}