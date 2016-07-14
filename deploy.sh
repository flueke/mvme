#!/bin/bash

if [ $# != 3 ] ; then
    echo "usage $0 PATH_TO_BINARY PATH_TO_SOURCES TARGET_FOLDER"
    exit 1
fi

PATH_TO_BINARY="$1"
PATH_TO_SOURCES="$2"
TARGET_FOLDER="$3"

# if we cannot find the the binary we have to abort
if [ ! -f "$PATH_TO_BINARY" ] ; then
    echo "The file '$PATH_TO_BINARY' was not found. Aborting!"
    exit 1
fi

if [ ! -d "$PATH_TO_SOURCES" ] ; then
    echo "The file '$PATH_TO_SOURCES' was not found. Aborting!"
    exit 1
fi

# copy the binary to the target folder
# create directories if required
echo "---> copy binary to target folder"
cp -v "$PATH_TO_BINARY" "$TARGET_FOLDER" || exit 1

# copy the required shared libs to the target folder
# create directories if required
echo "---> copy libraries"
for lib in `ldd "$PATH_TO_BINARY" | cut -d'>' -f2 | awk '{print $1}'` ; do
   if [ -f "$lib" ] ; then
        cp -v "$lib" "$TARGET_FOLDER"
   fi  
done

echo "---> copy qt plugins"
cp -vr /usr/lib/x86_64-linux-gnu/qt5/plugins/platforms "$TARGET_FOLDER"
cp -vr /usr/lib/x86_64-linux-gnu/qt5/plugins/imageformats "$TARGET_FOLDER"

echo "---> copy auxiliary files"
cp -v "$PATH_TO_SOURCES/README.txt" "$TARGET_FOLDER"
cp -v "$PATH_TO_SOURCES/default-initlist.init" "$TARGET_FOLDER"
cp -v "$PATH_TO_SOURCES/default-stack.stk" "$TARGET_FOLDER"
cp -v "$PATH_TO_SOURCES/mvme2.sh" "$TARGET_FOLDER"
cp -v "$PATH_TO_SOURCES/qt.conf" "$TARGET_FOLDER"
