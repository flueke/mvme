# Automatic variables:
# $@: file name of the target of the rule
# $<: name of the first prerequisite

# Define a Pattern Rule for dia files.
%.png: %.dia
	dia -e $@ $<

%.svg: %.dia
	dia -e $@ $<

DIA_IN  = $(wildcard *.dia)
DIA_OUT_PNG = $(DIA_IN:.dia=.png)
DIA_OUT_SVG = $(DIA_IN:.dia=.svg)

# Pattern rule for gimp xcf to png export
%.png: %.xcf
	xcf2png -v -o $@ $<

GIMP_IN  = $(wildcard *.xcf)
GIMP_OUT = $(GIMP_IN:.xcf=.png)

.PHONY: all clean dia gimp

all: dia gimp

dia: $(DIA_OUT_PNG) $(DIA_OUT_SVG)

gimp: $(GIMP_OUT)

clean:
	-rm -f $(DIA_OUT_PNG)
	-rm -f $(GIMP_OUT)
