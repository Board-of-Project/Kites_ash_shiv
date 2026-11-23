#pragma once
#include "common/assembled_program.h"
#include "reordering/reorder_core.h"
#include "reordering/testcase.h"
#include <string>

namespace Kites
{
std::string formatReordered(const ReorderResult &r);
AssembledProgram applyReordering(const AssembledProgram &in, const ReorderResult &r);
} // namespace Kites