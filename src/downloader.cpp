// This file is part of the downloader library
//
// Copyright (c) 2018-2023, zero.kwok@foxmail.com
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#include "nlog.h"
#include "range.hpp"
#include "range_file.hpp"
#include "downloader.h"

#include "cpr/cpr.h"
#include "common/scope.hpp"
#include "common/assert.hpp"
#include "platform/platform_util.h"
#include <boost/algorithm/string.hpp>

static inline std::shared_ptr<cpr::Session> MakeSession(const cpr::Url& url)
{
    auto session = std::make_shared<cpr::Session>();
    session->SetUrl(url);
    session->SetOption(cpr::Redirect{});
    session->SetOption(cpr::VerifySsl{ false });
    session->SetConnectTimeout(3000);
    session->SetHeader(cpr::Header{
        {"Connection", "keep-alive"}
        });
    return session;
}

static inline size_t WriteHeadCallback(char* buffer, size_t size, size_t nitems, file_attribute* attribute) {
    size *= nitems;
    if (strncmp(buffer, "Accept-Ranges:", 14) == 0)
        attribute->acceptRanges = boost::algorithm::trim_copy(std::string(buffer + 14, size - 14));
    if (strncmp(buffer, "Content-Range:", 14) == 0)
        attribute->contentRange = boost::algorithm::trim_copy(std::string(buffer + 14, size - 14));
    attribute->header.append(buffer, size);
    return size;
}

bool GetFileAttribute(file_attribute& attribute, const std::string& url, std::error_code& error)
{
    return GetFileAttribute(attribute, url, 3000, error);
}

bool GetFileAttribute(
    file_attribute& attribute,
    const std::string& url,
    int timeout,
    std::error_code& error)
{
    error.clear();
    attribute = {};
    try
    {
        CURL* curl = curl_easy_init();
        if (curl == nullptr)
            return !(error = util::MakeError(util::kRuntimeError));
        util_scope_exit = [&] { curl_easy_cleanup(curl); };

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout);

        cpr::VerifySsl verify{ false };
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);

        cpr::Redirect redirect{};
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, redirect.follow ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, redirect.maximum);
        curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, redirect.cont_send_cred ? 1L : 0L);

        // NOLINTNEXTLINE (google-runtime-int)
        long mask = 0;
        if (cpr::any(redirect.post_flags & cpr::PostRedirectFlags::POST_301))
            mask |= CURL_REDIR_POST_301;
        if (cpr::any(redirect.post_flags & cpr::PostRedirectFlags::POST_302))
            mask |= CURL_REDIR_POST_302;
        if (cpr::any(redirect.post_flags & cpr::PostRedirectFlags::POST_303))
            mask |= CURL_REDIR_POST_303;
        curl_easy_setopt(curl, CURLOPT_POSTREDIR, mask);

        // header
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteHeadCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &attribute);

        // range
        curl_easy_setopt(curl, CURLOPT_RANGE, "0-");

        CURLcode res = curl_easy_perform(curl);
        switch (res)
        {
        case CURLE_OK: {
            long status_code{};
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
            if (200 == status_code || 206 == status_code)
                curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &attribute.contentLength);
            if (206 == status_code && attribute.acceptRanges.empty())
                attribute.acceptRanges = "bytes"; // 这里要确定: 返回206 是否一定表示支持: range: bytes
        }
        break;

        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SSL_CONNECT_ERROR:
            // 网络错误
            NLOG_ERR("GetFileAttribute() failed, error: {1}, {2}")
                % res
                % curl_easy_strerror(res);
            error = util::MakeError(util::kNetworkError);
            break;

        default:
            // 未知错误 或 运行时错误
            NLOG_ERR("GetFileAttribute() failed, error: {1}, {2}")
                % res
                % curl_easy_strerror(res);
            error = util::MakeError(util::kRuntimeError);
            break;
        }
    }
    catch (const std::exception& e)
    {
        NLOG_ERR("Unhandled exception: ") << e.what();
        error = util::MakeError(util::kRuntimeError);
    }

    return !error;
}

enum { kRunning = 0, kFailed, kCancelled };

//
// 错误处理
// 致命错误 & 非致命错误
//   文件系统错误
//      磁盘已满
//      权限不足
//      磁盘无法访问
//   网络错误
//      断开连接 ?
// 返回true, 表示致命错误
// 
bool HandleRequestError(
    const cpr::Response& response, 
    const std::error_code& ecode, 
    const std::atomic_int& flag,
    std::error_code& error)
{
    switch (response.error.code)
    {
    case cpr::ErrorCode::REQUEST_CANCELLED:
        if (ecode)
        {
            // 因为错误终止, 在这里几乎都是文件操作, 可以归属为致命错误
            NLOG_ERR("Request Error: {1}, {2}")
                % response.status_code
                % ecode.message();
            error = ecode;
        }
        else
        {
            // 操作取消
            util_assert(flag != kRunning);
            if (flag == kCancelled) // 只有取消才改写error
                error = util::MakeError(util::kOperationInterrupted);
        }
        return true;

        // 余下的错误, 不好判断是否属于致命错误(因为网络错误可能因为重试变得好转)
        // 因此, 这里仅设置错误码, 由外部做决断
    case cpr::ErrorCode::NETWORK_SEND_FAILURE:
    case cpr::ErrorCode::NETWORK_RECEIVE_ERROR:
    case cpr::ErrorCode::HOST_RESOLUTION_FAILURE:
    case cpr::ErrorCode::CONNECTION_FAILURE:
    case cpr::ErrorCode::OPERATION_TIMEDOUT:
    case cpr::ErrorCode::SSL_CONNECT_ERROR:
        // 网络错误
        NLOG_ERR("Request Error: {1}, {2}, {3}")
            % response.status_code
            % int(response.error.code)
            % response.error.message;
        error = util::MakeError(util::kNetworkError);
        return false;

    case cpr::ErrorCode::INTERNAL_ERROR:
    case cpr::ErrorCode::EMPTY_RESPONSE:
        // 未知错误 或 运行时错误
        NLOG_ERR("Request Error: {1}, {2}, {3}")
            % response.status_code
            % int(response.error.code)
            % response.error.message;
        error = util::MakeError(util::kNetworkError);
        return false;

    case cpr::ErrorCode::OK:
        if (200 == response.status_code || 206 == response.status_code)
            return false; // 成功

        if (404 == response.status_code) { // 资源不存在
            error = util::MakeError(util::kFileNotFound);
            return true;
        }

        if (503 == response.status_code) { // 服务不可用
            error = util::MakeError(util::kServerError);
            return true;
        }

        if (400 <= response.status_code) { // 下载错误
            NLOG_ERR("Request Error: {1}, {2}")
                % response.status_code
                % response.error.message;
            error = util::MakeError(util::kOperationFailed);
        }
        return false;
    }

    return false;
}

bool DownloadFile(
    const std::string& url, 
    const std::filesystem::path& filename,
    const std::function<bool(const download_status&)>& callback,
    const download_preference config,
    std::error_code& error
    )
{
    error.clear();
    try
    {
        std::atomic_int flag(kRunning);

        NLOG_PRO("DownloadFile() ...");
        NLOG_PRO(" - URL : ") << url;
        NLOG_PRO(" - File: ") << filename;
        NLOG_PRO(" - TimeOut(MS): ") << config.timeout;
        NLOG_PRO(" - Connections: ") << config.connections;
        NLOG_PRO(" - BlockSize: ") << config.blockSize;
        NLOG_PRO(" - Interval: ") << config.interval;

        file_attribute attribute = {};
        if (config.connections > 1) //  单点下载不用探测文件长度
        {
            int tryCount = 5;
            do 
            {
                if (!GetFileAttribute(attribute, url, config.timeout, error))
                    if (error.value() != util::kNetworkError)
                        return !error;
                break;
            }
            while (--tryCount > 0);

            if (error) // 重试后仍不成功
            {
                NLOG_ERR("DownloadFile() failed, error: ") << error.message();
                return !error;
            }

            NLOG_PRO("GetFileAttribute() -> {1}\r\n{2}")
                % attribute.contentLength
                % attribute.header;
        }

        util::ferror ferr;
        if (util::file_exist(filename, ferr))
            util::file_remove(filename, ferr);
        if (ferr) {
            NLOG_ERR("util::file_*() failed, error: ") << ferr.message();
            return !(error = util::MakeErrorFromNative(ferr.code(), filename, util::kFilesystemError));
        }

        RangeFile rf;
        util_scope_exit = [&] {
            std::error_code ecode;
            if (rf && !rf.close(!error, ecode)) {
                error = error ? error :ecode; // 若关闭前有错误, 则不改变之前的错误
                NLOG_ERR("RangeFile::close() failed, error: ") << ecode.message();
            }
        };

        auto session1 = MakeSession(url);
        if (attribute.contentLength == -1 || 
            attribute.contentLength <= config.blockSize ||
            attribute.acceptRanges.empty())
        {
            NLOG_PRO("Direct download ...");

            // 未知大小 or 长度太短 or 不支持范围请求, 只能单点下载
            session1->SetConnectTimeout(config.timeout);
            session1->SetProgressCallback(cpr::ProgressCallback(
                [&](cpr::cpr_off_t downloadTotal,
                    cpr::cpr_off_t downloadNow,
                    cpr::cpr_off_t, cpr::cpr_off_t, intptr_t) -> bool
                {
                    // downloadTotal 很可能为0
                    if (callback && !callback({ downloadTotal, downloadNow })) {
                        flag = kCancelled;
                        return false;
                    }
                    return true;
                }));

            rf.reserve(attribute.contentLength);
            if (!rf.open(filename, error)) {
                NLOG_ERR("rf.open({1}) failed, error: {2}")
                    % filename.wstring()
                    % error.message();
                return !error;
            }

            std::error_code ecode;
            auto response = session1->Download(cpr::WriteCallback{
                [&](const std::string& data, intptr_t userdata) -> bool {
                    return rf.fill(data, data.size(), ecode);
                }});
            HandleRequestError(response, ecode, flag, error);

            if (response.status_code != 200 || error)
            {
                NLOG_ERR("Direct download failed, status code: {1}, error: {2}")
                    % response.status_code
                    % error.message();
            }
            else
            {
                NLOG_PRO("Direct download finished, status code: {1}")
                    % response.status_code;
            }
            return !error;
        }

        NLOG_PRO("Multipoint download ...");

        rf.reserve(attribute.contentLength, config.blockSize);
        if(!rf.open(filename, error)) {
            NLOG_ERR("RangeFile::open() failed, error: ") << error.message();
            return !error;
        }

        // 工作线程状态
        struct State {
            enum { 
                kThreadNone = 0, 
                kThreadRunning, 
                kThreadFinished, 
                kThreadInterrupted 
            };
            int flag = kThreadNone;
            std::error_code error;
        };

        auto worker = [&](State& state)
        {
            state.flag = State::kThreadRunning;
            NLOG_APP("Worker start: {1}") % std::this_thread::get_id();
            util_scope_exit = [&] {
                state.flag = state.error ? State::kThreadInterrupted 
                                         : State::kThreadFinished;
                NLOG_APP("Worker finished: {1}, result: {2}")
                    % std::this_thread::get_id()
                    % state.error.message();
            };

            try 
            {
                auto session = MakeSession(url);

                Range2 range;
                while (flag == kRunning && rf.allocate(range))
                {
                    util_scope_exit = [&] {
                        rf.deallocate(range);
                    };

                    std::error_code ecode;
                    session->SetOption(cpr::Range{ range.start, range.end });

#if 0
                    //
                    // 有的服务器可能会在下载过程中返回错误信息, 此时下载的内容并不是文件内容
                    // 因此不能直接写入文件
                    session->SetWriteCallback(cpr::WriteCallback{
                        [&](const std::string& data, intptr_t userdata) -> bool
                        {
                            rf.fill(range, data, data.size(), ecode);
                            return !ecode && flag == kRunning;
                        } });
#endif

                    auto response = session->Get();
                    if (response.status_code == 200 || response.status_code == 206)
                        rf.fill(range, response.text, response.text.size(), ecode);

                    if (HandleRequestError(response, ecode, flag, state.error))
                        return;
                }
            }
            catch (const std::exception& e)
            {
                NLOG_ERR("Unhandled exception: ") << e.what();
                state.error = util::MakeError(util::kRuntimeError);
            }
        };

        std::vector<State> states(config.connections);
        std::vector<std::shared_ptr<std::thread>> threads;
        for (int i = 0; i < config.connections; ++i)
            threads.push_back(std::make_shared<std::thread>(worker, std::ref(states[i])));

        size_t threadIndex = 0;
        while (flag.load() == kRunning && !rf.is_full())
        {
            // 在下载阶段的尾声, 部分线程开始陆续退出, 此时不会设置错误
            // 因此, 在检测是否发生错误时, 要排除正常结束的线程.
            if (states[threadIndex].flag == State::kThreadFinished) {
                if (++threadIndex >= states.size())
                    break;
            }

            if (states[threadIndex].error) // 存在错误
            {
                std::map<int, int> counts;
                for (auto& s : states)
                    counts[s.error.value()]++;
                if (counts.count(util::kSucceed) == 0) // 所有连接均出错
                {
                    std::pair<int, int> item;
                    for (auto p : counts)
                        item = item.second > p.second ? item : p;
                    NLOG_ERR("download_file({1}, {2}) failed, error: {3}, count: {4}")
                        % url
                        % filename.wstring()
                        % item.first
                        % item.second;

                    flag  = kFailed;
                    error = util::MakeError(item.first);
                    break;
                }
            }

            if (callback)
            {
                if (!callback({ attribute.contentLength, rf.processed() }))
                {
                    flag  = kCancelled;
                    error = util::MakeError(util::kOperationInterrupted);
                    break;
                }
            }

            std::error_code ecode;
            if (!rf.dump(ecode))
                NLOG_WAR("RangeFile::dump() failed, error: ") << ecode.message();

            std::this_thread::sleep_for(std::chrono::milliseconds(config.interval));
        }

        for (auto t : threads)
            t->join();
    }
    catch (const std::exception& e)
    {
        NLOG_ERR("Unhandled exception: ") << e.what();
    }
    NLOG_PRO("Download() finished, result: {1}") % error.message();

    return !error;
}

int RequestContent(
    const std::string& url,
    std::string& data,
    std::error_code& error)
{
    error.clear();
    try
    {
        auto session1 = MakeSession(url);
        session1->SetConnectTimeout(8000);
        auto response = session1->Get();

        if (response.status_code == 200)
            data = std::move(response.text);
        else
            HandleRequestError(response, {}, kFailed, error);

        return response.status_code;
    }
    catch (const std::exception& e)
    {
        NLOG_ERR("Unhandled exception: ") << e.what();
    }

    return -1;
}
