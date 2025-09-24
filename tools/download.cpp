// This file is part of the downloader library
//
// Copyright (c) 2018-2025, Zero <zero.kwok@foxmail.com>
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#include <nlog.h>
#include <signal.h>
#include <conio.h>

#include <iostream>
#include <fmt/core.h>
#include <boost/program_options.hpp>

#include "downloader.h"
#include "common/unit.h"
#include "common/assert.hpp"
#include "common/digest.hpp"
#include "platform/console_win.h"
#include "filesystem/path_util.h"

namespace po = boost::program_options;

enum {
    kFlagRunning = 0x0,
    kFlagInterrupted = 0x1,
};
static std::atomic_int gFlags(kFlagRunning);

void BreakCallback(int signum)
{
    switch (signum)
    {
    case SIGINT:
    case SIGBREAK:
        gFlags |= kFlagInterrupted;
        break;
    }
    signal(signum, BreakCallback);
}

int main(int argc, char** argv)
{
    signal(SIGINT, BreakCallback);
    signal(SIGBREAK, BreakCallback);
    try {
        bool debug = false;
        std::string url;
        std::string file;
        uint64_t timeout = 0;
        uint64_t connections = 0;

        // 定义命令行选项
        po::options_description desc("Usage: download.exe <url> [options]");
        desc.add_options()
            ("help,h", "Show help message")
            ("url", po::value<std::string>(&url), "Download URL")
            ("file,f", po::value<std::string>(&file), "Output file path")
            ("timeout,t", po::value<uint64_t>(&timeout)->default_value(0), "Timeout in milliseconds")
            ("connections,c", po::value<uint64_t>(&connections)->default_value(4), "Number of connections (default 4)")
            ("debug,d", po::bool_switch(&debug), "Enable debug mode (pause before run)");

        po::positional_options_description pos;
        pos.add("url", 1);
        pos.add("file", 1);

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(desc).positional(pos).run(), vm);
        po::notify(vm);

        if (vm.count("help") || !vm.count("url")) {
            std::cerr << desc << std::endl;
            return -2;
        }

        if (!vm.count("file")) {
            file = util::path_find_filename(url);
        }

        if (debug) {
            std::cout << "Press any key to continue..." << std::endl;
            _getch();
        }

        download_preference preference;
        preference.timeout = timeout;
        preference.connections = connections;

        // 日志初始化
        NLOG_CFG cfg = {
            util::path_from_temp("DownloadLogs"),
            L"download-%m%d%H.log",
            L"",
            L"[{time}][{level}][{id}][{file}:{line}]: "
        };
        NLOG_SET_CONFIG(cfg);

        NLOG_APP();
        NLOG_APP(" download.exe argc: %d", argc);
        NLOG_APP(" - URL: ") << url;
        NLOG_APP(" - File: ") << file;
        NLOG_APP(" - Timeout(MS): ") << preference.timeout;
        NLOG_APP(" - Connections: ") << preference.connections;

        std::cout << "Downloading ..." << std::endl;

        std::error_code ecode;
        auto start = std::chrono::steady_clock::now();
        auto measure = [](auto start) -> int {
            namespace chr = std::chrono;
            return (int)chr::duration_cast<chr::milliseconds>(
                chr::steady_clock::now() - start).count();
            };
        auto lastBytes = 0LL;

        auto last = start;
        auto cpos = util::win::cursor_pos();
        DownloadFile(url, file, [&](const download_status& status)->bool
            {
                auto elapse = measure(last);
                if (elapse >= 500) {
                    last = std::chrono::steady_clock::now();
                    
                    auto speed = (status.processedBytes - lastBytes) / elapse * 1000;
                    auto progress = status.processedBytes * 100.0 / status.totalBytes;

                    lastBytes = status.processedBytes;

                    util::win::cursor_goto(cpos);
                    util::win::output_progress(progress);

                    fmt::print(" {}/{}\t{}/s        ",
                        util::bytes_add_suffix(status.processedBytes),
                        (status.totalBytes == 0 ? "--" : util::bytes_add_suffix(status.totalBytes)),
                        util::bytes_add_suffix(speed));
                }
                return gFlags == kFlagRunning;
            },
            preference, ecode);
        auto elapse = measure(start);

        std::cout << std::endl;
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
    }
    catch (const std::exception& e) {
        std::cerr << "\nException: " << e.what() << std::endl;
    }

    return 0;
}

