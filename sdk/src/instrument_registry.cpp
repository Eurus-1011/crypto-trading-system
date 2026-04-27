#include <defs.hpp>

InstrumentRegistry& InstrumentRegistry::Instance() {
    static InstrumentRegistry instance;
    return instance;
}
