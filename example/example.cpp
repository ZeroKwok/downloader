// This file is part of the downloader library
//
// Copyright (c) 2018-2023, zero.kwok@foxmail.com
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#include <nlog.h>
#include <signal.h>
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

int main()
{
    //text1();
    //text();
    //UtilTestForClassRange();
    //UtilTestForClassRangeFile();

    NLOG_CFG cfg = {
        L"",
        L"download-%m%d%H%M.log",
        L"",
        L"[{time}][{level}][{id}][{file}:{line}]: "
    };
    NLOG_SET_CONFIG(cfg);
    signal(SIGBREAK, BreakCallback);

    auto file1 = "https://secure-appldnld.apple.com/itunes12/001-80042-20210422-E8A351F2-A3B2-11EB-9A8F-CF1B67FC6302/iTunesSetup.exe";
    auto file2 = "https://updates.cdn-apple.com/ASU/032-71981-20230602-DC0154EB-2A7F-4411-B820-C78298A03DE3/AppleServiceUtilityCustomer.dmg";
    auto file3 = "http://localhost:3000/download/setup.exe";
    auto file4 = "http://localhost:3000/download2/setup.exe";
    auto file5 = "http://www2.aomeisoftware.com/download/ftl/FoneTool_free.exe";
    auto file6 = "http://192.168.3.104:8000/FoneTool_install.exe";
    auto file7 = "https://www2.aomeisoftware.com/download/testing-env/FoneTool_free.exe";
    auto file8 = "http://192.168.4.199/FTpackage/FoneTool_setup.exe";

    auto url = file8;
    std::filesystem::path directory = LR"(L:\Temp)";
    std::filesystem::path file = directory / util::path_find_filename(url);

    auto pos = util::win::cursor_pos();

    std::error_code ecode;
    DownloadFile(url, file, [=](const download_status& status)->bool
        {
            util::win::cursor_goto(pos);
            util::win::output_progress(status.processedBytes * 100.0 / status.totalBytes);
            return gFlags == kFlagRunning;
        },
        {}, ecode);

    std::cout << std::endl;
    if (ecode)
        std::cout << "download_file() failed, error: " << ecode.message() << std::endl;
    else
    {
        std::cout << "download_file() filished!" << std::endl;

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
        std::cout << "file_sha1_digest() -> " << util::bytes_into_hex(digest) << std::endl;
    }

    ::system("pause");

    return 0;
}

