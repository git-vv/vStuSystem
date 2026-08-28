# registerStudent

适合小型机构的报名管理系统，可自定义报名活动页面、记录学生考勤、账户管理、教师工作流痕等功能。

C++ 星芽教务系统，基于 Crow Web 框架提供 HTTP 服务，支持学生注册、报名、班级管理、资源管理等功能。

## 技术栈

- C++11
- CMake 3.7+
- Crow（header-only Web 框架）
- SQLite3
- Mustache（Crow 内置模板引擎）
- plog（日志库）
- 前端：HTML + CSS + JavaScript
- 其他：Openssl、minizip、boost

## 项目结构

```
registerStudent
├── 3rd/                    # 第三方库（boost、plog、sqlite3）
├── bin/                    # 构建输出
├── build/                  # CMake 构建目录
├── docs/features/          # 功能文档（需求、设计、计划、验证、评审）
├── include/                # 公共头文件（crow_all.h）
├── src/                    # 源代码
│   ├── main.cpp            # 程序入口
│   ├── page_handler.*      # 页面路由处理
│   ├── registration_handler.*  # 报名业务处理
│   ├── class_create_handler.*  # 创建班级处理
│   ├── class_manage_handler.*  # 班级管理处理
│   ├── resource_handler.*  # 资源管理处理
│   ├── i_registration_dao.h    # 报名 Dao 接口
│   ├── i_class_dao.h       # 班级 Dao 接口
│   ├── i_resource_dao.h    # 资源 Dao 接口
│   ├── error_codes.h       # 通用错误码定义
│   └── utils.*             # 工具函数（日志等）
├── tests/                  # 单元测试
├── webui/                  # 前端资源
│   ├── static/             # CSS / JS / 图片
│   └── templates/          # HTML 模板
└── CMakeLists.txt
```

## 架构设计

三层架构，严格遵循 7 大设计原则（SRP、OCP、DIP、LSP、ISP、LOD、CARP）：

```
Handler 层（路由处理，依赖 Database 层接口）
    ↓
Database 层（数据访问，通过 Dao 接口抽象）
    ↓
Utility 层（工具函数，被所有模块依赖）
```

- Handler 层通过 Dao 接口访问数据，不依赖具体实现
- 按业务域拆分 Handler 和 Dao 接口（接口隔离原则）

## 构建与运行

项目支持 Linux 与 Windows 双平台编译，所有第三方依赖（Crow、SQLite3、plog、inih、boost 头文件）已包含在 `3rd/` 与 `include/` 目录下，编译时无需从网络下载任何额外依赖包。

### Linux 编译

#### 环境要求

| 工具      | 版本要求        |
| ------- | ----------- |
| GCC     | 支持 C++11 标准 |
| CMake   | 3.7 及以上     |
| SQLite3 | 系统库或预编译版本   |

#### 编译步骤

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

编译产物输出到 `bin/` 目录：
- `bin/vStuSystem` — 主可执行文件
- `bin/run_tests` — 单元测试程序

若 CMake 缓存损坏，删除 `build/CMakeCache.txt` 后重新配置。

### Windows 编译

#### 环境要求

| 工具            | 版本要求                                  |
| ------------- | ------------------------------------- |
| Visual Studio | 2019 (16.x) 或 2022 (17.x)，含 MSVC v143 |
| CMake         | 3.7 及以上                               |
| Windows SDK   | 10.0.26100.0 或以上                      |
| NSIS          | 3.8 及以上（仅打包时需要，编译不依赖）                 |

支持 Windows 10 x64 / Windows 11 x64，不支持 Windows 7/8/8.1 与 x86 架构。

#### 编译步骤（命令行）

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

编译产物输出到 `bin\Release\` 目录：
- `bin\Release\vStuSystem.exe` — 主可执行文件（窗口化子系统，无控制台窗口）
- `bin\Release\run_tests.exe` — 单元测试程序
- `bin\Release\run_win_pack_tests.exe` — Windows 平台特有测试程序

#### 编译步骤（Visual Studio IDE）

1. 用 VS 2019/2022 打开 `build/vStuSystem.sln`
2. 选择 `Release` 配置 + `x64` 平台
3. 生成解决方案（F7）

#### 关键编译选项

| 选项              | 值                                        | 说明                           |
| --------------- | ---------------------------------------- | ---------------------------- |
| C++ 标准          | C++11                                    | `set(CMAKE_CXX_STANDARD 11)` |
| 静态运行库           | `/MT` (Release) / `/MTd` (Debug)         | 目标机器无需预装 VCRedist            |
| 子系统             | `/SUBSYSTEM:WINDOWS`                     | 双击启动无控制台窗口（仅 vStuSystem）     |
| 源文件编码           | `/utf-8`                                 | 处理含中文字符串的 UTF-8 源文件          |
| Manifest        | `/MANIFEST:NO`                           | 由 vStuSystem.rc 嵌入，禁用链接器自动生成 |
| Boost auto-link | `BOOST_ALL_NO_LIB`                       | 项目仅含 boost 头文件，禁用 .lib 自动链接  |
| SQLite 编译宏      | `SQLITE_OS_WIN`、`SQLITE_THREADSAFE=1`、`SQLITE_OMIT_LOAD_EXTENSION` | amalgamation 源码编译            |

#### 编译常见问题

1. **`cl : error D8021: 无效的数值参数"/Wextra"`** — `-Wall -Wextra -fPIC` 是 GCC 选项，已通过 `if(NOT WIN32)` 隔离，仅 Linux 分支生效。
2. **`crow_all.h(5106): error C2059`** — Crow 通过 boost 间接包含 `<windows.h>`，与 `crow::HTTPMethod` 枚举冲突。已通过 `src/crow_safe.h` 包装头解决，所有源文件应 `#include "crow_safe.h"` 而非直接 `#include "crow_all.h"`。
3. **`CVT1100 资源重复 MANIFEST`** — `vStuSystem.rc` 已嵌入 manifest，链接器标志加 `/MANIFEST:NO` 禁用自动生成。
4. **`LNK1104 libboost_date_time-vc142-mt-s-x64-1_74.lib`** — 定义 `BOOST_ALL_NO_LIB` 禁用 Boost auto-link。

### 运行

#### Linux 运行

```bash
./bin/vStuSystem
```

服务启动后监听 `0.0.0.0:18080`，浏览器访问 http://localhost:18080 。

#### Windows 运行

**方式一：双击启动**

直接双击 `bin\Release\vStuSystem.exe`，程序以窗口化子系统启动，无控制台窗口弹出。启动后：
- 在任务栏右下角显示托盘图标
- 浏览器访问 http://localhost:18080
- 托盘右键菜单提供"打开页面"、"开机自启"、"关于"、"退出"功能

**方式二：命令行启动**

```bat
bin\Release\vStuSystem.exe
```

**方式三：指定配置文件**

```bat
bin\Release\vStuSystem.exe -c <配置文件绝对路径>
bin\Release\vStuSystem.exe --conf=<配置文件绝对路径>
bin\Release\vStuSystem.exe -h
```

命令行参数：
- `-c, --conf <path>` — 指定配置文件路径
- `-h, --help` — 显示帮助信息

**配置文件自动生成**：首次启动时若配置文件不存在，程序会自动在 `%APPDATA%\registerStudent\conf\register_student.conf` 路径下生成默认配置。

**端口冲突处理**：默认监听 18080，若被占用自动尝试 18081/18082/18083，全部被占用时托盘气泡提示，可修改配置文件 `[server] port` 后重启。

**开机自启**：通过托盘菜单"Auto Start"切换，写入注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\vStuSystem`。

**服务退出**：通过托盘菜单"Exit"触发优雅退出（`raise(SIGINT)` → Crow 内部 `Server::stop()` → 工作线程 join → 关闭数据库）。Linux 下使用 `kill -15 <PID>` 或 Ctrl+C。

### 运行测试

#### Linux

```bash
./bin/run_tests
```

#### Windows

```bat
bin\Release\run_tests.exe
bin\Release\run_win_pack_tests.exe
```

- `run_tests.exe` — 56 个既有测试用例
- `run_win_pack_tests.exe` — Windows 平台特有的 12 个测试用例（覆盖配置文件自动生成、Crow 优雅退出、WinTray 纯逻辑）

### 打包（仅 Windows）

#### 一键构建打包

```bat
packaging\windows\build.bat
```

该脚本自动执行：
1. CMake 配置（生成 `build/vStuSystem.sln`）
2. CMake 编译（Release 配置，输出到 `bin\Release\`）
3. NSIS 打包（生成安装包）

产出安装包：`packaging\output\vStuSystem-0.0.2-windows-x64-setup.exe`

#### 手动打包（仅 NSIS 步骤）

若已手动完成编译，可单独执行 NSIS 打包：

```bat
cd packaging\windows
makensis register_student.nsi
```

#### 安装包文件清单

| 文件                           | 来源                   | 用途      |
| ---------------------------- | -------------------- | ------- |
| `bin\Release\vStuSystem.exe` | CMake 编译产物           | 主可执行文件  |
| `webui\*`                    | `webui\` 全部静态资源      | 前端模板与样式 |
| `license.txt`                | `packaging\windows\` | 许可协议    |
| `readme.txt`                 | `packaging\windows\` | 使用说明    |
| `uninstall.exe`              | NSIS 自动生成            | 卸载程序    |

#### 静默安装参数

| 参数                | 默认值  | 说明         |
| ----------------- | ---- | ---------- |
| `/S`              | (无)  | 静默安装，无 UI  |
| `/DESKTOP=1\|0`   | 1    | 是否创建桌面快捷方式 |
| `/AUTOSTART=1\|0` | 0    | 是否开机自启动    |

示例：

```bat
vStuSystem-0.0.2-windows-x64-setup.exe /S /DESKTOP=1 /AUTOSTART=0
```

#### 安装路径

默认安装到 `%APPDATA%\registerStudent\`（按用户安装，无需管理员权限），所有数据文件（数据库、日志、上传文件）均存放在此目录下。

#### SmartScreen 警告

安装包未签名，首次运行时 Windows SmartScreen 会拦截。用户点击"更多信息" → "仍要运行"即可继续安装。长期方案需申请代码签名证书并签名安装包。

## API 路由

| 方法   | 路径                     | 说明                  |
| ---- | ---------------------- | ------------------- |
| GET  | `/`                    | 主页                  |
| GET  | `/registration`        | 报名缴费页面              |
| GET  | `/class/create`        | 创建班级页面              |
| GET  | `/class/manage`        | 班级管理页面              |
| GET  | `/resource`            | 资源管理页面              |
| POST | `/api/registration`    | 报名接口                |
| POST | `/api/class/create`    | 创建班级接口              |
| POST | `/api/class/manage`    | 班级管理接口              |
| POST | `/api/resource`        | 资源管理接口              |
| GET  | `/static/<dir>/<file>` | 静态资源（css/js/images） |

## 错误码

| 错误码  | 含义        |
| ---- | --------- |
| 0    | 成功（DB_OK） |
| 1001 | 参数无效      |
| 2001 | Dao 对象为空  |
| 3001 | 数据库未打开    |
| 3002 | SQL 预处理失败 |
| 3003 | SQL 执行失败  |
