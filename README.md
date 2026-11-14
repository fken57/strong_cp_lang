# fken_cpl

競技プログラミング向けの言語を C++20 に変換する、ローカル版の最初の実装です。

[fken57/calc_compile](https://github.com/fken57/calc_compile) の
[00c7607](https://github.com/fken57/calc_compile/commit/00c76076a1e222522b2948464559de71d021911c)
を土台にしています。元の C 製フロントエンドとアセンブリ生成器を引き継ぎ、
C++ 生成器と自動昇格する整数型を追加しています。

## 動かす

Python 3、C コンパイラ、GCC または Clang の C++20 コンパイラ、
Boost の開発用ヘッダが必要です。Ubuntu の例：

~~~sh
sudo apt-get install python3 gcc g++ libboost-dev
./fken_cpl example_bigint.fken --run
~~~

最初の実行時に変換器を `.build/` へビルドします。
この作業環境では Boost ヘッダを `.deps/boost/usr/include/` に用意済みで、
ランチャーが自動で参照します。別の場所に用意した場合は
`FKEN_BOOST_INCLUDE` でヘッダの親ディレクトリを指定できます。

~~~sh
# C++ をファイルに出力
./fken_cpl example_bigint.fken -o main.cpp

# その場で記述して実行
./fken_cpl --code 'let a = 9223372036854775807; print(a + 1);' --run

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

## 現在の言語

~~~text
let a = 1;
let b = 2;
while (a < 5) {
    if (a == 2) {
        print(b);
    } else {
        print(a);
    }
    a = a + 1;
}
~~~

- 変数宣言・代入、整数リテラル、`+ - * /`、単項 `-`
- 比較 `== != < <= > >=`（結果は整数の `0` または `1`）
- `print`、`if / else`、`while`、ブロック、`//` コメント
- ブロック内の変数による外側の変数の隠蔽。同一スコープの再宣言はエラー
- 初期化式は新しい変数を登録する前に評価
- 行・列を含むコンパイルエラー

配列、標準入力を読む関数、ユーザー定義関数、競プロ用 API、Web エディタは次段階です。

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
fken_cpl.c       C++ 生成用の入口
lex.c           字句解析
parser.c        構文解析
analyze.c       変数束縛・スコープの解析
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
python3 -m unittest test_e2e test_codegen_e2e test_cpp_e2e -v
gcc -std=c11 test_ast.c -o .build/test_ast
.build/test_ast
~~~

新しい E2E は、Python の整数計算との比較、64bit表現と昇格の検査、
巨大リテラル、スコープ、診断、CLI の入出力を検証します。
C++ 実行には UndefinedBehaviorSanitizer を使用します。
