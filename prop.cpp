#include "prop.h"
#include <cstdint>
#include "guitext.h"




uint64_t buildProperty(unsigned id, unsigned parent, unsigned scene,
                       bool isQuote, bool isOnetime) {
  uint64_t prop = 0;
  prop |= (id & 0xFF);                      // bit 0-7: id
  prop |= ((uint64_t(parent) & 0xFF) << 8); // bit 8-15: parent
  if (isQuote)
    prop |= (1ULL << 17); // bit 17: isQuote
  if (isOnetime)
    prop |= (1ULL << 18);                   // bit 18: isOnetime
  prop |= ((uint64_t(scene) & 0x0F) << 20); // bit 20-23: scene
  return prop;
}
