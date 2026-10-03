export interface User {
    login: string;
    name: string | null;
    avatar_url: string | null;
    html_url: string | null;
    more?: { bio?: string | null } | null;
}

// "unavailable" covers the docs dev server and deployments without OAuth configured.
export type Status = "loading" | "signed-in" | "signed-out" | "unavailable";

export const LOGIN_BASE = "/auth";

export async function fetchUser(): Promise<{ status: Exclude<Status, "loading">; user: User | null }> {
    try {
        const res = await fetch(`${LOGIN_BASE}/user`, {
            credentials: "same-origin",
            headers: { accept: "application/json" },
            cache: "no-store",
        });
        if (res.ok && res.headers.get("content-type")?.includes("application/json")) {
            return { status: "signed-in", user: await res.json() };
        }
        return { status: res.status === 401 ? "signed-out" : "unavailable", user: null };
    } catch {
        return { status: "unavailable", user: null };
    }
}
