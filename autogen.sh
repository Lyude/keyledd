#!/bin/sh

set -e

[ -d "build-aux" ] || mkdir build-aux
autoreconf --install -v --force
if [ "$NOCONFIGURE" != "1" ]; then
	./configure $@
fi
