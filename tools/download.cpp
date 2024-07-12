// This file is part of the downloader library
//
// Copyright (c) 2018-2023, zero.kwok@foxmail.com
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#include <nlog.h>
#include <signal.h>
#include <conio.h>
#include "downloader.h"
#include "common/unit.h"
#include "common/assert.hpp"
#include "common/digest.hpp"
#include "platform/console_win.h"
#include "filesystem/path_util.h"

#include <iostream>

enum {
    kFlagRunning     = 0x0,
    kFlagInterrupted = 0x1,
};
static std::atomic_int gFlags(kFlagRunning);

void BreakCallback(int signum)
{
    switch (signum)
    {
    case SIGBREAK:
        gFlags |= kFlagInterrupted;
        break;
    }
    signal(signum, BreakCallback);
}

int main(int argc, char** argv)
{
    auto showHelp = []()
        {
            std::cerr << "Using download.exe <url> [file] [timeout-ms] [connections]" << std::endl;
            return -2;
        };

    if (argc < 2)
        return showHelp();

    NLOG_CFG cfg = {
        util::path_from_temp("DownloadLogs"),
        L"download-%m%d%H%M.log",
        L"",
        L"[{time}][{level}][{id}][{file}:{line}]: "
    };
    NLOG_SET_CONFIG(cfg);
    signal(SIGBREAK, BreakCallback);

    std::error_code ecode;
    std::string url  = argv[1];
    std::string file = argc > 2 ? argv[2] : util::path_find_filename(url);

    download_preference preference;
    if (argc > 3)
    {
        char* tail = nullptr;
        preference.timeout = strtoull(argv[3], &tail, 10);
        if (tail && (tail[0] != '\0'))
            return showHelp();
    }

    if (argc > 4)
    {
        char* tail = nullptr;
        preference.connections = strtoull(argv[4], &tail, 10);
        if (tail && (tail[0] != '\0'))
            return showHelp();
    }

    //_getch();
    NLOG_APP(" download.exe argc: %d", argc);
    NLOG_APP(" - URL: ") << url;
    NLOG_APP(" - File: ") << file;
    NLOG_APP(" - Timeout(MS): ") << preference.timeout;
    NLOG_APP(" - Connections: ") << preference.connections;

    std::cout << "Downloading ..." << std::endl;

    auto pos = util::win::cursor_pos();
    auto start = std::chrono::steady_clock::now();
    auto laset = 0.0;
    DownloadFile(url, file, [&](const download_status& status)->bool
        {
            auto p = status.processedBytes * 100.0 / status.totalBytes;
            if (p - laset > 0.1) {
                laset = p;
                util::win::cursor_goto(pos);
                util::win::output_progress(p);
            }
            return gFlags == kFlagRunning;
        },
        preference, ecode);
    auto end = std::chrono::steady_clock::now();
    auto elapse = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (ecode)
    {
        std::cerr << "Download failed, elapse: " 
            << util::duration_ms_format(elapse) 
            << ", error: " << ecode.message();
    }
    else
    {
        std::cout << "Download finished, elapse: " 
            << util::duration_ms_format(elapse)
            << std::endl;

        auto pos1 = util::win::cursor_pos();
        auto block = 1024 * 512;
        auto digest = util::file_sha1_digest(file, block,
            [=](util::fsize processed, util::fsize size) -> bool {
                if (processed % (block * 4) == 0) {
                    util::win::cursor_goto(pos1);
                    util::win::output_progress(processed * 100.0 / size);
                }
                return gFlags == kFlagRunning;
            });

        std::cout << std::endl;
        std::cout << "SHA1: " << util::bytes_into_hex(digest) << std::endl;
    }

    return 0;
}
