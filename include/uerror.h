// This file is part of the uerror.h
//
// Copyright (c) 2018-2025, Zero <zero.kwok@foxmail.com>
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#ifndef uerror_h__
#define uerror_h__

#include <filesystem>
#include <system_error>
#include <string/string_util.h>

namespace util {

    enum Error : int
    {
        kSucceed                             = 0x00, //!< 成功
        kUnknownError                        = 0x01, //!< 未知错误
        kInvalidParam                        = 0x02, //!< 参数错误
        kRuntimeError                        = 0x03, //!< 运行时错误
        kOutOfMemory                         = 0x04, //!< 内存不足
        kPermissionDenied                    = 0x05, //!< 权限不足

        kOperationFailed                     = 0x2a, //!< 操作失败
        kOperationInterrupted                = 0x2b, //!< 操作中断, 用户取消

        kFilesystemError                     = 0x51, //!< 文件系统错误
        kFilesystemIOError                   = 0x52, //!< 文件系统IO错误
        kFilesystemNotSupportLargeFiles      = 0x53, //!< 文件系统不支持大文件, Windows下通常是FAT32/FAT16
        kFilesystemUnavailable               = 0x54, //!< 文件系统不可用, 如U盘突然被拔出
        kFilesystemNoSpace                   = 0x55, //!< 本地磁盘空间不足
        kFilesystemNetworkError              = 0x56, //!< 文件系统网络错误

        kFileNotFound                        = 0x61, //!< 文件文件未找到(包括 404)
        kFileNotWritable                     = 0x62, //!< 本地文件不可写
        kFilePathTooLong                     = 0x63, //!< 本地文件路径太长
        kFileWasUsedByOtherProcesses         = 0x64, //!< 文件被其他进程使用

        kNetworkError                        = 0x81, //!< 网络错误
        kServerError                         = 0xa1, //!< 服务器错误
    };

    //!
    //! ErrorCategory
    //!
    class ErrorCategory : public std::error_category {
        public:
        static ErrorCategory& Instance() {
            static ErrorCategory _imp;
            return _imp;
        }

        virtual const char* name() const noexcept { return "Error"; }
        virtual std::string message(int ev) const {
            if (ev == kSucceed)
                return "Succeed";
            return util::sformat("Error: 0x%08x", ev);
        }
    };

    //! @brief 通过错误码创建标准错误码对象.
    //! @param ecode 由BaseError定义的错误码之一.
    //! @return 返回cpp错误码对象.
    inline std::error_code MakeError(int ecode) {
        return std::error_code(
            static_cast<int>(ecode), ErrorCategory::Instance());
    }

    //! @brief 通过本地系统错误码创建标准错误码对象.
    //! @param ecode 本地系统错误码.
    //! @param filename 如果是文件相关的错误, 则可以携带文件名, 这可以进一步探测错误原因.
    //! @param defaultCode 默认错误码.
    //! @return 返回cpp错误码对象.
    std::error_code MakeErrorFromNative(
        const int ecode,
        const std::filesystem::path& filename = {},
        const Error defaultCode = kRuntimeError);

} // namespace util

#endif // uerror_h__
