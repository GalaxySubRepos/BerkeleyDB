/*-
 * Copyright (c) 2006, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file EXAMPLES-LICENSE for license information.
 *
 * $Id$
 */

/* User-specified role an environment should play in the replication group. */
typedef enum { MASTER, CLIENT, UNKNOWN } ENV_ROLE;

/* User-specified information about a replication site. */
typedef struct {
	char *host;		/* Host name. */
	u_int32_t port;		/* Port on which to connect to this site. */
	int peer;		/* Whether remote site is repmgr peer. */
	int creator;		/* Whether local site is group creator. */
} repsite_t;

/* Data used for common replication setup. */
typedef struct {
	const char *progname;
	char *home;
	int nsites;
	int remotesites;
	ENV_ROLE role;
	repsite_t self;
	repsite_t *site_list;
} SETUP_DATA;

/* Data shared by both repmgr and base versions of this program. */
typedef struct {
	int is_master;
	int app_finished;
	int in_client_sync;
	int is_repmgr;
} SHARED_DATA;

/* Arguments for support threads. */
typedef struct {
	DB_ENV *dbenv;
	SHARED_DATA *shared;
} supthr_args;

/*
 * Per-thread Replication Manager structure to associate a PERM_FAILED event
 * with its originating transaction.
 */
typedef struct {
	char *thread_name;
	int flag;
} permfail_t;

/* Portability macros for basic threading & timing */
#ifdef _WIN32
#define	WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define	snprintf		_snprintf
#define	sleep(s)		Sleep(1000 * (s))

extern int getopt(int, char * const *, const char *);

typedef HANDLE thread_t;
#define	thread_create(thrp, attr, func, arg)				   \
    (((*(thrp) = CreateThread(NULL, 0,					   \
	(LPTHREAD_START_ROUTINE)(func), (arg), 0, NULL)) == NULL) ? -1 : 0)
#define	thread_join(thr, statusp)					   \
    ((WaitForSingleObject((thr), INFINITE) == WAIT_OBJECT_0) &&		   \
    GetExitCodeThread((thr), (LPDWORD)(statusp)) ? 0 : -1)

/* Thread-specific data key for PERM_FAILED structure for Windows. */
extern DWORD permfail_key;
/* Thread local storage routine definitions for Windows. */
#define thread_key_create(keyp) ((*keyp = TlsAlloc()) ==			\
    TLS_OUT_OF_INDEXES ? (int)GetLastError() : 0)
#define thread_key_delete(key) (TlsFree(key) ? 0 : (int)GetLastError())
#define thread_setspecific(key, value) (TlsSetValue(key, value) ? 0 :	\
    (int)GetLastError())
#define thread_getspecific(key) TlsGetValue(key)

#else /* !_WIN32 */
#include <sys/time.h>
#include <pthread.h>

typedef pthread_t thread_t;
#define	thread_create(thrp, attr, func, arg)				   \
    pthread_create((thrp), (attr), (func), (arg))
#define	thread_join(thr, statusp) pthread_join((thr), (statusp))

/* Thread-specific data key for PERM_FAILED structure for Posix. */
extern pthread_key_t permfail_key;
/* Thread local storage routine definitions for Posix. */
#define thread_key_create(keyp) pthread_key_create(keyp, NULL)
#define thread_key_delete(key) pthread_key_delete(key)
#define thread_setspecific(key, value) pthread_setspecific(key, value)
#define thread_getspecific(key) pthread_getspecific(key)

#endif

void *checkpoint_thread __P((void *));
int common_rep_setup __P((DB_ENV *, int, char *[], SETUP_DATA *));
int create_env __P((const char *, DB_ENV **));
int doloop __P((DB_ENV *, SHARED_DATA *));
int env_init __P((DB_ENV *, const char *));
int finish_support_threads __P((thread_t *, thread_t *));
void *log_archive_thread __P((void *));
int start_support_threads __P((DB_ENV *, supthr_args *, thread_t *,
    thread_t *));
void usage __P((const int, const char *));
