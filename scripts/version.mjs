import { execFileSync } from "node:child_process"
import { existsSync, readFileSync, writeFileSync } from "node:fs"
import { dirname, resolve } from "node:path"
import { fileURLToPath } from "node:url"

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..")
const packageJsonPath = resolve(root, "package.json")
const packageLockPath = resolve(root, "package-lock.json")
const spinTomlPath = resolve(root, "spin.toml")

const [command = "sync", ...args] = process.argv.slice(2)
const dryRun = args.includes("--dry-run")
const release = args.find((arg) => !arg.startsWith("--")) ?? "patch"

function readJson(path) {
    return JSON.parse(readFileSync(path, "utf-8"))
}

function writeJson(path, value) {
    writeFileSync(path, `${JSON.stringify(value, null, 4)}\n`)
}

function updatePackageLock(version) {
    if (!existsSync(packageLockPath)) {
        return
    }

    const packageLock = readJson(packageLockPath)
    packageLock.version = version

    if (packageLock.packages?.[""]) {
        packageLock.packages[""].version = version
    }

    if (!dryRun) {
        writeJson(packageLockPath, packageLock)
    }
}

function updateSpinToml(version) {
    const manifest = readFileSync(spinTomlPath, "utf-8")
    const applicationSectionPattern = /(^\[application\]\s*$)([\s\S]*?)(?=^\[|(?![\s\S]))/m
    const match = manifest.match(applicationSectionPattern)

    if (!match) {
        throw new Error("Unable to find [application] section in spin.toml")
    }

    const body = match[2]
    if (!/^(\s*version\s*=\s*)"[^"]+"(\s*)$/m.test(body)) {
        throw new Error("Unable to update [application].version in spin.toml")
    }

    const nextBody = body.replace(/^(\s*version\s*=\s*)"[^"]+"(\s*)$/m, `$1"${version}"$2`)

    if (!dryRun && body !== nextBody) {
        writeFileSync(spinTomlPath, manifest.replace(applicationSectionPattern, `${match[1]}${nextBody}`))
    }
}

function syncVersion(version) {
    updatePackageLock(version)
    updateSpinToml(version)
}

function git(args) {
    return execFileSync("git", args, {
        cwd: root,
        encoding: "utf-8",
        stdio: ["ignore", "pipe", "ignore"]
    }).trim()
}

function tagExists(tag) {
    try {
        git(["rev-parse", "--verify", "--quiet", `refs/tags/${tag}`])
        return true
    } catch {
        return false
    }
}

function historyRange(version) {
    for (const tag of [`v${version}`, version]) {
        if (tagExists(tag)) {
            return `${tag}..HEAD`
        }
    }

    try {
        return `${git(["log", "-n", "1", "--format=%H", "-G", "\"version\":", "--", "package.json"])}..HEAD`
    } catch {
        // Fall through to a semver tag or the full history.
    }

    try {
        return `${git(["describe", "--tags", "--match", "v[0-9]*", "--match", "[0-9]*", "--abbrev=0"])}..HEAD`
    } catch {
        return ""
    }
}

function commitMessagesSince(version) {
    const range = historyRange(version)
    const args = ["log", "--format=%B%x1e"]

    if (range) {
        args.push(range)
    }

    try {
        return git(args).split("\x1e").map((message) => message.trim()).filter(Boolean)
    } catch {
        return []
    }
}

function releaseFromHistory(version) {
    let inferredRelease = null

    for (const message of commitMessagesSince(version)) {
        const subject = message.split("\n", 1)[0]

        if (/^[a-z]+(?:\([^)]+\))?!:/.test(subject) || /^BREAKING CHANGE:/m.test(message)) {
            return "major"
        }

        if (/^feat(?:\([^)]+\))?:/.test(subject)) {
            inferredRelease = inferredRelease === "minor" ? inferredRelease : "minor"
        } else if (/^(fix|perf)(?:\([^)]+\))?:/.test(subject) && inferredRelease !== "minor") {
            inferredRelease = "patch"
        } else if (!/^chore\(release\):/.test(subject) && !inferredRelease) {
            inferredRelease = "patch"
        }
    }

    return inferredRelease
}

function bumpVersion(version, releaseType) {
    const match = version.match(/^(\d+)\.(\d+)\.(\d+)$/)

    if (!match) {
        throw new Error(`Unsupported package version "${version}". Expected major.minor.patch.`)
    }

    let major = Number(match[1])
    let minor = Number(match[2])
    let patch = Number(match[3])

    switch (releaseType) {
        case "major":
            major += 1
            minor = 0
            patch = 0
            break
        case "minor":
            minor += 1
            patch = 0
            break
        case "patch":
            patch += 1
            break
        default:
            throw new Error(`Unsupported release type "${releaseType}". Use patch, minor, or major.`)
    }

    return `${major}.${minor}.${patch}`
}

function writePackageVersion(version) {
    const packageJson = readJson(packageJsonPath)
    packageJson.version = version

    if (!dryRun) {
        writeJson(packageJsonPath, packageJson)
    }
}

const packageJson = readJson(packageJsonPath)

if (command === "sync") {
    syncVersion(packageJson.version)
    console.log(`Synced spin.toml and package-lock.json to ${packageJson.version}`)
} else if (command === "bump") {
    const nextVersion = bumpVersion(packageJson.version, release)
    writePackageVersion(nextVersion)
    syncVersion(nextVersion)
    console.log(`${dryRun ? "Would bump" : "Bumped"} package version to ${nextVersion}`)
} else if (command === "bump-from-history") {
    const inferredRelease = releaseFromHistory(packageJson.version)

    if (!inferredRelease) {
        console.log("No commits found for version bump; package version unchanged")
    } else {
        const nextVersion = bumpVersion(packageJson.version, inferredRelease)
        writePackageVersion(nextVersion)
        syncVersion(nextVersion)
        console.log(`${dryRun ? "Would bump" : "Bumped"} ${inferredRelease} version to ${nextVersion}`)
    }
} else {
    throw new Error(`Unknown version command "${command}"`)
}