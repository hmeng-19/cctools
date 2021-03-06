#! /bin/sh

. ../../dttools/test/test_runner_common.sh
. ./parrot-test.sh

export PARROT_ALLOW_SWITCHING_CVMFS_REPOSITORIES="yes"
export HTTP_PROXY=http://eddie.crc.nd.edu:3128
export PARROT_CVMFS_REPO='*.cern.ch:pubkey=<BUILTIN-cern.ch.pub>,url=http://cvmfs-stratum-one.cern.ch/cvmfs/*.cern.ch'

tmp_dir_master=${PWD}/parrot_temp_dir
tmp_dir_hitcher=${PWD}/parrot_temp_dir_hitcher

test_file=/cvmfs/atlas.cern.ch/repo/conditions/logDir/lastUpdate

prepare()
{
	$0 clean
}

run()
{
	if parrot --check-driver cvmfs
	then
		parrot -t${tmp_dir_master} -- sh -c "head $test_file > /dev/null; sleep 10" &
		pid_master=$!

		parrot -t${tmp_dir_hitcher} --cvmfs-alien-cache=${tmp_dir_master}/cvmfs -- sh -c "stat $test_file"
		status=$?

		kill $pid_master

		return $status
	else
		return 0
	fi
}

clean()
{
	if [ -n "${tmp_dir_master}" -a -d "${tmp_dir_master}" ]
	then
		rm -rf ${tmp_dir_master}
	fi

	if [ -n "${tmp_dir_hitcher}" -a -d ${tmp_dir_hitcher} ]
	then
		rm -rf ${tmp_dir_hitcher}
	fi

	return 0
}

dispatch "$@"

# vim: set noexpandtab tabstop=4:
