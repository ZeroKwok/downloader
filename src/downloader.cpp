// This file is part of the downloader library
//
// Copyright (c) 2018-2025, Zero <zero.kwok@foxmail.com>
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

namespace chr = std::chrono;

static inline std::shared_ptr<cpr::Session> MakeSession(
    const cpr::Url& url,
    std::map<std::string, std::string> header)
{
    auto session = std::make_shared<cpr::Session>();
    session->SetUrl(url);
    session->SetOption(cpr::Redirect{});
    session->SetOption(cpr::VerifySsl{ false });

    // session->SetTimeout(8000);                 // 总时间一旦超过, 就断开连接, 不论连接是否正常。
    session->SetConnectTimeout(3000);             // 建立连接的时间一旦超过, 就断开连接
    session->SetLowSpeed(cpr::LowSpeed{           // 速度小于1kb/s，且持续超过8秒，则断开连接。
        1024, 
        std::chrono::seconds(8)}); 
    session->SetHeader(cpr::Header
        {
            {"Connection", "keep-alive"}
        });
    for (auto& pair : header)
        session->UpdateHeader({ pair });

    return session;
}

static inline size_t WriteHeadCallback(
    char* buffer, 
    size_t size,
    size_t nitems, 
    file_attribute* attribute) 
{
    size *= nitems;
    if (strncmp(buffer, "Accept-Ranges:", 14) == 0)
        attribute->acceptRanges = boost::algorithm::trim_copy(std::string(buffer + 14, size - 14));
    if (strncmp(buffer, "Content-Range:", 14) == 0)
        attribute->contentRange = boost::algorithm::trim_copy(std::string(buffer + 14, size - 14));
    attribute->header.append(buffer, size);
    return size;
}

static inline size_t DiscardBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    // 中断下载
    return 0;
}

bool GetFileAttribute(file_attribute& attribute, const std::string& url, std::error_code& error)
{
    return GetFileAttribute(attribute, url, {}, 3000, error);
}

bool GetFileAttribute(
    file_attribute& attribute,
    const std::string& url,
    const std::map<std::string, std::string>& header,
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
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout);

        // SSL 验证
        cpr::VerifySsl verify{ false };
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);

        // 跟随跳转
        cpr::Redirect redirect{};
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, redirect.follow ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, redirect.maximum);
        curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, redirect.cont_send_cred ? 1L : 0L);

        // post redirect flag
        long mask = 0;
        if (cpr::any(redirect.post_flags & cpr::PostRedirectFlags::POST_301))
            mask |= CURL_REDIR_POST_301;
        if (cpr::any(redirect.post_flags & cpr::PostRedirectFlags::POST_302))
            mask |= CURL_REDIR_POST_302;
        if (cpr::any(redirect.post_flags & cpr::PostRedirectFlags::POST_303))
            mask |= CURL_REDIR_POST_303;
        curl_easy_setopt(curl, CURLOPT_POSTREDIR, mask);

        // header setting
        curl_slist* chunk = nullptr;
        for (const auto& item : header) {
            std::string header_string = item.first;
            if (item.second.empty()) {
                header_string += ";";
            }
            else {
                header_string += ": " + item.second;
            }

            curl_slist* temp = curl_slist_append(chunk, header_string.c_str());
            if (temp) {
                chunk = temp;
            }
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

        // header handle
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteHeadCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &attribute);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DiscardBodyCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullptr);

        // range
        curl_easy_setopt(curl, CURLOPT_RANGE, "10-");

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(chunk);

        switch (res)
        {
        case CURLE_WRITE_ERROR: // 由 DiscardBodyCallback 返回0引起的中断
        case CURLE_OK: {
            long status_code{};
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &attribute.contentLength);

            if (206 == status_code)
            {
                attribute.contentLength += 10;
                if (attribute.acceptRanges.empty())
                    attribute.acceptRanges = "bytes";
            }

            if (200 == status_code) {
                attribute.acceptRanges.clear();
            }
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
    const std::error_code& fserr, 
    const std::atomic_int& flag,
    std::error_code& error)
{
    if (fserr)
    {
        // 因为文件操作错误终止, 归属为致命错误
        NLOG_ERR("Filesystem Error: {1}, status_code: {2}")
            % fserr.message()
            % response.status_code;
        error = fserr;
        return true;
    }

    switch (response.error.code)
    {
    case cpr::ErrorCode::ABORTED_BY_CALLBACK:
    case cpr::ErrorCode::AGAIN:
        util_assert(flag != kRunning);
        if (flag == kCancelled) // 只有取消才改写error
            error = util::MakeError(util::kOperationInterrupted);
        return true;

        // 网络错误 (可重试)
        // 这里仅设置错误码, 是否属于致命错误需要外部判断(因为网络错误可能因为重试变得好转)
    case cpr::ErrorCode::SEND_ERROR:
    case cpr::ErrorCode::RECV_ERROR:
    case cpr::ErrorCode::COULDNT_RESOLVE_HOST:
    case cpr::ErrorCode::COULDNT_CONNECT:
    case cpr::ErrorCode::PROXY:
    case cpr::ErrorCode::OPERATION_TIMEDOUT:
    case cpr::ErrorCode::SSL_CONNECT_ERROR:
        NLOG_ERR("Request Error: status_code: {1}, error_code: {2}, error_message: {3}")
            % response.status_code
            % int(response.error.code)
            % response.error.message;
        error = util::MakeError(util::kNetworkError);
        return false;

        // 未知错误 / 内部错误
    case cpr::ErrorCode::UNKNOWN_ERROR:
    case cpr::ErrorCode::GOT_NOTHING:
        NLOG_ERR("Request Error: status_code: {1}, error_code: {2}, error_message: {3}")
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
            NLOG_ERR("Request Error: status_code: {1}, error_code: {2}, error_message: {3}")
                % response.status_code
                % int(response.error.code)
                % response.error.message;
            error = util::MakeError(util::kOperationFailed);
        }
        return false;

    default:
        NLOG_ERR("Request Error: status_code: {1}, error_code: {2}, error_message: {3}")
            % response.status_code
            % int(response.error.code)
            % response.error.message;
        error = util::MakeError(util::kRuntimeError);
    }

    return false;
}

struct DownloadFileContext {
  std::string url;
  std::filesystem::path filename;
  std::atomic_int flag;
  std::function<bool(const download_status &)> callback;
  download_preference config;
  RangeFile rf;
  file_attribute attribute;
  std::chrono::steady_clock::time_point start;
};

inline int measure(const std::chrono::steady_clock::time_point& start) {
    return (int)chr::duration_cast<chr::milliseconds>(
        chr::steady_clock::now() - start).count();
};

bool DownloadFileBySingle(DownloadFileContext& context, std::error_code& error)
{
    NLOG_PRO("Direct download ...");

    // 未知大小 or 长度太短 or 不支持范围请求, 只能单点下载
    context.rf.reserve(context.attribute.contentLength);
    if (!context.rf.open(context.filename, error)) {
        NLOG_ERR("rf.open({1}) failed, error: {2}")
        % context.filename.wstring()
        % error.message();
        return !error;
    }
    
    auto session = MakeSession(context.url, context.config.header);
    session->SetProgressCallback(cpr::ProgressCallback(
        [&](cpr::cpr_off_t downloadTotal,
            cpr::cpr_off_t downloadNow,
            cpr::cpr_off_t, cpr::cpr_off_t, intptr_t) -> bool
        {
            // downloadTotal 很可能为0
            if (context.callback && !context.callback({ downloadTotal, downloadNow })) {
                context.flag = kCancelled;
                return false;
            }
            return true;
        }));

    cpr::Response response;
    do 
    {
        std::error_code ecode;
        response = session->Download(cpr::WriteCallback{
            [&](const std::string_view& data, intptr_t userdata) -> bool {
                return context.rf.fill(data, data.size(), ecode);
            }});
        if (HandleRequestError(response, ecode, context.flag, error))
            return !error; // 致命错误, 直接终止

        if (error.value() == util::kNetworkError)
        {
            if (measure(context.start) < context.config.timeout)
            {
                NLOG_PRO("keep trying...");
                continue;
            }
        }
        break;
    } 
    while (1);

    if (error)
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

bool DownloadFileByMulti(DownloadFileContext& context, std::error_code& error)
{
    NLOG_PRO("Multipoint download ...");

    context.rf.reserve(context.attribute.contentLength, context.config.blockSize);
    if(!context.rf.open(context.filename, error)) {
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
        int workerId = 0;
        std::error_code error;
    };

    auto worker = [&](State& state)
    {
        state.flag = State::kThreadRunning;
        NLOG_APP("Worker start: {1}") % std::this_thread::get_id();
        util_scope_exit = [&] {
            state.flag = state.error ? State::kThreadInterrupted 
                                     : State::kThreadFinished;
            NLOG_APP("Worker finished: {1}, flag: {2}, result: {3}")
                % std::this_thread::get_id()
                % context.flag.load()
                % state.error.message();
        };

        try 
        {
            auto session = MakeSession(context.url, context.config.header);

            Range2 range;
            while (context.flag == kRunning && context.rf.allocate(range))
            {
                util_scope_exit = [&] {
                    context.rf.deallocate(range);
                };

                std::error_code ecode;
                session->SetOption(cpr::Range{ range.start, range.end });
                session->SetWriteCallback(cpr::WriteCallback{
                    [&](const std::string_view& data, intptr_t userdata) -> bool {
                        return context.rf.fill(range, data, data.size(), ecode);
                    } });
                auto response = session->Get();
                util_assert(response.status_code == 206);

                if (HandleRequestError(response, ecode, context.flag, state.error)) {
                    NLOG_ERR("HandleRequestError() Fatal error, abort({1})") % state.error;
                    return;
                }
            }
            return;
        }
        catch (const std::exception& e) {
            NLOG_ERR("Unhandled exception: ") << e.what();
        }
        catch (...) {
            NLOG_ERR("Unhandled exception");
        }
        state.error = util::MakeError(util::kRuntimeError);
    };

    std::vector<State> states(context.config.connections);
    std::vector<std::shared_ptr<std::thread>> threads;
    for (int i = 0; i < context.config.connections; ++i) {
        states[i].workerId = i;
        threads.push_back(std::make_shared<std::thread>(worker, std::ref(states[i])));
    }


    auto lastIndex = 0;
    auto lastDump = chr::steady_clock::now();
    while (context.flag.load() == kRunning && !context.rf.is_full())
    {
        // 在下载阶段的尾声, 部分线程开始陆续退出, 此时不会设置错误
        // 因此, 在检测是否发生错误时, 要排除正常结束的线程.
        if (states[lastIndex].flag == State::kThreadFinished) {
            if (++lastIndex >= states.size())
                break;
        }

        auto elapse = measure(context.start);
        if (elapse > context.config.timeout)
        {
            if (states[lastIndex].error) // 存在错误
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
                        % context.url
                        % context.filename.wstring()
                        % item.first
                        % item.second;

                    context.flag = kFailed;
                    error = util::MakeError(item.first);
                    break;
                }
            }
        }

        if (context.callback)
        {
            if (!context.callback({ context.attribute.contentLength, context.rf.processed() }))
            {
                NLOG_WAR("callback() instructing to terminate a task...");

                context.flag  = kCancelled;
                error = util::MakeError(util::kOperationInterrupted);
                break;
            }
        }

        // 限制存储下载状态的频率
        if (measure(lastDump) >= 5000)
        {
            std::error_code ecode;
            if (!context.rf.dump(ecode))
                NLOG_WAR("RangeFile::dump() failed, error: ") << ecode.message();
            lastDump = chr::steady_clock::now();
        }

        std::this_thread::sleep_for(chr::milliseconds(context.config.interval));
    }

    for (auto t : threads)
        t->join();

    return !error;
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
        DownloadFileContext context{ 
            url, filename, kRunning, callback, config, 
            {}, {}, chr::steady_clock::now() };

        NLOG_PRO("DownloadFile() ...");
        NLOG_PRO(" - URL : ") << url;
        NLOG_PRO(" - File: ") << filename;
        NLOG_PRO(" - TimeOut(MS): ") << config.timeout;
        NLOG_PRO(" - Connections: ") << config.connections;
        NLOG_PRO(" - BlockSize: ") << config.blockSize;
        NLOG_PRO(" - Interval: ") << config.interval;

        if (context.config.connections > 1) //  单点下载不用探测文件长度
        {
            int timeout = context.config.timeout;
            do 
            {
                // 当 SSL/TLS 握手失败时, 将突破 CONNECTTIMEOUT 超时限制, 因此这里需要加入重试机制
                error.clear();
                if (!GetFileAttribute(context.attribute, context.url, context.config.header, timeout, error))
                {
                    if (error.value() == util::kNetworkError)
                    {
                        if (measure(context.start) < context.config.timeout)
                        {
                            NLOG_PRO("keep trying ...");
                            continue;
                        }
                    }
                }
                break;
            }
            while (1);

            if (error) // 重试后仍不成功
            {
                NLOG_ERR("DownloadFile() failed, error: ") << error.message();
                return !error;
            }

            NLOG_PRO("GetFileAttribute() -> {1}\r\n{2}")
                % context.attribute.contentLength
                % context.attribute.header;
        }

        util::ferror ferr;
        if (util::file_exist(filename, ferr))
            util::file_remove(filename, ferr);
        if (ferr) {
            NLOG_ERR("util::file_*() failed, error: ") << ferr.message();
            return !(error = util::MakeErrorFromNative(ferr.code(), filename, util::kFilesystemError));
        }

        util_scope_exit = [&] {
            auto finished = !error;
            std::error_code ecode;
            if (context.rf && !context.rf.close(finished, ecode)) {
                error = error ? error : ecode; // 若关闭前有错误, 则不改变之前的错误
                NLOG_ERR("RangeFile::close({1}) failed, error: {2}")
                    % (finished ? "true" : "false")
                    % ecode.message();
            }
        };

        auto is_small = context.attribute.contentLength > 0 && context.attribute.contentLength < 10 * 1024 * 1024;
        if (context.attribute.contentLength == -1 || 
            context.attribute.contentLength <= context.config.blockSize ||
            context.attribute.acceptRanges.empty() ||
            is_small)
        {
            return DownloadFileBySingle(context, error);
        }

       return DownloadFileByMulti(context, error);
    }
    catch (const std::exception& e)
    {
        NLOG_ERR("Unhandled exception: ") << e.what();
        error = util::MakeError(util::kRuntimeError);
    }
    NLOG_PRO("Download() finished, result: {1}") % error.message();

    return !error;
}

int RequestContent(
    const std::string& url,
    std::map<std::string, std::string> header,
    std::string& data,
    std::error_code& error)
{
    error.clear();
    try
    {
        auto session1 = MakeSession(url, header);
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
