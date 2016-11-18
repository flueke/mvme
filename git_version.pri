GIT_VERSION = $$system(git --git-dir $$PWD/.git --work-tree $$PWD/ describe --always --tags)
DEFINES += GIT_VERSION=\\\"$$GIT_VERSION\\\"

VERSION = $$GIT_VERSION
win32 {
    VERSION ~= s/-\d+-g[a-f0-9]{6,}//
}

message("Git version" $$GIT_VERSION)
