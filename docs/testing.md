# Phase 1 測試紀錄

測試環境：Windows 11 x64、Visual Studio 2026 Community、MSVC 19.51.36260、Windows SDK 10.0.26100、Qt 6.12.0 msvc2022_64、CMake 4.5.0-rc1。本機安裝路徑及私人模型檔名不公開。

## 已執行

- Debug / Release 全部 CMake targets 成功編譯。
- Release CTest：7/7 通過，43.66 秒。
- Debug CTest：7/7 通過，46.73 秒；紀錄在 `test-results/debug-ctest.log`。
- 最終程式的真實模型回歸：通過，17.595 秒（Qt Test 的 init / integration / cleanup 三項全部成功）。
- 真實 llama-server / GGUF 的跨程序驗收成功：使用自選 llama-server 與約 2.7 GB 的 9B GGUF 模型，CPU 模式、threads=2、context=512、parallel=1、測試選擇未使用 port。測試內確認 /health HTTP 200、GUI 重開的 PID 一致、日誌可取得、手動停止成功。HTTP 檢查只在測試程式內，產品沒有實作 Phase 2 HealthMonitor。
- 使用 Windows Qt 平台和 software scene graph 執行真實 QML GUI，取得實際畫面並檢查繁體中文字型、路徑、狀態、控制項與日誌。
- windeployqt 產生 `deploy/`。清除 PATH 中 Qt SDK 路徑後，已部署 GUI 與 Agent smoke test exit=0。MSVC redistributable installer 一併放入部署目錄；本機未執行安裝程式。

完整結果存於 `test-results/`；每組最新執行的原始 Qt Test 記錄也位於 `build/*-results.txt`。Release 紀錄為 `test-results/release-ctest.log`；真實模型結果為 `test-results/real-model-results.txt`；實際 GUI 畫面為 `test-results/real-model-gui.png`。

## 自動測試範圍

| 測試 | 驗證 |
| --- | --- |
| protocolFraming | partial/coalesced frames、版本錯誤、1 MiB 上限 |
| persistenceAndValidation | 儲存與重讀、備份復原、非法數字及受管理參數覆寫 |
| boundedLogs | 2000 筆 log 後記憶體有限、磁碟檔數與容量限制 |
| processLifecycle | 真實 QProcess 子程序、Unicode/空白 exe 與 GGUF 路徑、cwd、stdout/stderr、重複 start、手動 restart/stop |
| failuresAndCancellation | Port 已占用、不存在/無效 exe、非預期 exit 42、停止 --help 預檢、10 秒預檢 timeout |
| agentAndGuiReconnect | 外部 Agent、單例鎖、IPC 命令/錯誤、兩次 GUI 取得相同 PID 並正常退出、斷線回放、超過啟動 timeout 後仍 RUNNING、停止、不合法客戶端隔離 |
| guiDetachedAgent | GUI 自動啟動 detached Agent，GUI 正常關閉後仍可透過 IPC 取得 STOPPED 狀態 |

FakeServer 是真的小型 console executable，不是只回傳假狀態的 UI mock。沒有模型/GPU 也可執行上述測試。每組由 CTest 啟動獨立測試程序，60 秒 timeout；測試用資料放在工作目錄下 `lsm-test-XXXXXX`，不使用正式 APPDATA 設定。

## 重現

```powershell
$qtRoot = Read-Host "Qt MSVC x64 套件目錄"
$env:PATH = (Join-Path $qtRoot "bin") + ";" + $env:PATH
$env:QT_QPA_PLATFORM = "windows"
$env:QT_QUICK_BACKEND = "software"
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Debug --output-on-failure
```

Windows named pipe 存取限制在 AppContainer 沙箱內可能回傳 Access denied；整合測試需一般使用者的非沙箱 Windows 工作階段，不需管理員權限。GUI 測試會短暫顯示視窗並自動關閉。無桌面的 CI 可改用 `QT_QPA_PLATFORM=offscreen`，但該後端的字型渲染不適合視覺驗收。

選用真實模型驗收（以使用者自選路徑取代）：

```powershell
$env:LSM_TEST_REAL_EXE = Read-Host "llama-server.exe 完整路徑"
$env:LSM_TEST_REAL_MODEL = Read-Host "GGUF 模型完整路徑"
& .\build\Release\lsm-tests.exe agentAndGuiReconnect -o real-model-results.txt,txt
Remove-Item Env:\LSM_TEST_REAL_EXE,Env:\LSM_TEST_REAL_MODEL
```

此測試載入真實模型並使用 CPU，等待最多 90 秒達 HTTP 200；成功流程最後停止模型，測試 Agent 會結束。未提供環境變數時使用 fake server。

## 發現並修正

- QML validator 後多餘分號與 C++ 不完整 QJsonObject 型別。
- 沙箱暫存目錄寫入限制：測試改用工作目錄中的隔離資料夾。
- 沙箱拒絕 UserAccessOption named pipe：保持正式權限設定，在非沙箱環境驗證。
- GUI socket 析構時的 disconnected callback 使用已析構成員，造成 Windows heap corruption；改為先 disconnect，再關閉 socket。AgentServer 同步加入防護。
- Windows QProcess 在 start() 內同步發出 started：必須先啟動 timeout timer 再 start，否則已啟動程序會在 10 秒後被誤殺。已加入超過 10 秒的回歸驗證。
- Agent destructor 不再處理 pending restart，避免關閉期間又啟動程序。

## 未宣稱完成的驗證

- 沒有用滑鼠自動操作原生 Windows 檔案選擇對話框；已實作 Qt FileDialog/FolderDialog 與 URL→local path 轉換，需人工按「瀏覽」完成最後的互動驗收。
- 沒有執行多小時壓力測試；容量限制以自動化小上限輪替測試驗證。
- 沒有 Windows 10 或乾淨 VM 驗收；本機部署 smoke test 不能等同所有電腦可攜性驗證。Qt 6.12 的實際 OS/runtime 支援也需在目標 Windows 10 機器確認。
- 沒有驗證 GPU offload、任意 fork 的所有額外參數、Job assignment 失敗時的 wrapper 程序樹、Agent crash 後接管；詳見 architecture.md。
- 未做 Phase 2–4 的自動復原、HTTP watchdog、效能監控、多實例或系統整合測試。

人工驗收：開啟 deploy/llama-manager.exe → 瀏覽 exe/GGUF → 儲存 → 啟動 → 看到載入完成 → 關閉視窗 → 重開確認 PID 與日誌 → 停止，確認該 PID 結束。

# 多模型與 Phase 2 Watchdog 測試紀錄

測試環境：Ubuntu 24.04、GCC、Qt 6.4.2（`QT_QPA_PLATFORM=offscreen`）。僅為驗證用，CMake 最低版本暫時降為 6.4；專案本身仍要求 Qt 6.5。**以下結果皆未在 Windows 上執行。**

## 已執行

- 全部 targets 編譯成功，無警告。
- 13 項中 12 項通過，連續 3 輪皆通過（`guiDetachedAgent` 只在 Windows 執行，已略過）。
- 以 fake server 建立 4 個模型（健康、載入中、重啟等待中、超過上限而放棄），截取英文與繁中 GUI 畫面檢查。

## 新增 / 更新的自動測試

| 測試 | 驗證 |
| --- | --- |
| restartPolicy | 冷卻與指數退避（含上限）、時間窗內重啟上限、時間窗過後恢復額度、reset、maxRestarts=0 |
| healthMonitor | 200 → Healthy、503 → Loading、萬用位址改走 loopback、卡住的伺服器逾時且請求不重疊、舊 generation 的延遲回應不會送出、連線被拒 |
| watchdogStartupGrace | 寬限期內載入中的 503 不算故障；寬限期後仍未健康 → FAILED，並停止卡住的程序 |
| watchdogCrashRecovery | 第一次當掉 → 自動重啟 → RUNNING，紀錄為 recovered 且 PID 改變；新的 supervisor 會讀回紀錄；關閉自動重啟時當掉 → FAILED，不重啟 |
| watchdogHealthRestart | /health 停止回應 → 連續失敗 → RESTARTING → 新 PID 復原；關閉自動重啟時維持 UNHEALTHY 且程序繼續執行；可從 UNHEALTHY 手動重啟 |
| watchdogLimitsAndManualStop | 一直當掉：重啟 3 次（出現 BACKOFF）後 FAILED，不會無限重啟；等待重啟期間手動停止會取消重啟；放棄後手動啟動會重新計算額度 |
| persistenceAndValidation | 另外驗證 watchdog 欄位：預設值、範圍、型別，以及舊設定檔沒有 watchdog 欄位時套用預設值 |
| agentAndGuiReconnect | 另外驗證 getRestartHistory 與 RUNNING 時 health=Healthy |

CTest 每項測試的 timeout 由 60 秒提高為 180 秒，讓選用的真實模型測試有時間載入（RUNNING 現在代表 /health 已回傳 200）。

Watchdog 測試透過 `ServerSupervisor::setTimeScale(100)`，把設定中的 1「秒」縮短為 100 ms。FakeServer 現在提供真的 HTTP `/health`，可用環境變數控制：`FAKE_HEALTH_LOADING_MS`（先回 503）、`FAKE_HEALTH_HANG_AFTER_MS`（之後不回應）、`FAKE_ONCE_FILE`（只有第一次執行會出問題）。

## 尚未驗證

- Windows 建置與 CTest（請執行上方「重現」的指令）。
- Windows 上的 Stop 會先 terminate，3 秒後才 kill；因 health 失敗而重啟時，Windows 會比 Linux 多等約 3 秒。測試的逾時已預留空間，但尚未實際在 Windows 上跑過。
- 真實 llama-server：載入期間的 503 → 200 轉換、長時間執行時 health 檢查的穩定性。

# Phase 3 Dashboard 測試紀錄

測試環境：Ubuntu 24.04、GCC 13.2（`-Wall -Wextra`）、Qt 6.4.2（`QT_QPA_PLATFORM=offscreen`）。與上一輪相同，僅為本機驗證暫時把 CMake 的 Qt 最低版本降為 6.4；交付的 CMakeLists.txt 仍要求 Qt 6.5。**以下結果皆未在 Windows 上執行，也沒有使用真實的 NVIDIA GPU。**

## 已執行

- 全部 targets 編譯成功，`-Wall -Wextra` 無警告。
- 18 項中 17 項通過，連續 2 輪皆通過（`guiDetachedAgent` 只在 Windows 執行，已略過）。
- 以 fake server 與假 NVML 建立 3 個模型（2 個執行中），截取英文（2 張假 GPU）與繁中（無 GPU）的主畫面與模型頁檢查：資源面板、GPU 選擇、無 GPU 時的說明、模型卡上的 CPU/RAM/VRAM、Console 的等級計數與上色。
- 確認 Console 使用的隱藏 TextEdit 複製方式可寫入剪貼簿（qmltestrunner，offscreen）。

## 新增 / 更新的自動測試

| 測試 | 驗證 |
| --- | --- |
| systemMonitor | 第一次取樣 CPU 為 -1、系統 RAM 合理、本程序 RAM > 1 MiB、不存在的 PID 回報未執行、忙碌迴圈後 CPU > 0 且不超過單一邏輯處理器的比例、不再追蹤的 PID 會清除基準 |
| gpuMonitor | 找不到 NVML 時不可用且有原因；假 NVML：2 張 GPU 的名稱、使用率、VRAM、溫度、功耗（不支援時為 -1）；同一張卡 compute 與 graphics 清單不重複計算、跨卡相加；驅動未提供時為 -1；無 GPU、初始化失敗時的原因 |
| metricsCollector | 背景執行緒取樣、間隔限制在 1–10 秒、每個模型的 CPU/RAM/GPU 記憶體、已結束的程序、歷史資料格式；高速取樣下歷史仍不超過 600 點 |
| agentMetricsAndSettings | 設定驗證（範圍、型別、未知欄位）、updateSettings 寫入 settings.json、`metrics` 事件含執行中模型的 PID 與用量、getMetrics 的歷史、Agent 重啟後設定保留 |
| logModel | 等級判斷（含 `E `/`W ` 前綴與整字比對）、一筆紀錄拆成多行、搜尋（不分大小寫）、等級篩選、暫停時暫存新行、5,000 行上限（同步移除列與計數）、UTF-8 匯出與錯誤路徑；並用 QAbstractItemModelTester 檢查 model 的一致性 |
| agentAndGuiReconnect | 原有的 GUI 冒煙測試現在也會載入資源面板與新的 Console，stderr 不得出現 QML 錯誤 |

假 NVML（`tests/FakeNvml.cpp`）是真的共享函式庫，匯出與 nvml.dll 相同的函式名稱；`FAKE_NVML_MODE` 可切換 `ok`、`no-gpu`、`init-fail`。Agent 透過環境變數 `LSM_NVML_LIBRARY` 改用它（僅供測試）。GUI 的 `--smoke-test` 另外接受 `LSM_SCREENSHOT_DELAY_MS` 與 `LSM_SMOKE_OPEN=<profileId>`，用來截取模型頁。FakeServer 新增 `FAKE_LOG_LEVELS`，會輸出幾行類似 llama.cpp 的 warning / error。

## 尚未驗證

- Windows 建置（MSVC `/W4`）與 CTest。Windows 專用的程式碼路徑（`GetSystemTimes`、`GetProcessTimes`、`GetProcessMemoryInfo`、`GlobalMemoryStatusEx`、從系統路徑載入 nvml.dll）只寫好、尚未執行。
- 真實 NVIDIA GPU 與驅動：實際的 NVML 數值、WDDM 下每個程序的 VRAM 是否如預期顯示 n/a、多張 GPU。
- 原生 Windows 檔案對話框的「匯出…」互動，以及長時間執行下圖表的流暢度。

# 複製 Profile、自動啟動、停止 Agent 測試紀錄

測試環境同上一節（Ubuntu 24.04、Qt 6.4.2、offscreen）。**尚未在 Windows 上執行。**

- 全部 targets 編譯成功，`-Wall -Wextra` 無警告；18 項中 18 項通過（`guiDetachedAgent` 在非 Windows 平台自動略過），連續 2 輪。
- 新增 `autoStartAndShutdown`：`autoStart` 欄位的預設值、型別檢查與保存；建立 Profile 時不會自動啟動；`shutdownAgent` 會停止執行中的模型（程序確實結束、日誌留下原因）、Agent 以 exit code 0 結束並釋放 lock；重新啟動 Agent 後只有勾選自動啟動的模型會執行，Port 衝突的那個留在 STOPPED，並出現在 warning 與該模型的日誌；開始關閉後拒絕會改變狀態的命令。
- GUI 截圖檢查：模型頁的「Duplicate」按鈕、預填複本設定的新增頁（名稱加上「(copy)」、使用下一個 Port、不勾自動啟動）、GUI 連線中停止 Agent 後顯示「Agent stopped」與「Start Agent」，且 GUI 不會自己重新啟動 Agent。

尚未驗證：Windows 上停止 Agent 時，llama-server 不回應 terminate 會在 3 秒後被強制結束，因此整個關閉會多花約 3 秒；「Start Agent」按鈕與原本的自動啟動共用同一段程式，但沒有自動化 GUI 點擊測試。

# Phase 4（第一部分）測試紀錄：系統匣、登入時啟動、安裝程式

Linux 部分的測試環境同上（Ubuntu 24.04、Qt 6.4.2）；Windows 部分由使用者在 Windows 上手動驗證（Qt 6.12.0 msvc2022_64、Inno Setup 6.7.3）。**尚未在 macOS 上執行。**

## 已執行

- 全部 targets 編譯成功（`-Wall -Wextra` 無警告）；20 項中 20 項通過（`guiDetachedAgent` 在非 Windows 平台自動略過）。
- 新增 `loginStartup`：Windows 工作排程 XML（格式正確、只限目前使用者、沒有執行時間上限、一般優先順序、含空白與 `&` 的路徑及參數）；Linux `.desktop` 的 Exec 引號規則；Linux 與 macOS 後端實際寫檔、讀回、程式搬移後自動修正路徑、關閉後移除檔案、桌面環境停用時視為關閉。
- 新增 `guiSingleInstanceAndQuit`：第二次啟動 GUI 會交給第一個並立即結束（exit 0）；`--quit` 會關閉 GUI、讓 Agent 停止所有模型後以 exit 0 結束、模型程序確實結束；沒有東西在執行時 `--quit` 也正常結束。
- 在 Xvfb + openbox + stalonetray 的真實 X11 桌面上手動檢查系統匣：圖示與綠色狀態點、選單（顯示/隱藏視窗、狀態、每個模型的子選單、開啟日誌資料夾、登入時啟動、停止 Agent、結束）、按關閉鈕後視窗隱藏但 GUI 繼續執行、左鍵切換視窗、第二次啟動叫回視窗、`--quit` 結束；勾選「隨 Agent 啟動」的模型會自動啟動。
- `cmake --install` 安裝規則（Linux）：執行檔與授權檔安裝正確。Qt 部署腳本需要 Qt 6.5，本機 Qt 6.4 會略過。

## Windows 手動驗證

- 登入時啟動：在 GUI 勾選後成功建立工作排程，登出再登入後程式會自動啟動。
- `packaging/windows/build-installer.ps1`：建置、`cmake --install`（windeployqt 部署 Qt 執行環境）與 Inno Setup 編譯成功，產生 setup.exe。
- 安裝程式的工作選項頁：核取方塊在顯示縮放大於 100% 時能完整顯示。
- 通知區域的系統匣圖示與選單。
- 安裝、升級、移除的完整流程。
- `llama-manager.exe` 與 `llama-manager-agent.exe` 的圖示與版本資訊（`.rc` 資源）。
- 全新安裝的 Windows 11 虛擬機（沒有 Visual C++ 執行階段、沒有獨立顯示卡）：安裝程式自動安裝 Visual C++ 執行階段，程式正常運作；沒有 NVIDIA GPU 時資源監控也正常。

## Windows 上發現並修正

- 勾選登入時啟動出現「Cannot create the sign-in task: 程序無法存取檔案，因為檔案正由另一個程序使用」：Windows 上的 `QTemporaryFile` 在 `close()` 後仍以獨佔方式佔用檔案，`schtasks.exe /XML` 無法讀取。改為寫入一般檔案並完全關閉，`schtasks.exe` 結束後再刪除。
- `build-installer.ps1` 找不到 `...\packaging\windows\G`：PowerShell 管線只有一個結果時會變成字串，`$licenseDirs[0]` 取到的是第一個字元。改用 `Select-Object -First 1` 直接取得路徑。
- 安裝程式的核取方塊在高 DPI 下被切掉：Inno Setup 的 `TNewCheckListBox` 只替核取方塊預留 13 px，卻依 DPI 畫出放大的主題核取方塊（6.5.4 到 7.1.0 都有此問題，升級無效；`WizardStyle` 的 `excludelightcontrols` 也無效）。在 `InitializeWizard` 依縮放比例加寬工作清單與完成頁清單的 `Offset`。
- 以 `ctest` 執行時所有測試都以 `0xc0000135`（找不到 DLL）結束：在 Qt Creator 外 Qt 的 DLL 不在 PATH 上。`CMakeLists.txt` 在 Windows 上以 `ENVIRONMENT_MODIFICATION` 把 Qt 的 bin 資料夾加到每個測試的 PATH 前面。
- `loginStartup` 在 Windows 上失敗（「存取被拒」）：`FileBackend::query()` 在檔案仍開啟時呼叫 `done()`，而 `refresh()` 會在其中以 `QSaveFile` 改寫同一個檔案，Windows 不允許取代開啟中的檔案。改為讀完即關閉檔案。測試也會印出 `error()` 的內容。
- `watchdogHealthRestart` 偶爾失敗：第一次啟動假 llama-server 時（防毒掃描新執行檔）進入 RUNNING 需要約 5.5 秒，超過測試的 5 秒。放寬為 10 秒。

## 尚未驗證

- macOS：app bundle、選單列單色圖示、LaunchAgent 登入啟動（目前只測試了 plist 的內容與讀寫）。

# Alias、opencode 設定、生成速度測試紀錄

測試環境同上（Ubuntu 24.04、Qt 6.4.2、offscreen）。**尚未在 Windows 上執行，也尚未連接真正的 llama-server。**

- 全部 targets 編譯成功（`-Wall -Wextra` 無警告）；`ctest` 29 項全部通過（`guiDetachedAgent` 在非 Windows 平台自動略過），連續 2 輪。
- 新增 `aliasOption`：`aliasEnabled` / `alias` 預設關閉、型別檢查與保存；啟用時不可為空、不可有前後空白、逗號或控制字元、最長 128 字；啟用時參數加上 `--alias`；額外參數不可設定 `-a` / `--alias`。
- 新增 `opencodeConfigBuild`：沒有模型 ID 時回報錯誤；provider 與模型名稱等於模型頁名稱；`0.0.0.0` 轉為 `127.0.0.1`；有 `/props` 時用它的 context，沒有時用 `-c` 除以 `--parallel`；output 上限 8192；多個 ID 時名稱加上 ID；`--api-key` 會帶入 `apiKey`。
- 新增 `opencodeConfigFetch`：對假 llama-server 實際讀取 `/v1/models`（回報 alias）與 `/props`；伺服器停止後回報錯誤、不產生設定。
- 新增 `throughputMeter`：以 `n_decoded` 的增加量計算每秒 token 數；第一筆只當基準；新任務在讀完提示前仍保留上一個任務的數字，不會被算成新輸出；生成結束後記錄該段的平均速度；支援舊版 `/slots` 格式（`next_token` 為陣列、`state` 為數字）。
- 新增 `throughputMonitor`：假伺服器以每秒 200 token 生成時，量到的速度在 100 到 300 之間、忙碌處理槽 1/2；停止後標記為無法取得；`--no-slots`（HTTP 501）回報已停用。
- 新增 `clientThroughputAndOpencode`：透過真的 Agent，GUI 用戶端累積速度歷史，並產生 alias 對應的 opencode 設定。
- GUI 截圖檢查：編輯頁的 Alias 勾選框（名稱連動、自訂後不再連動、清空時的警告與無法存檔）；模型頁的「opencode config」按鈕與對話框；資訊列的速度與「速度」分頁的圖表。

尚未驗證：真正的 llama-server `/slots` 在長時間生成、多個處理槽同時生成、推測解碼（`n_decoded` 一次增加多個）時的數字；opencode 實際讀取產生的設定。
- 新增 `agentCompatibility`：舊版存下、額外參數含 `--alias` 的 profiles.json 會轉成別名選項（逗號分隔時取第一個），Agent 讀取後存檔一次並留下 `.bak`；新格式仍不允許在額外參數設定 `--alias`；狀態帶有 `revision`；舊版 GUI 存檔時不帶別名欄位，別名會被保留。
- 新增 `clientRestartsOutdatedAgent`：連到沒有 `revision` 的舊 Agent 時 `agentOutdated` 為 true；「重新啟動 Agent」會停止舊 Agent，再啟動並連上這個版本的 Agent。
- 用使用者實際的 profiles.json 驗證轉換：兩個模型都能載入，第一個模型的 `--alias bonsai-2-27b` 移到別名選項，其餘設定不變。
