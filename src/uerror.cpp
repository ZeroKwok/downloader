// This file is part of the uerror.h
//
// Copyright (c) 2018-2025, Zero <zero.kwok@foxmail.com>
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#ifndef uerror_cpp__
#define uerror_cpp__

#include "uerror.h"
#include <string/string_util.h>
#include <filesystem/path_util.h>
#include <platform/platform_util.h>

// SDK v7.1A 中没有定义这个错误码
#ifndef ERROR_NO_SUCH_DEVICE
#   define ERROR_NO_SUCH_DEVICE 433L
#endif

namespace util {

std::string ErrorCategory::message(int ev) const
{
    switch (ev) {
    case kSucceed:                         return "Succeed";
    case kUnknownError:                    return "Unknown error";
    case kInvalidParam:                    return "Invalid parameter";
    case kRuntimeError:                    return "Runtime error";
    case kOutOfMemory:                     return "Out of memory";
    case kPermissionDenied:                return "Permission denied";

    case kOperationFailed:                 return "Operation failed";
    case kOperationInterrupted:            return "Operation interrupted (canceled by user)";

    case kFilesystemError:                 return "Filesystem error";
    case kFilesystemIOError:               return "Filesystem I/O error";
    case kFilesystemNotSupportLargeFiles:  return "Filesystem does not support large files (e.g. FAT32/FAT16 on Windows)";
    case kFilesystemUnavailable:           return "Filesystem unavailable (e.g. device unplugged)";
    case kFilesystemNoSpace:               return "No space left on device";
    case kFilesystemNetworkError:          return "Filesystem network error";

    case kFileNotFound:                    return "File not found (including 404)";
    case kFileNotWritable:                 return "File not writable";
    case kFilePathTooLong:                 return "File path too long";
    case kFileWasUsedByOtherProcesses:     return "File is being used by another process";

    case kNetworkError:                    return "Network error";
    case kServerError:                     return "Server error";

    default:
        return util::sformat("Unknown error: 0x%08x", ev);
    }
}

std::error_code MakeErrorFromNative(
    const int ecode,
    const std::filesystem::path& filename,
    const Error defaultCode)
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
                    std::error_code err;
                    fs::space_info space = fs::space(fname, err);
                    if (!err && space.free > 0x200000) // 2MB
                        return MakeError(kFilesystemNotSupportLargeFiles);
                }
            }
            catch (...)
            {
            }
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

#endif // uerror_cpp__
