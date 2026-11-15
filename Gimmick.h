#pragma once

#include <cstdint>

class Gimmick {
public:
  Gimmick();
  ~Gimmick();

private:
  void update();
  void scene0(uint_fast64_t prop);

private:
	unsigned ticketNumber = 1032;
};
