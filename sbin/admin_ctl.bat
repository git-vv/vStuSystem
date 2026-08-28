@echo off
REM ==================================================================
REM admin_ctl.bat - Admin account management script (Windows)
REM
REM Usage:
REM   sbin\admin_ctl.bat -c <conf_path> <command>
REM
REM Commands:
REM   reset   Reset admin password to default (admin123)
REM   show    Show admin username and account info
REM   clean   Delete admin account
REM
REM Example:
REM   sbin\admin_ctl.bat -c conf\register_student.conf clean
REM
REM Equivalent to sbin/admin_ctl.sh on Linux.
REM ==================================================================

setlocal enabledelayedexpansion

set "CONF_PATH="
set "DEFAULT_PWD=admin123"
set "COMMAND="

REM Parse arguments
:parse_args
if "%~1"=="" goto :parse_done
if /i "%~1"=="-c" (
    if "%~2"=="" (
        echo Error: option -c requires an argument
        goto :usage
    )
    set "CONF_PATH=%~2"
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="--help" goto :usage
REM First non-option argument is the command
if "%COMMAND%"=="" (
    set "COMMAND=%~1"
    shift
    goto :parse_args
)
shift
goto :parse_args

:parse_done

if "%CONF_PATH%"=="" goto :usage
if "%COMMAND%"=="" goto :usage

if not exist "%CONF_PATH%" (
    echo Error: config file not found: %CONF_PATH%
    exit /b 1
)

REM Locate Python (python.exe preferred, fall back to py launcher)
where python >nul 2>nul
if not errorlevel 1 (
    set "PY=python"
    goto :py_found
)
where py >nul 2>nul
if not errorlevel 1 (
    set "PY=py -3"
    goto :py_found
)
echo Error: python not found in PATH.
echo        Please install Python 3 from https://www.python.org/
exit /b 1

:py_found

REM Write embedded Python script to a temp file (cmd.exe does not support heredoc).
REM Use a separate file rather than inline block to avoid quoting issues.
set "TMP_PY=%TEMP%\admin_ctl_%RANDOM%.py"
call :write_py_script "%TMP_PY%"

%PY% "%TMP_PY%" "%CONF_PATH%" "%COMMAND%" "%DEFAULT_PWD%"
set "RC=%ERRORLEVEL%"

del /q "%TMP_PY%" >nul 2>nul

exit /b %RC%

REM ------------------------------------------------------------------
REM Subroutine: write the Python helper script to a target path.
REM Using >>> redirection per line is more robust than (echo ...) blocks
REM since parentheses inside Python code break cmd's block parser.
REM ------------------------------------------------------------------
:write_py_script
set "OUT=%~1"
> "%OUT%" echo import sys
>> "%OUT%" echo import os
>> "%OUT%" echo import sqlite3
>> "%OUT%" echo import hashlib
>> "%OUT%" echo import configparser
>> "%OUT%" echo.
>> "%OUT%" echo conf_path = sys.argv[1]
>> "%OUT%" echo command = sys.argv[2]
>> "%OUT%" echo default_pwd = sys.argv[3]
>> "%OUT%" echo.
>> "%OUT%" echo config = configparser.ConfigParser^(^)
>> "%OUT%" echo config.read^(conf_path, encoding='utf-8'^)
>> "%OUT%" echo.
>> "%OUT%" echo if not config.has_section^('db'^) or not config.has_option^('db', 'path'^):
>> "%OUT%" echo     print^('Error: [db] path not found in config'^)
>> "%OUT%" echo     sys.exit^(1^)
>> "%OUT%" echo.
>> "%OUT%" echo db_path = config.get^('db', 'path'^)
>> "%OUT%" echo.
>> "%OUT%" echo if not os.path.isabs^(db_path^):
>> "%OUT%" echo     conf_dir = os.path.dirname^(os.path.abspath^(conf_path^)^)
>> "%OUT%" echo     base = os.path.dirname^(conf_dir^)
>> "%OUT%" echo     db_path = os.path.join^(base, db_path^)
>> "%OUT%" echo.
>> "%OUT%" echo if not os.path.exists^(db_path^):
>> "%OUT%" echo     print^('Error: database not found: ' + db_path^)
>> "%OUT%" echo     sys.exit^(1^)
>> "%OUT%" echo.
>> "%OUT%" echo db = sqlite3.connect^(db_path^)
>> "%OUT%" echo row = db.execute^('SELECT id, username FROM users WHERE role=0 LIMIT 1'^).fetchone^(^)
>> "%OUT%" echo.
>> "%OUT%" echo if command == 'show':
>> "%OUT%" echo     if not row:
>> "%OUT%" echo         print^('No admin account found.'^)
>> "%OUT%" echo         sys.exit^(1^)
>> "%OUT%" echo     admin_id, admin_name = row
>> "%OUT%" echo     print^('Admin account info:'^)
>> "%OUT%" echo     print^('  Database: ' + db_path^)
>> "%OUT%" echo     print^('  ID:       ' + str^(admin_id^)^)
>> "%OUT%" echo     print^('  Username: ' + admin_name^)
>> "%OUT%" echo     print^('  Password: (hashed, use reset to set a known password'^)
>> "%OUT%" echo.
>> "%OUT%" echo elif command == 'reset':
>> "%OUT%" echo     if not row:
>> "%OUT%" echo         print^('No admin account found.'^)
>> "%OUT%" echo         sys.exit^(1^)
>> "%OUT%" echo     admin_id, admin_name = row
>> "%OUT%" echo     salt = os.urandom^(16^).hex^(^)
>> "%OUT%" echo     hash_val = hashlib.sha256^((default_pwd + salt^).encode^(^)^).hexdigest^(^)
>> "%OUT%" echo     db.execute^('UPDATE users SET password_hash=?, salt=? WHERE id=?', (hash_val, salt, admin_id^)^)
>> "%OUT%" echo     db.commit^(^)
>> "%OUT%" echo     print^('Admin password reset successfully.'^)
>> "%OUT%" echo     print^('  Database: ' + db_path^)
>> "%OUT%" echo     print^('  Username: ' + admin_name^)
>> "%OUT%" echo     print^('  Password: ' + default_pwd^)
>> "%OUT%" echo.
>> "%OUT%" echo elif command == 'clean':
>> "%OUT%" echo     if not row:
>> "%OUT%" echo         print^('No admin account found.'^)
>> "%OUT%" echo         sys.exit^(1^)
>> "%OUT%" echo     admin_id, admin_name = row
>> "%OUT%" echo     db.execute^('DELETE FROM users WHERE id=?', (admin_id,^)^)
>> "%OUT%" echo     db.commit^(^)
>> "%OUT%" echo     print^('Admin account deleted: ' + admin_name + ' (id=' + str^(admin_id^) + ')'^)
>> "%OUT%" echo     print^('  Database: ' + db_path^)
>> "%OUT%" echo.
>> "%OUT%" echo else:
>> "%OUT%" echo     print^('Unknown command: ' + command^)
>> "%OUT%" echo     sys.exit^(1^)
>> "%OUT%" echo.
>> "%OUT%" echo db.close^(^)
goto :eof

:usage
echo Usage: %0 -c ^<conf_path^> {reset^|show^|clean}
echo.
echo Options:
echo   -c ^<conf_path^>  Path to register_student.conf
echo.
echo Commands:
echo   reset   Reset admin password to default (%DEFAULT_PWD%)
echo   show    Show admin username and account info
echo   clean   Delete admin account from database
exit /b 1
