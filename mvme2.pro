TEMPLATE = subdirs
SUBDIRS = src #test

win32 {
    copytemplates.commands = $(COPY_DIR) /E $$shell_path($$PWD/templates) $$shell_path($$OUT_PWD/templates)
}

unix {
    copytemplates.commands = $(COPY_DIR) $$shell_path($$PWD/templates) $$shell_path($$OUT_PWD)
}

first.depends = $(first) copytemplates
export(first.depends)
export(copytemplates.commands)
QMAKE_EXTRA_TARGETS += first copytemplates

# vim:ft=conf
