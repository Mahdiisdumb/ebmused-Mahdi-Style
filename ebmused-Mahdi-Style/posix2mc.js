const fs = require('fs');
const path = require('path');

// Big POSIX database for reporting
const posixDB = {
    // File & directory
    stat: "_stat",
    fstat: "_fstat",
    lstat: "_stat",
    mkdir: "_mkdir",
    rmdir: "RemoveDirectory / _rmdir",
    unlink: "_unlink",
    open: "_open",
    close: "_close",
    read: "_read",
    write: "_write",
    lseek: "_lseek",
    rename: "_rename",
    access: "_access",
    chmod: "_chmod",
    chown: "No direct equivalent",
    symlink: "No direct equivalent",
    link: "_link",
    opendir: "_opendir or FindFirstFile",
    readdir: "_readdir or FindNextFile",
    closedir: "_closedir or FindClose",

    // Process & signals
    fork: "No direct equivalent",
    execve: "_execve / CreateProcess",
    getpid: "_getpid",
    kill: "No direct equivalent",
    signal: "signal / SetConsoleCtrlHandler",

    // Sleep & time
    sleep: "Sleep(seconds*1000)",
    usleep: "Sleep(microseconds/1000)",
    nanosleep: "Sleep(milliseconds)",

    // Memory & misc
    mmap: "VirtualAlloc",
    munmap: "VirtualFree",
    getenv: "_getenv",
    setenv: "_putenv",
    getenv_s: "_dupenv_s",
    getenv_r: "No direct equivalent",
    perror: "perror",
    printf: "printf",
    snprintf: "_snprintf",
    vsnprintf: "_vsnprintf",
    strdup: "_strdup",
    strcasecmp: "_stricmp",
    strncasecmp: "_strnicmp",
    getline: "fgets / custom wrapper",
};

// Detect any function usage
function detectFunctionUsage(content, funcName) {
    const regex = new RegExp(`\\b${funcName}\\s*\\(([^)]*)\\)`, 'g');
    let matches = [];
    let m;
    while ((m = regex.exec(content)) !== null) {
        matches.push(m[1].trim());
    }
    return matches;
}

// Detect wrapper functions
function detectWrappers(content) {
    const wrapperRegex = /(\w+)\s*\(([^)]*)\)\s*{[^}]*?(\w+)\s*\(/g;
    let wrappers = [];
    let m;
    while ((m = wrapperRegex.exec(content)) !== null) {
        const wrapperName = m[1];
        const wrapperCall = m[3];
        if (posixDB[wrapperCall]) {
            wrappers.push({ wrapperName, calls: [wrapperCall] });
        }
    }
    return wrappers;
}

// Scan single file
function scanFile(filePath) {
    const content = fs.readFileSync(filePath, 'utf-8');
    let report = '';

    for (const func in posixDB) {
        const calls = detectFunctionUsage(content, func);
        if (calls.length) {
            for (const call of calls) {
                report += `Function: ${func}(${call}) → MSVC: ${posixDB[func]}\n`;
            }
        }
    }

    const wrappers = detectWrappers(content);
    for (const w of wrappers) {
        report += `Wrapper detected: ${w.wrapperName} calls POSIX: ${w.calls.join(", ")}\n`;
    }

    return report;
}

// Recursively scan folder
function scanFolder(folderPath, reportFile = 'posix_full_report.txt') {
    let finalReport = '';

    const files = fs.readdirSync(folderPath);
    for (const file of files) {
        const fullPath = path.join(folderPath, file);
        const stats = fs.statSync(fullPath);

        if (stats.isDirectory()) {
            finalReport += scanFolder(fullPath, reportFile);
        } else if (file.endsWith('.c') || file.endsWith('.cpp') || file.endsWith('.h')) {
            const report = scanFile(fullPath);
            if (report) {
                finalReport += `File: ${fullPath}\n${report}--------------------------------\n\n`;
            }
        }
    }

    fs.writeFileSync(reportFile, finalReport);
    return finalReport;
}

// === Example usage ===
const projectFolder = './src'; // Change to your project folder
scanFolder(projectFolder);
console.log('POSIX scan complete. Report saved to posix_full_report.txt');