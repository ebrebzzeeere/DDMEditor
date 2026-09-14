#include "DialogueDataManager.h"
#include "GameLib/Base/OStringStream.h"
#include "GameLib/FileIO/InFile.h"
#include "GameLib/FileIO/OutFile.h"
#include "GameLib/Framework.h"
#include "GameLib/Math/Random.h"
#include "GameLib/PseudoXml/Attribute.h"
#include "GameLib/PseudoXml/Element.h"
#include <sstream>

namespace {}

DialogueDataManager::DialogueDataManager()
    : mRoot(0), mUnlocks(0x0), InputStates(0x0), StagedDialogueIdxs{},
      SelectedDialogueIdxs(0), bHasIntermissionStarted(0) {
  PseudoXml::Document src = PseudoXml::Document::create("Demo2.txt");
  while (!src.isReady()) {
    ;
  }

  MDialogues.setSize(512);
  const PseudoXml::Attribute &Data = src.child(0).child(0).attribute(0);
  int DialogueNumber = 0;
  const char *data = Data.value();
  while (*data != '\0') {
    ostringstream oss;
    while (*data != ',' && *data != '\0') {
      oss << *data;
      ++data;
    }

    if (*data == ',' || *data == '\0') {
      MDialogues[DialogueNumber] = oss.str();
      ++DialogueNumber;
      ++data;
    }
  }
  MProperties.setSize(3, DialogueNumber);

  const PseudoXml::Attribute &AProperties1 = src.child(0).child(1).attribute(0);
  const PseudoXml::Attribute &AProperties2 = src.child(0).child(2).attribute(0);
  const PseudoXml::Attribute &AProperties3 = src.child(0).child(3).attribute(0);
  double *a = new double[DialogueNumber];
  double *b = new double[DialogueNumber];
  double *c = new double[DialogueNumber];
  AProperties1.getDoubleValues(a, DialogueNumber);
  AProperties2.getDoubleValues(b, DialogueNumber);
  AProperties3.getDoubleValues(c, DialogueNumber);

  src.release();

  for (int i = 0; i < DialogueNumber; ++i) {
    MProperties(0, i) = static_cast<unsigned>(a[i]);
    MProperties(1, i) = static_cast<unsigned>(b[i]);
    MProperties(2, i) = static_cast<unsigned>(c[i]);
  }

  SAFE_DELETE_ARRAY(a);
  SAFE_DELETE_ARRAY(b);
  SAFE_DELETE_ARRAY(c);

  ExtractActiveData(0);
  StageDialogues(0, 0);
}

DialogueDataManager::~DialogueDataManager() {
  cout << mRoot << ", " << mUnlocks << ", " << mLastSendIdx << endl;
}

string DialogueDataManager::GetDialogue(int SelectedDialogueIdx) const {
  /*
   */
  unsigned StagedDialogueIdx = StagedDialogueIdxs[SelectedDialogueIdx];
  return StagedDialogueIdx ? static_cast<string>(ADialogues[StagedDialogueIdx])
                           : "\0";
}

void DialogueDataManager::ExtractActiveData(unsigned Idx) {
  mUnlocks = 0;
  mRoot = 0;

  int DataSize = 0;
  unsigned DialogueData[255];
  unsigned Scene;
  if (ADialogues.size()) {
    Scene = GetNewScene(Idx) ? GetNewScene(Idx) : GetScene(Idx);
  } else {
    Scene = 8; // default scene.
  }
  for (int i = 0; i < MProperties.size(1); ++i) {
    if (GetScene(i) == Scene) {
      DialogueData[DataSize] = i;
      ++DataSize;
    }
  }

  if (DataSize) {
    ADialogues.clear();
    ADialogues.setSize(DataSize);
    AProperties.clear();
    AProperties.setSize(3, DataSize);

    int h = 0;
    for (int i = 0; i < DataSize; ++i) {
      ADialogues[h] = MDialogues[DialogueData[i]];
      AProperties(0, h) = MProperties(0, DialogueData[i]); // Prop1
      AProperties(1, h) = MProperties(1, DialogueData[i]); // Prop2
      AProperties(2, h) = MProperties(2, DialogueData[i]); // Prop3
      ++h;
    }
  }
}

void DialogueDataManager::ExecuteSelection(unsigned Idx) {
  // Debug
  mLastSendIdx = Idx;

  UpdateUnlocks(Idx);

  if (mUnlocks & 0x80) // Exit flag.
  {
    bHasIntermissionStarted = true;
  }

  if (IsOnetime(Idx)) {
    MarkAsRead(Idx);
  }

  if (GetRoot(Idx)) {
    if (GetRoot(Idx) == 0xffff) {
      // mRoot is zero. So mRoot cant be 0x0.
      mRoot = 0x0; // Reset DefaultID.
    } else {
      mRoot = GetRoot(Idx);
      Idx = 0; // This Idx should not be a Parent.
    }

    if (!GetNewScene(Idx)) {
      // Just Change the mRoot.
      StageDialogues(Idx, 0);
      return;
    }
  } // GetRoot() end.

  // Make New List.
  if (GetNewScene(Idx)) {
    ExtractActiveData(Idx);
    StageDialogues(Idx, 0);
    return;
  }
  StageDialogues(Idx, 0);
}

void DialogueDataManager::StageDialogues(unsigned Idx, int SlotIdx) {
  if (!SlotIdx) {
    // 知らない子がいる。
    // memset(StagedDialogueIdxs, 0, sizeof(StagedDialogueIdxs));
  } else if (!Idx) {
    return; // Idx is null and SlotIdx is valid.
  }

  bool bHasChild;
  if (Idx) {
    bHasChild = GetChild(Idx);
  } else {
    bHasChild = false;
  }

  if (bHasChild) {
    bHasChild = !HasRead(AProperties(0, GetChild(Idx))) &&
                IsUnlocked(AProperties(1, GetChild(Idx)));
  }

  if (bHasChild) // Specify child uniquely.
  {
    if (!SlotIdx) {
      StagedDialogueIdxs[0] = GetChild(Idx);
      return;
    } else {

      StagedDialogueIdxs[SlotIdx * 5 + 1] = GetChild(Idx);
      return;
    }
  }

  // Child part end.
  // Indeed this HasChild part is rediculous.

  unsigned Parent;
  if (Idx) {
    Parent = GetId(Idx);
  } else {
    Parent = mRoot;
  }
  int StagedDialogueNumber = SlotIdx ? 1 : 0;
  unsigned RandomPickUpFlag = 0x0; // 0x-NumberOfQuotes[2]
  for (int i = 0; i < AProperties.size(1); ++i) {
    if (GetParent(i) == Parent) {
      if (!HasRead(AProperties(0, i)) && IsUnlocked(AProperties(1, i))) {
        if (SlotIdx) {
          if (IsQuote(i) /* && IsOnetime(i) */) {
            continue;
          }

          StagedDialogueIdxs[(SlotIdx - 1) * 5 + StagedDialogueNumber] = i;
          StagedDialogueNumber =
              StagedDialogueNumber == 4 ? 0 : ++StagedDialogueNumber;
          if (!StagedDialogueNumber) {
            break;
          }
        } else {
          if (IsQuote(
                  i) /* && IsOnetime(i) */) // Kind of spagetti. I don't know
                                            // why this commentout is needed.
                                            // Details is discribed in below.
          {
            ++RandomPickUpFlag; // random pick up is needed.
          }

          StagedDialogueIdxs[StagedDialogueNumber * 5] = i;
          StagedDialogueNumber =
              StagedDialogueNumber == 4 ? 0 : ++StagedDialogueNumber;

          if (!StagedDialogueNumber) {
            break; // Got full 4 Dialogues.
          }
        }
      }
    }
  }

  if (SlotIdx) {
    return;
  }

  // Parents go to get to recursively their children.
  for (int i = 0; i < 4; ++i) {
    StageDialogues(StagedDialogueIdxs[i * 5], i + 1);
  }

  // Post Process
  if (!StagedDialogueNumber) // No dialogue staged, initialize.
  {
    StageDialogues(0, 0);
    return;
  }

  if (RandomPickUpFlag) // If there are multiple quotes, narrow it down to one.
  {
    for (int i = 0; i < 4; ++i) {
      if (!IsQuote(StagedDialogueIdxs[i * 5])) {
        return;
      }
    } // Kind of spagetti. I don't know how this work.
    unsigned msTime =
        static_cast<unsigned>(Framework::instance().time()) & 0xffff;
    Math::Random rand = Math::Random::create(msTime);
    int a = rand.getInt(RandomPickUpFlag);
    for (int i = 0; i < 16; ++i) {
      if (i == 0) {
        StagedDialogueIdxs[0] = StagedDialogueIdxs[a * 5];
        continue;
      }
      StagedDialogueIdxs[i] = 0;
    }
    return;
    /*
    If you have a not one time quote in verb, you need to avoid Random pick up.
    While if you append random pick up to one time quotes, quotes will show
    options directly.

    */
  }

  // I didn't consider every case enough, so there might be bugs.
  if (IsQuote(Idx) && !SlotIdx && Idx) {
    StagedDialogueIdxs[1] = StagedDialogueIdxs[0];
    StagedDialogueIdxs[2] = StagedDialogueIdxs[5];
    StagedDialogueIdxs[3] = StagedDialogueIdxs[10];
    StagedDialogueIdxs[4] = StagedDialogueIdxs[15];
    StagedDialogueIdxs[0] = Idx;
    StagedDialogueIdxs[5] = 0;
    StagedDialogueIdxs[10] = 0;
    StagedDialogueIdxs[15] = 0;
  }
}

void DialogueDataManager::HandleInput(unsigned Input) {
  if (InputStates & 0x40) {
    /* Quote is focused. */
    int SelectedQuoteIdx = static_cast<int>(SelectedDialogueIdxs >> 2);
    int MaxDialogueNumber = 0;
    switch (Input) {
    case 0x1: // To up.
      // Nothing happens.
      break;

    case 0x2: // To left.
      for (int i = 1; i <= 4; ++i) {
        if (StagedDialogueIdxs[SelectedQuoteIdx * 5 + i] == 0) {
          break; // leave the loop.
        }
        ++MaxDialogueNumber;
      }

      if (SelectedQuoteIdx - 1 < 0) {
        SelectedQuoteIdx = MaxDialogueNumber - 1;
        SelectedDialogueIdxs = static_cast<unsigned>(SelectedQuoteIdx << 2) |
                               (SelectedDialogueIdxs & 0x3);
      } else {
        --SelectedQuoteIdx;
        SelectedDialogueIdxs = static_cast<unsigned>(SelectedQuoteIdx << 2) |
                               (SelectedDialogueIdxs & 0x3);
      }
      break;

    case 0x4:              // To down.
      InputStates &= 0x1f; // Down 6th flag. Quote is not focused.
      break;

    case 0x8: // To right.
      for (int i = 1; i <= 4; ++i) {
        if (StagedDialogueIdxs[SelectedQuoteIdx * 5 + i] == 0) {
          break;
        }
        ++MaxDialogueNumber;
      }

      if (SelectedQuoteIdx + 1 == MaxDialogueNumber) {
        SelectedQuoteIdx = 0;
        SelectedDialogueIdxs &= 0x3;
      } else {
        ++SelectedQuoteIdx;
        SelectedDialogueIdxs = static_cast<unsigned>(SelectedQuoteIdx << 2) |
                               (SelectedDialogueIdxs & 0x3);
      }
      break;

    case 0x20: // Confirm
      SelectedQuoteIdx = (SelectedDialogueIdxs >> 2) + 1 /* to 1 based */ +
                         (SelectedDialogueIdxs & 0x3) * 5;
      ExecuteSelection(StagedDialogueIdxs[SelectedQuoteIdx]);

      SelectedDialogueIdxs = 0x0;
      InputStates = 0x0;
      break;
    }
  } else {
    /* Verb is focused. */
    SelectedDialogueIdxs &= 0x3; // reset Quote selection.
    int SelectedVerbIdx = static_cast<int>(SelectedDialogueIdxs & 0x3);
    int MaxDialogueNumber = 0;
    switch (Input) // Verb is focused.
    {
    case 0x1:              // To up.
      InputStates |= 0x40; // Quote is focused.

      /*
      How to handle if verb has no quote?

      */
      break;

    case 0x2: // To left.
      for (int i = 0; i < 4; ++i) {
        if (StagedDialogueIdxs[i * 5] == 0) {
          break;
        }
        ++MaxDialogueNumber;
      }

      if (SelectedVerbIdx - 1 < 0) {
        SelectedVerbIdx = MaxDialogueNumber - 1;
        SelectedDialogueIdxs = (SelectedDialogueIdxs & 0xc) |
                               static_cast<unsigned>(SelectedVerbIdx);
      } else {
        --SelectedVerbIdx;
        SelectedDialogueIdxs = (SelectedDialogueIdxs & 0xc) |
                               static_cast<unsigned>(SelectedVerbIdx);
      }
      break;

    case 0x4: // To down.
      // Nothing happens.
      break;

    case 0x8: // To right.
      for (int i = 0; i < 4; ++i) {
        if (StagedDialogueIdxs[i * 5] == 0) {
          break;
        }
        ++MaxDialogueNumber;
      }

      if (SelectedVerbIdx + 1 == MaxDialogueNumber) {
        SelectedDialogueIdxs &= 0xc;
      } else {
        ++SelectedVerbIdx;
        SelectedDialogueIdxs = (SelectedDialogueIdxs & 0xc) |
                               static_cast<unsigned>(SelectedVerbIdx);
      }
      break;

    case 0x10: // Cancel
      break;

    case 0x20: // Confirm
      SelectedVerbIdx = (SelectedDialogueIdxs & 0x3) * 5;
      if (IsQuote(StagedDialogueIdxs[SelectedVerbIdx]) &&
          GetParent(StagedDialogueIdxs[SelectedVerbIdx])) {
        ExecuteSelection(StagedDialogueIdxs[SelectedVerbIdx]);

        SelectedDialogueIdxs = 0x0;
        InputStates = 0x0;
      }
      break;
    }
  }
}

void DialogueDataManager::Resume(
    unsigned Root, unsigned Unlocks,
    unsigned LastSendIdx) { // Only valid in the same scene.
  mRoot = Root;
  mUnlocks = Unlocks;
  StageDialogues(LastSendIdx, 0);
}

// return value is 1 based.
unsigned DialogueDataManager::SelectedQuoteIdx() const {
  return InputStates & 0x40 ? (SelectedDialogueIdxs >> 2) + 1 : 0;
}

// return value is 0 based.
unsigned DialogueDataManager::SelectedVerbIdx() const {
  return SelectedDialogueIdxs & 0x3;
}

/*
Management Functions. A lot of overheads but easy to read codes.
*/
void DialogueDataManager::MarkAsRead(unsigned Idx) {
  AProperties(0, Idx) |= 0x40000000; // Mark as Read.
}

void DialogueDataManager::UpdateUnlocks(unsigned Idx) {
  unsigned unlocks = GetUnlocks(Idx);

  mUnlocks |= unlocks; // Activate Lamp.
}

unsigned DialogueDataManager::GetUnlocks(unsigned Idx) {
  return AProperties(2, Idx) >> 16 & 0xffff;
}

unsigned DialogueDataManager::GetRequires(unsigned prop) {
  return prop & 0xffff;
}

unsigned DialogueDataManager::GetExcludes(unsigned prop) {
  return (prop >> 16) & 0xffff;
}

unsigned DialogueDataManager::GetId(unsigned Idx) {
  return AProperties(0, Idx) & 0xff;
}

unsigned DialogueDataManager::GetScene(unsigned Idx) {
  return (MProperties(0, Idx) & 0xf00) >> 8;
}

unsigned DialogueDataManager::GetParent(unsigned Idx) {
  return (AProperties(0, Idx) >> 12) & 0xff;
}

unsigned DialogueDataManager::GetChild(unsigned Idx) {
  return (AProperties(0, Idx) >> 20) & 0xff;
}

unsigned DialogueDataManager::GetNewScene(unsigned Idx) {
  return AProperties(2, Idx) & 0xf;
}

unsigned DialogueDataManager::GetRoot(unsigned Idx) {
  return (AProperties(2, Idx) & 0xff0) >> 4;
}

void DialogueDataManager::SetUV(unsigned Idx) {
  if (AProperties(2, Idx) & 0xf000) // If Texture is valIdx.
  {
    // UV = (AProperties(2, Idx) & 0xf000) >> 12;
  }
}

void DialogueDataManager::MarkAsUnread(unsigned Idx) {
  AProperties(0, Idx) &= 0x3fffffff; // Mark as not Read.
}

bool DialogueDataManager::IsQuote(int index) {
  return AProperties(0, index) & 0x10000000; // Quote has a valIdx byte.
}

bool DialogueDataManager::HasRead(unsigned prop) { return prop & 0x40000000; }

bool DialogueDataManager::IsUnlocked(unsigned prop) {
  unsigned Requires = GetRequires(prop);
  unsigned Excludes = GetExcludes(prop);
  bool IsExcluded = Excludes ? mUnlocks & Excludes : false;
  return ((mUnlocks & Requires) == Requires) /* every flags must be up */ &&
         !IsExcluded /* even one flag mustn't be up. */;
}

bool DialogueDataManager::IsOnetime(unsigned Idx) {
  return AProperties(0, Idx) & 0x20000000;
}

namespace {} // namespace
