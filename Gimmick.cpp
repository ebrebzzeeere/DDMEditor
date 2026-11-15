#include "Gimmick.h"

Gimmick::Gimmick() {}
Gimmick::~Gimmick() {}

void Gimmick::scene0(uint64_t prop) {
	switch (prop) {
		case 0x0010000000100a07:
			// ここから直接、sendProperty()を呼ぶような形で、Elementを挿入しなくてはいけない。
			// 多分、クラス中にtextなどのデータを持たせることはしないで、こちらでバックアップするような形にするべきだろう。
			// この関数でtrueを返して、sendProperty()中で単純にif分岐してもいいかも。
			ticketNumber += gTick;
		break;
	
	}
  /*
   * 
   */
}
