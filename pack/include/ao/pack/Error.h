#pragma once

namespace ao::pack {
enum class Error {
    Ok,
    Eof,
    BadArg,     // Bad argument
    BadData,    // User configured error
    Unaligned,  // Error when using bytes but not aligned
    Overflow    // On writes to fixed buffers and no more memory
};
}
