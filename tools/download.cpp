// This file is part of the downloader library
//
// Copyright (c) 2018-2023, zero.kwok@foxmail.com
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#include <nlog.h>
#include <signal.h>
#include <conio.h>
#include "downloader.h"
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
    if (argc < 2) {
        std::cerr << "Using download.exe <url> [file] [timeout] [connections]" << std::endl;
        return -2;
    }

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
        if (tail && (tail[0] != '\0')) {
            std::cerr << "Using download.exe <url> [file] [timeout] [connections]" << std::endl;
            return -2;
        }
    }

    if (argc > 4)
    {
        char* tail = nullptr;
        preference.connections = strtoull(argv[4], &tail, 10);
        if (tail && (tail[0] != '\0')) {
            std::cerr << "Using download.exe <url> [file] [timeout] [connections]" << std::endl;
            return -2;
        }
    }

    //_getch();
    NLOG_APP(" download.exe argc: %d", argc);
    NLOG_APP(" - URL: ") << url;
    NLOG_APP(" - File: ") << file;
    NLOG_APP(" - Timeout: ") << preference.timeout;
    NLOG_APP(" - Connections: ") << preference.connections;

    auto pos = util::win::cursor_pos();
    DownloadFile(url, file, [=](const download_status& status)->bool
        {
            util::win::cursor_goto(pos);
            util::win::output_progress(status.processedBytes * 100.0 / status.totalBytes);
            return gFlags == kFlagRunning;
        },
        preference, ecode);

    std::cout << std::endl;
    if (ecode)
    {
        std::cerr << "Download failed, error: " << ecode.message() << std::endl;
    }
    else
    {
        std::cout << "Download finished!" << std::endl;

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

