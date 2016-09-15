#!/bin/bash

exec pandoc -S -s README.md -o README.pdf \
    --latex-engine=lualatex \
    #--template=latex.template \
    #--number-sections \
