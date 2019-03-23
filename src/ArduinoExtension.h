#ifndef ArduinoExtension_h
#define ArduinoExtension_h

#include <Arduino.h>

/**
 *
 * @tparam size: automatic filled
 * @param pin: array of pin numbers
 * @param mode: INPUT or OUTPUT
 */
template <size_t size>
void pinMode(const uint8_t (&pin)[size], uint8_t mode)
{
    for (size_t i = 0; i < size; ++i) {
        pinMode(pin[i], mode);
    }
}

/**
 *
 * @tparam size: automatic filled
 * @param pin: array of pin numbers
 * @param val: HIGH or LOW
 */
template <size_t size>
void digitalWrite(const uint8_t (&pin)[size], uint8_t val)
{
    for (size_t i = 0; i < size; ++i) {
        digitalWrite(pin[i], val);
    }
}

/**
 * 
 * The doWhileLoopDelay function guarantees a minimum of 1 execution
 * of the callback function.
 * 
 * @param delay: loop time
 * @param cb: callback function
 * 
 * ussage e.g. 5 seconds:
 * doWhileLoopDelay(5000, []() {
 *     // code
 * });
 */
bool doWhileLoopDelay(unsigned long delay, bool (*cb)())
{
    unsigned long timeNow = millis();
    do {
        if (cb()) {
            return true;
        }
        yield();
    } while (millis() - timeNow < delay);
    return false;
}

void doWhileLoopDelay(unsigned long delay, void (*cb)())
{
    unsigned long timeNow = millis();
    do {
        cb();
        yield();
    } while (millis() - timeNow < delay);
}

/**
 * The whileLoopDelay function can't guarantee a minimum of 1 execution
 * of the callback function.
 * 
 * @param delay: loop time
 * @param cb: callback function
 * 
 * ussage e.g. 5 seconds:
 * doWhileLoopDelay(5000, []() {
 *     // code
 * });
 */
bool whileLoopDelay(unsigned long delay, bool (*cb)())
{
    unsigned long timeNow = millis();
    while (millis() - timeNow < delay) {
        if (cb()) {
            return true;
        }
        yield();
    }
    return false;
}

void whileLoopDelay(unsigned long delay, void (*cb)())
{
    unsigned long timeNow = millis();
    while (millis() - timeNow < delay) {
        cb();
        yield();
    }
}

#endif