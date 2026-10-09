#!/bin/bash
# ====================================================================
# CMake 模块验证脚本
# Version: 1.1.0
# Date: 2026-01-21
# ====================================================================
# 用途：检查所有模块 CMakeLists.txt 是否符合最佳实践
# 基于：cmake_template_audit.md 中的反模式总结
# ====================================================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 计数器
TOTAL_ISSUES=0
TOTAL_WARNINGS=0
TOTAL_MODULES=0

echo "======================================================================"
echo "CMake Module Validation Script v1.1.0"
echo "======================================================================"
echo ""

# --------------------------------------------------------------------
# 检查 1: file(GLOB) 反模式（库源文件）
# --------------------------------------------------------------------
echo "[ CHECK 1 ] Detecting file(GLOB) anti-pattern in library sources..."
GLOB_PATTERN_FILES=$(find . -name "CMakeLists.txt" -exec grep -l "file(GLOB.*SOURCES.*src/\*\.c)" {} \; 2>/dev/null || true)

if [ -n "$GLOB_PATTERN_FILES" ]; then
    echo -e "${RED}❌ Found dangerous file(GLOB) pattern:${NC}"
    for file in $GLOB_PATTERN_FILES; do
        echo "   - $file"
        TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
    done
    echo ""
    echo "   Recommendation: Replace with explicit source list or glob_library_sources()"
    echo ""
else
    echo -e "${GREEN}✅ No dangerous file(GLOB) patterns found${NC}"
fi
echo ""

# --------------------------------------------------------------------
# 检查 2: 测试文件是否混入库源文件
# --------------------------------------------------------------------
echo "[ CHECK 2 ] Detecting test files in library source lists..."
TEST_IN_SOURCES=$(find . -name "CMakeLists.txt" -exec grep -H "set(.*SOURCES" {} \; | grep "test_.*\.c" || true)

if [ -n "$TEST_IN_SOURCES" ]; then
    echo -e "${RED}❌ Found test files in library source lists:${NC}"
    echo "$TEST_IN_SOURCES"
    TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
    echo ""
    echo "   Recommendation: Move test files to add_module_tests()"
    echo ""
else
    echo -e "${GREEN}✅ No test files found in library sources${NC}"
fi
echo ""

# --------------------------------------------------------------------
# 检查 3: 测试头文件过滤（install 规则）
# --------------------------------------------------------------------
echo "[ CHECK 3 ] Checking test header filtering in install() rules..."
INSTALL_WITHOUT_FILTER=$(find . -name "CMakeLists.txt" -exec grep -l "install(.*DIRECTORY.*src/" {} \; | while read file; do
    if ! grep -q 'PATTERN.*"test_.*\.h".*EXCLUDE' "$file"; then
        echo "$file"
    fi
done)

if [ -n "$INSTALL_WITHOUT_FILTER" ]; then
    echo -e "${YELLOW}⚠️  Warning: install() rules may expose test headers:${NC}"
    for file in $INSTALL_WITHOUT_FILTER; do
        echo "   - $file"
        TOTAL_WARNINGS=$((TOTAL_WARNINGS + 1))
    done
    echo ""
    echo "   Recommendation: Add PATTERN \"test_*.h\" EXCLUDE to install() rules"
    echo ""
else
    echo -e "${GREEN}✅ Test headers properly filtered in install() rules${NC}"
fi
echo ""

# --------------------------------------------------------------------
# 检查 4: 命名空间头文件生成（检测重复代码）
# --------------------------------------------------------------------
echo "[ CHECK 4 ] Detecting manual namespace header generation..."
MANUAL_NAMESPACE=$(find . -name "CMakeLists.txt" -exec grep -l "foreach(header.*configure_file" {} \; | wc -l)

if [ "$MANUAL_NAMESPACE" -gt 0 ]; then
    echo -e "${YELLOW}⚠️  Found $MANUAL_NAMESPACE modules with manual namespace header generation${NC}"
    echo ""
    echo "   Recommendation: Use generate_namespace_headers() from HelperFunctions v1.1"
    echo ""
    TOTAL_WARNINGS=$((TOTAL_WARNINGS + 1))
else
    echo -e "${GREEN}✅ Using generate_namespace_headers() helper${NC}"
fi
echo ""

# --------------------------------------------------------------------
# 检查 5: configure_module_includes() 未使用函数
# --------------------------------------------------------------------
echo "[ CHECK 5 ] Checking for unused configure_module_includes()..."
UNUSED_FUNCTION=$(find . -name "HelperFunctions.cmake" -exec grep -l "function(configure_module_includes)" {} \;)
FUNCTION_USAGE=$(find . -name "CMakeLists.txt" -exec grep -l "configure_module_includes(" {} \; | wc -l)

if [ -n "$UNUSED_FUNCTION" ] && [ "$FUNCTION_USAGE" -eq 0 ]; then
    echo -e "${YELLOW}⚠️  HelperFunctions.cmake defines configure_module_includes() but it's never used${NC}"
    echo ""
    echo "   Recommendation: Remove unused function from HelperFunctions.cmake"
    echo ""
    TOTAL_WARNINGS=$((TOTAL_WARNINGS + 1))
else
    echo -e "${GREEN}✅ No unused helper functions detected${NC}"
fi
echo ""

# --------------------------------------------------------------------
# 检查 6: CMake 最低版本要求
# --------------------------------------------------------------------
echo "[ CHECK 6 ] Checking CMake version requirements..."
LOW_CMAKE_VERSION=$(find . -name "CMakeLists.txt" -exec grep "cmake_minimum_required" {} \; | grep -v "VERSION 3.2[0-9]" || true)

if [ -n "$LOW_CMAKE_VERSION" ]; then
    echo -e "${YELLOW}⚠️  Some modules require CMake < 3.20${NC}"
    echo "$LOW_CMAKE_VERSION"
    echo ""
    echo "   Recommendation: Update to CMake 3.25+ for better DLL handling"
    echo ""
    TOTAL_WARNINGS=$((TOTAL_WARNINGS + 1))
else
    echo -e "${GREEN}✅ All modules require CMake 3.20+${NC}"
fi
echo ""

# --------------------------------------------------------------------
# 检查 7: 模块总数统计
# --------------------------------------------------------------------
TOTAL_MODULES=$(find . -name "CMakeLists.txt" | grep -v "/build/" | grep -v "/cmake/" | wc -l)

# --------------------------------------------------------------------
# 总结报告
# --------------------------------------------------------------------
echo "======================================================================"
echo "Validation Summary"
echo "======================================================================"
echo "Modules Scanned:  $TOTAL_MODULES"
echo "Critical Issues:  $TOTAL_ISSUES"
echo "Warnings:         $TOTAL_WARNINGS"
echo ""

if [ "$TOTAL_ISSUES" -eq 0 ] && [ "$TOTAL_WARNINGS" -eq 0 ]; then
    echo -e "${GREEN}🎉 All checks passed! CMake modules follow best practices.${NC}"
    exit 0
elif [ "$TOTAL_ISSUES" -eq 0 ]; then
    echo -e "${YELLOW}⚠️  No critical issues, but $TOTAL_WARNINGS warning(s) found.${NC}"
    echo "   Review warnings and consider improvements."
    exit 0
else
    echo -e "${RED}❌ Found $TOTAL_ISSUES critical issue(s). Please fix before release.${NC}"
    exit 1
fi
