# Copyright (c) 2012, 2019 Oracle and/or its affiliates.  All rights reserved.
#
# See the file LICENSE for license information.
#
# $Id$
#
# repmgr035script - procs to use at each replication site in the
# replication manager upgrade test.
#

proc repmgr035scr_starttest { role oplist envid mydir markerdir\
    hoststr local_port remote_ports dbopt } {
	global util_path
	global repfiles_in_memory

	set repmemargs ""
	if { $repfiles_in_memory } {
		set repmemargs "-rep_inmem_files "
	}

	set bdbver [berkdb version]
	set vermaj [lindex $bdbver 0]
	set vermin [lindex $bdbver 1]
	set verpatch [lindex $bdbver 2]

	if { $vermaj > 18 || [expr $vermaj == 18 && $vermin > 1] \
	    || [expr $vermaj == 18 && $vermin == 1 && $verpatch >= 20] } {
		set sslargs [setup_repmgr_sslargs]
	} else {
		set sslargs ""
	}

	puts "set up env cmd"
	set lockmax 40000
	set logbuf [expr 16 * 1024]
	set logmax [expr $logbuf * 4]
	if { $role == "MASTER" } {
		set rolearg master
	} elseif { $role == "CLIENT" } {
		set rolearg client
	} else {
		puts "FAIL: unrecognized replication role $role"
		return
	}
	set rep_env_cmd "berkdb_env_noerr -create -home $mydir $sslargs\
	    -log_max $logmax -log_buffer $logbuf $repmemargs \
	    -lock_max_objects $lockmax -lock_max_locks $lockmax \
	    -errpfx $role -txn -data_dir DATADIR \
	    -verbose {rep on} -errfile /dev/stderr -rep -thread"

	# Change directories to where this will run.
	# !!!
	# mydir is an absolute path of the form
	# <path>/build_unix/TESTDIR/MASTERDIR or
	# <path>/build_unix/TESTDIR/CLIENTDIR.0
	#
	# So we want to run relative to the build_unix directory
	cd $mydir/../..

	puts "open repenv $rep_env_cmd"
	set repenv [eval $rep_env_cmd]
	error_check_good repenv_open [is_valid_env $repenv] TRUE

	set legacy_str ""
	set nsites_str ""
	if { [have_group_membership] } {
		# With group membership, use the legacy option to start the
		# sites in the replication group because this will work when
		# some sites are still at an older, pre-group-membership 
		# version of Berkeley DB.
		set legacy_str "legacy"
	} else {
		# When running an earlier version of Berkeley DB before
		# group membership, we must supply an nsites value.
		set nsites_str " -nsites [expr [llength $remote_ports] + 1]"
	}
	set repmgr_conf " -start $rolearg $nsites_str \
	    -local { $hoststr $local_port $legacy_str }"
	# Reduce connection retry time so that environmental connection
	# issues are less likely to cause intermittent failures.  In
	# particular, make sure there is time for a few connection retries
	# within the 20 second await_startup_done time.  Note that the tcl
	# name of the connection retry timeout changed between BDB 5.0 and 5.1.
	if { $vermaj == 5 && $vermin == 0 } {
		append repmgr_conf " -timeout { conn_retry 5000000 }"
	} else {
		append repmgr_conf " -timeout { connection_retry 5000000 }"
	}
	# Append each remote site.  This is required for group membership
	# legacy startups, and doesn't hurt the other cases.
	foreach rmport $remote_ports {
		append repmgr_conf " -remote { $hoststr $rmport $legacy_str }"
	}
	# Turn off elections so that clients still running at the end of the
	# test after the master shuts down do not create extra log records.
	$repenv rep_config {mgrelections off}
	# For diskandinmem, the master must start up and create an in-memory
	# database before the clients start up, so delay client starts here.
	if { $dbopt == "diskandinmem" && $role == "CLIENT" } {
		set waitsec 0
		set waitincr 2
		while { [file exists $markerdir/MASTERSTARTED] == 0 } {
			set waitsec [expr $waitsec + $waitincr]
			tclsleep $waitincr
			puts "STARTTEST diskandinmem: waited $waitsec seconds"
			if { $waitsec > [expr $waitincr * 90] } {
				error "Master site never started."
			}
		}
	}
	eval $repenv repmgr $repmgr_conf

	# Create an in-memory database on the master to cause an abbreviated
	# internal init.  This is a less common case but it has unique
	# upgrade issues when upgrading from releases before 6.0.  Before
	# 6.0, the master would send back a list of all database files for
	# an abbreviated internal init and it was up to the client to only
	# request pages for in-memory databases.  Starting in 6.0, the master 
	# only sends back a list of in-memory database files.  We need to
	# make sure a current-version client can handle either type of
	# database list.
	#
	# The abbreviated internal init will only happen on the clients if
	# the master has already started and created the in-memory database.
	#
	# For the more typical on-disk-only case, this timing is not
	# required and it is also useful to test all sites starting
	# simultaneously.
	set inmemdbname { "" "inmemtest.db" }
	if { $dbopt == "diskandinmem" && $role == "MASTER" } {
		set method [lindex $oplist 1]
		set omethod [convert_method $method]
		set imdb [eval "berkdb_open_noerr -create -mode 0644 $omethod \
		    -auto_commit -env $repenv $inmemdbname"]
		set key1 1
		set key2 2
		error_check_good imdb_put1 \
		    [eval $imdb put $key1 [chop_data $omethod data$key1]] 0
		error_check_good imdb_put2 \
		    [eval $imdb put $key2 [chop_data $omethod data$key2]] 0
	}

	if { $role == "CLIENT" } {
		await_startup_done $repenv
	}

	puts "repenv is $repenv"
	#
	# Indicate that we're done starting up.  Sleep to let
	# others do the same.
	#
	puts "create START$envid marker file"
	upgrade_create_markerfile $markerdir/START$envid
	# For diskandinmem, inform clients that the master has started.
	if { $dbopt == "diskandinmem" && $role == "MASTER" } {
		upgrade_create_markerfile $markerdir/MASTERSTARTED
	}
	puts "sleeping after marker"
	tclsleep 3

	# Here is where the real test starts.
	#
	# Different operations may have different args in their list.
	# REPTEST: Args are method, niter, nloops.
	# REPTEST_GET: Does not use args.
	set op [lindex $oplist 0]
	if { $op == "REPTEST" } {
		upgradescr_reptest $repenv $oplist $markerdir
	}
	if { $op == "REPTEST_GET" } {
		upgradescr_repget $repenv $oplist $mydir $markerdir
	}
	if { $dbopt == "diskandinmem" && $role == "CLIENT" } {
		# Need an open in-memory db handle on client to dump it.
		set imdb [eval "berkdb_open_noerr -unknown \
		    -env $repenv -rdonly $inmemdbname"]
	}
	if { $dbopt == "diskandinmem" } {
		# Must dump in-memory database for later verification
		# here because its contents will not be available after
		# we close the environment.
		set dumpfile "$mydir/VERIFY/dbinmemdump"
		dump_file $imdb "" $dumpfile rep_test_upg.inmem.check
		error_check_good imdb_close [$imdb close] 0
	}
	puts "Closing env"
	$repenv mpool_sync
	error_check_good envclose [$repenv close] 0

}

proc repmgr035scr_verify { oplist mydir } {
	global util_path
	set bdbver [berkdb version]
	set vermaj [lindex $bdbver 0]
	set vermin [lindex $bdbver 1]
	set verpatch [lindex $bdbver 2]

	if { $vermaj > 18 || [expr $vermaj == 18 && $vermin > 1] \
	    || [expr $vermaj == 18 && $vermin == 1 && $verpatch >= 20] } {
		set sslargs [setup_repmgr_sslargs]
	} else {
		set sslargs ""
	}

	set rep_env_cmd "berkdb_env_noerr -home $mydir -txn $sslargs\
	    -data_dir DATADIR"

	upgradescr_verify $oplist $mydir $rep_env_cmd
}

#
# Arguments:
# type: START, VERIFY
# 	START starts up a replication site and performs an operation.
#		the operations are:
#		REPTEST runs the rep_test_upg procedure on the master.
#		REPTEST_GET run a read-only test on a client.
#	VERIFY dumps the log and database contents.
# role: master or client
# op: operation to perform
# envid: environment id number for use in replsend
# ctldir: controlling directory
# mydir: directory where this participant runs
# reputils_path: location of reputils.tcl
# hoststr: host string for repmgr sites
# local_port: port for local repmgr site
# remote_ports: ports for remote repmgr sites
# dbopt: diskonly or diskandinmem
#
set usage "upgradescript type role op envid ctldir mydir reputils_path \
hoststr local_port remote_ports dbopt"

# Verify usage
if { $argc != 11 } {
	puts stderr "Argc $argc, argv $argv"
	puts stderr "FAIL:[timestamp] Usage: $usage"
	exit
}

# Initialize arguments
set type [ lindex $argv 0 ]
set role [ lindex $argv 1 ]
set op [ lindex $argv 2 ]
set envid [ lindex $argv 3 ]
set ctldir [ lindex $argv 4 ]
set mydir [ lindex $argv 5 ]
set reputils_path [ lindex $argv 6 ]
set hoststr [ lindex $argv 7 ]
set local_port [ lindex $argv 8 ]
set remote_ports [ lindex $argv 9 ]
set dbopt [ lindex $argv 10 ]

set histdir $mydir/../..
puts "Histdir $histdir"

global env
cd $histdir
set stat [catch {eval exec ./db_printlog -V} result]
if { $stat != 0 } {
	set env(LD_LIBRARY_PATH) ":$histdir:$histdir/.libs:$env(LD_LIBRARY_PATH)"
}
source ./include.tcl
source $test_path/test.tcl

set is_repchild 1
puts "Did args. now source reputils"
source $reputils_path/reputils.tcl

set markerdir $ctldir/TESTDIR/MARKER

puts "Calling proc for type $type"
if { $type == "START" } {
	# VERIFY directory is needed in advance for diskandinmem so that
	# in-memory database can be dumped while it still exists inside
	# the starttest script.
	file mkdir $mydir/VERIFY
	repmgr035scr_starttest $role $op $envid $mydir $markerdir \
	    $hoststr $local_port $remote_ports $dbopt
} elseif { $type == "VERIFY" } {
	repmgr035scr_verify $op $mydir
} else {
	puts "FAIL: unknown type $type"
	return
}
