#include "map_id_filter.hpp"

#include <cassert>
#include <string>

int main()
{
    using simagv::l2::isMapIdCompatible;

    assert(!isMapIdCompatible("", ""));
    assert(!isMapIdCompatible("a", ""));
    assert(!isMapIdCompatible("", "a"));
    assert(isMapIdCompatible("a", "a"));
    assert(!isMapIdCompatible("a", "b"));

    return 0;
}
