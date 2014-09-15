#!/bin/bash
rm -rf Changelog.txt common-mountlist test

#https
wget https://www.eff.org/files/Changelog.txt

#https, redirect to http
wget https://www3.nd.edu/~ccl/research/data/hep-case-study/common-mountlist

#git based on https
git clone https://github.com/hmeng-19/test.git
rm -rf test

#git based on ssh
git clone git@github.com:hmeng-19/test.git

#pure ssh
ssh wizard.cse.nd.edu
