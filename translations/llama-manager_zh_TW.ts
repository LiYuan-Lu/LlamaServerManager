<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_TW" sourcelanguage="en">
<context>
    <name>AddCard</name>
    <message>
        <source>Add model</source>
        <translation>新增模型</translation>
    </message>
</context>
<context>
    <name>LogView</name>
    <message>
        <source>Auto-scroll</source>
        <translation>自動捲動</translation>
    </message>
    <message>
        <source>Copy all</source>
        <translation>複製全部</translation>
    </message>
    <message>
        <source>Clear view</source>
        <translation>清除畫面</translation>
    </message>
</context>
<context>
    <name>Main</name>
    <message>
        <source>● Agent connected</source>
        <translation>● Agent 已連線</translation>
    </message>
    <message>
        <source>● Agent reconnecting</source>
        <translation>● Agent 重新連線中</translation>
    </message>
    <message>
        <source>Llama Server Manager</source>
        <translation>Llama Server Manager</translation>
    </message>
    <message>
        <source>%1 of %2 models running · The Agent and models keep running after the window is closed</source>
        <translation>%1 / %2 個模型執行中 · 關閉視窗後，Agent 與模型仍會持續執行</translation>
    </message>
    <message>
        <source>No models yet. Click + to add your first model.</source>
        <translation>尚未新增任何模型。點選 + 新增第一個模型。</translation>
    </message>
</context>
<context>
    <name>ModelCard</name>
    <message>
        <source>Untitled model</source>
        <translation>未命名模型</translation>
    </message>
    <message>
        <source>No model file selected</source>
        <translation>尚未選擇模型檔案</translation>
    </message>
    <message>
        <source>  ·  PID %1</source>
        <translation>  ·  PID %1</translation>
    </message>
    <message>
        <source>  ·  up %1</source>
        <translation>  ·  已執行 %1</translation>
    </message>
    <message>
        <source>Restart %1/%2 in %3 s</source>
        <translation>%3 秒後第 %1/%2 次重啟</translation>
    </message>
    <message>
        <source>Restarting (attempt %1/%2)…</source>
        <translation>重新啟動中（第 %1/%2 次）…</translation>
    </message>
    <message>
        <source>Starting…</source>
        <translation>啟動中…</translation>
    </message>
    <message>
        <source>Loading model…</source>
        <translation>模型載入中…</translation>
    </message>
    <message>
        <source>Healthy</source>
        <translation>健康</translation>
    </message>
    <message>
        <source>Auto restart off</source>
        <translation>自動重啟已關閉</translation>
    </message>
    <message>
        <source>Health checks off</source>
        <translation>未啟用健康檢查</translation>
    </message>
    <message>
        <source>Not running</source>
        <translation>未執行</translation>
    </message>
    <message>
        <source>Loading model (HTTP 503)</source>
        <translation>模型載入中（HTTP 503）</translation>
    </message>
    <message>
        <source>Waiting for /health</source>
        <translation>等待 /health 回應</translation>
    </message>
</context>
<context>
    <name>ModelPage</name>
    <message>
        <source>Delete model</source>
        <translation>刪除模型</translation>
    </message>
    <message>
        <source>Delete &quot;%1&quot;? Its profile and logs will be removed. The model file itself is not touched.</source>
        <translation>要刪除「%1」嗎？其設定檔與日誌會被移除，模型檔案本身不受影響。</translation>
    </message>
    <message>
        <source>← All models</source>
        <translation>← 所有模型</translation>
    </message>
    <message>
        <source>New model</source>
        <translation>新增模型</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>刪除</translation>
    </message>
    <message>
        <source>PID  %1</source>
        <translation>PID  %1</translation>
    </message>
    <message>
        <source>Port  %1</source>
        <translation>Port  %1</translation>
    </message>
    <message>
        <source>Uptime  %1</source>
        <translation>執行時間  %1</translation>
    </message>
    <message>
        <source>Health  %1</source>
        <translation>健康狀態  %1</translation>
    </message>
    <message>
        <source>Restarts  %1/%2</source>
        <translation>重啟次數  %1/%2</translation>
    </message>
    <message>
        <source>Automatic restarts within the restart window</source>
        <translation>重啟時間窗內的自動重啟次數</translation>
    </message>
    <message>
        <source>With health checks on, the model stays STARTING until GET /health returns 200, then becomes RUNNING. Failed checks, crashes, and startup failures trigger automatic restarts with backoff; Stop always cancels them.</source>
        <translation>啟用健康檢查時，模型會維持 STARTING，直到 GET /health 回傳 200 才變成 RUNNING。健康檢查失敗、程序當掉或啟動失敗時，會以遞增的等待時間自動重啟；按下停止一定會取消自動重啟。</translation>
    </message>
    <message>
        <source>Console</source>
        <translation>主控台</translation>
    </message>
    <message>
        <source>Restart history (%1)</source>
        <translation>重啟紀錄（%1）</translation>
    </message>
    <message>
        <source>Healthy</source>
        <translation>健康</translation>
    </message>
    <message>
        <source>Health checks off</source>
        <translation>未啟用健康檢查</translation>
    </message>
    <message>
        <source>Not running</source>
        <translation>未執行</translation>
    </message>
    <message>
        <source>Loading model (HTTP 503)</source>
        <translation>模型載入中（HTTP 503）</translation>
    </message>
    <message>
        <source>Waiting for /health</source>
        <translation>等待 /health 回應</translation>
    </message>
</context>
<context>
    <name>ProfileEditor</name>
    <message>
        <source>Extra arguments must be a JSON array, and environment variables must be a JSON object</source>
        <translation>額外參數必須是 JSON 陣列，環境變數必須是 JSON 物件</translation>
    </message>
    <message>
        <source>Numeric fields must be integers</source>
        <translation>數值欄位必須為整數</translation>
    </message>
    <message>
        <source>Select llama-server.exe</source>
        <translation>選擇 llama-server.exe</translation>
    </message>
    <message>
        <source>Select GGUF model</source>
        <translation>選擇 GGUF 模型</translation>
    </message>
    <message>
        <source>Select working directory</source>
        <translation>選擇工作目錄</translation>
    </message>
    <message>
        <source>Select the executable</source>
        <translation>選擇執行檔</translation>
    </message>
    <message>
        <source>Browse…</source>
        <translation>瀏覽…</translation>
    </message>
    <message>
        <source>GGUF model</source>
        <translation>GGUF 模型</translation>
    </message>
    <message>
        <source>Select the model file</source>
        <translation>選擇模型檔案</translation>
    </message>
    <message>
        <source>Working directory</source>
        <translation>工作目錄</translation>
    </message>
    <message>
        <source>Leave empty to use the executable directory</source>
        <translation>留空使用執行檔目錄</translation>
    </message>
    <message>
        <source>Extra arguments (JSON)</source>
        <translation>額外參數 (JSON)</translation>
    </message>
    <message>
        <source>Environment variables (JSON)</source>
        <translation>環境變數 (JSON)</translation>
    </message>
    <message>
        <source>Save profile</source>
        <translation>儲存設定</translation>
    </message>
    <message>
        <source>Revert changes</source>
        <translation>還原編輯</translation>
    </message>
    <message>
        <source>Unsaved changes; save before starting</source>
        <translation>尚未儲存；儲存後才能啟動</translation>
    </message>
    <message>
        <source>Starts with the saved profile</source>
        <translation>使用已儲存的設定啟動</translation>
    </message>
    <message>
        <source>Enter a name for this model</source>
        <translation>請輸入模型名稱</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <source>Shown on the model card</source>
        <translation>顯示在模型卡片上</translation>
    </message>
    <message>
        <source>Add model</source>
        <translation>新增模型</translation>
    </message>
    <message>
        <source>Fill in the paths, then add the model to the list</source>
        <translation>填好路徑後，將模型加入清單</translation>
    </message>
    <message>
        <source>Watchdog</source>
        <translation>Watchdog</translation>
    </message>
    <message>
        <source>Restart automatically after crashes, startup failures, and failed health checks</source>
        <translation>程序當掉、啟動失敗或健康檢查失敗時自動重新啟動</translation>
    </message>
    <message>
        <source>Check GET /health (STARTING lasts until it returns 200)</source>
        <translation>檢查 GET /health（回傳 200 前維持 STARTING）</translation>
    </message>
    <message>
        <source>Interval (s)</source>
        <translation>間隔（秒）</translation>
    </message>
    <message>
        <source>Timeout (s)</source>
        <translation>逾時（秒）</translation>
    </message>
    <message>
        <source>Failures before unhealthy</source>
        <translation>判定異常的連續失敗次數</translation>
    </message>
    <message>
        <source>Startup grace (s)</source>
        <translation>載入寬限期（秒）</translation>
    </message>
    <message>
        <source>Restart cooldown (s)</source>
        <translation>重啟冷卻（秒）</translation>
    </message>
    <message>
        <source>Max backoff (s)</source>
        <translation>最長等待（秒）</translation>
    </message>
    <message>
        <source>Max restarts</source>
        <translation>最多重啟次數</translation>
    </message>
    <message>
        <source>Within (s)</source>
        <translation>時間窗（秒）</translation>
    </message>
</context>
<context>
    <name>RestartHistoryView</name>
    <message>
        <source>Crash</source>
        <translation>程序當掉</translation>
    </message>
    <message>
        <source>Startup failure</source>
        <translation>啟動失敗</translation>
    </message>
    <message>
        <source>Health check</source>
        <translation>健康檢查失敗</translation>
    </message>
    <message>
        <source>Recovered</source>
        <translation>已復原</translation>
    </message>
    <message>
        <source>Restart failed</source>
        <translation>重啟失敗</translation>
    </message>
    <message>
        <source>Restart pending</source>
        <translation>等待重啟</translation>
    </message>
    <message>
        <source>Gave up: restart limit reached</source>
        <translation>已放棄：達到重啟上限</translation>
    </message>
    <message>
        <source>Cancelled by manual stop</source>
        <translation>已因手動停止取消</translation>
    </message>
    <message>
        <source>Superseded by manual restart</source>
        <translation>已由手動重啟取代</translation>
    </message>
    <message>
        <source>Auto restart disabled</source>
        <translation>未啟用自動重啟</translation>
    </message>
    <message>
        <source>Interrupted by Agent restart</source>
        <translation>因 Agent 重新啟動而中斷</translation>
    </message>
    <message>
        <source>No failures recorded. Crashes, startup failures, and failed health checks will appear here.</source>
        <translation>目前沒有故障紀錄。程序當掉、啟動失敗與健康檢查失敗會顯示在這裡。</translation>
    </message>
    <message>
        <source>Attempt %1</source>
        <translation>第 %1 次</translation>
    </message>
    <message>
        <source>PID %1</source>
        <translation>PID %1</translation>
    </message>
</context>
<context>
    <name>ServerControls</name>
    <message>
        <source>▶ Start</source>
        <translation>▶ 啟動</translation>
    </message>
    <message>
        <source>Stopping…</source>
        <translation>停止中…</translation>
    </message>
    <message>
        <source>■ Stop</source>
        <translation>■ 停止</translation>
    </message>
    <message>
        <source>⟳ Restart</source>
        <translation>⟳ 重新啟動</translation>
    </message>
</context>
</TS>
