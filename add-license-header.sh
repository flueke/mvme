#!/bin/bash
# From: https://stackoverflow.com/a/151690

FILES=(*.h *.cc)

for i in ${FILES[@]}
do
    if ! grep -q Copyright $i
    then
        echo $i
        #cat copyright.txt $i >$i.new && mv $i.new $i
    fi
done
