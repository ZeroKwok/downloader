// This file is part of the downloader library
//
// Copyright (c) 2018-2025, Zero <zero.kwok@foxmail.com>
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#ifndef downloader_h__
#define downloader_h__

#ifdef DOWNLOADER_SHARE_LIB
#    define DOWNLOADER_LIB __declspec(dllexport)
#else
#    ifdef DOWNLOADER_STATIC_LIB
#        define DOWNLOADER_LIB
#    else
#        define DOWNLOADER_LIB __declspec(dllimport)
#    endif // DOWNLOADER_STATIC_LIB
#endif // DOWNLOADER_SHARE_LIB


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
    int interval    = 1000 / 10;    //!< 状态汇报的间隔时长(毫秒), 单连接下载时失效
    int blockSize   = 1024 * 1024;  //!< 连接分块传输的大小, 单连接下载时失效
    int timeout     = 5000;         //!< 请求的超时时间

    //! 请求头
    std::map<std::string, std::string> header;
};

//! @brief 下载文件
//! @param url 文件url
//! @param filename 存储本地文件名.
//! @param callback 下载状态回调, 该回调返回false, 将终止加载过程并设置错误码为: kOperationInterrupted
//! @param config 下载策略
//! @param error 失败时将包含具体的错误原因(BaseError)
//! @return 成功返回true, 否则失败
DOWNLOADER_LIB bool DownloadFile(
    const std::string& url,
    const std::filesystem::path& filename,
    const std::function<bool(const download_status&)>& callback,
    const download_preference config,
    std::error_code& error);

//! @brief 请求内容
//! @param url
//! @param data 请求到的数据
//! @param error 失败时将包含具体的错误原因(BaseError)
//! @return HTTP response status codes
DOWNLOADER_LIB int RequestContent(
    const std::string& url,
    const std::map<std::string, std::string>& header,
    std::string& data,
    std::error_code& error);

//!
//! 文件属性
//!
struct file_attribute
{
    int64_t     contentLength = -1; //!< 文件长度(字节数), -1 未知文件长度
    std::string contentRange;       //!< 内容范围
    std::string acceptRanges;       //!< 可以接受的范围请求格式
    std::string header;             //!< http 响应头
};

//! @brief 请求文件属性
//! @param attribute 输出的文件属性.
//! @param url 文件 url
//! @param timeout 超时时间, 毫秒
//! @param error 失败时将包含具体的错误原因(BaseError)
//! @return 成功返回true, 否则失败
DOWNLOADER_LIB bool GetFileAttribute(file_attribute& attribute, const std::string& url, std::error_code& error);
DOWNLOADER_LIB bool GetFileAttribute(
    file_attribute& attribute, 
    const std::string& url, 
    const std::map<std::string, std::string>& header,
    int timeout, 
    std::error_code& error);

#endif // downloader_h__
