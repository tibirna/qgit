qgit, a git GUI viewer
======================

With qgit you will be able to browse revisions history, view patch content
and changed files, graphically following different development branches.

Bugs or feature requests should be sent to the git@vger.kernel.org
mailing list.

Main features
-------------

 - View revisions, diffs, files history, files annotation, archive tree.

 - Commit changes visually cherry picking modified files.

 - Apply or save patch series from selected commits, drag and
   drop commits between two instances of qgit.

 - Associate commands sequences, scripts and anything else executable
   to a custom action. Actions can be run from menu and corresponding
   output is grabbed by a terminal window.

 - qgit implements a GUI for the most common StGIT commands like push/pop
   and apply/save patches. You can also create new patches or refresh
   current top one using the same semantics of git commit, i.e. cherry
   picking single modified files.


Installation
------------

You need Qt developer libraries, version 4.3 or later, already installed.
Be sure compiler, qmake (Qt4 version) and Qt4 bin tools are in path.

NOTE: Correct qmake for Qt4 (/usr/lib/qt4/bin/qmake) must be called, in
case also Qt3 version is in path the former must be explicitly invoked.

To compile, install and start qgit:

 - unpack the released tar file or clone from a git public archive

 - (Windows only) open src/src.pro and set the proper GIT_EXEC_DIR value

 - generate Makefiles (only the first time)
   Unix/Linux and Windows:  qmake qgit.pro
   Mac Os X:                qmake -spec macx-g++ qgit.pro

 - (Windows only with VC2008 IDE) open qgit.sln solution file

 - make

 - make install

 - (Windows only) run start_qgit.bat to start the application. You can
    also create a a desktop icon linked to 'start_qgit.bat' and double
    click on it.

Installation directory is $HOME/bin under Linux and the directory of git
exe files under Windows.

You need to run 'qmake qgit.pro' only the first time to generate Makefile
files, then you simply call 'make' and  'make install'. You may need to
run 'qmake qgit.pro' again only after patches that modify 'qgit.pro' or
'src/src.pro' files or in case of 'strange' compile errors.

Remember to manually delete all Makefile* files in 'src/' directory before to
start 'qmake qgit.pro'.


Performance tweaks
------------------

A 'git log' command is used to load the repository at startup or when
refreshing. This is an highly performance critical task. Default method is
based on a temporary file as data exchange facility. This is the fastest
on Linux with a tmpfs filesystem mounted under /tmp, as is common with most
distributions. In case of portability issues it is possible to fallback
on a standard QProcess based interface. To do this uncomment USE_QPROCESS
define in 'src/dataloader.h' before to compile.


Command line arguments
----------------------
Run qgit from a git working directory, command line arguments
are filtered by 'git log'. Some examples:

	qgit --no-merges
	qgit v2.6.18.. include/scsi drivers/scsi
	qgit --since="2 weeks ago" -- kernel/
	qgit -r --name-status release..test

If qgit is launched without arguments or if you change archive with
'open' menu, a dialog for range select is shown.
You can select top and bottom rev tags from the list or paste a
specific revision. Values are passed to 'git log' to narrow
data loading to chosen revisions.


Main view
---------
You can navigate through logs, file names, file history, archive tree.
All the views will be updated accordingly.

Copy/paste is supported on all fields. Copy (CTRL+C) is supported on
all views.

All the references found recursively under .git/refs/ directory are
highlighted according to their type: current branch(HEAD), branch, tag,
other. Reference names and any associated messages can be viewed in status
bar when a tagged revision is selected.

When you right click on main view a context sensitive pop-up menu is shows
available commands and a 'quick jump' tag list.

*Key bindings*::

`-------------`----------------------------------
r             Go to revisions list page
p             Go to patch page
f             Go to file page
<Alt+wheel>   Go to next/previous page
t             Toggle tree view
s             Toggle view of secondary panes
h             Toggle view of revision header
<Home>        Move to first revision
<End>         Move to last revision
i             Move up one revision in main view (global scope)
n, k          Move down one revision in main view (global scope)
<Shift-Up>    Move to previous highlighted line
<Shift-Down>  Move to next highlighted line
<Left>	      Go back in history list
<Right>	      Go forward in history list
<CTRL-plus>   Increase font size
<CTRL-minus>  Decrease font size
<Delete>, b   Scroll content up one page
<Backspace>   Scroll content up one page
<Space>       Scroll content down one page
u             Scroll content up 18 lines
d             Scroll content down 18 lines
-------------------------------------------------

*Directory tree*::
From menu or toolbar button it is possible to show a side panel with
tree view of repository files and directories.
+
Double clicking on a file opens file annotation window. With 'filter by tree'
button it is possible to compress revision list to show only selected
files/directories in tree view.
+
Tree view supports multi-selection. When you right click on a file on tree
view a context sensitive pop-up menu is shows with available commands.

*Working directory changes*::
When 'Check working dir' flag is set, as example from main view context
pop-up menu, a pseudo-revision is shown and highlighted at the top of the
list. Highlight and revision name reflect current working directory status:
'Nothing to commit' or 'Working directory changes' respectively.
+
To check for working directory modified files set corresponding preference in
Edit->Settings->'Working dir'. QGit checks for possible new files added in
working directory using ignoring rules according to git ls-files
specifications, see menu Edit->Settings->'Working dir'.
+
TIP: If you don't need to see modified files in working dir, disable
corresponding setting and start-up time will be shorter.

*Lane info*::
Selecting a lane with mouse right button will display a pop-up
with the list of children and parent. Select one and you jump to it.

*Filter / Highlight*::
Use the combo box to select where you want to filter or highlight on.
Currently supported fields are: log header, log message, revision author,
revision SHA1, file name and patch content.
+
Write a filter string, press filter button and the view
will update showing only commits that contain the filter string,
case insensitive. Toggle filter button to release the filter.
+
Alternatively press the magnifying glass button, in this case matched
lines will be highlighted, you can use <Shift-Up> and <Shift-Down>
keys to browse them. Toggle the button to remove the highlighting.
+
NOTE: In case of patch content regexp filtering, the given string is
interpreted as a POSIX regular expression, not as a simple substring.
+
TIP: Very useful to quick retrieve a sha writing only first 3-4
digits and filtering / highlighting on revision sha. The sha value
can then be copied from SHA field.
+
TIP: It is possible to insert an abbreviated sha directly in the
SHA line edit at the top right of the window. After pressing enter
this will trigger an higlighting of the matched revisions. It is
a kind of shortcut of the previous tip.

*Save patch series*::
After mouse selecting the chosen revisions (use standard CTRL+left click)
for single select or SHIFT+left click for range select), press 'Save
Patch' button or use file menu and a dialog will let you choose patches
destination directory. Then 'git format-patch-script' will be called and
patches created. It is possible to specify additional options with
Edit->Settings menu.

*Apply patch*::
This menu entry is complementary to save patch and it's an interface
to 'git am'.

*Drag and drop*::
It is possible to drag some selected revs from one instance of qgit to another
open on a different archive. In this case 'git format-patch' is used in the
dragging archive to create temporary patches imported in the dropping archive
by 'git am'.

*Make branch*::
Select a revision and open Edit->'Make Branch' or use right click context
pop-up menu. A dialog will be shown asking for a branch name.

*Make tag*::
Select a revision and open Edit->'Make Tag' or use right click context
pop-up menu. Two dialogs will be shown, the first asking for a tag name, the
second for a tag message (not mandatory). If a non empty message is written,
this will be saved together with the tag. Tags and tag messages can be viewed
in status bar when a tagged revision is selected.

*Delete tag*::
Select a tagged revision and open Edit->'Delete Tag' or use right click
context pop-up menu. After confirmation the selected revision will be
untagged.

*Save file*::
Select a file from tree or file list and open File->'Save file as' or use the
tree view context sensitive pop-up menu (right click), a dialog will be shown
asking for a file name (default to current) and destination directory. Input
a valid name, press OK and the file will be saved.

*Commit changes*::
When enabled with Edit->Settings->'Working dir'->'Diff against working dir'
and there is something committable, a special highlighted first revision is
shown, with the status of the archive and the possible pending stuff.
From Edit->Commit it is then possible to invoke the commit dialog.
+
In commit dialog select the files to commit or, simply, to sync with index
(call 'git update-index' on them). A proper commit message may be entered and,
after confirmation, changes are committed and a new revision is created.
+
It is also possible to amend last commit. The Edit->Amend commit opens the
same dialog, but changes are added to the head commit instead of creating new
commit.
+
The core commit function is performed by 'git commit'.
+
TIP: It is possible to use a template for commit message, use
Edit->Settings->Commit to define template file path.


Patch viewer
------------
To open patch tab use context menu, double click on a revision or file in
main view or select View->'View patch' menu (CTRL+P). The patch shown is
the diff of current selected commit against:

 - Parent (default)
 - HEAD
 - Selected SHA or reference name

In the last case SHA is chosen by writing or pasting a tree-ish or a reference
names in the corresponding field and pressing return. You get the same result
also with a CTRL+right click on a revision in main list. Selected target
will be highlighted. CTRL+right click again on the highlighted revision to
release the filter.

With the 'filter' button at the right of the tool bar it is possible to
toggle the display of removed code lines. This can be useful to easy
reading of the patch.

External diff tool
--------------------

From 'View->External diff' it is possible to invoke an external diff tool,
as example to view the diffs in a two vertical tiled windows.

External diff tool shows the diffs between two files.
First file is the current selected file of current revision.
Second file is the same file of the parent revision or of a specific revision
if 'diff to sha' feature is enabled (diff target is highlighted, see above).

Default external viewer is kompare, but it is possible to set a preferred one
from 'Edit->Settings->External Diff Tool'.


File viewer
-----------

It is possible to view file contents of any file at any revision time in
history.

*File list panel*::
In the bottom right of main view a list of files modified by current
revision is shown. Selecting a file name will update the patch view
to center on the file. File names colors use the following convention

 - black for modified files
 - green for new files
 - red for removed files
 - dark blue for renamed/copied files

*Merge files*::
In case of merges the groups of files corresponding to each merge parent
are separated by two empty lines.
+
In case of merges you can chose between to see all the merge files or only
the interesting ones (default), i.e. the files modified by more then one
merge parent.

*File content*::
To view file content double click on a file name in tree view, or use context
menu in file list or select View->'View file' menu (CTRL+A).
+
In file view page will be shown current revision's file content and file
history.
+
It is possible to copy to the clipboard the selected content with CTRL+C or
with the corresponding button.

*File annotations*::
On opening or updating file viewer, file history will be retrieved from archive
together with file content. Annotations are then calculated in background
and the view is updated when ready.
+
Double clicking on an annotation index number will update history list
to point to corresponding revision.
+
Hovering the mouse over an annotation index will show a tool tip with the
corresponding revision description.
+
File content will change too, to show new selected revision file. To keep
the same view content after double clicking, probably what you want, just pin
it with 'Pin view' check button. Next to the check button there is a spinbox
to show/select the current revision number.
+
Double click on history list entry to update main, patch and tree views to
corresponding revision.

*Code region filter*::
When annotation info is available the 'filter' button is enabled and it is
possible to mouse select a region of file content. Then, when pressing
the filter button, only revisions that modify the selected region will be
visible. Selected code region is highlighted and a shrunken history is
shown. Filter button is a toggle button, so just press it again to
release the filter.

*Syntax highlighter*::
If GNU Source-highlight (http://www.gnu.org/software/src-highlite/) is
installed and in PATH then it is possible to toggle source code highlight
pressing the 'Color text' tool button. Please refer to Source-highlight
site for the list of supported languages and additional documentation.


Actions
------
Actions can be added/removed using a dedicated dialog invoked
from 'Actions->Setup actions...' menu. Actions can be activated
clicking on their name from the Actions menu.

Each action can be associated to a list of any type of git or shell
commands or to an external script.

While an action is running a terminal window is shown to display the
corresponding output.

An action can also ask for command line arguments before to run so
to allow for maximum flexibility.

NOTE: command line arguments are always appended to the first command only.
This lets you define an action like:

  git fetch
  git merge

And if you type 'origin' when prompted, the action executed will be:

  git fetch origin
  git merge

If you need a more complex arguments passing with a shell like notation
define a script and associate your action to it.


Integration with StGIT
----------------------
When a StGIT stack is found on top of a git archive, qgit transparently
handles the added information.

Integration with StGIT is implemented both by new and modified functions.

.New functions are automatically activated:

 - Visualization of applied and unapplied patches in main view.
 - Interface to push/pop patches by a mouse right click on selected items.
   Push supports also multi-selection.

.Existing functions change behavior:

 - Amend commit dialog refreshes top stack patch with modified files instead
   of amending the commit. It is appropriately renamed in the menu.
 - Commit dialog creates a new patch on the top of the stack filled with
   modified working directory content instead of commit a new revision to
   git repository.
 - Apply patch changes to interface StGIT import and fold commands instead
   of applying patch directly on the git repository.
