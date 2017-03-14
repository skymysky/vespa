// Copyright 2017 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/fastos/fastos.h>
#include "search_context_params.h"
#include <limits>

namespace search {
namespace attribute {

SearchContextParams::SearchContextParams()
    : _diversityAttribute(nullptr),
      _diversityCutoffGroups(std::numeric_limits<uint32_t>::max()),
      _useBitVector(false),
      _diversityCutoffStrict(false)
{
}

}
}
