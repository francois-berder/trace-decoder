# swt
SiFive implementation of utility that translates single-wire trace (SWT) serial data into Nexus messages, and multicasts messages to socket clients

The name of the utility is `swt`

### Licensing:

The SWT utility is released under the GPL 3 License. For a full copy of the license, see the COPYING file included with the trace-decoder. The licensing statement is:

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


### Building:

The swt utility will build on Linux.  Windows and Mac OS X support are forthcoming.  To build the swt utility, the correct build tools must first be installed. Please refer to the trace decoder's README; the same tool prerequisites apply to swt utility as apply to the trace decoder.

#### Build Targets

The swt utility builds as part of the trace-decoder build procedure.  Please refer to the trace-decoder README for instructions on how to build the trace-decoder.

### Running:

Use `swt -h` to display usage information. It will display something like:

```
Usage: swt [-device serial_device] [-port n] [-srcbits n]

-device serial_device:   The file system path of the serial device to use.  Default is /dev/ttyUSB0.
-port n:   The TCP port on which swt will listen for client socket connections.  Default is 4567.
-srcbits n:   The size in bits of the src field in the trace messages. n must 0 to 8. Setting srcbits to 0 disables
              multi-core. n > 0 enables multi-core. If the -srcbits=n switch is not used, srcbits is 0 by default.
-h:           Display this usage information.
```

Omitting the -h argument invokes the swt utility in its normal mode, which connects to the specified serial device and passes along any full Nexus messages to
any socket clients.

### Socket protocol:

Client sockets, at minimum, just need to connect to swt's server port, and then read from the socket to receive any Nexus messages being sent from the serial device.
Nexus messages are formatted as one message per line, where a message is formatted as a sequence of <field>=<value> pairs.  The set of possible fields currently of "tcode", "idtag", "dqdata", and "timestamp".  The "timestamp" field won't appear if a timestamp was not a part of the message.  Field values are implicitly in hexadecimal format, without a 0x prefix.

Here is an example snippet of messages flowing to a socket client (no timestamp fields):

tcode=7 idtag=0 dqdata=74736574
tcode=7 idtag=2 dqdata=6e69
tcode=7 idtag=2 dqdata=a67
tcode=7 idtag=0 dqdata=74736574
tcode=7 idtag=2 dqdata=6e69
tcode=7 idtag=2 dqdata=a67
tcode=7 idtag=0 dqdata=74736574
tcode=7 idtag=2 dqdata=6e69
tcode=7 idtag=2 dqdata=a67


If a particular socket client wishes to filter out certain ITC channels from the stream of messages, client should send a line of input to swt over the socket, that is formatted like this: "itcfilter <32-bit hexadecimal bitmask>".  If bit N is 1 in the bitmask, then ITC channel N will be omitted from client's message stream.

When socket client is no longer interested in reading Nexus messages, client can close its end of the socket.
