#!/bin/sh

set -e -u

if ! [ -f tests/regression/run.sh ]; then
	echo "Make sure you are in OTTD checkout root before starting this script."
	exit 1
fi

params="-snull -mnull -vnull:ticks=10000"
gdb=0
if [ "$#" -gt 0 ] && [ "$1" = "-g" ]; then
	gdb=1
fi

if [ "$#" -gt 0 ] && [ -f "$1" ]; then
	tests="$1"
elif [ "$#" -gt 1 ] && [ -f "$2" ]; then
	tests="$2"
else
	tests=tests/regression/*.sav
fi

for sav in $tests; do
	echo "Running $sav... "

	test_folder="tests/regression/`basename $sav .sav`"
	mkdir -p $test_folder

	cfg="$test_folder/default.cfg"
	if [ ! -f "$cfg" ]; then
		cp tests/regression/default.cfg "$cfg"
	fi

	if [ $gdb = 1 ]; then
		gdb --ex run --args ./bin/openttd -x -c $cfg $params -g $sav
	else
		./bin/openttd -x -c $cfg $params -g $sav -d sl=1
	fi

	rm -rf $test_folder  # Cleanup
done
