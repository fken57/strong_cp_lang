# fken_cpl

入力・配列・ループ・関数を使って競技プログラミングの問題を記述し、C++20 に変換する言語です。
CLI、VS Code 拡張、stdio LSP、補完・リアルタイム診断付きのローカル Web エディタを利用できます。

[fken57/calc_compile](https://github.com/fken57/calc_compile) の
[00c7607](https://github.com/fken57/calc_compile/commit/00c76076a1e222522b2948464559de71d021911c)
を土台にしています。元の C 製フロントエンドとアセンブリ生成器を引き継ぎ、
C++ 生成器と自動昇格する整数型を追加しています。

## 動かす

### VS Code 拡張

`.build/fken-cpl-0.6.0.vsix` を VS Code の **VSIX からのインストール…** で開きます。
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
生成コードの実行には下記 CLI を使用します。

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

## 現在の言語

~~~text
int n = read_int();
int[] a = read_vec(n);
int total = 0;
for (int i = 0; i < n; i++) {
    total += a[i];
}
print(total);
~~~

- 変数宣言・代入、整数リテラル、`+ - * /`、単項 `-`
- 比較 `== != < <= > >=`（結果は整数の `0` または `1`）
- `print`、`if / else`、`while`、C 形式の `for`、`rep`、`break`、ブロック、`//` コメント
- 整数配列リテラル `[1, 2, 3]`、添字アクセスと要素更新 `a[i] = value`
- 更新文 `i++`、`i--`、`++i`、`--i`、`i += 2`、`i -= 2`、`i *= 2`、`i /= 2`
- ブロック内の変数による外側の変数の隠蔽。同一スコープの再宣言はエラー
- 初期化式は新しい変数を登録する前に評価
- 行・列を含むコンパイルエラー
- `fn` による関数定義、引数・戻り値、再帰・相互再帰、参照渡し

探索やデータ構造の競プロ用 API は次段階です。

### 宣言と可変長配列

~~~text
int x;                  // 0
int[] a;                // 空の一次元 vector
int[] values(3);        // [0, 0, 0]
int[][] grid(2, 3);     // 2行×3列、0初期化
int[][][] cube(2, 3, 4);
int[][] initial = [[1, 2], [3]];

a.push(10);
grid[0].push(7);        // この行だけ長くする
grid.push([4, 5]);      // 行を追加
print(grid[0][3]);       // 7
~~~

`[]` の数が次元数、`(...)` が各次元の初期サイズです。サイズは実行時の整数式で指定でき、
左から一度ずつ評価します。コンストラクタに指定するサイズ数は次元数と一致させます。
サイズ指定を省略すると外側が空になります。`int[] a[n];`、`int[][] a[n][m];` も
初期サイズ指定として受け付けます。初期サイズは固定長の制約にはなりません。

| メソッド | 戻り値・動作 |
|---|---|
| `a.push(value)` | 末尾へ追加。戻り値は `void` |
| `a.pop()` | 末尾を削除して返す |
| `a.back()` | 末尾の値をコピーして返す |
| `a.size()` | この次元の要素数を `int` で返す |
| `a.resize(n[, value])` | サイズ変更。追加要素は整数なら0、配列なら空。第2引数で指定も可能 |
| `a.clear()` | 全要素を削除。戻り値は `void` |

多次元配列の要素は一つ内側の配列です。たとえば `int[][]` への `push` は `int[]` を渡します。
`pop`・`back` もその要素型を返します。更新メソッドは変数またはその添字要素に対して呼び出します。
空の配列への `pop`・`back`、負や大きすぎるサイズ、範囲外アクセスは実行時エラーです。
`print` は一次元を空白区切り、多次元は内側の配列を角括弧で囲んで出力します。

`let` は後方互換用の型推論宣言として警告なしで受け付けます。
既存の `let a = [1, 2];` や `for (let i = 0; ... )` も利用できます。
新しいサンプル・補完は型付き宣言を基本にします。

~~~sh
./fken_cpl example_vectors.fken --run
~~~

### 関数

~~~text
fn factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}
print(factorial(30));

fn add_to_all(a: &int[], delta) -> void {
    rep(i, 0, len(a)) { a[i] += delta; }
}
int[] a = [1, 2, 3];
add_to_all(a, 10);
print(a); // 11 12 13
~~~

引数と戻り値の型を省略すると `int` になります。`int` は64bitから自動昇格する整数型です。
多次元も `a: int[][]`、`-> int[][]` のように表記します。
配列の引数は `a: int[]`、配列の戻り値は `-> int[]`、値を返さない関数は `-> void` と書きます。
整数・配列とも通常は値渡しです。呼び出し元を更新する場合や配列のコピーを避ける場合は
`x: &int` や `a: &int[]` を指定します。参照引数には変数名を渡します。
配列要素や計算結果を参照引数として渡す構文は未対応です。

~~~text
fn clone(a: int[]) -> int[] { a[0] += 1; return a; }
int[] b = clone(a); // a は変更されず、b に変更後の配列が入る
~~~

関数定義はトップレベルに置きます。定義より前から呼び出すことができ、相互再帰も可能です。
関数のスコープは引数と関数内のローカル変数です。必要な外部データは引数で渡します。
引数は左から右に評価し、値渡しの引数はその時点の値を保持します。
関数呼び出しは単独の文や `for` の初期化・更新節でも使えます。

値を返す関数には、すべての経路で `return 値;` が必要です。
戻り値の検査では、ループだけを根拠に必ず返るとは判定しません。
`void` 関数は末尾まで実行するか `return;` で終了します。
引数の数・型、戻り値の型、戻り値の不足、関数名の重複は変換時に検出します。
組み込み関数名は再定義できません。

### for と rep

~~~text
for (int i = 0; i < n; i++) { print(a[i]); }
for (int i = n - 1; i >= 0; i -= 2) { print(a[i]); }

// for (int i = 0; i < n; i++) と同じ
rep(i, 0, n) { print(a[i]); }
~~~

`for` の初期化・条件・更新はそれぞれ省略できます。条件を省略すると真になります。
条件は毎回評価し、更新は各周回の末尾に実行します。
`for` の初期化に宣言した変数と `rep` の変数は、そのループ内だけで有効です。
`rep(i, start, end)` は `for (int i = start; i < end; i++)` に展開するため、
終点は含まず、終点の式は毎回評価します。
更新文は通常の文と `for` の更新節で使えます。配列要素にも `a[i]++` や `a[i] += 2` を使えます。
更新文は値を返す式にはまだ対応していません。

`break;` は最も内側の `for`・`while`・`rep` を終了します。
`for` の更新節は実行しません。ループ外の `break` はコンパイルエラーです。

### 入力と配列

| 関数 | 動作 |
|---|---|
| `read_int()` | 空白区切りの整数を一つ読む。符号・巨大整数に対応 |
| `read_vec(n)` | 整数を n 個読んで配列にする |
| `array(n)` | 長さ n の配列を 0 で初期化 |
| `array(n, value)` | 長さ n の配列を value で初期化 |
| `len(a)` | 配列の長さを整数で返す |

配列は整数を末端の要素とする可変長配列です。`int[] b = a;` や `b = a;` は値をコピーします。
多次元も内側までコピーします。代入する配列の次元数は一致させます。
配列変数には別の長さの整数配列を再代入できます。`print(a)` は要素を空白区切りで出力します。
添字は 0 始まりで検査されます。負の長さ・範囲外の添字・不足した入力・不正な整数は実行時エラーです。
整数と配列の取り違えや、組み込み関数の引数の誤りは変換時に検出します。
式は左から右に評価するため、`read_int() - read_int()` も入力順を維持します。

実行時の入力にはソースファイルか `--code` と標準入力を組み合わせます。
`-` を指定すると標準入力全体をソースとして読み込みます。

## int の仕様

利用者には一つの `int` として見えます。

- 値が符号付き64bitに収まる間は `int64_t` で保持・計算します。
- 加算・減算・乗算のオーバーフローを安全に検出し、
  元のオペランドから Boost.Multiprecision の `cpp_int` で再計算します。
- `-INT64_MIN`、`INT64_MIN / -1` も自動昇格します。
- 巨大リテラルは桁を失わず読み込みます。先頭が `0` でも十進数です。
- 結果が64bitに収まれば、64bit表現に戻します。
- 除算はゼロ方向への切り捨てです。ゼロ除算は実行時エラーです。

タグの確認とオーバーフロー検出の分、通常の C++ の `int64_t` 演算にはないコストがあります。
任意精度演算のコストは桁数にも依存します。

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
