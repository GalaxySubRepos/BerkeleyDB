#!/bin/sh

TOP=`dirname $0`
TOP=`cd $TOP && /bin/pwd`
SQLITE=$TOP/../../lang/sql/sqlite
OS_NAME=`uname -s`

# Excluded Tests
#
# thread1.test - Reace condition can lead to hangs.
#
BDB_TESTS_PASSING="\
affinity2.test
affinity3.test
aggerror.test
alter.test
alter3.test
alter4.test
altermalloc.test
analyze.test
analyze3.test
analyze4.test
analyze5.test
analyze6.test
analyze7.test
analyzeC.test
analyzer1.test
async.test
async2.test
async3.test
async4.test
async5.test
attach4.test
autoanalyze1.test
autovacuum.test
auth.test
auth2.test
auth3.test
autoinc.test
autoindex2.test
autoindex3.test
autoindex4.test
autovacuum_ioerr2.test
backup2.test
backup_malloc.test
badutf.test
badutf2.test
bdb_deadlock.test
bdb_dupset.test
bdb_logsize.test
bdb_exclusive.test
bdb_inmem_memleak.test
bdb_mvcc.test
bdb_persistent_pragma.test
bdb_pragmas.test
bdb_rdonly.test
bdb_replication.test
bdb_sequence.test
bdb_sqlthreadtest
bestindex1.test
between.test
bigrow.test
bigsort.test
bind.test
bindxfer.test
bitvec.test
blob.test
boundary1.test
boundary2.test
boundary3.test
boundary4.test
btree01.test
btree02.test
capi3d.test
cast.test
check.test
closure01.test
coalesce.test
collate1.test
collate2.test
collate3.test
collate4.test
collate5.test
collate6.test
collate7.test
collate8.test
collate9.test
collateA.test
collateB.test
colmeta.test
colname.test
cost.test
count.test
createtab.test
cse.test
csv01.test
cursorhint2.test
cursorhint.test
date.test
default.test
delete.test
delete2.test
delete3.test
delete4.test
descidx1.test
descidx2.test
descidx3.test
distinctagg.test
e_blobbytes.test
e_blobopen.test
e_blobwrite.test
e_changes.test
e_createtable.test
e_droptrigger.test
e_insert.test
e_reindex.test
e_resolve.test
e_select.test
e_select2.test
e_totalchanges.test
e_update.test
enc.test
enc3.test
enc4.test
eqp.test
eval.test
exec.test
exists.test
expr.test
fkey1.test
fkey2.test
fkey3.test
fkey8.test
fkey_malloc.test
func.test
func2.test
func3.test
fuzz2.test
fuzzer1.test
fuzzer2.test
fuzz-oss1.test
gcfault.test
hidden.test
hook.test
hook2.test
ieee754.test
icu.test
in.test
in2.test
in3.test
in4.test
incrblob.test
incrblob2.test
incrblob3.test
incrblob4.test
incrblob_err.test
incrvacuum.test
incrvacuum2.test
incrvacuum_ioerr.test
index.test
index2.test
index3.test
indexedby.test
indexexpr1.test
insert.test
insert2.test
insert3.test
insert4.test
insert5.test
instrfault.test
intarray.test
interrupt.test
intpkey.test
join.test
join2.test
join3.test
join4.test
join5.test
join6.test
json101.test
json102.test
json103.test
json104.test
keyword1.test
lastinsert.test
laststmtchanges.test
like.test
like2.test
like3.test
limit.test
limit2.test
loadext.test
loadext2.test
lookaside.test
malloc4.test
malloc6.test
malloc7.test
malloc8.test
malloc9.test
mallocA.test
mallocB.test
mallocD.test
mallocE.test
mallocF.test
mallocG.test
mallocH.test
mallocJ.test
mallocL.test
mallocM.test
manydb.test
mem5.test
memdb.test
memsubsys2.test
minmax.test
minmax2.test
minmax3.test
minmax4.test
misc2.test
misc3.test
misc4.test
misc6.test
misc8.test
misuse.test
nan.test
notify1.test
notify2.test
notnull.test
null.test
numindex1.test
offset1.test
openv2.test
orderby6.test
orderby8.test
orderby9.test
ovfl.test
pagesize.test
parser1.test
printf.test
ptrchng.test
quote.test
randexpr1.test
rbu.test
rdonly.test
reindex.test
rollback.test
rowhash.test
rowid.test
rowvalue.test
rowvalue2.test
rowvalue3.test
rowvalue4.test
rowvalue5.test
rowvalue6.test
rowvalue7.test
rowvalue8.test
rowvalue9.test
rowvaluefault.test
rtree.test
savepoint2.test
savepoint5.test
savepoint7.test
savepointfault.test
schema.test
schema2.test
schema4.test
schema5.test
securedel.test
select1.test
select2.test
select3.test
select4.test
select5.test
select6.test
select7.test
select8.test
select9.test
selectA.test
selectB.test
selectC.test
selectF.test
selectG.test
server1.test
session.test
shared2.test
shared3.test
shared4.test
shared6.test
shared7.test
sharedB.test
sidedelete.test
snapshot.test
snapshot2.test
snapshot_fault.test
sort.test
sort2.test
sort3.test
sort4.test
sqldiff1.test
sqllog.test
sqllimits1.test
subquery.test
subselect.test
substr.test
subtype1.test
tabfunc01.test
table.test
tempdb.test
temptable.test
temptable3.test
temptrigger.test
thread001.test
thread003.test
thread004.test
thread005.test
thread2.test
tkt-2a5629202f.test
tkt-38cb5df375.test
tkt-3998683a16.test
tkt-3a77c9714e.test
tkt-4ef7e3cfca.test
tkt-5e10420e8d.test
tkt-385a5b56b9.test
tkt-752e1646fc.test
tkt-80ba201079.test
tkt-8454a207b9.test
tkt-8c63ff0ec.test
tkt-9a8b09f8e6.test
tkt-b351d95f9.test
tkt-b75a9ca6b0.test
tkt-ba7cbfaedc.test
tkt-bdc6bbbb38.test
tkt-f67b41381a.test
tkt-f7b4edec.test
tokenize.test
trace.test
trace2.test
trace3.test
trans.test
trans2.test
trans3.test
trigger1.test
trigger2.test
trigger3.test
trigger4.test
trigger5.test
trigger6.test
trigger7.test
trigger8.test
trigger9.test
triggerA.test
triggerB.test
triggerC.test
triggerD.test
triggerE.test
triggerF.test
triggerG.test
types.test
types2.test
types3.test
unique.test
unique2.test
unixexcl.test
unordered.test
update.test
userauth01.test
utf16align.test
vacuum.test
vacuum2.test
vacuum4.test
vacuummem.test
view.test
vtab1.test
vtab2.test
vtab3.test
vtab4.test
vtab5.test
vtab6.test
vtab7.test
vtab8.test
vtab9.test
vtabA.test
vtabB.test
vtabC.test
vtabD.test
vtab_alter.test
vtab_err.test
vtab_shared.test
where.test
where2.test
where3.test
where4.test
where5.test
where6.test
where7.test
where8.test
where9.test
whereA.test
whereB.test
whereC.test
whereI.test
wherelimit.test
wherefault.test
with1.test
with2.test
with3.test
without_rowid6.test
zeroblob.test"

# Tests to run with blob files enabled.  This is designed as 
# a quick sanity test, to be run manually with the command
# sh ../../test/sql/bdb-test.sh blobs 
BDB_TESTS_BLOB="\
alter.test
alter3.test
alter4.test
analyze.test
analyze7.test
async.test
attach4.test
autoinc.test
autovacuum.test
backup.test
backup2.test
bdb_logsize.test
bdb_rdonly.test
bdb_sequence.test
between.test
bigrow.test
bind.test
bindxfer.test
bitvec.test
blob.test
boundary1.test
boundary2.test
boundary3.test
boundary4.test
capi3d.test
cast.test
check.test
coalesce.test
collate1.test
collate2.test
collate3.test
collate4.test
collate5.test
collate6.test
collate7.test
collate8.test
collate9.test
collateA.test
colmeta.test
colname.test
count.test
createtab.test
cse.test
date.test
default.test
delete2.test
descidx1.test
descidx2.test
descidx3.test
distinctagg.test
e_createtable.test
e_droptrigger.test
e_insert.test
e_reindex.test
e_resolve.test
e_select.test
e_select2.test
e_update.test
enc.test
enc3.test
enc4.test
eqp.test
exec.test
exists.test
expr.test
fkey1.test
fkey2.test
fkey3.test
func.test
func2.test
func3.test
fuzz2.test
fuzz-oss1.test
hook.test
icu.test
in.test
in2.test
in3.test
in4.test
incrblob.test
incrblob2.test
incrblob4.test
incrblob_err.test
incrvacuum.test
incrvacuum2.test
index.test
index2.test
index3.test
indexedby.test
insert.test
insert2.test
insert3.test
insert4.test
insert5.test
intarray.test
interrupt.test
intpkey.test
join.test
join2.test
join3.test
join4.test
join5.test
join6.test
keyword1.test
lastinsert.test
laststmtchanges.test
like.test
like2.test
limit.test
loadext.test
loadext2.test
lookaside.test
minmax.test
minmax2.test
minmax3.test
minmax4.test
misc2.test
misc3.test
misc4.test
misc6.test
misuse.test
nan.test
notify1.test
notify2.test
notnull.test
null.test
openv2.test
pagesize.test
printf.test
ptrchng.test
quote.test
randexpr1.test
rdonly.test
reindex.test
rollback.test
rowhash.test
rowid.test
rtree.test
savepoint2.test
savepoint5.test
savepoint7.test
schema.test
schema2.test
schema4.test
schema5.test
securedel.test
select1.test
select2.test
select3.test
select4.test
select5.test
select6.test
select7.test
select8.test
select9.test
selectA.test
selectB.test
selectC.test
server1.test
shared2.test
shared3.test
shared4.test
shared6.test
shared7.test
sidedelete.test
sort.test
sqllimits1.test
subquery.test
subselect.test
substr.test
table.test
tempdb.test
temptable.test
temptrigger.test
thread001.test
thread004.test
thread005.test
thread2.test
tkt-2a5629202f.test
tkt-38cb5df375.test
tkt-3998683a16.test
tkt-3a77c9714e.test
tkt-5e10420e8d.test
tkt-385a5b56b9.test
tkt-752e1646fc.test
tkt-80ba201079.test
tkt-8454a207b9.test
tkt-b351d95f9.test
tkt-bdc6bbbb38.test
tkt-f7b4edec.test
tokenize.test
trace.test
trace2.test
trans.test
trans2.test
trans3.test
trigger1.test
trigger2.test
trigger3.test
trigger4.test
trigger5.test
trigger6.test
trigger7.test
trigger8.test
trigger9.test
triggerB.test
triggerC.test
triggerD.test
types.test
types2.test
types3.test
unique.test
unordered.test
update.test
utf16align.test
vacuum.test
vacuum2.test
vacuum4.test
view.test
vtab1.test
vtab2.test
vtab3.test
vtab4.test
vtab5.test
vtab6.test
vtab7.test
vtab8.test
vtab9.test
vtabA.test
vtabB.test
vtabC.test
vtabD.test
vtab_alter.test
vtab_err.test
vtab_shared.test
where.test
where2.test
where3.test
where4.test
where5.test
where6.test
where7.test
where8.test
where8m.test
where9.test
whereA.test
whereB.test
whereC.test
wherelimit.test
zeroblob.test"

BDB_TESTS_ERRORS="\
progress.test"

BDB_TESTS_HANGS="\
backup.test"

BDB_TESTS_ALL="$BDB_TESTS_PASSING $BDB_TESTS_ERRORS $BDB_TESTS_HANGS"

# Define extension test suites (fts3, rtree, etc)
BDB_FTS3_TESTS="\
e_fts3.test
fts3.test
fts3aux1.test
fts3comp1.test
fts3corrupt.test
fts3corrupt2.test
fts3defer.test
fts3malloc.test
fts3matchinfo.test
fts3offsets.test
fts3rnd.test"

BDB_RTREE_TESTS="\
rtree1.test
rtree2.test
rtree3.test
rtree4.test
rtree5.test
rtree6.test"

BDB_REPLICATION_TESTS="\
bdb_replication.test"

exe_suffix=""
cygwin=`uname | grep -i "cygwin"`
if [ "$cygwin" != "" ]; then
	exe_suffix=".exe"
fi

# kill tests if still running after 30 minutes
TIMEOUT=18000
alarm() { perl -e 'alarm shift; exec @ARGV' "$@"; }

# number of threads
PARALLEL=0 ; NPROCESS=1
TESTFIXTURE=./testfixture$exe_suffix
TESTLOG="test.log"
LOGPATH="./"

# Clean up any old log - do it before adjusting the path for parallel config
rm -f $TESTLOG $TESTLOG.tmp

case "$1" in
-j)	PARALLEL=1 ; NPROCESS="$2" ; TESTFIXTURE="../$TESTFIXTURE" ;
	LOGPATH="../"
	shift; shift;;
esac

case "$1" in
passing)    TEST_CASES="$BDB_TESTS_PASSING";;
blobs)      TEST_CASES="$BDB_TESTS_BLOB";;
errors)     TEST_CASES="$BDB_TESTS_ERRORS";;
hangs)      TEST_CASES="$BDB_TESTS_HANGS";;
fts3)	    TEST_CASES="$BDB_FTS3_TESTS"
	    TIMEOUT=7200
	    ;;
rtree)	    TEST_CASES="$BDB_RTREE_TESTS"
	    TIMEOUT=7200
	    ;;
replication) TEST_CASES="$BDB_REPLICATION_TESTS";;
*)       TEST_CASES="$BDB_TESTS_ALL";;
esac

trap "kill 0" INT TERM

PROCESS=0
LOG="current_test.log"
while [ $PROCESS -lt $NPROCESS ] ; do
	(if [ $PARALLEL -ne 0 ] ; then
		rm -rf TEST$PROCESS && mkdir TEST$PROCESS && cd TEST$PROCESS
	fi
	i=0
	for t in $TEST_CASES ; do
		i=`expr $i + 1`
		[ `expr $i % $NPROCESS` -ne $PROCESS ] && continue

		case "$t" in
		bdb*) tpath=$TOP/$t ;;
		*) tpath=$SQLITE/test/$t ;;
		esac

		# Adjust path for rtree tests
		if [ "$TEST_CASES" = "$BDB_RTREE_TESTS" ]; then
			tpath=$SQLITE/ext/rtree/$t
		fi

		if [ "$TEST_CASES" = "$BDB_TESTS_BLOB" ]; then
			export BDB_BLOB_SETTING=2
		fi

		# bdb_sqlthreadtest is C, not tcl. Look for it in ..
		if [ "$t" = "bdb_sqlthreadtest" ]; then
			case "$OS_NAME" in
				Win*|win*|Cygwin*|CYGWIN*|cygwin*)
				alarm $TIMEOUT ./$t > $LOG 2>&1
				;;
				*)
				(cd .. && make $t)
				alarm $TIMEOUT ../$t > $LOG 2>&1
				;;
			esac
			
		else 
			alarm $TIMEOUT $TESTFIXTURE $tpath > $LOG 2>&1
		fi

		# Detect test result
		result=`grep "errors out of" $LOG || echo "failed"`

		# Detect memory leak
		is_leaked=`grep "Unfreed memory:" $LOG || echo "no memleaks"`

		# Save log and clean environment
		cat $LOG >> $LOGPATH$TESTLOG
		rm -rf *.db*
		rm -f $LOG

		# Output summary
		echo "$t: ${result}, ${is_leaked}" | tee -a $LOGPATH$TESTLOG.tmp
	done) &
	PROCESS=`expr $PROCESS + 1`
done

wait

FAILURES=`grep -v " 0 erro" $TESTLOG.tmp`
FAIL_CNT=`echo $FAILURES | wc -l`
MEM_FAILURES=`grep "Unfreed memory" $TESTLOG.tmp`
MEMFAIL_CNT=`echo $MEM_FAILURES | wc -l`
if [ $FAIL_CNT -ne 0 -o $MEMFAIL_CNT -ne 0 ] ; then
	echo " =================== FAILURES ===================
$FAILURES
$MEM_FAILURES"
else
	echo " =================== NO FAILURES ==================="
fi
rm -f $TESTLOG.tmp

trap - INT TERM
