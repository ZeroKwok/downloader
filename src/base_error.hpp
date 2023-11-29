// This file is part of the error.hpp library
//
// Copyright (c) 2018-2023, zero.kwok@foxmail.com
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#ifndef base_error_h__
#define base_error_h__

#include <system_error>
#include <string/string_util.h>
#include <filesystem/path_util.h>
#include "platform/platform_util.h"

namespace util {

    enum BaseError : int
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

        kFileNotFound                        = 0x61, //!< 文件系统文件未找到
        kFileNotWritable                     = 0x62, //!< 本地文件不可写
        kFilePathTooLong                     = 0x63, //!< 本地文件路径太长
        kFileWasUsedByOtherProcesses         = 0x64, //!< 文件被其他进程使用

        kNetworkError                        = 0x81, //!< 网络错误
        kServerError                         = 0xa1, //!< 服务器错误
    };

    //!
    //! BaseErrorCategory
    //!
    class BaseErrorCategory : public std::error_category {
        public:
        static BaseErrorCategory& Instance() {
            static BaseErrorCategory _imp;
            return _imp;
        }

        virtual const char* name() const noexcept { return "BaseError"; }
        virtual std::string message(int ev) const {
            if (ev == kSucceed)
                return "Succeed";
            return util::sformat("ERROR: 0x%08x", ev);
        }
    };

    //! @brief 通过错误码创建标准错误码对象.
    //! @param ecode 由BaseError定义的错误码之一.
    //! @return 返回cpp错误码对象.
    inline std::error_code MakeError(int ecode) {
        return std::error_code(
            static_cast<int>(ecode),
            BaseErrorCategory::Instance());
    }

    //! @brief 通过本地系统错误码创建标准错误码对象.
    //! @param ecode 本地系统错误码.
    //! @param filename 如果是文件相关的错误, 则可以携带文件名, 这可以进一步探测错误原因.
    //! @param defaultCode 默认错误码.
    //! @return 返回cpp错误码对象.
    inline std::error_code MakeErrorFromNative(
        const int ecode,
        const std::filesystem::path& filename = {},
        const BaseError defaultCode = kRuntimeError)
    {
        switch (ecode)
        {
        case ERROR_DISK_FULL:       // 磁盘空间不足或不支持大文件
        {
            if (!filename.empty())  // 若文件名不为空, 则探测下是否是不支持大文件
            {
                try
                {
                    auto fstype = util::path_filesystem(filename);
                    if (fstype == util::FAT16 || fstype == util::FAT32)
                    {
                        namespace fs = std::filesystem;
                        auto fname = filename;
                        if (!fs::is_directory(fname))
                            fname = fname.parent_path().lexically_normal();
                        fs::space_info space = fs::space(fname);
                        if (!ecode && space.free > 0x200000) // 2MB
                            return MakeError(kFilesystemNotSupportLargeFiles);
                    }
                }
                catch(...)
                {}
            }

            return MakeError(kFilesystemNoSpace);
        }

        case ERROR_ACCESS_DENIED:
            return MakeError(kFileNotWritable);

        case ERROR_PATH_NOT_FOUND:
        case ERROR_FILE_NOT_FOUND:
            return MakeError(kFileNotFound);

        case ERROR_WRONG_DISK:
        case ERROR_FILE_INVALID:    // 文件所在的卷已被外部更改，因此打开的文件不再有效。
        case ERROR_NO_SUCH_DEVICE:  // 没有这样的设备, U 盘突然被拔出
            return MakeError(kFilesystemUnavailable);

        case ERROR_INVALID_NAME:    // 无效文件名, 语法无效或太长, 这里排除bug那么只剩下路径过长
            return MakeError(kFilePathTooLong);

        default:
            if (util::win::is_network_error(ecode))
            {
                if (!filename.empty())
                    return MakeError(kFilesystemNetworkError);
                else
                    return MakeError(kNetworkError);
            }
            return MakeError(kFilesystemError);
        }

        return MakeError(defaultCode);
    }

} // namespace util

#endif // base_error_h__
