const assert = require('node:assert/strict');
const vscode = require('vscode');
const fs = require('node:fs/promises');

async function waitFor(check, description) {
    const until = Date.now() + 20000;
    while (Date.now() < until) {
        const value = await check();
        if (value) return value;
        await new Promise(resolve => setTimeout(resolve, 100));
    }
    throw new Error(`Timed out: ${description}`);
}

async function replace(document, text) {
    const edit = new vscode.WorkspaceEdit();
    edit.replace(document.uri, new vscode.Range(new vscode.Position(0, 0), document.positionAt(document.getText().length)), text);
    assert.equal(await vscode.workspace.applyEdit(edit), true);
}

exports.run = async function () {
    const uri = vscode.Uri.joinPath(vscode.workspace.workspaceFolders[0].uri, 'main.fken');
    const document = await vscode.workspace.openTextDocument(uri);
    await vscode.window.showTextDocument(document);
    assert.equal(document.languageId, 'fken_cpl');
    const extension = vscode.extensions.getExtension('fken57.fken-cpl');
    assert.ok(extension, 'extension is installed');
    await waitFor(() => extension.isActive, 'automatic language activation');
    await extension.exports.ready;
    const errors = () => vscode.languages.getDiagnostics(uri).filter(item => item.source === 'fken_cpl');
    const prefix = '// 日本語🙂\nfn double(n) { return n * 2; }\nint amount = 21;\n';

    await replace(document, `${prefix}rea`);
    const completions = await waitFor(async () => {
        const list = await vscode.commands.executeCommand('vscode.executeCompletionItemProvider', uri, new vscode.Position(3, 3));
        return list?.items.find(item => item.label === 'read_vec') && list;
    }, 'compiler-backed completions');
    assert.ok(completions.items.some(item => item.label === 'read_int'));
    assert.match(completions.items.find(item => item.label === 'read_vec').detail, /int\[\]/);

    await replace(document, `${prefix}print(missing);`);
    await waitFor(() => errors().some(item => /undefined variable/.test(item.message)), 'live diagnostics');
    assert.equal(errors()[0].range.start.line, 3);
    assert.equal(errors()[0].range.start.character, 6);
    await replace(document, `${prefix}print(double(amount));`);
    await waitFor(() => errors().length === 0, 'diagnostics clear after editing');
    const hovers = await vscode.commands.executeCommand('vscode.executeHoverProvider', uri, new vscode.Position(3, 15));
    assert.ok(hovers.some(hover => hover.contents.some(value => /int amount/.test((typeof value === 'string' ? value : value.value).replace(/&nbsp;/g, ' ')))), JSON.stringify(hovers));

    const vectorPrefix = 'int[][] grid(2, 3);\ngrid[0].';
    await replace(document, vectorPrefix);
    const methods = await waitFor(async () => {
        const list = await vscode.commands.executeCommand('vscode.executeCompletionItemProvider', uri, new vscode.Position(1, 8), '.');
        return list?.items.find(item => item.label === 'push') && list;
    }, 'vector receiver completion');
    assert.match(methods.items.find(item => item.label === 'push').detail, /value: int\)/);
    await replace(document, 'int[][] grid(2,3); grid[0].push(7); while(1){break;}');
    await waitFor(() => errors().length === 0, 'typed vectors and break accepted');
    await replace(document, 'int[][] grid; grid.push(1);');
    await waitFor(() => errors().some(item => /array expression/.test(item.message)), 'vector dimension error');
    await replace(document, 'break;');
    await waitFor(() => errors().some(item => /break is only allowed/.test(item.message)), 'break outside a loop');

    // A dirty file must compile its buffer, rather than the saved print(21).
    await replace(document, 'int[] a; a.push(987654321); print(a);\n');
    assert.equal(document.isDirty, true);
    const generated = await vscode.commands.executeCommand('fkenCpl.convertToCpp');
    assert.equal(generated.languageId, 'cpp');
    assert.equal(generated.isUntitled, true);
    assert.match(generated.getText(), /987654321/);
    assert.match(generated.getText(), /int main\(\)/);
    assert.equal(document.getText(), 'int[] a; a.push(987654321); print(a);\n');

    await vscode.commands.executeCommand('fkenCpl.restartLanguageServer');
    await vscode.window.showTextDocument(document);
    await replace(document, 'print(missing_again);');
    await waitFor(() => errors().some(item => /missing_again/.test(item.message)), 'diagnostics after server restart');
    const untitled = await vscode.workspace.openTextDocument({ language: 'fken_cpl', content: 'print(123456789);' });
    await vscode.window.showTextDocument(untitled);
    const fromUntitled = await vscode.commands.executeCommand('fkenCpl.convertToCpp');
    assert.match(fromUntitled.getText(), /123456789/);
    if (process.env.FKEN_TEST_RESULT) await fs.writeFile(process.env.FKEN_TEST_RESULT, 'passed');
    console.log('PASS: file association, activation, completion, UTF-16 diagnostics, hover, typed vectors, method completion, break, dirty/untitled C++ conversion, restart.');
};
