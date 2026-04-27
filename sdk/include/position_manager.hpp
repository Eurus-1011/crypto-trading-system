#pragma once

#include <defs.hpp>
#include <string>

class IPositionManager {
  public:
    virtual ~IPositionManager();

    virtual void ReserveSpot(const std::string& currency, double amount) = 0;
    virtual double GetEffectiveAvailableSpot(const std::string& currency) const = 0;
    virtual SwapPosition GetSwapPosition(const std::string& instrument, PosSide position_side) const = 0;
};
