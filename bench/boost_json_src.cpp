/// \file
/// \brief Boost.JSON compiled into this project rather than linked.
///
/// The Homebrew libboost_json.dylib does not carry every template
/// instantiation this codec reaches, and a missing one is a link error rather
/// than a diagnostic. Compiling the library's own source in one translation
/// unit is the arrangement Boost.JSON documents for exactly this, and it also
/// means the benchmark measures the same compiler and flags for every codec
/// rather than one prebuilt with somebody else's.

#include <boost/json/src.hpp>
