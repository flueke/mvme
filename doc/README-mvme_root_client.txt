Quick start for the mvme_root_client
==========================================================================================

* Downloading files
------------------------------------------------------------------------------------------

Some parts required the 'snake-mvme-workspace' but of course any mvme experiment setup
should work.

To get started download the workspace, which includes pre-recorded listfiles, and the mvme
software from the mesytec website.

- workspace: https://mesytec.com/kundendaten/MLL/snake-mvme-workspace.tar.bz2
- mvme: https://mesytec.com/downloads/mvme/beta/mvme-0.9.5.5-85-Linux-x64-feat_mvlc.tar.bz2

Alternatively instead of using prebuilt binaries the mvme sourcecode can be found on
github: https://github.com/flueke/mvme/tree/feat/mvlc

* Unpack and install
------------------------------------------------------------------------------------------

Unpack the mvme tarball, inside the unpacked directory run
  source bin/initMVME

This will setup some enviroment variables so that the next steps can locate required
files.

Still from within the root of the extracted package run
    make -C ${MVME}/share/mvme_root_client install

You should now be able to start `mvme_root_client' and exit it again via Ctrl-C.

* Replaying data to be received by the root client
------------------------------------------------------------------------------------------

Next unpack the snake-mvme-workspace, cd into the workspace and create a directory to hold
the generated code and the ROOT data files, e.g. 'mkdir root'. Run the mvme_root_client
from within this directory.

In another terminal start mvme. Use the 'File -> Open Workspace' menu entry and locate
the snake-mvme-workspace directory. After opening the workspace you should see a tree of
events and modules which resemble the SNAKE setup. Also the mvme analysis will have data
extraction filters setup similar to what the SNAKE analysis is using.
The `event_server' component of mvme will listen on localhost and the root client should
be able to connect immediately.

In mvme select 'Window -> Listfile Browser' or hit Ctrl-4 to open the listfile browser
window. You should see two example listiles: snake_fake_{002,003}.zip. Double click one of
them to have mvme open it. Then in the main window press the 'Start Replay' button.

In the terminal where the mvme_root_client is running you should see that it was able to
connect to mvme and is receiving data. After the replay ends it will print some statistics
and then wait for the next run or replay to begin. Exit via Ctrl-C.

For each run the client should have produced two root files: one containing the 'raw' data
stored in TTrees and one containing the raw, uncalibrated histograms.

* Basics of the mvme_root_client
------------------------------------------------------------------------------------------

The tree storage scheme used by the root client is very similar to what's currently done
in Marabou: a hierarchy of Event and Module objects where each modules data is stored in a
branch of the events tree.

For each defined VME event a C++ event class and classes for the submodules of the event
are generated. For example for the 'pp' event a class 'Event_pp' and several module
classes ('Module_ppA', 'Module_ppB', ...) are generated. See the files Snake_mvme.{h, cxx}
for details. This code is compiled into a ROOT library and loaded via TSystem::Load().

Additionally if it does not exist yet a basic analysis.cc C++ file is generated containing
empty function implementations for each of the defined VME events, e.g. analyze_pp,
analyze_rbs, etc. During a replay or a live DAQ run these functions will be repeatedly
called with instances of the respective event class. This is where the user analysis code
goes.

The mvme_root_client also generates a Makefile and an 'analysis.mk' makefile snippet which
can be used to customize the analysis build.

During runtime the root client runs 'make' to compile the generated code into a ROOT
library and to create a shared object from the user analysis. The shared object is loaded
at runtime using dlopen() and the analysis functions are resolved via dlsym().

The following auto-generated files will be overwritten if needed:

  Makefile
  Snake_mvme.cxx
  Snake_mvme.h
  Snake_mvme_LinkDef.h

The following files are created initially but will not be overwritten on subsequent runs:

  analysis.cxx
  analysis.mk
  mvme_root_premake_hook.C

The last file mvme_root_premake_hook.C is executed as a ROOT macro by the client prior to
invoking 'make'. It is meant as a hook to e.g. generate your own additional code files or
modify the analysis.mk snippet.
