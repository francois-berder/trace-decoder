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
Usage: swt (NOTE: need to fill in the details here)
-h:           Display this usage information.
```
