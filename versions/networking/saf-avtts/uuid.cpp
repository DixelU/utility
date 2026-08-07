#include "detail/uuid.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace maxcalls::detail {

std::string generate_uuid() {
    static thread_local boost::uuids::random_generator gen;
    return boost::uuids::to_string(gen());
}

} // namespace maxcalls::detail
