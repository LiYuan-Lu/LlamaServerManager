# Llama Server Manager — Phase 1

Windows x64、Qt 6 / QML / C++17 的單一 llama-server 管理工具。GUI 與 Agent 是獨立執行檔，GUI 關閉或崩潰不會停止 Agent 或模型。這個版本不包含 Phase 2–4 的 Watchdog、HTTP 健康檢查、資源監控、多設定檔、多實例、系統匣或 Windows Service。

Release 與 Debug 各 7/7 測試通過，另已用真實 9B GGUF 驗證載入、GUI 重連及停止。完成下方建置與部署步驟後，可開啟 `deploy/llama-manager.exe`。

## 建置

需要 Qt 6.5 以上的 MSVC x64 套件（Core、Network、Quick、Quick Controls、Test）、CMake、VS 2026 C++ 桌面開發工具與 Windows SDK。Qt 的 `msvc2022_64` 套件名稱不代表必須使用 VS 2022 IDE；本機以 VS 2026 MSVC 建置。使用 VS 2026 generator 需要支援該 generator 的新版 CMake。

在專案根目錄 PowerShell 執行，依提示輸入自己的 Qt MSVC x64 套件目錄：

```powershell
$qtRoot = Read-Host "Qt MSVC x64 套件目錄"
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  "-DCMAKE_PREFIX_PATH=$qtRoot"
cmake --build build --config Release -- /m:1 /nr:false
$env:PATH = (Join-Path $qtRoot "bin") + ";" + $env:PATH
ctest --test-dir build -C Release --output-on-failure
& .\build\Release\llama-manager.exe
```

`/m:1 /nr:false` 避免受限執行環境中的 MSBuild worker 通訊等待；一般環境也可使用 `--parallel`。CMake 會自動尋找 Visual Studio；多個安裝版本時，可自行加上 `-DCMAKE_GENERATOR_INSTANCE` 指定安裝目錄。VS 2022 可改用 `Visual Studio 17 2022` generator。Debug 建置將 `Release` 改為 `Debug`。

## 使用

1. 開啟 `llama-manager.exe`。GUI 非同步連線；沒有 Agent 時會啟動同目錄的 `llama-manager-agent.exe`。
2. 使用「瀏覽」選擇自己的 `llama-server.exe` 與 `.gguf`。可指定工作目錄、Host、Port、Context、GPU layers、Threads、Parallel 與 Metrics。
3. 額外參數使用 JSON 字串陣列，如 `["--verbose"]`，每個陣列項目是一個參數。環境變數使用 JSON 物件，如 `{"CUDA_VISIBLE_DEVICES":"0"}`。不經 shell 執行。
4. 先「儲存設定」，再「啟動」。Agent 會先以 10 秒上限讀取所選執行檔的 `--help`；缺少必要旗標會明確拒絕啟動。不同 llama.cpp fork 的額外參數由該版本自行驗證，錯誤會出現在 stderr。
5. 查看 PID、Port、執行時間及 stdout/stderr。`RUNNING` 只代表 OS 程序啟動，不等於 GGUF 已載入或 API 已就緒；請查看日誌。
6. 「停止」先呼叫 terminate，3 秒後仍未結束便強制停止所擁有的程序。Windows console 程序通常會走強制停止。重新啟動是明確的手動操作。
7. 關閉 GUI 後 Agent / 模型持續執行。重新開啟自動恢復狀態及 Agent 記憶體中的近期日誌。

Agent 也可以獨立執行：`llama-manager-agent.exe`。兩支程式均支援 `--data-dir <絕對路徑>` 供隔離測試，必須傳相同路徑才能連線。正常使用不要混用不同資料目錄。Phase 1 不提供 GUI 停止 Agent 的操作；若要維護 Agent，先停止模型，再於工作管理員結束對應 Agent PID。不要依執行檔名稱批次殺程序。

## 資料與限制

- 正式設定位於 `%APPDATA%/LlamaServerManager/profiles.json`。單一 profile、schema version 1，使用 QSaveFile 原子寫入，保留上一個有效版本 `.bak`；損壞時讀取備份或預設值並記錄警告。
- 日誌：同目錄 `logs/server.jsonl`，每檔上限 4 MiB，保留 3 份輪替檔（總計約 16 MiB）。近期回放限制 1000 筆／256 KiB；GUI 文字也有上限。清除畫面不會刪除磁碟日誌。
- 不執行自動重啟；非預期結束顯示 FAILED 及 exit code。使用者 Stop 後維持停止。
- 單一 Agent 鎖定以資料目錄為範圍；named pipe 限目前使用者。不是對同一使用者的不可信程式提供安全隔離。
- Windows Job Object 用於所擁有的模型及其後續子程序；若系統拒絕 assignment，記錄警告並退回直接子程序管理。詳見架構文件。
- Port 預檢只能減少衝突，檢查至子程序 bind 間仍有競態，實際失敗由程序退出與 stderr 顯示。
- 模型與 llama-server 不隨專案附帶，無 Python runtime 依賴。

## 開發用部署

兩支 exe 要放在同一目錄。以下建立本機可執行資料夾；正式安裝程式、CPack、排程器及 Service 留給 Phase 4。

```powershell
New-Item -ItemType Directory -Force deploy | Out-Null
Copy-Item build\Release\llama-manager.exe,build\Release\llama-manager-agent.exe deploy
& (Join-Path $qtRoot "bin\windeployqt.exe") --release --qmldir apps\gui\qml deploy\llama-manager.exe
& (Join-Path $qtRoot "bin\windeployqt.exe") --release deploy\llama-manager-agent.exe
```

目標電腦需要相符的 MSVC x64 Redistributable。不要散布 Debug CRT。

若 windeployqt 找不到 Visual Studio，可在 Visual Studio Developer PowerShell 中執行，或將 `$env:VCINSTALLDIR` 設為自己的 Visual Studio `VC` 目錄，再加 `--compiler-runtime`。產生的 redistributable 安裝程式不會自動執行。

## 專案結構

```text
apps/agent/main.cpp          Agent entry、單例鎖
apps/gui/main.cpp            QML entry、AgentClient 注入
apps/gui/qml/Main.qml        狀態與啟停畫面
apps/gui/qml/ProfileEditor.qml  路徑／參數編輯及檔案對話框
apps/gui/qml/LogView.qml     即時日誌
core/Profile.*             型別、驗證、參數陣列
core/ProfileStore.*        原子儲存與備份復原
core/LlamaProcessManager.* 非同步 QProcess 與狀態轉換
core/LogStore.*             有限容量回放與磁碟輪替
ipc/Protocol.*             分幀、版本、大小限制
ipc/AgentServer.*          指令、狀態與 log 推播
ipc/AgentClient.*          非同步連線、重連、request timeout
tests/FakeServer.cpp       可控測試用 console 子程序
tests/Tests.cpp            Qt Test 與跨程序整合測試
docs/                      架構、協定、測試與後續工作
```

測試結果與尚未驗證的範圍見 [docs/testing.md](docs/testing.md)；Phase 2 待辦見 [docs/phase2.md](docs/phase2.md)。
