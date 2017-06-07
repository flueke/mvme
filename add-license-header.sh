#!/bin/bash
# From: https://stackoverflow.com/a/151690

FILES=(*.h *.cc)
INPUTFILE="`dirname $0`/LICENSE-SHORT.TXT"

for i in ${FILES[@]}
do
    if ! grep -q Copyright $i
    then
        echo $i
        cat $INPUTFILE $i >$i.new && mv $i.new $i
    fi
done
