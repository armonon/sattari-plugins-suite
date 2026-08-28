#pragma once

/**
 * Prevents parameter jumps when a hardware absolute-encoder value doesn't match
 * the current software value.
 *
 * Self-calibrating: call update(hwValue, currentParamValue) on every incoming CC.
 * Returns true when the hardware has caught up and the value should be applied.
 * Automatically re-gates if the parameter is moved externally (preset load, automation).
 *
 * Ported from Mixxx's softtakeover.cpp, simplified for JUCE float-param use.
 */
class SoftTakeover
{
public:
    /**
     * Call with each incoming normalized [0,1] hardware value and the current [0,1]
     * parameter value. Returns true when the value should be applied to the parameter.
     */
    bool update (float hwValue, float currentParamValue) noexcept
    {
        // Once tracking: keep applying hardware directly.
        // Re-gate only if the param diverged significantly (preset load, automation).
        if (tracking)
        {
            if (std::abs (currentParamValue - lastApplied) > threshold)
                tracking = false;   // param was changed externally — wait for hw to catch up
            else
            {
                lastHw = hwValue;
                lastApplied = hwValue;
                return true;
            }
        }

        // Not tracking: wait for the hardware to reach the current parameter value.
        const bool closeEnough = std::abs (hwValue - currentParamValue) <= threshold;
        const bool justCrossed = (lastHw >= 0.0f)
                              && ((lastHw < currentParamValue && hwValue >= currentParamValue)
                               || (lastHw > currentParamValue && hwValue <= currentParamValue));

        lastHw = hwValue;

        if (closeEnough || justCrossed)
        {
            tracking  = true;
            lastApplied = hwValue;
            return true;
        }

        return false;
    }

    void setThreshold (float t) noexcept { threshold = juce::jlimit (0.001f, 0.1f, t); }

private:
    float threshold   = 0.02f;   // 2 % of normalized range
    float lastHw      = -1.0f;   // -1 = no prior hw value
    float lastApplied = 0.0f;    // last value we wrote to the param
    bool  tracking    = false;   // true once hw caught up
};
