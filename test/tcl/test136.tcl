# Copyright (c) 2011, 2019 Oracle and/or its affiliates.  All rights reserved.
#
# See the file LICENSE for license information.
#
# $Id$
#
# TEST	test136
# TEST  Test operations on similar overflow records. [#20329]
# TEST  Here, we use subdatabases.

proc test136 {method {keycnt 10} {datacnt 10} args} {
	source ./include.tcl

	eval {test135 $method $keycnt $datacnt 1 "136"} $args
}

