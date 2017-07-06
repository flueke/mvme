# Automatic variables:
# $@: file name of the target of the rule
# $<: name of the first prerequisite

# Define a Pattern Rule for dia files.
%.png: %.dia
	dia -e $@ $<

DIA_IN  = $(wildcard *.dia)
DIA_OUT = $(DIA_IN:.dia=.png)

.PHONY: all clean dia

all: dia

dia: $(DIA_OUT)

clean:
	-rm $(DIA_OUT)
