/*-
 * Copyright (c) 2009, 2019 Oracle and/or its affiliates.  All rights reserved.
 *
 * See the file LICENSE for license information.
 *
 */
using System;
using System.Collections.Generic;
using System.Text;

namespace BerkeleyDB {
    /// <summary>
    /// A class representing the address of a replication site used by Berkeley
    /// DB HA.
    /// </summary>
    public class ReplicationHostAddress {
        /// <summary>
        /// The site's host identification string, generally a TCP/IP host name. 
        /// </summary>
        public string Host;
        /// <summary>
        /// The port number on which the site is receiving. 
        /// </summary>
        public uint Port;

        /// <summary>
        /// Instantiate a new, empty address
        /// </summary>
        public ReplicationHostAddress() { }
        /// <summary>
        /// Instantiate a new address, parsing the host and port from the given
        /// string
        /// </summary>
        /// <param name="HostAndPort">A string in host:port format</param>
        public ReplicationHostAddress(string HostAndPort) {
            /// Look for index of the last colon in the HostAndPort string.
            int sep = HostAndPort.LastIndexOf(':');
            if (sep == -1)
                throw new ArgumentException(
                    "Hostname and port must be separated by a colon.");
            if (sep == 0)
                throw new ArgumentException(
                    "Invalid hostname.");
            try {
                Port = UInt32.Parse(HostAndPort.Substring(sep + 1));
            } catch {
                throw new ArgumentException("Invalid port number.");
            }
            Host = HostAndPort.Substring(0, sep);
        }
        /// <summary>
        /// Instantiate a new address
        /// </summary>
        /// <param name="Host">The site's host identification string</param>
        /// <param name="Port">
        /// The port number on which the site is receiving.
        /// </param>
        public ReplicationHostAddress(string Host, uint Port) {
            this.Host = Host;
            this.Port = Port;
        }
    }
}
