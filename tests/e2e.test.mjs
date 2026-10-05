// End-to-end tests for the Spin app. Target with TEST_TARGET=local|aka, or set BASE_URL directly.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { before, describe, test } from "node:test";

const TIMEOUT_MS = 120_000;

function resolveBaseUrl() {
    if (process.env.BASE_URL) {
        return process.env.BASE_URL.replace(/\/$/, "");
    }
    const target = process.env.TEST_TARGET ?? "local";
    if (target === "local") {
        return "http://127.0.0.1:3000";
    }
    if (target === "aka") {
        const status = execFileSync("spin", ["aka", "app", "status"], { encoding: "utf8", timeout: 60_000 });
        const url = status.match(/https:\/\/\S+\.fwf\.app/)?.[0];
        if (!url) {
            throw new Error(`Could not find the aka app URL in:\n${status}`);
        }
        return url;
    }
    throw new Error(`Unknown TEST_TARGET "${target}" (expected local or aka)`);
}

const baseUrl = resolveBaseUrl();

async function get(path, init) {
    const response = await fetch(`${baseUrl}${path}`, { signal: AbortSignal.timeout(TIMEOUT_MS), ...init });
    return { status: response.status, contentType: response.headers.get("content-type") ?? "", body: await response.text() };
}

function sqlGet(sql) {
    return get(`/c?${new URLSearchParams({ SQL: sql })}`);
}

function sqlPost(sql) {
    return get("/c", { method: "POST", body: new URLSearchParams({ SQL: sql }) });
}

// Text between "[Result]" and the next section of the /c report.
function resultSection(body) {
    const match = body.match(/\[Result\]\n([\s\S]*?)\n\[Request Info\]/);
    assert.ok(match, `No [Result] section in:\n${body}`);
    return match[1].trim();
}

before(() => console.log(`Testing ${baseUrl}`));

describe("vitepress", () => {
    test("serves the docs home page", async () => {
        const { status, body } = await get("/");
        assert.equal(status, 200);
        assert.match(body, /<!DOCTYPE html>/i);
    });
});

describe("test-rust", () => {
    test("returns a JSON fact", async () => {
        const { status, contentType, body } = await get("/rust");
        assert.equal(status, 200);
        assert.match(contentType, /application\/json/);
        assert.equal(typeof JSON.parse(body).fact, "string");
    });
});

describe("test-ts", () => {
    test("says hello", async () => {
        const { status, body } = await get("/ts");
        assert.equal(status, 200);
        assert.match(body, /hello/i);
    });
});

describe("test-oauth", () => {
    test("serves the login page", async () => {
        const { status, contentType } = await get("/auth");
        assert.equal(status, 200);
        assert.match(contentType, /text\/html/);
    });

    test("rejects /auth/user without a session", async () => {
        const { status, body } = await get("/auth/user");
        assert.equal(status, 401);
        assert.equal(JSON.parse(body).error, "unauthenticated");
    });
});

describe("test-c", () => {
    test("default report lists objects, schema and category counts", async () => {
        const { status, body } = await get("/c");
        assert.equal(status, 200);
        assert.match(body, /DuckDB version: v\d+\.\d+\.\d+/);
        assert.match(body, /\[S3 Metadata\][\s\S]*Objects: [1-9]/);
        assert.match(body, /\[File Schema\][\s\S]*category \| VARCHAR/);
        assert.match(body, /\[Records per Category\]\n(?:\S+ \| \d+\n)+/);
    });

    test("runs SQL from a GET query parameter", async () => {
        const { status, body } = await sqlGet("SELECT 1 + 1 AS two");
        assert.equal(status, 200);
        assert.equal(resultSection(body), "Rows: 1\ntwo\n2");
    });

    test("runs SQL from a POST form body", async () => {
        const { status, body } = await sqlPost("SELECT 'a&b=c+d' AS s, 42 AS n");
        assert.equal(status, 200);
        assert.equal(resultSection(body), "Rows: 1\ns | n\na&b=c+d | 42");
    });

    test("prefers the POST body over the query string", async () => {
        const { body } = await get(`/c?${new URLSearchParams({ SQL: "SELECT 'query' AS src" })}`, {
            method: "POST",
            body: new URLSearchParams({ SQL: "SELECT 'body' AS src" }),
        });
        assert.equal(resultSection(body), "Rows: 1\nsrc\nbody");
    });

    test("ignores SQL in a non-form POST body", async () => {
        const { status, body } = await get("/c", {
            method: "POST",
            headers: { "content-type": "text/plain" },
            body: "SQL=SELECT 1",
        });
        assert.equal(status, 200);
        assert.doesNotMatch(body, /\[SQL\]/);
        assert.match(body, /\[POST data\]\nSQL=SELECT 1/);
    });

    test("reports SQL errors in the response", async () => {
        const { status, body } = await sqlGet("SELEC 1");
        assert.equal(status, 200);
        assert.match(resultSection(body), /Parser Error/);
    });

    test("queries parquet on S3", async () => {
        const { body: report } = await get("/c");
        const file = report.match(/^(s3:\/\/\S+\.parquet)$/m)?.[1];
        assert.ok(file, "No parquet object listed in the default report");
        const { status, body } = await sqlGet(`SELECT COUNT(*) AS n FROM read_parquet('${file}')`);
        assert.equal(status, 200);
        assert.match(resultSection(body), /^Rows: 1\nn\n[1-9]\d*$/);
    });
});
