# IPC v1

傳輸為 QLocalSocket / QLocalServer，UTF-8 JSON object，每筆以 LF 結束。JSON 字串內 newline 以 JSON escaping 表示。收包累積處理 partial/coalesced frames，單筆最大 1 MiB。endpoint 由資料目錄絕對路徑 SHA-256 前 24 位生成，名稱前綴 `LlamaServerManager-v1-`。同一 GUI/Agent 使用相同絕對路徑。

Request（id 必須為 1–128 字元字串）：

```json
{"version":1,"id":"uuid","command":"getStatus"}
```

Response：

```json
{"version":1,"id":"uuid","ok":true,"error":"","data":{"state":"RUNNING","pid":1234}}
```

Push：

```json
{"version":1,"event":"log","data":{"sequence":1,"time":"2026-10-23T02:00:00.000Z","channel":"stdout","text":"loading...\n"}}
```

Phase 1 指令：

| Command | Payload / response |
| --- | --- |
| getStatus | data: state, pid, port, model, uptimeSeconds, error, arguments, health, profile |
| getLogs | data.entries: 有限容量近期 log 陣列 |
| updateProfile | request.profile: Profile object；成功回傳完整狀態；執行期間拒絕 |
| startServer | 使用已存 profile，接受後異步更新狀態 |
| stopServer | 取消手動 restart，停止當前 owned process；重複停止安全 |
| restartServer | RUNNING 時手動停止再啟動；STOPPED/FAILED 時等同 start |

`status` push 是完整 snapshot，`log` push 是單筆紀錄，`warning` push 含 message（例如磁碟日誌失敗）。未知命令回傳 ok=false。非法 JSON、版本不相容、超過大小或非法 id 將斷線。沒有實作後續階段的 listProfiles/deleteProfile/getMetrics/getRestartHistory。

成功回應代表「命令已接受」，不代表模型已載入。真正結果由 status 與 log 觀察。連線中斷不會停止服務；GUI 重連必須重新 getStatus/getLogs，不得自動重送 start/restart。

Profile 欄位與預設：executable/model/workingDirectory 空字串、host=127.0.0.1、port=8080、context=4096、gpuLayers=0、threads=4、parallel=1、metrics=false、extraArguments=[]、environment={}。啟動時要求 exe/GGUF 絕對路徑存在。Host 為 numeric IP，GPU layers 允許 -1。每個 extraArguments 元素獨立傳給 QProcess，不經 shell；禁止覆寫受管理的主要選項。
