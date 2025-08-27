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
#include <fstream>
#include <algorithm>
#include <filesystem>

#include "config.h"
#include "uerror.h"
#include "range.hpp"
#include "common/scope.hpp"
#include "common/assert.hpp"
#include "common/bytedata.hpp"
#include "filesystem/path_util.h"

#include <boost/algorithm/string.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/serialization.hpp>

//
// 包含填充状态的文件区间
//
struct Range2 : public Range {
    int64_t position = 0; // 不包含右边端点
    enum {
        kUnfilled,
        kPending,
        kPartial,
        kFilled,
    } state = kUnfilled;
};

//
// 区间化文件的元数据结构，用于文件分块处理的状态序列化和追踪
//
struct RangeFileMeta {
    int64_t          _blockHint = 0;        // 区块大小暗示，用于区间分配的建议大小
    int64_t          _bytesTotal = -1;      // 文件总字节数，-1表示未初始化
    int64_t          _bytesProcessed = 0;   // 已成功处理的字节总数
    
    std::set<Range2> _allocateRanges;       // 已分配但未完成的区间集合，这些区间正在被处理但尚未完成
    std::set<Range2> _finishedRanges;       // 已成功完成处理的区间集合，这些区间的数据处理已经完成
    std::set<Range2> _availableRanges;      // 可用区间集合，表示尚未分配处理的文件区间
                                            // allocate() 操作从这里分配区间，分配后从这里移除

    // 跟踪并输出当前状态信息到日志
    void trace() {
        std::list<std::string> text;
        for (auto const& r : _finishedRanges)
            text.push_back(util::sformat("[%08" PRIx64 ", %08" PRIx64 "]", r.start, r.end));
        NLOG_PRO(" - Status: finished-ranges: {1}, available-ranges: {2}, available-ranges: {3}")
            % _finishedRanges.size()
            % _availableRanges.size()
            % _allocateRanges.size();
        NLOG_PRO(" - Finished: ") << boost::join(text, ", ");
        NLOG_PRO(" - BytesProcessed: ") << _bytesProcessed;
    }

    // 验证当前状态是否一致有效
    // 检查所有区间的总字节数是否等于文件总字节数
    bool valid() {
        auto size = 0LL;
        for (auto const& r : _finishedRanges)
            size += r.size();
        for (auto const& r : _availableRanges)
            size += r.size();
        for (auto const& r : _allocateRanges)
            size += r.size();
        return size == _bytesTotal; // 验证总和是否等于文件总大小
    }
};

namespace boost {
    namespace serialization {
        template<class Archive>
        void serialize(Archive& ar, Range2& d, const unsigned int version) {
            ar & d.start;
            ar & d.end;
            ar & d.position;
            ar & d.state;
        }

        template<class Archive>
        void serialize(Archive& ar, RangeFileMeta& d, const unsigned int version) {
            ar & d._blockHint;
            ar & d._bytesTotal;
            ar & d._bytesProcessed;
            ar & d._allocateRanges;
            ar & d._finishedRanges;
            ar & d._availableRanges;
        }
    } // namespace serialization
} // boost

// 区间化文件实现
// 
// 1. 分配未使用区间
//      需要防止重复分配, 需要记录正在使用的未决区间, 完毕(成功填充, 部分填充, 未填充)后移除
// 1. 区间填充时, 会将数据写入文件, 填充位置记录到区间状态中
// 1. 区间完毕后, 记录已经填充的区间, 部分填充的区间则需要缩小

class RangeFile : public RangeFileMeta
{
    std::filesystem::path _filename;
    util::ffile           _file;
    std::recursive_mutex  _mutex;
    std::mutex            _mutexFile;
    std::mutex            _mutexMeta;

public:
    RangeFile(int64_t size = -1, int sizeHint = 0x100000) {
        _blockHint  = sizeHint;
        _bytesTotal = size;
    }

    ~RangeFile() {
        if (opened())
            close(false, std::error_code{});
    }

    operator bool() const {
        return opened();
    }

    // 文件已经打开 或 已经分配了区域, 则不能再指派大小
    bool reserve(int64_t size = -1, int sizeHint = 0x100000) {
        if (opened() || 
            _finishedRanges.size() ||
            _allocateRanges.size()) {
            return false;
        }
        _blockHint = sizeHint;
        _bytesTotal = size;
        return true;
    }

    // 分配区域并保证不相交
    bool allocate(Range2& range)
    {
        if (_bytesTotal <= 0)
            return {};

        std::lock_guard<std::recursive_mutex> locker(_mutex);
        if (_availableRanges.empty() && _allocateRanges.empty() && _finishedRanges.empty())
        {
            Range2 last = { 0, 0, 0, Range2::kUnfilled };
            do
            {
                last.start = last.end > 0 ? last.end + 1 : 0;
                last.end = std::min(last.start + _blockHint - 1, _bytesTotal - 1);
                _availableRanges.insert(last);

                util_assert(last.size() <= _blockHint);
            } 
            while (last.end < (_bytesTotal - 1));

            NLOG_PRO("allocate() calculate the available range of the file: ");
            NLOG_PRO(" - block-hint: ") << _blockHint;
            NLOG_PRO(" - bytes-total: ") << _bytesTotal;
            NLOG_PRO(" - available-ranges: ") << _availableRanges.size();
        }

        if (_availableRanges.size() > 0)
        {
            range = *_availableRanges.begin();
            range.state = Range2::kPending;
            range.position = range.start;
            util_assert(range.size() <= _blockHint);

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
            if (filename.has_parent_path())
            {
                std::error_code ecode;
                if (!std::filesystem::exists(filename.parent_path(), ecode))
                    std::filesystem::create_directories(filename.parent_path(), ecode);
                if (ecode)
                    return !(error = util::MakeErrorFromNative(ecode.value(), filename, util::kFilesystemError));
            }

            auto temp = std::filesystem::path(filename) += L".temp";;
            auto meta = std::filesystem::path(filename) += L".meta";
            auto file = util::file_open(temp, O_CREAT | O_RDWR);
            auto size = util::file_size(file);

            // 文件总大小有效, 则文件被设置为同等大小.
            // 文件总大小无效, 则文件被截断为0.
            if (size != _bytesTotal)
            {
                util::file_seek(file, std::max<int64_t>(_bytesTotal, 0), 0);

                // If the function succeeds, the return value is nonzero.
                // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setendoffile
                //
                if (SetEndOfFile((HANDLE)file.native_id()) == 0)
                    throw util::ferror(::GetLastError(), "SetEndOfFile() failed");

                util::file_seek(file, 0, 0);

                // 调整了文件大小, 则尝试删除可能的元数据文件, 并忽略错误
                util::ferror ferr;
                if (util::file_exist(meta, ferr))
                    util::file_remove(meta, ferr);
            }
            else if (_bytesTotal > 0)
            {
                if (util::file_exist(meta))
                {
                    // 大小有效 且 文件大小没有被调整, 尝试打开同步上一次的元数据
                    RangeFileMeta archive = {};
                    try
                    {
                        std::ifstream is(meta, std::ios::binary);
                        boost::archive::binary_iarchive ia(is);
                        ia >> archive;
                    }
                    catch (const std::exception& e)
                    {
                        NLOG_WAR("open({1}) failed to synchronize metadata, error: {2}")
                            % meta.wstring()
                            % e.what();
                        util::file_remove(meta);
                    }

                    if (archive._blockHint == _blockHint &&
                        archive._bytesTotal == _bytesTotal)
                    {
                        // 恢复状态的话, 先用简单方案处理: 暴力的丢弃所有正在处理的区间
                        for (auto r : archive._allocateRanges) {
                            archive._availableRanges.insert({ r.start, r.end });
                            archive._bytesProcessed -= (r.position - r.start);
                        }
                        archive._allocateRanges.clear();

                        NLOG_PRO("open() Restore the previous status:");
                        archive.trace();

                        if (archive.valid())
                        {
                            std::lock_guard<std::recursive_mutex> locker(_mutex);
                            _bytesProcessed = archive._bytesProcessed;
                            _finishedRanges = std::move(archive._finishedRanges);
                            _availableRanges = std::move(archive._availableRanges);
                        }
                        else {
                            NLOG_ERR("open() drop the invalid status");
                        }
                    }
                }
            }

            _file = file;
            _filename = filename;
        }
        catch (const util::ferror& ferr)
        {
            NLOG_ERR("open({1}) failed, error: {2}")
                % _filename.wstring()
                % ferr.message();
            return !(error = util::MakeErrorFromNative(ferr.code(), filename, util::kFilesystemError));
        }

        return !error;
    }

    bool opened() const {
        return _file;
    }

    bool close(bool finished, std::error_code& error)
    {
        error.clear();

        util_assert(_file);
        util_assert(_allocateRanges.empty());
        {
            std::lock_guard<std::mutex> locker(_mutexFile);
            _file.close();
        }

        if (finished)
        {
            if (_bytesTotal > 0 && !is_full()) {
                NLOG_WAR("close() debug the status:");
                NLOG_WAR(" - _bytesTotal: ") << _bytesTotal;
                NLOG_WAR(" - is_full: ") << (is_full() ? "true" : "false");
                trace();
                return !(error = util::MakeError(util::kRuntimeError));
            }

            auto file = _filename;
            try
            {
                file += ".temp";
                util::file_move(file, _filename);

                file =  _filename;
                file += ".meta";
                util::file_remove(file);
            }
            catch (const util::ferror& ferr)
            {
                NLOG_ERR("close({1}) failed, error: {2}")
                    % file.wstring()
                    % ferr.message();
                return !(error = util::MakeErrorFromNative(ferr.code(), file, util::kFilesystemError));
            }
        }

        {
            std::lock_guard<std::recursive_mutex> locker(_mutex);
            _allocateRanges.clear();
            _finishedRanges.clear();
            _availableRanges.clear();
        }
        _blockHint = 0x100000;
        _bytesTotal = -1;
        _bytesProcessed = 0;
        _filename.clear();

        return !error;
    }

    bool dump(std::error_code& error)
    {
        error.clear();
        try
        {
#if IS_DEBUG
            if (!RangeFileMeta::valid()) {
                NLOG_PRO("dump() invalid status:");
                trace();
            }
#endif

            RangeFileMeta archive = {};
            {
                std::lock_guard<std::recursive_mutex> locker(_mutex);
                archive = *static_cast<RangeFileMeta*>(this);
            }

            try
            {
                auto meta = std::filesystem::path(_filename) += L".meta";
                auto temp = std::filesystem::path(_filename) += L".meta.temp";
                std::lock_guard<std::mutex> locker(_mutexMeta);
                {
                    std::ofstream os(temp, std::ios::binary | std::ios::trunc);
                    boost::archive::binary_oarchive oa(os);
                    oa << archive;
                }
                util::file_remove(meta);
                util::file_move(temp, meta);
            }
            catch (const util::ferror& ferr)
            {
                NLOG_ERR("dump() failed, file: {1}.meta, error: {2}")
                    % _filename.wstring()
                    % ferr.message();
                return !(error = util::MakeErrorFromNative(ferr.code(), _filename, util::kFilesystemError));
            }
        }
        catch (const std::exception& e)
        {
            NLOG_ERR("dump() failed to synchronize metadata, file: {1}.meta, error: {2}") 
                % _filename.wstring()
                % e.what();
            error = util::MakeError(util::kRuntimeError);
        }

        return !error;
    }

    bool fill(const std::string_view& bytes, int64_t size, std::error_code& error)
    {
        error.clear();
        try
        {
            if (size <= 0) // 没有可填充的数据
                return true;

            {
                std::lock_guard<std::mutex> locker(_mutexFile);
                util::file_write(_file, bytes.data(), size);
            }
            _bytesProcessed += size;
        }
        catch (const util::ferror& ferr)
        {
            NLOG_ERR("fill() failed, size: {1}, error: {2}")
                % size
                % ferr.message();
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
                NLOG_ERR("fill() failed, range: {1}, size: {2}, error: {3}")
                    % util::sformat("[%08" PRIx64 ", %08" PRIx64 ": %d / %06" PRIx64 "]",
                        range.start, range.end, range.state, range.position)
                    % size
                    % ferr.message();
                return !(error = util::MakeErrorFromNative(ferr.code(), _filename, util::kFilesystemError));
            }
            _bytesProcessed += size;

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
        }
        catch (const std::exception& e)
        {
            NLOG_ERR("fill() failed, file: {1}, error: {2}")
                % _filename.wstring()
                % e.what();
            error = util::MakeError(util::kRuntimeError);
        }

        return !error;
    }

    bool is_full() const {
        std::lock_guard<std::recursive_mutex> locker(const_cast<std::recursive_mutex&>(_mutex));
        if (_finishedRanges.size() == 1)
            return *_finishedRanges.cbegin() == Range2{ 0, _bytesTotal - 1 };
        return false;
    }

    int64_t size() const {
        if (_bytesTotal > 0)
            return _bytesTotal;
        return 0;
    }

    int64_t processed() const {
        return _bytesProcessed;
    }
};

//
// 简易的单元测试
//
#if DEBUG || _DEBUG

#include <random>
#include <iostream>
#include "string/string_util.h"

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