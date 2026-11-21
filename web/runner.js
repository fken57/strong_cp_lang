"use strict";

function encodeBytes(value) {
  let result = "";
  for (const byte of new TextEncoder().encode(value)) result += String.fromCharCode(byte);
  return result;
}

// Only the compiler's analyzed instruction tree is interpreted in this worker.
function execute(plan, stdin, print) {
  const functions = new Map(plan.functions.map(([name, params, body]) => [name, {params, body}]));
  const tokens = encodeBytes(stdin).match(/[^\x09-\x0d\x20]+/g) || [];
  let cursor = 0, tokenOffset = 0, remainingElements = 2000000;

  function allocate(n) {
    if (n < 0 || n > remainingElements) throw Error("配列サイズが Web 実行の上限を超えています。");
    remainingElements -= n;
  }
  const stride = 65536, edgeType = stride, graphType = 2*stride, digraphType = 3*stride;
  const mintBase = 16384 * stride, assocBase = 24576 * stride;
  const moduli = plan.moduli.map(BigInt);
  function modulus(type) { return type >= mintBase && type < assocBase ? moduli[(type - mintBase) / stride] : undefined; }
  function normalize(value, mod) {
    const remainder = value % mod;
    return remainder < 0n ? remainder + mod : remainder;
  }
  function gcd(a, b) {
    while (b) [a, b] = [b, a % b];
    return a < 0n ? -a : a;
  }
  function powMod(base, exponent, mod) {
    if (mod <= 0n || exponent < 0n) throw Error("法は正、指数は 0 以上にしてください。");
    base = normalize(base, mod);
    let result = 1n % mod;
    while (exponent) {
      if (exponent & 1n) result = result * base % mod;
      exponent >>= 1n;
      if (exponent) base = base * base % mod;
    }
    return result;
  }
  function invMod(value, mod) {
    if (mod <= 0n) throw Error("法は正にしてください。");
    let a = mod, b = normalize(value, mod), x = 0n, y = 1n;
    while (b) {
      const q = a / b;
      [a, b] = [b, a - q * b];
      [x, y] = [y, x - q * y];
    }
    if (a !== 1n) throw Error("値と法は互いに素である必要があります。");
    return normalize(x, mod);
  }
  function typedBinary(op, x, y, type) {
    const mod = modulus(type);
    if (mod === undefined) return binary(op, x, y);
    return normalize(op === "TK_SLASH" ? x * invMod(y, mod) : binary(op, x, y), mod);
  }
  function isGraph(type) { return type === graphType || type === digraphType; }
  const charType = 6 * stride, stringType = charType + 1;
  function initial(type) {
    if (type >= assocBase && type % stride === 0)
      return {groups: new Map(), info: plan.assocs[(type - assocBase) / stride], total: 0};
    if (mintBase <= type && type % stride === 0) return 0n;
    if (type === charType) return "\0";
    if (type === stringType) return "";
    if (type % stride) return [];
    if (!type) return 0n;
    if (type === edgeType) return [0n, 0n, 0n];
    if (isGraph(type)) return {rows: [], edges: 0n};
    if (type === 4 * stride) return [0n, 0n];
    if (type === 5 * stride) return [0n, 0n, 0n];
    const fields = plan.records[type / stride - 7];
    allocate(fields.length);
    return fields.map(initial);
  }
  function copy(x) {
    if (typeof x === "object" && !Array.isArray(x)) {
      if (x.groups) {
        allocate(x.groups.size);
        const groups = new Map();
        for (const [key, value] of x.groups) groups.set(key, copy(value));
        return {groups, info: x.info, total: x.total};
      }
      return {rows: copy(x.rows), edges: x.edges};
    }
    if (!Array.isArray(x)) return x;
    allocate(x.length);
    return x.map(copy);
  }
  function nextToken() {
    const value = tokens[cursor++]?.slice(tokenOffset);
    tokenOffset = 0;
    return value;
  }
  function read() {
    const token = nextToken();
    if (!token || !/^[+-]?\d+$/.test(token)) throw Error("標準入力に整数が必要です。");
    return BigInt(token);
  }
  function construct(shape, depth, element) {
    if (depth === shape.length) return initial(element);
    const n = Number(shape[depth]);
    allocate(n);
    if (depth === shape.length - 1 && element === charType) return "\0".repeat(n);
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
      case "TK_PERCENT": if (y === 0n) throw Error("0 で割ることはできません。"); return x % y;
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
  function groupCount(container, key) {
    if (!container.groups.has(key)) return 0;
    const [kind] = container.info;
    if (kind >= 4) return container.groups.get(key);
    return (kind & 2) ? container.groups.get(key).length : 1;
  }
  function assocIndex(container, key) {
    const [kind, , valueType] = container.info;
    if (kind >= 4) return BigInt(groupCount(container, key));
    if (kind >= 2) return copy(container.groups.get(key) ?? initial(valueType + 1));
    if (!container.groups.has(key)) {
      allocate(1);
      container.groups.set(key, initial(valueType));
    }
    return container.groups.get(key);
  }
  function assocMethod(container, name, values) {
    const [kind, keyType, valueType] = container.info;
    const [key, value] = values, groups = container.groups;
    const multi = Boolean(kind & 2);
    switch (name) {
      case "size": return BigInt(multi ? container.total : groups.size);
      case "count": return BigInt(groupCount(container, key));
      case "contains": return BigInt(groups.has(key));
      case "erase": {
        const removed = groupCount(container, key);
        groups.delete(key);
        if (multi) container.total -= removed;
        return BigInt(removed);
      }
      case "keys": {
        allocate(groups.size);
        const keys = [...groups.keys()];
        if (!(kind & 1)) keys.sort((a, b) => a < b ? -1 : a > b ? 1 : 0);
        return keyType === charType ? keys.join("") : keys;
      }
      case "insert":
        if (multi || !groups.has(key)) allocate(1);
        if (kind >= 4) groups.set(key, multi ? groupCount(container, key) + 1 : 1);
        else if (!multi) groups.set(key, value);
        else {
          const bucket = groups.get(key) ?? initial(valueType + 1);
          if (valueType === charType) groups.set(key, bucket + value);
          else { bucket.push(value); groups.set(key, bucket); }
        }
        if (multi) container.total++;
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
    if (parent.get().groups) {
      const value = parent.get();
      if (value.info[0] < 2) assocIndex(value, i);
      return {get: () => assocIndex(parent.get(), i), set: v => parent.get().groups.set(i, v)};
    }
    function position() { const value = parent.get(); const a = value.rows ?? value; return [a, index(a, i)]; }
    position();
    return {
      get() { const [a, k] = position(); return a[k]; },
      set(v) {
        const [a, k] = position();
        if (typeof a === "string") parent.set(a.slice(0, k) + v + a.slice(k + 1));
        else a[k] = v;
      }
    };
  }
  function reference(node, env) {
    if (node[0] === "var" || node[0] === "index" || node[0] === "field") return location(node, env);
    return cell(expr(node, env));
  }
  function cell(value) {
    return {get: () => value, set: next => { value = next; }};
  }
  function diameter(graph) {
    const n = graph.rows.length;
    if (!n || graph.edges !== BigInt(n - 1)) throw Error("直径を求めるには空でない無向木が必要です。");
    function farthest(start) {
      allocate(2 * n);
      const distances = Array(n).fill(-1n);
      const order = [start];
      distances[start] = 0n;
      let best = start;
      for (const u of order) {
        for (const [to, cost] of graph.rows[u]) {
          if (cost < 0n) throw Error("直径を求めるには辺の重みが非負である必要があります。");
          const v = Number(to);
          if (distances[v] >= 0n) continue;
          distances[v] = distances[u] + cost;
          order.push(v);
          if (best === start || distances[v] > distances[best]) best = v;
        }
      }
      if (order.length !== n) throw Error("直径を求めるには連結な木が必要です。");
      return [best, distances[best]];
    }
    const [one] = farthest(0);
    const [two, dist] = farthest(one);
    allocate(3);
    return [dist, BigInt(one), BigInt(two)];
  }
  function breadthFirst(rows, source) {
    allocate(2 * rows.length);
    const distance = Array(rows.length).fill(-1n), queue = [index(rows, source)];
    distance[queue[0]] = 0n;
    for (const u of queue) {
      for (const [to] of rows[u]) {
        const v = Number(to);
        if (distance[v] !== -1n) continue;
        distance[v] = distance[u] + 1n;
        queue.push(v);
      }
    }
    return distance;
  }
  function components(rows) {
    allocate(2 * rows.length);
    const result = Array(rows.length).fill(-1n);
    let id = 0n;
    for (let start = 0; start < rows.length; start++) {
      if (result[start] !== -1n) continue;
      const queue = [start];
      result[start] = id;
      for (const u of queue) {
        for (const [to] of rows[u]) {
          const v = Number(to);
          if (result[v] !== -1n) continue;
          result[v] = id;
          queue.push(v);
        }
      }
      id++;
    }
    return result;
  }
  function dijkstra(rows, source) {
    allocate(rows.length);
    const start = index(rows, source), distance = Array(rows.length).fill(-1n);
    const heap = [];
    function push(cost, vertex) {
      allocate(2);
      let i = heap.length;
      const item = [cost, vertex];
      heap.push(item);
      while (i) {
        const parent = Math.floor((i - 1) / 2);
        if (heap[parent][0] <= cost) break;
        heap[i] = heap[parent];
        i = parent;
      }
      heap[i] = item;
    }
    function pop() {
      const first = heap[0], tail = heap.pop();
      if (!heap.length) return first;
      let i = 0;
      while (2*i + 1 < heap.length) {
        let child = 2*i + 1;
        if (child + 1 < heap.length && heap[child + 1][0] < heap[child][0]) child++;
        if (heap[child][0] >= tail[0]) break;
        heap[i] = heap[child];
        i = child;
      }
      heap[i] = tail;
      return first;
    }
    distance[start] = 0n;
    push(0n, start);
    while (heap.length) {
      const [cost, u] = pop();
      if (distance[u] !== cost) continue;
      for (const [to, weight] of rows[u]) {
        if (weight < 0n) throw Error("dijkstra には非負の辺重みが必要です。");
        const v = Number(to), candidate = cost + weight;
        if (distance[v] !== -1n && candidate >= distance[v]) continue;
        distance[v] = candidate;
        push(candidate, v);
      }
    }
    return distance;
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
      if (assocBase <= rank && rank % stride === 0) return assocMethod(a, name, values);
      const mod = modulus(rank);
      if (mod !== undefined) return name === "val" ? a : powMod(a, values[0], mod);
      if (isGraph(rank)) {
        if (name === "bfs") return breadthFirst(a.rows, values[0]);
        if (name === "dijkstra") return dijkstra(a.rows, values[0]);
        if (name === "components") return components(a.rows);
        if (name === "diameter") return diameter(a);
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
      if (rank === stringType) {
        switch (name) {
          case "size": return BigInt(a.length);
          case "clear": ref.set(""); return;
          case "push":
          case "push_back": allocate(1); ref.set(a + values[0]); return;
          case "back":
          case "pop":
            if (!a.length) throw Error("文字列が空です。");
            if (name === "pop") ref.set(a.slice(0, -1));
            return a.at(-1);
          case "resize": {
            const n = Number(values[0]);
            if (n < 0) throw Error("文字列の長さは 0 以上にしてください。");
            const added = Math.max(0, n - a.length);
            allocate(added);
            ref.set(a.slice(0, n) + (values[1] ?? "\0").repeat(added));
            return;
          }
        }
      }
      switch (name) {
        case "sort": a.sort((x, y) => x < y ? -1 : x > y ? 1 : 0); return;
        case "at_least":
        case "more_than":
        case "at_most":
        case "less_than": {
          const strict = name === "more_than" || name === "at_most";
          const last = name === "at_most" || name === "less_than";
          const [x] = values;
          let low = 0, high = a.length;
          while (low < high) {
            const mid = low + Math.floor((high - low) / 2);
            if (a[mid] < x || (strict && a[mid] === x)) low = mid + 1;
            else high = mid;
          }
          return BigInt(last ? low - 1 : low);
        }
        case "run_length": {
          const runs = [];
          for (const value of a) {
            const last = runs.at(-1);
            if (last && last[0] === value) last[1]++;
            else {
              allocate(3);
              runs.push([value, 1n]);
            }
          }
          return runs;
        }
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
    if (name === "read_string") {
      const token = nextToken();
      if (!token) throw Error("文字列の標準入力が必要です。");
      return token;
    }
    if (name === "read_char") {
      const token = tokens[cursor];
      if (!token) throw Error("文字の標準入力が必要です。");
      const ch = token[tokenOffset++];
      if (tokenOffset === token.length) { cursor++; tokenOffset = 0; }
      return ch;
    }
    if (name === "gcd") return gcd(...values);
    if (name === "lcm") {
      const [a, b] = values;
      const value = a && b ? a / gcd(a, b) * b : 0n;
      return value < 0n ? -value : value;
    }
    if (name === "pow_mod") return powMod(...values);
    if (name === "inv_mod") return invMod(...values);
    const n = Number(values[0]);
    allocate(n);
    if (name === "read_vec") return Array.from({length: n}, read);
    if (name === "array") return Array(n).fill(values[1] ?? 0n);
    throw Error("未対応の関数です。");
  }
  function expr(node, env) {
    switch (node[0]) {
      case "residue": return normalize(expr(node[2], env), modulus(node[1]));
      case "text": return node[1];
      case "int": return BigInt(node[1]);
      case "var": case "index": case "field": return copy(location(node, env).get());
      case "neg": {
        const result = -expr(node[1], env), mod = modulus(node[2]);
        return mod === undefined ? result : normalize(result, mod);
      }
      case "arrayLiteral": {
        allocate(node[2].length);
        const values = node[2].map(e => expr(e, env));
        return node[1] === stringType ? values.join("") : values;
      }
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
      default: return typedBinary(node[0], expr(node[1], env), expr(node[2], env), node[3]);
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
        ref.set(s[1] === "TK_ASSIGN" ? rhs : typedBinary(op, ref.get(), rhs, s[4]));
        break;
      }
      case "write": writeValue(expr(s[1], env)); break;
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
      if (output.length + line.length > 65536) throw Error("出力が 64 KiB を超えました。");
      output += line;
    });
  } catch (e) { error = e.message; }
  const bytes = Uint8Array.from(output, c => c.charCodeAt(0));
  self.postMessage({output: new TextDecoder("utf-8", {ignoreBOM: true}).decode(bytes), error});
};
