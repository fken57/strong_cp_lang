# fken_cpl

入力・配列・ループ・関数を使って競技プログラミングの問題を記述し、C++20 に変換する言語です。
CLI、VS Code 拡張、stdio LSP、補完・リアルタイム診断付きのローカル Web エディタを利用できます。

[fken57/calc_compile](https://github.com/fken57/calc_compile) の
[00c7607](https://github.com/fken57/calc_compile/commit/00c76076a1e222522b2948464559de71d021911c)
を土台にしています。元の C 製フロントエンドとアセンブリ生成器を引き継ぎ、
C++ 生成器と自動昇格する整数型を追加しています。

## AtCoder に提出する

Web エディタで **C++ に変換** を押し、**提出用コピー** で C++ 全体をコピーします。
CLI で同じ提出用ファイルを作るには、`./fken_cpl solution.fken -o Main.cpp` を実行します。
C++ の先頭には fken_cpl の GitHub URL と変換元のコードがコメントで入ります。
AtCoder の言語選択は **C++23 (GCC 15.2.0)** を使用できます。
[AtCoder の言語環境](https://img.atcoder.jp/file/language-update/2025-10/language-list.html)

fken_cpl の変換と補完は言語の規則に基づきます。開催中の ABC・ARC・AGC では生成AIによるコード変換が禁止されています。
開催前に生成AIで作成されたコードを使う場合は、開始前の公開と出典URLの添付が必要です。
[AtCoder の公式ルール](https://info.atcoder.jp/entry/llm-rules-ja)

## 言語リファレンス

- [基礎仕様](docs/SPEC.md) — 型、文字列、配列、構造体、関数、制御構文、入出力
- [アルゴリズム](docs/ALGORITHMS.md) — 二分探索、ランレングス圧縮、グラフ型、木の直径

## 動かす

### VS Code 拡張

`.build/fken-cpl-0.9.0.vsix` を VS Code の **VSIX からのインストール…** で開きます。
Remote WSL の場合は WSL に接続したウィンドウへインストールします。
Python 3 と GCC / Clang を用意し、`.fken` を開くと補完・診断・ホバーが有効になります。
右上のボタンまたは Ctrl+Alt+C で、未保存の編集も含めて C++ を新しいタブへ生成します。
生成コードの実行には別途 C++20 と Boost が必要です。

拡張のビルド・設定・テスト手順は [editors/vscode/README.md](editors/vscode/README.md) に記載しています。

### Web エディタ

~~~sh
./fken_cpl --web
~~~

[http://127.0.0.1:8765](http://127.0.0.1:8765) をブラウザで開きます。
ポートは `--port 9000` で変更できます。終了は端末で Ctrl+C です。
Web と LSP の起動には Python 3 と C コンパイラを使います。追加の Python パッケージ、Node.js、CDN は不要です。

- 入力後 250ms で構文・名前・型・戻り値をチェックし、エラー箇所に下線を表示
- Ctrl+Space で補完。関数・組み込み API・見える変数・`for` / `rep` などのテンプレートを選択
- ↑↓ と Tab / Enter で候補を挿入。テンプレートの入力箇所は Tab で移動
- 診断をクリックするとエラー位置へ移動
- Ctrl+Enter または「C++ に変換」で生成し、全体をコピー・`.cpp` 保存
- ブラウザへの自動保存、サンプル切り替え、直前のコードへの復元
- 入力を変更した後は古い生成結果のコピー・保存を無効化
- エディタからキーボードで移動する場合は Esc → Tab

生成 C++ の整数ランタイムは画面上で折りたためます。コピー・保存にはランタイムも含まれます。
Web はローカルの Python サーバーから C 製変換器を呼び、C++ ソースを生成します。
「標準入力」にテストデータを入れて「実行」を押すと、ブラウザの Worker が解析済みの fken プログラムを実行し、標準出力を表示します。停止ボタンで中断できます。Web 実行は 3 秒、出力 64 KiB、配列の累計確保 200 万要素までです。生成した C++ 自体の実行には下記 CLI を使用します。

エディタは丸括弧・角括弧・波括弧を対で挿入します。選択中の文字は括弧で囲み、空の対の中で Backspace を押すと両側を削除します。

### NeoShowcase にデプロイ

リポジトリ直下の `Dockerfile` を使って既存の Python / C 版を配置します。
C 製変換器はイメージのビルド時に作成し、実行用イメージには C コンパイラを含めません。
非 root ユーザーで動作し、書き込み不要で起動できます。

| 項目 | 設定 |
|---|---|
| Repository | `https://github.com/fken57/strong_cp_lang` |
| Branch | `deploy/neoshowcase` |
| Deploy Type | Runtime |
| Build Type | Dockerfile |
| Dockerfile / Build Context | `Dockerfile` / `.` |
| HTTP ポート | `8080` |
| URL パス | `/` |
| アクセス制限 | なし（一般公開） |
| Database | 使用しない |

NeoShowcase でリポジトリを登録し、この設定で新しいアプリを作成します。
公開ドメインは管理画面で割り当てます。アプリ側のシークレットや DB 設定は不要です。
`FKEN_ALLOWED_HOSTS` に公開ホスト名をカンマ区切りで設定すると、そのホストに限定できます。
Docker の既定値は `*` で、割り当てた公開ホストから接続できます。POST は同一 Origin を検査します。
ローカル CLI の既定動作は引き続き `127.0.0.1` 限定です。

~~~sh
docker build -t fken-cpl .
docker run --rm --read-only --tmpfs /tmp:rw,noexec,nosuid,size=16m -p 8080:8080 fken-cpl
# 別の端末で、リバースプロキシの公開 Host / HTTPS Origin を模擬して確認
python3 scripts/smoke_web.py --host public.example
~~~

`/api/health` が起動確認用エンドポイントです。`.github/workflows/container.yml` でも
実際にイメージをビルドし、変換・補完・診断まで確認します。
アプリは fken ソースから C++ を生成します。生成したプログラムの実行は利用者の環境で行います。

### CLI

Python 3、C コンパイラ、GCC または Clang の C++20 コンパイラ、
Boost の開発用ヘッダが必要です。Ubuntu の例：

~~~sh
sudo apt-get install python3 gcc g++ libboost-dev
./fken_cpl example_bigint.fken --run

# 標準入力から N 個の整数を読み、配列・ループで処理
./fken_cpl example_sum.fken --run < example_sum.in

# 再帰、配列への参照渡し、戻り値のサンプル
./fken_cpl example_functions.fken --run
~~~

最初の実行時に変換器を `.build/` へビルドします。
この作業環境では Boost ヘッダを `.deps/boost/usr/include/` に用意済みで、
ランチャーが自動で参照します。別の場所に用意した場合は
`FKEN_BOOST_INCLUDE` でヘッダの親ディレクトリを指定できます。

~~~sh
# C++ をファイルに出力
./fken_cpl example_bigint.fken -o main.cpp

# C++ を生成せずに検査
./fken_cpl example_functions.fken --check

# その場で記述して実行
./fken_cpl --code 'int a = 9223372036854775807; print(a + 1);' --run

# 標準入力からソースを読み、C++ を標準出力へ
printf 'print(42);' | ./fken_cpl -
~~~

生成した C++ には整数型の実装も含まれます。プロジェクト固有のヘッダは不要です。
Boost がシステムにインストール済みなら、次のように単独でビルドできます。

~~~sh
g++ -std=c++20 -O2 main.cpp -o main
./main
~~~

プロジェクト内の Boost を使う場合は `-I .deps/boost/usr/include` を追加します。
`CC`、`CFLAGS`、`CXX`、`CXXFLAGS` でコンパイラと追加オプションを指定できます。
`--run` は生成したプログラムをローカルで実行します。

### LSP

~~~sh
./fken_cpl --lsp
# 同じサーバーを直接起動する場合
python3 lsp_server.py
~~~

LSP クライアントを持つエディタから stdio で起動します。単独の端末では入力待ちになります。
設定する起動コマンドの例は `python3 /home/kenta/strong_cp_lang/lsp_server.py`、
対象の拡張子は `.fken`、言語 ID は `fken_cpl` です。
VS Code では上記の専用拡張がこのサーバーを起動します。

[LSP 3.17](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/) の次の機能に対応しています。

- initialize / shutdown / exit と Content-Length による stdio 通信
- didOpen / didChange（全文・差分）/ didSave / didClose
- textDocument/completion（スニペット対応を交渉）と textDocument/hover
- publishDiagnostics と textDocument/diagnostic
- UTF-16 の位置情報、ドキュメントのバージョンによる古い更新の除外
- 拡張向けの `fken_cpl/compile`（編集中のソースを C++ に変換）

診断は CLI と同じ C 製コンパイラを使います。現在は最初のエラーを通知します。
補完は同じ字句解析器のトークンから宣言・スコープを推定するため、編集中の不完全なコードでも動作します。
補完の型情報は候補表示用の推定です。正当性はコンパイラによる診断で確認します。
ローカル版ではソースを 256 KiB、変換器の1回の処理を2秒までに制限しています。

## 構成

~~~text
fken_cpl          Python CLI：ビルド、変換、ローカル実行
compiler.py      CLI・LSP・Web で共有するビルド処理
language_service.py コンパイラによる診断、トークンを使う補完・ホバー
lsp_server.py    stdio LSP
web_server.py    ローカル HTTP サーバー
web/             Web エディタの HTML / CSS / JavaScript
editors/vscode/  VS Code 拡張、サーバー同梱・VSIX 生成、Extension Host テスト
fken_cpl.c       C++ 生成用の入口
lex.c           字句解析
parser.c        構文解析
analyze.c       変数・関数の名前解決、型・戻り値の検査
typedef.c       共通データ構造
generate_cpp.c  C++ ソース生成
runtime.hpp     生成 C++ に埋め込む整数型
generate.c      元のアセンブリ生成器
calc / calc4.c  元のアセンブリ版の入口
~~~

C++ 版でも既存のスタック位置を変数 ID として利用しています。
型や関数の導入時に必要になれば、独立したシンボル ID へ移行します。
元の `calc` は64bit固定のアセンブリ版です。新しい整数仕様を使う入口は `fken_cpl` です。

## テスト

~~~sh
python3 -m unittest test_e2e test_codegen_e2e test_cpp_e2e test_language_tools -v
gcc -std=c11 test_ast.c -o .build/test_ast
.build/test_ast
~~~

新しい E2E は、Python の整数計算との比較、64bit表現と昇格の検査、
巨大リテラル、スコープ、診断、CLI の入出力、再帰・参照渡し・関数の診断を検証します。
C++ 実行には UndefinedBehaviorSanitizer を使用します。
配列の再確保を伴う要素更新は AddressSanitizer でも検証します。
LSP は実プロセスとの通信、UTF-16 の差分更新、診断と補完の一致を検証します。
Web API のテストはローカルポートへの接続権限が必要です。

Playwright が利用できる Node.js 環境では `node test_web_ui.cjs` でブラウザの E2E も実行できます。
別の場所にある Playwright モジュールは `FKEN_PLAYWRIGHT`、既に起動した Web は `FKEN_TEST_URL` で指定します。
ブラウザ E2E は補完、テンプレート移動、診断、コピー・保存、古い結果の除外、自動保存、モバイル表示を検証します。
