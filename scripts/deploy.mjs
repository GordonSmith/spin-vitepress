#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const targets = ["cloud", "aka"];
const [target, ...extraArgs] = process.argv.slice(2);
if (!targets.includes(target)) {
    console.error(`Usage: deploy.mjs <${targets.join("|")}> [spin deploy args...]`);
    process.exit(1);
}

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const envFile = resolve(root, process.env.SPIN_ENV_FILE ?? ".env");

if (!existsSync(envFile)) {
    console.error(`Missing env file: ${envFile}`);
    process.exit(1);
}
process.loadEnvFile(envFile);

const variables = Object.entries(process.env)
    .filter(([key, value]) => key.startsWith("SPIN_VARIABLE_") && value)
    .flatMap(([key, value]) => ["--variable", `${key.slice("SPIN_VARIABLE_".length).toLowerCase()}=${value}`]);

const args = [target, "deploy", ...variables, ...extraArgs];
const { status, error } = spawnSync("spin", args, { cwd: root, stdio: "inherit", shell: process.platform === "win32" });

if (error) {
    console.error(error.message);
    process.exit(1);
}
process.exit(status ?? 1);
