# Copyright (c) 1996, 2019 Oracle and/or its affiliates.  All rights reserved.
#
# See the file LICENSE for license information.
#
# $Id$
#
# TEST	test018
# TEST	Offpage duplicate test
# TEST	Key_{first,last,before,after} offpage duplicates.
# TEST	Run duplicates with small page size so that we test off page
# TEST	duplicates.
proc test018 { method {nentries 10000} args} {
	puts "Test018: Off page duplicate tests"
	set pgindex [lsearch -exact $args "-pagesize"]
	if { $pgindex != -1 } {
		puts "Test018: Skipping for specific pagesizes"
		return
	}
	eval {test011 $method $nentries 19 "018" -pagesize 512} $args
}
