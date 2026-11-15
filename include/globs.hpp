#pragma once

#include <string>

enum class GlobType {
    All, // *, 0+ characters
    One, // ?
    List,// [abc], [!abc], [a-z] etc...
};

class All
