# Phase 2 尚待實作

本次沒有建立未完成的 HealthMonitor / RestartPolicy 空殼。Phase 1 的 FAILED 與 exit code 只做基本程序監控，不會自動復原。

- 獨立 HealthMonitor：非同步 GET /health、每次 5 秒 timeout、預設每 30 秒一次、禁止重疊請求；啟動 600 秒寬限、載入中 503 處理。
- 獨立 RestartPolicy：區分使用者停止與 crash / 啟動失敗 / HTTP 不健康；連續失敗閾值、冷卻、指數 backoff、10 分鐘重啟上限。
- 擴展 UNHEALTHY / RESTARTING / BACKOFF 狀態，防止同時復原與重啟循環。
- Process generation token：忽略舊 HTTP 回應與舊 timer，避免影響新程序。
- Restart History：故障時間、profile ID、PID、原因、重試次數、復原結果與容量限制。
- Watchdog 設定儲存與 GUI 控制、手動 Stop 清除所有待處理復原。
- Fake HTTP / 可注入 clock 與 policy 測試；涵蓋載入 503、timeout、重複失敗、重試上限、舊結果、手動停止。
- 可選主動推論健康測試；不作預設強制重啟條件。

Phase 3 的系統/GPU監控、多 profiles、多 instances、進階 Log Viewer，以及 Phase 4 的系統匣／排程器／Service／安裝包繼續維持未實作。
