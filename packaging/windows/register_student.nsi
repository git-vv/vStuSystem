Unicode true
!pragma warning disable 9999
; ==================================================================
; vStuSystem Windows 安装脚本 (NSIS 3.x)
; 仅简体中文 UI，默认安装到 %LOCALAPPDATA%\Programs\vStuSystem
; 用户可在安装向导中自定义安装目录（含中文路径）
; ==================================================================

!ifndef VERSION
  !define VERSION "0.0.2"
!endif

Name "vStuSystem"
OutFile "..\output\vStuSystem-${VERSION}-windows-x64-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\vStuSystem"
InstallDirRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\vStuSystem" "InstallLocation"
RequestExecutionLevel user

; ---------- MUI 现代界面 ----------
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!define MUI_ICON "vStuSystem.ico"
!define MUI_UNICON "vStuSystem.ico"
!define MUI_ABORTWARNING

; 安装页面
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; 卸载页面
!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; 简体中文 UI（必须在所有 PAGE 之后）
!insertmacro MUI_LANGUAGE "SimpChinese"

; ---------- 组件定义 ----------
LangString SECTION_CORE ${LANG_SIMPCHINESE} "核心组件 (必选)"
LangString SECTION_DESKTOP ${LANG_SIMPCHINESE} "桌面快捷方式"
LangString SECTION_STARTMENU ${LANG_SIMPCHINESE} "开始菜单组"
LangString SECTION_AUTOSTART ${LANG_SIMPCHINESE} "开机自启动"

; 静默安装参数处理
Var OptDesktop
Var OptAutoStart

; ---------- 安装前停止运行中的进程 ----------
Function .onInit
  ; 检查 vStuSystem.exe 是否正在运行
  nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq vStuSystem.exe"'
  Pop $R0
  StrCpy $R1 $R0 2
  ${If} $R1 == "0"
    ; tasklist 输出包含进程名，提示用户
    MessageBox MB_OKCANCEL|MB_ICONSTOP \
      "vStuSystem 正在运行，安装程序需要先关闭它。$\n$\n点击 OK 自动关闭程序，Cancel 取消安装。" \
      IDOK KillProcess
    Abort
    KillProcess:
    nsExec::Exec 'taskkill /IM vStuSystem.exe /F'
    ; 等待进程退出（最多 3 秒）
    Sleep 3000
  ${EndIf}

  ; 默认值：静默安装创建桌面快捷方式，不开机自启动
  StrCpy $OptDesktop "1"
  StrCpy $OptAutoStart "0"

  ; 解析 /DESKTOP= 与 /AUTOSTART= 自定义参数
  ${GetParameters} $R0
  ${GetOptions} $R0 "/DESKTOP=" $R1
  ${If} $R1 != ""
    StrCpy $OptDesktop $R1
  ${EndIf}
  ${GetOptions} $R0 "/AUTOSTART=" $R1
  ${If} $R1 != ""
    StrCpy $OptAutoStart $R1
  ${EndIf}
FunctionEnd

; ---------- 安装 Section ----------
Section "Core" SecCore
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "..\..\bin\Release\vStuSystem.exe"
  ; 打包 OpenSSL 运行时 DLL（与 exe 同目录，不影响系统已有的 OpenSSL）
  File /nonfatal "..\..\bin\Release\libssl*.dll"
  File /nonfatal "..\..\bin\Release\libcrypto*.dll"
  File /r "..\..\webui\*.*"
  File "license.txt"
  File "readme.txt"

  ; 打包 sbin/ 管理脚本到安装目录下的 sbin/
  SetOutPath "$INSTDIR\sbin"
  File "..\..\sbin\admin_ctl.bat"
  File "..\..\sbin\admin_ctl.sh"

  ; 打包 conf/ 配置文件到安装目录下的 conf/
  SetOutPath "$INSTDIR\conf"
  File "..\..\conf\register_student.conf"

  SetOutPath "$INSTDIR"

  ; 生成卸载程序
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; 写入注册表卸载项
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\vStuSystem" \
    "DisplayName" "vStuSystem"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\vStuSystem" \
    "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\vStuSystem" \
    "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\vStuSystem" \
    "Publisher" "Xingya Education"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\vStuSystem" \
    "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\vStuSystem" \
    "DisplayIcon" "$INSTDIR\vStuSystem.exe"
SectionEnd

Section "Desktop Shortcut" SecDesktop
  ; 静默安装时按 $OptDesktop 决定；交互模式下由用户勾选
  ${If} ${Silent}
    ${If} $OptDesktop == "0"
      Goto SkipDesktop
    ${EndIf}
  ${EndIf}
  ; 先删除旧快捷方式（强制图标刷新，避免 Windows 图标缓存残留旧图标）
  Delete "$DESKTOP\vStuSystem.lnk"
  CreateShortcut "$DESKTOP\vStuSystem.lnk" "$INSTDIR\vStuSystem.exe" "" "$INSTDIR\vStuSystem.exe" 0
  SkipDesktop:
SectionEnd

Section "Start Menu" SecStartMenu
  ; 先删除旧快捷方式（强制图标刷新）
  Delete "$SMPROGRAMS\vStuSystem\vStuSystem.lnk"
  CreateDirectory "$SMPROGRAMS\vStuSystem"
  CreateShortcut "$SMPROGRAMS\vStuSystem\vStuSystem.lnk" "$INSTDIR\vStuSystem.exe" "" "$INSTDIR\vStuSystem.exe" 0
  CreateShortcut "$SMPROGRAMS\vStuSystem\Uninstall.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section "Auto Start" SecAutoStart
  ; 静默安装时按 $OptAutoStart 决定；交互模式下由用户勾选
  ${If} ${Silent}
    ${If} $OptAutoStart == "0"
      Goto SkipAutoStart
    ${EndIf}
  ${EndIf}
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" \
    "vStuSystem" "$INSTDIR\vStuSystem.exe"
  SkipAutoStart:
SectionEnd

; 安装完成询问是否立即启动
Function .onInstSuccess
  ${IfNot} ${Silent}
    MessageBox MB_YESNO|MB_ICONQUESTION "vStuSystem 安装完成，是否立即启动？" IDNO SkipLaunch
    Exec "$INSTDIR\vStuSystem.exe"
    SkipLaunch:
  ${EndIf}
FunctionEnd

; ---------- 卸载 Section ----------
Section "Uninstall"
  ; 检查程序是否运行中
  nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq vStuSystem.exe"'
  Pop $R0
  StrCpy $R1 $R0 2
  ${If} $R1 == "0"
    ; tasklist 输出包含进程名，则提示用户
    MessageBox MB_OKCANCEL|MB_ICONSTOP \
      "vStuSystem 正在运行，请先通过托盘菜单退出。$\n$\n点击 OK 强制结束进程，Cancel 取消卸载。" \
      IDOK ForceKill
    Abort
    ForceKill:
    nsExec::Exec 'taskkill /IM vStuSystem.exe /F'
  ${EndIf}

  ; 询问是否保留用户数据
  MessageBox MB_YESNO|MB_ICONQUESTION \
    "是否保留用户数据（数据库、上传文件、配置）？$\n$\n选择 是 保留 data/conf 目录，选择 否 全部删除。" \
    IDYES KeepData

  ; 删除程序文件 + 用户数据
  Delete "$INSTDIR\libssl*.dll"
  Delete "$INSTDIR\libcrypto*.dll"
  Delete "$INSTDIR\vStuSystem.exe"
  RMDir /r "$INSTDIR\templates"
  RMDir /r "$INSTDIR\static"
  RMDir /r "$INSTDIR\sbin"
  Delete "$INSTDIR\license.txt"
  Delete "$INSTDIR\readme.txt"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\data"
  RMDir /r "$INSTDIR\logs"
  RMDir /r "$INSTDIR\conf"
  Goto DeleteShortcuts

  KeepData:
  ; 保留 data/（数据库、上传文件）和 conf/（配置，含路径设置，
  ; 重装后用户可凭 conf 找回原数据位置），其他目录全部删除
  Delete "$INSTDIR\libssl*.dll"
  Delete "$INSTDIR\libcrypto*.dll"
  Delete "$INSTDIR\vStuSystem.exe"
  RMDir /r "$INSTDIR\templates"
  RMDir /r "$INSTDIR\static"
  RMDir /r "$INSTDIR\sbin"
  Delete "$INSTDIR\license.txt"
  Delete "$INSTDIR\readme.txt"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\logs"

  DeleteShortcuts:
  Delete "$DESKTOP\vStuSystem.lnk"
  RMDir /r "$SMPROGRAMS\vStuSystem"

  ; 清理注册表
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\vStuSystem"
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "vStuSystem"

  ; 尝试删除安装根目录（若非空则保留）
  RMDir "$INSTDIR"
SectionEnd
