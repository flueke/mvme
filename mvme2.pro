TEMPLATE = subdirs
SUBDIRS = src test

copytemplates.commands = $(COPY_DIR) $$shell_path($$PWD/templates) $$shell_path($$OUT_PWD)
copyfiles.commands = $(COPY) $$shell_path($$PWD/default.mvmecfg) $$shell_path($$OUT_PWD)
first.depends = $(first) copytemplates copyfiles
export(first.depends)
export(copytemplates.commands)
export(copyfiles.commands)
QMAKE_EXTRA_TARGETS += first copytemplates copyfiles

