# luamylib - some funcs in Lua
# (c) 2009-10 Alacner zhang <alacner@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# If you use this package in a product, an acknowledgment in the product
# documentation would be greatly appreciated (but it is not required).

#CC = gcc
#RM = rm


DRIVER_LIBS= -L/usr/local/pgsql/lib -lpq
DRIVER_INCS= -I/usr/local/pgsql/include

# Name of .pc file. "lua5.1" on Debian/Ubuntu
LUAPKG = lua5.1

WARN= -Wall -Wmissing-prototypes -Wmissing-declarations
CFLAGS = `pkg-config $(LUAPKG) --cflags` -O3 $(WARN) $(DRIVER_INCS)
INSTALL_PATH = `pkg-config $(LUAPKG) --variable=INSTALL_CMOD`
LIBS = `pkg-config $(LUAPKG) --libs`

## If your system doesn't have pkg-config, comment out the previous lines and
## uncomment and change the following ones according to your building
## enviroment.

#CFLAGS = -I/usr/include/lua5.1/ -O3 -Wall
#LIBS = -llua5.1
#INSTALL_PATH = /usr/lib/lua/5.1


pgsql.so: luapgsql.c
	$(CC) -o pgsql.so -shared $(LIBS) $(CFLAGS) luapgsql.c $(DRIVER_LIBS)

install: pgsql.so
	make test
	install -s pgsql.so $(INSTALL_PATH)

clean:
	$(RM) pgsql.so

test: pgsql.so test_pgsql.lua
	lua test_pgsql.lua

all: pgsql.so
