<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from "vue";
import { useRoute } from "vitepress";
import { fetchUser, LOGIN_BASE, type Status, type User } from "../auth";

const route = useRoute();
const status = ref<Status>("loading");
const user = ref<User | null>(null);
const open = ref(false);
const root = ref<HTMLElement | null>(null);
const returnTo = ref("/");

const displayName = computed(() => user.value?.name || user.value?.login || "");
const signInHref = computed(() => `${LOGIN_BASE}/login?return_to=${encodeURIComponent(returnTo.value)}`);
const signOutHref = computed(() => `${LOGIN_BASE}/logout?return_to=${encodeURIComponent(returnTo.value)}`);

async function refresh() {
    const result = await fetchUser();
    status.value = result.status;
    user.value = result.user;
}

function updateReturnTo() {
    let { pathname } = window.location;
    // The static fileserver doesn't resolve VitePress clean URLs on a full page load.
    if (!pathname.endsWith("/") && !/\.[a-z0-9]+$/i.test(pathname)) {
        pathname += ".html";
    }
    returnTo.value = pathname + window.location.search + window.location.hash;
}

function onDocumentClick(e: MouseEvent) {
    if (open.value && root.value && !root.value.contains(e.target as Node)) {
        open.value = false;
    }
}

function onKeydown(e: KeyboardEvent) {
    if (e.key === "Escape") open.value = false;
}

function onVisibilityChange() {
    if (document.visibilityState === "visible") refresh();
}

watch(() => route.path, () => {
    open.value = false;
    updateReturnTo();
});

onMounted(() => {
    updateReturnTo();
    refresh();
    window.addEventListener("hashchange", updateReturnTo);
    document.addEventListener("click", onDocumentClick);
    document.addEventListener("keydown", onKeydown);
    document.addEventListener("visibilitychange", onVisibilityChange);
});

onBeforeUnmount(() => {
    window.removeEventListener("hashchange", updateReturnTo);
    document.removeEventListener("click", onDocumentClick);
    document.removeEventListener("keydown", onKeydown);
    document.removeEventListener("visibilitychange", onVisibilityChange);
});
</script>

<template>
    <div v-if="status !== 'unavailable'" ref="root" class="auth-status">
        <span v-if="status === 'loading'" class="auth-placeholder" aria-hidden="true" />

        <a v-else-if="status === 'signed-out'" class="auth-sign-in" :href="signInHref" target="_self"
            title="Sign in with GitHub">
            <svg class="auth-gh" viewBox="0 0 16 16" aria-hidden="true">
                <path fill="currentColor"
                    d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z" />
            </svg>
            <span class="auth-sign-in-label">Sign in</span>
        </a>

        <template v-else-if="status === 'signed-in' && user">
            <button class="auth-avatar-button" type="button" aria-haspopup="menu" :aria-expanded="open"
                :title="`Signed in as @${user.login}`" @click="open = !open">
                <img v-if="user.avatar_url" class="auth-avatar" :src="user.avatar_url" alt="" width="28" height="28">
                <span v-else class="auth-avatar auth-avatar-fallback">{{ user.login.charAt(0).toUpperCase() }}</span>
                <svg class="auth-caret" viewBox="0 0 16 16" aria-hidden="true">
                    <path fill="currentColor"
                        d="M4.427 7.427l3.396 3.396a.25.25 0 00.354 0l3.396-3.396A.25.25 0 0011.396 7H4.604a.25.25 0 00-.177.427z" />
                </svg>
            </button>

            <div v-show="open" class="auth-menu" role="menu">
                <div class="auth-menu-header">
                    <span class="auth-menu-hint">Signed in as</span>
                    <strong class="auth-menu-name">{{ displayName }}</strong>
                    <span v-if="user.name" class="auth-menu-login">@{{ user.login }}</span>
                </div>
                <div class="auth-menu-divider" />
                <a v-if="user.html_url" class="auth-menu-item" role="menuitem" :href="user.html_url" target="_blank"
                    rel="noreferrer">Your GitHub profile</a>
                <a class="auth-menu-item" role="menuitem" href="/account.html">Account</a>
                <div class="auth-menu-divider" />
                <a class="auth-menu-item" role="menuitem" :href="signOutHref" target="_self">Sign out</a>
            </div>
        </template>
    </div>
</template>

<style scoped>
.auth-status {
    position: relative;
    display: flex;
    align-items: center;
    margin-left: 12px;
}

.auth-placeholder {
    display: inline-block;
    width: 28px;
    height: 28px;
    border-radius: 50%;
    background: var(--vp-c-default-soft);
}

.auth-sign-in {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    height: 32px;
    padding: 0 12px;
    border: 1px solid var(--vp-c-brand-1);
    border-radius: 16px;
    color: var(--vp-c-brand-1);
    font-size: 14px;
    font-weight: 500;
    white-space: nowrap;
    transition: color 0.25s, background-color 0.25s, border-color 0.25s;
}

.auth-sign-in:hover {
    background-color: var(--vp-c-brand-1);
    color: var(--vp-c-white);
}

.dark .auth-sign-in:hover {
    color: var(--vp-c-black);
}

.auth-gh {
    width: 16px;
    height: 16px;
}

.auth-avatar-button {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    padding: 2px;
    border-radius: 18px;
    color: var(--vp-c-text-2);
    transition: color 0.25s;
}

.auth-avatar-button:hover,
.auth-avatar-button[aria-expanded="true"] {
    color: var(--vp-c-text-1);
}

.auth-avatar {
    width: 28px;
    height: 28px;
    border-radius: 50%;
    border: 1px solid var(--vp-c-divider);
    object-fit: cover;
}

.auth-avatar-fallback {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: var(--vp-c-brand-soft);
    color: var(--vp-c-brand-1);
    font-size: 13px;
    font-weight: 600;
}

.auth-caret {
    width: 12px;
    height: 12px;
}

.auth-menu {
    position: absolute;
    top: calc(100% + 8px);
    right: 0;
    z-index: 100;
    min-width: 200px;
    padding: 6px 0;
    border: 1px solid var(--vp-c-divider);
    border-radius: 12px;
    background: var(--vp-c-bg-elv);
    box-shadow: var(--vp-shadow-3);
}

.auth-menu-header {
    display: flex;
    flex-direction: column;
    padding: 6px 14px 8px;
    line-height: 1.4;
}

.auth-menu-hint {
    font-size: 12px;
    color: var(--vp-c-text-2);
}

.auth-menu-name {
    font-size: 14px;
    color: var(--vp-c-text-1);
}

.auth-menu-login {
    font-size: 13px;
    color: var(--vp-c-text-2);
}

.auth-menu-divider {
    height: 1px;
    margin: 4px 0;
    background: var(--vp-c-divider);
}

.auth-menu-item {
    display: block;
    padding: 6px 14px;
    font-size: 14px;
    color: var(--vp-c-text-1);
    white-space: nowrap;
    transition: background-color 0.25s, color 0.25s;
}

.auth-menu-item:hover {
    background: var(--vp-c-default-soft);
    color: var(--vp-c-brand-1);
}

@media (max-width: 767px) {
    .auth-sign-in {
        width: 32px;
        padding: 0;
        justify-content: center;
    }

    .auth-sign-in-label {
        display: none;
    }
}
</style>
