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
