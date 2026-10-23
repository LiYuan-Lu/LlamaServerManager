\# Llama Server Manager — 開發規格書



\*\*版本：\*\* 1.0  

\*\*目標平台：\*\* Windows 10 / Windows 11 x64  

\*\*技術：\*\* Qt 6 + QML + C++17 + CMake  

\*\*開發環境：\*\* Visual Studio 2022 / MSVC x64  

\*\*專案類型：\*\* Windows 原生桌面應用程式



\---



\## 1. 專案目標



開發一套 Windows 原生 GUI 工具，用來管理以 `llama.cpp` 的 `llama-server.exe` 啟動的本地 GGUF 大語言模型。



系統需要提供：



1\. GUI 模型管理與啟動參數設定。

2\. 一鍵啟動、停止、重新啟動模型服務。

3\. 多個模型設定檔（Profiles）管理。

4\. 多個 `llama-server` 實例管理。

5\. CPU、RAM、GPU、VRAM 資源監控。

6\. API Health Check 與執行狀態監控。

7\. Process Crash 自動重新啟動。

8\. HTTP API 無回應或卡死時自動復原。

9\. 即時 Console Log 與故障歷史。

10\. Windows 系統匣與背景運作。

11\. 設定資料持久化。

12\. 未來支援 Windows Service 與開機自動執行。



\### 核心限制



\- 不依賴 Docker、Ollama 或 LM Studio。

\- 不修改 llama.cpp 原始碼。

\- 使用既有的 `llama-server.exe` 與 GGUF 模型。

\- 不需要 Python Runtime。

\- GUI 關閉或崩潰，不應造成 Agent 或模型服務停止。

\- 自動重啟必須區分使用者主動停止與非預期故障。

\- 同一個模型服務只能由一個 Agent 擁有與管理。



\---



\## 2. 技術架構



採用 GUI + Agent + llama-server 三層架構。



\### A. GUI Application



使用 Qt Quick / QML 實作。



負責：



\- 模型列表與設定

\- 啟停控制

\- Dashboard

\- GPU / CPU / RAM 圖表

\- Log Viewer

\- Watchdog 設定

\- 系統匣

\- Agent 連線狀態



GUI 不直接管理 `llama-server.exe` 的生命週期。



\### B. Manager Agent



使用 C++ / Qt Core 開發獨立背景程式。



負責：



\- 透過 QProcess 管理 llama-server

\- 健康檢查

\- 自動復原

\- 資源資訊收集

\- 日誌處理

\- 設定讀寫

\- 多實例管理

\- IPC Server



Agent 必須可以獨立於 GUI 執行。



第一版使用一般 Windows 背景程式，透過工作排程器啟動；Windows Service 為後續階段。



\### C. llama-server



由 Agent 啟動的外部執行檔。



應支援常見參數：



\- `-m`：GGUF 模型路徑

\- `-c`：Context Size

\- `-ngl`：GPU Layers

\- `-t`：CPU Threads

\- `--host`：監聽位址

\- `--port`：API Port

\- `--parallel`：並行 Slot 數量

\- `--metrics`：Metrics API



實際可用參數應以使用者指定的 `llama-server.exe --help` 為準，不可假設不同版本均支援同樣參數。



\---



\## 3. 技術選型



| 功能 | 技術 |

|---|---|

| GUI | Qt Quick / QML |

| UI 元件 | Qt Quick Controls |

| C++ 標準 | C++17 |

| 建置系統 | CMake |

| 行程管理 | QProcess |

| HTTP API | QNetworkAccessManager |

| 定時器 | QTimer |

| GUI / Agent IPC | QLocalSocket / QLocalServer |

| 狀態同步 | QObject / Signal / Slot |

| 資源監控 | Windows API / PDH |

| GPU 資訊 | NVIDIA NVML |

| 設定儲存 | JSON |

| Log | QFile / QTextStream |

| 即時圖表 | Qt Charts 或自製 QML Chart |

| 系統匣 | QSystemTrayIcon |

| 單元測試 | Qt Test |

| 部署 | windeployqt / CPack |



必須避免在 GUI Thread 執行阻塞性工作。



\---



\## 4. GUI 功能規格



\### 4.1 Dashboard



首頁顯示：



\- Agent 連線狀態

\- llama-server 運行狀態

\- 已載入模型名稱

\- Process PID

\- API Port

\- 運行時間

\- CPU 使用率

\- RAM 使用量

\- GPU 使用率

\- VRAM 使用量

\- GPU 溫度

\- API Health Status

\- 自動重啟次數

\- 最後一次故障原因



提供 Start、Stop、Restart 按鈕。



資源監控資料每 1～2 秒更新一次，頻率必須可設定。



若偵測不到 NVIDIA GPU，GPU 資訊應顯示不可用，不可造成應用程式崩潰。



\### 4.2 Model Profiles



使用者可新增、編輯、複製、刪除模型設定檔。



每個 Profile 包含：



\- Profile ID

\- 顯示名稱

\- llama-server.exe 路徑

\- GGUF 模型路徑

\- Working Directory

\- Host

\- Port

\- Context Size

\- GPU Layers

\- Threads

\- Parallel Slots

\- 額外 CLI Arguments

\- Environment Variables

\- 自動啟動設定

\- Watchdog 設定



所有欄位應有合理預設值與輸入驗證。



檔案路徑使用 Windows 檔案選擇器。



使用 QProcess 的 program 與 arguments 分開傳遞，避免自行拼接 Windows 命令列造成路徑空白及引號問題。



不應預設不同 Profile 能同時載入；多實例啟動時需先檢查 Port 衝突與資源可用性。



\### 4.3 Server Management



每個 Server Instance 提供：



\- Start

\- Stop

\- Restart

\- Auto Restart Enable / Disable

\- 查看啟動參數

\- 查看執行狀態

\- 查看 PID

\- 查看 Log

\- 查看 Restart History



支援至少三種操作狀態：



\- 已停止

\- 啟動中

\- 執行中



並延伸支援故障與復原狀態。



\### 4.4 Log Viewer



功能：



\- 即時顯示 stdout / stderr

\- 依 Server Instance 分類

\- 自動捲動

\- 暫停顯示

\- 搜尋 Log

\- 清除畫面

\- 複製文字

\- 匯出 Log

\- 顯示 Error / Warning / Info



Log 必須同時儲存到檔案，並實作檔案輪替與容量上限，避免硬碟被填滿。



\---



\## 5. Process Manager



建立 `LlamaProcessManager` C++ 類別。



主要使用：



\- QProcess::start()

\- QProcess::terminate()

\- QProcess::kill()

\- QProcess::finished()

\- QProcess::errorOccurred()

\- QProcess::readyReadStandardOutput()

\- QProcess::readyReadStandardError()



\### 功能要求



1\. 正確啟動指定 llama-server。

2\. 設定 Working Directory。

3\. 傳入 CLI 參數與環境變數。

4\. 取得 Process PID。

5\. 接收 stdout、stderr。

6\. 偵測 Process 非預期結束。

7\. 偵測啟動失敗。

8\. 實作停止逾時與強制終止。

9\. 防止重複啟動。

10\. 區分正常停止與 Crash。



Windows 上 Console Process 不一定能正常回應 QProcess::terminate()，必須設計可靠的停止流程及逾時處理。



對於子行程與衍生行程的管理，優先評估 Windows Job Objects。



不可以因為 Agent 遭遇異常，就盲目殺掉與本工具無關的 `llama-server.exe`。



\---



\## 6. Health Monitor



建立獨立 `HealthMonitor` 類別。



\### 6.1 Process Health



檢查：



\- Process 是否存在

\- QProcess 是否 Running

\- PID 是否有效

\- 是否非預期退出



Process Crash 必須觸發 Restart Policy。



\### 6.2 HTTP Health



定期請求：



`GET http://127.0.0.1:{port}/health`



建議預設值：



| 項目 | 預設 |

|---|---|

| 檢查間隔 | 30 秒 |

| HTTP Timeout | 5 秒 |

| 連續失敗閾值 | 3 次 |

| 載入寬限期 | 600 秒 |

| 重啟冷卻時間 | 5 秒 |

| 10 分鐘內最多重啟 | 3 次 |



處理方式：



\- HTTP 200：健康。

\- 載入期間 HTTP 503：依目前 llama.cpp 版本及狀態判斷，不直接視為卡死。

\- HTTP Timeout：記錄失敗。

\- Connection Refused：記錄失敗。

\- 其他異常：記錄並依策略判斷。



HTTP 請求必須非同步執行。



必須避免上一個 Health Check 尚未結束，又重複發送檢查請求。



\### 6.3 Inference Health（第二階段）



支援可選的主動推論測試。



例如：



\- 定期送出極短測試 Prompt。

\- 設定最大回應時間。

\- 確認推論確實能完成。

\- 避免在模型忙碌時誤判卡死。

\- 可單獨啟用或停用。



此功能不應作為第一版預設的強制重啟條件。



\---



\## 7. Watchdog / Restart Policy



建立獨立 `RestartPolicy` 類別。



\### 狀態機



定義：



```text

STOPPED

STARTING

RUNNING

UNHEALTHY

RESTARTING

BACKOFF

FAILED

STOPPING

```



\### 行為



\*\*STOPPED\*\*



使用者主動停止後不得自動重啟。



\*\*STARTING\*\*



啟動模型並等待模型載入完成。



\*\*RUNNING\*\*



定期執行 Health Check。



\*\*UNHEALTHY\*\*



持續健康檢查失敗，達到設定閾值。



\*\*RESTARTING\*\*



執行安全停止與重新啟動。



\*\*BACKOFF\*\*



短時間多次故障時增加重啟等待時間。



\*\*FAILED\*\*



超出重試上限後停止自動重啟，等待人工介入或依使用者設定執行復原。



\*\*STOPPING\*\*



正在停止行程，避免重複 Stop / Start 指令。



\### 必須支援



\- Crash Restart

\- Health Check Restart

\- Startup Failure Retry

\- Exponential Backoff

\- Max Restart Attempts

\- Restart History

\- Manual Stop Override

\- Restart Cooldown

\- 防止重複重啟

\- 防止重啟循環

\- 防止舊的非同步 Health Check 結果干擾新行程



每次故障必須記錄時間、Profile ID、PID、故障原因、重試次數和復原結果。



\---



\## 8. GPU / System Monitor



建立 `SystemMonitor` 與 `GpuMonitor`。



\### CPU / RAM



提供：



\- 系統 CPU 使用率

\- 系統 RAM 使用率

\- llama-server Process CPU 使用率

\- llama-server Process RAM 使用量



\### NVIDIA GPU



使用 NVML API。



提供：



\- GPU 名稱

\- GPU Utilization

\- VRAM Used

\- VRAM Total

\- GPU Temperature

\- GPU Power Usage（可選）

\- 多 GPU 支援



GPU 數值應區分整張 GPU 的使用情況與個別 Process 使用情況，不得將全卡 VRAM 使用量錯誤標示為某個模型專用量。



NVML 未安裝或 GPU 不支援時，應優雅降級。



\### 效能圖表



GUI 顯示最近 5 分鐘的：



\- GPU Usage

\- VRAM Usage

\- CPU Usage

\- RAM Usage



保留固定數量的資料點，防止記憶體無限制增加。



\---



\## 9. GUI / Agent IPC



使用：



\- QLocalServer

\- QLocalSocket



採用 JSON Message Protocol。



\### 基本 Command



```text

getStatus

listProfiles

startServer

stopServer

restartServer

getLogs

updateProfile

deleteProfile

getMetrics

getRestartHistory

```



\### 通訊要求



\- 非同步請求及回應

\- Request ID

\- 命令錯誤處理

\- GUI 自動重新連接 Agent

\- Agent 主動推播狀態

\- 支援多 Server Instances

\- 限制 IPC 存取權限

\- 單一 Agent 實例鎖定

\- GUI 中斷連線不得使服務停止



需要定義統一的 JSON 訊息格式與 Protocol Version。



\---



\## 10. 設定持久化



使用 JSON 儲存：



```text

%APPDATA%/LlamaServerManager/

&#x20;   profiles.json

&#x20;   settings.json

```



Log 和 Agent 共用狀態放置於合適且權限受控的資料目錄；如果未來採 Windows Service，應考慮改用 `%PROGRAMDATA%`。



資料需支援：



\- 預設值

\- 版本欄位

\- 設定驗證

\- 設定升級

\- 原子寫入

\- 備份與錯誤復原



不可直接將未驗證的 JSON 設定拼接成 shell 命令。



\---



\## 11. 系統匣



提供：



\- 顯示主視窗

\- 隱藏主視窗

\- 查看服務狀態

\- Start / Stop / Restart

\- 開啟 Log

\- 結束 GUI



GUI 結束時，不應停止 Agent 或 llama-server。



「停止模型」、「結束 GUI」、「停止 Agent」必須是不同操作，避免使用者誤操作。



\---



\## 12. 專案目錄



```text

LlamaServerManager/

├── CMakeLists.txt

├── README.md

├── docs/

│   ├── architecture.md

│   ├── ipc-protocol.md

│   └── testing.md

├── apps/

│   ├── gui/

│   │   ├── main.cpp

│   │   └── qml/

│   │       ├── Main.qml

│   │       ├── Dashboard.qml

│   │       ├── ModelsPage.qml

│   │       ├── LogsPage.qml

│   │       ├── SettingsPage.qml

│   │       └── components/

│   └── agent/

│       └── main.cpp

├── core/

│   ├── LlamaProcessManager.h

│   ├── LlamaProcessManager.cpp

│   ├── HealthMonitor.h

│   ├── HealthMonitor.cpp

│   ├── RestartPolicy.h

│   ├── RestartPolicy.cpp

│   ├── ProfileManager.h

│   ├── ProfileManager.cpp

│   ├── SystemMonitor.h

│   ├── SystemMonitor.cpp

│   ├── GpuMonitor.h

│   └── GpuMonitor.cpp

├── ipc/

│   ├── AgentServer.h

│   ├── AgentServer.cpp

│   ├── AgentClient.h

│   └── AgentClient.cpp

├── tests/

├── resources/

└── packaging/

```



\---



\## 13. 開發階段



\### Phase 1 — 可運作 MVP



優先完成：



\- Qt 6 / CMake 專案

\- QML GUI

\- 獨立 Agent

\- IPC 通訊

\- 單一 llama-server 啟停

\- 自訂 exe / GGUF 路徑

\- 啟動參數設定

\- 即時 stdout / stderr

\- 基本行程狀態監控



交付目標：能使用 GUI 啟動、停止本地 GGUF 模型，並在關閉 GUI 後繼續運作。



\### Phase 2 — Watchdog



完成：



\- Crash Detection

\- HTTP Health Check

\- Auto Restart

\- Restart Backoff

\- Manual Stop Override

\- Restart History

\- 啟動失敗處理

\- Watchdog 單元測試



交付目標：模型非預期 Crash 或 HTTP API 持續無回應時，自動復原。



\### Phase 3 — Dashboard



完成：



\- CPU / RAM Monitor

\- GPU / VRAM Monitor

\- 即時效能圖表

\- 多模型 Profiles

\- 多 Server Instances

\- Log Viewer 改善



交付目標：一個 GUI 管理多個 llama-server 實例。



\### Phase 4 — Windows 整合



完成：



\- 系統匣

\- 工作排程器自動啟動

\- Windows Service 支援

\- Agent 權限管理

\- 安裝及解除安裝

\- 發布版本打包



Windows Service 必須是獨立服務程序，不能把 GUI 直接當 Windows Service 執行。



\---



\## 14. 驗收標準



以下測試必須通過：



1\. 使用 GUI 選擇 llama-server.exe 與 GGUF，成功啟動模型。

2\. GUI 能顯示正確 PID、Port 與運行狀態。

3\. GUI 關閉後，模型與 Agent 繼續運作。

4\. GUI 重新開啟後，能重新連線並顯示現有模型狀態。

5\. 強制結束 llama-server 時，Agent 能按策略自動重新啟動。

6\. HTTP API 連續逾時時，Agent 能按策略執行復原。

7\. 使用者手動 Stop 後不會自動重啟。

8\. 模型正常載入時不會被過早誤判為故障。

9\. 超過重啟次數上限後不會無限重啟。

10\. llama-server 路徑含空白與 Unicode 字元時仍可正常運作。

11\. 啟動失敗、Port 衝突及模型路徑錯誤會正確顯示錯誤。

12\. 無 NVIDIA GPU 或 NVML 不可用時，程式仍正常運作。

13\. 多實例不會誤殺彼此的 Process。

14\. Agent 重複啟動時，不會產生多個管理同一服務的控制程序。

15\. Log 長時間運行不會無限佔用磁碟或記憶體。

16\. GUI 不因 HTTP 請求、GPU 監控或模型啟動而凍結。



\---



\## 15. Codex 實作指示



請根據此規格建立可編譯、可執行的 Qt 6 + QML + C++ 專案。



開發要求：



1\. 先檢查目前 Repository 與開發環境，不要覆蓋既有有效程式碼。

2\. 使用 CMake 組織 GUI、Agent、Core、IPC 與 Tests。

3\. GUI 與 Agent 必須是兩個獨立 Executable。

4\. 優先使用 Qt 官方 API，避免不必要的第三方依賴。

5\. 使用 QObject、Signals / Slots 與非同步事件架構。

6\. 核心邏輯與 GUI 分離，以便進行單元測試。

7\. UI 使用 QML，採用簡潔的深色 Dashboard 風格。

8\. 不要只產生 Mock UI；第一階段必須能實際管理 llama-server。

9\. 不要假設使用者的 llama-server 或 GGUF 位於固定路徑。

10\. 所有外部 Process 與 HTTP 操作均需有錯誤及逾時處理。

11\. 使用狀態機處理啟動、停止、故障與重啟，避免競態條件。

12\. 提供可替換的 Fake Process / Fake HTTP 實作，讓自動重啟邏輯可在沒有 GPU 和 GGUF 的情況下測試。

13\. 使用 Qt Test 編寫核心測試。

14\. 提供 Windows 建置、執行、部署說明。

15\. 不要宣稱沒有實際執行的測試已通過。

16\. 每一階段完成後，檢查編譯錯誤、執行測試，並明確記錄已完成與尚未完成的功能。

17\. 若無法在目前環境編譯 Windows 版本，仍需產出完整原始碼與建置步驟，並明確註明未驗證項目。



\*\*先實作 Phase 1 的完整可運作 MVP，不要一次將所有 Phase 寫成空殼。\*\*



Phase 1 完成時必須交付：



\- 可編譯的 CMake 專案

\- GUI 與 Agent 的完整原始碼

\- 基本 IPC Protocol

\- llama-server Process Manager

\- Profile 設定讀寫

\- QML 模型啟停畫面

\- 即時 Log 顯示

\- 相關單元測試

\- README 與執行步驟



請以可維護性、穩定性、模組化和可擴充性為優先，避免將全部邏輯集中在 main.cpp 或 Main.qml。



