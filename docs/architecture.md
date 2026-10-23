# Phase 1 架構

GUI (`QGuiApplication`) → AgentClient → per-user QLocalSocket → AgentServer (`QCoreApplication`) → LlamaProcessManager → QProcess → llama-server。

GUI 從不持有 llama-server QProcess。Agent 由 startDetached 啟動，生命週期不受 GUI socket 與 GUI 結束影響。Agent 活著時，模型 PID、狀態、執行參數和近期日誌由 Agent 持有；GUI 重連不會重新啟動模型。不將磁碟舊 PID 當作仍有效的程序，也不重新接管未知程序。

## 狀態

STOPPED / FAILED → STARTING（非同步 --help 預檢與 OS 啟動）→ RUNNING。
STARTING / RUNNING → STOPPING → STOPPED。
非使用者停止的退出或啟動失敗 → FAILED。RUNNING 代表 OS 程序存在，沒有 HTTP 健康語意。

手動 restart 只在 RUNNING 接受，先停止再重新啟動；STOPPING 時禁止重複 start / restart。Stop 取消待進行的 restart。每次 --help 與程序建立都有 10 秒 timeout；Stop 在 3 秒後 kill。模型載入時間本身不設限，因此大型模型不會因載入慢而被終止。

Profile 是純資料型別，ProfileStore 處理檔案；LlamaProcessManager 不依賴 QML 或 IPC。AgentServer 是命令協調層。測試可以直接建立 ProcessManager 或啟動完整 Agent。

## 程序擁有權

每個資料目錄取得 QLockFile 後才 listen，同帳號 GUI 使用同一 endpoint。QLocalServer 使用 UserAccessOption；QLockFile 不依鎖檔年齡移除仍存活的鎖。

Windows 上模型在 started 時加入帶 KILL_ON_JOB_CLOSE 的 Job Object；只影響這個 Agent 建立並加入的程序。Agent 異常退出時通常也會終止此 job 中模型，避免無法接管的孤兒。這不影響 GUI 關閉，因 GUI 不擁有 job。

限制：Qt started signal 到 job assignment 有短暫空窗，立即衍生的子程序不保證被納管；assignment 被外部 job 限制時會記錄警告並僅管理直接子程序。標準 llama-server 為直接單一程序；未宣稱能可靠管理任意 wrapper 的程序樹。若後續必須支援任意 wrapper，應以 suspended launch + job assignment + resume 進行 Windows 專用擴充。從不掃描或按名稱殺掉其他 llama-server。

## 容量與失敗

JSON newline frame 上限 1 MiB，單一 socket 待送上限 2 MiB，超過則中斷慢客戶端，使模型繼續執行。最多 16 個 GUI 連線。每個 log chunk 最多 4096 bytes，UTF-8 streaming decoder 保留跨讀取字元。記憶體與磁碟均有限容量；磁碟寫入／輪替失敗推播警告，不停止模型。

設定先驗證再原子寫入；讀取壞檔不立即覆寫。GUI 非同步重連間隔 1.5 秒，啟動 Agent 嘗試最多每 10 秒一次。請求 15 秒後報 timeout，不自動重送可能改變狀態的命令，避免重複操作。Agent 每秒推播狀態。

Qt 主執行緒的事件式 QProcess/IPC 不使用 waitForStarted/waitForFinished；僅 Agent 結束時 destructor 有 bounded cleanup，測試亦可使用等待。磁碟操作為短小同步本機 JSON/log 寫入；此 MVP 未實作慢速網路磁碟的背景寫入佇列，正式資料目錄應置於本機磁碟。
