#!/bin/sh
#in this version, the structure of namelist has been changed: <filepath, the fork info of resolve_name>.
#the idea here is: once we need the whole content of a file, there will be one line containing only the file name, and with the help of sort command,we must first meet this line. 
#otherwise, the filepath will be followed by the fork info of resolve_name. In this case, when we first meet one filepath, it will be followed by sth.
listfile=""
package_path="/dev/shm/package-hep"
log="/dev/shm/package-hep.log"

date>>$log
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
        -h | --help)
            show_help
            ;;
        *)
            break
            ;;
    esac
    shift
done

#sort the namelist file and delete the duplicate items.
cp $listfile $listfile.bak
sort -u $listfile >$listfile.tmp
rm $listfile
cp $listfile.tmp $listfile
rm $listfile.tmp
echo "Backup the original namelist file as .bak file"

#create package directory
if [[ -e $package_path ]]; then
    echo "The package directory has existed. Please change another package directory or delete the existed package directory first."
    exit 1
fi
mkdir -p $package_path 2>/dev/null

n=0
nocopy="no"
echo "The process of packaging has began ...."

#files from these paths will be ignored.
special_path_strings="var sys dev proc net misc selinux"
declare -a special_path_list=($special_path_strings)

#these system calls will result in the whole copy of one file item.
special_caller="lstat stat open_object bind32 connect32 bind64 connect64 truncate link1 mkalloc lsalloc whoami md5 copyfile1 copyfile2 follow_symlink link2 symlink2 readlink unlink"
declare -a special_caller_list=($special_caller)

#get the relative path version of one absolute path
relative_path()
{
    first_parameter=$1
    prefix=$2
    num=${line//[^\/]}
    n1=${#num}
    n1=`expr $n1 - 1`
    for i in `seq 1 $n1`
    do
        prefix=$prefix"../"
    done
    prefix=$prefix".."
}

#deal with the sub-directory items under one common directory
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

subfile_process()
{
    line=$1
    filelist=`find $line/ -maxdepth 1 -type f`
    if [[ X$filelist != X"" ]]; then
        IFS=$'\n'
        set $filelist
        for subfile in $@
        do
            #echo "subfile:"$subfile
            if [[ ! -e $package_path$subfile ]]; then
                touch $package_path$subfile
            fi
        done
        IFS=""
    fi
}

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
            #dir; if yes, exit; if no, create the empty dir and the empty linked dir, maintain their link relationship.
            if [[ -d $sublink ]]; then
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
                #file; if yes, exit; if no, create the null file and null linked file, maintain their link relationship.
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

#process a common directory item
regular_dir()
{
    line=$1
    echo "this is a regular dir"
    #first, judge whether the directory has existed in the package.
    if [[ -e $package_path$line ]]; then
        #if yes, check if the .__complete file exist under the target directory.
        if [[ -e $package_path$line/.__complete ]]; then
            #if yes, nothing to do. leave.
            echo "regular dir; the dir exists and complete!">>$log
        else
            echo "the dir exist, but the dir tree is not full">>$log
            #if no, begin to deal with type d, type f, type l.
            #type d; for each dir, first check whether it exists in the pacakge. if yes, exit; if no, create an empty dir.
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
        #if no, compelte create.
        #type d; create an empty dir.
        mkdir -p $package_path$line
        #begin to deal with type d, type f, type l.
        #type d
        subdir_process $line
        #type f; touch it.
        subfile_process $line
        #type l
        sublink_process $line
        #create the .__complete file under the target directory.
        touch $package_path$line/.__complete
        echo "regular dir; the dir does not exist, now has been created completely!" >>$log
    fi
}


symbolink_dir()
{
    line=$1
    #symbolic link dirA (linked dir is dirB).(do not consider recursively symbolic link dir)
    line_bak=$line
    filename=${line##/*/}
    filepath=${line%$filename}
    #as for the dirB, treat it as one rugular dir and do the above things.
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
        #echo "regular_dir after line: "$line
        #as for the dirA, ln -s dirB dirA
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

fullcopy_file()
{
    line=$1
    filename=$2
    filepath=$3
	#metadata + data. check whether the file is in the .wholefiles, items inside the .__wholefiles should avoid the longest prefix matching.
    find_result=`cat $package_path$filepath/.__wholefiles 2>/dev/null|grep "$line whole"`
    #if yes, no need to copy. do nothing. exit.
    #if no, judge whether it is a symbolic link file.
    if [[ X$find_result == X"" ]]; then
        if [[ -L $line ]]; then
            #if yes, cp -p(linked file); create the file itself and ln -s it to the linked file,. then add it into .wholefiles.
            #test how to preserve the metadata using cp command.(doubt)
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
            #doubt use rsync pt parameter to ensure that the metadata of file is preserved.
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

metadatacopy_file()
{
    line=$1
    filename=$2
    filepath=$3
    find_result=`cat $package_path$filepath/.__metadatafiles 2>/dev/null|grep "$line metadata"`
    #if yes, no need to copy. do nothing. exit.
    if [[ X$find_result == X"" ]]; then
        #if no, judge whether it is a symbolic link file.
        #echo "file not exist in medata"
        if [[ -L $line ]]; then
            #if yes, cp -p + truncate(linked file); create the file itself and ln -s it to the linked file. then add it into .metadatafiles.
            #test how to preserve the metadata using cp command.(doubt)
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
            #doubt use rsync pt parameter to ensure that the metadata of file is preserved.
            create_symbolink $line $filename $filepath $readlink_f_path
            cd ->/dev/null 2>&1
            echo $line metadata>>$package_path$filepath/.__metadatafiles
            echo "symbolic link file; metadata only, does not exist and copy successfully">>$log
        else
            #if no, cp -p + truncate. then add it into .metadatafiles.
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


line_process()
{
    line=$1
    echo "line_process the line: "$line >> $log
    if [[ -d $line ]]; then
        #first check whether it is a symbolic link dir.
        #regular dir.
        if [[ ! -L $line ]]; then
            regular_dir $line
        else
            symbolink_dir $line
        fi
    else
        #echo "this is one file item inside the namelist"
        caller=$2
        filename=${line##/*/}
        filepath=${line%$filename}
        #file item of namelist (maintain two list file under the target directory in the pacakge: .__wholefiles + .__metadatafiles.)
        #first judge whether it is necessary to copy metadata + data.
        if [[ X$caller == X"" ]]; then
            fullcopy_file $line $filename $filepath
        else
            #metadata. check whether the file is in the .__metadatafiles.
            metadatacopy_file $line $filename $filepath
        fi #final of metadata file/metadata+data file
    fi #final of file/dir
}

#as for each line in listfile, we firstly check whether the source file exists. If yes, we then copy it; otherwise, directly go to deal with next line.
last_filename=""
last_caller=""
for whole_line in $(cat $listfile)
do
    echo `expr $n + 1` >> $log
    n=`expr $n + 1`
    echo "whole_line: "$whole_line >>$log
    IFS="|"     # Set the field separator
    set $whole_line      # Breaks the string into $1, $2, ...
    line=$1
    caller=$2
    IFS=""
    echo "line: "$line >> $log
    echo "last_filename: "$last_filename >> $log
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
    first_path=""
    second_path=""
    echo $line >> $log
    IFS="/"     # Set the field separator
    set $line      # Breaks the string into $1, $2, ...
    first_path=$2
    second_path=$3
    IFS=""
    for i in ${special_path_list[*]};
    do
        if [[ "$i" == "$first_path" ]]; then
            nocopy="yes"
            break
        fi
    done
    if [ "$nocopy" == "yes" ]; then
        nocopy="no"
        echo "this file is under proc dev sys, no necessary to copy it!" >> $log
        continue
    fi
    #here, delete all slash '/' at the end of line.
    #doubt, modify it.
    line=${line%/}
    #judge the existence of the file
    if [[ ! -e $line ]]; then
        echo "this source file does not exist!" >> $log
        continue
    fi
    line_process $line $caller
done


#echo "/cvmfs* /tmp/FilePackage1.3/cvmfs" >> $mountlistfile
sizeinfo=`du -hs $package_path`
IFS=" "
set $sizeinfo
size=$1
IFS=""

file_num=`find $package_path -type f|wc -l`

echo "the path of pacakge is: "$package_path >> $log
echo "the total size of package is: "$size >> $log
echo "the total number of files is: "$file_num >> $log

echo "the path of pacakge is: "$package_path
echo "the total size of package is: "$size
echo "the total number of files is: "$file_num

date>>$log




