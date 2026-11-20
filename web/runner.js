"use strict";

// Only the compiler's analyzed instruction tree is interpreted in this worker.
function execute(plan, stdin, print) {
  const functions = new Map(plan.functions.map(([name, params, body]) => [name, {params, body}]));
  const tokens = stdin.match(/\S+/g) || [];
  let cursor = 0, remainingElements = 2000000;

  function allocate(n) {
    if (n < 0 || n > remainingElements) throw Error("配列サイズが Web 実行の上限を超えています。");
    remainingElements -= n;
  }
  const stride = 65536, edgeType = stride, graphType = 2*stride, digraphType = 3*stride;
  function isGraph(type) { return type === graphType || type === digraphType; }
  function initial(type) {
    if (type % stride) return [];
    if (!type) return 0n;
    if (type === edgeType) return [0n, 0n, 0n];
    if (isGraph(type)) return {rows: [], edges: 0n};
    const fields = plan.records[type / stride - 4];
    allocate(fields.length);
    return fields.map(initial);
  }
  function copy(x) {
    if (typeof x === "object" && !Array.isArray(x)) return {rows: copy(x.rows), edges: x.edges};
    if (!Array.isArray(x)) return x;
    allocate(x.length);
    return x.map(copy);
  }
  function read() {
    const token = tokens[cursor++];
    if (!token || !/^[+-]?\d+$/.test(token)) throw Error("標準入力に整数が必要です。");
    return BigInt(token);
  }
  function construct(shape, depth, element) {
    if (depth === shape.length) return initial(element);
    const n = Number(shape[depth]);
    allocate(n);
    return Array.from({length: n}, () => construct(shape, depth + 1, element));
  }
  function index(a, i) {
    if (i < 0n || i >= BigInt(a.length)) throw Error("配列の添字が範囲外です。");
    return Number(i);
  }
  function binary(op, x, y) {
    switch (op) {
      case "TK_PLUS": return x + y;
      case "TK_MINUS": return x - y;
      case "TK_STAR": return x * y;
      case "TK_SLASH": if (y === 0n) throw Error("0 で割ることはできません。"); return x / y;
      case "TK_LT": return BigInt(x < y);
      case "TK_LE": return BigInt(x <= y);
      case "TK_GT": return BigInt(x > y);
      case "TK_GE": return BigInt(x >= y);
      case "TK_EQ": return BigInt(x === y);
      case "TK_NE": return BigInt(x !== y);
      default: throw Error("未対応の演算です。");
    }
  }
  // Capture indices once, then resolve against the latest root after side effects.
  function location(node, env) {
    if (node[0] === "var") return env.get(node[1]);
    if (node[0] === "field") {
      const ref = reference(node[2], env);
      return {get: () => ref.get()[node[1]], set: v => { ref.get()[node[1]] = v; }};
    }
    const parent = reference(node[1], env);
    const i = expr(node[2], env);
    function position() { const value = parent.get(); const a = value.rows ?? value; return [a, index(a, i)]; }
    position();
    return {
      get() { const [a, k] = position(); return a[k]; },
      set(v) { const [a, k] = position(); a[k] = v; }
    };
  }
  function reference(node, env) {
    if (node[0] === "var" || node[0] === "index" || node[0] === "field") return location(node, env);
    return cell(expr(node, env));
  }
  function cell(value) {
    return {get: () => value, set: next => { value = next; }};
  }
  function call([, name, receiver, args, rank], env) {
    const fn = functions.get(name);
    if (fn) {
      const local = new Map();
      const self = receiver ? reference(receiver, env) : null;
      fn.params.forEach(([slot, byRef], i) => {
        if (self && !i) { local.set(slot, self); return; }
        if (self) i--;
        local.set(slot, byRef ? reference(args[i], env) : cell(expr(args[i], env)));
      });
      return statement(fn.body, local)?.value;
    }
    if (receiver) {
      const ref = reference(receiver, env);
      const values = args.map(arg => expr(arg, env));
      const a = ref.get();
      if (isGraph(rank)) {
        if (name === "size") return BigInt(a.rows.length);
        if (name === "edge_count") return a.edges;
        const [u, v, cost = 1n] = values;
        const from = index(a.rows, u), to = index(a.rows, v);
        allocate(rank === graphType ? 8 : 4);
        const id = a.edges++;
        a.rows[from].push([v, cost, id]);
        if (rank === graphType) a.rows[to].push([u, cost, id]);
        return id;
      }
      switch (name) {
        case "size": return BigInt(a.length);
        case "clear": a.length = 0; return;
        case "push": allocate(1); a.push(values[0]); return;
        case "pop":
        case "back":
          if (!a.length) throw Error("空の配列に " + name + " は使えません。");
          return name === "pop" ? a.pop() : copy(a[a.length - 1]);
        case "resize": {
          const n = Number(values[0]);
          if (n < 0) throw Error("配列サイズは 0 以上にしてください。");
          allocate(Math.max(0, n - a.length));
          const fill = values[1] ?? initial(rank - 1);
          while (a.length < n) a.push(copy(fill));
          a.length = n; return;
        }
      }
    }
    if (name === "len") return BigInt(reference(args[0], env).get().length);
    const values = args.map(arg => expr(arg, env));
    if (name === "read_int") return read();
    const n = Number(values[0]);
    allocate(n);
    if (name === "read_vec") return Array.from({length: n}, read);
    if (name === "array") return Array(n).fill(values[1] ?? 0n);
    throw Error("未対応の関数です。");
  }
  function expr(node, env) {
    switch (node[0]) {
      case "int": return BigInt(node[1]);
      case "var": case "index": case "field": return copy(location(node, env).get());
      case "neg": return -expr(node[1], env);
      case "arrayLiteral": allocate(node[1].length); return node[1].map(e => expr(e, env));
      case "record": {
        const fields = initial(node[1]);
        for (const [i, value] of node[2]) fields[i] = expr(value, env);
        return fields;
      }
      case "construct": {
        if (node[1] === 0) return 0n;
        const shape = node[2].map(e => expr(e, env));
        if (shape.some(n => n < 0n)) throw Error("配列サイズは 0 以上にしてください。");
        if (isGraph(node[1])) return {rows: construct(shape, 0, 1), edges: 0n};
        return shape.length ? construct(shape, 0, node[1] - node[1] % stride) : initial(node[1]);
      }
      case "call": return call(node, env);
      default: return binary(node[0], expr(node[1], env), expr(node[2], env));
    }
  }
  function writeValue(value) {
    if (!Array.isArray(value)) { print(String(value)); return; }
    value.forEach((item, i) => {
      if (i) print(" ");
      if (Array.isArray(item)) print("[");
      writeValue(item);
      if (Array.isArray(item)) print("]");
    });
  }
  function statement(s, env) {
    if (!s) return;
    switch (s[0]) {
      case "block":
        for (let i = 1; i < s.length; ++i) {
          const flow = statement(s[i], env);
          if (flow) return flow;
        }
        break;
      case "let": env.set(s[1], cell(expr(s[2], env))); break;
      case "assign": {
        const ref = location(s[2], env);
        const rhs = s[3] ? expr(s[3], env) : 1n;
        const op = {TK_INC:"TK_PLUS", TK_DEC:"TK_MINUS"}[s[1]] || s[1].replace("_ASSIGN", "");
        ref.set(s[1] === "TK_ASSIGN" ? rhs : binary(op, ref.get(), rhs));
        break;
      }
      case "write": if (s[2]) writeValue(expr(s[2], env)); else print(String.fromCharCode(s[1])); break;
      case "print": writeValue(expr(s[1], env)); print("\n"); break;
      case "expr": expr(s[1], env); break;
      case "if": return statement(expr(s[1], env) ? s[2] : s[3], env);
      case "break": return {stop: true};
      case "return": return {value: s[1] ? expr(s[1], env) : undefined};
      case "each":
        for (const item of expr(s[2], env)) {
          env.set(s[1], cell(copy(item)));
          const flow = statement(s[3], env);
          if (flow?.stop) break;
          if (flow) return flow;
        }
        break;
      case "loop":
        statement(s[1], env);
        while (!s[2] || expr(s[2], env)) {
          const flow = statement(s[4], env);
          if (flow?.stop) break;
          if (flow) return flow;
          statement(s[3], env);
        }
    }
  }
  statement(plan.main, new Map());
}

self.onmessage = ({data}) => {
  let output = "", error = "";
  try {
    execute(data.plan, data.stdin, line => {
      if (output.length + line.length > 65536) throw Error("出力が 64 Ki 文字を超えました。");
      output += line;
    });
  } catch (e) { error = e.message; }
  self.postMessage({output, error});
};
