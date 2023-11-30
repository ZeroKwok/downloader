// This file is part of the downloader library
//
// Copyright (c) 2018-2023, zero.kwok@foxmail.com
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#ifndef range_file_h__
#define range_file_h__

#include <set>
#include <mutex>
#include <memory>
#include <algorithm>
#include <filesystem>

#include "config.h"
#include "range.hpp"
#include "base_error.hpp"
#include "common/scope.hpp"
#include "common/assert.hpp"
#include "filesystem/path_util.h"

struct Range2 : public Range {
    int64_t position = 0; // 不包含右边端点
    enum {
        kUnfilled,
        kPending,
        kPartial,
        kFilled,
    } state = kUnfilled;
};

// 1. 分配未使用区间
//      需要防止重复分配, 需要记录正在使用的未决区间, 完毕(成功填充, 部分填充, 未填充)后移除
// 1. 区间填充时, 会将数据写入文件, 填充位置记录到区间状态中
// 1. 区间完毕后, 记录已经填充的区间, 部分填充的区间则需要缩小

class RangeFile
{
    int64_t               _sizeHint = 0;
    int64_t               _sizeTotal = -1;
    std::set<Range2>      _allocateRanges;
    std::set<Range2>      _finishedRanges;
    std::set<Range2>      _availableRanges;
    std::filesystem::path _filename;
    util::ffile           _file;
    std::recursive_mutex  _mutex;
    std::mutex            _mutexFile;

public:
    RangeFile(int64_t size = -1, int sizeHint = 0x100000)
        : _sizeHint(sizeHint)
        , _sizeTotal(size)
    {}

    // 分配区域并保证不相交
    bool allocate(Range2& range)
    {
        if (_sizeTotal <= 0)
            return {};

        std::lock_guard<std::recursive_mutex> locker(_mutex);
        if (_availableRanges.empty() && 
            _allocateRanges.empty() && 
            _finishedRanges.empty())
        {
            Range2 last = { 0, 0, 0, Range2::kUnfilled };
            do
            {
                last.start = last.end > 0 ? last.end + 1 : 0;
                last.end = std::min(last.start + _sizeHint - 1, _sizeTotal - 1);
                _availableRanges.insert(last);

                util_assert(last.size() <= _sizeHint);
            } 
            while (last.end < (_sizeTotal - 1));
        }

        if (_availableRanges.size() > 0)
        {
            range = *_availableRanges.begin();
            range.state = Range2::kPending;
            range.position = range.start;
            util_assert(range.size() <= _sizeHint);

            _allocateRanges.insert(range);
            _availableRanges.erase(_availableRanges.begin());
            return true;
        }

        return false;
    }

    bool deallocate(Range2& range)
    {
        util_assert(range.valid());
        util_assert(range.state != Range2::kUnfilled);

        auto mergeRanges = [](auto& ranges) {
            std::remove_reference<decltype(ranges)>::type duplicate;
            Range2 last;
            for (const auto& r : ranges) {
                util::scope_exit _exit = [&] { last = r; };
                if (!last.valid())
                    continue;

                if (last.mergeable(r))
                {
                    _exit.reset();
                    last = {last + r, std::max(last.position, r.position), last.state};
                    continue;
                }
                duplicate.insert(last);
            }
            duplicate.insert(last);

            util_assert(!duplicate.empty());
            if (ranges.size() != duplicate.size())
                ranges = std::move(duplicate);
        };

        std::lock_guard<std::recursive_mutex> locker(_mutex);
        auto it = _allocateRanges.find(range);
        if (it == _allocateRanges.end())
            return false;

        _allocateRanges.erase(it);
        switch (range.state)
        {
        case Range2::kPending:
            _availableRanges.insert({ range.start, range.end });
            return true;

        case Range2::kFilled:
            util_assert(range.position == (range.end + 1));
            _finishedRanges.insert(range);
            mergeRanges(_finishedRanges);
            return true;

        case Range2::kPartial:
            util_assert(range.start <= range.position && range.position <= range.end);
            _finishedRanges.insert({ range.start, range.position - 1, range.position - 1, Range2::kFilled });
            _availableRanges.insert({ range.position, range.end });
            mergeRanges(_finishedRanges);
            return true;
        }

        return false;
    }

    bool open(const std::filesystem::path& filename, std::error_code& error)
    {
        error.clear();
        try
        {
            std::error_code ecode;
            if (!std::filesystem::exists(filename.parent_path(), ecode))
                std::filesystem::create_directories(filename.parent_path(), ecode);
            if (ecode)
                return !(error = util::MakeErrorFromNative(ecode.value(), filename, util::kFilesystemError));

            auto file = util::file_open(filename, O_CREAT | O_RDWR);
            auto size = util::file_size(file);

            if (size != _sizeTotal && _sizeTotal > 0)
            {
                util::file_seek(file, _sizeTotal, 0);

                // If the function succeeds, the return value is nonzero.
                // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setendoffile
                //
                if (SetEndOfFile((HANDLE)file.native_id()) == 0)
                    throw util::ferror(::GetLastError(), "SetEndOfFile() failed");

                util::file_seek(file, 0, 0);
            }

            _file = file;
            _filename = filename;

            // TODO
            // 读取mate文件, 恢复断点
        }
        catch (const util::ferror& ferr)
        {
            return !(error = util::MakeErrorFromNative(ferr.code(), filename, util::kFilesystemError));
        }

        return !error;
    }

    void close() 
    {
        util_assert(_file);
        util_assert(_allocateRanges.empty());

        std::lock_guard<std::mutex> locker(_mutexFile);
        {
            _file.close();
            _filename.clear();
        }
    }

    bool fill(const std::string_view& bytes, int64_t size, std::error_code& error)
    {
        error.clear();
        try
        {
            if (size <= 0) // 没有可填充的数据
                return true;

            std::lock_guard<std::mutex> locker(_mutexFile);
            util::file_write(_file, bytes.data(), size);
        }
        catch (const util::ferror& ferr)
        {
            error = util::MakeErrorFromNative(ferr.code(), _filename, util::kFilesystemError);
        }

        return !error;
    }

    bool fill(Range2& range, 
        const std::string_view& bytes, int64_t size,
        std::error_code& error)
    {
        error.clear();
        try
        {
            if (!range.valid() || range.state == Range2::kFilled || range.state == Range2::kUnfilled)
                return !(error = util::MakeError(util::kRuntimeError));
            if (size <= 0) // 没有可填充的数据
                return true;
            util_assert(range.position >= range.start);

            try
            {
                std::lock_guard<std::mutex> locker(_mutexFile);
                util::file_seek(_file, range.position, 0);
                util::file_write(_file, bytes.data(), size);
            }
            catch (const util::ferror& ferr)
            {
                return !(error = util::MakeErrorFromNative(ferr.code(), _filename, util::kFilesystemError));
            }

            // position 是下一个要填充的元素
            range.position = range.position + size;
            if (range.position == (range.end + 1))
                range.state = Range2::kFilled;
            else
                range.state = Range2::kPartial;

            std::lock_guard<std::recursive_mutex> locker(_mutex);
            auto it = _allocateRanges.find(range);
            if (it != _allocateRanges.end()) {
                const_cast<Range2&>(*it).state = range.state;
                const_cast<Range2&>(*it).position = range.position;
            }

            // TODO
            // 元数据同步至磁盘
        }
        catch (const util::ferror& ferr)
        {
            error = util::MakeErrorFromNative(ferr.code(), _filename, util::kFilesystemError);
        }

        return !error;
    }

    bool is_full() const {
        std::lock_guard<std::recursive_mutex> locker(const_cast<std::recursive_mutex&>(_mutex));
        if (_finishedRanges.size() == 1)
            return *_finishedRanges.cbegin() == Range2{ 0, _sizeTotal - 1 };
        return false;
    }

    int64_t size() const {
        if (_sizeTotal > 0)
            return _sizeTotal;
        return 0;
    }
};

#if DEBUG || _DEBUG

#include <random>
#include <iostream>
#include "string/string_util.h"

// 重载 << 运算符  
inline std::ostream& operator<<(std::ostream& os, const Range2& br) {
    return os << (util::sformat("[%08" PRIx64 ", %08" PRIx64 ": %d / %06" PRIx64 "]", 
        br.start, br.end, br.state, br.position));
}

inline void UtilTestForClassRangeFile()
{
    if (0)
    {
        RangeFile rf(0x100000 * 10);
        std::list<Range2> ranges;

        Range2 range;
        while (rf.allocate(range)) {
            std::cout << range << std::endl;
            ranges.push_back(range);
        }

        for (auto& r : ranges) {
            rf.deallocate(r);
        }
    }

    if (0)
    {
        auto path = LR"(G:\collect\弥留之国的爱丽丝.Alice.in.Borderlands.S01E02.1080p.H265-官方中字.mp4)";
        auto file = util::file_open(path, O_RDONLY);

        std::error_code ecode;
        std::string buffer(1024 * 4, 0);

        RangeFile rf(util::file_size(file), 1024*4);
        rf.open(R"(J:\FoneToolTemp\alice.mp4)", ecode);

        Range2 range;
        while (rf.allocate(range)) {
            util_scope_exit = [&] { 
                rf.deallocate(range); 
            };
            std::cout << range << std::endl;
            
            util::file_seek(file, range.start, 0);
            util::file_read(file, &buffer[0], (int)range.size());

            rf.fill(range, buffer, range.size(), ecode);
        }
    }

    if (1)
    {
        auto path = LR"(G:\collect\弥留之国的爱丽丝.Alice.in.Borderlands.S01E02.1080p.H265-官方中字.mp4)";
        auto file = util::file_open(path, O_RDONLY);

        std::mutex mutex;
        std::error_code ecode;

        RangeFile rf(util::file_size(file), 1024 * 4);
        rf.open(R"(J:\FoneToolTemp\alice.mp4)", ecode);

        auto worker = [=, &rf, &mutex, &file]
        {
            std::string buffer(1024 * 4, 0);
            std::random_device rd;
            std::mt19937 gen(rd());

            Range2 range;
            while (rf.allocate(range)) {
                util_scope_exit = [&] {
                    rf.deallocate(range);
                };

#if 0
                int size = range.size();
#else
                int size = std::uniform_int_distribution<>(0, range.size())(gen);
                if (size > range.size() / 2.0)
                    size = range.size();
#endif
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    //std::cout << std::this_thread::get_id() << " " << range << std::endl;

                    util::file_seek(file, range.position, 0);
                    util::file_read(file, &buffer[0], size);
                }

                std::error_code ecode;
                rf.fill(range, buffer, size, ecode);
            }
        };

        std::vector<std::shared_ptr<std::thread>> threads;
        for (int i = 0; i < 8; ++i)
            threads.push_back(std::make_shared<std::thread>(worker));
        for (auto t : threads)
            t->join();

        util_assert(rf.is_full());
    }
}
#endif

#endif // range_file_h__