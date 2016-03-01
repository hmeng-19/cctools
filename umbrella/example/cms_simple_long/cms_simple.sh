#!/bin/sh

rm -rf cmsjob
mkdir cmsjob
cd cmsjob

. ${CMS_DIR}/cmsset_default.sh
scramv1 project CMSSW ${CMS_VERSION}
cd ${CMS_VERSION}
eval `scram runtime -sh`
cd ..
cmsDriver.py TTbar_Tauola_7TeV_cfi --conditions auto:startup -s GEN,SIM --datatier GEN-SIM -n 100 --relval 9000,50 --eventcontent RAWSIM --fileout CMS_GEN_SIM.root

# vim: set noexpandtab tabstop=4:
