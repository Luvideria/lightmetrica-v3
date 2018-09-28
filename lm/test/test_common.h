/*
    Lightmetrica - Copyright (c) 2018 Hisanari Otsu
    Distributed under MIT license. See LICENSE file for details.
*/

#pragma once

#include <lm/lm.h>
#include <doctest.h>

#define LM_TEST_NAMESPACE lmtest

LM_NAMESPACE_BEGIN(LM_TEST_NAMESPACE)

// Captures outputs from std::cout
std::string captureStdout(const std::function<void()>& testFunc);

LM_NAMESPACE_END(LM_TEST_NAMESPACE)
