#!/bin/bash

# prepare the staging folder
echo Preparing folder...
rm -rf staging
mkdir staging
mkdir staging/BO3MacFix

# copy our compiled binaries
echo Copying binaries...
cp BO3MacFix.dylib staging/BO3MacFix/BO3MacFix.dylib
cp installer/Install_BO3MacFix staging/BO3MacFix/Install_BO3MacFix
cp launcher/AppBundleExe staging/BO3MacFix/AppBundleExe

# copy the binary assets (c2m)
echo Copying assets...
cp -r assets/* staging/BO3MacFix

# set the permissions permissions
echo Setting permissions...
chmod 0644 staging/BO3MacFix/BO3MacFix.dylib
chmod 0755 staging/BO3MacFix/Install_BO3MacFix
chmod 0755 staging/BO3MacFix/AppBundleExe

# make a release tgz file
mkdir release
cd staging
echo Compressing files...
tar -czvf ../release/BO3MacFix.tar.gz BO3MacFix
cd ..
