GIT_VERSION = $$system(git --git-dir $$shell_path($$PWD/.git) --work-tree $$shell_path($$PWD) describe --always --tags)
DEFINES += GIT_VERSION=\\\"$$GIT_VERSION\\\"

VERSION = $$GIT_VERSION
win32 {
    VERSION ~= s/-\d+-g[a-f0-9]{6,}//
    message("Windows VERSION" $$VERSION)
}

message("Git version" $$GIT_VERSION)

# vim:ft=conf
