// This file is part of the downloader library
//
// Copyright (c) 2018-2023, zero.kwok@foxmail.com
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#ifndef downloader_h__
#define downloader_h__

#include <string>
#include <functional>
#include <filesystem>
#include <system_error>

//!
//! 下载状态
//!
struct download_status
{
    int64_t totalBytes;             //!< 总字节数
    int64_t processedBytes;         //!< 已处理的字节数
};

//!
//! 下载偏好
//!
struct download_preference
{
    int connections = 4;            //!< 下载连接数
    int blockSize   = 1024 * 1024;  //!< 连接分块传输的大小
    int interval    = 1000 / 10;    //!< 状态汇报的间隔时长(毫秒)
};

//! @brief 下载文件
//! @param url 文件url
//! @param filename 存储本地文件名.
//! @param callback 下载状态回调, 该回调返回false, 将终止加载过程并设置错误码为: kOperationInterrupted
//! @param config 下载策略
//! @param error 失败时将包含具体的错误原因(BaseError)
//! @return 成功返回true, 否则失败
bool DownloadFile(
    const std::string& url,
    const std::filesystem::path& filename,
    const std::function<bool(const download_status&)>& callback,
    const download_preference config,
    std::error_code& error);

//! @brief 请求文件长度
//! @param length 输出的文件长度(字节数), -1 未知文件长度.
//! @param url 文件 url
//! @param error 失败时将包含具体的错误原因(BaseError)
//! @return 成功返回true, 否则失败
bool GetFileLength(int64_t& length, const std::string& url, std::error_code& error);

#endif // downloader_h__
