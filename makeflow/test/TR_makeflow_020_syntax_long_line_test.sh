#!/bin/sh

. ../../dttools/src/test_runner.common.sh

prepare()
{
    ln ../src/makeflow ../src/makeflow_util
    syntax/long_line_test.pl > syntax/long_line_test.makeflow
    exit 0
}

run()
{
    cd syntax; ../../src/makeflow_util -k long_line_test.makeflow && exit 0
    exit 1
}

clean()
{
    rm -f syntax/long_line_test.makeflow ../src/makeflow_util
    exit 0
}

dispatch $@

# vim: set noexpandtab tabstop=4:
