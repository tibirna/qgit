This is qgit.exe statically linked to Trolltech Qt4.3.3 for Windows Open source edition

PREREQUISITES

- You need msysgit (http://code.google.com/p/msysgit/) correctly installed


NOTES

- This version has NOT been tested with cygwin version of git

- In case qgit has problem to find correct msysgit files you should
  add mysysgit\bin directory to your PATH before to start qgit.
  But normally installer should had already take care of this for you.


WINDOWS DEVELOPER INFO

This version has been tested with Visual C++ 2008, there is a VC2008
solution and a project file for your convenience.

Please note, the FIRST TIME you need to generate a Makefile. To do this
the Qt qmake tool must be used. Open a console on the project directory
and type (qmake should be in PATH)

     qmake qgit.pro

That's all, now you can fire your VC2008 IDE, open the qgit solution
file and enjoy ;-)


Please report any problem to the Git Mailing List <git@vger.kernel.org>
specifying [QGit] in your mail subject
