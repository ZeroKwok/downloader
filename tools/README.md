# download.exe

## 语法

```bash
$ ./download.exe
Using download.exe <url> [file] [timeout-ms] [connections]
```

## 用法

```bash
$ ./download.exe https://api.ipsw.me/v4/device/iPhone4,1 "J:\Temp\response.js" 30000
Downloading ...
Download failed, elapse: 00:30.5, error: Error: 0x00000081
```

## 日志

`%Temp%\DownloadLogs\`