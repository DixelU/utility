#ifndef MAXCALLS_DETAIL_UUID_HPP
#define MAXCALLS_DETAIL_UUID_HPP

#include <string>

namespace maxcalls::detail {

// Returns a freshly generated random (v4) UUID in canonical 8-4-4-4-12 form.
std::string generate_uuid();

} // namespace maxcalls::detail

#endif // MAXCALLS_DETAIL_UUID_HPP
