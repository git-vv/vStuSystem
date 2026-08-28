vStuSystem 使用说明
====================

【软件简介】
  vStuSystem 是星芽教务系统，提供学生报名、班级管理、资源分配、
  考勤记录等核心业务功能。本软件基于 Crow Web 框架与 SQLite3 数据库，
  在用户本机以 HTTP 服务方式运行，通过浏览器访问。

【系统要求】
  - Windows 10 x64 或 Windows 11 x64
  - 至少 200 MB 可用磁盘空间
  - 任意现代浏览器（Edge、Chrome、Firefox 等）

【安装方法】
  1. 双击 vStuSystem-<版本>-windows-x64-setup.exe
  2. 如出现 SmartScreen 警告，点击"更多信息" → "仍要运行"
  3. 按安装向导提示完成安装（默认安装到 %APPDATA%\registerStudent）
  4. 安装完成后可勾选"立即启动"

【启动方法】
  - 双击桌面快捷方式 "vStuSystem"
  - 或双击开始菜单 "vStuSystem" → "vStuSystem"
  - 程序启动后无可见窗口，仅在任务栏右下角显示托盘图标

【访问方法】
  - 程序启动后会自动打开默认浏览器
  - 或右键托盘图标选择"打开页面"
  - 浏览器访问地址：http://localhost:18080
  - 端口被占用时会自动切换到 18081-18083

【托盘菜单说明】
  - Open Page：在浏览器中打开主界面
  - Auto Start：开机自启动切换（带勾选状态）
  - About：显示关于对话框
  - Exit：优雅退出程序

【数据存储位置】
  - 配置文件：%APPDATA%\registerStudent\conf\register_student.conf
  - 业务数据库：%APPDATA%\registerStudent\data\register_student.db
  - 操作日志库：%APPDATA%\registerStudent\data\operation_log.db
  - 上传文件：%APPDATA%\registerStudent\data\uploads\
  - 日志文件：%APPDATA%\registerStudent\logs\register_student.log

【卸载方法】
  - 控制面板 → 程序和功能 → 找到 vStuSystem → 卸载
  - 或运行 %APPDATA%\registerStudent\uninstall.exe
  - 卸载时可选择保留或删除用户数据

【防火墙提示】
  首次启动时 Windows 防火墙会弹窗询问是否允许 vStuSystem 通信：
  - 选择"专用网络"：仅本机与局域网可访问
  - 选择"公用网络"：所有网络可访问（不推荐）
  - 若仅在本地使用，可全部拒绝，仍可通过 localhost 访问

【技术支持】
  如遇问题，请查看日志文件：logs\register_student.log
