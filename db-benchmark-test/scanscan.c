/* Scan the bench.tokudb/bench.db over and over. */

#include <db.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

DB_ENV *env;
DB *db;
DB_TXN *tid=0;
DBC *dbc;

#define STRINGIFY2(s) #s
#define STRINGIFY(s) STRINGIFY2(s)
const char *dbdir = "./bench."  STRINGIFY(DIRSUF) "/"; /* DIRSUF is passed in as a -D argument to the compiler. */;
#define TXNS
#ifdef TXNS
int env_open_flags = DB_CREATE|DB_PRIVATE|DB_INIT_MPOOL|DB_INIT_TXN|DB_INIT_LOG|DB_INIT_LOCK;
#else
int env_open_flags = DB_CREATE|DB_PRIVATE|DB_INIT_MPOOL;
#endif
char *dbfilename = "bench.db";

void setup (void) {
    int r;
    r = db_env_create(&env, 0);                                 assert(r==0);
    r = env->set_cachesize(env, 0, 127*1024*1024, 1);           assert(r==0);
    r = env->open(env, dbdir, env_open_flags, 0644);            assert(r==0);
    r = db_create(&db, env, 0);                                 assert(r==0);
#ifdef TXNS
    r = env->txn_begin(env, 0, &tid, 0);                        assert(r==0);
#endif
    r = db->open(db, tid, dbfilename, NULL, DB_BTREE, 0, 0644); assert(r==0);
    r = db->cursor(db, tid, &dbc, 0);                           assert(r==0);
}

void shutdown (void) {
    int r;
    r = dbc->c_close(dbc);                                      assert(r==0);
    r = db->close(db, 0);                                       assert(r==0);
#ifdef TXNS
    r = tid->commit(tid, 0);                                    assert(r==0);
#endif
    r = env->close(env, 0);                                     assert(r==0);
    {
	extern unsigned long toku_get_maxrss(void);
	printf("maxrss=%.2fMB\n", toku_get_maxrss()/256.0);
    }
}

double gettime (void) {
    struct timeval tv;
    int r = gettimeofday(&tv, 0);
    assert(r==0);
    return tv.tv_sec + 1e-6*tv.tv_usec;
}

void scanscan (void) {
    int r;
    int counter=0;
    for (counter=0; counter<2; counter++) {
	long long totalbytes=0;
	int rowcounter=0;
	double prevtime = gettime();
	DBT k,v;
	memset(&k, 0, sizeof(k));
	memset(&v, 0, sizeof(v));
	r = dbc->c_get(dbc, &k, &v, DB_FIRST);
	if (r!=DB_NOTFOUND) {
	    totalbytes += k.size + v.size;
	    rowcounter++;
	    assert(r==0);
	    while (1) {
		r = dbc->c_get(dbc, &k, &v, DB_NEXT);
		if (r==DB_NOTFOUND) {
		    break;
		}
		assert(r==0);
		totalbytes += k.size + v.size;
		rowcounter++;
	    }
	}
	double thistime = gettime();
	double tdiff = thistime-prevtime;
	printf("Scan %lld bytes (%d rows) in %9.6fs at %9fMB/s\n", totalbytes, rowcounter, tdiff, 1e-6*totalbytes/tdiff);
    }
}

int main (int argc, char *argv[]) {
    argc=argc;
    argv=argv;
    setup();
    scanscan();
    shutdown();
    return 0;
}
