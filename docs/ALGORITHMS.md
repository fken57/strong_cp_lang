# アルゴリズム

言語の宣言・型・入出力は [基礎仕様](SPEC.md) を参照してください。

## 整数配列の探索とランレングス圧縮

`a.sort()` は `int[]` をその場で昇順に並べ替えます。以下の探索は昇順の `int[]` に対して二分探索を行い、0 始まりの添字を返します。

| メソッド | 返す添字 | 該当なし |
|---|---|---|
| `a.at_least(x)` | `x` 以上の最初 | `a.size()` |
| `a.more_than(x)` | `x` より大きい最初 | `a.size()` |
| `a.at_most(x)` | `x` 以下の最後 | `-1` |
| `a.less_than(x)` | `x` 未満の最後 | `-1` |

`a.run_length()` は元の順序で隣接する同値をまとめ、`pair<int,int>[]` を返します。
各ペアの `.first` が値、`.second` が連続回数です。空配列からは空配列が返ります。

```fken
int[] a = [3, 3, 1, 3];
pair<int,int>[] runs = a.run_length();
for (pair<int,int> run : runs) {
    cout << run.first << ' ' << run.second << '\n';
}
// 3 2
// 1 1
// 3 1
```

## グラフ型

`graph` は無向、`digraph` は有向で、頂点番号は 0 始まりです。`add(u,v,w)` は 0 始まりの辺 ID を返し、重みの既定値は 1 です。
`G.size()` は頂点数、`G.edge_count()` は辺数、`G[u]` は追加順の読み取り専用隣接列です。
無向辺の両端から同じ ID が見えます。多重辺は異なる ID を持ち、無向の自己ループは隣接列に二度現れます。
範囲 for は開始時の列をコピーし、各要素を値で受け取ります。
Web エディタのサンプル「構造体と重み付きグラフ」で実行できます。

```fken
int n, m;
cin >> n >> m;
graph G(n);
rep(i, 0, m) {
    int u, v, w;
    cin >> u >> v >> w;
    G.add(u - 1, v - 1, w);
}
if (n > 0) {
    for (edge e : G[0]) {
        cout << e.to + 1 << ' ' << e.cost << '\n';
    }
}
```

### 木の直径

```fken
graph G(3);
G.add(0, 1, 5);
G.add(1, 2, 8);

auto cnt = G.diameter;
print(cnt.dist);  // 13

auto [diameter_dist, diameter_one, diameter_two] = G.diameter;
cout << diameter_dist << ' ' << diameter_one << ' ' << diameter_two << '\n';  // 13 2 0
```

`G.diameter` は非負の重みを持つ空でない無向木に対して、直径の距離と 0 始まりの端点を
`.dist`、`.one`、`.two` の順で返します。1 頂点の結果は距離 0、両端点 0 です。
候補が複数ある場合はそのうち一つを返します。各アクセスは頂点と辺を線形時間で走査します。

Web エディタと VS Code では、`auto cnt = G.diameter;` の末尾で補完を開き「分解して展開」を選ぶと、
`auto [diameter_dist, diameter_one, diameter_two] = G.diameter;` に置き換えます。
構造体とペアの宣言でも同じ操作を使えます。
