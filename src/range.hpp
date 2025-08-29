// This file is part of the downloader library
//
// Copyright (c) 2018-2025, Zero <zero.kwok@foxmail.com>
// For the full copyright and license information, please view the LICENSE
// file that was distributed with this source code.

#ifndef range_h__
#define range_h__

#include "config.h"
#include "common/scope.hpp"
#include "common/assert.hpp"
#include <algorithm>

/// @brief 表示一个整数范围 [start, end]（包含两端点）
struct Range
{
    int64_t start = -1;  ///< 范围起始值（包含），-1 表示无效
    int64_t end   = -1;  ///< 范围结束值（包含），-1 表示无效

    /// @brief 检查范围是否有效
    /// @details
    /// 00-05, size 6
    /// 06-10, size 5
    /// 11-15, size 5
    /// ----------[    ]------------
    /// -[    ]---------------------
    /// ------[    ]----------------
    /// --------------[    ]--------
    /// ------------------[    ]----
    /// -------[           ]--------
    bool valid() const {
        return start >= 0 && start <= end;
    }

    /// @brief 转换为布尔值，等价于 valid()
    operator bool() const {
        return valid();
    }

    /// @brief 比较两个范围的起始值
    bool operator<(const Range& other) const {
        return start < other.start;
    }

    /// @brief 检查两个范围是否完全相同
    bool operator==(const Range& other) const {
        return start == other.start && end == other.end;
    }

    /// @brief 合并两个可合并的范围（相交或相邻）
    /// @note 若两个范围不可合并，返回无效范围
    /// @example 
    ///   [1, 5] + [3, 7] = [1, 7]   // 相交
    ///   [1, 5] + [6, 8] = [1, 8]   // 相邻
    ///   [1, 5] + [7, 9] = invalid  // 不相邻也不相交
    Range operator+(const Range& other) const {
        util_assert(valid() && other.valid());
        if (mergeable(other)) {
            if (intersected(other))
                return Range{ std::min(start, other.start), std::max(end, other.end) };
            return *this < other ? Range{ start, other.end } : Range{ other.start, end };
        }
        return {};
    }

    /// @brief 计算两个非重叠范围之间的间隔范围
    /// @note 若两个范围相交或相邻，返回无效范围（无间隔）
    /// @example 
    ///   [1, 5] - [7, 9] = [6, 6]     // 间隔为单个点
    ///   [1, 5] - [8, 10] = [6, 7]    // 间隔为范围
    ///   [1, 5] - [3, 7] = invalid    // 存在重叠
    Range operator-(const Range& other) const {
        util_assert(valid() && other.valid());
        if (!mergeable(other))
            return *this < other ? Range{ end + 1, other.start - 1 } : Range{ other.end + 1, start - 1 };
        return {};
    }

    /// @brief 检查两个范围是否相交（包含端点重叠）
    /// @note 完全包含或部分重叠均视为相交
    bool intersected(const Range& other) const {
        return !(end < other.start || start > other.end);
    }

    /// @brief 检查两个范围是否可合并（相交或相邻）
    /// @note 相邻指两个范围之间没有间隔（如 [1,5] 和 [6,10]）
    bool mergeable(const Range& other) const {
        if (!valid() || !other.valid())
            return false;
        if (intersected(other))
            return true;
        return std::abs(start - other.end) == 1 || std::abs(end - other.start) == 1;
    }

    /// @brief 获取范围大小（包含边端点，因此需要 +1）
    /// @example [1,5] 的大小为 5 (5-1+1)
    int64_t size() const {
        return valid() ? end - start + 1 : 0;
    }
};

//
// 简易的单元测试
//
#if DEBUG || _DEBUG
inline void UtilTestForClassRange()
{
    // 测试默认构造函数
    Range defaultRange;
    util_assert(defaultRange.start == -1);
    util_assert(defaultRange.end == -1);
    util_assert(!defaultRange.valid());
    util_assert(defaultRange.size() == 0);

    Range range0{ 0, 0};
    util_assert(range0.valid());
    util_assert(range0.start == 0);
    util_assert(range0.end == 0);
    util_assert(range0.size() == 1);

    // 测试带参数构造函数和valid()函数
    Range range1{ 1, 5 };
    util_assert(range1.start == 1);
    util_assert(range1.end == 5);
    util_assert(range1.valid());
    util_assert(range1.size() == 5);

    // 测试intersected()函数
    Range range2{ 3, 8 };
    Range range3{ 6, 10 };
    Range range4{ 10, 15 };

    util_assert(!range0.intersected(range1));
    util_assert(!range1.intersected(range0));
    util_assert(range1.intersected(range2));
    util_assert(range2.intersected(range1));
    util_assert(!range1.intersected(range3));
    util_assert(!range3.intersected(range1));
    util_assert(!range1.intersected(range4));
    util_assert(!range4.intersected(range1));

    // 测试mergeable()函数
    util_assert(range0.mergeable(range1));
    util_assert(range1.mergeable(range0));
    util_assert(range1.mergeable(range2));
    util_assert(range2.mergeable(range1));
    util_assert(range1.mergeable(range3));
    util_assert(range3.mergeable(range1));
    util_assert(range2.mergeable(range3));
    util_assert(range3.mergeable(range2));
    util_assert(!range1.mergeable(range4));
    util_assert(!range4.mergeable(range1));

    // 测试operator+函数
    Range mergedRange0 = range1 + range0;
    util_assert(mergedRange0.start == 0);
    util_assert(mergedRange0.end == 5);

    Range mergedRange1 = range1 + range2;
    util_assert(mergedRange1.start == 1);
    util_assert(mergedRange1.end == 8);

    Range mergedRange2 = range2 + range3;
    util_assert(mergedRange2.start == 3);
    util_assert(mergedRange2.end == 10);

    Range nonMergeableRange = range1 + range4;
    util_assert(!nonMergeableRange.valid());
    util_assert(!nonMergeableRange);

    // 测试operator-函数
    Range diffRange1 = range2 - range1;
    util_assert(!diffRange1.valid());
    util_assert(!diffRange1);

    Range diffRange2 = range1 - range3;
    util_assert(!diffRange2.valid());
    util_assert(!diffRange2);
    util_assert(diffRange2.start == -1);
    util_assert(diffRange2.end == -1);

    Range diffRange3 = range4 - range2;
    util_assert(diffRange3.start == 9);
    util_assert(diffRange3.end == 9);
    util_assert(diffRange3.size() == 1);

    Range nonDiffRange = range1 - range2;
    util_assert(!nonDiffRange.valid());
    util_assert(!nonDiffRange);

    // 测试size()函数
    util_assert(range1.size() == 5);
    util_assert(range2.size() == 6);
    util_assert(range3.size() == 5);
    util_assert(range4.size() == 6);
}
#endif

#endif // range_h__
