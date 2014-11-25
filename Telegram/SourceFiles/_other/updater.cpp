/*
This file is part of Telegram Desktop,
an official desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "updater.h"

bool _debug = false;

wstring exeName, exeDir;

bool equal(const wstring &a, const wstring &b) {
	return !_wcsicmp(a.c_str(), b.c_str());
}

void updateError(const WCHAR *msg, DWORD errorCode) {
	WCHAR errMsg[2048];
	LPTSTR errorText = NULL, errorTextDefault = L"(Unknown error)";
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&errorText, 0, 0);
	if (!errorText) {
		errorText = errorTextDefault;
	}
	wsprintf(errMsg, L"%s, error code: %d\nError message: %s", msg, errorCode, errorText);

	MessageBox(0, errMsg, L"Update error!", MB_ICONERROR);

	if (errorText != errorTextDefault) {
		LocalFree(errorText);
	}
}

HANDLE _logFile = 0;
void openLog() {
	if (!_debug || _logFile) return;
	wstring logPath = L"DebugLogs";
	if (!CreateDirectory(logPath.c_str(), NULL)) {
		DWORD errorCode = GetLastError();
		if (errorCode && errorCode != ERROR_ALREADY_EXISTS) {
			updateError(L"Failed to create log directory", errorCode);
			return;
		}
	}

	SYSTEMTIME stLocalTime;

	GetLocalTime(&stLocalTime);

	static const int maxFileLen = MAX_PATH * 10;
	WCHAR logName[maxFileLen];
	wsprintf(logName, L"DebugLogs\\%04d%02d%02d_%02d%02d%02d_upd.txt",
		stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
		stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond);
	_logFile = CreateFile(logName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (_logFile == INVALID_HANDLE_VALUE) { // :(
		updateError(L"Failed to create log file", GetLastError());
		_logFile = 0;
		return;
	}
}

void closeLog() {
	if (!_logFile) return;

	CloseHandle(_logFile);
	_logFile = 0;
}

void writeLog(const wstring &msg) {
	if (!_logFile) return;

	wstring full = msg + L'\n';
	DWORD written = 0;
	BOOL result = WriteFile(_logFile, full.c_str(), full.size() * sizeof(wchar_t), &written, 0);
	if (!result) {
		updateError((L"Failed to write log entry '" + msg + L"'").c_str(), GetLastError());
		closeLog();
		return;
	}
	BOOL flushr = FlushFileBuffers(_logFile);
	if (!flushr) {
		updateError((L"Failed to flush log on entry '" + msg + L"'").c_str(), GetLastError());
		closeLog();
		return;
	}
}

void delFolder() {
	wstring delPath = L"tupdates\\ready", delFolder = L"tupdates";
	WCHAR path[4096];
	memcpy(path, delPath.c_str(), (delPath.size() + 1) * sizeof(WCHAR));
	path[delPath.size() + 1] = 0;
	writeLog(L"Fully clearing path '" + delPath + L"'..");
	SHFILEOPSTRUCT file_op = {
		NULL,
		FO_DELETE,
		path,
		L"",
		FOF_NOCONFIRMATION |
		FOF_NOERRORUI |
		FOF_SILENT,
		false,
		0,
		L""
	};
	int res = SHFileOperation(&file_op);
	if (res) writeLog(L"Error: failed to clear path! :(");
	RemoveDirectory(delFolder.c_str());
}

bool update() {
	writeLog(L"Update started..");

	wstring updDir = L"tupdates\\ready";

	deque<wstring> dirs;
	dirs.push_back(updDir);

	deque<wstring> from, to, forcedirs;

	do {
		wstring dir = dirs.front();
		dirs.pop_front();

		wstring toDir = exeDir;
		if (dir.size() > updDir.size() + 1) {
			toDir += (dir.substr(updDir.size() + 1) + L"\\");
			forcedirs.push_back(toDir);
			writeLog(L"Parsing dir '" + toDir + L"' in update tree..");
		}

		WIN32_FIND_DATA findData;
		HANDLE findHandle = FindFirstFileEx((dir + L"\\*").c_str(), FindExInfoStandard, &findData, FindExSearchNameMatch, 0, 0);
		if (findHandle == INVALID_HANDLE_VALUE) {
			DWORD errorCode = GetLastError();
			if (errorCode == ERROR_PATH_NOT_FOUND) { // no update is ready
				return true;
			}
			writeLog(L"Error: failed to find update files :(");
			updateError(L"Failed to find update files", errorCode);
			delFolder();
			return false;
		}

		do {
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (findData.cFileName != wstring(L".") && findData.cFileName != wstring(L"..")) {
					dirs.push_back(dir + L"\\" + findData.cFileName);
					writeLog(L"Added dir '" + dir + L"\\" + findData.cFileName + L"' in update tree..");
				}
			} else {
				wstring fname = dir + L"\\" + findData.cFileName;
				wstring tofname = exeDir + fname.substr(updDir.size() + 1);
				if (equal(tofname, exeName)) { // bad update - has Updater.exe - delete all dir
					writeLog(L"Error: bad update, has Updater.exe! '" + tofname + L"' equal '" + exeName + L"'");
					delFolder();
					return false;
				}
				from.push_back(fname);
				to.push_back(tofname);
				writeLog(L"Added file '" + fname + L"' to be copied to '" + tofname + L"'");
			}
		} while (FindNextFile(findHandle, &findData));
		DWORD errorCode = GetLastError();
		if (errorCode && errorCode != ERROR_NO_MORE_FILES) { // everything is found
			writeLog(L"Error: failed to find next update file :(");
			updateError(L"Failed to find next update file", errorCode);
			delFolder();
			return false;
		}
		FindClose(findHandle);
	} while (!dirs.empty());

	for (size_t i = 0; i < forcedirs.size(); ++i) {
		wstring forcedir = forcedirs[i];
		writeLog(L"Forcing dir '" + forcedir + L"'..");
		if (!forcedir.empty() && !CreateDirectory(forcedir.c_str(), NULL)) {
			DWORD errorCode = GetLastError();
			if (errorCode && errorCode != ERROR_ALREADY_EXISTS) {
				writeLog(L"Error: failed to create dir '" + forcedir + L"'..");
				updateError(L"Failed to create directory", errorCode);
				delFolder();
				return false;
			}
			writeLog(L"Already exists!");
		}
	}

	for (size_t i = 0; i < from.size(); ++i) {
		wstring fname = from[i], tofname = to[i];
		BOOL copyResult;
		do {
			writeLog(L"Copying file '" + fname + L"' to '" + tofname + L"'..");
			int copyTries = 0;
			do {
				copyResult = CopyFile(fname.c_str(), tofname.c_str(), FALSE);
				if (copyResult == FALSE) {
					++copyTries;
					Sleep(100);
				} else {
					break;
				}
			} while (copyTries < 30);
			if (copyResult == FALSE) {
				writeLog(L"Error: failed to copy, asking to retry..");
				WCHAR errMsg[2048];
				wsprintf(errMsg, L"Failed to update Telegram :(\n%s is not accessible.", tofname);
				if (MessageBox(0, errMsg, L"Update error!", MB_ICONERROR | MB_RETRYCANCEL) != IDRETRY) {
					delFolder();
					return false;
				}
			}
		} while (copyResult == FALSE);
	}

	writeLog(L"Update succeed! Clearing folder..");
	delFolder();
	return true;
}

void updateRegistry() {
	writeLog(L"Updating registry..");

	HANDLE versionFile = CreateFile(L"tdata\\version", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (versionFile != INVALID_HANDLE_VALUE) {
		DWORD versionNum = 0, versionLen = 0, readLen = 0;
		WCHAR versionStr[32];
		if (ReadFile(versionFile, &versionNum, sizeof(DWORD), &readLen, NULL) != TRUE || readLen != sizeof(DWORD)) {
			versionNum = 0;
		} else if (ReadFile(versionFile, &versionLen, sizeof(DWORD), &readLen, NULL) != TRUE || readLen != sizeof(DWORD) || versionLen > 63) {
			versionNum = 0;
		} else if (ReadFile(versionFile, versionStr, versionLen, &readLen, NULL) != TRUE || readLen != versionLen) {
			versionNum = 0;
		}
		CloseHandle(versionFile);
		writeLog(L"Version file read.");
		if (versionNum) {
			versionStr[versionLen / 2] = 0;
			HKEY rkey;
			LSTATUS status = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{53F49750-6209-4FBF-9CA8-7A333C87D1ED}_is1", 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &rkey);
			if (status == ERROR_SUCCESS) {
				writeLog(L"Checking registry install location..");
				static const int bufSize = 4096;
				DWORD locationType, locationSize = bufSize * 2;
				WCHAR locationStr[bufSize], exp[bufSize];
				if (RegQueryValueEx(rkey, L"InstallLocation", 0, &locationType, (BYTE*)locationStr, &locationSize) == ERROR_SUCCESS) {
					locationSize /= 2;
					if (locationStr[locationSize - 1]) {
						locationStr[locationSize++] = 0;
					}
					if (locationType == REG_EXPAND_SZ) {
						DWORD copy = ExpandEnvironmentStrings(locationStr, exp, bufSize);
						if (copy <= bufSize) {
							memcpy(locationStr, exp, copy * sizeof(WCHAR));
						}
					}
					if (locationType == REG_EXPAND_SZ || locationType == REG_SZ) {
						if (PathCanonicalize(exp, locationStr) == TRUE) {
							memcpy(locationStr, exp, bufSize * sizeof(WCHAR));
							if (GetFullPathName(L".", bufSize, exp, 0) < bufSize) {
								wstring installpath = locationStr, mypath = exp;
								if (installpath == mypath + L"\\" || true) { // always update reg info, if we found it
									WCHAR nameStr[bufSize], dateStr[bufSize], publisherStr[bufSize], icongroupStr[bufSize];
									SYSTEMTIME stLocalTime;
									GetLocalTime(&stLocalTime);
									RegSetValueEx(rkey, L"DisplayVersion", 0, REG_SZ, (BYTE*)versionStr, ((versionLen / 2) + 1) * sizeof(WCHAR));
									wsprintf(nameStr, L"Telegram Desktop version %s", versionStr);
									RegSetValueEx(rkey, L"DisplayName", 0, REG_SZ, (BYTE*)nameStr, (wcslen(nameStr) + 1) * sizeof(WCHAR));
									wsprintf(publisherStr, L"Telegram Messenger LLP");
									RegSetValueEx(rkey, L"Publisher", 0, REG_SZ, (BYTE*)publisherStr, (wcslen(publisherStr) + 1) * sizeof(WCHAR));
									wsprintf(icongroupStr, L"Telegram Desktop");
									RegSetValueEx(rkey, L"Inno Setup: Icon Group", 0, REG_SZ, (BYTE*)icongroupStr, (wcslen(icongroupStr) + 1) * sizeof(WCHAR));
									wsprintf(dateStr, L"%04d%02d%02d", stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay);
									RegSetValueEx(rkey, L"InstallDate", 0, REG_SZ, (BYTE*)dateStr, (wcslen(dateStr) + 1) * sizeof(WCHAR));

									WCHAR *appURL = L"https://tdesktop.com";
									RegSetValueEx(rkey, L"HelpLink", 0, REG_SZ, (BYTE*)appURL, (wcslen(appURL) + 1) * sizeof(WCHAR));
									RegSetValueEx(rkey, L"URLInfoAbout", 0, REG_SZ, (BYTE*)appURL, (wcslen(appURL) + 1) * sizeof(WCHAR));
									RegSetValueEx(rkey, L"URLUpdateInfo", 0, REG_SZ, (BYTE*)appURL, (wcslen(appURL) + 1) * sizeof(WCHAR));
								}
							}
						}
					}
				}
				RegCloseKey(rkey);
			}
		}
	}
}

#include <ShlObj.h>

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdParamarg, int cmdShow) {
	openLog();

#ifdef _NEED_WIN_GENERATE_DUMP
	_oldWndExceptionFilter = SetUnhandledExceptionFilter(_exceptionFilter);
#endif

	writeLog(L"Updaters started..");

	LPWSTR *args;
	int argsCount;

	bool needupdate = false, autostart = false, debug = false;
	args = CommandLineToArgvW(GetCommandLine(), &argsCount);
	if (args) {
		for (int i = 1; i < argsCount; ++i) {
			if (equal(args[i], L"-update")) {
				needupdate = true;
			} else if (equal(args[i], L"-autostart")) {
				autostart = true;
			} else if (equal(args[i], L"-debug")) {
				debug = _debug = true;
				openLog();
			}
		}
		if (needupdate) writeLog(L"Need to update!");
		if (autostart) writeLog(L"From autostart!");

		exeName = args[0];
		writeLog(L"Exe name is: " + exeName);
		if (exeName.size() > 11) {
			if (equal(exeName.substr(exeName.size() - 11), L"Updater.exe")) {
				exeDir = exeName.substr(0, exeName.size() - 11);
				writeLog(L"Exe dir is: " + exeDir);
				if (needupdate && update()) {
					updateRegistry();
				}
			} else {
				writeLog(L"Error: bad exe name!");
			}
		} else {
			writeLog(L"Error: short exe name!");
		}
		LocalFree(args);
	} else {
		writeLog(L"Error: No command line arguments!");
	}

	wstring targs = L"-noupdate";
	if (autostart) targs += L" -autostart";
	if (debug) targs += L" -debug";

	ShellExecute(0, 0, (exeDir + L"Telegram.exe").c_str(), targs.c_str(), 0, SW_SHOWNORMAL);

	writeLog(L"Executed Telegram.exe, closing log and quiting..");
	closeLog();

	return 0;
}

#ifdef _NEED_WIN_GENERATE_DUMP
static const WCHAR *_programName = L"Telegram Desktop"; // folder in APPDATA, if current path is unavailable for writing
static const WCHAR *_exeName = L"Updater.exe";

LPTOP_LEVEL_EXCEPTION_FILTER _oldWndExceptionFilter = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_miniDumpWriteDump)(
    _In_ HANDLE hProcess,
    _In_ DWORD ProcessId,
    _In_ HANDLE hFile,
    _In_ MINIDUMP_TYPE DumpType,
    _In_opt_ PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    _In_opt_ PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    _In_opt_ PMINIDUMP_CALLBACK_INFORMATION CallbackParam
);
t_miniDumpWriteDump miniDumpWriteDump = 0;

HANDLE _generateDumpFileAtPath(const WCHAR *path) {
	static const int maxFileLen = MAX_PATH * 10;

	WCHAR szPath[maxFileLen];
	wsprintf(szPath, L"%stdumps\\", path);

    if (!CreateDirectory(szPath, NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS) {
			return 0;
		}
	}

    WCHAR szFileName[maxFileLen];
	WCHAR szExeName[maxFileLen];

	wcscpy_s(szExeName, _exeName);
	WCHAR *dotFrom = wcschr(szExeName, WCHAR(L'.'));
	if (dotFrom) {
		wsprintf(dotFrom, L"");
	}

    SYSTEMTIME stLocalTime;

    GetLocalTime(&stLocalTime);

    wsprintf(szFileName, L"%s%s-%s-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp", 
             szPath, szExeName, updaterVersionStr, 
             stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay, 
             stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond, 
             GetCurrentProcessId(), GetCurrentThreadId());
    return CreateFile(szFileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
}

void _generateDump(EXCEPTION_POINTERS* pExceptionPointers) {
	static const int maxFileLen = MAX_PATH * 10;

	closeLog();

	HMODULE hDll = LoadLibrary(L"DBGHELP.DLL");
	if (!hDll) return;

	miniDumpWriteDump = (t_miniDumpWriteDump)GetProcAddress(hDll, "MiniDumpWriteDump");
	if (!miniDumpWriteDump) return;

	HANDLE hDumpFile = 0;

	WCHAR szPath[maxFileLen];
	DWORD len = GetModuleFileName(GetModuleHandle(0), szPath, maxFileLen);
	if (!len) return;

	WCHAR *pathEnd = szPath  + len;

	if (!_wcsicmp(pathEnd - wcslen(_exeName), _exeName)) {
		wsprintf(pathEnd - wcslen(_exeName), L"");
		hDumpFile = _generateDumpFileAtPath(szPath);
	}
	if (!hDumpFile || hDumpFile == INVALID_HANDLE_VALUE) {
		WCHAR wstrPath[maxFileLen];
		DWORD wstrPathLen;
		if (wstrPathLen = GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
			wsprintf(wstrPath + wstrPathLen, L"\\%s\\", _programName);
			hDumpFile = _generateDumpFileAtPath(wstrPath);
		}
	}
	
	if (!hDumpFile || hDumpFile == INVALID_HANDLE_VALUE) {
		return;
	}

	MINIDUMP_EXCEPTION_INFORMATION ExpParam = {0};
    ExpParam.ThreadId = GetCurrentThreadId();
    ExpParam.ExceptionPointers = pExceptionPointers;
    ExpParam.ClientPointers = TRUE;

    miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithDataSegs, &ExpParam, NULL, NULL);
}

LONG CALLBACK _exceptionFilter(EXCEPTION_POINTERS* pExceptionPointers) {
	_generateDump(pExceptionPointers);
    return _oldWndExceptionFilter ? (*_oldWndExceptionFilter)(pExceptionPointers) : EXCEPTION_CONTINUE_SEARCH;
}

#endif
