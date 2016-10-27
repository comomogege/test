/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <string>
#include <deque>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <cstdarg>
#include <ctime>
#include <iostream>

using std::string;
using std::deque;
using std::cout;

bool do_mkdir(const char *path) { // from http://stackoverflow.com/questions/675039/how-can-i-create-directory-tree-in-c-linux
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) {
        /* Directory does not exist. EEXIST for race condition */
        if (mkdir(path, S_IRWXU) != 0 && errno != EEXIST) return false;
    } else if (!S_ISDIR(statbuf.st_mode)) {
        errno = ENOTDIR;
        return false;
    }

    return true;
}

bool _debug = false;
string exeName, exeDir, workDir;

FILE *_logFile = 0;
void openLog() {
    if (!_debug || _logFile) return;

    if (!do_mkdir((workDir + "DebugLogs").c_str())) {
        return;
    }

    time_t timer;

    time(&timer);
    struct tm *t = localtime(&timer);

    static const int maxFileLen = 65536;
    char logName[maxFileLen];
    sprintf(logName, "%sDebugLogs/%04d%02d%02d_%02d%02d%02d_upd.txt", workDir.c_str(),
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    _logFile = fopen(logName, "w");
}

void closeLog() {
    if (!_logFile) return;

    fclose(_logFile);
    _logFile = 0;
}

void writeLog(const char *format, ...) {
    if (!_logFile) return;

    va_list args;
    va_start(args, format);
    vfprintf(_logFile, format, args);
    fprintf(_logFile, "\n");
    fflush(_logFile);
    va_end(args);
}

bool copyFile(const char *from, const char *to) {
    FILE *ffrom = fopen(from, "rb"), *fto = fopen(to, "wb");
    if (!ffrom) {
        if (fto) fclose(fto);
        return false;
    }
    if (!fto) {
        fclose(ffrom);
        return false;
    }
    static const int BufSize = 65536;
    char buf[BufSize];
    while (size_t size = fread(buf, 1, BufSize, ffrom)) {
        fwrite(buf, 1, size, fto);
    }

    struct stat fst; // from http://stackoverflow.com/questions/5486774/keeping-fileowner-and-permissions-after-copying-file-in-c
    //let's say this wont fail since you already worked OK on that fp
    if (fstat(fileno(ffrom), &fst) != 0) {
        fclose(ffrom);
        fclose(fto);
        return false;
    }
    //update to the same uid/gid
    if (fchown(fileno(fto), fst.st_uid, fst.st_gid) != 0) {
        fclose(ffrom);
        fclose(fto);
        return false;
    }
    //update the permissions
    if (fchmod(fileno(fto), fst.st_mode) != 0) {
        fclose(ffrom);
        fclose(fto);
        return false;
    }

    fclose(ffrom);
    fclose(fto);

    return true;
}

bool remove_directory(const string &path) { // from http://stackoverflow.com/questions/2256945/removing-a-non-empty-directory-programmatically-in-c-or-c
    DIR *d = opendir(path.c_str());
    writeLog("Removing dir '%s'", path.c_str());

    if (!d) {
        writeLog("Could not open dir '%s'", path.c_str());
        return (errno == ENOENT);
    }

    while (struct dirent *p = readdir(d)) {
        /* Skip the names "." and ".." as we don't want to recurse on them. */
        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

        string fname = path + '/' + p->d_name;
        struct stat statbuf;
        writeLog("Trying to get stat() for '%s'", fname.c_str());
        if (!stat(fname.c_str(), &statbuf)) {
            if (S_ISDIR(statbuf.st_mode)) {
                if (!remove_directory(fname.c_str())) {
                    closedir(d);
                    return false;
                }
            } else {
                writeLog("Unlinking file '%s'", fname.c_str());
                if (unlink(fname.c_str())) {
                    writeLog("Failed to unlink '%s'", fname.c_str());
                    closedir(d);
                    return false;
                }
            }
        } else {
            writeLog("Failed to call stat() on '%s'", fname.c_str());
        }
    }
    closedir(d);

    writeLog("Finally removing dir '%s'", path.c_str());
    return !rmdir(path.c_str());
}

bool mkpath(const char *path) {
    int status = 0, pathsize = strlen(path) + 1;
    char *copypath = new char[pathsize];
    memcpy(copypath, path, pathsize);

    char *pp = copypath, *sp;
    while (status == 0 && (sp = strchr(pp, '/')) != 0) {
        if (sp != pp) {
            /* Neither root nor double slash in path */
            *sp = '\0';
            if (!do_mkdir(copypath)) {
                delete[] copypath;
                return false;
            }
            *sp = '/';
        }
        pp = sp + 1;
    }
    delete[] copypath;
    return do_mkdir(path);
}

bool equal(string a, string b) {
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return a == b;
}

void delFolder() {
    string delPathOld = workDir + "tupdates/ready", delPath = workDir + "tupdates/temp", delFolder = workDir + "tupdates";
    writeLog("Fully clearing old path '%s'..", delPathOld.c_str());
    if (!remove_directory(delPathOld)) {
        writeLog("Failed to clear old path! :( New path was used?..");
    }
	writeLog("Fully clearing path '%s'..", delPath.c_str());
	if (!remove_directory(delPath)) {
		writeLog("Error: failed to clear path! :(");
	}
	rmdir(delFolder.c_str());
}

bool update() {
    writeLog("Update started..");

    string updDir = workDir + "tupdates/temp", readyFilePath = workDir + "tupdates/temp/ready", tdataDir = workDir + "tupdates/temp/tdata";
	{
		FILE *readyFile = fopen(readyFilePath.c_str(), "rb");
		if (readyFile) {
			fclose(readyFile);
            writeLog("Ready file found! Using new path '%s'..", updDir.c_str());
        } else {
            updDir = workDir + "tupdates/ready"; // old
            tdataDir = workDir + "tupdates/ready/tdata";
            writeLog("Ready file not found! Using old path '%s'..", updDir.c_str());
        }
	}

    deque<string> dirs;
	dirs.push_back(updDir);

    deque<string> from, to, forcedirs;

	do {
        string dir = dirs.front();
		dirs.pop_front();

        string toDir = exeDir;
		if (dir.size() > updDir.size() + 1) {
            toDir += (dir.substr(updDir.size() + 1) + '/');
			forcedirs.push_back(toDir);
            writeLog("Parsing dir '%s' in update tree..", toDir.c_str());
		}

        DIR *d = opendir(dir.c_str());
        if (!d) {
            writeLog("Failed to open dir %s", dir.c_str());
            return false;
        }

        while (struct dirent *p = readdir(d)) {
            /* Skip the names "." and ".." as we don't want to recurse on them. */
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

            string fname = dir + '/' + p->d_name;
            struct stat statbuf;
            if (fname.substr(0, tdataDir.size()) == tdataDir && (fname.size() <= tdataDir.size() || fname.at(tdataDir.size()) == '/')) {
                writeLog("Skipping 'tdata' path '%s'", fname.c_str());
            } else if (!stat(fname.c_str(), &statbuf)) {
                if (S_ISDIR(statbuf.st_mode)) {
                    dirs.push_back(fname);
                    writeLog("Added dir '%s' in update tree..", fname.c_str());
                } else {
                    string tofname = exeDir + fname.substr(updDir.size() + 1);
                    if (equal(tofname, exeName)) { // bad update - has Updater - delete all dir
                        writeLog("Error: bad update, has Updater! '%s' equal '%s'", tofname.c_str(), exeName.c_str());
                        delFolder();
                        return false;
                    }
					if (fname == readyFilePath) {
						writeLog("Skipped ready file '%s'", fname.c_str());
                    } else {
						from.push_back(fname);
						to.push_back(tofname);
						writeLog("Added file '%s' to be copied to '%s'", fname.c_str(), tofname.c_str());
					}
                }
            } else {
                writeLog("Could not get stat() for file %s", fname.c_str());
            }
        }
        closedir(d);
	} while (!dirs.empty());

	for (size_t i = 0; i < forcedirs.size(); ++i) {
        string forcedir = forcedirs[i];
        writeLog("Forcing dir '%s'..", forcedir.c_str());
        if (!forcedir.empty() && !mkpath(forcedir.c_str())) {
            writeLog("Error: failed to create dir '%s'..", forcedir.c_str());
            delFolder();
            return false;
		}
	}

	for (size_t i = 0; i < from.size(); ++i) {
        string fname = from[i], tofname = to[i];
        writeLog("Copying file '%s' to '%s'..", fname.c_str(), tofname.c_str());
        int copyTries = 0, triesLimit = 30;
        do {
            if (!copyFile(fname.c_str(), tofname.c_str())) {
                ++copyTries;
                usleep(100000);
            } else {
                break;
            }
        } while (copyTries < triesLimit);
        if (copyTries == triesLimit) {
            writeLog("Error: failed to copy, asking to retry..");
            delFolder();
            return false;
        }
    }

    writeLog("Update succeed! Clearing folder..");
    delFolder();
	return true;
}

int main(int argc, char *argv[]) {
    bool needupdate = true, autostart = false, debug = false, tosettings = false, startintray = false, testmode = false;

    char *key = 0, *crashreport = 0;
    for (int i = 1; i < argc; ++i) {
        if (equal(argv[i], "-noupdate")) {
            needupdate = false;
        } else if (equal(argv[i], "-autostart")) {
            autostart = true;
		} else if (equal(argv[i], "-debug")) {
			debug = _debug = true;
		} else if (equal(argv[i], "-startintray")) {
			startintray = true;
		} else if (equal(argv[i], "-testmode")) {
			testmode = true;
        } else if (equal(argv[i], "-tosettings")) {
            tosettings = true;
        } else if (equal(argv[i], "-key") && ++i < argc) {
            key = argv[i];
        } else if (equal(argv[i], "-workpath") && ++i < argc) {
            workDir = argv[i];
		} else if (equal(argv[i], "-crashreport") && ++i < argc) {
			crashreport = argv[i];
		}
    }
    openLog();

    writeLog("Updater started..");
    for (int i = 0; i < argc; ++i) {
        writeLog("Argument: '%s'", argv[i]);
    }
    if (needupdate) writeLog("Need to update!");
    if (autostart) writeLog("From autostart!");

    exeName = argv[0];
    writeLog("Exe name is: %s", exeName.c_str());
    if (exeName.size() >= 7) {
        if (equal(exeName.substr(exeName.size() - 7), "Updater")) {
            exeDir = exeName.substr(0, exeName.size() - 7);
            writeLog("Exe dir is: %s", exeDir.c_str());
            if (needupdate) {
                if (workDir.empty()) { // old app launched, update prepared in tupdates/ready (not in tupdates/temp)
                    writeLog("No workdir, trying to figure it out");
                    struct passwd *pw = getpwuid(getuid());
                    if (pw && pw->pw_dir && strlen(pw->pw_dir)) {
                        string tryDir = pw->pw_dir + string("/.TelegramDesktop/");
                        struct stat statbuf;
                        writeLog("Trying to use '%s' as workDir, getting stat() for tupdates/ready", tryDir.c_str());
                        if (!stat((tryDir + "tupdates/ready").c_str(), &statbuf)) {
                            writeLog("Stat got");
                            if (S_ISDIR(statbuf.st_mode)) {
                                writeLog("It is directory, using home work dir");
                                workDir = tryDir;
                            }
                        }
                    }
                    if (workDir.empty()) {
                        workDir = exeDir;

                        struct stat statbuf;
                        writeLog("Trying to use current as workDir, getting stat() for tupdates/ready");
                        if (!stat("tupdates/ready", &statbuf)) {
                            writeLog("Stat got");
                            if (S_ISDIR(statbuf.st_mode)) {
                                writeLog("It is directory, using current dir");
                                workDir = string();
                            }
                        }
                    }
                } else {
                    writeLog("Passed workpath is '%s'", workDir.c_str());
                }
                update();
            }
        } else {
            writeLog("Error: bad exe name!");
        }
    } else {
        writeLog("Error: short exe name!");
    }

    static const int MaxLen = 65536, MaxArgsCount = 128;

    char path[MaxLen] = {0};
    strcpy(path, (exeDir + "Telegram").c_str());

    char *args[MaxArgsCount] = {0}, p_noupdate[] = "-noupdate", p_autostart[] = "-autostart", p_debug[] = "-debug", p_tosettings[] = "-tosettings", p_key[] = "-key", p_startintray[] = "-startintray", p_testmode[] = "-testmode";
    int argIndex = 0;
    args[argIndex++] = path;
	if (crashreport) {
		args[argIndex++] = crashreport;
	} else {
		args[argIndex++] = p_noupdate;
		if (autostart) args[argIndex++] = p_autostart;
		if (debug) args[argIndex++] = p_debug;
		if (startintray) args[argIndex++] = p_startintray;
		if (testmode) args[argIndex++] = p_testmode;
		if (tosettings) args[argIndex++] = p_tosettings;
		if (key) {
			args[argIndex++] = p_key;
			args[argIndex++] = key;
		}
	}
    pid_t pid = fork();
    switch (pid) {
    case -1: writeLog("fork() failed!"); return 1;
    case 0: execv(path, args); return 1;
    }

    writeLog("Executed Telegram, closing log and quitting..");
	closeLog();

	return 0;
}
