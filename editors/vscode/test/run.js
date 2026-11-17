const fs = require('node:fs/promises');
const path = require('node:path');
const { runTests } = require('@vscode/test-electron');

async function main() {
    const extension = path.resolve(__dirname, '..');
    const build = path.resolve(extension, '../../.build');
    await fs.mkdir(build, { recursive: true });
    const work = await fs.mkdtemp(path.join(build, 'vscode-test-'));
    const workspace = path.join(work, 'workspace');
    const resultPath = path.join(work, 'test-result');
    await fs.mkdir(workspace);
    const userSettings = path.join(work, 'user data', 'User');
    await fs.mkdir(userSettings, { recursive: true });
    await fs.writeFile(path.join(userSettings, 'settings.json'), JSON.stringify({
        'chat.disableAIFeatures': true, 'telemetry.telemetryLevel': 'off',
        'workbench.startupEditor': 'none', 'extensions.autoCheckUpdates': false,
    }));
    await fs.writeFile(path.join(workspace, 'main.fken'), 'print(21);\n');
    let passed = false;
    try {
        await runTests({
            extensionDevelopmentPath: process.env.FKEN_TEST_EXTENSION || extension,
            extensionTestsPath: path.join(__dirname, 'suite.js'),
            extensionTestsEnv: { FKEN_TEST_RESULT: resultPath },
            launchArgs: [
                workspace, '--no-sandbox', '--disable-gpu', '--disable-extensions',
                '--skip-welcome', '--skip-release-notes', '--disable-workspace-trust',
                `--user-data-dir=${path.join(work, 'user data')}`,
                `--extensions-dir=${path.join(work, 'extensions')}`,
            ],
        });
        if (await fs.readFile(resultPath, 'utf8').catch(() => '') !== 'passed') {
            throw new Error('VS Code exited before the extension test suite completed.');
        }
        passed = true;
    } finally {
        if (passed) await fs.rm(work, { recursive: true, force: true });
        else console.error(`VS Code test logs: ${path.join(work, 'user data', 'logs')}`);
    }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
