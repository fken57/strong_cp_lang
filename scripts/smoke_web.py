"""Exercise the deployed container's public HTTP routes without executing user programs."""
import argparse
import json
import time
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

parser = argparse.ArgumentParser()
parser.add_argument('--url', default='http://127.0.0.1:8080')
parser.add_argument('--host', help='Public Host header used by the reverse proxy')
args = parser.parse_args()
base = args.url.rstrip('/')
if urlsplit(base).scheme not in ('http', 'https'):
    parser.error('--url must be HTTP or HTTPS')
host = args.host or urlsplit(base).netloc
origin = 'https://' + host


def request(path, data=None, *, expected=200, headers=None):
    combined = {'Host': host, **(headers or {})}
    body = None
    if data is not None:
        body = json.dumps(data).encode()
        combined.update({'Content-Type': 'application/json', 'Origin': origin})
        combined.update(headers or {})
    req = Request(base + path, data=body, headers=combined)
    try:
        with urlopen(req, timeout=5) as response:
            status, content = response.status, response.read()
    except HTTPError as error:
        status, content = error.code, error.read()
    assert status == expected, (path, status, content[:500])
    return content


until = time.monotonic() + 30
while True:
    try:
        health = json.loads(request('/api/health'))
        assert health['status'] == 'ready'
        break
    except (OSError, AssertionError):
        if time.monotonic() >= until:
            raise
        time.sleep(0.25)
assert b'id="source"' in request('/')
assert request('/app.js')
assert request('/style.css')
assert request('/runner.js')
examples = json.loads(request('/api/examples'))
graph = next(item['source'] for item in examples if item['id'] == 'graph')
plan = json.loads(request('/api/plan', {'source': graph}))
assert not plan['diagnostics'] and plan['plan']['records']
assert 'fken::Graph<false>' in json.loads(request('/api/compile', {'source': graph}))['cpp']
source = 'int[][] g(2,3); g[0].push(7); for(int i=0;i<g.size();i++){if(i==1){break;} print(g[i]);}'
compiled = json.loads(request('/api/compile', {'source': source, 'version': 4}))
assert compiled['diagnostics'] == [] and compiled['version'] == 4
assert 'int main()' in compiled['cpp'] and 'fken::push' in compiled['cpp']
invalid = json.loads(request('/api/analyze', {'source': 'int[][] g; g.push(1);'}))
assert invalid['diagnostics']
items = json.loads(request('/api/complete', {'source': 'int[][] g;\ng[0].', 'position': {'line': 1, 'character': 5}}))
assert any(item['label'] == 'push' and 'value: int)' in item['detail'] for item in items['items'])
release_source = """
char[][] labels = ["cat", "dog"];
labels[0].push_back('s');
int[] a = [2, 1, 2];
a.sort();
pair<int,int>[] runs = a.run_length();
auto [value, count] = runs[1];
graph G(2);
G.add(0, 1, 7);
auto [dist, one, two] = G.diameter;
print(labels[0]); print(a.more_than(1)); print(count + dist);
"""
compiled = json.loads(request('/api/compile', {'source': release_source}))
assert not compiled['diagnostics'] and 'fken::push_back' in compiled['cpp'] and '.diameter(' in compiled['cpp']
planned = json.loads(request('/api/plan', {'source': release_source}))
assert not planned['diagnostics'] and planned['plan']['main']
request('/api/compile', {'source': 'print(1);'}, expected=403, headers={'Origin': 'https://foreign.invalid'})
print('PASS: container health, editor assets, examples, C++ conversion, diagnostics, method completion, same-origin checks.')
