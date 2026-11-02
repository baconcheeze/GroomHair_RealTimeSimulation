#pragma once
#include "OrientationTests.h"
#include "WindowSolver.h"
void testPositionLocal(const OrientationTestContext &context);
void testWindowSolver(const OrientationTestContext &context);
void testWindowGroom(guidegpu::State &state, WindowSolver &solver, const OrientationTestContext &context);
