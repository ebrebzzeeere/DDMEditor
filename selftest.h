#pragma once
// DDM_SELFTEST=1 の自己検証。10 phase（round-trip baseline/selection/family/constraint/editor/props/font/palette audit/game-mode/data unchanged）を phase 別 static 関数に分割。
// 期待値は実行時計算・ハードコード禁止の原則は各 phase の原文のまま（data.txt に依存して計算する）。

bool runSelftest();
