# fken_cpl for VS Code

`.fken` のシンタックスハイライト、補完、リアルタイム診断、ホバー、C++20 への変換を提供します。
同梱した Python LSP と C 製変換器を使用するため、リポジトリの別途クローンは不要です。

## インストールと使用

1. 実行する環境に Python 3.9 以降と GCC / Clang の C コンパイラを用意します。
   Ubuntu / WSL は `sudo apt-get install python3 gcc`。
2. VS Code の拡張機能メニューから **VSIX からのインストール…** を選び、`fken-cpl-0.8.0.vsix` を開きます。
   Remote WSL を使う場合は、WSL に接続したウィンドウでインストールします。
3. 信頼したフォルダーで `main.fken` を作成します。初回は C 製変換器を自動ビルドします。
4. Ctrl+Space で補完、マウスを重ねて型・説明を表示します。診断は問題パネルにも表示されます。
5. 右上の変換ボタン、コマンド **fken_cpl: C++ に変換**、または Ctrl+Alt+C（macOS は Cmd+Alt+C）で C++ を開きます。

```text
fn double(n) { return n * 2; }
int answer = 0;
rep(i, 0, 5) { answer += double(i); }
print(answer);
int[][] grid(2, 3);
grid[0].push(answer);
```

変換は未保存の編集も反映します。生成結果は保存前の新しい C++ タブに開きます。
元のファイルや既存の `.cpp` は上書きしません。ソースの検査・変換だけなら Boost と C++ コンパイラは不要です。
生成した C++ の実行には C++20 コンパイラと Boost 開発用ヘッダが必要です。

現在の動作確認対象は Linux / Remote WSL です。Windows で使う場合は Remote WSL を推奨します。
ブラウザー版 VS Code には対応していません。

## 設定とトラブルシューティング

設定で `fkenCpl` を検索できます。

| 設定 | 用途 |
|---|---|
| `fkenCpl.pythonPath` | Python 3 実行ファイルの名前またはパス。既定は `python3`、Windows は `python` |
| `fkenCpl.cCompilerPath` | GCC / Clang の実行ファイルの名前またはパス。空なら環境変数 `CC` または `cc` |
| `fkenCpl.serverPath` | 開発用 `lsp_server.py` の絶対パス。空なら同梱版 |
| `fkenCpl.trace.server` | LSP 通信ログの詳細度 |

実行ファイルの設定に引用符・引数は含めません。Remote WSL では WSL 内のパスを指定します。
設定変更でサーバーを再起動します。手動では **fken_cpl: 言語サーバーを再起動** を使えます。
**fken_cpl: 出力ログを表示** から起動エラーを確認できます。
変換器のキャッシュは VS Code の拡張ストレージに保存します。

`.` を入力すると受け手の次元に応じた `push` / `pop` / `size` などの補完を表示します。
`int[]`・`int[][]` 宣言、多次元の添字、`break` に対応し、旧 `let` も引き続き使えます。

診断・補完は CLI / Web と同じ言語サービスです。診断は最初のエラーを表示し、
補完の型情報は編集中のトークンからの推定です。1 ファイルは 256 KiB、変換処理は 2 秒までです。

## 開発

このディレクトリで Node.js 22 以降と Python 3 を使います。

```sh
npm ci
npm run bundle-server
# このディレクトリを VS Code で開いて F5、または
npm test
npm run package
```

`npm test` は VS Code をダウンロードし、分離したユーザーデータで Extension Host を起動します。
Linux は GUI セッションまたは Xvfb が必要です。
`npm run package` は共有サーバーの最新ソースをコピーし、リポジトリの `.build/` に VSIX を生成します。
`server/` は生成物です。修正はリポジトリ直下の Python / C ファイルに行います。

VSIX のローカル配布を想定しています。Marketplace への公開とライセンス設定は別途行います。
