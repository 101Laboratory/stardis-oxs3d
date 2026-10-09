/* Copyright (C) 2013-2023, 2025 Vincent Forest (vaplv@free.fr)
 *
 * The RSys library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The RSys library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the RSys library. If not, see <http://www.gnu.org/licenses/>. */

#ifndef WINDOWS_PLATFORM_H
#define WINDOWS_PLATFORM_H

/* Windows平台专用结构体定义
 * 这些定义仅在Windows实现文件中可见，不暴露给公共API
 * 确保Windows实现文件之间结构体布局一致
 */

#define _WIN32_WINNT 0x0600 /* Windows Vista for CONDITION_VARIABLE, SRWLOCK */
#include <windows.h>

/* 互斥量结构体 - 使用CRITICAL_SECTION实现 */
struct mutex {
    CRITICAL_SECTION cs;
};

/* 读写锁结构体 - 使用SRWLOCK实现（Windows Vista+） */
struct mutex_rw {
    SRWLOCK srwlock;
};

/* 条件变量结构体 - 使用CONDITION_VARIABLE实现（Windows Vista+） */
struct cond {
    CONDITION_VARIABLE cv;
};

#endif /* WINDOWS_PLATFORM_H */