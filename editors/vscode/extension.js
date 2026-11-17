const vscode = require('vscode');
const path = require('node:path');
const { LanguageClient } = require('vscode-languageclient/node');

let client;
let pending = Promise.resolve();
let output;

function activate(context) {
    output = vscode.window.createOutputChannel('fken_cpl', { log: true });
    context.subscriptions.push(output);

    function startServer() {
        const config = vscode.workspace.getConfiguration('fkenCpl');
        const python = config.get('pythonPath') || (process.platform === 'win32' ? 'python' : 'python3');
        const server = config.get('serverPath') || context.asAbsolutePath('server/lsp_server.py');
        if (!path.isAbsolute(server)) {
            throw new Error('fkenCpl.serverPath には絶対パスを指定してください。');
        }
        const env = {
            ...process.env,
            PYTHONUTF8: '1',
            PYTHONUNBUFFERED: '1',
            FKEN_BUILD_DIR: path.join(context.globalStorageUri.fsPath, 'compiler'),
        };
        if (config.get('cCompilerPath')) env.FKEN_CC_PATH = config.get('cCompilerPath');
        client = new LanguageClient('fkenCpl', 'fken_cpl', {
            command: python, args: [server], options: { env },
        }, {
            documentSelector: [
                { scheme: 'file', language: 'fken_cpl' },
                { scheme: 'untitled', language: 'fken_cpl' },
            ],
            outputChannel: output,
        });
        return client.start();
    }

    function restart() {
        pending = pending.catch(() => {}).then(async () => {
            if (client) await client.dispose();
            await startServer();
        });
        return pending;
    }

    async function reportFailure(error) {
        output.appendLine(String(error.stack || error));
        const action = await vscode.window.showErrorMessage(
            `fken_cpl: ${error.message} Python 3 と C コンパイラの設定を確認してください。`, 'ログを表示');
        if (action) output.show();
    }

    context.subscriptions.push(
        vscode.commands.registerCommand('fkenCpl.showOutput', () => output.show()),
        vscode.commands.registerCommand('fkenCpl.restartLanguageServer', async () => {
            try { await restart(); } catch (error) { await reportFailure(error); }
        }),
        vscode.commands.registerCommand('fkenCpl.convertToCpp', async () => {
            const document = vscode.window.activeTextEditor?.document;
            if (!document || document.languageId !== 'fken_cpl') return;
            const version = document.version;
            const source = document.getText();
            try {
                await pending;
                const result = await vscode.window.withProgress({
                    location: vscode.ProgressLocation.Window, title: 'fken_cpl: C++ に変換中',
                }, () => client.sendRequest('fken_cpl/compile', { source, version }));
                if (document.isClosed || document.version !== result.version) {
                    vscode.window.showInformationMessage('ソースが変更されたため、もう一度 C++ に変換してください。');
                    return;
                }
                if (result.diagnostics.length) {
                    vscode.window.showErrorMessage(`fken_cpl: ${result.diagnostics[0].message}`);
                    return;
                }
                const generated = await vscode.workspace.openTextDocument({ language: 'cpp', content: result.cpp });
                await vscode.window.showTextDocument(generated, { viewColumn: vscode.ViewColumn.Beside });
                return generated;
            } catch (error) { await reportFailure(error); }
        }),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (['pythonPath', 'cCompilerPath', 'serverPath'].some(key => event.affectsConfiguration(`fkenCpl.${key}`))) {
                restart().catch(reportFailure);
            }
        }),
    );
    // Keep commands available after a failed start so settings + restart can recover.
    const ready = restart();
    ready.catch(reportFailure);
    return { ready };
}

async function deactivate() {
    await pending.catch(() => {});
    if (client) await client.dispose();
}

module.exports = { activate, deactivate };
