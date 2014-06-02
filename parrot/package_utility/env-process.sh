#!/bin/sh
#The script is used to obtain and preserve the environment variables.
show_help()
{
	echo "Options:"
	echo "-s, --shell       The type of shell used to do the experiment."
	echo "-p, --path        The path of the environment-variable file."
	echo "-h, --help        Show this help message."

	exit 1
}

while [ $# -gt 0 ]
do
	case $1 in
		-s | --shell)
			shift
			shell_type=$1
			;;
		-p | --path)
			shift
			env_path=$1
			;;
		-h | --help)
			show_help
			;;
		*)
			break
			;;
	esac
	shift
done

if [[ X$shell_type == X"" ]]; then
	echo "Please indicate the shell type!"
	exit 1
fi

if [[ X$env_path == X"" ]]; then
	echo "Please indicate the path of the environment variable!"
	exit 1
fi

if [[ -e $env_path ]]; then
	rm $env_path
fi

if [[ -e $env_path.bak ]]; then
	rm $env_path.bak
fi

env>$env_path

IFS=''
while read line
do
	IFS="="     # Set the field separator
	set $line      # Breaks the string into $1, $2, ...
	variable_name=$1
	variable_value=$2
	echo setenv $variable_name \"$variable_value\" >> $env_path.bak
	IFS=""
done < $env_path

cp -f $env_path.bak $env_path
rm -f $env_path.bak
