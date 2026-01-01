# 差動二輪ロボット用コントローラ化のExecPlan

このExecPlanは生きたドキュメントであり、`Progress`、`Surprises & Discoveries`、`Decision Log`、`Outcomes & Retrospective`の各セクションは作業の進行に合わせて必ず更新する。

本ドキュメントはリポジトリ直下の`PLANS.md`にあり、本書に従って維持・更新される必要がある。

## Purpose / Big Picture

STAMPFLYCONTROLLERを差動二輪ロボット用コントローラに作り替え、左右のジョイスティックで左右タイヤを独立操作できるようにする。前に倒すと前進、後ろに倒すと後退となり、指示値はシリアルでPCへ送信される。変更後は、PC側のシリアルモニタで`L:<value>,R:<value>`が流れていることを確認でき、スティック操作に応じて値が正負に変化することを観察できる。

## Progress

- [x] (2025-02-14 03:00Z) ジョイスティックの生値レンジと向きを確認し、左右タイヤ用に正規化手順を決定する。
- [x] (2025-02-14 03:00Z) `src/main.cpp`と`lib/ATOMS3Joy/atoms3joy.*`に左右タイヤ指示値の変換関数と生値取得関数を追加する。
- [x] (2025-02-14 03:00Z) `loop()`内の送信処理をシリアル送信に置換し、ESP-NOW送信を無効化する。
- [ ] (2025-02-14 03:00Z) デッドゾーンとスケールを調整し、シリアル出力が期待通りか確認する（実装済み、実機確認が残り）。
- [ ] (2025-02-14 03:00Z) `pio run`でビルドし、実機でシリアル出力を検証する。

## Surprises & Discoveries

- Observation: なし。
  Evidence: ビルド・実機検証は未実施。

## Decision Log

- Decision: 初期状態ではテキスト1行1フレーム形式`L:<value>,R:<value>`で送信する。
  Rationale: PC側の受信実装が簡単で、デバッグが容易なため。
  Date/Author: 2025-02-14 / Codex
- Decision: 左右スティックYの生値は`lib/ATOMS3Joy/atoms3joy.*`に専用getterを追加して取得する。
  Rationale: 既存のスティック割当（THROTTLE等）に依存せず、左右固定の読み取りを保証するため。
  Date/Author: 2025-02-14 / Codex
- Decision: ESP-NOW関連処理は`kUseEspNow`で無効化し、差動二輪モードではシリアル送信のみ行う。
  Rationale: 目的がPCへの指示値送信であり、無線の初期化と送信が不要なため。
  Date/Author: 2025-02-14 / Codex

## Outcomes & Retrospective

- まだ未着手。実装完了後に成果と残課題をまとめる。

## Context and Orientation

本リポジトリはPlatformIO + Arduino構成で、ターゲットは`m5stack-atoms3`である。エントリポイントは`src/main.cpp`で、ジョイスティック入力は`atoms3joy`系のAPIを用いて取得している。既存実装はESP-NOWで機体と通信しているが、本変更ではESP-NOWを使わず、PCへシリアル送信を行う。主要ファイルは`src/main.cpp`と`lib/ATOMS3Joy/atoms3joy.*`であり、送信ロジックの差し替えと入力変換、左右スティックYの生値取得をここに集約する。

## Plan of Work

最初に既存のジョイスティック取得関数と生値レンジを確認し、左右スティックYの向きと符号を把握する。次に`lib/ATOMS3Joy/atoms3joy.*`へ左右スティックYの生値取得用getterを追加し、`src/main.cpp`へ左右タイヤの指示値を作る変換関数を追加する。スティック中心を0とする正規化・デッドゾーン・スケール変換を統一的に行う。続いて`loop()`内の送信処理を差動二輪用に置き換え、ESP-NOW送信は呼び出されないように整理する。最後にシリアルモニタで値の正負とスケールを確認し、必要に応じてデッドゾーンとスケールを調整する。

## Concrete Steps

作業ディレクトリはリポジトリ直下とする。

1. ジョイスティック入力の確認
   - 対象ファイル: `src/main.cpp`
   - 既存の取得関数（例: `getThrottle()`、`getAileron()`など）と、左右スティックYに相当する入力の取得箇所を確認する。

2. 変換関数の追加
   - `lib/ATOMS3Joy/atoms3joy.*`に左右スティックYの生値取得関数を追加する。
   - `src/main.cpp`に、左右タイヤ用の変換関数（例: `int16_t map_stick_to_wheel(int16_t raw)`）を追加し、中心化、正規化、デッドゾーン、スケール変換を実装する。

3. シリアル送信への切り替え
   - `loop()`内で左右タイヤ指示値を算出し、`USBSerial`または`Serial`で1行1フレームのテキストを送る。
   - 例出力:
       L:120,R:-80

4. ESP-NOW送信の無効化
   - `data_send()`などESP-NOW送信ロジックを差動二輪モードでは呼び出さない。

5. ビルドと確認
   - 実行コマンド（リポジトリ直下）:
       pio run -e m5stack-atoms3
       pio run -t upload -e m5stack-atoms3
       pio device monitor -e m5stack-atoms3

## Validation and Acceptance

- シリアルモニタで`L:<value>,R:<value>`形式の出力が連続的に表示される。
- 左スティック前倒しで`L`が正、後ろ倒しで`L`が負に変化する。
- 右スティック前倒しで`R`が正、後ろ倒しで`R`が負に変化する。
- スティック中立では`L:0,R:0`付近で安定する。

## Idempotence and Recovery

- 変更は`src/main.cpp`中心の追加・置換であり、同じ手順を繰り返しても安全に再適用できる。
- もしシリアル出力が期待通りでない場合は、デッドゾーン幅や符号の反転を調整し再書き込みする。

## Artifacts and Notes

- 例: シリアル出力サンプル
    L:0,R:0
    L:250,R:240
    L:-300,R:-310

## Interfaces and Dependencies

- 使用ライブラリ: Arduino標準の`Serial`/`USBSerial`、既存の`atoms3joy`入力API。
- 出力インターフェース: テキスト1行1フレームのシリアル出力。
- 変換関数のシグネチャ例:
    int16_t map_stick_to_wheel(int16_t raw)
    int16_t apply_deadzone(int16_t value, int16_t deadzone)


変更履歴: 2025-02-14 / PLANS.mdを差動二輪ロボット用コントローラ化のExecPlanとして日本語で全面更新。
変更履歴: 2025-02-14 / 実装反映に合わせてProgress、Context、Decision Log、Plan of Workを更新。
