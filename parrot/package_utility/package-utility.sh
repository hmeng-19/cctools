#!/bin/sh
#in this version, the structure of namelist has been changed: <filepath, the fork info of resolve_name>.
#the idea here is: once we need the whole content of a file, there will be one line containing only the file name, and with the help of sort command,we must first meet this line. 
#otherwise, the filepath will be followed by the fork info of resolve_name. In this case, when we first meet one filepath, it will be followed by sth.

show_help()
{
	echo "Use: /bin/bash package-utility.sh -L namelist"
	echo "Principle:"
	echo "Read the namelist file, check the file type of each item inside the namelist."
	echo "According to the file type and the system call type of each item, choose the copy degree."
	echo "Options:"
	echo "-L, --namelist <namelist-path>    Generate one package containing all the files referred inside namelist-path."
	echo  "-h, --help                       Show this help message."
	exit 1
}

#options parse
while [ $# -gt 0 ]
do
	case $1 in
		-L | --namelist)
			shift
			listfile=$1
			;;
		-p | --path)
			shift
			package_path=$1
			log=$package_path.log
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

#create package directory
if [[ -e $package_path ]]; then
	echo "The package directory has existed. Please change another package directory or delete the existed package directory first."
	exit 1
fi
mkdir -p $package_path 2>/dev/null

date>>$log

#sort the namelist file and delete the duplicate items.
cp $listfile $listfile.bak
sort -u $listfile >$listfile.tmp
rm $listfile
cp $listfile.tmp $listfile
rm $listfile.tmp
echo "Backup the original namelist file as .bak file"

n=0
nocopy="no"
echo "The process of packaging has began ...."

#files from these paths will be ignored.
special_path_strings="var sys dev proc net misc selinux"
declare -a special_path_list=($special_path_strings)

#these system calls will result in the whole copy of one file item.
special_caller="lstat stat open_object bind32 connect32 bind64 connect64 truncate link1 mkalloc lsalloc whoami md5 copyfile1 copyfile2 follow_symlink link2 symlink2 readlink unlink"
declare -a special_caller_list=($special_caller)

#get the relative path prefix of one absolute path. If current_path is /home/hmeng, the prefix will be ../.., the relative path will be ../../home/hmeng.
relative_path()
{
	current_path=$1
	prefix=$2
	num=${current_path//[^\/]}
	n1=${#num}
	n1=`expr $n1 - 1`
	for i in `seq 1 $n1`
	do
		prefix=$prefix"../"
	done
	prefix=$prefix".."
}

#create the sub-directory items under one common directory to maintain the 1th-depth directory struction.
subdir_process()
{
	line=$1
	dirlist=`find $line/ -maxdepth 1 -type d`
	if [[ X$dirlist != X"" ]]; then
		IFS=$'\n'
		set $dirlist
		for subdir in $@
		do
			mkdir -p $package_path$subdir
		done
		IFS=""
	fi
}

#create the sub-file items under one common directory to maintain the 1th-depth directory struction.
subfile_process()
{
	line=$1
	filelist=`find $line/ -maxdepth 1 -type f`
	if [[ X$filelist != X"" ]]; then
		IFS=$'\n'
		set $filelist
		for subfile in $@
		do
			if [[ ! -e $package_path$subfile ]]; then
				touch $package_path$subfile
			fi
		done
		IFS=""
	fi
}

#create one symbolink and transfer absolute link target into relative link target.
create_symbolink()
{
	sublink=$1
	filename=$2
	filepath=$3
	readlink_f_path=$4
	readlink_path=`readlink $sublink`
	first_ch=${readlink_path:0:1}
	if [[ $first_ch == '/' ]]; then
		current_dir=$filepath
		prefix=""
		relative_path $current_dir $prefix
		real_path=$prefix$readlink_f_path
		ln -s $real_path $filename
	else
		ln -s $readlink_path $filename
	fi
}

#create the sub-symbolink items under one common directory to maintain the 1th-depth directory struction.
#as for each symbolink item (directory link or file link), first create its target and then create the symbolink.
#Problem of this implementation: if the target of one symbolink is still a symbolink, what will happen?
sublink_process()
{
	linklist=`find $line/ -maxdepth 1 -type l`
	if [[ X$linklist != X"" ]]; then
		IFS=$'\n'
		set $linklist
		for sublink in $@
		do
			filename=${sublink##/*/}
			filepath=${sublink%$filename}
			if [[ -d $sublink ]]; then
				#dir. if the dir has existed, exit; if no, create the empty dir and the empty linked dir, maintain their link relationship.
				if [[ ! -e $package_path$sublink ]]; then
					readlink_f_path=`readlink -f $sublink`
					parrot_actual_path=$package_path$readlink_f_path
					mkdir -p $parrot_actual_path
					parrot_symlink_path=$package_path$filepath
					mkdir -p $parrot_symlink_path
					cd $parrot_symlink_path
					create_symbolink $sublink $filename $filepath $readlink_f_path
					cd ->/dev/null 2>&1
				fi
			else
				#file. if the file has existed, exit; if no, create the null file and null linked file, maintain their link relationship.
				if [[ ! -e $package_path$sublink ]]; then
					readlink_f_path=`readlink -f $sublink`
					actual_filename=${readlink_f_path##/*/}
					actual_filepath=${readlink_f_path%$actual_filename}
					mkdir -p $package_path$actual_filepath
					#judge whether the linked file exist, if yes, do nothing. if no, touch the linked file.
					if [[ ! -e $package_path$readlink_f_path ]]; then
						touch $package_path$readlink_f_path
					fi
					len=${#filepath}
					len=`expr $len - 1`
					if [ $len -gt 0 ]; then
						filepath=${filepath:0:len}
					fi
					mkdir -p $package_path$filepath
					cd $package_path$filepath
					create_symbolink $sublink $filename $filepath $readlink_f_path
					cd ->/dev/null 2>&1
				fi
			fi
		done
		IFS=""
	fi
}

#process a common directory item. To maintain the 1st-depth directory struction of the directory item, each file, directory, symbolink directly under the directory will be created. If the struction of one directory is complete, one .__complete file will be placed under the directory.
regular_dir()
{
	line=$1
	echo "this is a regular dir"
	#first, judge whether the directory has existed in the package.
	if [[ -e $package_path$line ]]; then
		#if yes, check if the .__complete file exist under the target directory.
		if [[ -e $package_path$line/.__complete ]]; then
			#if yes, nothing to do. 
			echo "regular dir; the dir exists and complete!">>$log
		else
			#if no, begin to deal with type d, type f, type l.
			#type d; for each dir, create an empty dir.
			subdir_process $line
			#type f; for each file, first check whether it exists in the package.if yes, exit; if no, touch it.
			subfile_process $line
			#type l; for each link, first check whether it exists, in the pacakge. if yes, exit; if no, create it and the linked file/dir.
			sublink_process $line
			#create the .__complete file under the target directory.
			touch $package_path$line/.__complete
			echo "regular dir; the dir exists but not complete, now has been created completely!">>$log
		fi
	else
		#if no, compeltely create the directory.
		#type d; create an empty dir.
		mkdir -p $package_path$line
		#begin to deal with type d, type f, type l.
		#type d
		subdir_process $line
		#type f 
		subfile_process $line
		#type l
		sublink_process $line
		#create the .__complete file under the target directory.
		touch $package_path$line/.__complete
		echo "regular dir; the dir does not exist, now has been created completely!" >>$log
	fi
}

#process one symbolink directory item. First treat its target as a regular directory and create it; then create the symbolink.
#Problem of this implementation: if the target of one symbolink is still a symbolink, what will happen?
symbolink_dir()
{
	line=$1
	line_bak=$line
	filename=${line##/*/}
	filepath=${line%$filename}
	readlink_f_path=`readlink -f $line`
	parrot_actual_path=$package_path$readlink_f_path
	echo "symbolic link dir: "$line", its linked dir is: "$readlink_f_path"  the next line is about the linked dir">>$log
	regular_dir $readlink_f_path
	if [[ ! -e $package_path$line_bak ]]; then
		line=$line_bak
		filename=${line##/*/}
		filepath=${line%$filename}
		readlink_f_path=`readlink -f $line`
		parrot_actual_path=$package_path$readlink_f_path
		parrot_symlink_path=$package_path$filepath
		mkdir -p $parrot_symlink_path
		cd $parrot_symlink_path
		create_symbolink $line $filename $filepath $readlink_f_path
		cd ->/dev/null 2>&1
		echo "symbolic link dir, not exist; create successfully">>$log
	else
		echo "symbolic link dir, has existed">>$log
	fi
}

#full copy of one file item.
#maintain two files under each directory to maintain the list of full-copy files and metadata-copy files: .__wholefiles and .__metadatafiles.
fullcopy_file()
{
	line=$1
	filename=$2
	filepath=$3
	#check whether the file is in the .wholefiles, items inside the .__wholefiles should avoid the longest prefix matching.
	find_result=`cat $package_path$filepath/.__wholefiles 2>/dev/null|grep "$line whole"`
	#if yes, do nothing. if no, judge whether it is a symbolic link file.
	if [[ X$find_result == X"" ]]; then
		if [[ -L $line ]]; then
			#if is a symbolink, use cp -p(linked file) to create the target file, create the file itself and maintain their relationship. then add it into .wholefiles.
			#Problem: test how to preserve the metadata using cp command?
			readlink_f_path=`readlink -f $line`
			actual_filename=${readlink_f_path##/*/}
			actual_filepath=${readlink_f_path%$actual_filename}
			mkdir -p $package_path$actual_filepath
			#judge whether the linked file exist, if yes, do nothing. if no, touch the linked file.
			linked_find_result=`cat $package_path$actual_filepath/.__wholefiles 2>/dev/null|grep "$readlink_f_path whole"`
			if [[ X$linked_find_result == X"" ]]; then
				if [[ -e $package_path$readlink_f_path ]]; then
					rm $package_path$readlink_f_path
				fi
				cp -p $readlink_f_path $package_path$readlink_f_path
				echo $readlink_f_path whole>>$package_path$actual_filepath/.__wholefiles
				echo $readlink_f_path metadata>>$package_path$actual_filepath/.__metadatafiles
			fi
			echo "metadata+data, symbolic link file: "$line", its linked file is: "$readlink_f_path". the next line is about the linked file">>$log
			len=${#filepath}
			len=`expr $len - 1`
			if [ $len -gt 0 ]; then
				filepath=${filepath:0:len}
			fi
			mkdir -p $package_path$filepath
			cd $package_path$filepath
			create_symbolink $line $filename $filepath $readlink_f_path
			cd ->/dev/null 2>&1
			echo $line whole>>$package_path$filepath/.__wholefiles
			echo $line metadata>>$package_path$filepath/.__metadatafiles
			echo "symbolic file; metadata+data, does not exist and copy successfully">>$log
		else
			#if no, cp -p. then add it into .wholefiles.
			mkdir -p $package_path$filepath
			cp -p $line $package_path$line
			echo $line whole>>$package_path$filepath/.__wholefiles
			echo $line metadata>>$package_path$filepath/.__metadatafiles
			echo "regular file; metadata+data, does not exist and copy successfully">>$log
		fi #final
	else
		echo "file; metadata+data, has existed and no need to copy">>$log
	fi #if [[ X$find_result == X"" ]];
}

#only preserve metadata of one file.
#Problem: How to complete metadata preservation?
metadatacopy_file()
{
	line=$1
	filename=$2
	filepath=$3
	#First, judge whether this metadata of the file has been preserved. if yes, no need to copy. do nothing. exit.
	find_result=`cat $package_path$filepath/.__metadatafiles 2>/dev/null|grep "$line metadata"`
	if [[ X$find_result == X"" ]]; then
		#judge whether it is a symbolic link file.
		if [[ -L $line ]]; then
			#if yes, create the target file and the file itself and maintain their relationship. then add it into .metadatafiles.
			readlink_f_path=`readlink -f $line`
			actual_filename=${readlink_f_path##/*/}
			actual_filepath=${readlink_f_path%$actual_filename}
			mkdir -p $package_path$actual_filepath
			#judge whether the linked file exist, if yes, do nothing. if no, touch the linked file.
			if [[ ! -e $package_path$readlink_f_path ]]; then
				touch $package_path$readlink_f_path
				last_modified=`stat -c "%y" $readlink_f_path`
				touch -d "$last_modified" $package_path$readlink_f_path
			fi
			linked_find_result=`cat $package_path$actual_filepath/.__metadatafiles 2>/dev/null|grep "$readlink_f_path metadata"`
			if [[ X$linked_find_result == X"" ]]; then
				echo $readlink_f_path metadata>>$package_path$actual_filepath/.__metadatafiles
			fi
			echo "metadata only, symbolic link file: "$line", its linked file is: "$readlink_f_path". the next line is about the linked file">>$log
			len=${#filepath}
			len=`expr $len - 1`
			if [ $len -gt 0 ]; then
				filepath=${filepath:0:len}
			fi
			mkdir -p $package_path$filepath
			cd $package_path$filepath
			create_symbolink $line $filename $filepath $readlink_f_path
			cd ->/dev/null 2>&1
			echo $line metadata>>$package_path$filepath/.__metadatafiles
			echo "symbolic link file; metadata only, does not exist and copy successfully">>$log
		else
			#if no, create one empty file and then add it into .metadatafiles.
			mkdir -p $package_path$filepath
			touch $package_path$line
			last_modified=`stat -c "%y" $line`
			touch -d "$last_modified" $package_path$line
			echo $line metadata>>$package_path$filepath/.__metadatafiles
			echo "regular file; metadata only, does not exist and copy successfully">>$log
		fi #final
	else
		echo "file; metadata only, has existed and no need to copy">>$log
	fi
}

#process one line of the namelist file. There are four subtypes: common file item; common directory item; symbolink file item; symbolic directory item.
line_process()
{
	line=$1
	if [[ -d $line ]]; then
		if [[ ! -L $line ]]; then
			regular_dir $line
		else
			symbolink_dir $line
		fi
	else
		caller=$2
		filename=${line##/*/}
		filepath=${line%$filename}
		if [[ X$caller == X"" ]]; then
			fullcopy_file $line $filename $filepath
		else
			metadatacopy_file $line $filename $filepath
		fi #final of metadata file/metadata+data file
	fi #final of file/dir
}

#as for each line in listfile, we firstly check whether the source file exists. If yes, we then copy it; otherwise, directly go to deal with next line.
last_filename=""
last_caller=""
for whole_line in $(cat $listfile)
do
	n=`expr $n + 1`
	echo "line" $n": "$whole_line >> $log
	IFS="|"     # Set the field separator
	set $whole_line      # Breaks the string into $1, $2, ...
	line=$1
	caller=$2
	IFS=""
	#use null to differentiate special system calls of files which need to be fully copied.
	for i in ${special_caller_list[*]};
	do
		if [[ "$i" == "$caller" ]]; then
			caller=""
			break
		fi
	done
	if [[ $last_filename == $line ]] && [[ $last_caller == $caller ]]; then
		echo "this file has been processed. this line is equal to last line" >> $log
		continue
	else
		last_filename=$line
		last_caller=$caller
	fi
	IFS="/"     # Set the field separator
	set $line      # Breaks the string into $1, $2, ...
	first_path=$2
	IFS=""
	#judge whether this file is under special directories (/dev /proc /sys /net ...)
	for i in ${special_path_list[*]};
	do
		if [[ "$i" == "$first_path" ]]; then
			nocopy="yes"
			break
		fi
	done
	if [ "$nocopy" == "yes" ]; then
		echo "this file is under proc dev sys, no necessary to copy it!" >> $log
		nocopy="no"
		continue
	fi
	#delete all slash '/' at the end of line.
	#Problem delete all the slashes at the end of one line?
	line=${line%/}
	#judge the existence of the file
	if [[ ! -e $line ]]; then
		echo "this source file does not exist!" >> $log
		continue
	else
		line_process $line $caller
	fi
done

sizeinfo=`du -hs $package_path`
IFS=" "
set $sizeinfo
size=$1
IFS=""

file_num=`find $package_path -type f|wc -l`
dir_num=`find $package_path -type d|wc -l`
symlink_num=`find $package_path -type l|wc -l`

echo "the path of pacakge is: "$package_path >> $log
echo "the total size of package is: "$size >> $log
echo "the total number of files is: "$file_num >> $log
echo "the total number of directories is: "$dir_num >> $log
echo "the total number of symlink is: "$symlink_num >> $log
echo "the path of pacakge is: "$package_path
echo "the total size of package is: "$size
echo "the total number of files is: "$file_num
echo "the total number of directories is: "$dir_num 
echo "the total number of symlink is: "$symlink_num
echo "the log file is: "$log
date>>$log




