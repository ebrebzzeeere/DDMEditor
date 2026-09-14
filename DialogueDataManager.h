#pragma once

#include "GameLib/PseudoXml/Document.h"
#include <map>
#include "GameLib/Base/Array.h"
#include "GameLib/Base/Array2d.h"
#include <string>

using namespace GameLib;
using namespace std;


//class GameLib::PseudoXml::Document;

/*
NOTE ON PROPERTIES:
Prop1 - 31bit
HAS_READ[31] - 1bit
IS_Onetime[30] - 1bit
IS_QUOTE[29] - 1bit
CHILD ff - 8bit
PARENT ff -
SCENE f - 4bit
ID ff - 8bit

PROP2 - 32bit
EXCLUDES[16-31] - 16bit // Opend and Out
REQUIRES[0-15] - 16bit // Opend and Go

Prop3 - 32bit
UNLOCKS ffff - 16bit // Open
UV f - 4bit
NEW_ROOT ff - 8bit
NEW_SCENE f - 4bit

*/

class DialogueDataManager
{
public:


	DialogueDataManager();
	~DialogueDataManager();


public:
	string GetDialogue(int SelectedDialogueIdx) const;

	unsigned SelectedQuoteIdx() const;
	unsigned SelectedVerbIdx() const;
	void HandleInput(unsigned Input);
	bool bHasIntermissionStarted;
	void Resume(unsigned Root, unsigned Unlocks, unsigned LastSendIdx);

private:
	unsigned mLastSendIdx;

private:
	// master Data
	Array<string> MDialogues; // M stands for master.
	Array2d<unsigned>MProperties;
	
	// active Data
	Array<string>ADialogues; // A stands for active.
	Array2d<unsigned>AProperties;

	// State Data.
	unsigned mRoot;
	unsigned mUnlocks; // 8bit flag. Indicates phase of story. Bit0 is one-time flag, temporary.

	unsigned InputStates; // 0b-IsQuoteFocused-Cancel-Confirm-D-A-S-W
	unsigned SelectedDialogueIdxs; // Idxs for Staded Dialogue. 0x-SelectedQuote(2bit)-SelectedVerb(2bit).
	unsigned StagedDialogueIdxs[20]; // Verb[0+i*5], Quote1[1+i*5],Quote2[2+i*5],Quote3[3+i*5],Quote4[4+i*5],

	/*
	[
		0Verb1, 1Quote1, 2Quote2, 3Quote3, 4Quote4,
		5Verb2, Quote1, Quote2, Quote3, Quote4,
		10Verb3, Quote1, Quote2, Quote3, Quote4,
		15Verb4, Quote1, Quote2, Quote3, 19Quote4,
	]

	slot 0 : groupIndex
	slot 1 : entry1
	slot 2 : entry2
	slot 3 : entry3
	slot 4 : entry4

[0, 5, 10, 15], [1, -,4], [6, -,9]

	
	AProp[StagedDataIdxs[SelectedDialoguIdx]] = MProp[x]

	
	*/
private:
	void StageDialogues(unsigned Idx, int SlotIdx);
	void ExecuteSelection(unsigned Idx);
	void ExtractActiveData(unsigned Idx);
	bool IsQuote(int Idx);
	bool HasRead(unsigned prop);
	bool IsUnlocked(unsigned prop);
	bool IsOnetime(unsigned Idx);


	void MarkAsRead(unsigned Idx);
	void UpdateUnlocks(unsigned Idx);

	//overhead functions
	unsigned GetId(unsigned Idx);
	unsigned GetScene(unsigned Idx);
	unsigned GetParent(unsigned Idx);
	unsigned GetChild(unsigned Idx);
	unsigned GetRequires(unsigned prop);
	unsigned GetExcludes(unsigned prop);
	unsigned GetUnlocks(unsigned Idx);
	unsigned GetNewScene(unsigned Idx);
	unsigned GetRoot(unsigned Idx);
	void SetUV(unsigned Idx);
	void MarkAsUnread(unsigned Idx);






private:
	DialogueDataManager(DialogueDataManager&) {} // Do not use.
	void operator=(DialogueDataManager&) {} // Do not use.
};

/* RULES ON DIALOGUE DATA.

	Attribute must be two, Dialogue(Attribute0) and Properties(Attribute1).
	Each Dialogue and Properties must be same number.

*/
